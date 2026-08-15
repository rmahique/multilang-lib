'use strict';

/**
 * Runs the shared, language-agnostic conformance suite
 * (../../conformance/cases.json) against this JavaScript implementation.
 *
 * This is the enforcement mechanism for the "same functionality and
 * results across every language and every database" requirement.
 *
 * By default this runs against SQLite (a fresh temp file per case). Set
 * MULTILANG_DB_BACKEND=postgres or =mysql (plus MULTILANG_DB_HOST/_PORT/
 * _USER/_PASSWORD/_NAME) to run the exact same suite against a real
 * server — see ../../conformance/run-live-db-tests.sh, which stands up
 * disposable Postgres/MySQL containers and runs every port's suite
 * against both.
 */

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { dbConnector, retrieveData, insertData, searchData, ValidationError } = require('../src/index');

const casesPath = path.join(__dirname, '..', '..', 'conformance', 'cases.json');
const suite = JSON.parse(fs.readFileSync(casesPath, 'utf8'));

const OPS = { retrieve_data: retrieveData, insert_data: insertData, search_data: searchData };
const BACKEND = process.env.MULTILANG_DB_BACKEND || 'sqlite';

// conformance/cases.json uses Python-style snake_case argument names
// (string_id, language_id, original_language, updated_by) since it's
// shared across all ports; this translates them to this port's camelCase
// call signature.
function callOp(fn, conn, args) {
  if (fn === retrieveData) {
    return fn(conn, args.string_id, args.language_id, args.context ?? '');
  }
  if (fn === searchData) {
    // search_data returns full rows, not a single JSON-comparable value
    // like retrieve_data -- cases.json's "expect" for this op is an
    // array of [language_id, string_id, context] triples, so the result
    // is projected down to that same shape here. See docs/conformance.md.
    return fn(conn, args.query, {
      mode: args.mode,
      languageId: args.language_id ?? null,
      context: args.context ?? null,
      status: args.status ?? null,
      caseSensitive: args.case_sensitive ?? false,
      limit: args.limit ?? 50,
      offset: args.offset ?? 0,
    }).then((rows) => rows.map((r) => [r.language_id, r.string_id, r.context]));
  }
  return fn(conn, args.string_id, args.language_id, args.content, {
    context: args.context,
    originalLanguage: args.original_language,
    status: args.status,
    updatedBy: args.updated_by,
  });
}

// Postgres/MySQL share one long-lived server across the whole run (no
// per-case temp file the way SQLite gets one), so each case truncates the
// table itself instead of connecting to a throwaway database — cheaper,
// and just as isolating since every case starts from zero rows either way.
async function isolate(conn) {
  if (BACKEND === 'sqlite') {
    return;
  }
  const client = BACKEND === 'postgres' ? conn._client : conn._pool;
  await client.query('TRUNCATE TABLE strings');
}

async function freshConn(name) {
  if (BACKEND === 'sqlite') {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'multilang-js-conf-'));
    return dbConnector('sqlite', { path: path.join(dir, `${name}.db`) });
  }
  if (BACKEND === 'filesystem') {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'multilang-js-conf-fs-'));
    return dbConnector('filesystem', { path: path.join(dir, name) });
  }
  const conn = await dbConnector(BACKEND);
  await isolate(conn);
  return conn;
}

for (const testCase of suite.cases) {
  test(`conformance: ${testCase.name}`, async () => {
    const conn = await freshConn(testCase.name);
    try {
      for (const step of testCase.operations) {
        const fn = OPS[step.op];
        if (step.expect_error) {
          await assert.rejects(() => callOp(fn, conn, step.args), ValidationError);
          continue;
        }
        const result = await callOp(fn, conn, step.args);
        if ('expect' in step) {
          assert.deepStrictEqual(result, step.expect);
        }
      }
    } finally {
      await conn.close();
    }
  });
}
