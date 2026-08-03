#include "../include/multilang.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
        failures++; \
    } \
} while (0)

static ml_backend *fresh_conn(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/multilang_c_%s_%d.db", name, getpid());
    unlink(path);

    ml_credentials creds = {0};
    creds.path = path;

    ml_backend *conn = NULL;
    char err[ML_ERRBUF_LEN];
    ml_status status = ml_connect("sqlite", &creds, &conn, err, sizeof(err));
    if (status != ML_OK) {
        fprintf(stderr, "ml_connect failed: %s\n", err);
        exit(1);
    }
    return conn;
}

static void test_insert_then_retrieve(void)
{
    ml_backend *conn = fresh_conn("t1");
    char err[ML_ERRBUF_LEN];
    CHECK(ml_insert_data(conn, "greeting", "en", "Hello world", NULL, err, sizeof(err)) == ML_OK, "insert ok");

    char *content = NULL;
    CHECK(ml_retrieve_data(conn, "greeting", "en", NULL, &content, err, sizeof(err)) == ML_OK, "retrieve ok");
    CHECK(content != NULL && strcmp(content, "Hello world") == 0, "content matches");
    free(content);
    ml_close(conn);
}

static void test_missing_row_returns_null(void)
{
    ml_backend *conn = fresh_conn("t2");
    char err[ML_ERRBUF_LEN];
    char *content = (char *) 0x1; /* poison value to make sure it's cleared */
    CHECK(ml_retrieve_data(conn, "nope", "en", NULL, &content, err, sizeof(err)) == ML_OK, "retrieve ok");
    CHECK(content == NULL, "missing row -> NULL");
    ml_close(conn);
}

static void test_upsert_updates_existing_row(void)
{
    ml_backend *conn = fresh_conn("t3");
    char err[ML_ERRBUF_LEN];
    ml_insert_data(conn, "greeting", "en", "Hello", NULL, err, sizeof(err));
    ml_insert_data(conn, "greeting", "en", "Hello!", NULL, err, sizeof(err));

    char *content = NULL;
    ml_retrieve_data(conn, "greeting", "en", NULL, &content, err, sizeof(err));
    CHECK(content != NULL && strcmp(content, "Hello!") == 0, "upsert updated content");
    free(content);
    ml_close(conn);
}

static void test_differently_cased_language_id_is_same_row(void)
{
    ml_backend *conn = fresh_conn("t4");
    char err[ML_ERRBUF_LEN];
    ml_insert_data(conn, "greeting", "en-US", "Hello", NULL, err, sizeof(err));
    ml_insert_data(conn, "greeting", "en-us", "Hello there", NULL, err, sizeof(err));

    char *content = NULL;
    ml_retrieve_data(conn, "greeting", "EN-US", NULL, &content, err, sizeof(err));
    CHECK(content != NULL && strcmp(content, "Hello there") == 0, "same row regardless of casing");
    free(content);
    ml_close(conn);
}

static void test_differently_cased_string_id_is_same_row(void)
{
    ml_backend *conn = fresh_conn("t5");
    char err[ML_ERRBUF_LEN];
    ml_insert_data(conn, "Greeting", "en", "Hello", NULL, err, sizeof(err));
    ml_insert_data(conn, "GREETING", "en", "Hello there", NULL, err, sizeof(err));

    char *content = NULL;
    ml_retrieve_data(conn, "greeting", "en", NULL, &content, err, sizeof(err));
    CHECK(content != NULL && strcmp(content, "Hello there") == 0, "same row regardless of casing");
    free(content);
    ml_close(conn);
}

static void test_context_disambiguates(void)
{
    ml_backend *conn = fresh_conn("t6");
    char err[ML_ERRBUF_LEN];
    ml_insert_options opts1 = {0};
    opts1.context = "button.publish";
    ml_insert_options opts2 = {0};
    opts2.context = "menu.item";

    ml_insert_data(conn, "post", "fr", "Publier", &opts1, err, sizeof(err));
    ml_insert_data(conn, "post", "fr", "Article", &opts2, err, sizeof(err));

    char *c1 = NULL, *c2 = NULL;
    ml_retrieve_data(conn, "post", "fr", "button.publish", &c1, err, sizeof(err));
    ml_retrieve_data(conn, "post", "fr", "menu.item", &c2, err, sizeof(err));
    CHECK(c1 && strcmp(c1, "Publier") == 0, "context 1");
    CHECK(c2 && strcmp(c2, "Article") == 0, "context 2");
    free(c1);
    free(c2);
    ml_close(conn);
}

static void test_translation_computes_source_checksum(void)
{
    ml_backend *conn = fresh_conn("t7");
    char err[ML_ERRBUF_LEN];
    ml_insert_data(conn, "greeting", "en", "Hello world", NULL, err, sizeof(err));

    ml_insert_options opts = {0};
    opts.original_language = "en";
    ml_insert_data(conn, "greeting", "es", "Hola mundo", &opts, err, sizeof(err));

    /* Peek at the raw row via a direct sqlite3 handle on the same file to
     * confirm source_checksum/original_language, mirroring the other
     * ports' tests. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/multilang_c_t7_%d.db", getpid());
    sqlite3 *db = NULL;
    sqlite3_open(path, &db);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db,
        "SELECT source_checksum, original_language FROM strings "
        "WHERE language_id='es' AND string_id='greeting' AND context=''",
        -1, &stmt, NULL);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW, "row exists");
    CHECK(sqlite3_column_text(stmt, 0) != NULL, "source_checksum set");
    CHECK(strcmp((const char *) sqlite3_column_text(stmt, 1), "en") == 0, "original_language set");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    ml_close(conn);
}

static void test_retrieve_rejects_invalid_language_id(void)
{
    ml_backend *conn = fresh_conn("t8");
    char err[ML_ERRBUF_LEN];
    char *content = NULL;
    ml_status s = ml_retrieve_data(conn, "greeting", "not-a-real-lang-tag-!!", NULL, &content, err, sizeof(err));
    CHECK(s == ML_ERR_VALIDATION, "expected validation error");
    ml_close(conn);
}

static void test_insert_rejects_invalid_status(void)
{
    ml_backend *conn = fresh_conn("t9");
    char err[ML_ERRBUF_LEN];
    ml_insert_options opts = {0};
    opts.status = "live";
    ml_status s = ml_insert_data(conn, "greeting", "en", "Hello", &opts, err, sizeof(err));
    CHECK(s == ML_ERR_VALIDATION, "expected validation error");
    ml_close(conn);
}

static void test_insert_rejects_empty_content(void)
{
    ml_backend *conn = fresh_conn("t10");
    char err[ML_ERRBUF_LEN];
    ml_status s = ml_insert_data(conn, "greeting", "en", "", NULL, err, sizeof(err));
    CHECK(s == ML_ERR_VALIDATION, "expected validation error");
    ml_close(conn);
}

int main(void)
{
    test_insert_then_retrieve();
    test_missing_row_returns_null();
    test_upsert_updates_existing_row();
    test_differently_cased_language_id_is_same_row();
    test_differently_cased_string_id_is_same_row();
    test_context_disambiguates();
    test_translation_computes_source_checksum();
    test_retrieve_rejects_invalid_language_id();
    test_insert_rejects_invalid_status();
    test_insert_rejects_empty_content();

    if (failures == 0) {
        printf("test_strings: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_strings: %d failure(s)\n", failures);
    return 1;
}
