# Conformance suite

`cases.json` is the single source of truth for "same functionality and
results across every language and every backend (SQLite, Postgres,
MySQL, filesystem)." It is not
duplicated by hand per language — every port reads this same file and runs
it, so the suite itself can't drift out of sync between implementations.

## Format

```json
{
  "cases": [
    {
      "name": "case_name",
      "operations": [
        { "op": "insert_data", "args": { ... } },
        { "op": "retrieve_data", "args": { ... }, "expect": "value or null" },
        { "op": "insert_data", "args": { ... }, "expect_error": true }
      ]
    }
  ]
}
```

- Each case runs its operations **in order** against one fresh
  connection/table (SQLite: a temp file; Postgres/MySQL: a throwaway
  schema/database), so cases never see each other's data.
- `args` map 1:1 to `retrieve_data`/`insert_data` parameter names
  (`string_id`, `language_id`, `content`, `context`, `original_language`,
  `status`, `updated_by`).
- `expect` on a `retrieve_data` op is the exact string returned, or JSON
  `null` for "no row found."
- `expect_error: true` means the operation must raise/return that
  language's equivalent of `ValidationError` — the row must NOT be written
  or returned.

## Requirement

Every language implementation's test suite must:
1. Load `cases.json` (not hand-copy it).
2. Run every case against every backend that implementation supports.
3. Fail the build if any case's actual result doesn't match `expect`/
   `expect_error`.

See `python/tests/test_conformance.py` for the reference runner.

## Running the unit tests (SQLite/filesystem) — always in a container

```bash
./run-unit-tests.sh                 # all 5 ports
./run-unit-tests.sh python go       # only these ports
```

Same principle as the live-DB run below, applied to the fast local suite
too: every test run — including SQLite/filesystem, which need no server
— happens inside a disposable `--rm` container, never directly against
the host's Python/Node/PHP/Go/C toolchain. It reuses each port's
existing `Dockerfile.conformance` image (already has every dependency
installed) with its live-DB-only `ENTRYPOINT` overridden to run that
port's plain unit test command instead. `make test`/`make test-<lang>`
call this script; see [`AGENTS.md`](../AGENTS.md#tests-always-run-in-disposable-containers)
for why this isn't optional.

## Running against real Postgres/MySQL

```bash
./run-live-db-tests.sh                    # all 5 ports, both backends
./run-live-db-tests.sh python go           # just these
POSTGRES_VERSION=15 MYSQL_VERSION=5.7 ./run-live-db-tests.sh
PYTHON_VERSION=3.12 ./run-live-db-tests.sh python
```

Everything runs in containers — the databases, and each language's test
run — never against a host-installed toolchain. `run-live-db-tests.sh`:

1. Starts disposable Postgres and MySQL containers on a private container
   network (no host ports published).
2. Builds one image per language from `<lang>/Dockerfile.conformance`
   (build context is the repo root, since each needs sibling access to
   `conformance/`).
3. Runs **one container per language** — not one per (language, backend)
   pair. Inside that single container, `docker/run-both-backends.sh` (the
   image's `ENTRYPOINT`) runs the suite against Postgres, then against
   MySQL, addressing each database by its container IP on the shared
   network.
4. Tears down all containers and the network on exit, success or failure.

Every version this depends on is a variable with a default, never
hardcoded — database versions (`POSTGRES_VERSION`, `MYSQL_VERSION`) and
each language's runtime/base-image version (`PYTHON_VERSION`,
`NODE_VERSION`, `PHP_VERSION`, `GO_VERSION`, `DEBIAN_VERSION` for the
C/C++ image) — so testing a different version is a variable override, not
a file edit. Container engine is auto-detected (`docker`, falling back to
`podman`) or forced with `CONTAINER_ENGINE`.

`docker/docker-compose.yml` is an optional convenience for just running
the two databases locally (e.g. to poke at them with `psql`/`mysql`
directly) if you already have compose tooling — it is not what CI uses;
`run-live-db-tests.sh` has no dependency on either compose binary.

## TLS

Every port's Postgres and MySQL backends share the same posture,
controlled by `MULTILANG_DB_SSLMODE`:

- **`prefer` (default)** — attempt TLS, fall back to plaintext if the
  server doesn't offer it. A plain localhost/container database has no
  certificate to negotiate and shouldn't be required to have one.
- **`require`** — attempt TLS, fail the connection instead of falling
  back if the server didn't actually negotiate it.
- **`disable`** — plaintext only, no TLS attempt.

libpq (Postgres, used directly by Python/Go/C, and indirectly by PHP's
PDO_PGSQL) has this exact three-way mode natively. MySQL client libraries
generally don't — go-sql-driver/mysql's `tls=preferred` and PyMySQL's
default (no `ssl_disabled`/no explicit `ssl_*` options) both implement it
natively too, but node-postgres, mysql2, and PDO_MYSQL only support "try"
or "don't" — so their backends implement `prefer` themselves: attempt a
TLS connection first, and only fall back to a plaintext one if that
attempt fails. None of this changes what's encrypted or verified in
`require` mode: like every backend's `require`, it's "encrypt, don't
verify the certificate" — appropriate for a self-signed cert on a
container/localhost server, not a substitute for real CA verification in
production.

`run-live-db-tests.sh` doesn't set `MULTILANG_DB_SSLMODE` at all — the
default `prefer` is exercised as-is against the test containers (both of
which do offer TLS out of the box), so the live-DB run is itself a
standing check that `prefer` mode actually works end to end, not just
that plaintext still does.

## Coverage

61 cases as of the last update, grouped by what they pin down:
- Basic insert/retrieve/upsert semantics
- Case-insensitivity of every id-shaped field (`language_id`, `string_id`,
  `context`), including that a translation's `original_language` still
  matches a source row inserted under a differently-cased tag
- `context` disambiguation, including that an unknown context returns no
  row even when the same `string_id` exists under a different context
- Length boundaries at exactly the limit and one over it, for
  `string_id`, `context`, and `content` (`MAX_STRING_ID_LEN=200`,
  `MAX_CONTEXT_LEN=200`, `MAX_CONTENT_LEN=65535`)
- `content` round-trips exactly: Unicode (multi-byte text and emoji), SQL
  metacharacters (quotes, `;`, `--`, `%`, `_`, backslash — proof every
  port uses parameterized queries, never string-built SQL), mixed case
  (content is *not* lowercased, unlike the id-shaped columns), and
  whitespace-only content
- Non-Latin scripts specifically, covering the most-spoken languages and
  the scripts most likely to break a naive implementation: Arabic
  (right-to-left, plus a case mixing RTL text with embedded Western
  digits — a classic bidi edge case), Hindi (Devanagari conjunct
  consonants), Russian (Cyrillic), Bengali, Japanese (hiragana/katakana/
  kanji mixed in one string), Korean (Hangul), Simplified Chinese, Thai
  (no whitespace between words — nothing in this library assumes word
  boundaries, but it's an easy thing to get wrong if content is ever
  touched instead of stored verbatim), Vietnamese (Latin script but
  multiple stacked diacritics per character — a real Unicode
  normalization footgun), Turkish content containing dotted/dotless
  İ/ı (content is *never* lowercased, unlike `language_id`, so this
  confirms the Turkish-locale lowercasing bug documented in
  [`validation.md`](../docs/validation.md#resolved-cross-language-inconsistencies)
  can't resurface via `content`), and a ZWJ emoji sequence (a "single"
  emoji that's actually several codepoints joined by U+200D) — all as
  `content`, plus a real `language_id`/`original_language` pair
  (`ar` source, `zh-Hans` translation) so the checksum/staleness path
  gets exercised with non-Latin content too, not just `content`'s own
  round-trip
- BCP 47 edge shapes: a numeric region (`es-419`), a multi-variant tag
  (`sl-rozaj-biske`), a real non-Latin-script language's region subtag
  (`ar-SA`), and malformed tags (trailing/double hyphen)
- `string_id`/`context` reject non-Latin scripts the same as any other
  out-of-charset input (Arabic, Han, Japanese) — the identifier charset
  (`^[A-Za-z0-9._:-]+$`) is ASCII-only by design, so this is expected
  rejection, not a script-support gap; only `content` is unrestricted
- Validation rejection for every out-of-shape input, on both
  `retrieve_data` and `insert_data`

## Resolved cross-language inconsistencies

Four real behavioral differences were found by auditing each port's
validation code side by side, then confirmed by executing the exact same
input against each language's regex/length-check/case-folding engine
directly (not just by reasoning about it). All four are now fixed and
covered here so they can't silently regress.

**1. `content` and `updated_by` length limits measured in different
units.** Both fields have no charset restriction, so their length limits
(`MAX_CONTENT_LEN=65535`, `MAX_UPDATED_BY_LEN=200`) are the only place a
counting-unit mismatch can actually bite (`string_id`/`context`/
`language_id` are ASCII-only by charset, so codepoints == UTF-16 units ==
bytes for them regardless). PHP (`strlen`), Go (`len(string)`), and C
(`strlen`) measure bytes natively; Python (`len()`) and JavaScript
(`.length`) measured codepoints and UTF-16 code units respectively.
Switched both to explicit UTF-8 byte counting
(`len(value.encode("utf-8"))` / `Buffer.byteLength(value, 'utf8')`), since
bytes are what the database columns actually store and the one unit every
port can measure identically. `content_at_max_length_multibyte` /
`content_exceeds_max_length_multibyte` and
`updated_by_at_max_length_multibyte` /
`updated_by_exceeds_max_length_multibyte` use `€` (U+20AC — 1 codepoint, 1
UTF-16 unit, 3 UTF-8 bytes) sized so the old codepoint/UTF-16 counting
would have accepted up to 3x more than PHP/Go/C did for the same input.

**2. No length cap on `language_id`.** The BCP47 pattern's variant-subtag
group (`(-[A-Za-z0-9]{5,8})*`) repeats with no upper bound, so a
regex-only check accepts arbitrarily long tags. C already had an implicit
cap because it validates into a fixed-size stack buffer
(`ML_MAX_LANGUAGE_ID_LEN=35`) — every other port had no cap at all, so a
38+ character tag was silently accepted by Python/JS/PHP/Go but rejected
by C. Added an explicit `MAX_LANGUAGE_ID_LEN=35` check to every port,
matching C's existing limit. `language_id_at_max_length` /
`language_id_exceeds_max_length` cover the boundary.

**3. Regex `$` anchor accepts a trailing newline in Python and PHP.**
Perl-derived regex engines (Python `re`, PCRE without the `D` modifier)
let `$` match just before a trailing `"\n"`, not only at the true end of
string; POSIX ERE (C) and RE2 (Go) and JavaScript's regex engine don't
special-case this. So `"en-US\n"` passed validation in Python and PHP but
was correctly rejected everywhere else — and since nothing strips the
newline, the row would be stored with an untrimmed tag unreachable by any
other port. Fixed by switching Python to `re.fullmatch` (requires
consuming the whole string, independent of `$` semantics) and adding
PCRE's `D` modifier (`PCRE_DOLLAR_ENDONLY`) to both PHP patterns.
`invalid_language_id_trailing_newline` and
`invalid_string_id_trailing_newline` cover this.

**4. Locale-dependent lowercasing in C and PHP.** `tolower()` (C) and
`strtolower()` (PHP) are both sensitive to the process's `LC_CTYPE`
locale. Confirmed live: under `tr_TR.utf8`, `tolower('I')` and
`strtolower("I")` both return `'I'` unchanged instead of `'i'` — Turkish
distinguishes dotted/dotless i, and neither function can represent the
non-ASCII result in a single byte, so glibc declines to convert at all.
Python (`str.lower()`), JavaScript (`.toLowerCase()`), and Go
(`strings.ToLower`) all use fixed Unicode case-folding tables and are
unaffected by process locale — confirmed the same way. This is a
genuinely dangerous one for a library whose entire job is
internationalization: a PHP or C service deployed under a Turkish (or
similarly-affected) locale would silently fail to normalize
`"EN"`/`"INFO"`-shaped ids containing `I`, breaking the case-insensitive
primary-key dedup guarantee specifically for those rows, while every
other port on the same data keeps working normally. Fixed by replacing
both with explicit, locale-independent ASCII-range mappings (C: manual
`'A'..'Z'` → `+32` loop; PHP: `strtr()` with a fixed 26-letter
translation table — `strtr()` does literal byte substitution and is
never locale-aware).
This can't be expressed as a `cases.json` case (it depends on process
locale state, which the shared fixture has no way to set), so each
affected port instead got a native unit test
(`test/test_validation.c::test_lowercasing_is_locale_independent`,
`tests/ValidationTest.php::testLowercasingIsLocaleIndependent`) that
switches `LC_CTYPE` to `tr_TR.utf8` for the duration of one assertion and
restores it afterward; Python got the same test as insurance even though
`str.lower()` is documented as locale-independent, since it does have
mutable global locale state that some future change could accidentally
route through.

## Adding a case

Add it once here, not per language. If a new case requires a behavior no
port implements yet, that's a real gap — fix the ports, don't weaken the
case.
