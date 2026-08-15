/* SQLite backend — the zero-setup default, and what the test suite uses. */

#include "backend.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS strings ("
    "    language_id       TEXT NOT NULL,"
    "    string_id         TEXT NOT NULL,"
    "    context           TEXT NOT NULL DEFAULT '',"
    "    content           TEXT NOT NULL,"
    "    original_language TEXT,"
    "    status            TEXT NOT NULL DEFAULT 'draft',"
    "    source_checksum   TEXT,"
    "    updated_by        TEXT,"
    "    date_updated      TEXT NOT NULL,"
    "    PRIMARY KEY (language_id, string_id, context)"
    ")";

static const char *UPSERT_SQL =
    "INSERT INTO strings"
    "    (language_id, string_id, context, content, original_language,"
    "     status, source_checksum, updated_by, date_updated)"
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    "ON CONFLICT(language_id, string_id, context) DO UPDATE SET"
    "    content=excluded.content,"
    "    original_language=excluded.original_language,"
    "    status=excluded.status,"
    "    source_checksum=excluded.source_checksum,"
    "    updated_by=excluded.updated_by,"
    "    date_updated=excluded.date_updated";

static const char *SELECT_SQL =
    "SELECT content FROM strings WHERE language_id = ? AND string_id = ? AND context = ?";

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *msg)
{
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
}

static ml_status sqlite_ensure_schema(void *ctx, char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = (sqlite3 *) ctx;
    char *err = NULL;
    if (sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, err ? err : "sqlite: failed to create schema");
        sqlite3_free(err);
        return ML_ERR_DB;
    }
    return ML_OK;
}

static ml_status sqlite_select_content(void *ctx, const char *string_id, const char *language_id,
                                        const char *context, char **out_content, int *out_found,
                                        char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = (sqlite3 *) ctx;
    sqlite3_stmt *stmt = NULL;
    *out_found = 0;

    if (sqlite3_prepare_v2(db, SELECT_SQL, -1, &stmt, NULL) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        return ML_ERR_DB;
    }
    sqlite3_bind_text(stmt, 1, language_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, string_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, context, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        *out_content = text ? strdup((const char *) text) : strdup("");
        *out_found = 1;
    } else if (rc != SQLITE_DONE) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return ML_ERR_DB;
    }
    sqlite3_finalize(stmt);
    return ML_OK;
}

static ml_status sqlite_upsert(void *ctx, const ml_row *row, char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = (sqlite3 *) ctx;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, UPSERT_SQL, -1, &stmt, NULL) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        return ML_ERR_DB;
    }

    /* SQLite has no native timestamp type; store ISO-8601 text, the same
     * UTC instant every other backend's column ultimately represents. */
    char date_buf[32];
    struct tm tm_utc;
    gmtime_r(&row->date_updated, &tm_utc);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    sqlite3_bind_text(stmt, 1, row->language_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row->string_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, row->context, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, row->content, -1, SQLITE_TRANSIENT);
    if (row->original_language)
        sqlite3_bind_text(stmt, 5, row->original_language, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);
    sqlite3_bind_text(stmt, 6, row->status, -1, SQLITE_TRANSIENT);
    if (row->source_checksum)
        sqlite3_bind_text(stmt, 7, row->source_checksum, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 7);
    if (row->updated_by)
        sqlite3_bind_text(stmt, 8, row->updated_by, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 8);
    sqlite3_bind_text(stmt, 9, date_buf, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    ml_status status = ML_OK;
    if (rc != SQLITE_DONE) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        status = ML_ERR_DB;
    }
    sqlite3_finalize(stmt);
    return status;
}

/* Parses "%Y-%m-%dT%H:%M:%SZ" (the exact format sqlite_upsert writes)
 * back into a UTC time_t, without relying on strptime()/timegm() -- both
 * are BSD/glibc extensions not guaranteed visible under this project's
 * strict -D_POSIX_C_SOURCE=200809L build flags (see c/Makefile). The
 * day-count math is Howard Hinnant's days_from_civil algorithm: a
 * standard, allocation-free proleptic-Gregorian date -> days-since-epoch
 * conversion. */
static time_t parse_utc_timestamp(const char *text)
{
    int year, mon, day, hour, min, sec;
    sscanf(text, "%d-%d-%dT%d:%d:%dZ", &year, &mon, &day, &hour, &min, &sec);

    int y = year - (mon <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned) (y - era * 400);
    unsigned doy = (unsigned) ((153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long) doe - 719468L;

    return (time_t) days * 86400 + hour * 3600 + min * 60 + sec;
}

static ml_status sqlite_select_rows(void *ctx, const char *language_id, const char *context,
                                     const char *status, ml_backend_row **out_rows, size_t *out_count,
                                     char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = (sqlite3 *) ctx;

    char sql[512] =
        "SELECT string_id, language_id, context, content, original_language, "
        "status, source_checksum, updated_by, date_updated FROM strings";
    int n = 0;
    if (language_id != NULL) {
        strcat(sql, n++ == 0 ? " WHERE language_id = ?" : " AND language_id = ?");
    }
    if (context != NULL) {
        strcat(sql, n++ == 0 ? " WHERE context = ?" : " AND context = ?");
    }
    if (status != NULL) {
        strcat(sql, n++ == 0 ? " WHERE status = ?" : " AND status = ?");
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        return ML_ERR_DB;
    }
    int idx = 1;
    if (language_id != NULL) sqlite3_bind_text(stmt, idx++, language_id, -1, SQLITE_TRANSIENT);
    if (context != NULL) sqlite3_bind_text(stmt, idx++, context, -1, SQLITE_TRANSIENT);
    if (status != NULL) sqlite3_bind_text(stmt, idx++, status, -1, SQLITE_TRANSIENT);

    size_t capacity = 8, count = 0;
    ml_backend_row *rows = malloc(capacity * sizeof(ml_backend_row));

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(ml_backend_row));
        }
        ml_backend_row *r = &rows[count++];
        r->string_id = strdup((const char *) sqlite3_column_text(stmt, 0));
        r->language_id = strdup((const char *) sqlite3_column_text(stmt, 1));
        r->context = strdup((const char *) sqlite3_column_text(stmt, 2));
        r->content = strdup((const char *) sqlite3_column_text(stmt, 3));
        const unsigned char *ol = sqlite3_column_text(stmt, 4);
        r->original_language = ol ? strdup((const char *) ol) : NULL;
        r->status = strdup((const char *) sqlite3_column_text(stmt, 5));
        const unsigned char *sc = sqlite3_column_text(stmt, 6);
        r->source_checksum = sc ? strdup((const char *) sc) : NULL;
        const unsigned char *ub = sqlite3_column_text(stmt, 7);
        r->updated_by = ub ? strdup((const char *) ub) : NULL;
        r->date_updated = parse_utc_timestamp((const char *) sqlite3_column_text(stmt, 8));
    }

    if (rc != SQLITE_DONE) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        ml_backend_free_rows(rows, count);
        return ML_ERR_DB;
    }
    sqlite3_finalize(stmt);

    *out_rows = rows;
    *out_count = count;
    return ML_OK;
}

static ml_status sqlite_truncate(void *ctx, char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = (sqlite3 *) ctx;
    char *err = NULL;
    /* SQLite has no TRUNCATE statement; DELETE with no WHERE is the
     * equivalent for a table this small. */
    if (sqlite3_exec(db, "DELETE FROM strings", NULL, NULL, &err) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, err ? err : "sqlite: failed to truncate strings");
        sqlite3_free(err);
        return ML_ERR_DB;
    }
    return ML_OK;
}

static void sqlite_close(void *ctx)
{
    sqlite3_close((sqlite3 *) ctx);
}

static const ml_backend_vtable SQLITE_VTABLE = {
    .ensure_schema = sqlite_ensure_schema,
    .select_content = sqlite_select_content,
    .upsert = sqlite_upsert,
    .select_rows = sqlite_select_rows,
    .truncate = sqlite_truncate,
    .close = sqlite_close,
};

ml_status ml_sqlite_backend_open(const char *path, ml_backend **out, char *errbuf, size_t errbuf_len)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        set_errbuf(errbuf, errbuf_len, sqlite3_errmsg(db));
        sqlite3_close(db);
        return ML_ERR_DB;
    }
    sqlite3_exec(db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA busy_timeout = 60000", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

    ml_backend *conn = malloc(sizeof(ml_backend));
    conn->vtable = &SQLITE_VTABLE;
    conn->ctx = db;
    *out = conn;
    return ML_OK;
}
