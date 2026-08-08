# multilang

A small string/translation storage library, implemented identically in
five languages — Python, PHP, Go, JavaScript, C/C++ — against SQLite,
PostgreSQL, MySQL/MariaDB, or a serverless filesystem backend. Every
port exposes the same three operations and the same behavior:
`db_connector` (open a connection), `insert_data` (upsert one string),
`retrieve_data` (fetch one string, content only).

This site is usage examples only. For install/test instructions, distro
packages, and design rationale (schema, validation rules, architecture),
see the [GitHub repo](https://github.com/rmahique/multilang-lib) and its
`docs/` folder.

## The same four lines, in every port

=== "Python"

    ```python
    conn = db_connector("sqlite", path="strings.db")
    insert_data(conn, "greeting", "en", "Hello world")
    insert_data(conn, "greeting", "es", "Hola mundo", original_language="en")
    retrieve_data(conn, "greeting", "es")  # -> "Hola mundo"
    ```

=== "JavaScript"

    ```javascript
    const conn = await dbConnector('sqlite', { path: 'strings.db' });
    await insertData(conn, 'greeting', 'en', 'Hello world');
    await insertData(conn, 'greeting', 'es', 'Hola mundo', { originalLanguage: 'en' });
    await retrieveData(conn, 'greeting', 'es'); // -> "Hola mundo"
    ```

=== "PHP"

    ```php
    $conn = Connector::connect('sqlite', ['path' => 'strings.db']);
    Strings::insertData($conn, 'greeting', 'en', 'Hello world');
    Strings::insertData($conn, 'greeting', 'es', 'Hola mundo', '', 'en');
    Strings::retrieveData($conn, 'greeting', 'es'); // -> "Hola mundo"
    ```

=== "Go"

    ```go
    conn, _ := multilang.Connect("sqlite", multilang.Credentials{Path: "strings.db"})
    multilang.InsertData(conn, "greeting", "en", "Hello world", multilang.InsertOptions{})
    multilang.InsertData(conn, "greeting", "es", "Hola mundo", multilang.InsertOptions{
        OriginalLanguage: "en",
    })
    content, found, _ := multilang.RetrieveData(conn, "greeting", "es", "") // -> "Hola mundo", true
    ```

=== "C"

    ```c
    ml_backend *conn = NULL;
    char errbuf[ML_ERRBUF_LEN];
    ml_credentials creds = { .path = "strings.db" };
    ml_connect("sqlite", &creds, &conn, errbuf, sizeof(errbuf));

    ml_insert_data(conn, "greeting", "en", "Hello world", NULL, errbuf, sizeof(errbuf));

    ml_insert_options opts = { .original_language = "en" };
    ml_insert_data(conn, "greeting", "es", "Hola mundo", &opts, errbuf, sizeof(errbuf));

    char *content = NULL;
    ml_retrieve_data(conn, "greeting", "es", NULL, &content, errbuf, sizeof(errbuf));
    /* content -> "Hola mundo" */
    ```

Every one of the pages below is copied verbatim from a runnable,
CI-tested example file in the repo — not a hand-typed snippet that can
drift from what actually works:

<div class="grid cards" markdown>

- **[Python](python.md)** — `python/examples/basic_usage.py`
- **[JavaScript](javascript.md)** — `javascript/examples/basic-usage.js`
- **[PHP](php.md)** — `php/examples/basic_usage.php`
- **[Go](go.md)** — `go/examples/basic_usage.go`
- **[C / C++](c.md)** — `c/examples/basic_usage.c` + `.cpp`

</div>

## Every example covers the same five things

1. Connecting (defaulting to a throwaway SQLite file, no setup needed)
2. Inserting a source string, then a translation of it (`original_language`)
3. Disambiguating the same `string_id` used two different ways via `context`
4. What you get back when a row doesn't exist (never an exception)
5. What you get back when input fails validation (always the same kind
   of typed error, never a bare/generic exception)

## Switching backends

Every port reads the same `MULTILANG_DB_*` environment variables — no
code changes needed to point an example at a real Postgres or MySQL
server, or at the dependency-free filesystem backend:

```bash
# Postgres or MySQL
MULTILANG_DB_BACKEND=postgres \
MULTILANG_DB_HOST=localhost MULTILANG_DB_USER=multilang \
MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
<run the example>

# Filesystem (no server, one JSON file per row)
MULTILANG_DB_BACKEND=filesystem MULTILANG_DB_PATH=./example-strings \
<run the example>
```

## Downloads

Prebuilt `.deb`/`.rpm` packages for every port, across Debian, Fedora,
and openSUSE, are published on
[GitHub Releases](https://github.com/rmahique/multilang-lib/releases/latest).
