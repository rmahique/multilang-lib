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

module.exports = { retrieveData, insertData };
