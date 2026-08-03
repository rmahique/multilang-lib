"""Filesystem-backend-specific behavior not covered by the shared
conformance suite (which only exercises retrieve_data/insert_data, not
the on-disk shape)."""

import json

from multilang import db_connector, insert_data, retrieve_data


def test_layout_on_disk(tmp_path):
    conn = db_connector("filesystem", path=str(tmp_path))
    insert_data(conn, "greeting", "en", "Hello world", updated_by="alice")
    insert_data(conn, "post", "en", "Publish", context="button.publish")
    conn.close()

    default_file = tmp_path / "en" / "greeting" / "@default" / "content.json"
    context_file = tmp_path / "en" / "post" / "button.publish" / "content.json"
    assert default_file.is_file()
    assert context_file.is_file()

    record = json.loads(default_file.read_text(encoding="utf-8"))
    assert record["content"] == "Hello world"
    assert record["updated_by"] == "alice"
    assert record["status"] == "draft"


def test_upsert_overwrites_in_place(tmp_path):
    conn = db_connector("filesystem", path=str(tmp_path))
    insert_data(conn, "greeting", "en", "Hello")
    insert_data(conn, "greeting", "en", "Hello again")
    assert retrieve_data(conn, "greeting", "en") == "Hello again"
    conn.close()

    # exactly one file for this key, not two
    matches = list((tmp_path / "en" / "greeting").rglob("content.json"))
    assert len(matches) == 1


def test_missing_row_returns_none(tmp_path):
    conn = db_connector("filesystem", path=str(tmp_path))
    assert retrieve_data(conn, "nope", "en") is None
    conn.close()


def test_ensure_schema_creates_root(tmp_path):
    root = tmp_path / "does" / "not" / "exist" / "yet"
    assert not root.exists()
    conn = db_connector("filesystem", path=str(root))
    assert root.is_dir()
    conn.close()


def test_dot_string_id_rejected_at_validation(tmp_path):
    # "." and ".." are validation-legal by charset alone, but as a bare
    # string_id they'd collapse the directory back up a level instead of
    # naming one -- e.g. ".." made every language_id collide onto the
    # same file (see conformance case string_id_double_dot_rejected).
    # validation.py now rejects both outright, so this never reaches the
    # backend at all.
    from multilang import ValidationError

    conn = db_connector("filesystem", path=str(tmp_path))
    for bad in (".", ".."):
        try:
            insert_data(conn, bad, "en", "should not be written")
            raise AssertionError("expected ValidationError for string_id={!r}".format(bad))
        except ValidationError:
            pass
    assert list(tmp_path.rglob("content.json")) == []
    conn.close()


def test_three_dots_is_a_plain_string_id(tmp_path):
    # Only exactly "." and ".." are reserved on POSIX -- "..." is an
    # ordinary (if unusual) filename and must still work.
    conn = db_connector("filesystem", path=str(tmp_path))
    insert_data(conn, "...", "en", "three dots is fine")
    assert retrieve_data(conn, "...", "en") == "three dots is fine"
    conn.close()
