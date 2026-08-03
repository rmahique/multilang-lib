# The conformance suite

This is the mechanism that makes "same functionality and results across
every language and every database" an enforced property instead of an
aspiration. This page explains the design; see
[`../conformance/README.md`](../conformance/README.md) for the
operational how-to-run reference (including the container-based live-DB
testing setup and TLS configuration).

## The core idea: one fixture, not five test suites

`conformance/cases.json` is a language-agnostic list of test cases, each
a sequence of `retrieve_data`/`insert_data` calls with expected results:

```json
{
  "name": "context_disambiguates_same_string_id",
  "operations": [
    { "op": "insert_data", "args": { "string_id": "post", "language_id": "fr", "content": "Publier", "context": "button.publish" } },
    { "op": "insert_data", "args": { "string_id": "post", "language_id": "fr", "content": "Article", "context": "menu.item" } },
    { "op": "retrieve_data", "args": { "string_id": "post", "language_id": "fr", "context": "button.publish" }, "expect": "Publier" },
    { "op": "retrieve_data", "args": { "string_id": "post", "language_id": "fr", "context": "menu.item" }, "expect": "Article" }
  ]
}
```

Every port has a runner (`python/tests/test_conformance.py`,
`javascript/test/conformance.test.js`, `php/tests/ConformanceTest.php`,
`go/conformance_test.go`, `c/test/test_conformance.c` +
`c/test/test_cpp.cpp`) that **loads this same file** and executes it —
nobody hand-transcribes the cases into that language's native test
syntax. That's deliberate: a hand-copied case can drift from the
original without anyone noticing; a shared fixture can't drift from
itself. If a new behavior needs testing, it's added once, here, and
every port automatically inherits the obligation to satisfy it.

`expect` is the exact string `retrieve_data` must return, or JSON `null`
for "no row found." `expect_error: true` means the operation must raise
that language's `ValidationError` equivalent — the row must not be
written or returned.

## What it actually catches

Most of the current 61 cases exist because they were added *after* finding a
real cross-language discrepancy — length-boundary behavior, casing
normalization, BCP 47 edge shapes, and so on. See
[`validation.md`](validation.md#resolved-cross-language-inconsistencies)
for the four confirmed bugs this process found, and
[`../conformance/README.md`](../conformance/README.md#coverage) for the
full breakdown of what every case group pins down.

The methodology that mattered: don't just read five implementations side
by side and assume they agree — execute the same input against each
one's actual engine (regex library, length-counting, case-folding) and
compare real output. Several of the bugs above were invisible from
reading the code; they only showed up by actually running `"en-US\n"`
through Python's `re`, PHP's PCRE, JavaScript's regex engine, Go's RE2,
and C's POSIX ERE, and noticing two of the five disagreed.

## Backends: SQLite/filesystem locally, Postgres/MySQL in CI

By default every runner exercises SQLite (a fresh temp file per case —
cheap, no server needed, what you get from `pytest`/`npm test`/etc. with
no special setup). Setting `MULTILANG_DB_BACKEND=filesystem` (plus
`MULTILANG_DB_PATH`) runs the identical suite against a fresh temp
directory per case instead — same no-server cheapness as SQLite, just a
different on-disk shape (see
[`connectors.md#the-filesystem-backend`](connectors.md#the-filesystem-backend)).
Setting `MULTILANG_DB_BACKEND=postgres` or `=mysql` (plus the matching
`MULTILANG_DB_HOST`/`_PORT`/`_USER`/`_PASSWORD`/`_NAME` — see
[`connectors.md`](connectors.md)) runs the *identical* suite against a
real server instead. Since Postgres/MySQL share one long-lived server
across a whole run rather than getting a fresh file per case, isolation
there comes from truncating the `strings` table before each case, not
from a fresh database — cheaper than provisioning one per case, and just
as isolating since every case still starts from zero rows.

`conformance/run-live-db-tests.sh` automates this: containerized
Postgres/MySQL, one container per language (each running its suite
against both databases internally), database versions as variables, no
dependency on the host having any of the five languages installed at
all. `conformance/run-unit-tests.sh` does the equivalent for SQLite/
filesystem — same disposable-container principle, just without any
database container since neither backend needs a server. Both are what
`make test`/`make conformance` actually run; see
[`../conformance/README.md`](../conformance/README.md) for the full
walkthrough and [`../AGENTS.md`](../AGENTS.md#tests-always-run-in-disposable-containers)
for why no test suite in this project runs directly against a host
toolchain, unit tests included.
