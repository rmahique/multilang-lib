# Design documentation

This is the *why*, shared across all five language ports. For *how to
use* a specific port, see that port's own `README.md`
(`../python/README.md`, `../javascript/README.md`, etc.) — these pages
deliberately don't repeat per-language installation/usage instructions.

- **[`schema.md`](schema.md)** — the `strings` table, why it's one table
  and not a base/translations split, why `context` is part of the
  primary key, how `original_language`/`source_checksum` support
  staleness detection, and why field length limits are measured in the
  units they are.
- **[`validation.md`](validation.md)** — the allow-list rules every port
  enforces before a value reaches SQL, and the four real cross-language
  bugs this project's conformance process has found and fixed.
- **[`conformance.md`](conformance.md)** — how one shared fixture
  (`conformance/cases.json`) enforces identical behavior across five
  languages and four backend families (three databases plus the
  filesystem backend), and why that's the actual enforcement mechanism
  rather than a documentation claim. See
  [`../conformance/README.md`](../conformance/README.md) for the
  operational reference (running it, live-DB containers, TLS).
- **[`connectors.md`](connectors.md)** — the `db_connector`/
  `retrieve_data`/`insert_data` shape every port exposes, the
  `MULTILANG_DB_*` credential/environment-variable model, and the
  `MULTILANG_DB_SSLMODE` TLS behavior (and why it's implemented
  differently under the hood for MySQL than for Postgres, while behaving
  identically from the caller's side).
- **[`search.md`](search.md)** — `search_data`'s exact/natural/regex
  modes, why matching runs in-process instead of delegating to each
  backend's own query engine (the guarantee that decision buys, and what
  it costs), and the ASCII-only case-folding/regex-flavor limitations
  that follow from it.
- **[`errors.md`](errors.md)** — how each port surfaces validation vs.
  database failures (a table of the actual exception/error types), and
  why only C/C++ wrap database errors while the other four let the
  native driver's exception propagate.
- **[`extending.md`](extending.md)** — the concrete checklist for adding
  a new conformance case or a sixth language port, based on what
  actually happened building the existing five.
- **[`tutorial.md`](tutorial.md)** — a guided walkthrough: install a port,
  model a small translated string set against SQLite, switch to a real
  Postgres/MySQL via the bundled `docker-compose.yml` with no code
  changes, then check your own integration against the conformance
  suite. Start here if you're new; use the pages above once you need the
  *why* behind something it glosses over.
- **[`architecture.md`](architecture.md)** — four Mermaid diagrams: the
  layers inside a single port, how one fixture drives all five ports and
  four backend families, the containerized live-DB test topology, and
  where the library sits relative to a consuming application's own code.
