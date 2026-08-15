/*
 * The two public data functions: ml_retrieve_data and ml_insert_data.
 *
 * Both take an already-open ml_backend (from ml_connect) so the caller
 * controls connection lifetime; neither function opens or closes a
 * connection itself.
 */

#include "../include/multilang.h"
#include "backend.h"
#include "validation.h"

#include <openssl/evp.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *sha256_hex(const char *text)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, text, strlen(text));
    EVP_DigestFinal_ex(mdctx, digest, &digest_len);
    EVP_MD_CTX_free(mdctx);

    char *hex = malloc(digest_len * 2 + 1);
    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[digest_len * 2] = '\0';
    return hex;
}

ml_status ml_retrieve_data(ml_backend *conn, const char *string_id,
                            const char *language_id, const char *context,
                            char **out_content, char *errbuf, size_t errbuf_len)
{
    char valid_string_id[ML_MAX_STRING_ID_LEN + 1];
    char valid_language_id[ML_MAX_LANGUAGE_ID_LEN + 1];
    char valid_context[ML_MAX_CONTEXT_LEN + 1];
    ml_status status;

    *out_content = NULL;

    if ((status = ml_validate_string_id(string_id, valid_string_id, sizeof(valid_string_id),
                                         errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_language_id(language_id, valid_language_id, sizeof(valid_language_id),
                                           errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_context(context, valid_context, sizeof(valid_context),
                                       errbuf, errbuf_len)) != ML_OK) {
        return status;
    }

    int found = 0;
    status = conn->vtable->select_content(conn->ctx, valid_string_id, valid_language_id,
                                           valid_context, out_content, &found, errbuf, errbuf_len);
    return status;
}

ml_status ml_insert_data(ml_backend *conn, const char *string_id,
                          const char *language_id, const char *content,
                          const ml_insert_options *opts, char *errbuf, size_t errbuf_len)
{
    static const ml_insert_options DEFAULT_OPTS = {0};
    if (opts == NULL) {
        opts = &DEFAULT_OPTS;
    }

    char valid_string_id[ML_MAX_STRING_ID_LEN + 1];
    char valid_language_id[ML_MAX_LANGUAGE_ID_LEN + 1];
    char valid_context[ML_MAX_CONTEXT_LEN + 1];
    char valid_original_language[ML_MAX_LANGUAGE_ID_LEN + 1];
    char valid_updated_by[ML_MAX_UPDATED_BY_LEN + 1];
    ml_status status;

    if ((status = ml_validate_string_id(string_id, valid_string_id, sizeof(valid_string_id),
                                         errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_language_id(language_id, valid_language_id, sizeof(valid_language_id),
                                           errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_context(opts->context, valid_context, sizeof(valid_context),
                                       errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_content(content, errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_optional_language_id(opts->original_language, valid_original_language,
                                                    sizeof(valid_original_language), errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    const char *status_value = (opts->status != NULL && opts->status[0] != '\0') ? opts->status : "draft";
    if ((status = ml_validate_status(status_value, errbuf, errbuf_len)) != ML_OK) {
        return status;
    }
    if ((status = ml_validate_updated_by(opts->updated_by, valid_updated_by, sizeof(valid_updated_by),
                                          errbuf, errbuf_len)) != ML_OK) {
        return status;
    }

    char *source_checksum = NULL;
    if (valid_original_language[0] != '\0') {
        char *source_content = NULL;
        int found = 0;
        status = conn->vtable->select_content(conn->ctx, valid_string_id, valid_original_language,
                                               valid_context, &source_content, &found, errbuf, errbuf_len);
        if (status != ML_OK) {
            return status;
        }
        if (found) {
            source_checksum = sha256_hex(source_content);
            free(source_content);
        }
    }

    ml_row row;
    row.string_id = valid_string_id;
    row.language_id = valid_language_id;
    row.context = valid_context;
    row.content = content;
    row.original_language = valid_original_language[0] != '\0' ? valid_original_language : NULL;
    row.status = status_value;
    row.source_checksum = source_checksum;
    row.updated_by = valid_updated_by[0] != '\0' ? valid_updated_by : NULL;
    row.date_updated = time(NULL); /* UTC instant; each backend formats it for its own column type */

    status = conn->vtable->upsert(conn->ctx, &row, errbuf, errbuf_len);
    free(source_checksum);
    return status;
}

/*
 * Lowercases only the ASCII A-Z range into a newly malloc'd copy, leaving
 * every other byte untouched. Deliberately not libc's tolower() -- see
 * validation.c's to_lower_copy for why this project never trusts a
 * locale-dependent routine for cross-language guarantees; docs/search.md
 * documents the resulting limitation that non-ASCII letters only match
 * by exact case.
 */
static char *ascii_fold(const char *text)
{
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) text[i];
        out[i] = (char) ((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    out[len] = '\0';
    return out;
}

/* Counts non-overlapping occurrences of needle in haystack. */
static int count_occurrences(const char *haystack, const char *needle)
{
    if (needle[0] == '\0') {
        return 0;
    }
    int count = 0;
    const char *p = haystack;
    size_t needle_len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

/* Counts non-overlapping matches of a compiled regex in content. */
static int count_regex_matches(const regex_t *pattern, const char *content)
{
    int count = 0;
    const char *p = content;
    regmatch_t match;
    while (*p != '\0' && regexec(pattern, p, 1, &match, 0) == 0) {
        count++;
        /* A zero-length match (e.g. pattern "a*" against "b") would loop
         * forever advancing by 0 -- step by one byte instead so progress
         * is always made. */
        p += (match.rm_eo == match.rm_so) ? match.rm_eo + 1 : match.rm_eo;
    }
    return count;
}

/*
 * Returns how many times `query` (or, for natural/regex, its
 * pre-processed form in `q`) matches `content` under `mode` -- 0 means
 * no match. See docs/search.md for the exact/natural/regex semantics.
 */
static int score_row(const char *content, ml_search_mode mode, const char *query,
                      const ml_search_query *q, int case_sensitive)
{
    if (mode == ML_SEARCH_REGEX) {
        return count_regex_matches(q->pattern, content);
    }

    char *folded_content = case_sensitive ? NULL : ascii_fold(content);
    const char *haystack = case_sensitive ? content : folded_content;
    int result;

    if (mode == ML_SEARCH_EXACT) {
        char *folded_query = case_sensitive ? NULL : ascii_fold(query);
        const char *needle = case_sensitive ? query : folded_query;
        result = count_occurrences(haystack, needle);
        free(folded_query);
    } else {
        /* natural: every term must appear at least once (AND); score is
         * the sum of each term's occurrence count. */
        int total = 0;
        for (size_t i = 0; q->terms[i] != NULL; i++) {
            char *folded_term = case_sensitive ? NULL : ascii_fold(q->terms[i]);
            const char *needle = case_sensitive ? q->terms[i] : folded_term;
            int occurrences = count_occurrences(haystack, needle);
            free(folded_term);
            if (occurrences == 0) {
                total = 0;
                break;
            }
            total += occurrences;
        }
        result = total;
    }
    free(folded_content);
    return result;
}

typedef struct {
    int score;
    ml_backend_row *row;
} scored_row;

/* Score descending, then (language_id, string_id, context) ascending as
 * a deterministic tiebreak. */
static int compare_scored(const void *a, const void *b)
{
    const scored_row *ra = (const scored_row *) a;
    const scored_row *rb = (const scored_row *) b;
    if (ra->score != rb->score) {
        return rb->score - ra->score;
    }
    int cmp = strcmp(ra->row->language_id, rb->row->language_id);
    if (cmp != 0) {
        return cmp;
    }
    cmp = strcmp(ra->row->string_id, rb->row->string_id);
    if (cmp != 0) {
        return cmp;
    }
    return strcmp(ra->row->context, rb->row->context);
}

ml_status ml_search_data(ml_backend *conn, const char *query, ml_search_mode mode,
                          const ml_search_options *opts,
                          ml_search_result **out_results, size_t *out_count,
                          char *errbuf, size_t errbuf_len)
{
    static const ml_search_options DEFAULT_OPTS = {0};
    if (opts == NULL) {
        opts = &DEFAULT_OPTS;
    }

    *out_results = NULL;
    *out_count = 0;

    const char *mode_str = "natural";
    if (mode == ML_SEARCH_EXACT) {
        mode_str = "exact";
    } else if (mode == ML_SEARCH_REGEX) {
        mode_str = "regex";
    } else if (mode != ML_SEARCH_NATURAL) {
        mode_str = ""; /* invalid enum value -- ml_validate_search_mode below rejects it */
    }

    ml_status status = ml_validate_search_mode(mode_str, errbuf, errbuf_len);
    if (status != ML_OK) {
        return status;
    }

    ml_search_query q;
    status = ml_validate_search_query(query, mode_str, opts->case_sensitive, &q, errbuf, errbuf_len);
    if (status != ML_OK) {
        return status;
    }

    char valid_language_id[ML_MAX_LANGUAGE_ID_LEN + 1];
    const char *language_id_filter = NULL;
    if (opts->language_id != NULL && opts->language_id[0] != '\0') {
        if ((status = ml_validate_language_id(opts->language_id, valid_language_id, sizeof(valid_language_id),
                                               errbuf, errbuf_len)) != ML_OK) {
            ml_free_search_query(&q);
            return status;
        }
        language_id_filter = valid_language_id;
    }

    char valid_context[ML_MAX_CONTEXT_LEN + 1];
    const char *context_filter = NULL;
    if (opts->context != NULL) {
        if ((status = ml_validate_context(opts->context, valid_context, sizeof(valid_context),
                                           errbuf, errbuf_len)) != ML_OK) {
            ml_free_search_query(&q);
            return status;
        }
        context_filter = valid_context;
    }

    const char *status_filter = NULL;
    if (opts->status != NULL && opts->status[0] != '\0') {
        if ((status = ml_validate_status(opts->status, errbuf, errbuf_len)) != ML_OK) {
            ml_free_search_query(&q);
            return status;
        }
        status_filter = opts->status;
    }

    int limit = opts->limit == 0 ? ML_DEFAULT_SEARCH_LIMIT : opts->limit;
    int offset = opts->offset;
    if ((status = ml_validate_search_pagination(limit, offset, errbuf, errbuf_len)) != ML_OK) {
        ml_free_search_query(&q);
        return status;
    }

    ml_backend_row *rows = NULL;
    size_t row_count = 0;
    status = conn->vtable->select_rows(conn->ctx, language_id_filter, context_filter, status_filter,
                                        &rows, &row_count, errbuf, errbuf_len);
    if (status != ML_OK) {
        ml_free_search_query(&q);
        return status;
    }

    scored_row *scored = malloc((row_count > 0 ? row_count : 1) * sizeof(scored_row));
    size_t scored_count = 0;
    for (size_t i = 0; i < row_count; i++) {
        int score = score_row(rows[i].content, mode, query, &q, opts->case_sensitive);
        if (score > 0) {
            scored[scored_count].score = score;
            scored[scored_count].row = &rows[i];
            scored_count++;
        }
    }
    ml_free_search_query(&q);

    qsort(scored, scored_count, sizeof(scored_row), compare_scored);

    size_t start = (size_t) offset < scored_count ? (size_t) offset : scored_count;
    size_t end = start + (size_t) limit;
    if (end > scored_count) {
        end = scored_count;
    }
    size_t result_count = end - start;

    /* Ownership of each paged-in row's fields transfers to *out_results
     * by nulling the ml_backend_row's pointers as they're copied out;
     * ml_backend_free_rows below then only frees what wasn't paged in
     * (non-matches and matches beyond limit/offset still own theirs). */
    ml_search_result *results = malloc((result_count > 0 ? result_count : 1) * sizeof(ml_search_result));
    for (size_t i = 0; i < result_count; i++) {
        ml_backend_row *r = scored[start + i].row;
        results[i].string_id = r->string_id;
        r->string_id = NULL;
        results[i].language_id = r->language_id;
        r->language_id = NULL;
        results[i].context = r->context;
        r->context = NULL;
        results[i].content = r->content;
        r->content = NULL;
        results[i].original_language = r->original_language;
        r->original_language = NULL;
        results[i].status = r->status;
        r->status = NULL;
        results[i].source_checksum = r->source_checksum;
        r->source_checksum = NULL;
        results[i].updated_by = r->updated_by;
        r->updated_by = NULL;
        results[i].date_updated = r->date_updated;
    }
    free(scored);
    ml_backend_free_rows(rows, row_count);

    *out_results = results;
    *out_count = result_count;
    return ML_OK;
}

void ml_free_search_results(ml_search_result *results, size_t count)
{
    if (results == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(results[i].string_id);
        free(results[i].language_id);
        free(results[i].context);
        free(results[i].content);
        free(results[i].original_language);
        free(results[i].status);
        free(results[i].source_checksum);
        free(results[i].updated_by);
    }
    free(results);
}
