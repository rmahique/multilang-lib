/*
 * Runs the shared, language-agnostic conformance suite
 * (../../conformance/cases.json) against this C implementation.
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

#include "../include/multilang.h"
#include "../src/backend.h"
#include "json_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
static int cases_run = 0;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t) size + 1);
    fread(buf, 1, (size_t) size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static const char *arg_str(const json_value *args, const char *key)
{
    return json_as_string(json_object_get(args, key), NULL);
}

/*
 * Maps cases.json's mode string to ml_search_mode. Falls back to a
 * deliberately out-of-range enum value for anything else (including the
 * "not-a-real-mode" fixture case) -- C's ml_search_data takes a typed
 * enum, not a free-form string like the other four ports, so an
 * out-of-range enum value is this API's equivalent of an invalid mode
 * string; ml_search_data's own validation (see strings.c) rejects it the
 * same way it would reject any other unrecognized ml_search_mode.
 */
static ml_search_mode parse_mode(const char *s)
{
    if (s != NULL && strcmp(s, "exact") == 0) return ML_SEARCH_EXACT;
    if (s != NULL && strcmp(s, "regex") == 0) return ML_SEARCH_REGEX;
    if (s != NULL && strcmp(s, "natural") == 0) return ML_SEARCH_NATURAL;
    return (ml_search_mode) -1;
}

/*
 * Opens a connection with a guaranteed-empty `strings` table. SQLite gets
 * a brand-new temp file per case; Postgres/MySQL share one long-lived
 * server across the whole run, so each case truncates the table itself
 * instead — cheaper than provisioning a throwaway database per case, and
 * just as isolating since every case starts from zero rows either way.
 */
static ml_backend *fresh_conn(const char *backend, const char *name, char *err, size_t err_len)
{
    ml_backend *conn = NULL;

    if (strcmp(backend, "sqlite") == 0) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/multilang_c_conf_%s_%d.db", name, getpid());
        unlink(path);
        ml_credentials creds = {0};
        creds.path = path;
        if (ml_connect("sqlite", &creds, &conn, err, err_len) != ML_OK) {
            return NULL;
        }
        return conn;
    }

    if (strcmp(backend, "filesystem") == 0) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/multilang_c_conf_fs_%s_%d", name, getpid());
        ml_credentials creds = {0};
        creds.path = path;
        if (ml_connect("filesystem", &creds, &conn, err, err_len) != ML_OK) {
            return NULL;
        }
        return conn;
    }

    ml_credentials creds = {0}; /* host/port/user/password/database from MULTILANG_DB_* env vars */
    if (ml_connect(backend, &creds, &conn, err, err_len) != ML_OK) {
        return NULL;
    }
    if (ml_backend_truncate(conn, err, err_len) != ML_OK) {
        ml_close(conn);
        return NULL;
    }
    return conn;
}

static void run_case(const json_value *test_case, const char *backend)
{
    const char *name = json_as_string(json_object_get(test_case, "name"), "?");
    cases_run++;

    char err[ML_ERRBUF_LEN];
    ml_backend *conn = fresh_conn(backend, name, err, sizeof(err));
    if (conn == NULL) {
        fprintf(stderr, "FAIL %s: connect: %s\n", name, err);
        failures++;
        return;
    }

    const json_value *operations = json_object_get(test_case, "operations");
    for (size_t i = 0; i < json_array_size(operations); i++) {
        const json_value *step = json_array_get(operations, i);
        const char *op = json_as_string(json_object_get(step, "op"), "");
        const json_value *args = json_object_get(step, "args");
        int expect_error = json_is_true(json_object_get(step, "expect_error"));
        const json_value *expect = json_object_get(step, "expect");
        int has_expect = expect != NULL;

        if (strcmp(op, "search_data") == 0) {
            ml_search_options opts = {0};
            opts.language_id = arg_str(args, "language_id");
            opts.context = arg_str(args, "context");
            opts.status = arg_str(args, "status");
            opts.case_sensitive = json_is_true(json_object_get(args, "case_sensitive"));
            opts.limit = (int) json_as_int(json_object_get(args, "limit"), 0);
            opts.offset = (int) json_as_int(json_object_get(args, "offset"), 0);

            ml_search_mode mode = parse_mode(arg_str(args, "mode"));
            ml_search_result *results = NULL;
            size_t count = 0;
            ml_status search_status = ml_search_data(conn, arg_str(args, "query"), mode, &opts,
                                                       &results, &count, err, sizeof(err));

            if (expect_error) {
                if (search_status != ML_ERR_VALIDATION) {
                    fprintf(stderr, "FAIL %s: op %s: expected ML_ERR_VALIDATION, got %d\n", name, op, search_status);
                    failures++;
                }
                ml_free_search_results(results, count);
                continue;
            }
            if (search_status != ML_OK) {
                fprintf(stderr, "FAIL %s: op %s: unexpected error: %s\n", name, op, err);
                failures++;
                ml_free_search_results(results, count);
                continue;
            }
            if (has_expect) {
                /* search_data returns full rows, not a single
                 * JSON-comparable value like retrieve_data --
                 * cases.json's "expect" for this op is an array of
                 * [language_id, string_id, context] triples, compared
                 * directly against the result's own fields. See
                 * docs/conformance.md. */
                int mismatch = (count != json_array_size(expect));
                for (size_t k = 0; !mismatch && k < count; k++) {
                    const json_value *triple = json_array_get(expect, k);
                    const char *want_lang = json_as_string(json_array_get(triple, 0), "");
                    const char *want_sid = json_as_string(json_array_get(triple, 1), "");
                    const char *want_ctx = json_as_string(json_array_get(triple, 2), "");
                    if (strcmp(results[k].language_id, want_lang) != 0 ||
                        strcmp(results[k].string_id, want_sid) != 0 ||
                        strcmp(results[k].context, want_ctx) != 0) {
                        mismatch = 1;
                    }
                }
                if (mismatch) {
                    fprintf(stderr, "FAIL %s: op %s: search_data result mismatch (got %zu rows, want %zu)\n",
                            name, op, count, json_array_size(expect));
                    failures++;
                }
            }
            ml_free_search_results(results, count);
            continue;
        }

        ml_status status;
        char *content = NULL;

        if (strcmp(op, "retrieve_data") == 0) {
            status = ml_retrieve_data(conn, arg_str(args, "string_id"), arg_str(args, "language_id"),
                                       arg_str(args, "context"), &content, err, sizeof(err));
        } else {
            ml_insert_options opts = {0};
            opts.context = arg_str(args, "context");
            opts.original_language = arg_str(args, "original_language");
            opts.status = arg_str(args, "status");
            opts.updated_by = arg_str(args, "updated_by");
            status = ml_insert_data(conn, arg_str(args, "string_id"), arg_str(args, "language_id"),
                                     arg_str(args, "content"), &opts, err, sizeof(err));
        }

        if (expect_error) {
            if (status != ML_ERR_VALIDATION) {
                fprintf(stderr, "FAIL %s: op %s: expected ML_ERR_VALIDATION, got %d\n", name, op, status);
                failures++;
            }
            free(content);
            continue;
        }

        if (status != ML_OK) {
            fprintf(stderr, "FAIL %s: op %s: unexpected error: %s\n", name, op, err);
            failures++;
            free(content);
            continue;
        }

        if (has_expect) {
            if (json_is_null(expect)) {
                if (content != NULL) {
                    fprintf(stderr, "FAIL %s: op %s: expected no row, got '%s'\n", name, op, content);
                    failures++;
                }
            } else {
                const char *want = json_as_string(expect, NULL);
                if (content == NULL || strcmp(content, want) != 0) {
                    fprintf(stderr, "FAIL %s: op %s: got '%s', want '%s'\n", name, op,
                            content ? content : "(null)", want);
                    failures++;
                }
            }
        }
        free(content);
    }

    ml_close(conn);
}

int main(void)
{
    const char *backend = getenv("MULTILANG_DB_BACKEND");
    if (backend == NULL || backend[0] == '\0') {
        backend = "sqlite";
    }

    char *text = read_file("../conformance/cases.json");
    json_value *suite = json_parse(text);
    if (suite == NULL) {
        fprintf(stderr, "failed to parse conformance/cases.json\n");
        return 1;
    }

    const json_value *cases = json_object_get(suite, "cases");
    for (size_t i = 0; i < json_array_size(cases); i++) {
        run_case(json_array_get(cases, i), backend);
    }

    json_free(suite);
    free(text);

    if (failures == 0) {
        printf("test_conformance: all %d cases passed\n", cases_run);
        return 0;
    }
    fprintf(stderr, "test_conformance: %d failure(s) across %d cases\n", failures, cases_run);
    return 1;
}
