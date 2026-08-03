# multilang (Go port)

Licensed under the GNU General Public License v3.0 or later — see `../LICENSE`.

Reusable string/translation storage backed by SQLite, PostgreSQL, MySQL,
or a plain filesystem tree. Same schema, same validation rules, same
results as every other language port in this project — see
`../conformance/README.md`.

## Schema

```sql
strings (
    language_id       TEXT NOT NULL,   -- BCP 47, stored lowercase
    string_id         TEXT NOT NULL,   -- stored lowercase
    context           TEXT NOT NULL DEFAULT '',  -- stored lowercase
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

```go
conn, err := multilang.Connect("sqlite", multilang.Credentials{Path: "strings.db"})
if err != nil {
    log.Fatal(err)
}
defer conn.Close()

multilang.InsertData(conn, "greeting", "en", "Hello world", multilang.InsertOptions{})
multilang.InsertData(conn, "greeting", "es", "Hola mundo", multilang.InsertOptions{OriginalLanguage: "en"})

content, found, err := multilang.RetrieveData(conn, "greeting", "es", "")
// content == "Hola mundo", found == true
```

## Credentials

Never passed as SQL — supplied via `Credentials{}` fields or environment
variables:

```
MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
MULTILANG_DB_HOST / _PORT / _USER / _PASSWORD / _NAME   (postgres/mysql)
```

The filesystem backend (`multilang.Connect("filesystem", multilang.Credentials{Path: "./strings"})`)
needs no server or driver — see
[`../docs/connectors.md#the-filesystem-backend`](../docs/connectors.md#the-filesystem-backend)
for its on-disk layout.

## Test

```bash
go test ./... -v
```

Runs the unit tests (`validation_test.go`, `strings_test.go`) and the
shared conformance suite (`conformance_test.go`) against SQLite (via the
pure-Go `modernc.org/sqlite` driver — no CGO required). Postgres/MySQL
require real servers — point `MULTILANG_DB_*` env vars at one and adapt
`strings_test.go`'s `freshConn` helper, or run in CI.

## Example

`examples/basic_usage.go` is a runnable, self-contained example covering
insert/translate/retrieve, context disambiguation, `*ValidationError`
handling, and switching backends via env vars:

```bash
go run ./examples
```
