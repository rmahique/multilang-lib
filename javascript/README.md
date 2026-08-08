# multilang (JavaScript port)

Licensed under the GNU General Public License v3.0 or later — see `../LICENSE`.

Reusable string/translation storage backed by SQLite, PostgreSQL, MySQL,
or a plain filesystem tree. Same schema, same validation rules, same
results as every other language port in this project — see
`../conformance/README.md`.

## Schema

```sql
strings (
    language_id       TEXT NOT NULL,   -- BCP 47, stored lowercase
    string_id         TEXT NOT NULL,   -- stored lowercase
    context           TEXT NOT NULL DEFAULT '',  -- stored lowercase
    content           TEXT NOT NULL,
    original_language TEXT,            -- BCP47 of source; NULL if this row IS the source
    status            TEXT NOT NULL DEFAULT 'draft',  -- draft | reviewed | published
    source_checksum   TEXT,            -- sha256 of the source content when this row was written
    updated_by        TEXT,
    date_updated      TEXT NOT NULL,
    PRIMARY KEY (language_id, string_id, context)
)
```

## Usage

```js
const { dbConnector, retrieveData, insertData } = require('multilang');

const conn = await dbConnector('sqlite', { path: 'strings.db' });

await insertData(conn, 'greeting', 'en', 'Hello world');
await insertData(conn, 'greeting', 'es', 'Hola mundo', { originalLanguage: 'en' });

await retrieveData(conn, 'greeting', 'es');  // -> "Hola mundo"

await conn.close();
```

## Credentials

Never passed as SQL — supplied via constructor args or environment variables:

```
MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
MULTILANG_DB_HOST / _PORT / _USER / _PASSWORD / _NAME   (postgres/mysql)
```

The filesystem backend (`dbConnector('filesystem', { path: './strings' })`)
needs no server or driver — see
[`../docs/connectors.md#the-filesystem-backend`](../docs/connectors.md#the-filesystem-backend)
for its on-disk layout.

## Install & test

```bash
npm install
npm test
```

`npm test` runs both the unit tests (`test/validation.test.js`,
`test/strings.test.js`) and the shared conformance suite
(`test/conformance.test.js`) against SQLite. Postgres/MySQL require real
servers — point `MULTILANG_DB_*` env vars at one and adapt `strings.test.js`'s
`freshConn` helper, or run in CI.

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging (source-only,
wrapping the npm package) lives in `packaging/` — see
`packaging/README.md` for build instructions.

## Example

`examples/basic-usage.js` is a runnable, self-contained example covering
insert/translate/retrieve, context disambiguation, `ValidationError`
handling, and switching backends via env vars:

```bash
node examples/basic-usage.js
```
