import locale

import pytest

from multilang import validation
from multilang.validation import ValidationError


@pytest.mark.parametrize(
    "tag,expected",
    [
        ("en", "en"),
        ("es", "es"),
        ("pt-BR", "pt-br"),
        ("zh-Hans", "zh-hans"),
        ("zh-Hans-CN", "zh-hans-cn"),
        ("en-US", "en-us"),
        ("sr-Latn-RS", "sr-latn-rs"),
    ],
)
def test_valid_language_ids(tag, expected):
    assert validation.validate_language_id(tag) == expected


@pytest.mark.parametrize("tag", ["", "english", "e", "en_US", "en--US", "123", None])
def test_invalid_language_ids(tag):
    with pytest.raises(ValidationError):
        validation.validate_language_id(tag)


@pytest.mark.parametrize(
    "raw,normalized",
    [("en-us", "en-us"), ("EN-US", "en-us"), ("zh-hans-cn", "zh-hans-cn"), ("EN", "en")],
)
def test_language_id_casing_normalized(raw, normalized):
    assert validation.validate_language_id(raw) == normalized


def test_optional_language_id_empty_is_none():
    assert validation.validate_optional_language_id("") is None
    assert validation.validate_optional_language_id(None) is None


@pytest.mark.parametrize("sid", ["hello", "button.publish", "menu:item-42", "a" * 200])
def test_valid_string_ids(sid):
    assert validation.validate_string_id(sid) == sid


@pytest.mark.parametrize("sid", ["", "a" * 201, "has space", "has/slash", None, "quote'"])
def test_invalid_string_ids(sid):
    with pytest.raises(ValidationError):
        validation.validate_string_id(sid)


def test_string_id_lowercased():
    assert validation.validate_string_id("Button.Publish") == "button.publish"


def test_context_defaults_to_empty_string():
    assert validation.validate_context(None) == ""
    assert validation.validate_context("") == ""


def test_context_lowercased():
    assert validation.validate_context("Menu.Item") == "menu.item"


def test_content_rejects_nul_byte():
    with pytest.raises(ValidationError):
        validation.validate_content("hello\x00world")


def test_content_rejects_empty():
    with pytest.raises(ValidationError):
        validation.validate_content("")


def test_status_allowlist():
    for ok in ("draft", "reviewed", "published"):
        assert validation.validate_status(ok) == ok
    with pytest.raises(ValidationError):
        validation.validate_status("live")


def test_lowercasing_is_locale_independent():
    """
    Regression test for a real bug found in the C and PHP ports: their
    libc-backed lowercasing (tolower()/strtolower()) is locale-sensitive
    — under a Turkish locale, 'I' doesn't lower to 'i' (Turkish
    distinguishes dotted/dotless i). str.lower() is documented as using
    fixed Unicode default casing regardless of locale, but this guards
    against a future change (e.g. switching to str.casefold() with some
    locale-aware wrapper) silently reintroducing the same class of bug
    that would make this process disagree with every other port about
    whether "EN" and "en" are the same language_id.
    """
    saved = locale.setlocale(locale.LC_CTYPE)
    try:
        try:
            locale.setlocale(locale.LC_CTYPE, "tr_TR.UTF-8")
        except locale.Error:
            pytest.skip("tr_TR.UTF-8 locale not installed")
        assert validation.validate_language_id("EN-US") == "en-us"
    finally:
        locale.setlocale(locale.LC_CTYPE, saved)
