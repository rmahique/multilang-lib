# JavaScript

## Install

```bash
cd javascript
npm install
```

## The example

Insert a source string plus a translation, retrieve with context
disambiguation, handle `ValidationError`, and switch backends via
environment variables. Copied verbatim from
[`javascript/examples/basic-usage.js`](https://github.com/rmahique/multilang-lib/blob/main/javascript/examples/basic-usage.js).

```javascript
const fs = require('fs');
const os = require('os');
const path = require('path');
const { dbConnector, retrieveData, insertData, ValidationError } = require('../src/index');

async function main() {
  // dbConnector reads MULTILANG_DB_BACKEND (and the matching
  // MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling back
  // to a temp SQLite file here just keeps this example runnable with no
  // setup at all.
  const backend = process.env.MULTILANG_DB_BACKEND || 'sqlite';
  let conn;
  if (backend === 'sqlite' && !process.env.MULTILANG_DB_PATH) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'multilang-example-'));
    conn = await dbConnector('sqlite', { path: path.join(dir, 'example.db') });
  } else {
    conn = await dbConnector(); // everything from MULTILANG_DB_* env vars
  }

  console.log(`Connected via backend=${JSON.stringify(backend)}`);

  // --- Insert a source string, then a translation of it -----------------
  await insertData(conn, 'greeting', 'en', 'Hello world');
  await insertData(conn, 'greeting', 'es', 'Hola mundo', { originalLanguage: 'en' });
  // originalLanguage: 'en' makes insertData hash the current English
  // content and store that hash as source_checksum -- the basis for
  // detecting later that a translation has gone stale relative to its
  // source. retrieveData itself never returns that metadata (data only,
  // by design).

  console.log(await retrieveData(conn, 'greeting', 'es')); // -> "Hola mundo"

  // --- context disambiguates the same stringId used two ways ------------
  await insertData(conn, 'post', 'en', 'Publish', { context: 'button.publish' });
  await insertData(conn, 'post', 'en', 'Post', { context: 'menu.item' });

  console.log(await retrieveData(conn, 'post', 'en', 'button.publish')); // -> "Publish"
  console.log(await retrieveData(conn, 'post', 'en', 'menu.item'));      // -> "Post"

  // --- retrieveData on a row that doesn't exist: null, not an error -----
  console.log(await retrieveData(conn, 'greeting', 'fr')); // -> null

  // --- invalid input rejects with ValidationError, not a bare exception -
  try {
    await insertData(conn, 'greeting', 'not-a-valid-bcp47-tag!!', 'test');
  } catch (err) {
    if (err instanceof ValidationError) {
      console.log(`rejected as expected: ${err.message}`);
    } else {
      throw err;
    }
  }

  await conn.close();
}

main().catch((err) => {
  console.error(err);
  process.exitCode = 1;
});
```

## Run it

```bash
cd javascript
node examples/basic-usage.js
```

Point it at a real Postgres/MySQL server, or the filesystem backend,
with no code changes — see [Switching backends](index.md#switching-backends).

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging (source-only,
wrapping the npm package) lives in `javascript/packaging/` — see that
directory's `README.md`, or grab a prebuilt one from
[GitHub Releases](https://github.com/rmahique/multilang-lib/releases/latest).
