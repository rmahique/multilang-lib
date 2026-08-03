import pytest

from multilang import db_connector, retrieve_data, insert_data, ValidationError


@pytest.fixture
def conn(tmp_path):
    c = db_connector("sqlite", path=str(tmp_path / "test.db"))
    yield c
    c.close()


def test_insert_then_retrieve(conn):
    insert_data(conn, "greeting", "en", "Hello world")
    assert retrieve_data(conn, "greeting", "en") == "Hello world"


def test_missing_row_returns_none(conn):
    assert retrieve_data(conn, "nope", "en") is None


def test_upsert_updates_existing_row(conn):
    insert_data(conn, "greeting", "en", "Hello")
    insert_data(conn, "greeting", "en", "Hello!")
    assert retrieve_data(conn, "greeting", "en") == "Hello!"


def test_differently_cased_language_id_is_same_row(conn):
    insert_data(conn, "greeting", "en-US", "Hello")
    insert_data(conn, "greeting", "en-us", "Hello there")
    assert retrieve_data(conn, "greeting", "EN-US") == "Hello there"


def test_differently_cased_string_id_is_same_row(conn):
    insert_data(conn, "Greeting", "en", "Hello")
    insert_data(conn, "GREETING", "en", "Hello there")
    assert retrieve_data(conn, "greeting", "en") == "Hello there"


def test_differently_cased_context_is_same_row(conn):
    insert_data(conn, "post", "fr", "Publier", context="Button.Publish")
    insert_data(conn, "post", "fr", "Publier!", context="button.publish")
    assert retrieve_data(conn, "post", "fr", context="BUTTON.PUBLISH") == "Publier!"


def test_context_disambiguates_same_string_id(conn):
    insert_data(conn, "post", "fr", "Publier", context="button.publish")
    insert_data(conn, "post", "fr", "Article", context="menu.item")
    assert retrieve_data(conn, "post", "fr", context="button.publish") == "Publier"
    assert retrieve_data(conn, "post", "fr", context="menu.item") == "Article"


def test_translation_computes_source_checksum(conn):
    insert_data(conn, "greeting", "en", "Hello world")
    insert_data(conn, "greeting", "es", "Hola mundo", original_language="en")

    cur = conn._conn.execute(
        "SELECT source_checksum, original_language FROM strings "
        "WHERE language_id='es' AND string_id='greeting' AND context=''"
    )
    checksum, original_language = cur.fetchone()
    assert checksum is not None
    assert original_language == "en"


def test_source_row_has_no_checksum(conn):
    insert_data(conn, "greeting", "en", "Hello world")
    cur = conn._conn.execute(
        "SELECT source_checksum FROM strings "
        "WHERE language_id='en' AND string_id='greeting' AND context=''"
    )
    assert cur.fetchone()[0] is None


def test_retrieve_rejects_invalid_language_id(conn):
    with pytest.raises(ValidationError):
        retrieve_data(conn, "greeting", "not-a-real-lang-tag-!!")


def test_insert_rejects_invalid_status(conn):
    with pytest.raises(ValidationError):
        insert_data(conn, "greeting", "en", "Hello", status="live")


def test_insert_rejects_empty_content(conn):
    with pytest.raises(ValidationError):
        insert_data(conn, "greeting", "en", "")
