# multilang

[![CI](https://github.com/rmahique/multilang-lib/actions/workflows/ci.yml/badge.svg)](https://github.com/rmahique/multilang-lib/actions/workflows/ci.yml)
[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)

**[Usage examples for all five ports](https://rmahique.github.io/multilang-lib/)**

A small string/translation storage library, implemented identically in
five languages (Python, PHP, Go, JavaScript, C/C++) against three
database backends (SQLite, PostgreSQL, MySQL/MariaDB) — deliberately, so
that whichever language a caller's stack happens to be in, `retrieve_data`
for a given `(string_id, language_id)` returns the same thing. There's
also a serverless filesystem backend, implemented in all five ports (one
JSON file per row, see
[`docs/connectors.md#the-filesystem-backend`](docs/connectors.md#the-filesystem-backend))
for when the data should live in version control instead of a database.

```python
conn = db_connector("sqlite", path="strings.db")
insert_data(conn, "greeting", "en", "Hello world")
insert_data(conn, "greeting", "es", "Hola mundo", original_language="en")
retrieve_data(conn, "greeting", "es")  # -> "Hola mundo"
```

Every port exposes the same three functions and the same behavior:
`db_connector` (open a connection), `insert_data` (upsert one string),
`retrieve_data` (fetch one string, content only). See
[`docs/`](docs/) for how and why, and each language directory's own
`README.md` for that port's specific usage.

## Layout

| Path | What it is |
|---|---|
| `python/`, `php/`, `go/`, `javascript/`, `c/` | The five ports. Each is self-contained: its own README, tests, and native Debian/RPM packaging (`<port>/packaging/`). |
| `conformance/` | The shared, language-agnostic test fixture (`cases.json`) every port runs, plus the container-based infrastructure to run it against real Postgres/MySQL. |
| `docs/` | Design rationale, the validation rules, error types, the connector/credentials model, architecture diagrams, a guided tutorial, and how to extend the project (new port, new conformance case). |
| `website/` | The [usage-examples site](https://rmahique.github.io/multilang-lib/) (MkDocs + Material) deployed to GitHub Pages by `.github/workflows/pages.yml` — every code block on it is copied verbatim from a `<port>/examples/basic_usage.*` file, nothing hand-typed. |

New to the project? [`docs/tutorial.md`](docs/tutorial.md) is a guided
walkthrough; [`CONTRIBUTING.md`](CONTRIBUTING.md) covers what to do
before opening a PR. Working on this with an AI coding agent?
[`AGENTS.md`](AGENTS.md) is a condensed, file-path-level index meant to
save it from re-deriving the project layout by grepping around.

## Quick start

Each port works standalone with SQLite out of the box — no server needed.
The commands each port's own `README.md` documents (`pip install -e
".[dev]" && pytest tests/`, `npm install && npm test`, etc.) run directly
against your host toolchain and are fine for quickly iterating on a
single test while you're editing — but the tests that actually count
(what CI runs, what a PR is judged against) always run in a disposable
container, never on the host:

```bash
make test          # all five ports, each in a disposable container
make test-python    # just one (python|javascript|php|go|c)
```

See [`AGENTS.md`](AGENTS.md#tests-always-run-in-disposable-containers)
for why, and [`conformance/run-unit-tests.sh`](conformance/run-unit-tests.sh)
for how — needs docker or podman.

To check all five against real Postgres and MySQL (fully containerized —
see [`docs/conformance.md`](docs/conformance.md)):

```bash
cd conformance && ./run-live-db-tests.sh
# or: make conformance / make conformance-python
```

## Downloads

Every push and PR builds a native `.deb`/`.rpm` for all 5 ports across 4
distros (Debian bookworm, Fedora latest, openSUSE Leap 15, openSUSE
Tumbleweed) — see each port's own `packaging/README.md` for what's
shipped and how to build one yourself. For a stable download link
instead of a workflow-run artifact:

**[Latest GitHub Release](https://github.com/rmahique/multilang-lib/releases/latest)**
— every `.deb`/`.rpm` plus a matching `.sha512`, published whenever a
`vX.Y.Z` tag is pushed (see
[`.github/workflows/release.yml`](.github/workflows/release.yml)).
Filenames are prefixed with their language and distro, e.g.
`python-fedora-latest-python3-multilang-0.2.0-1.fc40.noarch.rpm`.

No tagged release yet, or want a package for the exact commit you're
looking at? Every push/PR also uploads the same packages as [workflow
run artifacts](https://github.com/rmahique/multilang-lib/actions/workflows/ci.yml)
(open a run → Artifacts, named `multilang-<language>-<distro>`) — these
expire per GitHub's retention policy and require being signed in to
download, unlike the Release assets above.

## License

GNU General Public License v3.0 or later — see [`LICENSE`](LICENSE).
