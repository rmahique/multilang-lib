"""
Input validation — every value that reaches SQL goes through here first.

All checks are allow-list based (reject anything that doesn't match a known-good
shape) rather than deny-list based (reject known-bad patterns).

Every id-shaped column (language_id, original_language, string_id, context)
is normalized to lowercase by default, always — they're all part of the
exact-match composite primary key, so casing differences would otherwise
split what should be one row into duplicates.
"""

import re

MAX_STRING_ID_LEN = 200
MAX_CONTEXT_LEN = 200
MAX_CONTENT_LEN = 65535
MAX_UPDATED_BY_LEN = 200
# The BCP47 pattern below has no upper bound on repeated variant subtags,
# so without an explicit cap it would accept arbitrarily long tags. This
# must match every other port's limit exactly (see ../include/multilang.h
# ML_MAX_LANGUAGE_ID_LEN) or a tag valid in one language could be silently
# rejected in another.
MAX_LANGUAGE_ID_LEN = 35

_VALID_STATUSES = ("draft", "reviewed", "published")

# Simplified BCP 47: primary language (2-3 letters) + optional script
# (4 letters) + optional region (2 letters or 3 digits) + optional variants.
# Covers the vast majority of real-world tags: en, es, pt-BR, zh-Hans,
# zh-Hans-CN, en-US, sr-Latn-RS ...
_BCP47_RE = re.compile(
    r"^[a-zA-Z]{2,3}"
    r"(-[A-Za-z]{4})?"
    r"(-([A-Za-z]{2}|[0-9]{3}))?"
    r"(-[A-Za-z0-9]{5,8})*$"
)

# string_id / context: namespaced identifiers like "button.publish" or
# "menu:item-42". Letters, digits, dot, underscore, hyphen, colon.
_IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9._:-]+$")

# "." and ".." both match _IDENTIFIER_RE (it allows repeated dots) but are
# reserved path components on every filesystem the filesystem backend
# runs on. Since string_id/context become directory names there, either
# value silently collapses the path back up a level instead of naming a
# new one -- e.g. string_id=".." makes every language_id resolve to the
# same file, corrupting what should be independent rows. This is the
# strictest of the three backend families (SQL columns don't care), so
# it's the shared rule: identical validation everywhere, not just under
# the filesystem backend.
_RESERVED_IDENTIFIERS = (".", "..")


class ValidationError(ValueError):
    """Raised when caller-supplied data fails validation."""


def normalize_language_tag(value):
    """
    Normalize a BCP 47 tag to lowercase.

    BCP 47 comparison is defined as case-insensitive, and every id-shaped
    column in this schema is stored lowercase by default so that casing
    variation can never split what should be one row into two. Without
    this, "en-US" and "en-us" would be treated as two different languages
    instead of the same one, since language_id is part of the exact-match
    primary key.

    Args:
        value: A BCP 47 tag that has already passed _BCP47_RE.

    Returns:
        The tag lowercased, subtag order/content otherwise unchanged.
    """
    return value.lower()


def validate_language_id(value):
    """
    Validate that `value` is a well-formed BCP 47 language tag and return it
    lowercased (see normalize_language_tag) — like every id-shaped column,
    language_id is stored lowercase by default.

    Args:
        value: The candidate language tag, e.g. "en", "pt-BR", "zh-Hans-CN".

    Returns:
        The normalized tag.

    Raises:
        ValidationError: If `value` is not a string, is empty, or doesn't
            match the expected BCP 47 shape.
    """
    if not isinstance(value, str) or not value:
        raise ValidationError("language_id must be a non-empty string")
    if len(value) > MAX_LANGUAGE_ID_LEN:
        raise ValidationError("language_id {!r} exceeds {} characters".format(value, MAX_LANGUAGE_ID_LEN))
    if not _BCP47_RE.fullmatch(value):
        raise ValidationError("language_id {!r} is not a valid BCP 47 tag".format(value))
    return normalize_language_tag(value)


def validate_optional_language_id(value):
    """
    Like validate_language_id, but treats None/"" as "not provided" and
    returns None instead of raising. Used for original_language, which is
    empty exactly when a row is the source rather than a translation.

    Args:
        value: A BCP 47 tag, or None/"" to mean "no source language".

    Returns:
        The normalized tag, or None.

    Raises:
        ValidationError: If `value` is provided but not a valid BCP 47 tag.
    """
    if value is None or value == "":
        return None
    return validate_language_id(value)


def validate_string_id(value):
    """
    Validate a string_id: non-empty str, within length limit, and built only
    from the identifier charset (letters, digits, dot, underscore, hyphen,
    colon) so it's safe to use as a namespaced key like "button.publish".
    Returned lowercased — string_id is part of the exact-match primary key,
    so without this "Button.Publish" and "button.publish" would collide as
    two different rows instead of one.

    Args:
        value: The candidate string_id.

    Returns:
        `value` lowercased.

    Raises:
        ValidationError: If empty, too long, wrong type, or has stray chars.
    """
    if not isinstance(value, str) or not value:
        raise ValidationError("string_id must be a non-empty string")
    if len(value) > MAX_STRING_ID_LEN:
        raise ValidationError("string_id exceeds {} characters".format(MAX_STRING_ID_LEN))
    if not _IDENTIFIER_RE.fullmatch(value):
        raise ValidationError("string_id {!r} contains invalid characters".format(value))
    if value in _RESERVED_IDENTIFIERS:
        raise ValidationError("string_id {!r} is a reserved path component".format(value))
    return value.lower()


def validate_context(value):
    """
    Validate a context value. None is treated as "no context" and normalized
    to "" (the default row, matching the composite key's DEFAULT ''). Like
    string_id and language_id, a non-empty context is returned lowercased
    since it's part of the exact-match primary key.

    Args:
        value: The candidate context, or None.

    Returns:
        `value` lowercased, or "" if `value` was None.

    Raises:
        ValidationError: If not a string, too long, or has stray chars.
    """
    if value is None:
        return ""
    if not isinstance(value, str):
        raise ValidationError("context must be a string")
    if len(value) > MAX_CONTEXT_LEN:
        raise ValidationError("context exceeds {} characters".format(MAX_CONTEXT_LEN))
    if value and not _IDENTIFIER_RE.fullmatch(value):
        raise ValidationError("context {!r} contains invalid characters".format(value))
    if value in _RESERVED_IDENTIFIERS:
        raise ValidationError("context {!r} is a reserved path component".format(value))
    return value.lower()


def validate_content(value):
    """
    Validate the text to be stored. Rejects NUL bytes since some backends
    (and C callers, later) treat them as string terminators.

    MAX_CONTENT_LEN is measured in UTF-8 bytes, not characters — that's
    what the database columns actually store, and it's the one unit every
    language port can measure identically (Python's `len()` counts
    codepoints, which would let a Python caller store more non-ASCII text
    than PHP/Go/C, which measure bytes natively, would accept for the
    same string).

    Args:
        value: The candidate content.

    Returns:
        `value` unchanged.

    Raises:
        ValidationError: If empty, too long, wrong type, or contains NUL.
    """
    if not isinstance(value, str) or not value:
        raise ValidationError("content must be a non-empty string")
    if len(value.encode("utf-8")) > MAX_CONTENT_LEN:
        raise ValidationError("content exceeds {} bytes".format(MAX_CONTENT_LEN))
    if "\x00" in value:
        raise ValidationError("content must not contain NUL bytes")
    return value


def validate_status(value):
    """
    Validate that `value` is one of the allowed workflow states.

    Args:
        value: The candidate status.

    Returns:
        `value` unchanged.

    Raises:
        ValidationError: If not one of draft/reviewed/published.
    """
    if value not in _VALID_STATUSES:
        raise ValidationError(
            "status must be one of {} — got {!r}".format(_VALID_STATUSES, value)
        )
    return value


def validate_updated_by(value):
    """
    Validate the optional audit-trail field identifying who/what wrote a row.

    Like content, updated_by has no charset restriction, so
    MAX_UPDATED_BY_LEN is measured in UTF-8 bytes for the same reason
    validate_content measures bytes: that's what every other port
    measures natively, and the only unit that can't disagree.

    Args:
        value: The candidate updated_by, or None.

    Returns:
        `value` unchanged, or None.

    Raises:
        ValidationError: If not a string or exceeds the length limit.
    """
    if value is None:
        return None
    if not isinstance(value, str):
        raise ValidationError("updated_by must be a string")
    if len(value.encode("utf-8")) > MAX_UPDATED_BY_LEN:
        raise ValidationError("updated_by exceeds {} bytes".format(MAX_UPDATED_BY_LEN))
    return value
