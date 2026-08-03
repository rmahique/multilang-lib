/*
 * Filesystem backend — no server, no driver, just files. Useful when the
 * translation set is meant to be human-editable and diffable in version
 * control rather than queried through a database.
 *
 * Layout (see ../../docs/connectors.md#the-filesystem-backend):
 *
 *   <root>/<language_id>/<string_id>/<context>/content.json
 *   <root>/<language_id>/<string_id>/@default/content.json  (context == "")
 *
 * The leaf file is always named "content.json" -- the row's data never
 * becomes part of a filename, only directory names (language_id,
 * string_id, context) do.
 *
 * The JSON here is hand-rolled rather than a general parser: this file
 * is the only writer of content.json, so select_content only ever needs
 * to find and unescape the one "content" field it wrote, not parse
 * arbitrary JSON. "content" is always written first, so the first
 * unescaped `"content"` byte sequence in the file is guaranteed to be
 * the real key (any literal occurrence inside the value itself would
 * have its quotes escaped as \" by json_write_escaped, not appear as a
 * bare `"content"`).
 */

#include "backend.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FS_DEFAULT_CONTEXT_DIR "@default"
#define FS_CONTENT_FILENAME "content.json"

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *msg)
{
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
}

/* Creates every missing directory component of `path` (like `mkdir -p`). */
static int mkdir_p(const char *path)
{
    char *copy = strdup(path);
    if (copy == NULL) {
        return -1;
    }
    size_t len = strlen(copy);
    int rc = 0;

    for (size_t i = 1; i <= len; i++) {
        if (copy[i] != '/' && copy[i] != '\0') {
            continue;
        }
        char saved = copy[i];
        copy[i] = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            rc = -1;
            copy[i] = saved;
            break;
        }
        copy[i] = saved;
    }
    free(copy);
    return rc;
}

/* Recursively removes everything under (and including) `path`. */
static int remove_recursive(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    struct dirent *entry;
    int rc = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        size_t child_len = strlen(path) + 1 + strlen(entry->d_name) + 1;
        char *child = malloc(child_len);
        snprintf(child, child_len, "%s/%s", path, entry->d_name);
        if (remove_recursive(child) != 0) {
            rc = -1;
        }
        free(child);
    }
    closedir(dir);
    if (rc == 0) {
        rc = rmdir(path);
    }
    return rc;
}

/*
 * Builds "<root>/<language_id>/<string_id>/<context_dir>" and refuses to
 * return anything outside root. string_id/context allow "." and "-", so
 * a value like ".." is a valid identifier (see validation.c) but a
 * directory-traversal payload as a path segment. Checking containment
 * here is defense-in-depth, same spirit as every other backend
 * parameterizing its queries instead of trusting input shape alone --
 * neither can actually appear here since validation.c already rejects
 * "." and ".." outright and string_id/context can't contain "/".
 *
 * Returns a malloc'd string the caller must free, or NULL on allocation
 * failure.
 */
static char *dir_for(const char *root, const char *language_id, const char *string_id, const char *context)
{
    const char *context_dir = (context == NULL || context[0] == '\0') ? FS_DEFAULT_CONTEXT_DIR : context;

    size_t needed = strlen(root) + 1 + strlen(language_id) + 1 + strlen(string_id) + 1 + strlen(context_dir) + 1;
    char *dir = malloc(needed);
    if (dir == NULL) {
        return NULL;
    }
    snprintf(dir, needed, "%s/%s/%s/%s", root, language_id, string_id, context_dir);

    size_t root_len = strlen(root);
    if (strncmp(dir, root, root_len) != 0 || (dir[root_len] != '/' && dir[root_len] != '\0')) {
        free(dir);
        return NULL;
    }
    return dir;
}

/* Appends `s`, JSON-escaped, to the dynamic buffer (buf, len, cap). */
static int json_write_escaped(char **buf, size_t *len, size_t *cap, const char *s)
{
    size_t need = *len + strlen(s) * 6 + 3; /* worst case: every byte becomes \u00XX */
    if (need > *cap) {
        size_t new_cap = need * 2;
        char *grown = realloc(*buf, new_cap);
        if (grown == NULL) {
            return -1;
        }
        *buf = grown;
        *cap = new_cap;
    }

    char *out = *buf + *len;
    *out++ = '"';
    for (const unsigned char *p = (const unsigned char *) s; *p != '\0'; p++) {
        switch (*p) {
            case '"': *out++ = '\\'; *out++ = '"'; break;
            case '\\': *out++ = '\\'; *out++ = '\\'; break;
            case '\n': *out++ = '\\'; *out++ = 'n'; break;
            case '\r': *out++ = '\\'; *out++ = 'r'; break;
            case '\t': *out++ = '\\'; *out++ = 't'; break;
            default:
                if (*p < 0x20) {
                    out += sprintf(out, "\\u%04x", *p);
                } else {
                    *out++ = (char) *p;
                }
        }
    }
    *out++ = '"';
    *len = (size_t) (out - *buf);
    return 0;
}

/* Appends `s` (a JSON literal: null or an already-quoted string) as-is. */
static int json_write_raw(char **buf, size_t *len, size_t *cap, const char *s)
{
    size_t need = *len + strlen(s) + 1;
    if (need > *cap) {
        size_t new_cap = need * 2;
        char *grown = realloc(*buf, new_cap);
        if (grown == NULL) {
            return -1;
        }
        *buf = grown;
        *cap = new_cap;
    }
    memcpy(*buf + *len, s, strlen(s));
    *len += strlen(s);
    return 0;
}

static ml_status fs_ensure_schema(void *ctx, char *errbuf, size_t errbuf_len)
{
    const char *root = (const char *) ctx;
    if (mkdir_p(root) != 0) {
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to create root directory");
        return ML_ERR_DB;
    }
    return ML_OK;
}

static ml_status fs_select_content(void *ctx, const char *string_id, const char *language_id,
                                    const char *context, char **out_content, int *out_found,
                                    char *errbuf, size_t errbuf_len)
{
    const char *root = (const char *) ctx;
    *out_found = 0;

    char *dir = dir_for(root, language_id, string_id, context);
    if (dir == NULL) {
        set_errbuf(errbuf, errbuf_len, "filesystem: refusing to access path outside backend root");
        return ML_ERR_DB;
    }

    size_t path_len = strlen(dir) + 1 + strlen(FS_CONTENT_FILENAME) + 1;
    char *path = malloc(path_len);
    snprintf(path, path_len, "%s/%s", dir, FS_CONTENT_FILENAME);
    free(dir);

    FILE *f = fopen(path, "rb");
    free(path);
    if (f == NULL) {
        return ML_OK; /* not found -- *out_found stays 0 */
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t) size + 1);
    size_t read = fread(data, 1, (size_t) size, f);
    data[read] = '\0';
    fclose(f);

    const char *key = strstr(data, "\"content\"");
    if (key == NULL) {
        free(data);
        set_errbuf(errbuf, errbuf_len, "filesystem: content.json missing 'content' field");
        return ML_ERR_DB;
    }
    const char *quote = strchr(key + strlen("\"content\""), '"');
    if (quote == NULL) {
        free(data);
        set_errbuf(errbuf, errbuf_len, "filesystem: content.json has malformed 'content' field");
        return ML_ERR_DB;
    }
    quote++; /* past the opening quote of the value */

    char *unescaped = malloc(strlen(quote) + 1);
    char *out = unescaped;
    const char *p = quote;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p++;
            switch (*p) {
                case 'n': *out++ = '\n'; break;
                case 'r': *out++ = '\r'; break;
                case 't': *out++ = '\t'; break;
                case '"': *out++ = '"'; break;
                case '\\': *out++ = '\\'; break;
                case '/': *out++ = '/'; break;
                case 'u': {
                    unsigned int code = 0;
                    sscanf(p + 1, "%4x", &code);
                    *out++ = (char) code;
                    p += 4;
                    break;
                }
                default: *out++ = *p; break;
            }
            p++;
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';
    free(data);

    *out_content = unescaped;
    *out_found = 1;
    return ML_OK;
}

static ml_status fs_upsert(void *ctx, const ml_row *row, char *errbuf, size_t errbuf_len)
{
    const char *root = (const char *) ctx;

    char *dir = dir_for(root, row->language_id, row->string_id, row->context);
    if (dir == NULL) {
        set_errbuf(errbuf, errbuf_len, "filesystem: refusing to access path outside backend root");
        return ML_ERR_DB;
    }
    if (mkdir_p(dir) != 0) {
        free(dir);
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to create row directory");
        return ML_ERR_DB;
    }

    struct tm tm_utc;
    gmtime_r(&row->date_updated, &tm_utc);
    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    char *buf = malloc(256);
    size_t len = 0;
    size_t cap = 256;

    json_write_raw(&buf, &len, &cap, "{\n  \"content\": ");
    json_write_escaped(&buf, &len, &cap, row->content);
    json_write_raw(&buf, &len, &cap, ",\n  \"original_language\": ");
    if (row->original_language != NULL) {
        json_write_escaped(&buf, &len, &cap, row->original_language);
    } else {
        json_write_raw(&buf, &len, &cap, "null");
    }
    json_write_raw(&buf, &len, &cap, ",\n  \"status\": ");
    json_write_escaped(&buf, &len, &cap, row->status);
    json_write_raw(&buf, &len, &cap, ",\n  \"source_checksum\": ");
    if (row->source_checksum != NULL) {
        json_write_escaped(&buf, &len, &cap, row->source_checksum);
    } else {
        json_write_raw(&buf, &len, &cap, "null");
    }
    json_write_raw(&buf, &len, &cap, ",\n  \"updated_by\": ");
    if (row->updated_by != NULL) {
        json_write_escaped(&buf, &len, &cap, row->updated_by);
    } else {
        json_write_raw(&buf, &len, &cap, "null");
    }
    json_write_raw(&buf, &len, &cap, ",\n  \"date_updated\": ");
    json_write_escaped(&buf, &len, &cap, date_buf);
    json_write_raw(&buf, &len, &cap, "\n}\n");

    size_t path_len = strlen(dir) + 1 + strlen(FS_CONTENT_FILENAME) + 1;
    char *path = malloc(path_len);
    snprintf(path, path_len, "%s/%s", dir, FS_CONTENT_FILENAME);
    char *tmp_path = malloc(path_len + 4);
    snprintf(tmp_path, path_len + 4, "%s.tmp", path);
    free(dir);

    FILE *f = fopen(tmp_path, "wb");
    if (f == NULL) {
        free(buf);
        free(path);
        free(tmp_path);
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to open temp file for writing");
        return ML_ERR_DB;
    }
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);

    /* Atomic on POSIX for paths on the same filesystem, so a concurrent
     * reader never sees a partially written file. */
    ml_status status = ML_OK;
    if (rename(tmp_path, path) != 0) {
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to rename temp file into place");
        status = ML_ERR_DB;
    }
    free(path);
    free(tmp_path);
    return status;
}

static ml_status fs_truncate(void *ctx, char *errbuf, size_t errbuf_len)
{
    const char *root = (const char *) ctx;
    /* Remove everything under root, then recreate root itself -- the
     * closest filesystem equivalent of `DELETE FROM strings`. */
    if (remove_recursive(root) != 0) {
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to truncate root directory");
        return ML_ERR_DB;
    }
    if (mkdir_p(root) != 0) {
        set_errbuf(errbuf, errbuf_len, "filesystem: failed to recreate root directory");
        return ML_ERR_DB;
    }
    return ML_OK;
}

static void fs_close(void *ctx)
{
    free(ctx); /* the root path string -- see ml_filesystem_backend_open */
}

static const ml_backend_vtable FILESYSTEM_VTABLE = {
    .ensure_schema = fs_ensure_schema,
    .select_content = fs_select_content,
    .upsert = fs_upsert,
    .truncate = fs_truncate,
    .close = fs_close,
};

ml_status ml_filesystem_backend_open(const char *root, ml_backend **out, char *errbuf, size_t errbuf_len)
{
    (void) errbuf;
    (void) errbuf_len;
    char *root_copy = strdup(root);
    if (root_copy == NULL) {
        set_errbuf(errbuf, errbuf_len, "filesystem: out of memory");
        return ML_ERR_DB;
    }
    /* Strip a trailing slash so dir_for()'s containment check (which
     * expects the next byte after `root` to be '/' or '\0') is exact. */
    size_t len = strlen(root_copy);
    if (len > 1 && root_copy[len - 1] == '/') {
        root_copy[len - 1] = '\0';
    }

    ml_backend *conn = malloc(sizeof(ml_backend));
    conn->vtable = &FILESYSTEM_VTABLE;
    conn->ctx = root_copy;
    *out = conn;
    return ML_OK;
}
