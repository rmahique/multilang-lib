/* PostgreSQL backend, via libpq. */

#include "backend.h"
#include "backends_internal.h"

#include <libpq-fe.h>
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
    "    date_updated      TIMESTAMPTZ NOT NULL,"
    "    PRIMARY KEY (language_id, string_id, context)"
    ")";

static const char *UPSERT_SQL =
    "INSERT INTO strings"
    "    (language_id, string_id, context, content, original_language,"
    "     status, source_checksum, updated_by, date_updated)"
    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)"
    "ON CONFLICT (language_id, string_id, context) DO UPDATE SET"
    "    content=excluded.content,"
    "    original_language=excluded.original_language,"
    "    status=excluded.status,"
    "    source_checksum=excluded.source_checksum,"
    "    updated_by=excluded.updated_by,"
    "    date_updated=excluded.date_updated";

static const char *SELECT_SQL =
    "SELECT content FROM strings WHERE language_id = $1 AND string_id = $2 AND context = $3";

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *msg)
{
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "%s", msg);
    }
}

static ml_status pg_ensure_schema(void *ctx, char *errbuf, size_t errbuf_len)
{
    PGconn *conn = (PGconn *) ctx;
    PGresult *res = PQexec(conn, SCHEMA_SQL);
    ml_status status = ML_OK;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        set_errbuf(errbuf, errbuf_len, PQerrorMessage(conn));
        status = ML_ERR_DB;
    }
    PQclear(res);
    return status;
}

static ml_status pg_select_content(void *ctx, const char *string_id, const char *language_id,
                                    const char *context, char **out_content, int *out_found,
                                    char *errbuf, size_t errbuf_len)
{
    PGconn *conn = (PGconn *) ctx;
    const char *params[3] = {language_id, string_id, context};
    *out_found = 0;

    PGresult *res = PQexecParams(conn, SELECT_SQL, 3, NULL, params, NULL, NULL, 0);
    ml_status status = ML_OK;
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        set_errbuf(errbuf, errbuf_len, PQerrorMessage(conn));
        status = ML_ERR_DB;
    } else if (PQntuples(res) > 0) {
        *out_content = strdup(PQgetvalue(res, 0, 0));
        *out_found = 1;
    }
    PQclear(res);
    return status;
}

static ml_status pg_upsert(void *ctx, const ml_row *row, char *errbuf, size_t errbuf_len)
{
    PGconn *conn = (PGconn *) ctx;

    char date_buf[40];
    struct tm tm_utc;
    gmtime_r(&row->date_updated, &tm_utc);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S+00", &tm_utc);

    const char *params[9] = {
        row->language_id, row->string_id, row->context, row->content,
        row->original_language, row->status, row->source_checksum,
        row->updated_by, date_buf,
    };

    PGresult *res = PQexecParams(conn, UPSERT_SQL, 9, NULL, params, NULL, NULL, 0);
    ml_status status = ML_OK;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        set_errbuf(errbuf, errbuf_len, PQerrorMessage(conn));
        status = ML_ERR_DB;
    }
    PQclear(res);
    return status;
}

static ml_status pg_truncate(void *ctx, char *errbuf, size_t errbuf_len)
{
    PGconn *conn = (PGconn *) ctx;
    PGresult *res = PQexec(conn, "TRUNCATE TABLE strings");
    ml_status status = ML_OK;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        set_errbuf(errbuf, errbuf_len, PQerrorMessage(conn));
        status = ML_ERR_DB;
    }
    PQclear(res);
    return status;
}

static void pg_close(void *ctx)
{
    PQfinish((PGconn *) ctx);
}

static const ml_backend_vtable POSTGRES_VTABLE = {
    .ensure_schema = pg_ensure_schema,
    .select_content = pg_select_content,
    .upsert = pg_upsert,
    .truncate = pg_truncate,
    .close = pg_close,
};

ml_status ml_postgres_backend_open(const char *host, int port, const char *user,
                                    const char *password, const char *database,
                                    const char *sslmode, ml_backend **out,
                                    char *errbuf, size_t errbuf_len)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (sslmode == NULL || sslmode[0] == '\0') {
        /* "prefer": use TLS if the server offers it, but don't fail the
         * connection if it doesn't. A plain localhost/container Postgres
         * has no certificate to negotiate, and requiring TLS is an
         * opt-in the caller makes explicitly (via MULTILANG_DB_SSLMODE),
         * not something forced on every connection. */
        sslmode = "prefer";
    }

    /* PQconnectdbParams takes keyword/value pairs directly, so credentials
     * never pass through a hand-built conninfo string (which would need
     * careful quoting to avoid a value like a password containing a
     * space or quote from corrupting the connection string). */
    const char *keywords[] = {"host", "port", "user", "password", "dbname", "sslmode", NULL};
    const char *values[] = {host, port_str, user, password, database, sslmode, NULL};

    PGconn *conn = PQconnectdbParams(keywords, values, 0);
    if (PQstatus(conn) != CONNECTION_OK) {
        set_errbuf(errbuf, errbuf_len, PQerrorMessage(conn));
        PQfinish(conn);
        return ML_ERR_DB;
    }

    ml_backend *b = malloc(sizeof(ml_backend));
    b->vtable = &POSTGRES_VTABLE;
    b->ctx = conn;
    *out = b;
    return ML_OK;
}
