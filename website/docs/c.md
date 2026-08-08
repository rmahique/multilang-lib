# C / C++

## Build the library

```bash
cd c
make   # builds libmultilang.so (C) and libmultilangxx.so (C++ wrapper)
```

## The examples

Insert a source string plus a translation, retrieve with context
disambiguation, handle the validation error, and switch backends via
environment variables — once through the plain C API, once through the
C++ wrapper. Copied verbatim from
[`c/examples/basic_usage.c`](https://github.com/rmahique/multilang-lib/blob/main/c/examples/basic_usage.c)
and
[`c/examples/basic_usage.cpp`](https://github.com/rmahique/multilang-lib/blob/main/c/examples/basic_usage.cpp).

=== "C"

    ```c
    #include <multilang.h>

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    static void must(ml_status status, const char *errbuf)
    {
        if (status != ML_OK) {
            fprintf(stderr, "unexpected error: %s\n", errbuf);
            exit(1);
        }
    }

    int main(void)
    {
        /* ml_connect reads MULTILANG_DB_BACKEND (and the matching
         * MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set; falling
         * back to a temp SQLite file here just keeps this example runnable
         * with no setup at all. */
        const char *backend = getenv("MULTILANG_DB_BACKEND");
        if (backend == NULL || backend[0] == '\0') {
            backend = "sqlite";
        }

        ml_credentials creds = {0};
        if (strcmp(backend, "sqlite") == 0 && getenv("MULTILANG_DB_PATH") == NULL) {
            creds.path = "/tmp/multilang-example.db";
        }

        ml_backend *conn = NULL;
        char errbuf[ML_ERRBUF_LEN];
        must(ml_connect(backend, &creds, &conn, errbuf, sizeof(errbuf)), errbuf);

        printf("Connected via backend=%s\n", backend);

        /* --- Insert a source string, then a translation of it ------------ */
        must(ml_insert_data(conn, "greeting", "en", "Hello world", NULL, errbuf, sizeof(errbuf)), errbuf);

        ml_insert_options translation_opts = {0};
        translation_opts.original_language = "en";
        must(ml_insert_data(conn, "greeting", "es", "Hola mundo", &translation_opts, errbuf, sizeof(errbuf)), errbuf);
        /* original_language = "en" makes ml_insert_data hash the current
         * English content and store that hash as source_checksum -- the
         * basis for detecting later that a translation has gone stale
         * relative to its source. ml_retrieve_data itself never returns
         * that metadata (data only, by design). */

        char *content = NULL;
        must(ml_retrieve_data(conn, "greeting", "es", NULL, &content, errbuf, sizeof(errbuf)), errbuf);
        printf("%s\n", content); /* -> "Hola mundo" */
        free(content);

        /* --- context disambiguates the same string_id used two ways ------ */
        ml_insert_options o1 = {0};
        o1.context = "button.publish";
        must(ml_insert_data(conn, "post", "en", "Publish", &o1, errbuf, sizeof(errbuf)), errbuf);

        ml_insert_options o2 = {0};
        o2.context = "menu.item";
        must(ml_insert_data(conn, "post", "en", "Post", &o2, errbuf, sizeof(errbuf)), errbuf);

        must(ml_retrieve_data(conn, "post", "en", "button.publish", &content, errbuf, sizeof(errbuf)), errbuf);
        printf("%s\n", content); /* -> "Publish" */
        free(content);

        must(ml_retrieve_data(conn, "post", "en", "menu.item", &content, errbuf, sizeof(errbuf)), errbuf);
        printf("%s\n", content); /* -> "Post" */
        free(content);

        /* --- retrieve on a row that doesn't exist: NULL, not an error ---- */
        must(ml_retrieve_data(conn, "greeting", "fr", NULL, &content, errbuf, sizeof(errbuf)), errbuf);
        printf("content is NULL: %s\n", content == NULL ? "true" : "false");
        free(content);

        /* --- invalid input returns ML_ERR_VALIDATION, not a crash -------- */
        ml_status status = ml_insert_data(conn, "greeting", "not-a-valid-bcp47-tag!!", "test", NULL, errbuf, sizeof(errbuf));
        if (status == ML_ERR_VALIDATION) {
            printf("rejected as expected: %s\n", errbuf);
        } else {
            must(status, errbuf);
        }

        ml_close(conn);
        return 0;
    }
    ```

    ```bash
    cc -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude \
       -o examples/basic_usage examples/basic_usage.c -L. -lmultilang
    LD_LIBRARY_PATH=. ./examples/basic_usage
    ```

=== "C++"

    ```cpp
    #include <multilang.hpp>

    #include <cstdlib>
    #include <iostream>

    int main()
    {
        // Connection's constructor reads MULTILANG_DB_BACKEND (and the
        // matching MULTILANG_DB_HOST/_USER/_PASSWORD/_NAME/_PORT) if set;
        // falling back to a temp SQLite file here just keeps this example
        // runnable with no setup at all.
        const char *backend_env = std::getenv("MULTILANG_DB_BACKEND");
        std::string backend = (backend_env != nullptr && backend_env[0] != '\0') ? backend_env : "sqlite";

        multilang::Credentials creds;
        if (backend == "sqlite" && std::getenv("MULTILANG_DB_PATH") == nullptr) {
            creds.path = "/tmp/multilang-example-cpp.db";
        }

        multilang::Connection conn(backend, creds);
        std::cout << "Connected via backend=" << backend << "\n";

        // --- Insert a source string, then a translation of it -------------
        conn.insert_data("greeting", "en", "Hello world");
        multilang::InsertOptions translation_opts;
        translation_opts.original_language = "en";
        conn.insert_data("greeting", "es", "Hola mundo", translation_opts);
        // original_language = "en" makes insert_data hash the current
        // English content and store that hash as source_checksum -- the
        // basis for detecting later that a translation has gone stale
        // relative to its source. retrieve_data itself never returns that
        // metadata (data only, by design).

        std::cout << *conn.retrieve_data("greeting", "es") << "\n"; // -> "Hola mundo"

        // --- context disambiguates the same string_id used two ways -------
        multilang::InsertOptions o1;
        o1.context = "button.publish";
        conn.insert_data("post", "en", "Publish", o1);

        multilang::InsertOptions o2;
        o2.context = "menu.item";
        conn.insert_data("post", "en", "Post", o2);

        std::cout << *conn.retrieve_data("post", "en", "button.publish") << "\n"; // -> "Publish"
        std::cout << *conn.retrieve_data("post", "en", "menu.item") << "\n";      // -> "Post"

        // --- retrieve_data on a row that doesn't exist: nullopt, not an error
        auto missing = conn.retrieve_data("greeting", "fr");
        std::cout << "has_value: " << std::boolalpha << missing.has_value() << "\n"; // -> false

        // --- invalid input throws multilang::ValidationError --------------
        try {
            conn.insert_data("greeting", "not-a-valid-bcp47-tag!!", "test");
        } catch (const multilang::ValidationError &e) {
            std::cout << "rejected as expected: " << e.what() << "\n";
        }

        return 0;
    }
    ```

    ```bash
    g++ -std=c++17 -Iinclude -o examples/basic_usage_cpp \
        examples/basic_usage.cpp -L. -lmultilangxx -lmultilang
    LD_LIBRARY_PATH=. ./examples/basic_usage_cpp
    ```

Point either one at a real Postgres/MySQL server, or the filesystem
backend, with no code changes — see [Switching backends](index.md#switching-backends).

## Distro packages

Debian/Ubuntu, RHEL/Fedora, and openSUSE/SLES packaging — a real
compiled `libmultilang0`/`libmultilang-dev` (and `-devel`) split — lives
in `c/packaging/` — see that directory's `README.md`, or grab a
prebuilt one from
[GitHub Releases](https://github.com/rmahique/multilang-lib/releases/latest).
