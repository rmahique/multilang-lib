"""
The two public data functions: retrieve_data and insert_data.

Both take an already-open Backend (from db_connector) so the caller controls
connection lifetime; neither function opens or closes a connection itself.
"""

import hashlib
from datetime import datetime, timezone

from . import validation


def retrieve_data(conn, string_id, language_id, context=""):
    """
    Look up one piece of text by its identity.

    Every value is validated before it reaches SQL and every query is
    parameterized — no value here is ever concatenated into a query string.

    Args:
        conn: An open Backend instance from db_connector.
        string_id: The identifier of the string to fetch.
        language_id: BCP 47 tag of the language to fetch (e.g. "es").
        context: Optional disambiguator for string_ids reused with different
            wording in different places (e.g. "button.publish" vs
            "menu.item"). Defaults to the un-contextualized row.

    Returns:
        The stored content (str), or None if no matching row exists. This
        is the data only — no metadata (status, dates, etc.) is returned.

    Raises:
        ValidationError: If any argument fails validation.
    """
    string_id = validation.validate_string_id(string_id)
    language_id = validation.validate_language_id(language_id)
    context = validation.validate_context(context)

    return conn.select_content(string_id, language_id, context)


def insert_data(
    conn,
    string_id,
    language_id,
    content,
    context="",
    original_language=None,
    status="draft",
    updated_by=None,
):
    """
    Insert a new row, or update it in place if (string_id, language_id,
    context) already exists (upsert on the composite primary key).

    Args:
        conn: An open Backend instance from db_connector.
        string_id: The identifier of the string being written.
        language_id: BCP 47 tag of the language this content is written in.
        content: The text to store.
        context: Optional disambiguator; see retrieve_data. Defaults to "".
        original_language: BCP 47 tag of the source language, if this row is
            a translation. Leave as None when this row IS the source — the
            row's own original_language is then stored as NULL and no
            source_checksum is computed.
        status: Workflow state — one of "draft", "reviewed", "published".
            Defaults to "draft" so a fresh translation isn't live until
            reviewed.
        updated_by: Optional identifier of who/what wrote this row, for an
            audit trail.

    When `original_language` is given, the current content of the source
    row (language_id=original_language, same string_id/context) is hashed
    with SHA-256 and stored as source_checksum. Re-hashing the source later
    and comparing against source_checksum is how staleness is detected: if
    they differ, the source changed since this translation was written. If
    the source row doesn't exist yet, source_checksum is left as None.

    Raises:
        ValidationError: If any argument fails validation.
    """
    string_id = validation.validate_string_id(string_id)
    language_id = validation.validate_language_id(language_id)
    context = validation.validate_context(context)
    content = validation.validate_content(content)
    original_language = validation.validate_optional_language_id(original_language)
    status = validation.validate_status(status)
    updated_by = validation.validate_updated_by(updated_by)

    source_checksum = None
    if original_language is not None:
        source_content = conn.select_content(string_id, original_language, context)
        if source_content is not None:
            source_checksum = _checksum(source_content)

    row = {
        "string_id": string_id,
        "language_id": language_id,
        "context": context,
        "content": content,
        "original_language": original_language,
        "status": status,
        "source_checksum": source_checksum,
        "updated_by": updated_by,
        # A real datetime object, not a pre-formatted string: each backend's
        # driver adapts it to that database's native timestamp type
        # (psycopg2 and PyMySQL both do this correctly; a hand-formatted
        # ISO-8601 string with a "T" separator and "+00:00" offset is
        # rejected by MySQL's DATETIME parser in strict mode).
        "date_updated": datetime.now(timezone.utc),
    }
    conn.upsert(row)


def _checksum(text):
    """Return the hex SHA-256 digest of `text`, used for source_checksum."""
    return hashlib.sha256(text.encode("utf-8")).hexdigest()
