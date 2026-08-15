'use strict';

/** SQLite backend — the zero-setup default, and what the test suite uses. */

const Database = require('better-sqlite3');

const SCHEMA = `
CREATE TABLE IF NOT EXISTS strings (
    language_id       TEXT NOT NULL,
    string_id         TEXT NOT NULL,
    context           TEXT NOT NULL DEFAULT '',
    content           TEXT NOT NULL,
    original_language TEXT,
    status            TEXT NOT NULL DEFAULT 'draft',
    source_checksum   TEXT,
    updated_by        TEXT,
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
`;

const UPSERT = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (@language_id, @string_id, @context, @content, @original_language,
        @status, @source_checksum, @updated_by, @date_updated)
ON CONFLICT(language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated
`;

const SELECT = `
SELECT content FROM strings
WHERE language_id = ? AND string_id = ? AND context = ?
`;

const SELECT_ROWS_BASE = `
SELECT string_id, language_id, context, content, original_language,
       status, source_checksum, updated_by, date_updated
FROM strings
`;

class SQLiteBackend {
  /** @param {string} path Filesystem path to the SQLite database file. */
  constructor(path) {
    this._db = new Database(path, { timeout: 60000 });
    this._db.pragma('journal_mode = WAL');
    this._db.pragma('foreign_keys = ON');
  }

  /** Create the `strings` table if it doesn't already exist. */
  async ensureSchema() {
    this._db.exec(SCHEMA);
  }

  /** Return content for the given key, or null if no row matches. */
  async selectContent(stringId, languageId, context) {
    const row = this._db.prepare(SELECT).get(languageId, stringId, context);
    return row ? row.content : null;
  }

  /** Insert `row`, or update it in place on primary-key conflict. */
  async upsert(row) {
    // better-sqlite3 can't bind a Date directly; SQLite has no native
    // timestamp type, so we store it as ISO-8601 text, same as every other
    // backend's "date_updated" column ultimately represents the same UTC
    // instant.
    this._db.prepare(UPSERT).run({
      ...row,
      date_updated: row.date_updated.toISOString(),
    });
  }

  /** Return every row matching whichever of language_id/context/status are non-null. */
  async selectRows({ language_id = null, context = null, status = null } = {}) {
    const clauses = [];
    const params = [];
    if (language_id !== null) {
      clauses.push('language_id = ?');
      params.push(language_id);
    }
    if (context !== null) {
      clauses.push('context = ?');
      params.push(context);
    }
    if (status !== null) {
      clauses.push('status = ?');
      params.push(status);
    }
    let sql = SELECT_ROWS_BASE;
    if (clauses.length) sql += 'WHERE ' + clauses.join(' AND ');
    const rows = this._db.prepare(sql).all(...params);
    // SQLite has no native timestamp type; date_updated was stored as
    // ISO-8601 text by upsert(), so it's parsed back into a Date here.
    return rows.map((r) => ({ ...r, date_updated: new Date(r.date_updated) }));
  }

  /** Close the underlying connection. */
  async close() {
    this._db.close();
  }
}

module.exports = { SQLiteBackend };
