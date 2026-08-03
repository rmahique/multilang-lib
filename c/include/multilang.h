/*
 * multilang — reusable multi-language string storage: ml_retrieve_data,
 * ml_insert_data, ml_connect.
 *
 * Every id-shaped value (language_id, original_language, string_id,
 * context) is normalized to lowercase by default, always — they're all
 * part of the exact-match composite primary key, so casing differences
 * would otherwise split what should be one row into duplicates.
 *
 * All string outputs are written into caller-supplied fixed-size buffers
 * (no hidden allocation for anything id-shaped) except retrieved content,
 * which is malloc'd because its length is unbounded up to
 * ML_MAX_CONTENT_LEN; the caller must free() it. This keeps the ABI
 * simple and avoids surprising allocations for values with a known small
 * upper bound.
 */

#ifndef MULTILANG_H
#define MULTILANG_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ML_MAX_STRING_ID_LEN 200
#define ML_MAX_CONTEXT_LEN 200
#define ML_MAX_CONTENT_LEN 65535
#define ML_MAX_UPDATED_BY_LEN 200
#define ML_MAX_LANGUAGE_ID_LEN 35 /* BCP 47 tags top out around here */
#define ML_MAX_STATUS_LEN 20
#define ML_ERRBUF_LEN 256

typedef enum {
    ML_OK = 0,
    ML_ERR_VALIDATION = 1,
    ML_ERR_DB = 2,
    ML_ERR_ARGS = 3,
    ML_ERR_UNSUPPORTED = 4,
} ml_status;

/* Opaque connection handle returned by ml_connect. */
typedef struct ml_backend ml_backend;

/*
 * Credentials for ml_connect. Any field left empty ("" or 0) falls back
 * to the matching MULTILANG_DB_* environment variable:
 *   MULTILANG_DB_PATH, MULTILANG_DB_HOST, MULTILANG_DB_PORT,
 *   MULTILANG_DB_USER, MULTILANG_DB_PASSWORD, MULTILANG_DB_NAME
 * Credentials are always passed as driver connection parameters, never
 * interpolated into a query string or logged.
 */
typedef struct {
    const char *path;     /* sqlite (file), filesystem (root directory) */
    const char *host;     /* postgres/mysql */
    int port;              /* postgres/mysql; 0 = use backend default */
    const char *user;     /* postgres/mysql */
    const char *password; /* postgres/mysql */
    const char *database; /* postgres/mysql */
    const char *sslmode;  /* postgres/mysql; NULL = "prefer" (use TLS if
                           * offered); "require" forces TLS; "disable"
                           * forces plaintext */
} ml_credentials;

/*
 * Optional fields for ml_insert_data. Zero/NULL/"" means "not set":
 * context defaults to "", original_language/updated_by default to SQL
 * NULL, status defaults to "draft".
 */
typedef struct {
    const char *context;
    const char *original_language;
    const char *status;
    const char *updated_by;
} ml_insert_options;

/*
 * Open a connection to backend ("sqlite" | "postgres" | "mysql" |
 * "filesystem"), ensure the `strings` table (or root directory) exists,
 * and write the handle to *out.
 *
 * Returns ML_OK on success. On failure, returns a nonzero ml_status and
 * writes a human-readable message into errbuf (if non-NULL).
 */
ml_status ml_connect(const char *backend, const ml_credentials *creds,
                      ml_backend **out, char *errbuf, size_t errbuf_len);

/* Close a connection opened by ml_connect. Safe to call with NULL. */
void ml_close(ml_backend *conn);

/*
 * Look up one piece of text by its identity.
 *
 * Every value is validated before it reaches SQL and every query is
 * parameterized — no value here is ever concatenated into a query string.
 *
 * On success (ML_OK), *out_content is set to a malloc'd, NUL-terminated
 * copy of the stored content (caller must free()) if a row was found, or
 * to NULL if no row matches — the data only, no metadata.
 *
 * `context` may be NULL, meaning the un-contextualized row ("").
 */
ml_status ml_retrieve_data(ml_backend *conn, const char *string_id,
                            const char *language_id, const char *context,
                            char **out_content, char *errbuf, size_t errbuf_len);

/*
 * Insert a new row, or update it in place if (string_id, language_id,
 * context) already exists (upsert on the composite primary key).
 *
 * `opts` may be NULL to use every default (context="", no
 * original_language, status="draft", no updated_by).
 *
 * When opts->original_language is given, the current content of the
 * source row (language_id=opts->original_language, same
 * string_id/context) is hashed with SHA-256 and stored as
 * source_checksum, so staleness can be detected later by re-hashing the
 * source and comparing. If the source row doesn't exist yet,
 * source_checksum is left NULL.
 */
ml_status ml_insert_data(ml_backend *conn, const char *string_id,
                          const char *language_id, const char *content,
                          const ml_insert_options *opts, char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* MULTILANG_H */
