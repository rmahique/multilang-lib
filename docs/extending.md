# Adding a language port or a conformance case

## Adding a conformance case

Almost always the right move before touching any port's code — a new
case is how you *prove* a behavior is (or isn't) consistent across every
language, and it stays as regression coverage afterward.

1. Add a case to `conformance/cases.json` (see
   [`conformance.md`](conformance.md) for the format). Pick a name that
   describes what it pins down, not just "test5".
2. Run every port's conformance runner against it:
   ```bash
   cd python && PYTHONPATH=. pytest tests/test_conformance.py -k <case_name> -v
   cd javascript && node --test test/conformance.test.js
   cd php && vendor/bin/phpunit --filter ConformanceTest
   cd go && go test -run TestConformance ./...
   cd c && make test
   ```
3. If any port disagrees with another, that's a real finding — fix the
   port(s) that are wrong, don't weaken the case to match whichever
   answer happens to be convenient. See
   [`validation.md#resolved-cross-language-inconsistencies`](validation.md#resolved-cross-language-inconsistencies)
   for what this process has already caught; the bugs found there were
   real, shipped, and invisible until actually executed side by side.
4. If the case needs to run against Postgres/MySQL too (most do, since
   the whole point is cross-backend parity), it needs no special
   handling — every runner already supports `MULTILANG_DB_BACKEND`, and
   `conformance/run-live-db-tests.sh` exercises every case against real
   servers automatically.

## Adding a new language port

This project has done this five times; the checklist below is what
actually happened each time, not a theoretical plan.

1. **Read [`schema.md`](schema.md) and [`validation.md`](validation.md)
   first.** The schema and validation rules are not per-language
   decisions — a sixth port needs to implement exactly what's documented
   there, not redesign it. In particular: BCP 47 validation with the
   35-character cap, the `[A-Za-z0-9._:-]` identifier charset, UTF-8
   *byte* length limits for `content`/`updated_by` (not codepoints, not
   UTF-16 units — see [`schema.md`](schema.md) for why this specific
   point already caused real bugs), lowercasing every id-shaped field
   with locale-independent ASCII-only logic (not `tolower()`/
   `strtolower()` or any function that consults process locale).
2. **Structure it the same way the other five are structured**: a
   validation module, a backend interface/abstraction (one
   implementation per database), a connector that turns
   `MULTILANG_DB_*` env vars + explicit credentials into a connection
   (see [`connectors.md`](connectors.md)), and the two public functions
   built on top. Look at whichever existing port is closest to the new
   language's idioms as a template — Go's `Backend` interface and C's
   `ml_backend_vtable` are the same design in different clothing.
3. **Implement SQLite first**, get its own unit tests passing, *then*
   wire up the conformance runner against `conformance/cases.json` (see
   the existing five runners for the pattern — they're all short: load
   the fixture, dispatch `retrieve_data`/`insert_data` calls, assert
   `expect`/`expect_error`). Passing every existing case with zero
   modifications to `cases.json` is the actual bar for "this port is
   correct," not a finished-when-it-compiles judgment call.
4. **Implement Postgres and MySQL**, including the `MULTILANG_DB_SSLMODE`
   `prefer`/`require`/`disable` behavior described in
   [`connectors.md`](connectors.md). Check whether the new language's
   native driver supports opportunistic TLS (`prefer`) directly — if not,
   look at the JavaScript or PHP MySQL backend for the
   try-TLS-then-fall-back-to-plaintext pattern used to implement it
   without native support.
5. **Add a `Dockerfile.conformance`** in the new port's directory,
   modeled on the other five (see
   [`../conformance/README.md`](../conformance/README.md)) — pin the
   language runtime version via a build `ARG` with a sensible default,
   never hardcode it. Add the port's name to `ALL_PORTS` in
   `conformance/run-live-db-tests.sh`.
6. **Run the full live-DB suite**
   (`conformance/run-live-db-tests.sh <new-port>`) against real
   Postgres and MySQL before considering the port done — SQLite-only
   passing is necessary but not sufficient; several of the resolved
   inconsistencies in [`validation.md`](validation.md) were byte-length
   and TLS issues that only a real server run would surface.
7. **Write that port's own `README.md`** covering its specific usage
   (installation, the three functions' signatures in that language's
   idiom, credentials) — the top-level docs here are shared design
   rationale, not a substitute for per-port usage docs.
