/*
 * Runnable example: insert a source string plus translations, retrieve
 * with context disambiguation, handle ML_ERR_VALIDATION, and switch
 * backends via environment variables.
 *
 * Build and run from the c/ directory (after `make`, so libmultilang.so
 * exists):
 *
 *   cc -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude \
 *      -o examples/basic_usage examples/basic_usage.c -L. -lmultilang
 *   LD_LIBRARY_PATH=. ./examples/basic_usage
 *
 * By default this uses a throwaway SQLite file. To point it at a real
 * server instead, set the same MULTILANG_DB_* variables every port reads
 * (see ../../docs/connectors.md):
 *
 *   MULTILANG_DB_BACKEND=postgres \
 *   MULTILANG_DB_HOST=localhost MULTILANG_DB_USER=multilang \
 *   MULTILANG_DB_PASSWORD=multilang MULTILANG_DB_NAME=multilang \
 *   LD_LIBRARY_PATH=. ./examples/basic_usage
 *
 * Or point it at the filesystem backend (no server at all):
 *
 *   MULTILANG_DB_BACKEND=filesystem MULTILANG_DB_PATH=./example-strings \
 *   LD_LIBRARY_PATH=. ./examples/basic_usage
 */

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
     * that metadata (data only, by design -- see ../../docs/schema.md). */

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

    /* --- ml_search_data: find rows by content, not by exact key ------ */
    must(ml_insert_data(conn, "welcome1", "en", "Welcome to our platform", NULL, errbuf, sizeof(errbuf)), errbuf);
    must(ml_insert_data(conn, "welcome2", "en", "Welcome back, friend", NULL, errbuf, sizeof(errbuf)), errbuf);

    ml_search_options search_opts = {0};
    search_opts.language_id = "en";
    ml_search_result *matches = NULL;
    size_t match_count = 0;
    must(ml_search_data(conn, "welcome", ML_SEARCH_NATURAL, &search_opts, &matches, &match_count, errbuf,
                         sizeof(errbuf)),
         errbuf);
    for (size_t i = 0; i < match_count; i++) {
        printf("%s -> %s\n", matches[i].string_id, matches[i].content);
    }
    /* -> welcome1 -> Welcome to our platform */
    /* -> welcome2 -> Welcome back, friend */
    ml_free_search_results(matches, match_count);

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
