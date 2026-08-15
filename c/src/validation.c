#include "validation.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Simplified BCP 47: primary language (2-3 letters) + optional script
 * (4 letters) + optional region (2 letters or 3 digits) + optional
 * variants. Covers the vast majority of real-world tags: en, es, pt-BR,
 * zh-Hans, zh-Hans-CN, en-US, sr-Latn-RS ... */
static const char *BCP47_PATTERN =
    "^[A-Za-z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?(-[A-Za-z0-9]{5,8})*$";

/* string_id / context: namespaced identifiers like "button.publish" or
 * "menu:item-42". Letters, digits, dot, underscore, hyphen, colon. */
static const char *IDENTIFIER_PATTERN = "^[A-Za-z0-9._:-]+$";

/* "." and ".." both match IDENTIFIER_PATTERN (it allows repeated dots)
 * but are reserved path components on every filesystem the filesystem
 * backend runs on -- string_id/context become directory names there,
 * and either value silently collapses the path back up a level instead
 * of naming a new one. This is the strictest of the three backend
 * families (SQL columns don't care), so it's the shared rule
 * everywhere, not just under the filesystem backend. */
static int is_reserved_identifier(const char *value)
{
    return strcmp(value, ".") == 0 || strcmp(value, "..") == 0;
}

static const char *VALID_STATUSES[] = {"draft", "reviewed", "published"};
static const size_t VALID_STATUSES_COUNT = 3;

static void set_errbuf(char *errbuf, size_t errbuf_len, const char *fmt, const char *arg)
{
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, fmt, arg);
}

static int matches(const char *pattern, const char *value)
{
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        return 0; /* pattern is one of our own constants; a compile failure is a bug, not user input */
    }
    int rc = regexec(&re, value, 0, NULL, 0);
    regfree(&re);
    return rc == 0;
}

/* ASCII-only lowercasing, deliberately not libc's tolower(). tolower() is
 * locale-sensitive (LC_CTYPE): under a Turkish locale, glibc's ctype
 * table leaves tolower('I') as 'I' instead of 'i' (Turkish distinguishes
 * dotted/dotless i, and tolower() can't represent the non-ASCII result in
 * a single byte, so it declines to convert at all). Every id-shaped value
 * here is guaranteed ASCII-only by the regex checks that already ran, so
 * this must be a fixed, locale-independent mapping — otherwise this
 * process's normalization would silently disagree with every other
 * language port's (locale-independent) lowercasing for the same input,
 * splitting what should be one row into duplicates. */
static void to_lower_copy(const char *in, char *out, size_t n)
{
    size_t i;
    for (i = 0; in[i] != '\0' && i + 1 < n; i++) {
        unsigned char c = (unsigned char) in[i];
        out[i] = (char) ((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    out[i] = '\0';
}

ml_status ml_validate_language_id(const char *in, char *out, size_t out_size,
                                   char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        set_errbuf(errbuf, errbuf_len, "language_id must be a non-empty string", NULL);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) >= out_size) {
        set_errbuf(errbuf, errbuf_len, "language_id '%s' is too long", in);
        return ML_ERR_VALIDATION;
    }
    if (!matches(BCP47_PATTERN, in)) {
        set_errbuf(errbuf, errbuf_len, "language_id '%s' is not a valid BCP 47 tag", in);
        return ML_ERR_VALIDATION;
    }
    to_lower_copy(in, out, out_size);
    return ML_OK;
}

ml_status ml_validate_optional_language_id(const char *in, char *out, size_t out_size,
                                            char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return ML_OK;
    }
    return ml_validate_language_id(in, out, out_size, errbuf, errbuf_len);
}

ml_status ml_validate_string_id(const char *in, char *out, size_t out_size,
                                 char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        set_errbuf(errbuf, errbuf_len, "string_id must be a non-empty string", NULL);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) > ML_MAX_STRING_ID_LEN) {
        set_errbuf(errbuf, errbuf_len, "string_id '%s' exceeds the length limit", in);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) >= out_size) {
        set_errbuf(errbuf, errbuf_len, "string_id '%s' is too long for the output buffer", in);
        return ML_ERR_VALIDATION;
    }
    if (!matches(IDENTIFIER_PATTERN, in)) {
        set_errbuf(errbuf, errbuf_len, "string_id '%s' contains invalid characters", in);
        return ML_ERR_VALIDATION;
    }
    if (is_reserved_identifier(in)) {
        set_errbuf(errbuf, errbuf_len, "string_id '%s' is a reserved path component", in);
        return ML_ERR_VALIDATION;
    }
    to_lower_copy(in, out, out_size);
    return ML_OK;
}

ml_status ml_validate_context(const char *in, char *out, size_t out_size,
                               char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return ML_OK;
    }
    if (strlen(in) > ML_MAX_CONTEXT_LEN) {
        set_errbuf(errbuf, errbuf_len, "context '%s' exceeds the length limit", in);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) >= out_size) {
        set_errbuf(errbuf, errbuf_len, "context '%s' is too long for the output buffer", in);
        return ML_ERR_VALIDATION;
    }
    if (!matches(IDENTIFIER_PATTERN, in)) {
        set_errbuf(errbuf, errbuf_len, "context '%s' contains invalid characters", in);
        return ML_ERR_VALIDATION;
    }
    if (is_reserved_identifier(in)) {
        set_errbuf(errbuf, errbuf_len, "context '%s' is a reserved path component", in);
        return ML_ERR_VALIDATION;
    }
    to_lower_copy(in, out, out_size);
    return ML_OK;
}

ml_status ml_validate_content(const char *in, char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        set_errbuf(errbuf, errbuf_len, "content must be a non-empty string", NULL);
        return ML_ERR_VALIDATION;
    }
    /* Every other port rejects embedded NUL bytes explicitly. Here that
     * check is structural rather than explicit: this ABI takes
     * NUL-terminated C strings, so an "embedded NUL" would just truncate
     * `in` at strlen() and everything after it is invisible to this
     * function — there is no way for a well-formed C string to carry one
     * through. Binary-safe content (with real embedded NULs) is out of
     * scope for this ABI. */

    /* ML_MAX_CONTENT_LEN is measured in bytes — strlen() already does the
     * right thing here since C strings are byte arrays. This must stay
     * bytes, not characters, to match every other language port (see
     * ../../conformance/README.md): Python/JavaScript measure content
     * length in codepoints/UTF-16 units by default and have to explicitly
     * convert to UTF-8 byte length to agree with C/Go/PHP, which measure
     * bytes natively. */
    if (strlen(in) > ML_MAX_CONTENT_LEN) {
        set_errbuf(errbuf, errbuf_len, "content exceeds the maximum length in bytes", NULL);
        return ML_ERR_VALIDATION;
    }
    return ML_OK;
}

ml_status ml_validate_status(const char *in, char *errbuf, size_t errbuf_len)
{
    if (in == NULL) {
        set_errbuf(errbuf, errbuf_len, "status must be one of [draft, reviewed, published]", NULL);
        return ML_ERR_VALIDATION;
    }
    for (size_t i = 0; i < VALID_STATUSES_COUNT; i++) {
        if (strcmp(in, VALID_STATUSES[i]) == 0) {
            return ML_OK;
        }
    }
    set_errbuf(errbuf, errbuf_len, "status must be one of [draft, reviewed, published] — got '%s'", in);
    return ML_ERR_VALIDATION;
}

ml_status ml_validate_updated_by(const char *in, char *out, size_t out_size,
                                  char *errbuf, size_t errbuf_len)
{
    if (in == NULL || in[0] == '\0') {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return ML_OK;
    }
    if (strlen(in) > ML_MAX_UPDATED_BY_LEN) {
        set_errbuf(errbuf, errbuf_len, "updated_by '%s' exceeds the length limit", in);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) >= out_size) {
        set_errbuf(errbuf, errbuf_len, "updated_by '%s' is too long for the output buffer", in);
        return ML_ERR_VALIDATION;
    }
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
    return ML_OK;
}

ml_status ml_validate_search_mode(const char *in, char *errbuf, size_t errbuf_len)
{
    if (in == NULL || (strcmp(in, "exact") != 0 && strcmp(in, "natural") != 0 && strcmp(in, "regex") != 0)) {
        set_errbuf(errbuf, errbuf_len, "mode must be one of [exact, natural, regex]", NULL);
        return ML_ERR_VALIDATION;
    }
    return ML_OK;
}

/* Splits `value` on runs of whitespace into a NULL-terminated array of
 * malloc'd term strings. Returns ML_ERR_VALIDATION if no terms are found. */
static ml_status split_terms(const char *value, char ***out_terms, char *errbuf, size_t errbuf_len)
{
    size_t capacity = 4;
    char **terms = malloc(capacity * sizeof(char *));
    size_t count = 0;

    const char *p = value;
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char) *p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char) *p)) {
            p++;
        }
        size_t term_len = (size_t) (p - start);

        if (count + 1 >= capacity) {
            capacity *= 2;
            terms = realloc(terms, capacity * sizeof(char *));
        }
        char *term = malloc(term_len + 1);
        memcpy(term, start, term_len);
        term[term_len] = '\0';
        terms[count++] = term;
    }
    terms[count] = NULL;

    if (count == 0) {
        free(terms);
        set_errbuf(errbuf, errbuf_len, "query must contain at least one term in natural mode", NULL);
        return ML_ERR_VALIDATION;
    }
    *out_terms = terms;
    return ML_OK;
}

/* Compiles `value` as a POSIX extended regex (REG_ICASE if
 * !case_sensitive). Returns ML_ERR_VALIDATION with the engine's own
 * compile error folded into errbuf on failure. */
static ml_status compile_pattern(const char *value, int case_sensitive, regex_t **out_pattern,
                                  char *errbuf, size_t errbuf_len)
{
    regex_t *re = malloc(sizeof(regex_t));
    int flags = REG_EXTENDED;
    if (!case_sensitive) {
        flags |= REG_ICASE;
    }
    int rc = regcomp(re, value, flags);
    if (rc != 0) {
        char errmsg[256];
        regerror(rc, re, errmsg, sizeof(errmsg));
        free(re);
        set_errbuf(errbuf, errbuf_len, "query is not a valid regex: %s", errmsg);
        return ML_ERR_VALIDATION;
    }
    *out_pattern = re;
    return ML_OK;
}

ml_status ml_validate_search_query(const char *in, const char *mode, int case_sensitive,
                                    ml_search_query *out, char *errbuf, size_t errbuf_len)
{
    out->terms = NULL;
    out->pattern = NULL;

    /* Same structural note as ml_validate_content: this ABI takes
     * NUL-terminated C strings, so there's no way for an embedded NUL to
     * survive strlen() -- no explicit check is possible or needed. */
    if (in == NULL || in[0] == '\0') {
        set_errbuf(errbuf, errbuf_len, "query must be a non-empty string", NULL);
        return ML_ERR_VALIDATION;
    }
    if (strlen(in) > ML_MAX_SEARCH_QUERY_LEN) {
        set_errbuf(errbuf, errbuf_len, "query exceeds the maximum length in bytes", NULL);
        return ML_ERR_VALIDATION;
    }

    if (strcmp(mode, "regex") == 0) {
        return compile_pattern(in, case_sensitive, &out->pattern, errbuf, errbuf_len);
    }
    if (strcmp(mode, "natural") == 0) {
        return split_terms(in, &out->terms, errbuf, errbuf_len);
    }
    return ML_OK;
}

void ml_free_search_query(ml_search_query *q)
{
    if (q->terms != NULL) {
        for (size_t i = 0; q->terms[i] != NULL; i++) {
            free(q->terms[i]);
        }
        free(q->terms);
        q->terms = NULL;
    }
    if (q->pattern != NULL) {
        regfree(q->pattern);
        free(q->pattern);
        q->pattern = NULL;
    }
}

ml_status ml_validate_search_pagination(int limit, int offset, char *errbuf, size_t errbuf_len)
{
    if (limit < ML_MIN_SEARCH_LIMIT || limit > ML_MAX_SEARCH_LIMIT) {
        set_errbuf(errbuf, errbuf_len, "limit must be between 1 and 500", NULL);
        return ML_ERR_VALIDATION;
    }
    if (offset < 0) {
        set_errbuf(errbuf, errbuf_len, "offset must be a non-negative integer", NULL);
        return ML_ERR_VALIDATION;
    }
    return ML_OK;
}
