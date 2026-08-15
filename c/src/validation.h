/*
 * Input validation — every value that reaches SQL goes through here
 * first.
 *
 * All checks are allow-list based (reject anything that doesn't match a
 * known-good shape) rather than deny-list based (reject known-bad
 * patterns).
 *
 * Every id-shaped value (language_id, original_language, string_id,
 * context) is normalized to lowercase by default, always — they're all
 * part of the exact-match composite primary key, so casing differences
 * would otherwise split what should be one row into duplicates.
 *
 * These rules must stay identical to every other language port's
 * validation module — see ../../conformance/README.md.
 *
 * Every ml_validate_* function writes its (possibly lowercased) result
 * into a caller-supplied buffer and bounds every write to that buffer's
 * size — no unbounded copy ever happens here.
 */

#ifndef MULTILANG_VALIDATION_H
#define MULTILANG_VALIDATION_H

#include <regex.h>
#include <stddef.h>
#include "../include/multilang.h"

/* Validate a BCP 47 language tag; writes it lowercased into `out`. */
ml_status ml_validate_language_id(const char *in, char *out, size_t out_size,
                                   char *errbuf, size_t errbuf_len);

/*
 * Like ml_validate_language_id, but NULL/"" means "not provided": out[0]
 * is set to '\0' and ML_OK is returned instead of erroring.
 */
ml_status ml_validate_optional_language_id(const char *in, char *out, size_t out_size,
                                            char *errbuf, size_t errbuf_len);

/* Validate a string_id; writes it lowercased into `out`. */
ml_status ml_validate_string_id(const char *in, char *out, size_t out_size,
                                 char *errbuf, size_t errbuf_len);

/*
 * Validate a context value. NULL is treated as "no context" and
 * normalized to "" (the default row). Writes lowercased into `out`.
 */
ml_status ml_validate_context(const char *in, char *out, size_t out_size,
                               char *errbuf, size_t errbuf_len);

/*
 * Validate the text to be stored: non-empty, within ML_MAX_CONTENT_LEN,
 * and free of NUL bytes. Not copied — content isn't case-normalized, so
 * callers keep using the original pointer after a successful validation.
 */
ml_status ml_validate_content(const char *in, char *errbuf, size_t errbuf_len);

/* Validate that `in` is one of "draft"/"reviewed"/"published". */
ml_status ml_validate_status(const char *in, char *errbuf, size_t errbuf_len);

/*
 * Validate the optional audit-trail field. NULL/"" means "not provided":
 * out[0] is set to '\0' and ML_OK is returned.
 */
ml_status ml_validate_updated_by(const char *in, char *out, size_t out_size,
                                  char *errbuf, size_t errbuf_len);

/* Validate that `in` is one of "exact"/"natural"/"regex". */
ml_status ml_validate_search_mode(const char *in, char *errbuf, size_t errbuf_len);

/*
 * ml_search_data's pre-processed query, produced by
 * ml_validate_search_query and released by ml_free_search_query. Exactly
 * one field is populated, matching mode: "exact" -> neither ("in" itself
 * is the needle), "natural" -> terms (a NULL-terminated array of
 * malloc'd term strings), "regex" -> pattern (a malloc'd, already-
 * compiled POSIX extended regex, case-insensitivity already baked in via
 * REG_ICASE if requested).
 */
typedef struct {
    char **terms;
    regex_t *pattern;
} ml_search_query;

/*
 * Validate a search_data query and pre-process it into the form the
 * matcher for `mode` actually needs, so ml_search_data doesn't re-derive
 * it per row. Writes into *out (terms/pattern start NULL either way).
 */
ml_status ml_validate_search_query(const char *in, const char *mode, int case_sensitive,
                                    ml_search_query *out, char *errbuf, size_t errbuf_len);

/* Releases whatever ml_validate_search_query allocated into *q. */
void ml_free_search_query(ml_search_query *q);

/* Validate ml_search_data's limit/offset. */
ml_status ml_validate_search_pagination(int limit, int offset, char *errbuf, size_t errbuf_len);

#endif /* MULTILANG_VALIDATION_H */
