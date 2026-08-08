# multilang (C / C++ port)

Licensed under the GNU General Public License v3.0 or later — see `../LICENSE`.

Reusable string/translation storage backed by SQLite, PostgreSQL, MySQL,
or a plain filesystem tree. Same schema, same validation rules, same
results as every other language port in this project — see
`../conformance/README.md`.

Two layers:
- **C** (`include/multilang.h`, `libmultilang.so`) — the core implementation.
  Status-code based (`ml_status`), caller-supplied output buffers for
  every id-shaped value, no hidden allocation except for retrieved
  content (`char*`, malloc'd, caller `free()`s it).
- **C++** (`include/multilang.hpp`, `libmultilangxx.so`) — a thin RAII
  wrapper over the C layer. `multilang::Connection` owns the connection
  and closes it in its destructor; `ml_status` becomes
  `multilang::ValidationError` / `multilang::DbError` exceptions;
  `std::optional<std::string>` instead of an out-parameter + found flag.
  Adds no behavior beyond that translation.

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

## Usage — C

```c
#include <multilang.h>

ml_credentials creds = {0};
creds.path = "strings.db";

ml_backend *conn = NULL;
char err[ML_ERRBUF_LEN];
if (ml_connect("sqlite", &creds, &conn, err, sizeof(err)) != ML_OK) { /* handle err */ }

ml_insert_data(conn, "greeting", "en", "Hello world", NULL, err, sizeof(err));

char *content = NULL;
ml_retrieve_data(conn, "greeting", "en", NULL, &content, err, sizeof(err));
/* content == "Hello world"; free(content) when done */

ml_close(conn);
```

## Usage — C++

```cpp
#include <multilang.hpp>

multilang::Credentials creds;
creds.path = "strings.db";
multilang::Connection conn("sqlite", creds);

conn.insert_data("greeting", "en", "Hello world");
auto content = conn.retrieve_data("greeting", "en");
// content == std::optional<std::string>("Hello world")
```

## Credentials

Never passed as SQL — supplied via `ml_credentials`/`Credentials` fields
or environment variables:

```
MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
MULTILANG_DB_HOST / _PORT / _USER / _PASSWORD / _NAME   (postgres/mysql)
```

The filesystem backend (`creds.path = "./strings"; ml_connect("filesystem", &creds, ...)`)
needs no server or driver — see
[`../docs/connectors.md#the-filesystem-backend`](../docs/connectors.md#the-filesystem-backend)
for its on-disk layout.

## Build & test

```bash
make          # builds libmultilang.so and libmultilangxx.so
make test     # + builds and runs all 4 test binaries
```

Requires development headers for sqlite3, libpq (PostgreSQL), and
libmariadb (MySQL/MariaDB client, API-compatible with libmysqlclient) —
on openSUSE/SLES: `sqlite3-devel postgresql-devel libmariadb-devel`.

`make test` runs `test/test_validation`, `test/test_strings`, and the
shared conformance suite twice — once through the C API
(`test/test_conformance`) and once through the C++ wrapper
(`test/test_cpp`) — against SQLite. Postgres/MySQL require real servers;
point `MULTILANG_DB_*` env vars at one and adapt the `fresh_conn` helpers,
or run in CI.

The conformance runners parse `../conformance/cases.json` with
`test/json_mini.{h,c}`, a JSON reader written specifically for that one
fixed, controlled file rather than vendoring a general-purpose
third-party JSON library — smaller surface, no unpinned supply-chain
dependency for something this narrow. It is test-only tooling, not part
of the shipped library.

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging — real compiled
`libmultilang0`/`libmultilang-dev` (and `-devel`) split, built via
`../Makefile`'s `install` target — lives in `packaging/` — see
`packaging/README.md` for build instructions.

## Example

`examples/basic_usage.c` (C API) and `examples/basic_usage.cpp` (C++ API)
are runnable, self-contained examples covering insert/translate/retrieve,
context disambiguation, validation-error handling, and switching backends
via env vars:

```bash
make   # builds libmultilang.so / libmultilangxx.so first
cc  -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -o examples/basic_usage examples/basic_usage.c -L. -lmultilang
g++ -std=c++17 -Iinclude -o examples/basic_usage_cpp examples/basic_usage.cpp -L. -lmultilangxx -lmultilang
LD_LIBRARY_PATH=. ./examples/basic_usage
LD_LIBRARY_PATH=. ./examples/basic_usage_cpp
```
