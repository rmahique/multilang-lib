#include "../include/multilang.h"
#include "../src/validation.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
        failures++; \
    } \
} while (0)

static void test_valid_language_ids(void)
{
    struct { const char *in; const char *want; } cases[] = {
        {"en", "en"}, {"es", "es"}, {"pt-BR", "pt-br"}, {"zh-Hans", "zh-hans"},
        {"zh-Hans-CN", "zh-hans-cn"}, {"en-US", "en-us"}, {"sr-Latn-RS", "sr-latn-rs"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[ML_MAX_LANGUAGE_ID_LEN + 1];
        char err[ML_ERRBUF_LEN];
        ml_status s = ml_validate_language_id(cases[i].in, out, sizeof(out), err, sizeof(err));
        CHECK(s == ML_OK, "expected ML_OK");
        CHECK(strcmp(out, cases[i].want) == 0, "lowercased mismatch");
    }
}

static void test_invalid_language_ids(void)
{
    const char *bad[] = {"", "english", "e", "en_US", "en--US", "123"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char out[ML_MAX_LANGUAGE_ID_LEN + 1];
        char err[ML_ERRBUF_LEN];
        ml_status s = ml_validate_language_id(bad[i], out, sizeof(out), err, sizeof(err));
        CHECK(s == ML_ERR_VALIDATION, "expected ML_ERR_VALIDATION");
    }
}

static void test_optional_language_id_empty(void)
{
    char out[ML_MAX_LANGUAGE_ID_LEN + 1];
    char err[ML_ERRBUF_LEN];
    CHECK(ml_validate_optional_language_id(NULL, out, sizeof(out), err, sizeof(err)) == ML_OK, "NULL ok");
    CHECK(out[0] == '\0', "NULL -> empty");
    CHECK(ml_validate_optional_language_id("", out, sizeof(out), err, sizeof(err)) == ML_OK, "\"\" ok");
    CHECK(out[0] == '\0', "\"\" -> empty");
}

static void test_valid_string_ids(void)
{
    const char *ok[] = {"hello", "button.publish", "menu:item-42"};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        char out[ML_MAX_STRING_ID_LEN + 1];
        char err[ML_ERRBUF_LEN];
        CHECK(ml_validate_string_id(ok[i], out, sizeof(out), err, sizeof(err)) == ML_OK, "expected ML_OK");
        CHECK(strcmp(out, ok[i]) == 0, "should be unchanged (already lowercase)");
    }
}

static void test_invalid_string_ids(void)
{
    const char *bad[] = {"", "has space", "has/slash", "quote'"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char out[ML_MAX_STRING_ID_LEN + 1];
        char err[ML_ERRBUF_LEN];
        CHECK(ml_validate_string_id(bad[i], out, sizeof(out), err, sizeof(err)) == ML_ERR_VALIDATION, "expected error");
    }
}

static void test_string_id_lowercased(void)
{
    char out[ML_MAX_STRING_ID_LEN + 1];
    char err[ML_ERRBUF_LEN];
    CHECK(ml_validate_string_id("Button.Publish", out, sizeof(out), err, sizeof(err)) == ML_OK, "ok");
    CHECK(strcmp(out, "button.publish") == 0, "lowercased");
}

static void test_context_default_and_lowercase(void)
{
    char out[ML_MAX_CONTEXT_LEN + 1];
    char err[ML_ERRBUF_LEN];
    CHECK(ml_validate_context(NULL, out, sizeof(out), err, sizeof(err)) == ML_OK, "NULL ok");
    CHECK(out[0] == '\0', "NULL -> empty");
    CHECK(ml_validate_context("Menu.Item", out, sizeof(out), err, sizeof(err)) == ML_OK, "ok");
    CHECK(strcmp(out, "menu.item") == 0, "lowercased");
}

static void test_content_rejects_empty(void)
{
    char err[ML_ERRBUF_LEN];
    CHECK(ml_validate_content("", err, sizeof(err)) == ML_ERR_VALIDATION, "empty content rejected");
    CHECK(ml_validate_content("hello", err, sizeof(err)) == ML_OK, "non-empty content accepted");
}

static void test_status_allowlist(void)
{
    char err[ML_ERRBUF_LEN];
    const char *ok[] = {"draft", "reviewed", "published"};
    for (size_t i = 0; i < 3; i++) {
        CHECK(ml_validate_status(ok[i], err, sizeof(err)) == ML_OK, "valid status accepted");
    }
    CHECK(ml_validate_status("live", err, sizeof(err)) == ML_ERR_VALIDATION, "invalid status rejected");
}

/*
 * Regression test for a real bug: libc's tolower() is locale-sensitive.
 * Under a Turkish locale, glibc's ctype table leaves tolower('I') as 'I'
 * instead of 'i' (Turkish distinguishes dotted/dotless i; tolower() can't
 * represent the non-ASCII result in one byte, so it declines to
 * convert). Every other language port lowercases 'I' unconditionally, so
 * if this ever regresses to a locale-sensitive lowering, a C caller
 * running under a Turkish (or similar) locale would silently disagree
 * with Python/JS/Go/PHP about whether "EN" and "en" are the same
 * language_id.
 */
static void test_lowercasing_is_locale_independent(void)
{
    char *restored = setlocale(LC_CTYPE, NULL);
    char restored_copy[64];
    snprintf(restored_copy, sizeof(restored_copy), "%s", restored ? restored : "C");

    if (setlocale(LC_CTYPE, "tr_TR.utf8") == NULL) {
        fprintf(stderr, "SKIP test_lowercasing_is_locale_independent: tr_TR.utf8 locale not installed\n");
        return;
    }

    char out[ML_MAX_LANGUAGE_ID_LEN + 1];
    char err[ML_ERRBUF_LEN];
    ml_status s = ml_validate_language_id("EN-US", out, sizeof(out), err, sizeof(err));
    CHECK(s == ML_OK, "expected ML_OK under tr_TR locale");
    CHECK(strcmp(out, "en-us") == 0, "language_id must lowercase to 'en-us' regardless of process locale");

    setlocale(LC_CTYPE, restored_copy);
}

int main(void)
{
    test_valid_language_ids();
    test_invalid_language_ids();
    test_optional_language_id_empty();
    test_valid_string_ids();
    test_invalid_string_ids();
    test_string_id_lowercased();
    test_context_default_and_lowercase();
    test_content_rejects_empty();
    test_status_allowlist();
    test_lowercasing_is_locale_independent();

    if (failures == 0) {
        printf("test_validation: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_validation: %d failure(s)\n", failures);
    return 1;
}
