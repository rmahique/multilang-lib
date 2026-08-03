# Contributing

## Adding a conformance case or a new language port

Both have a concrete, step-by-step checklist already — see
[`docs/extending.md`](docs/extending.md). Read it before writing code;
it's based on what actually happened building the existing five ports,
not a theoretical plan.

## Before opening a PR

```bash
make test          # every port's unit tests, each in a disposable container
make conformance    # optional but recommended for anything touching validation,
                     # backends, or connectors -- needs docker or podman
```

Both need docker or podman — see the next section.

## Tests always run in disposable containers

Never run a port's test suite directly against your host's Python/Node/
PHP/Go/C toolchain — `make test`/`make test-<lang>` build and run each
port's tests inside a `--rm` container (see
`conformance/run-unit-tests.sh`), and `make conformance` does the same
against real Postgres/MySQL containers (`conformance/run-live-db-tests.sh`).
Every container is single-use: built or reused from cache, run once, and
destroyed on exit whether the run passed or failed. This is not just for
the live-DB suite — it's every test run, including the fast SQLite/
filesystem unit tests. See [`AGENTS.md`](AGENTS.md#tests-always-run-in-disposable-containers)
for the reasoning.

If you're touching validation rules, field limits, or lowercasing
behavior specifically: read [`docs/validation.md`](docs/validation.md)
first. Every rule documented there was arrived at because a
cross-language inconsistency was actually found and fixed — see
[`docs/validation.md#resolved-cross-language-inconsistencies`](docs/validation.md#resolved-cross-language-inconsistencies)
before assuming a "quick fix" in one port doesn't need the same fix in
the other four.

## The one rule that matters most

**A behavior is only correct if it's identical in all five ports against
every backend it supports (three databases plus the filesystem
backend).** If you find or suspect a divergence, the fix is
almost never "pick whichever one is convenient" — see
[`docs/extending.md#adding-a-conformance-case`](docs/extending.md#adding-a-conformance-case)
for how to pin it down with a real conformance case before touching any
port's code.

## Update the docs in the same PR, not a follow-up

If a PR adds a backend or port, changes a validation rule, a schema
column, an env var, or a public function signature, it should also
update whatever in `docs/`, the affected `README.md`(s), `AGENTS.md`,
`CHANGELOG.md`, and `examples/basic_usage.*` that change makes wrong or
incomplete. A stale doc is trusted by default and actively misleads —
worse than no doc at all. Reviewers should treat an undocumented
behavior change the same as an untested one.

## License

By contributing, you agree your changes are licensed under the GNU
General Public License v3.0 or later, same as the rest of this project
— see [`LICENSE`](LICENSE).
