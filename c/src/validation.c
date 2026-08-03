#include "validation.h"

#include <ctype.h>
#include <regex.h>
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
