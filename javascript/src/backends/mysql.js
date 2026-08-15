'use strict';

/**
 * MySQL/MariaDB backend, via `mysql2`.
 *
 * Requires MySQL 5.7.9+ or MariaDB 10.2.2+ (innodb_large_prefix on by
 * default, DYNAMIC row format default) — the composite primary key
 * (language_id, string_id, context) in utf8mb4 can exceed the legacy
 * 767-byte InnoDB index-prefix limit on older versions/configurations.
 */

const mysql = require('mysql2/promise');

const SCHEMA = `
CREATE TABLE IF NOT EXISTS strings (
    language_id       VARCHAR(35) NOT NULL,
    string_id         VARCHAR(200) NOT NULL,
    context           VARCHAR(200) NOT NULL DEFAULT '',
    content           MEDIUMTEXT NOT NULL,
    original_language VARCHAR(35),
    status            VARCHAR(20) NOT NULL DEFAULT 'draft',
    source_checksum   VARCHAR(64),
    updated_by        VARCHAR(200),
    date_updated      DATETIME NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
) ENGINE=InnoDB ROW_FORMAT=DYNAMIC DEFAULT CHARSET=utf8mb4
`;

const UPSERT = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
ON DUPLICATE KEY UPDATE
    content=VALUES(content),
    original_language=VALUES(original_language),
    status=VALUES(status),
    source_checksum=VALUES(source_checksum),
    updated_by=VALUES(updated_by),
    date_updated=VALUES(date_updated)
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

class MySQLBackend {
  /** @param {import('mysql2/promise').Pool} pool An already-connected pool. */
  constructor(pool) {
    this._pool = pool;
  }

  /**
   * Opens a connection pool, choosing TLS the same way the Postgres
   * backend's sslmode does. mysql2 has no native "attempt TLS, fall back
   * to plaintext" mode (unlike libpq's sslmode=prefer), so "prefer" is
   * implemented here: probe with TLS first, and only fall back to a
   * plaintext pool if that probe fails.
   *
   * @param {object} opts
   * @param {string} opts.host
   * @param {number} opts.port
   * @param {string} opts.user
   * @param {string} opts.password
   * @param {string} opts.database
   * @param {string} [opts.sslmode='prefer'] "prefer" (default): attempt
   *   TLS, fall back to plaintext if the server doesn't support it.
   *   "require": attempt TLS, throw if it fails rather than falling
   *   back. "disable": plaintext only, no TLS attempt.
   * @returns {Promise<MySQLBackend>}
   */
  static async create({ host, port, user, password, database, sslmode = 'prefer' }) {
    // Credentials are passed as driver connection parameters, never
    // interpolated into a query string or logged.
    const base = { host, port, user, password, database, charset: 'utf8mb4' };

    if (sslmode === 'disable') {
      return new MySQLBackend(mysql.createPool(base));
    }

    // rejectUnauthorized: false — same non-verifying posture as the
    // Postgres backend's sslmode=require/prefer: encrypt against a
    // self-signed cert on a container/localhost server without failing
    // on an untrusted CA.
    const tlsPool = mysql.createPool({ ...base, ssl: { rejectUnauthorized: false } });
    try {
      await tlsPool.query('SELECT 1');
      return new MySQLBackend(tlsPool);
    } catch (err) {
      await tlsPool.end().catch(() => {});
      if (sslmode === 'require') {
        throw err;
      }
      return new MySQLBackend(mysql.createPool(base));
    }
  }

  /** Create the `strings` table if it doesn't already exist. */
  async ensureSchema() {
    await this._pool.query(SCHEMA);
  }

  /** Return content for the given key, or null if no row matches. */
  async selectContent(stringId, languageId, context) {
    const [rows] = await this._pool.query(SELECT, [languageId, stringId, context]);
    return rows.length ? rows[0].content : null;
  }

  /** Insert `row`, or update it in place on primary-key conflict. */
  async upsert(row) {
    await this._pool.query(UPSERT, [
      row.language_id,
      row.string_id,
      row.context,
      row.content,
      row.original_language,
      row.status,
      row.source_checksum,
      row.updated_by,
      row.date_updated,
    ]);
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
    const [rows] = await this._pool.query(sql, params);
    return rows;
  }

  /** Close the underlying connection pool. */
  async close() {
    await this._pool.end();
  }
}

module.exports = { MySQLBackend };
