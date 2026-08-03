'use strict';

/** PostgreSQL backend, via the `pg` package. */

const { Client } = require('pg');

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
    date_updated      TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
`;

const UPSERT = `
INSERT INTO strings
    (language_id, string_id, context, content, original_language,
     status, source_checksum, updated_by, date_updated)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
ON CONFLICT (language_id, string_id, context) DO UPDATE SET
    content=excluded.content,
    original_language=excluded.original_language,
    status=excluded.status,
    source_checksum=excluded.source_checksum,
    updated_by=excluded.updated_by,
    date_updated=excluded.date_updated
`;

const SELECT = `
SELECT content FROM strings
WHERE language_id = $1 AND string_id = $2 AND context = $3
`;

class PostgresBackend {
  /** @param {import('pg').Client} client An already-connected client. */
  constructor(client) {
    this._client = client;
  }

  /**
   * Opens a connection, choosing TLS the same way the MySQL backend's
   * sslmode does. node-postgres's `ssl` option is either attempted or
   * not — there's no native "use TLS if available" middle ground the way
   * libpq's sslmode=prefer normally provides for other clients — so
   * "prefer" is implemented here: probe with TLS first, and only fall
   * back to a plaintext client if that probe fails.
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
   * @returns {Promise<PostgresBackend>}
   */
  static async create({ host, port, user, password, database, sslmode = 'prefer' }) {
    // Credentials are passed as driver connection parameters, never
    // interpolated into a query string or logged.
    const base = { host, port, user, password, database };

    if (sslmode === 'disable') {
      const client = new Client(base);
      await client.connect();
      return new PostgresBackend(client);
    }

    // rejectUnauthorized: false — encrypt against a self-signed cert on
    // a container/localhost server without failing on an untrusted CA,
    // the same non-verifying posture "require" uses elsewhere.
    const tlsClient = new Client({ ...base, ssl: { rejectUnauthorized: false } });
    try {
      await tlsClient.connect();
      return new PostgresBackend(tlsClient);
    } catch (err) {
      await tlsClient.end().catch(() => {});
      if (sslmode === 'require') {
        throw err;
      }
      const client = new Client(base);
      await client.connect();
      return new PostgresBackend(client);
    }
  }

  /** Create the `strings` table if it doesn't already exist. */
  async ensureSchema() {
    await this._client.query(SCHEMA);
  }

  /** Return content for the given key, or null if no row matches. */
  async selectContent(stringId, languageId, context) {
    const res = await this._client.query(SELECT, [languageId, stringId, context]);
    return res.rows.length ? res.rows[0].content : null;
  }

  /** Insert `row`, or update it in place on primary-key conflict. */
  async upsert(row) {
    await this._client.query(UPSERT, [
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

  /** Close the underlying connection. */
  async close() {
    await this._client.end();
  }
}

module.exports = { PostgresBackend };
