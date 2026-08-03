"""
db_connector — the single entry point that turns a backend name + credentials
into a connected, schema-ready Backend instance.

Credentials are never accepted as raw SQL fragments and are never logged.
By default they are read from environment variables so they never need to
live in source or config files:

    MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
    MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
    MULTILANG_DB_HOST       (postgres/mysql)
    MULTILANG_DB_PORT       (postgres/mysql)
    MULTILANG_DB_USER       (postgres/mysql)
    MULTILANG_DB_PASSWORD   (postgres/mysql)
    MULTILANG_DB_NAME       (postgres/mysql)
    MULTILANG_DB_SSLMODE    (postgres/mysql; default "prefer" — use TLS
                             opportunistically but don't require it; set
                             to "require" to make TLS mandatory, "disable"
                             to force plaintext)

Explicit keyword arguments always take precedence over environment variables,
which is useful for tests and one-off scripts.
"""

import os

from .backends.filesystem_backend import FilesystemBackend
from .backends.mysql_backend import MySQLBackend
from .backends.postgres_backend import PostgresBackend
from .backends.sqlite_backend import SQLiteBackend

_SUPPORTED = ("sqlite", "postgres", "mysql", "filesystem")

_DEFAULT_PORTS = {"postgres": 5432, "mysql": 3306}


def db_connector(backend=None, **credentials):
    """
    Open a connection to the requested backend and return it ready to use.

    This is the single place that turns a backend name + credentials into a
    connected Backend instance — retrieve_data/insert_data never see raw
    connection details, only the Backend object this returns.

    Args:
        backend: One of "sqlite", "postgres", "mysql", "filesystem".
            Defaults to the MULTILANG_DB_BACKEND environment variable if
            not given.
        **credentials: Backend-specific connection parameters. Any key not
            passed explicitly falls back to the matching MULTILANG_DB_*
            environment variable (see module docstring for the full list).
            For sqlite: `path` (file). For filesystem: `path` (root
            directory). For postgres/mysql: `host`, `port`, `user`,
            `password`, `database`.

    Returns:
        A connected Backend instance (SQLiteBackend, PostgresBackend,
        MySQLBackend, or FilesystemBackend) with ensure_schema() already
        called, so the `strings` table (or root directory) is guaranteed
        to exist before the caller uses it.

    Raises:
        ValueError: If `backend` is missing/unsupported, or required
            credentials are missing for a non-sqlite backend.
        ImportError: If the driver package for the chosen backend (psycopg2
            for postgres, PyMySQL for mysql) isn't installed.
    """
    backend = backend or os.environ.get("MULTILANG_DB_BACKEND")
    if backend not in _SUPPORTED:
        raise ValueError(
            "backend must be one of {} — got {!r}".format(_SUPPORTED, backend)
        )

    if backend == "sqlite":
        path = credentials.get("path") or os.environ.get("MULTILANG_DB_PATH")
        if not path:
            raise ValueError("sqlite backend requires 'path' or MULTILANG_DB_PATH")
        conn = SQLiteBackend(path)
    elif backend == "filesystem":
        path = credentials.get("path") or os.environ.get("MULTILANG_DB_PATH")
        if not path:
            raise ValueError("filesystem backend requires 'path' or MULTILANG_DB_PATH")
        conn = FilesystemBackend(path)
    else:
        host = credentials.get("host") or os.environ.get("MULTILANG_DB_HOST")
        port = credentials.get("port") or os.environ.get("MULTILANG_DB_PORT") or _DEFAULT_PORTS[backend]
        user = credentials.get("user") or os.environ.get("MULTILANG_DB_USER")
        password = credentials.get("password") or os.environ.get("MULTILANG_DB_PASSWORD")
        database = credentials.get("database") or os.environ.get("MULTILANG_DB_NAME")

        missing = [
            name
            for name, val in (("host", host), ("user", user), ("password", password), ("database", database))
            if not val
        ]
        if missing:
            raise ValueError(
                "{} backend missing required credentials: {}".format(backend, ", ".join(missing))
            )

        sslmode = credentials.get("sslmode") or os.environ.get("MULTILANG_DB_SSLMODE") or "prefer"
        if backend == "postgres":
            conn = PostgresBackend(host, int(port), user, password, database, sslmode=sslmode)
        else:
            conn = MySQLBackend(host, int(port), user, password, database, sslmode=sslmode)

    conn.ensure_schema()
    return conn
