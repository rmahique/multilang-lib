'use strict';

/**
 * Input validation — every value that reaches SQL goes through here first.
 *
 * All checks are allow-list based (reject anything that doesn't match a
 * known-good shape) rather than deny-list based (reject known-bad patterns).
 *
 * Every id-shaped column (languageId, originalLanguage, stringId, context)
 * is normalized to lowercase by default, always — they're all part of the
 * exact-match composite primary key, so casing differences would otherwise
 * split what should be one row into duplicates.
 *
 * These rules must stay identical to every other language port's
 * validation module — see ../../conformance/README.md.
 */

const MAX_STRING_ID_LEN = 200;
const MAX_CONTEXT_LEN = 200;
const MAX_CONTENT_LEN = 65535;
const MAX_UPDATED_BY_LEN = 200;
// The BCP47 pattern below has no upper bound on repeated variant subtags,
// so without an explicit cap it would accept arbitrarily long tags. This
// must match every other port's limit exactly (see
// ../../c/include/multilang.h ML_MAX_LANGUAGE_ID_LEN) or a tag valid in
// one language could be silently rejected in another.
const MAX_LANGUAGE_ID_LEN = 35;

const VALID_STATUSES = new Set(['draft', 'reviewed', 'published']);

// Simplified BCP 47: primary language (2-3 letters) + optional script
// (4 letters) + optional region (2 letters or 3 digits) + optional variants.
// Covers the vast majority of real-world tags: en, es, pt-BR, zh-Hans,
// zh-Hans-CN, en-US, sr-Latn-RS ...
const BCP47_RE = /^[a-zA-Z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$/;

// stringId / context: namespaced identifiers like "button.publish" or
// "menu:item-42". Letters, digits, dot, underscore, hyphen, colon.
const IDENTIFIER_RE = /^[A-Za-z0-9._:-]+$/;

// "." and ".." both match IDENTIFIER_RE (it allows repeated dots) but are
// reserved path components on every filesystem the filesystem backend
// runs on -- stringId/context become directory names there, and either
// value silently collapses the path back up a level instead of naming a
// new one. This is the strictest of the three backend families (SQL
// columns don't care), so it's the shared rule everywhere, not just
// under the filesystem backend.
const RESERVED_IDENTIFIERS = new Set(['.', '..']);

class ValidationError extends Error {
  constructor(message) {
    super(message);
    this.name = 'ValidationError';
  }
}

/**
 * Normalize a BCP 47 tag to lowercase.
 *
 * BCP 47 comparison is defined as case-insensitive, and every id-shaped
 * column in this schema is stored lowercase by default so that casing
 * variation can never split what should be one row into two.
 *
 * @param {string} value A BCP 47 tag that has already passed BCP47_RE.
 * @returns {string} The tag lowercased.
 */
function normalizeLanguageTag(value) {
  return value.toLowerCase();
}

/**
 * Validate that `value` is a well-formed BCP 47 language tag and return it
 * lowercased.
 *
 * @param {*} value The candidate language tag, e.g. "en", "pt-BR".
 * @returns {string} The normalized (lowercased) tag.
 * @throws {ValidationError} If `value` is not a string, is empty, or
 *   doesn't match the expected BCP 47 shape.
 */
function validateLanguageId(value) {
  if (typeof value !== 'string' || value === '') {
    throw new ValidationError('language_id must be a non-empty string');
  }
  if (value.length > MAX_LANGUAGE_ID_LEN) {
    throw new ValidationError(`language_id ${JSON.stringify(value)} exceeds ${MAX_LANGUAGE_ID_LEN} characters`);
  }
  if (!BCP47_RE.test(value)) {
    throw new ValidationError(`language_id ${JSON.stringify(value)} is not a valid BCP 47 tag`);
  }
  return normalizeLanguageTag(value);
}

/**
 * Like validateLanguageId, but treats null/undefined/"" as "not provided"
 * and returns null instead of throwing. Used for originalLanguage, which
 * is empty exactly when a row is the source rather than a translation.
 *
 * @param {*} value A BCP 47 tag, or null/undefined/"" for "no source language".
 * @returns {string|null} The normalized tag, or null.
 * @throws {ValidationError} If `value` is provided but not a valid tag.
 */
function validateOptionalLanguageId(value) {
  if (value === null || value === undefined || value === '') {
    return null;
  }
  return validateLanguageId(value);
}

/**
 * Validate a stringId: non-empty string, within length limit, and built
 * only from the identifier charset (letters, digits, dot, underscore,
 * hyphen, colon). Returned lowercased since it's part of the exact-match
 * primary key.
 *
 * @param {*} value The candidate stringId.
 * @returns {string} `value` lowercased.
 * @throws {ValidationError} If empty, too long, wrong type, or has stray chars.
 */
function validateStringId(value) {
  if (typeof value !== 'string' || value === '') {
    throw new ValidationError('string_id must be a non-empty string');
  }
  if (value.length > MAX_STRING_ID_LEN) {
    throw new ValidationError(`string_id exceeds ${MAX_STRING_ID_LEN} characters`);
  }
  if (!IDENTIFIER_RE.test(value)) {
    throw new ValidationError(`string_id ${JSON.stringify(value)} contains invalid characters`);
  }
  if (RESERVED_IDENTIFIERS.has(value)) {
    throw new ValidationError(`string_id ${JSON.stringify(value)} is a reserved path component`);
  }
  return value.toLowerCase();
}

/**
 * Validate a context value. null/undefined is treated as "no context" and
 * normalized to "" (the default row, matching the composite key's
 * DEFAULT ''). Returned lowercased, same reasoning as stringId.
 *
 * @param {*} value The candidate context, or null/undefined.
 * @returns {string} `value` lowercased, or "" if `value` was absent.
 * @throws {ValidationError} If not a string, too long, or has stray chars.
 */
function validateContext(value) {
  if (value === null || value === undefined) {
    return '';
  }
  if (typeof value !== 'string') {
    throw new ValidationError('context must be a string');
  }
  if (value.length > MAX_CONTEXT_LEN) {
    throw new ValidationError(`context exceeds ${MAX_CONTEXT_LEN} characters`);
  }
  if (value !== '' && !IDENTIFIER_RE.test(value)) {
    throw new ValidationError(`context ${JSON.stringify(value)} contains invalid characters`);
  }
  if (RESERVED_IDENTIFIERS.has(value)) {
    throw new ValidationError(`context ${JSON.stringify(value)} is a reserved path component`);
  }
  return value.toLowerCase();
}

/**
 * Validate the text to be stored. Rejects NUL bytes since some backends
 * (and C callers) treat them as string terminators.
 *
 * MAX_CONTENT_LEN is measured in UTF-8 bytes, not characters/UTF-16 code
 * units — that's what the database columns actually store, and it's the
 * one unit every language port can measure identically (JS's `.length`
 * counts UTF-16 code units, which would let a JS caller store more
 * non-ASCII text than PHP/Go/C, which measure bytes natively, would
 * accept for the same string).
 *
 * @param {*} value The candidate content.
 * @returns {string} `value` unchanged.
 * @throws {ValidationError} If empty, too long, wrong type, or contains NUL.
 */
function validateContent(value) {
  if (typeof value !== 'string' || value === '') {
    throw new ValidationError('content must be a non-empty string');
  }
  if (Buffer.byteLength(value, 'utf8') > MAX_CONTENT_LEN) {
    throw new ValidationError(`content exceeds ${MAX_CONTENT_LEN} bytes`);
  }
  if (value.includes('\x00')) {
    throw new ValidationError('content must not contain NUL bytes');
  }
  return value;
}

/**
 * Validate that `value` is one of the allowed workflow states.
 *
 * @param {*} value The candidate status.
 * @returns {string} `value` unchanged.
 * @throws {ValidationError} If not one of draft/reviewed/published.
 */
function validateStatus(value) {
  if (!VALID_STATUSES.has(value)) {
    throw new ValidationError(
      `status must be one of ${[...VALID_STATUSES]} — got ${JSON.stringify(value)}`
    );
  }
  return value;
}

/**
 * Validate the optional audit-trail field identifying who/what wrote a row.
 *
 * Like content, updatedBy has no charset restriction, so
 * MAX_UPDATED_BY_LEN is measured in UTF-8 bytes for the same reason
 * validateContent measures bytes: that's what every other port measures
 * natively, and the only unit that can't disagree.
 *
 * @param {*} value The candidate updatedBy, or null/undefined.
 * @returns {string|null} `value` unchanged, or null.
 * @throws {ValidationError} If not a string or exceeds the length limit.
 */
function validateUpdatedBy(value) {
  if (value === null || value === undefined) {
    return null;
  }
  if (typeof value !== 'string') {
    throw new ValidationError('updated_by must be a string');
  }
  if (Buffer.byteLength(value, 'utf8') > MAX_UPDATED_BY_LEN) {
    throw new ValidationError(`updated_by exceeds ${MAX_UPDATED_BY_LEN} bytes`);
  }
  return value;
}

module.exports = {
  ValidationError,
  normalizeLanguageTag,
  validateLanguageId,
  validateOptionalLanguageId,
  validateStringId,
  validateContext,
  validateContent,
  validateStatus,
  validateUpdatedBy,
  MAX_STRING_ID_LEN,
  MAX_CONTEXT_LEN,
  MAX_CONTENT_LEN,
  MAX_UPDATED_BY_LEN,
  MAX_LANGUAGE_ID_LEN,
};
