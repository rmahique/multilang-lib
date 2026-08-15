"""Common interface every backend (SQLite, PostgreSQL, MySQL, filesystem) must implement."""

from abc import ABC, abstractmethod


class Backend(ABC):
    """
    A connected backend. All SQL lives here — callers never see raw SQL or
    raw connection objects. Every method must use parameterized queries;
    no value is ever interpolated into a query string.
    """

    @abstractmethod
    def ensure_schema(self):
        """Create the `strings` table if it doesn't already exist."""

    @abstractmethod
    def select_content(self, string_id, language_id, context):
        """Return the content for (string_id, language_id, context), or None."""

    @abstractmethod
    def upsert(self, row):
        """
        Insert or update a row. `row` is a dict with keys:
        string_id, language_id, context, content, original_language,
        status, source_checksum, updated_by, date_updated.
        """

    @abstractmethod
    def select_rows(self, language_id=None, context=None, status=None):
        """
        Return every full row (same shape as upsert's `row` dict) matching
        whichever of language_id/context/status are not None — an
        omitted (None) filter matches every value of that column.

        No content matching happens here: search_data does its own
        in-process regex/natural/exact matching over whatever this
        returns, which is what keeps search behavior identical across
        every backend (see docs/search.md).
        """

    @abstractmethod
    def close(self):
        """Close the underlying connection."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
