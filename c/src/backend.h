/*
 * Common interface every backend (SQLite, PostgreSQL, MySQL, filesystem)
 * must implement, via a vtable — C's usual stand-in for an interface.
 * All SQL lives in the backend .c files — callers never see raw SQL or
 * the raw driver connection. Every implementation must use parameterized
 * queries; no value is ever interpolated into a query string. (The
 * filesystem backend has no SQL/driver connection, but implements the
 * same vtable shape for the same reason: callers never see its storage
 * details either.)
 */

#ifndef MULTILANG_BACKEND_H
#define MULTILANG_BACKEND_H

#include <time.h>
#include "../include/multilang.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One insert/upsert's worth of data. NULL means SQL NULL for the
 * nullable fields (original_language, source_checksum, updated_by). */
typedef struct {
    const char *string_id;
    const char *language_id;
    const char *context;
    const char *content;
    const char *original_language;
    const char *status;
    const char *source_checksum;
    const char *updated_by;
    time_t date_updated;
} ml_row;

/* One full row as returned by select_rows -- owned copies (malloc'd),
 * released via ml_backend_free_rows. NULL means SQL NULL for the
 * nullable fields (original_language, source_checksum, updated_by),
 * same convention as ml_row. */
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
} ml_backend_row;

typedef struct {
    ml_status (*ensure_schema)(void *ctx, char *errbuf, size_t errbuf_len);

    /* *out_content is malloc'd and must be freed by the caller if
     * *out_found is set to 1; left untouched if *out_found is 0. */
    ml_status (*select_content)(void *ctx, const char *string_id, const char *language_id,
                                 const char *context, char **out_content, int *out_found,
                                 char *errbuf, size_t errbuf_len);

    ml_status (*upsert)(void *ctx, const ml_row *row, char *errbuf, size_t errbuf_len);

    /* Returns every row matching whichever of language_id/context/status
     * are non-NULL (an omitted, NULL filter matches every value of that
     * column) via *out_rows and *out_count. Caller must free with
     * ml_backend_free_rows. No content matching happens here:
     * ml_search_data does its own in-process regex/natural/exact
     * matching over whatever this returns, which is what keeps search
     * behavior identical across every backend (see docs/search.md). */
    ml_status (*select_rows)(void *ctx, const char *language_id, const char *context,
                              const char *status, ml_backend_row **out_rows, size_t *out_count,
                              char *errbuf, size_t errbuf_len);

    /* Test-only: empties the `strings` table. Not part of the public API
     * (see multilang.h) — used to isolate conformance-suite cases from
     * each other on a shared Postgres/MySQL server, where (unlike
     * SQLite) there's no cheap way to hand each case its own file. */
    ml_status (*truncate)(void *ctx, char *errbuf, size_t errbuf_len);

    void (*close)(void *ctx);
} ml_backend_vtable;

struct ml_backend {
    const ml_backend_vtable *vtable;
    void *ctx;
};

/* Test-only convenience wrapper around vtable->truncate; see above. */
ml_status ml_backend_truncate(ml_backend *conn, char *errbuf, size_t errbuf_len);

/* Frees an array returned by vtable->select_rows. Safe to call with rows=NULL. */
void ml_backend_free_rows(ml_backend_row *rows, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MULTILANG_BACKEND_H */
