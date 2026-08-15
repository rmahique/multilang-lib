# `search_data`: finding rows without knowing their key

`retrieve_data`/`insert_data` require the caller to already know
`(language_id, string_id, context)`. `search_data` is the third public
function — it searches `content` and returns whichever rows matched,
across three modes: `exact`, `natural`, and `regex`.

## The one architectural decision that matters here

Every other database-shaped feature in this project pushes work down
into SQL. `search_data` deliberately does not: matching runs entirely
**in-process**, in shared code inside `search_data` itself, after
fetching candidate rows from the backend through one new, simple
`select_rows(language_id?, context?, status?)` method — a plain
`SELECT * WHERE ...` for the three SQL backends, a directory walk for
filesystem. No `SELECT` in any backend does content matching.

That was a real trade-off, not an oversight:

- **Native engines disagree with each other.** Postgres's `tsvector`
  stems words and ranks by a formula; MySQL's `FULLTEXT` has a 4-character
  minimum word length and its own stopword list; SQLite has no built-in
  regex or full-text search at all; the filesystem backend has no query
  engine full stop. Pushing matching into each engine would mean the same
  `search_data` call could return different rows depending on which
  backend happened to be configured — breaking this project's one rule
  that matters most (see [`conformance.md`](conformance.md)).
- **In-process scanning is identical by construction.** Because the exact
  same matching function runs after `select_rows` on every backend, the
  conformance suite can assert one expected result set per case and know
  it holds for SQLite, Postgres, MySQL, and filesystem alike — see the
  `search_*` cases in `conformance/cases.json`.
- **The cost is a full scan** of whatever `select_rows` returns (after its
  cheap, indexed `language_id`/`context`/`status` filtering) rather than
  an index-accelerated search. Acceptable at this library's target scale;
  revisit only if a real workload's data size makes it not be.

## The three modes

`content` is **never tokenized** in any mode — only the caller's `query`
is, and only in `natural` mode. This sidesteps Unicode word-boundary
ambiguity entirely: Thai, Chinese, and Japanese content has no
whitespace between words, so tokenizing content itself would be
guesswork. Every mode instead asks "does this substring/pattern appear
in `content`," which is well-defined regardless of script.

- **`exact`** — `query` must appear as a literal substring of `content`.
  Score (used for ranking, see below) is the count of non-overlapping
  occurrences. Implemented as a plain string search
  (`str.find`/`strstr`/`strings.Index`/`strpos`/`String.includes`) — no
  regex engine, no backtracking, no ReDoS surface.
- **`natural`** (the default) — `query` is split on runs of ASCII
  whitespace into terms. A row matches only if **every** term appears
  somewhere in `content` — AND, not OR, and not ranked by relevance in
  the full-text-search sense (no stemming, no stopwords). Score is the
  sum of each term's occurrence count. Still plain substring search per
  term. A query that produces zero terms after splitting (e.g. all
  whitespace) is a validation error, not an empty match.
- **`regex`** — `query` is a pattern in that language's own native regex
  engine: Python `re`, Go `RE2`, JavaScript `RegExp`, PCRE via PHP (in
  UTF-8 mode — see below), POSIX extended regular expressions via
  `<regex.h>` for C (a zero-new-dependency choice: glibc-native on every
  distro this project packages for). Score is the count of non-overlapping
  matches. An invalid pattern is a validation error with that engine's
  own compile error folded into the message.

Regex is the one place cross-*language* differences are expected and
already documented territory for this project (see the `$`-anchor
difference in [`validation.md`](validation.md)) — `^cat` behaves the same
in all five engines, but more exotic PCRE/RE2/POSIX-specific syntax will
not. Cross-*backend* behavior for a single language stays identical
regardless, since matching never leaves that language's own process.

## Case sensitivity: ASCII-only fold, not full Unicode

`case_sensitive` defaults to `false`. For `exact`/`natural`, that means
folding only the ASCII `A-Z` range on both `query` and `content` before
comparing — the same rule [`validation.md`](validation.md) already
applies to identifiers, for the same reason: C has no Unicode-aware
lowercasing in its standard library, and this project has already found
a real bug (`tolower()`/`strtolower()` under a Turkish locale) from
trusting a language's own "smart" case folding. Non-ASCII letters (`É`
vs `é`, dotted/dotless İ/ı) match by exact case only — a documented
limitation, not a bug.

For `regex`, `case_sensitive=false` maps to that engine's own
case-insensitive flag (`re.IGNORECASE`, `(?i)`, PCRE's `i` modifier,
JavaScript's `i` flag, POSIX `REG_ICASE`) and inherits whatever
Unicode-awareness that engine has — a per-language nuance, not a
per-backend one.

PHP's regex mode compiles with PCRE's `u` (UTF-8) modifier unconditionally
— without it, PCRE treats `content` as raw bytes and a metacharacter like
`.` would match half of a multi-byte UTF-8 character instead of one whole
one. That's a correctness fix, not an optional style choice.

## Filters, pagination, ordering

- `language_id`, `context`, `status` are optional, exact-match filters
  pushed down to `select_rows` (cheap — these are the columns every other
  query already indexes by). `language_id`/`status` unset means no
  filter. `context` is subtler: unset means no filter, but an explicit
  empty string (`""`) filters for *only* the default/un-contextualized
  row — a real, distinguishable case in every port (Go needs a `*string`
  for this specifically, since its zero value can't mean both "unset"
  and "a real filter value" the way every other language's native
  `None`/`null`/`NULL` already can).
- `limit` (default 50) and `offset` (default 0) paginate the *scored,
  sorted* result set. `limit` is clamped to 1–500 — rejected outside that
  range, not silently clamped — so a call can't pull an unbounded result
  set through the in-process scan.
- Results are ordered by match score **descending**, then
  `(language_id, string_id, context)` **ascending** as a deterministic
  tiebreak — one rule, for all three modes, not one per mode.
- A `search_data` result is a **full row** — `string_id`, `language_id`,
  `context`, `content`, `original_language`, `status`, `source_checksum`,
  `updated_by`, `date_updated` — not content only. This is the first
  public function to return metadata beyond bare content; unavoidable,
  since a caller can't act on a bag of matching strings without knowing
  which key each one belongs to. `date_updated`'s exact type/format
  follows each backend driver's own native convention (a `datetime`
  object from Postgres/MySQL's Python driver vs. an ISO-8601 string for
  SQLite/filesystem, for example) — the same "physical representation
  differs, logical value doesn't" stance [`schema.md`](schema.md) already
  takes for column types, not a new guarantee this feature adds.

## Validation limits

- `query`: non-empty, at most **500 UTF-8 bytes** — deliberately far
  below `content`'s 65535-byte cap. This bounds a regex pattern's
  worst-case backtracking blast radius and keeps a search query "a
  phrase a human typed," not an arbitrary payload. It is *defense in
  depth*, not a ReDoS guarantee — `regex` mode still delegates to each
  language's native engine and inherits its worst-case performance
  characteristics; a caller exposing raw user-supplied regex strings
  through this API should apply its own timeout/complexity guard.
- `mode`: one of `exact`/`natural`/`regex`.
- `limit`: integer, 1–500. `offset`: integer, ≥ 0.

## Why this is one `select_rows` method, not twenty search implementations

The alternative to in-process scanning wouldn't just have been
"different databases might disagree" — it would also have meant writing
and maintaining a bespoke search implementation for every
language-×-backend pair (20 of them). Instead, every backend implements
exactly one new method — `select_rows` — a handful of lines each,
identical in shape to the existing `select_content`. The actual
regex/natural/exact matching, scoring, sorting, and pagination logic is
written once per *language* (5 places, in each `strings.*`), and reused
across all four backends that language supports. That's what makes
`search_data`'s conformance cases assertable with one expected result per
case rather than one per backend.
