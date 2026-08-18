# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project isn't tagged/released anywhere yet — all five ports are at
`0.1.0`; this file starts tracking from here forward.

## [Unreleased]

### Added

- `search_data` — a third public function (fourth including
  `db_connector`) in all five ports: finds rows by `content` instead of
  by exact key, across three modes (`exact` literal substring, `natural`
  whitespace-split AND matching, `regex` in each language's native regex
  engine). Matching runs entirely in-process after a new
  `select_rows(language_id?, context?, status?)` backend method — one
  simple method per backend (20 total, mirroring `select_content`) rather
  than a bespoke search implementation per language-×-backend pair — so
  results are guaranteed identical across SQLite/Postgres/MySQL/
  filesystem, never dependent on a native engine's own full-text/regex
  behavior (Postgres `tsvector`, MySQL `FULLTEXT`, etc. were deliberately
  not used, for exactly that reason). Optional `language_id`/`context`/
  `status` exact-match filters, ASCII-only case folding by default
  (documented limitation, same precedent as identifier lowercasing),
  score-descending/`(language_id, string_id, context)`-ascending result
  ordering, and 1–500 `limit`/`offset` pagination. See
  [`docs/search.md`](docs/search.md) for the full design and its
  trade-offs, and 11 new `conformance/cases.json` cases covering every
  mode, filters, pagination, ordering, and the two new validation-error
  triggers (bad `mode`, invalid regex).
- Native Debian/RPM packaging for all five ports (`<port>/packaging/`),
  covering Debian bookworm, Fedora latest, openSUSE Leap 15, and openSUSE
  Tumbleweed — 20 CI jobs total, each producing a `.deb`/`.rpm` plus a
  `.sha512` checksum. Go, JavaScript, and PHP ship source-only packages;
  C/C++ ships a real compiled shared-library split (`libmultilang0` +
  `libmultilang-dev`/`-devel`, built via new `install`/`install-runtime`/
  `install-devel` targets in `c/Makefile`). Runtime/build dependency names
  are resolved automatically per distro (`${shlibs:Depends}`,
  `find-requires`, `pkgconfig(X)` BuildRequires) rather than hardcoded,
  since they differ across distro families. Version is computed by the
  shared `scripts/compute-version.sh` (exact git tag if released,
  otherwise `<latest tag or manifest version>+<date>`). Each combo builds
  inside a container for its own target distro via a Dockerfile at
  `<port>/packaging/docker/Dockerfile.<distro>` (20 total) with every
  build dependency baked in, matching the existing
  `Dockerfile.conformance` pattern rather than installing packages at
  runtime into a bare image.
- SLES 16 added as a fifth packaging distro (25 CI jobs total, up from
  20) for all five ports, via `<port>/packaging/docker/Dockerfile.sles-16`
  built `FROM registry.suse.com/bci/bci-base:16.0` — SUSE's free,
  anonymously-pullable Base Container Image, pre-configured with the
  `SLE_BCI` repo so `zypper install` needs no SCC registration/
  subscription. No spec/control file changes needed: the existing
  `%if 0%{?suse_version}` branches already cover it, and it follows the
  same no-`MULTILANG_LEAP15_PYTHON_WORKAROUND` path as openSUSE
  Tumbleweed, since SLES 16 ships a current default `python3` (3.13).
- `website/` — a usage-examples site (MkDocs + Material) covering all
  five ports, deployed to GitHub Pages by
  `.github/workflows/pages.yml` on every push to main that touches
  `website/` or an `examples/` directory. Every code block on it is
  copied verbatim from that port's `examples/basic_usage.*`, not
  hand-typed. Requires a one-time repo setting (Settings → Pages →
  Source: GitHub Actions) before the first deploy will publish.
- `.github/workflows/release.yml` — on a pushed version tag (this repo's
  own convention, no `v` prefix: `1.0`, `1.0rc1`, etc.), builds the
  same 20 packages (via the new `build-packages.yml` reusable workflow,
  shared with `ci.yml` so the matrix can't drift between the two) and
  attaches them to a GitHub Release, giving `README.md`'s Downloads
  section a stable `.../releases/latest` link instead of an expiring
  workflow-run artifact.
- `conformance/run-unit-tests.sh` — runs each port's unit tests
  (SQLite/filesystem, no live DB needed) inside a disposable `--rm`
  container, reusing the existing `Dockerfile.conformance` images with
  their live-DB `ENTRYPOINT` overridden. `make test`/`make test-<lang>`
  now call this instead of installing/running directly on the host.
  Codifies a permanent rule (`AGENTS.md`, `CONTRIBUTING.md`): no test
  suite in this project runs directly against a host toolchain, unit
  tests included — only the live-DB conformance suite had that guarantee
  before.

- Filesystem backend (`"filesystem"`), in all five ports: one
  `content.json` file per row under
  `<root>/<language_id>/<string_id>/<context>/`, no server or driver
  required. Passes the full conformance suite in every port.
- 16 conformance cases covering non-Latin scripts and the most-spoken /
  trickiest-to-get-wrong languages: Arabic (RTL, plus RTL text mixed
  with Western digits — a bidi edge case), Hindi (Devanagari conjunct
  consonants), Russian (Cyrillic), Bengali, Japanese (mixed hiragana/
  katakana/kanji), Korean (Hangul), Thai (no whitespace between words),
  Vietnamese (multiple stacked diacritics per character), Turkish
  content containing dotted/dotless İ/ı (confirms `content` is never
  touched by the Turkish-locale lowercasing bug documented in
  `docs/validation.md`, since only `language_id`/`string_id`/`context`
  are lowercased), a Simplified Chinese translation of an Arabic source
  (exercises the checksum/staleness path with non-Latin content on both
  ends), a ZWJ emoji sequence, a real non-Latin-script BCP 47 region tag
  (`ar-SA`), and rejection of non-ASCII `string_id`/`context` values
  (Arabic, Han, Japanese) — previously the only non-Latin coverage was
  one case with Chinese text and emoji, no other scripts or language
  tags at all.

### Changed

- `string_id`/`context` now reject the literal values `.` and `..` in
  all five ports (shared `validation.py`/`.go`/`.js`/`.php`/`.c`).
  Both previously matched the identifier regex but, discovered while
  building the filesystem backend, `..` as a `string_id` collapsed the
  directory path back up a level — different `language_id`s silently
  wrote to the same file instead of separate rows. The fix is in shared
  validation, not the filesystem backend, so the DB backends reject the
  same input too rather than the two behaving differently by accident.
  5 new conformance cases cover it.
- CI: GitHub Actions (`.github/workflows/ci.yml`) and GitLab CI
  (`.gitlab-ci.yml`), each with a fast SQLite-only unit-test stage on
  every push/PR and a slower live-DB conformance stage against real
  Postgres/MySQL, gated to PRs/main/schedule.
- `scripts/set-go-module-path.sh` — derives the Go module path from the
  repo's git remote instead of it being hand-edited; a CI job in both
  pipelines checks it hasn't drifted.
- `docs/errors.md` — how each port surfaces validation vs. database
  errors, with the actual type names side by side.
- `docs/tutorial.md` and `docs/architecture.md` — guided walkthrough and
  Mermaid diagrams, in addition to the existing design-rationale docs.
- Root `Makefile` and `CONTRIBUTING.md`.
- Runnable `examples/basic_usage.*` in all five ports.

## [0.1.0] — initial implementation

- The `strings` table schema (composite key `language_id` + `string_id`
  + `context`), implemented identically in Python, JavaScript, PHP, Go,
  and C/C++, against SQLite, PostgreSQL, and MySQL/MariaDB.
- Allow-list input validation, ASCII-only locale-independent lowercasing,
  UTF-8 byte-length limits — see `docs/validation.md` for the rules and
  the real cross-language bugs found while converging on them.
- `MULTILANG_DB_SSLMODE` (`prefer`/`require`/`disable`) for both
  Postgres and MySQL backends.
- The shared conformance fixture (`conformance/cases.json`) and a
  runner in every port, plus containerized live-DB testing
  (`conformance/run-live-db-tests.sh`) with versions exposed as
  variables and no reliance on DNS between containers.
- Debian/Fedora/openSUSE packaging for the Python port.
- Design documentation under `docs/`.
