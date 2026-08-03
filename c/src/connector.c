/*
 * ml_connect — the single entry point that turns a backend name +
 * credentials into a connected, schema-ready ml_backend.
 *
 * Credentials are never accepted as raw SQL fragments and are never
 * logged. By default they are read from environment variables so they
 * never need to live in source or config files:
 *
 *   MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
 *   MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
 *   MULTILANG_DB_HOST       (postgres/mysql)
 *   MULTILANG_DB_PORT       (postgres/mysql)
 *   MULTILANG_DB_USER       (postgres/mysql)
 *   MULTILANG_DB_PASSWORD   (postgres/mysql)
 *   MULTILANG_DB_NAME       (postgres/mysql)
 *   MULTILANG_DB_SSLMODE    (postgres/mysql; default "prefer" -- use TLS if
 *                            the server offers it; set to "require" to make
 *                            TLS mandatory, "disable" to force plaintext)
 *
 * Explicit fields in `creds` always take precedence over environment
 * variables, which is useful for tests and one-off scripts.
 */

#include "../include/multilang.h"
#include "backend.h"
#include "backends_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *fmt, const char *arg)
{
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    if (arg) {
        snprintf(errbuf, errbuf_len, fmt, arg);
    } else {
        snprintf(errbuf, errbuf_len, "%s", fmt);
    }
}

static const char *first_nonempty(const char *a, const char *b)
{
    return (a != NULL && a[0] != '\0') ? a : b;
}

ml_status ml_connect(const char *backend, const ml_credentials *creds,
                      ml_backend **out, char *errbuf, size_t errbuf_len)
{
    if (backend == NULL || backend[0] == '\0') {
        backend = getenv("MULTILANG_DB_BACKEND");
    }
    if (backend == NULL) {
        set_errbuf(errbuf, errbuf_len, "backend must be one of [sqlite, postgres, mysql, filesystem]", NULL);
        return ML_ERR_ARGS;
    }

    ml_credentials empty = {0};
    if (creds == NULL) {
        creds = &empty;
    }

    ml_status status;

    if (strcmp(backend, "sqlite") == 0) {
        const char *path = first_nonempty(creds->path, getenv("MULTILANG_DB_PATH"));
        if (path == NULL || path[0] == '\0') {
            set_errbuf(errbuf, errbuf_len, "sqlite backend requires path or MULTILANG_DB_PATH", NULL);
            return ML_ERR_ARGS;
        }
        status = ml_sqlite_backend_open(path, out, errbuf, errbuf_len);

    } else if (strcmp(backend, "filesystem") == 0) {
        const char *path = first_nonempty(creds->path, getenv("MULTILANG_DB_PATH"));
        if (path == NULL || path[0] == '\0') {
            set_errbuf(errbuf, errbuf_len, "filesystem backend requires path or MULTILANG_DB_PATH", NULL);
            return ML_ERR_ARGS;
        }
        status = ml_filesystem_backend_open(path, out, errbuf, errbuf_len);

    } else if (strcmp(backend, "postgres") == 0 || strcmp(backend, "mysql") == 0) {
        const char *host = first_nonempty(creds->host, getenv("MULTILANG_DB_HOST"));
        const char *user = first_nonempty(creds->user, getenv("MULTILANG_DB_USER"));
        const char *password = first_nonempty(creds->password, getenv("MULTILANG_DB_PASSWORD"));
        const char *database = first_nonempty(creds->database, getenv("MULTILANG_DB_NAME"));

        int port = creds->port;
        if (port == 0) {
            const char *port_env = getenv("MULTILANG_DB_PORT");
            if (port_env != NULL) {
                port = atoi(port_env);
            } else {
                port = (strcmp(backend, "postgres") == 0) ? 5432 : 3306;
            }
        }

        if (!host || !host[0] || !user || !user[0] || !password || !password[0] || !database || !database[0]) {
            set_errbuf(errbuf, errbuf_len, "%s backend missing required credentials", backend);
            return ML_ERR_ARGS;
        }

        const char *sslmode = first_nonempty(creds->sslmode, getenv("MULTILANG_DB_SSLMODE"));
        if (strcmp(backend, "postgres") == 0) {
            status = ml_postgres_backend_open(host, port, user, password, database, sslmode, out, errbuf, errbuf_len);
        } else {
            status = ml_mysql_backend_open(host, port, user, password, database, sslmode, out, errbuf, errbuf_len);
        }

    } else {
        set_errbuf(errbuf, errbuf_len, "backend must be one of [sqlite, postgres, mysql, filesystem] — got '%s'", backend);
        return ML_ERR_UNSUPPORTED;
    }

    if (status != ML_OK) {
        return status;
    }

    status = (*out)->vtable->ensure_schema((*out)->ctx, errbuf, errbuf_len);
    if (status != ML_OK) {
        (*out)->vtable->close((*out)->ctx);
        free(*out);
        *out = NULL;
    }
    return status;
}

void ml_close(ml_backend *conn)
{
    if (conn == NULL) {
        return;
    }
    conn->vtable->close(conn->ctx);
    free(conn);
}

ml_status ml_backend_truncate(ml_backend *conn, char *errbuf, size_t errbuf_len)
{
    return conn->vtable->truncate(conn->ctx, errbuf, errbuf_len);
}
