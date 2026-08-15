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

/* ml_search_data limits -- see docs/search.md for the rationale behind
 * these specific numbers and the exact/natural/regex semantics. */
#define ML_MAX_SEARCH_QUERY_LEN 500
#define ML_MIN_SEARCH_LIMIT 1
#define ML_MAX_SEARCH_LIMIT 500
#define ML_DEFAULT_SEARCH_LIMIT 50

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

/* Which of ml_search_data's three matching algorithms to use — see
 * docs/search.md. */
typedef enum {
    ML_SEARCH_NATURAL = 0,
    ML_SEARCH_EXACT = 1,
    ML_SEARCH_REGEX = 2,
} ml_search_mode;

/*
 * Optional fields for ml_search_data. Zero/NULL means "not set":
 * language_id/status NULL = no filter, context NULL = no filter (a
 * non-NULL pointer to "" filters for only the default/un-contextualized
 * row -- "" can't double as both "no filter" and "a real filter value"
 * the way it can for language_id/status, which are never valid as ""),
 * case_sensitive 0 (case-insensitive), limit 0 meaning the default
 * (ML_DEFAULT_SEARCH_LIMIT), offset 0.
 */
typedef struct {
    const char *language_id;
    const char *context;
    const char *status;
    int case_sensitive;
    int limit;
    int offset;
} ml_search_options;

/* One row as returned by ml_search_data — owned copies (malloc'd),
 * released via ml_free_search_results. NULL means SQL NULL for the
 * nullable fields (original_language, source_checksum, updated_by). */
typedef struct {
    char *string_id;
    char *language_id;
    char *context;
    char *content;
    char *original_language;
    char *status;
    char *source_checksum;
    char *updated_by;
    time_t date_updated;
} ml_search_result;

/*
 * Search content across every row matching opts' optional filters.
 *
 * Matching runs entirely in-process, after fetching candidate rows from
 * the backend filtered only by the cheap exact-match columns
 * (language_id/context/status) — this is what guarantees identical
 * search results across SQLite/Postgres/MySQL/filesystem: the matching
 * logic never touches backend-specific SQL/FTS engines. See
 * docs/search.md for the full rationale and the documented
 * cross-language regex-flavor/case-folding limitations.
 *
 * ML_SEARCH_EXACT: query is a literal substring of content.
 * ML_SEARCH_NATURAL: query is split on whitespace into terms, every one
 *   of which must appear as a substring of content -- AND, not OR.
 * ML_SEARCH_REGEX: query is a POSIX extended regular expression (see
 *   regex(7)) searched against content.
 *
 * `opts` may be NULL to use every default (see ml_search_options).
 *
 * On success (ML_OK), *out_results is set to a malloc'd array of
 * *out_count rows (possibly 0), ordered by match score descending, then
 * (language_id, string_id, context) ascending as a deterministic
 * tiebreak. The caller must free it with ml_free_search_results.
 */
ml_status ml_search_data(ml_backend *conn, const char *query, ml_search_mode mode,
                          const ml_search_options *opts,
                          ml_search_result **out_results, size_t *out_count,
                          char *errbuf, size_t errbuf_len);

/* Frees an array returned by ml_search_data. Safe to call with results=NULL. */
void ml_free_search_results(ml_search_result *results, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MULTILANG_H */
