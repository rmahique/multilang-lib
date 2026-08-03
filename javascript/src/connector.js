'use strict';

/**
 * dbConnector — the single entry point that turns a backend name +
 * credentials into a connected, schema-ready backend instance.
 *
 * Credentials are never accepted as raw SQL fragments and are never logged.
 * By default they are read from environment variables so they never need
 * to live in source or config files:
 *
 *   MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
 *   MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
 *   MULTILANG_DB_HOST       (postgres/mysql)
 *   MULTILANG_DB_PORT       (postgres/mysql)
 *   MULTILANG_DB_USER       (postgres/mysql)
 *   MULTILANG_DB_PASSWORD   (postgres/mysql)
 *   MULTILANG_DB_NAME       (postgres/mysql)
 *   MULTILANG_DB_SSLMODE    (postgres/mysql; default "prefer" — use TLS
 *                            opportunistically but don't require it; set
 *                            to "require" to make TLS mandatory, "disable"
 *                            to force plaintext)
 *
 * Explicit fields in `credentials` always take precedence over environment
 * variables, which is useful for tests and one-off scripts.
 */

const { SQLiteBackend } = require('./backends/sqlite');
const { PostgresBackend } = require('./backends/postgres');
const { MySQLBackend } = require('./backends/mysql');
const { FilesystemBackend } = require('./backends/filesystem');

const SUPPORTED = ['sqlite', 'postgres', 'mysql', 'filesystem'];
const DEFAULT_PORTS = { postgres: 5432, mysql: 3306 };

/**
 * Open a connection to `backend` ('sqlite' | 'postgres' | 'mysql' |
 * 'filesystem') and return a ready-to-use backend instance with its
 * schema ensured.
 *
 * @param {string} [backend] Defaults to MULTILANG_DB_BACKEND.
 * @param {object} [credentials] Backend-specific connection parameters.
 *   For sqlite: `path` (file). For filesystem: `path` (root directory).
 *   For postgres/mysql: `host`, `port`, `user`, `password`, `database`.
 *   Any field not passed falls back to the matching MULTILANG_DB_*
 *   environment variable.
 * @returns {Promise<SQLiteBackend|PostgresBackend|MySQLBackend|FilesystemBackend>}
 * @throws {Error} If `backend` is missing/unsupported, or required
 *   credentials are missing for a non-sqlite backend.
 */
async function dbConnector(backend, credentials = {}) {
  backend = backend || process.env.MULTILANG_DB_BACKEND;
  if (!SUPPORTED.includes(backend)) {
    throw new Error(`backend must be one of ${SUPPORTED} — got ${JSON.stringify(backend)}`);
  }

  let conn;
  if (backend === 'sqlite') {
    const path = credentials.path || process.env.MULTILANG_DB_PATH;
    if (!path) {
      throw new Error("sqlite backend requires 'path' or MULTILANG_DB_PATH");
    }
    conn = new SQLiteBackend(path);
  } else if (backend === 'filesystem') {
    const path = credentials.path || process.env.MULTILANG_DB_PATH;
    if (!path) {
      throw new Error("filesystem backend requires 'path' or MULTILANG_DB_PATH");
    }
    conn = new FilesystemBackend(path);
  } else {
    const host = credentials.host || process.env.MULTILANG_DB_HOST;
    const port = credentials.port || process.env.MULTILANG_DB_PORT || DEFAULT_PORTS[backend];
    const user = credentials.user || process.env.MULTILANG_DB_USER;
    const password = credentials.password || process.env.MULTILANG_DB_PASSWORD;
    const database = credentials.database || process.env.MULTILANG_DB_NAME;

    const missing = Object.entries({ host, user, password, database })
      .filter(([, v]) => !v)
      .map(([k]) => k);
    if (missing.length) {
      throw new Error(`${backend} backend missing required credentials: ${missing.join(', ')}`);
    }

    const sslmode = credentials.sslmode || process.env.MULTILANG_DB_SSLMODE || 'prefer';
    const opts = { host, port: Number(port), user, password, database, sslmode };
    if (backend === 'postgres') {
      conn = await PostgresBackend.create(opts);
    } else {
      conn = await MySQLBackend.create(opts);
    }
  }

  await conn.ensureSchema();
  return conn;
}

module.exports = { dbConnector };
