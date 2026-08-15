"""
MySQL/MariaDB backend.

Requires MySQL 5.7.9+ or MariaDB 10.2.2+ (innodb_large_prefix on by default,
DYNAMIC row format default) — the composite primary key
(language_id, string_id, context) in utf8mb4 can exceed the legacy 767-byte
InnoDB index-prefix limit on older versions/configurations.
"""

from .base import Backend

_SCHEMA = """
CREATE TABLE IF NOT EXISTS strings (
    language_id       VARCHAR(35) NOT NULL,
    string_id         VARCHAR(200) NOT NULL,
    context           VARCHAR(200) NOT NULL DEFAULT '',
    content           MEDIUMTEXT NOT NULL,
    original_language VARCHAR(35),
    status            VARCHAR(20) NOT NULL DEFAULT 'draft',
    source_checksum   VARCHAR(64),
    updated_by        VARCHAR(200),
    date_updated      DATETIME NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
) ENGINE=InnoDB ROW_FORMAT=DYNAMIC DEFAULT CHARSET=utf8mb4
"""

_UPSERT = """
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
ON DUPLICATE KEY UPDATE
    content=VALUES(content),
    original_language=VALUES(original_language),
    status=VALUES(status),
    source_checksum=VALUES(source_checksum),
    updated_by=VALUES(updated_by),
    date_updated=VALUES(date_updated)
"""

_SELECT = """
SELECT content FROM strings
WHERE language_id = %s AND string_id = %s AND context = %s
"""

_SELECT_ROWS = """
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings
"""


class MySQLBackend(Backend):
    """Backend implementation for MySQL/MariaDB, via PyMySQL."""

    def __init__(self, host, port, user, password, database, sslmode="prefer"):
        """
        Open a connection. Credentials are passed as driver connection
        parameters — never interpolated into a query string or logged.

        Args:
            host: MySQL server hostname or IP.
            port: MySQL server port.
            user: Username to authenticate as.
            password: Password to authenticate with.
            database: Database/schema name to use.
            sslmode: "prefer" (default) uses PyMySQL's native PREFERRED
                mode — attempt TLS, fall back to plaintext if the server
                doesn't support it, matching the Postgres backend's
                default. "require" also attempts TLS but raises instead
                of silently falling back if the server doesn't offer it.
                "disable" forces plaintext.

        Raises:
            ImportError: If PyMySQL is not installed.
            ConnectionError: If sslmode="require" but the server didn't
                negotiate TLS.
        """
        try:
            import pymysql
        except ImportError as exc:
            raise ImportError(
                "PyMySQL is required for the MySQL backend: pip install PyMySQL"
            ) from exc

        # Passing no ssl_* arguments at all (and ssl_disabled left falsy)
        # is what makes PyMySQL choose its own PREFERRED mode internally;
        # explicitly requesting cert verification here would instead
        # force TLS and fail against a self-signed cert on a
        # container/localhost server.
        self._conn = pymysql.connect(
            host=host,
            port=port,
            user=user,
            password=password,
            database=database,
            charset="utf8mb4",
            ssl_disabled=(sslmode == "disable"),
        )
        if sslmode == "require" and not getattr(self._conn, "ssl", False):
            self._conn.close()
            raise ConnectionError(
                "MULTILANG_DB_SSLMODE=require but the MySQL server did not negotiate TLS"
            )

    def ensure_schema(self):
        """Create the `strings` table if it doesn't already exist."""
        with self._conn.cursor() as cur:
            cur.execute(_SCHEMA)
        self._conn.commit()

    def select_content(self, string_id, language_id, context):
        """Return content for the given key, or None if no row matches."""
        with self._conn.cursor() as cur:
            cur.execute(_SELECT, (language_id, string_id, context))
            row = cur.fetchone()
        return row[0] if row else None

    def upsert(self, row):
        """Insert `row`, or update it in place on primary-key conflict."""
        with self._conn.cursor() as cur:
            cur.execute(
                _UPSERT,
                (
                    row["language_id"],
                    row["string_id"],
                    row["context"],
                    row["content"],
                    row["original_language"],
                    row["status"],
                    row["source_checksum"],
                    row["updated_by"],
                    row["date_updated"],
                ),
            )
        self._conn.commit()

    def select_rows(self, language_id=None, context=None, status=None):
        """Return every row matching whichever filters are not None."""
        clauses = []
        params = []
        if language_id is not None:
            clauses.append("language_id = %s")
            params.append(language_id)
        if context is not None:
            clauses.append("context = %s")
            params.append(context)
        if status is not None:
            clauses.append("status = %s")
            params.append(status)

        sql = _SELECT_ROWS
        if clauses:
            sql += "WHERE " + " AND ".join(clauses)

        with self._conn.cursor() as cur:
            cur.execute(sql, params)
            rows = cur.fetchall()
        return [
            {
                "string_id": r[0],
                "language_id": r[1],
                "context": r[2],
                "content": r[3],
                "original_language": r[4],
                "status": r[5],
                "source_checksum": r[6],
                "updated_by": r[7],
                "date_updated": r[8],
            }
            for r in rows
        ]

    def close(self):
        """Close the underlying connection."""
        self._conn.close()
