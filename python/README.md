# multilang (Python reference implementation)

Licensed under the GNU General Public License v3.0 or later — see `LICENSE`.

Reusable string/translation storage backed by SQLite, PostgreSQL, MySQL,
or a plain filesystem tree.

## Schema

```sql
strings (
    language_id       TEXT NOT NULL,   -- BCP 47 (en, es, pt-BR, zh-Hans...)
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,            -- BCP47 of source; NULL if this row IS the source
    status            TEXT NOT NULL DEFAULT 'draft',  -- draft | reviewed | published
    source_checksum   TEXT,            -- sha256 of the source content when this row was written
    updated_by        TEXT,
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
```

## Usage

```python
from multilang import db_connector, retrieve_data, insert_data, search_data

conn = db_connector("sqlite", path="strings.db")

insert_data(conn, "greeting", "en", "Hello world")
insert_data(conn, "greeting", "es", "Hola mundo", original_language="en")

retrieve_data(conn, "greeting", "es")  # -> "Hola mundo"

search_data(conn, "hola", mode="exact", language_id="es")
# -> [{"string_id": "greeting", "language_id": "es", "context": "", "content": "Hola mundo", ...}]

conn.close()
```

`search_data` also supports `mode="natural"` (default; every
whitespace-split term must appear) and `mode="regex"` (native `re`
syntax) — see [`../docs/search.md`](../docs/search.md).

## Credentials

Never passed as SQL — supplied via constructor args or environment variables:

```
MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
MULTILANG_DB_HOST / _PORT / _USER / _PASSWORD / _NAME   (postgres/mysql)
```

The filesystem backend (`db_connector("filesystem", path="./strings")`)
needs no server or driver — see
[`../docs/connectors.md#the-filesystem-backend`](../docs/connectors.md#the-filesystem-backend)
for its on-disk layout.

## Install

```bash
pip install -e ".[dev]"           # + pytest
pip install -e ".[postgres]"      # + psycopg2-binary
pip install -e ".[mysql]"         # + PyMySQL
pytest tests/ -v
```

## Cross-language / cross-database conformance

This is one of five planned language implementations, and must produce
identical results to the others for identical inputs (same data, same
validation errors) against every backend it supports. This is enforced by
`tests/test_conformance.py`, which runs the shared fixture at
`../conformance/cases.json` — see `conformance/README.md`.

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging lives in
`packaging/` — see `packaging/README.md` for build instructions.

## Example

`examples/basic_usage.py` is a runnable, self-contained example covering
insert/translate/retrieve, context disambiguation, `ValidationError`
handling, and switching backends via env vars:

```bash
PYTHONPATH=. python3 examples/basic_usage.py
```
