"""SQLite backend — the zero-setup default, and what the test suite uses."""

import sqlite3

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
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
"""

_UPSERT = """
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated
"""

_SELECT = """
SELECT content FROM strings
WHERE language_id = ? AND string_id = ? AND context = ?
"""


class SQLiteBackend(Backend):
    """Backend implementation for SQLite, via the stdlib sqlite3 module."""

    def __init__(self, path):
        """
        Open a connection to the SQLite file at `path` (created if absent).

        Args:
            path: Filesystem path to the SQLite database file.
        """
        # timeout + WAL: readers shouldn't block on a concurrent writer.
        self._conn = sqlite3.connect(path, timeout=60)
        self._conn.execute("PRAGMA journal_mode = WAL")
        self._conn.execute("PRAGMA busy_timeout = 60000")
        self._conn.execute("PRAGMA foreign_keys = ON")

    def ensure_schema(self):
        """Create the `strings` table if it doesn't already exist."""
        self._conn.execute(_SCHEMA)
        self._conn.commit()

    def select_content(self, string_id, language_id, context):
        """Return content for the given key, or None if no row matches."""
        cur = self._conn.execute(_SELECT, (language_id, string_id, context))
        row = cur.fetchone()
        return row[0] if row else None

    def upsert(self, row):
        """Insert `row`, or update it in place on primary-key conflict."""
        self._conn.execute(
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
