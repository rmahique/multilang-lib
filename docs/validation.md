# Validation rules

Every value reaches SQL only after passing through a validation function
first, in every port — `validation.py` (Python), `validation.js`
(JavaScript), `Validation.php` (PHP), `validation.go` (Go), and
`validation.c` (C, used by both the C and C++ APIs). All five files
implement the exact same rules; that sameness is what the conformance
suite ([`conformance.md`](conformance.md)) exists to keep true.

All checks are **allow-list based** — reject anything that doesn't match
a known-good shape — rather than deny-list based. No query in this
project is ever built by string concatenation; every value below is
bound as a parameter, which is what actually prevents injection. The
validation described here exists for a different reason: guaranteeing
every port treats the same input identically, not as an injection
defense (parameterized queries already are that).

## `language_id` / `original_language`

Must match a simplified BCP 47 shape: primary language (2–3 letters) +
optional 4-letter script + optional region (2 letters or 3 digits) +
optional variant subtags:

```
^[a-zA-Z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$
```

Valid examples: `en`, `pt-BR`, `zh-Hans`, `zh-Hans-CN`, `es-419` (numeric
UN region code), `sr-Latn-RS`, `sl-rozaj-biske` (multi-variant).

Capped at 35 characters (see [`schema.md`](schema.md#field-length-limits-and-why-theyre-measured-the-way-they-are)),
then lowercased. `original_language` uses the same validator, except
`null`/`""` is accepted and means "this row has no source" rather than
being rejected.

## `string_id` / `context`

Must match `^[A-Za-z0-9._:-]+$` — letters, digits, dot, underscore,
hyphen, colon, nothing else. `string_id` max 200 characters; `context`
max 200 characters, and `null`/`""` is accepted for `context` and means
"the default, un-contextualized row." Both are lowercased.

Additionally, the value cannot be exactly `.` or `..`. Both match the
charset above (it allows repeated dots) but are reserved path components
on the filesystem backend, where `string_id`/`context` become directory
names — see
[`connectors.md#the-filesystem-backend`](connectors.md#the-filesystem-backend).
This is a SQL-backend-agnostic rule enforced identically everywhere, not
just when the filesystem backend is in use, per the project's one rule
that matters most: identical validation across every port regardless of
which backend a given caller happens to be using. `...` and longer runs
of dots are unaffected — only the two literal reserved values are
rejected (see `string_id_multiple_dots_allowed` in
`conformance/cases.json`).

## `content`

Must be non-empty, contain no NUL bytes, and be at most 65535 **UTF-8
bytes** (not characters — see [`schema.md`](schema.md)). No charset
restriction otherwise, and **not** lowercased — unlike every other field
here, content's original casing is exactly what gets stored and
returned.

## `status`

Must be exactly one of `"draft"`, `"reviewed"`, `"published"`.

## `updated_by`

Optional. If provided, must be a string of at most 200 UTF-8 bytes. No
charset restriction, not lowercased (same reasoning as `content` — it's
free text, not an identifier).

## Resolved cross-language inconsistencies

These were found by auditing each port's validation code side by side
and then *confirmed by executing the same input* against each language's
actual regex/length-check/case-folding engine — not just by reading the
code and assuming it matched. See [`conformance.md`](conformance.md) for
how the fixture that pins these down works; summary of what was found:

1. **Byte-vs-character length counting.** Python (`len()`) and
   JavaScript (`.length`) originally measured `content`/`updated_by`
   length in codepoints/UTF-16 units instead of UTF-8 bytes, letting them
   accept up to 3x more non-ASCII text than PHP/Go/C would for the same
   input. Fixed by switching both to explicit UTF-8 byte counting.
2. **No length cap on `language_id`** in four of five ports — only C had
   one, as a side effect of validating into a fixed-size buffer. A 38+
   character tag was silently accepted everywhere except C. Fixed by
   adding the same explicit 35-character cap everywhere.
3. **Regex `$` anchor accepting a trailing newline** in Python (`re`) and
   PHP (PCRE) but not JavaScript/Go/C — `"en-US\n"` passed validation in
   two of five ports. Fixed via `re.fullmatch` in Python and PCRE's `D`
   modifier in PHP.
4. **Locale-dependent lowercasing in C and PHP.** `tolower()` and
   `strtolower()` are sensitive to the process's `LC_CTYPE` locale —
   under a Turkish locale, `tolower('I')` returns `'I'` unchanged instead
   of `'i'`. For a library whose entire job is internationalization, this
   is a genuinely dangerous class of bug: a C or PHP service running
   under an affected locale would silently stop normalizing casing for
   any id containing `I`. Fixed with explicit ASCII-only lowercasing that
   never consults process locale.
