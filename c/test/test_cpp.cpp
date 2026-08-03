#include "../include/multilang.hpp"
#include "../src/backend.h"
#include "json_mini.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

using multilang::Connection;
using multilang::Credentials;
using multilang::InsertOptions;
using multilang::ValidationError;

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);   \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static Connection fresh_conn(const std::string &name)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/multilang_cpp_%s_%d.db", name.c_str(), getpid());
    unlink(path);
    Credentials creds;
    creds.path = path;
    return Connection("sqlite", creds);
}

static void test_insert_then_retrieve()
{
    auto conn = fresh_conn("t1");
    conn.insert_data("greeting", "en", "Hello world");
    auto content = conn.retrieve_data("greeting", "en");
    CHECK(content.has_value() && *content == "Hello world", "content matches");
}

static void test_missing_row_returns_nullopt()
{
    auto conn = fresh_conn("t2");
    auto content = conn.retrieve_data("nope", "en");
    CHECK(!content.has_value(), "missing row -> nullopt");
}

static void test_upsert_updates_existing_row()
{
    auto conn = fresh_conn("t3");
    conn.insert_data("greeting", "en", "Hello");
    conn.insert_data("greeting", "en", "Hello!");
    auto content = conn.retrieve_data("greeting", "en");
    CHECK(content.has_value() && *content == "Hello!", "upsert updated content");
}

static void test_differently_cased_language_id_is_same_row()
{
    auto conn = fresh_conn("t4");
    conn.insert_data("greeting", "en-US", "Hello");
    conn.insert_data("greeting", "en-us", "Hello there");
    auto content = conn.retrieve_data("greeting", "EN-US");
    CHECK(content.has_value() && *content == "Hello there", "same row regardless of casing");
}

static void test_context_disambiguates()
{
    auto conn = fresh_conn("t5");
    InsertOptions o1;
    o1.context = "button.publish";
    InsertOptions o2;
    o2.context = "menu.item";
    conn.insert_data("post", "fr", "Publier", o1);
    conn.insert_data("post", "fr", "Article", o2);

    auto c1 = conn.retrieve_data("post", "fr", "button.publish");
    auto c2 = conn.retrieve_data("post", "fr", "menu.item");
    CHECK(c1.has_value() && *c1 == "Publier", "context 1");
    CHECK(c2.has_value() && *c2 == "Article", "context 2");
}

static void test_retrieve_rejects_invalid_language_id()
{
    auto conn = fresh_conn("t6");
    bool threw = false;
    try {
        conn.retrieve_data("greeting", "not-a-real-lang-tag-!!");
    } catch (const ValidationError &) {
        threw = true;
    }
    CHECK(threw, "expected ValidationError");
}

static void test_insert_rejects_empty_content()
{
    auto conn = fresh_conn("t7");
    bool threw = false;
    try {
        conn.insert_data("greeting", "en", "");
    } catch (const ValidationError &) {
        threw = true;
    }
    CHECK(threw, "expected ValidationError");
}

/*
 * Opens a connection with a guaranteed-empty `strings` table. SQLite gets
 * a brand-new temp file per case; Postgres/MySQL share one long-lived
 * server across the whole run, so each case truncates the table itself
 * instead — cheaper than provisioning a throwaway database per case, and
 * just as isolating since every case starts from zero rows either way.
 */
static Connection conformance_conn(const std::string &backend, const std::string &name)
{
    if (backend == "sqlite") {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/multilang_cpp_conf_%s_%d.db", name.c_str(), getpid());
        unlink(path);
        Credentials creds;
        creds.path = path;
        return Connection("sqlite", creds);
    }

    Connection conn(backend, Credentials{}); // host/port/user/password/database from MULTILANG_DB_* env vars
    char err[ML_ERRBUF_LEN];
    if (ml_backend_truncate(conn.raw(), err, sizeof(err)) != ML_OK) {
        throw multilang::DbError(err);
    }
    return conn;
}

/* Runs the shared conformance suite (../../conformance/cases.json)
 * through the C++ wrapper, same as every other port's runner. */
static void run_conformance()
{
    const char *backend_env = std::getenv("MULTILANG_DB_BACKEND");
    std::string backend = (backend_env != nullptr && backend_env[0] != '\0') ? backend_env : "sqlite";

    std::ifstream f("../conformance/cases.json");
    std::stringstream buf;
    buf << f.rdbuf();
    std::string text = buf.str();

    json_value *suite = json_parse(text.c_str());
    CHECK(suite != nullptr, "conformance suite parses");
    if (suite == nullptr) {
        return;
    }

    const json_value *cases = json_object_get(suite, "cases");
    int cases_run = 0;
    for (size_t i = 0; i < json_array_size(cases); i++) {
        const json_value *tc = json_array_get(cases, i);
        std::string name = json_as_string(json_object_get(tc, "name"), "?");
        cases_run++;

        Connection conn = conformance_conn(backend, name);

        const json_value *ops = json_object_get(tc, "operations");
        for (size_t j = 0; j < json_array_size(ops); j++) {
            const json_value *step = json_array_get(ops, j);
            std::string op = json_as_string(json_object_get(step, "op"), "");
            const json_value *args = json_object_get(step, "args");
            bool expect_error = json_is_true(json_object_get(step, "expect_error"));
            const json_value *expect = json_object_get(step, "expect");

            auto arg = [&](const char *key) -> std::string {
                return json_as_string(json_object_get(args, key), "");
            };

            try {
                if (op == "retrieve_data") {
                    auto result = conn.retrieve_data(arg("string_id"), arg("language_id"), arg("context"));
                    if (expect != nullptr) {
                        if (json_is_null(expect)) {
                            CHECK(!result.has_value(), (name + ": expected no row").c_str());
                        } else {
                            std::string want = json_as_string(expect, "");
                            CHECK(result.has_value() && *result == want, (name + ": content mismatch").c_str());
                        }
                    }
                } else {
                    InsertOptions opts;
                    opts.context = arg("context");
                    opts.original_language = arg("original_language");
                    std::string status = arg("status");
                    opts.status = status.empty() ? "draft" : status;
                    opts.updated_by = arg("updated_by");
                    conn.insert_data(arg("string_id"), arg("language_id"), arg("content"), opts);
                }
                CHECK(!expect_error, (name + ": expected an error but none was thrown").c_str());
            } catch (const ValidationError &) {
                CHECK(expect_error, (name + ": unexpected ValidationError").c_str());
            }
        }
    }

    json_free(suite);
    printf("conformance: ran %d cases\n", cases_run);
}

int main()
{
    test_insert_then_retrieve();
    test_missing_row_returns_nullopt();
    test_upsert_updates_existing_row();
    test_differently_cased_language_id_is_same_row();
    test_context_disambiguates();
    test_retrieve_rejects_invalid_language_id();
    test_insert_rejects_empty_content();
    run_conformance();

    if (failures == 0) {
        printf("test_cpp: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_cpp: %d failure(s)\n", failures);
    return 1;
}
