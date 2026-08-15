# Schema and design rationale

Every port creates the same single table:

```sql
strings (
    language_id       TEXT NOT NULL,   -- BCP 47, stored lowercase
    string_id         TEXT NOT NULL,   -- caller-chosen key, stored lowercase
    context           TEXT NOT NULL DEFAULT '',  -- disambiguator, stored lowercase
    content            TEXT NOT NULL,
    original_language TEXT,            -- BCP47 of source; NULL if this row IS the source
    status             TEXT NOT NULL DEFAULT 'draft',  -- draft | reviewed | published
    source_checksum    TEXT,           -- sha256 of the source content when this row was written
    updated_by         TEXT,
    date_updated        TEXT NOT NULL,  -- (native timestamp type per backend; see below)
    PRIMARY KEY (language_id, string_id, context)
)
```

Column types shown are SQLite's; Postgres and MySQL use their own native
equivalents for the same columns (see each port's backend source for the
exact `CREATE TABLE`) — the logical schema is identical everywhere, only
the physical column types differ where a backend has something better
than `TEXT` (e.g. `TIMESTAMPTZ` for `date_updated` on Postgres).

## Why one table, not base + translations

The original design this project was modeled after (a plant-database
project referenced during requirements gathering) used a two-table
pattern: a language-neutral base table plus a `*_translations` table per
entity. This project's actual requirement was simpler and stated
explicitly: **one table**, with `language_id` and `string_id` as a
composite key. That's what's implemented — there's no base/translations
split here, deliberately.

## Why `context` is part of the primary key, not just metadata

The same `string_id` can need different text depending on where it's
used — the English word "Post" is a different translation as a button
label ("Publier") than as a menu item meaning a mail item ("Article"). If
`context` were just an informational note, both usages would collide on
`(language_id, string_id)` and only one could be stored. Making it part
of the composite key (`PRIMARY KEY (language_id, string_id, context)`,
defaulting to `""` for the common case) lets both coexist:

```
language_id | string_id | context          | content
en          | post      | button.publish   | Publish
en          | post      | menu.item        | Post
```

`retrieve_data`/`insert_data` take `context` as an optional parameter
that defaults to `""` — the un-contextualized row — so callers who never
need disambiguation never have to think about it.

## Why every id-shaped column is lowercased, always

`language_id`, `string_id`, `context`, and `original_language` are all
part of, or reference, the exact-match composite primary key. Without
forced normalization, `"EN"` and `"en"` would be two different primary
keys instead of the same language, silently splitting what should be one
row into duplicates — and which casing a given caller happens to use is
exactly the kind of thing that varies by accident across five independent
language ports. Lowercasing on the way in removes the possibility
entirely rather than requiring every caller, in every language, to
remember to normalize first. See [`validation.md`](validation.md) for the
two real bugs this decision caught (case-insensitivity done inconsistently,
and a locale-dependent lowercasing bug in C/PHP).

## `original_language` and `source_checksum`: staleness tracking

A row's `original_language` is `NULL` when the row *is* the source text.
When it's set (this row is a translation), `insert_data` looks up the
current content of the source row — same `string_id`/`context`, language
`original_language` — hashes it with SHA-256, and stores that hash as
`source_checksum`.

This is how staleness detection works: if the source text later changes,
re-hashing it and comparing against the stored `source_checksum` reveals
that this translation was written against an older version of the source
and may need review. The library doesn't perform that check itself —
`retrieve_data` returns content only, never metadata — but the data
needed to do it is captured at write time, which is the only time it's
cheaply available (the alternative is diffing against some retained
history, which this schema doesn't keep).

## `status`: draft / reviewed / published

A plain workflow-state string, defaulting to `"draft"` on insert. Nothing
in `retrieve_data` currently filters by status — it's stored so that a
caller building a review workflow on top of this library has somewhere
to put that state without inventing a second table. Kept intentionally
small (three fixed values, allow-listed) rather than modeling a general
workflow engine. `search_data` (see [`search.md`](search.md)) can filter
by `status`, so a review-queue caller can ask for e.g. only `draft` rows
matching a query — but it still doesn't interpret the value beyond an
exact-match filter.

## Field length limits, and why they're measured the way they are

| Field | Limit | Unit |
|---|---|---|
| `language_id` / `original_language` | 35 chars | ASCII-only (charset-restricted; see below) |
| `string_id` | 200 chars | ASCII-only |
| `context` | 200 chars | ASCII-only |
| `content` | 65535 | **UTF-8 bytes** |
| `updated_by` | 200 | **UTF-8 bytes** |

`language_id`, `string_id`, and `context` are restricted to
`[A-Za-z0-9._:-]` (see [`validation.md`](validation.md)), so for them
"characters" and "bytes" are the same thing — the charset itself is
ASCII-only. `content` and `updated_by` have no charset restriction, so
their limits are measured in **UTF-8 bytes specifically**, not characters
or UTF-16 code units, because bytes are what the database columns
actually store, and it's the one unit that can't disagree between a
Python `len()` (codepoints), a JavaScript `.length` (UTF-16 code units),
and a Go/PHP/C `len(string)`/`strlen()` (bytes natively). This was a real
bug, not a hypothetical one — see
[`validation.md`](validation.md#resolved-cross-language-inconsistencies).

`language_id`'s 35-character cap exists because the BCP 47 pattern this
project validates against has no length bound of its own (its
variant-subtag group can repeat indefinitely) — 35 is a practical ceiling
matching what real-world BCP 47 tags need, enforced explicitly so a
regex-only check can't be tricked into accepting an arbitrarily long tag.
