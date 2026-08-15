# Connectors, credentials, and TLS

## The four-function shape

Every port exposes the same four entry points, under names idiomatic to
that language (`db_connector`/`retrieve_data`/`insert_data`/`search_data`
in Python, `dbConnector`/`retrieveData`/`insertData`/`searchData` in
JavaScript, `Connector::connect`/`Strings::retrieveData`/
`Strings::insertData`/`Strings::searchData` in PHP,
`Connect`/`RetrieveData`/`InsertData`/`SearchData` in Go,
`ml_connect`/`ml_retrieve_data`/`ml_insert_data`/`ml_search_data` in C,
and a thin `multilang::Connection` RAII wrapper over the C API in C++):

- **connect** — turns a backend name (`"sqlite"` | `"postgres"` |
  `"mysql"` | `"filesystem"`) and credentials into an open, schema-ready
  connection. This is the *only* place credentials and connection
  details exist; nothing downstream ever sees a raw connection string.
- **insert_data** — validates every argument (see
  [`validation.md`](validation.md)), computes `source_checksum` if this
  is a translation, and upserts the row.
- **retrieve_data** — validates its arguments and returns content only —
  no metadata, matching the schema design in
  [`schema.md`](schema.md).
- **search_data** — finds rows by content instead of by exact key
  (regex/natural/exact matching). See [`search.md`](search.md) for the
  full design; unlike the other three, it returns full rows, not content
  only.

Internally, every port implements this via a small backend abstraction —
an interface/vtable with `EnsureSchema`, `SelectContent`, `SelectRows`,
`Upsert`, `Close` (Go's `Backend` interface, C's `ml_backend_vtable`,
Python's `Backend` ABC, etc.) — so the SQLite/Postgres/MySQL/filesystem-
specific logic lives in exactly one place per language, and adding
another backend means implementing that interface once, not touching
`insert_data`/`retrieve_data`/`search_data` at all — that's exactly how
the filesystem backend below was added.

## The filesystem backend

No server, no driver — for when the translation set is meant to be
human-editable and diffable in version control rather than queried.
`MULTILANG_DB_PATH` (or the `path` credential) is a root directory,
created on connect if it doesn't exist. Layout:

```
<root>/<language_id>/<string_id>/<context>/content.json
<root>/<language_id>/<string_id>/@default/content.json   (context == "")
```

`content.json` holds every column that isn't part of the primary key
(`content`, `original_language`, `status`, `source_checksum`,
`updated_by`, `date_updated`) — the path itself *is* the composite key
(`language_id`, `string_id`, `context`), so it can never collide the way
an unindexed key could. Writes go to a temp file in the same directory
and are then atomically renamed into place (`os.replace`-equivalent),
matching the "no reader ever sees a half-written row" guarantee the SQL
backends get from a transaction commit.

`string_id`/`context` can't contain `/` (see
[`validation.md`](validation.md)), so each is always exactly one path
segment — the fixed 3-level nesting above means there's no directory
depth for a value like `".."` to actually escape `root`, only to cancel
out and land back inside it. Per-backend implementations still verify
path containment before touching disk as cheap defense-in-depth, the
same instinct that has every other backend parameterize its queries
rather than trust input shape alone.

## Credentials: environment variables, never hardcoded

Every port reads the same variable names, so a deployment's environment
configuration doesn't change per language:

| Variable | Applies to | Meaning |
|---|---|---|
| `MULTILANG_DB_BACKEND` | all | `sqlite` \| `postgres` \| `mysql` \| `filesystem` |
| `MULTILANG_DB_PATH` | sqlite | file path |
| `MULTILANG_DB_PATH` | filesystem | root directory (created if absent) |
| `MULTILANG_DB_HOST` | postgres/mysql | hostname or IP |
| `MULTILANG_DB_PORT` | postgres/mysql | defaults to 5432/3306 |
| `MULTILANG_DB_USER` | postgres/mysql | |
| `MULTILANG_DB_PASSWORD` | postgres/mysql | |
| `MULTILANG_DB_NAME` | postgres/mysql | database name |
| `MULTILANG_DB_SSLMODE` | postgres/mysql | see below |

Every `connect` function also accepts these as explicit arguments
(credentials dict/struct/options object), which take precedence over the
environment variable of the same name — useful for tests and one-off
scripts, but production configuration should come from the environment,
not from a value baked into source.

Credentials are never interpolated into a query string, never logged,
and never pass through anything a shell or log line could capture — they
go directly into the driver's own connection parameters (`psycopg2.connect(...)`,
a DSN built from individually-escaped fields, PDO's constructor args,
etc.).

## TLS: `MULTILANG_DB_SSLMODE`

Three values, identical meaning across every port and both network
backends:

- **`prefer` (default)** — attempt TLS, fall back to plaintext if the
  server doesn't offer it. A plain localhost/container database has no
  certificate to negotiate and shouldn't be required to have one.
- **`require`** — attempt TLS, fail the connection instead of silently
  falling back if the server didn't actually negotiate it.
- **`disable`** — plaintext only, no TLS attempt.

In every case, `require` means "encrypt the connection," not "verify the
server's certificate against a CA" — appropriate for a self-signed cert
on a container/localhost server, not a substitute for real certificate
verification in a production deployment talking to a remote server.

This is symmetric between Postgres and MySQL, which is worth calling out
because the two backends' native driver capabilities are not symmetric:

- Postgres (via libpq, used directly by Python/Go/C and indirectly by
  PHP's PDO_PGSQL) has a real three-way `sslmode` natively — `prefer` is
  libpq's own built-in behavior.
- MySQL client libraries mostly don't. go-sql-driver/mysql's
  `tls=preferred` and PyMySQL's default behavior (when no `ssl_*` options
  are given) both implement `prefer` natively too, but node-postgres,
  mysql2, and PDO_MYSQL only support "attempt" or "don't" — so those
  backends implement `prefer` themselves: try a TLS connection first,
  fall back to a plaintext one only if that attempt fails.

None of this is a difference a caller needs to know about — every port's
`MULTILANG_DB_SSLMODE=prefer` behaves the same from the outside. It's
mentioned here because it's the reason the JavaScript and PHP MySQL
backends contain more connection logic than the Go/Python ones: they're
compensating for a driver capability gap, not doing something different
in intent.

See [`../conformance/README.md#tls`](../conformance/README.md#tls) for
how this is actually exercised against real servers (the live-DB test
run uses the `prefer` default as-is, so it's a standing check that
opportunistic TLS actually negotiates, not just that plaintext still
works).
