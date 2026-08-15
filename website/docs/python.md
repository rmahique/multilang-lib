# Python

## Install

```bash
cd python
pip install -e .                  # SQLite + filesystem only
pip install -e ".[postgres]"      # + psycopg2
pip install -e ".[mysql]"         # + PyMySQL
```

## The example

Insert a source string plus a translation, retrieve with context
disambiguation, handle `ValidationError`, and switch backends via
environment variables. Copied verbatim from
[`python/examples/basic_usage.py`](https://github.com/rmahique/multilang-lib/blob/main/python/examples/basic_usage.py).

```python
import os
import tempfile

from multilang import db_connector, retrieve_data, insert_data, search_data, ValidationError


def main():
    # db_connector reads MULTILANG_DB_BACKEND (and the matching
    # MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling
    # back to a temp SQLite file here just keeps this example runnable
    # with no setup at all.
    backend = os.environ.get("MULTILANG_DB_BACKEND", "sqlite")
    if backend == "sqlite" and "MULTILANG_DB_PATH" not in os.environ:
        path = os.path.join(tempfile.mkdtemp(), "example.db")
        conn = db_connector("sqlite", path=path)
    else:
        conn = db_connector()  # everything from MULTILANG_DB_* env vars

    print(f"Connected via backend={backend!r}")

    # --- Insert a source string, then a translation of it ---------------
    insert_data(conn, "greeting", "en", "Hello world")
    insert_data(conn, "greeting", "es", "Hola mundo", original_language="en")
    # original_language="en" makes insert_data hash the current English
    # content and store that hash as source_checksum -- the basis for
    # detecting later that a translation has gone stale relative to its
    # source. retrieve_data itself never returns that metadata (data
    # only, by design).

    print(retrieve_data(conn, "greeting", "es"))  # -> "Hola mundo"

    # --- context disambiguates the same string_id used two ways ---------
    insert_data(conn, "post", "en", "Publish", context="button.publish")
    insert_data(conn, "post", "en", "Post", context="menu.item")

    print(retrieve_data(conn, "post", "en", context="button.publish"))  # -> "Publish"
    print(retrieve_data(conn, "post", "en", context="menu.item"))       # -> "Post"

    # --- search_data: find rows by content, not by exact key ------------
    insert_data(conn, "welcome1", "en", "Welcome to our platform")
    insert_data(conn, "welcome2", "en", "Welcome back, friend")
    for row in search_data(conn, "welcome", mode="natural", language_id="en"):
        print(row["string_id"], "->", row["content"])
    # -> welcome1 -> Welcome to our platform
    # -> welcome2 -> Welcome back, friend

    # --- retrieve_data on a row that doesn't exist: None, not an error --
    print(retrieve_data(conn, "greeting", "fr"))  # -> None

    # --- invalid input raises ValidationError, not a bare exception -----
    try:
        insert_data(conn, "greeting", "not-a-valid-bcp47-tag!!", "test")
    except ValidationError as e:
        print(f"rejected as expected: {e}")

    conn.close()


if __name__ == "__main__":
    main()
```

## Run it

```bash
cd python
PYTHONPATH=. python3 examples/basic_usage.py
```

Point it at a real Postgres/MySQL server, or the filesystem backend,
with no code changes — see [Switching backends](index.md#switching-backends).

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging lives in
`python/packaging/` — see that directory's `README.md`, or grab a
prebuilt one from [GitHub Releases](https://github.com/rmahique/multilang-lib/releases/latest).
