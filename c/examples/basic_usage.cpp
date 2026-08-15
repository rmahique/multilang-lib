/*
 * Runnable example: insert a source string plus translations, retrieve
 * with context disambiguation, handle multilang::ValidationError, and
 * switch backends via environment variables.
 *
 * Build and run from the c/ directory (after `make`, so libmultilang.so
 * and libmultilangxx.so exist):
 *
 *   g++ -std=c++17 -Iinclude -o examples/basic_usage_cpp \
 *       examples/basic_usage.cpp -L. -lmultilangxx -lmultilang
 *   LD_LIBRARY_PATH=. ./examples/basic_usage_cpp
 *
 * By default this uses a throwaway SQLite file. To point it at a real
 * server instead, set the same MULTILANG_DB_* variables every port reads
 * (see ../../docs/connectors.md):
 *
 *   MULTILANG_DB_BACKEND=postgres \
 *   MULTILANG_DB_HOST=localhost MULTILANG_DB_USER=multilang \
 *   MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
 *   LD_LIBRARY_PATH=. ./examples/basic_usage_cpp
 *
 * Or point it at the filesystem backend (no server at all):
 *
 *   MULTILANG_DB_BACKEND=filesystem MULTILANG_DB_PATH=./example-strings \
 *   LD_LIBRARY_PATH=. ./examples/basic_usage_cpp
 */

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
    // metadata (data only, by design -- see ../../docs/schema.md).

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

    // --- search_data: find rows by content, not by exact key ----------
    conn.insert_data("welcome1", "en", "Welcome to our platform");
    conn.insert_data("welcome2", "en", "Welcome back, friend");
    multilang::SearchOptions search_opts;
    search_opts.language_id = "en";
    for (const auto &row : conn.search_data("welcome", multilang::SearchMode::Natural, search_opts)) {
        std::cout << row.string_id << " -> " << row.content << "\n";
    }
    // -> welcome1 -> Welcome to our platform
    // -> welcome2 -> Welcome back, friend

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
