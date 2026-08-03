'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');
const { dbConnector, retrieveData, insertData, ValidationError } = require('../src/index');

async function freshConn(name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'multilang-js-'));
  return dbConnector('sqlite', { path: path.join(dir, `${name}.db`) });
}

test('insert then retrieve', async () => {
  const conn = await freshConn('t1');
  await insertData(conn, 'greeting', 'en', 'Hello world');
  assert.equal(await retrieveData(conn, 'greeting', 'en'), 'Hello world');
  await conn.close();
});

test('missing row returns null', async () => {
  const conn = await freshConn('t2');
  assert.equal(await retrieveData(conn, 'nope', 'en'), null);
  await conn.close();
});

test('upsert updates existing row', async () => {
  const conn = await freshConn('t3');
  await insertData(conn, 'greeting', 'en', 'Hello');
  await insertData(conn, 'greeting', 'en', 'Hello!');
  assert.equal(await retrieveData(conn, 'greeting', 'en'), 'Hello!');
  await conn.close();
});

test('differently cased language id is same row', async () => {
  const conn = await freshConn('t4');
  await insertData(conn, 'greeting', 'en-US', 'Hello');
  await insertData(conn, 'greeting', 'en-us', 'Hello there');
  assert.equal(await retrieveData(conn, 'greeting', 'EN-US'), 'Hello there');
  await conn.close();
});

test('differently cased string id is same row', async () => {
  const conn = await freshConn('t5');
  await insertData(conn, 'Greeting', 'en', 'Hello');
  await insertData(conn, 'GREETING', 'en', 'Hello there');
  assert.equal(await retrieveData(conn, 'greeting', 'en'), 'Hello there');
  await conn.close();
});

test('differently cased context is same row', async () => {
  const conn = await freshConn('t6');
  await insertData(conn, 'post', 'fr', 'Publier', { context: 'Button.Publish' });
  await insertData(conn, 'post', 'fr', 'Publier!', { context: 'button.publish' });
  assert.equal(await retrieveData(conn, 'post', 'fr', 'BUTTON.PUBLISH'), 'Publier!');
  await conn.close();
});

test('context disambiguates same string id', async () => {
  const conn = await freshConn('t7');
  await insertData(conn, 'post', 'fr', 'Publier', { context: 'button.publish' });
  await insertData(conn, 'post', 'fr', 'Article', { context: 'menu.item' });
  assert.equal(await retrieveData(conn, 'post', 'fr', 'button.publish'), 'Publier');
  assert.equal(await retrieveData(conn, 'post', 'fr', 'menu.item'), 'Article');
  await conn.close();
});

test('translation computes source checksum', async () => {
  const conn = await freshConn('t8');
  await insertData(conn, 'greeting', 'en', 'Hello world');
  await insertData(conn, 'greeting', 'es', 'Hola mundo', { originalLanguage: 'en' });

  const row = conn._db
    .prepare(
      "SELECT source_checksum, original_language FROM strings " +
        "WHERE language_id='es' AND string_id='greeting' AND context=''"
    )
    .get();
  assert.notEqual(row.source_checksum, null);
  assert.equal(row.original_language, 'en');
  await conn.close();
});

test('source row has no checksum', async () => {
  const conn = await freshConn('t9');
  await insertData(conn, 'greeting', 'en', 'Hello world');
  const row = conn._db
    .prepare(
      "SELECT source_checksum FROM strings WHERE language_id='en' AND string_id='greeting' AND context=''"
    )
    .get();
  assert.equal(row.source_checksum, null);
  await conn.close();
});

test('retrieve rejects invalid language id', async () => {
  const conn = await freshConn('t10');
  await assert.rejects(
    () => retrieveData(conn, 'greeting', 'not-a-real-lang-tag-!!'),
    ValidationError
  );
  await conn.close();
});

test('insert rejects invalid status', async () => {
  const conn = await freshConn('t11');
  await assert.rejects(
    () => insertData(conn, 'greeting', 'en', 'Hello', { status: 'live' }),
    ValidationError
  );
  await conn.close();
});

test('insert rejects empty content', async () => {
  const conn = await freshConn('t12');
  await assert.rejects(() => insertData(conn, 'greeting', 'en', ''), ValidationError);
  await conn.close();
});
