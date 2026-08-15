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


def _ascii_fold(text):
    """
    Lowercase only the ASCII A-Z range, leaving every other codepoint
    untouched. Deliberately not Unicode-aware str.lower() — search's
    case-insensitive matching (exact/natural modes) must behave
    identically across all five ports, and C has no Unicode-aware
    lowercasing in its standard library (see docs/validation.md's
    locale-dependent-tolower() bug for why this project never trusts a
    language's own "smart" lowercasing for cross-language guarantees).
    See docs/search.md for the resulting documented limitation: non-ASCII
    letters (e.g. "É"/"é") only match by exact case.
    """
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text)


def _count_occurrences(haystack, needle):
    """Count non-overlapping occurrences of `needle` in `haystack`."""
    if not needle:
        return 0
    count = 0
    start = 0
    while True:
        idx = haystack.find(needle, start)
        if idx == -1:
            return count
        count += 1
        start = idx + len(needle)


def _score_row(content, mode, query, extra, case_sensitive):
    """
    Return how many times `query` (or, for natural/regex, its
    pre-processed form `extra`) matches `content` under `mode` — 0 means
    no match. See docs/search.md for the exact/natural/regex semantics.
    """
    if mode == "regex":
        return len(extra.findall(content))

    haystack = content if case_sensitive else _ascii_fold(content)

    if mode == "exact":
        needle = query if case_sensitive else _ascii_fold(query)
        return _count_occurrences(haystack, needle)

    # natural: every term must appear at least once (AND); score is the
    # sum of each term's occurrence count.
    total = 0
    for term in extra:
        needle = term if case_sensitive else _ascii_fold(term)
        occurrences = _count_occurrences(haystack, needle)
        if occurrences == 0:
            return 0
        total += occurrences
    return total


def search_data(
    conn,
    query,
    mode="natural",
    language_id=None,
    context=None,
    status=None,
    case_sensitive=False,
    limit=50,
    offset=0,
):
    """
    Search `content` across every row matching the optional filters.

    Matching runs entirely in-process, after fetching candidate rows from
    the backend filtered only by the cheap exact-match columns
    (language_id/context/status) — this is what guarantees identical
    search results across SQLite/Postgres/MySQL/filesystem: the actual
    matching logic below never touches backend-specific SQL/FTS
    engines. See docs/search.md for the full rationale and the documented
    cross-language regex-flavor/case-folding limitations.

    Args:
        conn: An open Backend instance from db_connector.
        query: The text/pattern to search for. Non-empty, at most 500
            UTF-8 bytes.
        mode: "exact" (query is a literal substring of content),
            "natural" (default; query is split on whitespace into terms,
            every one of which must appear as a substring of content —
            AND, not OR), or "regex" (query is a Python `re` pattern
            searched against content).
        language_id: Optional exact-match filter. None (default) means no
            filter.
        context: Optional exact-match filter. None (default) means no
            filter; "" filters for only the default/un-contextualized row.
        status: Optional exact-match filter. None (default) means no
            filter.
        case_sensitive: If False (default), exact/natural matching folds
            ASCII letters only (non-ASCII letters always match by exact
            case — a documented limitation, see docs/search.md) and regex
            matching uses Python's re.IGNORECASE.
        limit: Maximum rows to return, 1-500. Defaults to 50.
        offset: Rows to skip before the first returned result, for
            pagination. Defaults to 0.

    Returns:
        A list of dicts — string_id, language_id, context, content,
        original_language, status, source_checksum, updated_by,
        date_updated — one per matching row, ordered by match score
        descending, then (language_id, string_id, context) ascending as a
        deterministic tiebreak.

    Raises:
        ValidationError: If any argument fails validation.
    """
    mode = validation.validate_search_mode(mode)
    query, extra = validation.validate_search_query(query, mode, case_sensitive)
    language_id = validation.validate_optional_language_id(language_id)
    if context is not None:
        context = validation.validate_context(context)
    if status is not None:
        status = validation.validate_status(status)
    limit, offset = validation.validate_search_pagination(limit, offset)

    rows = conn.select_rows(language_id=language_id, context=context, status=status)

    scored = []
    for row in rows:
        score = _score_row(row["content"], mode, query, extra, case_sensitive)
        if score > 0:
            scored.append((score, row))

    scored.sort(key=lambda item: (-item[0], item[1]["language_id"], item[1]["string_id"], item[1]["context"]))
    return [row for _, row in scored[offset : offset + limit]]
