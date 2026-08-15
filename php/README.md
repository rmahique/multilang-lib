# multilang (PHP port)

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

```php
use Multilang\Connector;
use Multilang\Strings;

$conn = Connector::connect('sqlite', ['path' => 'strings.db']);

Strings::insertData($conn, 'greeting', 'en', 'Hello world');
Strings::insertData($conn, 'greeting', 'es', 'Hola mundo', '', 'en');

Strings::retrieveData($conn, 'greeting', 'es');  // -> "Hola mundo"

Strings::searchData($conn, 'hola', 'exact', 'es');
// -> [['string_id' => 'greeting', 'language_id' => 'es', 'context' => '', 'content' => 'Hola mundo', ...]]
```

`searchData` also supports `'natural'` mode (default; every
whitespace-split term must appear) and `'regex'` mode (PCRE syntax,
UTF-8 mode) — see [`../docs/search.md`](../docs/search.md).

## Credentials

Never passed as SQL — supplied via `Connector::connect()` arguments or
environment variables:

```
MULTILANG_DB_BACKEND    sqlite | postgres | mysql | filesystem
MULTILANG_DB_PATH       (sqlite) file path, (filesystem) root directory
MULTILANG_DB_HOST / _PORT / _USER / _PASSWORD / _NAME   (postgres/mysql)
```

The filesystem backend (`Connector::connect('filesystem', ['path' => './strings'])`)
needs no server or driver — see
[`../docs/connectors.md#the-filesystem-backend`](../docs/connectors.md#the-filesystem-backend)
for its on-disk layout.

## Install & test

```bash
composer install
vendor/bin/phpunit
```

This runs `tests/ValidationTest.php`, `tests/StringsTest.php`, and the
shared conformance suite `tests/ConformanceTest.php` against SQLite.
Postgres/MySQL require real servers — point `MULTILANG_DB_*` env vars at
one and adapt `StringsTest::freshConn`, or run in CI.

Requires the `pdo_sqlite` extension (and `pdo_pgsql`/`pdo_mysql` for those
backends).

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging (source-only,
wrapping the Composer package) lives in `packaging/` — see
`packaging/README.md` for build instructions.

## Example

`examples/basic_usage.php` is a runnable, self-contained example covering
insert/translate/retrieve, context disambiguation, `ValidationException`
handling, and switching backends via env vars:

```bash
php examples/basic_usage.php
```
