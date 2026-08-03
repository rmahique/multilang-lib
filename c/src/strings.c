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
