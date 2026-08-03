#!/usr/bin/env python3
"""
Runnable example: insert a source string plus translations, retrieve with
context disambiguation, handle ValidationError, and switch backends via
environment variables.

Run from the python/ directory:

    PYTHONPATH=. python3 examples/basic_usage.py

By default this uses a throwaway SQLite file. To point it at a real
server instead, set the same MULTILANG_DB_* variables every port reads
(see ../../docs/connectors.md):

    MULTILANG_DB_BACKEND=postgres \
    MULTILANG_DB_HOST=localhost MULTILANG_DB_USER=multilang \
    MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
    PYTHONPATH=. python3 examples/basic_usage.py

Or point it at the filesystem backend (no server at all):

    MULTILANG_DB_BACKEND=filesystem MULTILANG_DB_PATH=./example-strings \
    PYTHONPATH=. python3 examples/basic_usage.py
"""

import os
import tempfile

from multilang import db_connector, retrieve_data, insert_data, ValidationError


def main():
    # db_connector reads MULTILANG_DB_BACKEND (and the matching
    # MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling back
    # to a temp SQLite file here just keeps this example runnable with no
    # setup at all.
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
    # only, by design -- see ../../docs/schema.md).

    print(retrieve_data(conn, "greeting", "es"))  # -> "Hola mundo"

    # --- context disambiguates the same string_id used two ways ---------
    insert_data(conn, "post", "en", "Publish", context="button.publish")
    insert_data(conn, "post", "en", "Post", context="menu.item")

    print(retrieve_data(conn, "post", "en", context="button.publish"))  # -> "Publish"
    print(retrieve_data(conn, "post", "en", context="menu.item"))       # -> "Post"

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
