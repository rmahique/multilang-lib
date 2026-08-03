"""PostgreSQL backend."""

from .base import Backend

_SCHEMA = """
CREATE TABLE IF NOT EXISTS strings (
    language_id       TEXT NOT NULL,
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,
    status            TEXT NOT NULL DEFAULT 'draft',
    source_checksum   TEXT,
    updated_by        TEXT,
    date_updated      TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
"""

_UPSERT = """
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
ON CONFLICT (language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated
"""

_SELECT = """
SELECT content FROM strings
WHERE language_id = %s AND string_id = %s AND context = %s
"""


class PostgresBackend(Backend):
    """Backend implementation for PostgreSQL, via psycopg2."""

    def __init__(self, host, port, user, password, database, sslmode="prefer"):
        """
        Open a connection. Credentials are passed as driver connection
        parameters — never interpolated into a query string or logged.

        Args:
            host: PostgreSQL server hostname or IP.
            port: PostgreSQL server port.
            user: Username to authenticate as.
            password: Password to authenticate with.
            database: Database name to connect to.
            sslmode: libpq sslmode. Defaults to "prefer" (use TLS if the
                server offers it, but don't fail the connection if it
                doesn't) — a plain localhost/container Postgres has no
                certificate to negotiate, and requiring TLS is an opt-in
                the caller makes explicitly, not something forced on every
                connection. Pass "require" (or set MULTILANG_DB_SSLMODE)
                to make TLS mandatory.

        Raises:
            ImportError: If psycopg2 is not installed.
        """
        try:
            import psycopg2
        except ImportError as exc:
            raise ImportError(
                "psycopg2 is required for the PostgreSQL backend: pip install psycopg2-binary"
            ) from exc

        self._conn = psycopg2.connect(
            host=host,
            port=port,
            user=user,
            password=password,
            dbname=database,
            sslmode=sslmode,
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

    def close(self):
        """Close the underlying connection."""
        self._conn.close()
