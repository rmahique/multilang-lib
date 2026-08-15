/*
 * MySQL/MariaDB backend, via the MariaDB Connector/C (mysql.h),
 * API-compatible with libmysqlclient.
 *
 * Requires MySQL 5.7.9+ or MariaDB 10.2.2+ (innodb_large_prefix on by
 * default, DYNAMIC row format default) — the composite primary key
 * (language_id, string_id, context) in utf8mb4 can exceed the legacy
 * 767-byte InnoDB index-prefix limit on older versions/configurations.
 */

#include "backend.h"
#include "backends_internal.h"

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML_CONTENT_BUF_SIZE (ML_MAX_CONTENT_LEN + 1)

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS strings ("
    "    language_id       VARCHAR(35) NOT NULL,"
    "    string_id         VARCHAR(200) NOT NULL,"
    "    context           VARCHAR(200) NOT NULL DEFAULT '',"
    "    content           MEDIUMTEXT NOT NULL,"
    "    original_language VARCHAR(35),"
    "    status            VARCHAR(20) NOT NULL DEFAULT 'draft',"
    "    source_checksum   VARCHAR(64),"
    "    updated_by        VARCHAR(200),"
    "    date_updated      DATETIME NOT NULL,"
    "    PRIMARY KEY (language_id, string_id, context)"
    ") ENGINE=InnoDB ROW_FORMAT=DYNAMIC DEFAULT CHARSET=utf8mb4";

static const char *UPSERT_SQL =
    "INSERT INTO strings"
    "    (language_id, string_id, context, content, original_language,"
    "     status, source_checksum, updated_by, date_updated)"
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    "ON DUPLICATE KEY UPDATE"
    "    content=VALUES(content),"
    "    original_language=VALUES(original_language),"
    "    status=VALUES(status),"
    "    source_checksum=VALUES(source_checksum),"
    "    updated_by=VALUES(updated_by),"
    "    date_updated=VALUES(date_updated)";

static const char *SELECT_SQL =
    "SELECT content FROM strings WHERE language_id = ? AND string_id = ? AND context = ?";

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *msg)
{
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
}

static ml_status mysql_ensure_schema(void *ctx, char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = (MYSQL *) ctx;
    if (mysql_query(conn, SCHEMA_SQL) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        return ML_ERR_DB;
    }
    return ML_OK;
}

static ml_status mysql_select_content(void *ctx, const char *string_id, const char *language_id,
                                       const char *context, char **out_content, int *out_found,
                                       char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = (MYSQL *) ctx;
    *out_found = 0;

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL || mysql_stmt_prepare(stmt, SELECT_SQL, strlen(SELECT_SQL)) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        if (stmt) mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (void *) language_id;
    params[0].buffer_length = (unsigned long) strlen(language_id);
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (void *) string_id;
    params[1].buffer_length = (unsigned long) strlen(string_id);
    params[2].buffer_type = MYSQL_TYPE_STRING;
    params[2].buffer = (void *) context;
    params[2].buffer_length = (unsigned long) strlen(context);

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    char *buf = malloc(ML_CONTENT_BUF_SIZE);
    unsigned long content_len = 0;
    my_bool is_null = 0;

    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = buf;
    result.buffer_length = ML_CONTENT_BUF_SIZE;
    result.length = &content_len;
    result.is_null = &is_null;

    if (mysql_stmt_bind_result(stmt, &result) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        free(buf);
        mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    int rc = mysql_stmt_fetch(stmt);
    if (rc == 0) {
        buf[content_len < ML_CONTENT_BUF_SIZE ? content_len : ML_CONTENT_BUF_SIZE - 1] = '\0';
        *out_content = buf;
        *out_found = 1;
    } else {
        free(buf);
    }

    mysql_stmt_close(stmt);
    return ML_OK;
}

static ml_status mysql_upsert(void *ctx, const ml_row *row, char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = (MYSQL *) ctx;

    char date_buf[24];
    struct tm tm_utc;
    gmtime_r(&row->date_updated, &tm_utc);
    /* MySQL DATETIME rejects the "T" separator and timezone offset an
     * ISO-8601 string would carry; format plainly. */
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", &tm_utc);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL || mysql_stmt_prepare(stmt, UPSERT_SQL, strlen(UPSERT_SQL)) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        if (stmt) mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    MYSQL_BIND params[9];
    memset(params, 0, sizeof(params));

    const char *values[9] = {
        row->language_id, row->string_id, row->context, row->content,
        row->original_language, row->status, row->source_checksum,
        row->updated_by, date_buf,
    };
    my_bool nulls[9];

    for (int i = 0; i < 9; i++) {
        nulls[i] = (values[i] == NULL) ? 1 : 0;
        params[i].buffer_type = MYSQL_TYPE_STRING;
        params[i].buffer = (void *) values[i];
        params[i].buffer_length = values[i] ? (unsigned long) strlen(values[i]) : 0;
        params[i].is_null = &nulls[i];
    }

    ml_status status = ML_OK;
    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        status = ML_ERR_DB;
    }
    mysql_stmt_close(stmt);
    return status;
}

/* Parses "%Y-%m-%d %H:%M:%S" (the exact format mysql_upsert writes --
 * DATETIME has no timezone, so this project treats it as implicitly
 * UTC) back into a UTC time_t -- see sqlite_backend.c's
 * parse_utc_timestamp for why this doesn't use strptime()/timegm().
 * Duplicated per file rather than shared, matching this project's
 * existing per-backend-file duplication of small helpers like
 * set_errbuf. */
static time_t parse_utc_timestamp(const char *text)
{
    int year, mon, day, hour, min, sec;
    sscanf(text, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec);

    int y = year - (mon <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned) (y - era * 400);
    unsigned doy = (unsigned) ((153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long) doe - 719468L;

    return (time_t) days * 86400 + hour * 3600 + min * 60 + sec;
}

static ml_status mysql_select_rows(void *ctx, const char *language_id, const char *context,
                                    const char *status, ml_backend_row **out_rows, size_t *out_count,
                                    char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = (MYSQL *) ctx;

    char sql[512] =
        "SELECT string_id, language_id, context, content, original_language, "
        "status, source_checksum, updated_by, date_updated FROM strings";
    char clause[256] = "";
    const char *values[3];
    int nparams = 0;
    if (language_id != NULL) {
        strcat(clause, nparams == 0 ? " WHERE language_id = ?" : " AND language_id = ?");
        values[nparams++] = language_id;
    }
    if (context != NULL) {
        strcat(clause, nparams == 0 ? " WHERE context = ?" : " AND context = ?");
        values[nparams++] = context;
    }
    if (status != NULL) {
        strcat(clause, nparams == 0 ? " WHERE status = ?" : " AND status = ?");
        values[nparams++] = status;
    }
    strcat(sql, clause);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        if (stmt) mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));
    for (int i = 0; i < nparams; i++) {
        params[i].buffer_type = MYSQL_TYPE_STRING;
        params[i].buffer = (void *) values[i];
        params[i].buffer_length = (unsigned long) strlen(values[i]);
    }
    if (nparams > 0 && mysql_stmt_bind_param(stmt, params) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    if (mysql_stmt_execute(stmt) != 0 || mysql_stmt_store_result(stmt) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    /* Field max_length is only meaningful after mysql_stmt_store_result()
     * has buffered the full result set -- it then reflects the actual
     * longest value per column, so result buffers can be sized to fit
     * real data instead of each column's worst-case declared width
     * (MEDIUMTEXT's would be enormous). */
    MYSQL_RES *meta = mysql_stmt_result_metadata(stmt);
    MYSQL_FIELD *fields = mysql_fetch_fields(meta);
    unsigned int ncols = mysql_num_fields(meta);

    MYSQL_BIND *results = calloc(ncols, sizeof(MYSQL_BIND));
    char **bufs = malloc(ncols * sizeof(char *));
    unsigned long *lengths = calloc(ncols, sizeof(unsigned long));
    my_bool *is_null = calloc(ncols, sizeof(my_bool));
    for (unsigned int i = 0; i < ncols; i++) {
        unsigned long buf_len = fields[i].max_length > 0 ? fields[i].max_length + 1 : 1;
        bufs[i] = malloc(buf_len);
        results[i].buffer_type = MYSQL_TYPE_STRING;
        results[i].buffer = bufs[i];
        results[i].buffer_length = buf_len;
        results[i].length = &lengths[i];
        results[i].is_null = &is_null[i];
    }
    mysql_free_result(meta);

    if (mysql_stmt_bind_result(stmt, results) != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_stmt_error(stmt));
        for (unsigned int i = 0; i < ncols; i++) free(bufs[i]);
        free(bufs);
        free(results);
        free(lengths);
        free(is_null);
        mysql_stmt_close(stmt);
        return ML_ERR_DB;
    }

    size_t capacity = 8, count = 0;
    ml_backend_row *rows = malloc(capacity * sizeof(ml_backend_row));

    while (mysql_stmt_fetch(stmt) == 0) {
        if (count >= capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(ml_backend_row));
        }
        ml_backend_row *r = &rows[count++];
        bufs[0][lengths[0]] = '\0';
        r->string_id = strdup(bufs[0]);
        bufs[1][lengths[1]] = '\0';
        r->language_id = strdup(bufs[1]);
        bufs[2][lengths[2]] = '\0';
        r->context = strdup(bufs[2]);
        bufs[3][lengths[3]] = '\0';
        r->content = strdup(bufs[3]);
        if (is_null[4]) {
            r->original_language = NULL;
        } else {
            bufs[4][lengths[4]] = '\0';
            r->original_language = strdup(bufs[4]);
        }
        bufs[5][lengths[5]] = '\0';
        r->status = strdup(bufs[5]);
        if (is_null[6]) {
            r->source_checksum = NULL;
        } else {
            bufs[6][lengths[6]] = '\0';
            r->source_checksum = strdup(bufs[6]);
        }
        if (is_null[7]) {
            r->updated_by = NULL;
        } else {
            bufs[7][lengths[7]] = '\0';
            r->updated_by = strdup(bufs[7]);
        }
        bufs[8][lengths[8]] = '\0';
        r->date_updated = parse_utc_timestamp(bufs[8]);
    }

    for (unsigned int i = 0; i < ncols; i++) free(bufs[i]);
    free(bufs);
    free(results);
    free(lengths);
    free(is_null);
    mysql_stmt_close(stmt);

    *out_rows = rows;
    *out_count = count;
    return ML_OK;
}

static ml_status mysql_truncate(void *ctx, char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = (MYSQL *) ctx;
    if (mysql_query(conn, "TRUNCATE TABLE strings") != 0) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        return ML_ERR_DB;
    }
    return ML_OK;
}

static void mysql_close_backend(void *ctx)
{
    mysql_close((MYSQL *) ctx);
}

static const ml_backend_vtable MYSQL_VTABLE = {
    .ensure_schema = mysql_ensure_schema,
    .select_content = mysql_select_content,
    .upsert = mysql_upsert,
    .select_rows = mysql_select_rows,
    .truncate = mysql_truncate,
    .close = mysql_close_backend,
};

ml_status ml_mysql_backend_open(const char *host, int port, const char *user,
                                 const char *password, const char *database,
                                 const char *sslmode,
                                 ml_backend **out, char *errbuf, size_t errbuf_len)
{
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        set_errbuf(errbuf, errbuf_len, "mysql_init failed");
        return ML_ERR_DB;
    }

    if (sslmode == NULL || sslmode[0] == '\0') {
        sslmode = "prefer";
    }
    /* "prefer" (default) / "require": request an encrypted connection
     * without pinning a specific cert/key/CA -- appropriate for a
     * self-signed cert on a container/localhost server, matching the
     * same non-verifying "require" semantics the Postgres backends use.
     * "disable" skips this entirely, forcing plaintext. Neither this API
     * (mysql_ssl_set) nor MYSQL_OPT_SSL_ENFORCE below make a failed TLS
     * handshake fall back to plaintext automatically the way libpq's
     * sslmode=prefer does -- if the server can't do TLS at all, the
     * connection fails either way and the caller must pass
     * sslmode="disable" explicitly. */
    if (strcmp(sslmode, "disable") != 0) {
        mysql_ssl_set(conn, NULL, NULL, NULL, NULL, NULL);
    }
    if (strcmp(sslmode, "require") == 0) {
        my_bool enforce = 1;
        mysql_options(conn, MYSQL_OPT_SSL_ENFORCE, &enforce);
    }

    /* Credentials are passed as client-library connection parameters,
     * never interpolated into a query string or logged. */
    if (mysql_real_connect(conn, host, user, password, database, (unsigned int) port, NULL, 0) == NULL) {
        set_errbuf(errbuf, errbuf_len, mysql_error(conn));
        mysql_close(conn);
        return ML_ERR_DB;
    }
    mysql_set_character_set(conn, "utf8mb4");

    ml_backend *b = malloc(sizeof(ml_backend));
    b->vtable = &MYSQL_VTABLE;
    b->ctx = conn;
    *out = b;
    return ML_OK;
}
