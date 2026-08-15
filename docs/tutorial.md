# Tutorial: from zero to a translated string set on a real database

This is a walkthrough, not a reference — for the full API shape see
[`connectors.md`](connectors.md); for a self-contained runnable listing
see each port's `examples/basic_usage.*` (task-focused, not narrated).
The commands below were run against this repository while writing this
page; none of them are hypothetical.

We'll use Python as the running example. Every step has a one-line
equivalent for the other four ports in the callout boxes — the point of
this project is that the equivalent really is one-for-one.

## 1. Install a port

Pick whichever language matches your project. Each is self-contained:

```bash
cd python && pip install -e ".[dev]"       # + psycopg2/PyMySQL: pip install -e ".[postgres,mysql]"
```

> **Other ports:** `cd javascript && npm install` · `cd php && composer install` ·
> `cd go && go build ./...` (nothing to install — it's the standard library plus
> two drivers already in `go.mod`) · `cd c && make` (needs `sqlite3-devel`
> `postgresql-devel` `libmariadb-devel` `openssl-devel` or your distro's
> equivalents).

## 2. Connect with SQLite and model a small string set

No server needed yet. SQLite is what every port defaults to, so this is
the fastest way to see the shape of the data before worrying about a
real backend.

```python
from multilang import db_connector, retrieve_data, insert_data

conn = db_connector("sqlite", path="catalog.db")
```

Say we're localizing a small storefront: a welcome message, and a
"Checkout" button whose translation genuinely differs depending on
whether it's read as a verb (the button) or later reused as a page title
(a noun) — exactly the situation `context` exists for (see
[`schema.md`](schema.md#why-context-is-part-of-the-primary-key-not-just-metadata)).

```python
# Source strings (English), then translations. original_language="en"
# on each translation records what it was translated from, so staleness
# can be detected later if the English source changes.
insert_data(conn, "welcome.message", "en", "Welcome back!")
insert_data(conn, "welcome.message", "es", "¡Bienvenido de nuevo!", original_language="en")
insert_data(conn, "welcome.message", "fr", "Bon retour !", original_language="en")

insert_data(conn, "checkout", "en", "Checkout", context="button.label")
insert_data(conn, "checkout", "en", "Order Summary", context="page.title")
insert_data(conn, "checkout", "es", "Finalizar compra", context="button.label", original_language="en")
insert_data(conn, "checkout", "es", "Resumen del pedido", context="page.title", original_language="en")

print(retrieve_data(conn, "welcome.message", "es"))          # ¡Bienvenido de nuevo!
print(retrieve_data(conn, "checkout", "es", context="button.label"))  # Finalizar compra
print(retrieve_data(conn, "checkout", "fr", context="button.label"))  # None — no French translation yet
```

That last line matters as much as the first two: `retrieve_data` returns
`None` (or your language's equivalent — `null`, `nullopt`, a `found`
bool) for a row that doesn't exist yet, rather than raising. Missing
translations are an expected, normal state in a system like this, not an
error condition.

> **Other ports:** the calls above are `insertData(conn, ...)` /
> `retrieveData(conn, ...)` in JS, `Strings::insertData(...)` /
> `Strings::retrieveData(...)` in PHP, `InsertData(conn, ..., InsertOptions{...})` /
> `RetrieveData(conn, ...)` in Go, `ml_insert_data(...)` / `ml_retrieve_data(...)`
> in C. Argument order and how optional fields (`context`,
> `original_language`) are passed differs by language idiom; the data
> model and validation behavior are identical.

## 3. Find rows by content with `search_data`

Once there's more than a handful of rows, looking things up only by
exact key stops being enough — `search_data` finds rows by what's *in*
`content`, across three modes (`exact` substring, `natural`
whitespace-AND, `regex`). See [`search.md`](search.md) for the full
semantics; the short version for this storefront example:

```python
from multilang import search_data

for row in search_data(conn, "welcome", mode="natural", language_id="en"):
    print(row["string_id"], "->", row["content"])
# welcome.message -> Welcome back!
```

> **Other ports:** `searchData(conn, query, { mode, languageId, ... })`
> in JS, `Strings::searchData($conn, $query, $mode, $languageId, ...)`
> in PHP, `SearchData(conn, query, mode, SearchOptions{...})` in Go,
> `ml_search_data(conn, query, mode, &opts, ...)` in C. Unlike
> `retrieve_data`, `search_data` returns full rows (not content only),
> since a caller can't act on a match without knowing which key it
> belongs to.

## 4. Switch to a real Postgres/MySQL — same code, no changes

Bring up disposable local databases with the compose file this project
already ships:

```bash
cd conformance/docker
podman-compose up -d          # or: docker compose up -d
```

Wait for both to report healthy (`podman-compose ps` /
`docker compose ps`), then point the exact same code at Postgres instead
of SQLite by changing only the connection, not a single line of the
insert/retrieve calls above:

```bash
export MULTILANG_DB_BACKEND=postgres
export MULTILANG_DB_HOST=127.0.0.1
export MULTILANG_DB_PORT=5432
export MULTILANG_DB_USER=multilang
export MULTILANG_DB_PASSWORD=multilang
export MULTILANG_DB_NAME=multilang
```

```python
conn = db_connector()  # no arguments: everything comes from MULTILANG_DB_* above
insert_data(conn, "welcome.message", "en", "Welcome back!")
print(retrieve_data(conn, "welcome.message", "en"))  # Welcome back!
```

Swap `MULTILANG_DB_BACKEND=mysql` and `MULTILANG_DB_PORT=3306` and it's
the same story against MySQL — this was run against both while writing
this page, using the storefront example above verbatim. See
[`connectors.md`](connectors.md#tls-multilang_db_sslmode) if you need to
require TLS instead of the opportunistic default.

```bash
podman-compose down -v        # tear back down when you're done
```

## 5. Try the filesystem backend — no server, human-editable files

There's a fourth option besides the three databases above: a backend
that writes one `content.json` file per row under a root directory
instead of talking to a server. Useful when the translation set should
live in version control (diffable, reviewable in a PR) rather than a
database.

```python
conn = db_connector("filesystem", path="./strings")
insert_data(conn, "welcome.message", "en", "Welcome back!")
print(retrieve_data(conn, "welcome.message", "en"))  # Welcome back!
```

```bash
cat strings/en/welcome.message/@default/content.json
```

Same validation, same primary key, same conformance suite — see
[`connectors.md#the-filesystem-backend`](connectors.md#the-filesystem-backend)
for the exact on-disk layout.

## 6. Check your own integration against the conformance suite

The shared fixture at `conformance/cases.json` isn't just for this
project's own CI — it's also a smoke test you can point at *your own*
configured environment to confirm a given port behaves as documented
against it, before you build on top of it. If you've just stood up a
real Postgres/MySQL (per step 3) and want to confirm this port's
behavior against *your* server rather than the maintainers' CI:

```bash
cd python
MULTILANG_DB_BACKEND=postgres MULTILANG_DB_HOST=127.0.0.1 \
MULTILANG_DB_USER=multilang MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
PYTHONPATH=. pytest tests/test_conformance.py -v
```

If everything passes, your environment (driver versions, server version,
network path) reproduces the same behavior this project's own containers
do. If something you're building depends on a scenario the suite doesn't
cover yet — a specific validation edge case, a specific interaction
between `context` and `original_language` — the right move is to add a
case to `conformance/cases.json` and confirm it passes on every port, not
just yours; see [`extending.md`](extending.md#adding-a-conformance-case)
for the exact process. That keeps your finding as permanent regression
coverage instead of a one-off manual check you'd have to remember to
repeat.
