'use strict';

/**
 * The two public data functions: retrieveData and insertData.
 *
 * Both take an already-open backend (from dbConnector) so the caller
 * controls connection lifetime; neither function opens or closes a
 * connection itself.
 */

const crypto = require('crypto');
const validation = require('./validation');

/**
 * Look up one piece of text by its identity.
 *
 * Every value is validated before it reaches SQL and every query is
 * parameterized — no value here is ever concatenated into a query string.
 *
 * @param {object} conn An open backend instance from dbConnector.
 * @param {string} stringId Identifier of the string to fetch.
 * @param {string} languageId BCP 47 tag of the language to fetch (e.g. "es").
 * @param {string} [context=''] Optional disambiguator for stringIds reused
 *   with different wording in different places.
 * @returns {Promise<string|null>} The stored content, or null if no
 *   matching row exists. Data only — no metadata is returned.
 * @throws {ValidationError} If any argument fails validation.
 */
async function retrieveData(conn, stringId, languageId, context = '') {
  stringId = validation.validateStringId(stringId);
  languageId = validation.validateLanguageId(languageId);
  context = validation.validateContext(context);

  return conn.selectContent(stringId, languageId, context);
}

/**
 * Insert a new row, or update it in place if (stringId, languageId,
 * context) already exists (upsert on the composite primary key).
 *
 * @param {object} conn An open backend instance from dbConnector.
 * @param {string} stringId Identifier of the string being written.
 * @param {string} languageId BCP 47 tag of the language this content is
 *   written in.
 * @param {string} content The text to store.
 * @param {object} [opts]
 * @param {string} [opts.context=''] Optional disambiguator; see retrieveData.
 * @param {string|null} [opts.originalLanguage=null] BCP 47 tag of the
 *   source language, if this row is a translation. Leave null when this
 *   row IS the source.
 * @param {string} [opts.status='draft'] One of "draft", "reviewed",
 *   "published".
 * @param {string|null} [opts.updatedBy=null] Optional identifier of
 *   who/what wrote this row.
 *
 * When `originalLanguage` is given, the current content of the source row
 * (languageId=originalLanguage, same stringId/context) is hashed with
 * SHA-256 and stored as sourceChecksum, so staleness can be detected later
 * by re-hashing the source and comparing. If the source row doesn't exist
 * yet, sourceChecksum is left as null.
 *
 * @returns {Promise<void>}
 * @throws {ValidationError} If any argument fails validation.
 */
async function insertData(conn, stringId, languageId, content, opts = {}) {
  const {
    context = '',
    originalLanguage = null,
    status = 'draft',
    updatedBy = null,
  } = opts;

  stringId = validation.validateStringId(stringId);
  languageId = validation.validateLanguageId(languageId);
  const validContext = validation.validateContext(context);
  content = validation.validateContent(content);
  const validOriginalLanguage = validation.validateOptionalLanguageId(originalLanguage);
  const validStatus = validation.validateStatus(status);
  const validUpdatedBy = validation.validateUpdatedBy(updatedBy);

  let sourceChecksum = null;
  if (validOriginalLanguage !== null) {
    const sourceContent = await conn.selectContent(stringId, validOriginalLanguage, validContext);
    if (sourceContent !== null) {
      sourceChecksum = checksum(sourceContent);
    }
  }

  await conn.upsert({
    string_id: stringId,
    language_id: languageId,
    context: validContext,
    content,
    original_language: validOriginalLanguage,
    status: validStatus,
    source_checksum: sourceChecksum,
    updated_by: validUpdatedBy,
    // A real Date object, not a pre-formatted string: each backend adapts
    // it to that database's native timestamp type itself.
    date_updated: new Date(),
  });
}

/** Return the hex SHA-256 digest of `text`, used for sourceChecksum. */
function checksum(text) {
  return crypto.createHash('sha256').update(text, 'utf8').digest('hex');
}

/**
 * Lowercase only the ASCII A-Z range, leaving every other character
 * untouched. Deliberately not String.prototype.toLowerCase() (Unicode-
 * aware) — search's case-insensitive matching (exact/natural modes) must
 * behave identically across all five ports, and C has no Unicode-aware
 * lowercasing in its standard library (see docs/validation.md's locale-
 * dependent-tolower() bug for why this project never trusts a language's
 * own "smart" lowercasing for cross-language guarantees). See
 * docs/search.md for the resulting documented limitation: non-ASCII
 * letters (e.g. "É"/"é") only match by exact case.
 *
 * @param {string} text
 * @returns {string}
 */
function asciiFold(text) {
  let out = '';
  for (const ch of text) {
    const code = ch.codePointAt(0);
    out += code >= 65 && code <= 90 ? String.fromCharCode(code + 32) : ch;
  }
  return out;
}

/** Count non-overlapping occurrences of `needle` in `haystack`. */
function countOccurrences(haystack, needle) {
  if (needle === '') return 0;
  let count = 0;
  let start = 0;
  for (;;) {
    const idx = haystack.indexOf(needle, start);
    if (idx === -1) return count;
    count += 1;
    start = idx + needle.length;
  }
}

/**
 * Return how many times `query` (or, for natural/regex, its
 * pre-processed form `terms`/`pattern`) matches `content` under `mode` —
 * 0 means no match. See docs/search.md for the exact/natural/regex
 * semantics.
 */
function scoreRow(content, mode, query, terms, pattern, caseSensitive) {
  if (mode === 'regex') {
    const global = new RegExp(pattern.source, pattern.flags.includes('g') ? pattern.flags : pattern.flags + 'g');
    const matches = content.match(global);
    return matches ? matches.length : 0;
  }

  const haystack = caseSensitive ? content : asciiFold(content);

  if (mode === 'exact') {
    const needle = caseSensitive ? query : asciiFold(query);
    return countOccurrences(haystack, needle);
  }

  // natural: every term must appear at least once (AND); score is the
  // sum of each term's occurrence count.
  let total = 0;
  for (const term of terms) {
    const needle = caseSensitive ? term : asciiFold(term);
    const occurrences = countOccurrences(haystack, needle);
    if (occurrences === 0) return 0;
    total += occurrences;
  }
  return total;
}

/**
 * Search `content` across every row matching the optional filters.
 *
 * Matching runs entirely in-process, after fetching candidate rows from
 * the backend filtered only by the cheap exact-match columns
 * (languageId/context/status) — this is what guarantees identical search
 * results across SQLite/Postgres/MySQL/filesystem: the actual matching
 * logic below never touches backend-specific SQL/FTS engines. See
 * docs/search.md for the full rationale and the documented
 * cross-language regex-flavor/case-folding limitations.
 *
 * @param {object} conn An open backend instance from dbConnector.
 * @param {string} query The text/pattern to search for. Non-empty, at
 *   most 500 UTF-8 bytes.
 * @param {object} [opts]
 * @param {string} [opts.mode='natural'] "exact" (query is a literal
 *   substring of content), "natural" (default; query is split on
 *   whitespace into terms, every one of which must appear as a substring
 *   of content — AND, not OR), or "regex" (query is a JS RegExp pattern
 *   searched against content).
 * @param {string|null} [opts.languageId=null] Optional exact-match
 *   filter. null (default) means no filter.
 * @param {string|null} [opts.context=null] Optional exact-match filter.
 *   null (default) means no filter; '' filters for only the
 *   default/un-contextualized row.
 * @param {string|null} [opts.status=null] Optional exact-match filter.
 *   null (default) means no filter.
 * @param {boolean} [opts.caseSensitive=false] If false, exact/natural
 *   matching folds ASCII letters only (non-ASCII letters always match by
 *   exact case — a documented limitation, see docs/search.md) and regex
 *   matching uses the `i` flag.
 * @param {number} [opts.limit=50] Maximum rows to return, 1-500.
 * @param {number} [opts.offset=0] Rows to skip before the first returned
 *   result, for pagination.
 * @returns {Promise<object[]>} Rows — stringId, languageId, context,
 *   content, originalLanguage, status, sourceChecksum, updatedBy,
 *   dateUpdated — matching the filters and query, ordered by match score
 *   descending, then (languageId, stringId, context) ascending as a
 *   deterministic tiebreak.
 * @throws {ValidationError} If any argument fails validation.
 */
async function searchData(conn, query, opts = {}) {
  const {
    mode = 'natural',
    languageId = null,
    context = null,
    status = null,
    caseSensitive = false,
    limit = 50,
    offset = 0,
  } = opts;

  const validMode = validation.validateSearchMode(mode);
  const { query: validQuery, terms, pattern } = validation.validateSearchQuery(query, validMode, caseSensitive);
  const validLanguageId = validation.validateOptionalLanguageId(languageId);
  const validContext = context === null ? null : validation.validateContext(context);
  const validStatus = status === null ? null : validation.validateStatus(status);
  const { limit: validLimit, offset: validOffset } = validation.validateSearchPagination(limit, offset);

  const rows = await conn.selectRows({
    language_id: validLanguageId,
    context: validContext,
    status: validStatus,
  });

  const scored = [];
  for (const row of rows) {
    const score = scoreRow(row.content, validMode, validQuery, terms, pattern, caseSensitive);
    if (score > 0) {
      scored.push({ score, row });
    }
  }

  scored.sort((a, b) => {
    if (a.score !== b.score) return b.score - a.score;
    if (a.row.language_id !== b.row.language_id) return a.row.language_id < b.row.language_id ? -1 : 1;
    if (a.row.string_id !== b.row.string_id) return a.row.string_id < b.row.string_id ? -1 : 1;
    if (a.row.context !== b.row.context) return a.row.context < b.row.context ? -1 : 1;
    return 0;
  });

  return scored.slice(validOffset, validOffset + validLimit).map((s) => s.row);
}

module.exports = { retrieveData, insertData, searchData };
