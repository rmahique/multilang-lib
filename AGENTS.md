# AGENTS.md

Condensed index for AI coding agents working in this repo. This is not a
replacement for `docs/` — it exists so you don't have to read 5 READMEs
and grep across 5 directories just to find where something lives. Every
fact below is a pointer or a value copied verbatim from source; the
*why* behind each one is one link away in `docs/`.

**Keep this file in sync with the code it points at.** A stale file path
or wrong regex here is worse than no file at all — it actively misleads
instead of saving a lookup. If you change a file path, a rule, or a
public function signature, update the matching line here in the same
change.

## The one rule that overrides everything else

Behavior must be identical across all ports and all backend families.
If a change to one port's logic isn't mirrored in the others (or isn't provably covered by
`conformance/cases.json`), it's not done. See
[`docs/extending.md`](docs/extending.md) before changing validation
rules, the schema, or connector behavior.

## Documentation is part of the change, not a follow-up

Before calling any change done, ask whether it made something in
`docs/`, a `README.md`, this file, `CHANGELOG.md`, an
`examples/basic_usage.*`, or the examples site (`website/docs/*.md`)
wrong, incomplete, or stale — and if so, fix it in the same change, not
a later one. Every code block on `website/docs/*.md` is copied verbatim
from a `<port>/examples/basic_usage.*` file — changing one without the
other is exactly the kind of drift this rule exists to prevent. "Relevant enough" means: a new
backend/port, a changed validation rule, a changed public function
signature, a changed schema column, a changed env var, or a changed
command someone would actually run. Docs that silently drift out of sync
with the code are worse than no docs, because they're trusted by default
— this project was built by treating every doc claim as something to
verify against the actual repo state (grep it, run it, read the file),
and it stays trustworthy only if every change keeps doing that. A
representative example: adding the filesystem backend touched not just
5 backend implementations, but every port's `README.md`, `docs/*.md`
backend counts, all 6 `examples/basic_usage.*` docstrings, `AGENTS.md`'s
file map, and `CHANGELOG.md` — in the same change, not a follow-up.

## Public API (same 3 functions/shapes in every port)

`db_connector(backend, credentials)` → connection · `insert_data(conn,
string_id, language_id, content, ...)` → upsert · `retrieve_data(conn,
string_id, language_id, context?)` → content or "not found" (never
raises for a missing row — see [`docs/errors.md`](docs/errors.md)).

## File map: where a concern lives, per port

| Concern | Python | JavaScript | PHP | Go | C |
|---|---|---|---|---|---|
| Validation | `python/multilang/validation.py` | `javascript/src/validation.js` | `php/src/Validation.php` | `go/validation.go` | `c/src/validation.c` |
| Connector | `python/multilang/connector.py` | `javascript/src/connector.js` | `php/src/Connector.php` | `go/connector.go` | `c/src/connector.c` |
| Public functions | `python/multilang/strings.py` | `javascript/src/strings.js` | `php/src/Strings.php` | `go/strings.go` | `c/src/strings.c` |
| Backend interface | `python/multilang/backends/base.py` | (duck-typed, see `backends/*.js`) | `php/src/Backends/BackendInterface.php` | `go/backend.go` (`Backend` interface) | `c/src/backends_internal.h` (vtable) |
| SQLite backend | `python/multilang/backends/sqlite_backend.py` | `javascript/src/backends/sqlite.js` | `php/src/Backends/SqliteBackend.php` | `go/sqlite_backend.go` | `c/src/sqlite_backend.c` |
| Postgres backend | `python/multilang/backends/postgres_backend.py` | `javascript/src/backends/postgres.js` | `php/src/Backends/PostgresBackend.php` | `go/postgres_backend.go` | `c/src/postgres_backend.c` |
| MySQL backend | `python/multilang/backends/mysql_backend.py` | `javascript/src/backends/mysql.js` | `php/src/Backends/MysqlBackend.php` | `go/mysql_backend.go` | `c/src/mysql_backend.c` |
| Filesystem backend | `python/multilang/backends/filesystem_backend.py` | `javascript/src/backends/filesystem.js` | `php/src/Backends/FilesystemBackend.php` | `go/filesystem_backend.go` | `c/src/filesystem_backend.c` |
| Public header/entry | `python/multilang/__init__.py` | `javascript/src/index.js` | (autoload via composer) | package `multilang` root | `c/include/multilang.h` (+ `.hpp` for C++) |
| Error type | `ValidationError` (`ValueError` subclass) | `ValidationError` (`Error` subclass) | `ValidationException` (`InvalidArgumentException` subclass) | `*ValidationError` (`error` interface) | `ML_ERR_VALIDATION` enum / `multilang::ValidationError` (C++) |
| Unit tests | `python/tests/` | `javascript/test/` | `php/tests/` | `go/*_test.go` | `c/test/` |
| Conformance runner | `python/tests/test_conformance.py` | `javascript/test/conformance.test.js` | `php/tests/ConformanceTest.php` | `go/conformance_test.go` | `c/test/test_conformance.c` + `test_cpp.cpp` |
| Container test image | `python/Dockerfile.conformance` | `javascript/Dockerfile.conformance` | `php/Dockerfile.conformance` | `go/Dockerfile.conformance` | `c/Dockerfile.conformance` |
| Runnable example | `python/examples/basic_usage.py` | `javascript/examples/basic-usage.js` | `php/examples/basic_usage.php` | `go/examples/basic_usage.go` | `c/examples/basic_usage.c` (+`.cpp`) |
| Distro packaging (`.deb`/`.rpm`, 4 distros: Debian bookworm, Fedora latest, openSUSE Leap 15, openSUSE Tumbleweed) | `python/packaging/` | `javascript/packaging/` | `php/packaging/` | `go/packaging/` | `c/packaging/` |

## Schema (identical DDL intent in every backend)

Single table `strings`, primary key `(language_id, string_id, context)`.
Columns: `content`, `original_language` (nullable — null means this row
*is* the source), `status` (`draft`/`reviewed`/`published`),
`source_checksum`, `updated_by`, `date_updated`. Full rationale:
[`docs/schema.md`](docs/schema.md).

## Validation rules (must match exactly across all ports)

| Field | Rule | Cap |
|---|---|---|
| `language_id` / `original_language` | `^[a-zA-Z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}\|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$` (simplified BCP 47), then lowercased | 35 chars |
| `string_id` / `context` | `^[A-Za-z0-9._:-]+$`, then lowercased; `.` and `..` rejected outright (reserved on the filesystem backend, see below) | 200 chars each |
| `content` | non-empty, no NUL bytes, no charset restriction, **not** lowercased | 65535 UTF-8 **bytes** (not codepoints/UTF-16 units) |
| `updated_by` | optional, no charset restriction, not lowercased | 200 UTF-8 bytes |
| `status` | exactly `draft` \| `reviewed` \| `published` | — |

Lowercasing must be ASCII-only and never consult process locale (no bare
`tolower()`/`strtolower()` — see the Turkish-locale bug in
[`docs/validation.md`](docs/validation.md#resolved-cross-language-inconsistencies)).
Regex end-anchors must use full-string match (`re.fullmatch` in Python,
PCRE `D` modifier in PHP) — `$` alone accepts a trailing `\n` in those
two engines but not JS/Go/C.

## Environment variables (identical names in every port)

`MULTILANG_DB_BACKEND` (`sqlite`\|`postgres`\|`mysql`\|`filesystem`) ·
`MULTILANG_DB_PATH` (sqlite: file path · filesystem: root dir) ·
`MULTILANG_DB_HOST` / `_PORT` / `_USER` / `_PASSWORD` / `_NAME`
(postgres/mysql) · `MULTILANG_DB_SSLMODE` (`prefer` default \| `require`
\| `disable`, same semantics for both postgres and mysql — implementation
mechanism differs per driver, see
[`docs/connectors.md`](docs/connectors.md#tls-multilang_db_sslmode)).

**Filesystem backend** (all 5 ports): one `content.json` per row at
`<root>/<language_id>/<string_id>/<context-or-@default>/content.json`.
Details: [`docs/connectors.md#the-filesystem-backend`](docs/connectors.md#the-filesystem-backend).

## Commands

```bash
make test              # all ports' unit tests, each in a disposable container
make test-python        # just one (python|javascript|php|go|c)
make conformance        # all ports vs real Postgres+MySQL, disposable containers
make conformance-go     # just one port vs real DBs
./scripts/set-go-module-path.sh   # sync go.mod to the actual git remote
```

## Tests always run in disposable containers

**Never run any test suite directly against the host's Python/Node/PHP/
Go/C toolchain — every test run happens inside a single-use container.**
This applies to the fast SQLite/filesystem unit tests, not just the
live-DB conformance suite:

- `make test` / `make test-<lang>` → `conformance/run-unit-tests.sh` —
  builds (or reuses) that port's `Dockerfile.conformance` image and runs
  its unit tests inside a `--rm` container, entrypoint overridden to
  skip the live-DB flow.
- `make conformance` / `make conformance-<lang>` →
  `conformance/run-live-db-tests.sh` — same containers, plus disposable
  Postgres/MySQL containers on a private network, IP-addressed, no DNS
  between containers, torn down on exit including on failure.

**Why:** a test result should depend only on the code and the pinned
image versions (`PYTHON_VERSION`, `NODE_VERSION`, etc.), never on
whatever happens to be installed on whichever machine ran it — the same
reasoning that already applied to live-DB tests, just generalized to all
of them. It also means a test run can never leave stray state on the
host: no installed deps, no leftover SQLite/filesystem temp files, no
stale build artifacts from a previous run's different dependency
versions silently getting reused.

**If you're an AI agent working in this repo:** run `make test`/`make
test-<lang>`/`make conformance`, never `pytest`/`npm test`/`phpunit`/
`go test`/`make test` (inside `c/`) directly — those commands still work
locally (each port's own README documents them, and they're what the
container images run internally), but invoking them yourself bypasses
the container. If no container engine is available, say so rather than
falling back to running tests on the host silently.

Never build SQL by string concatenation — every query in every port is
parameterized.

## Docs index

[`docs/README.md`](docs/README.md) lists all of `docs/` with one-line
descriptions. Most relevant when actually changing code:
[`docs/validation.md`](docs/validation.md) (rules + past bugs),
[`docs/errors.md`](docs/errors.md) (error types per language),
[`docs/extending.md`](docs/extending.md) (checklist for a new
conformance case or 6th port), [`docs/architecture.md`](docs/architecture.md)
(diagrams).
