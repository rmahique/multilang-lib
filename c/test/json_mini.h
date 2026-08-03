/*
 * A minimal JSON reader, scoped to what the conformance test needs:
 * objects, arrays, strings, null, and true/false. No numbers, since
 * conformance/cases.json doesn't use any.
 *
 * This is test-only tooling (not part of the shipped library) written
 * specifically for this one fixed, controlled file, rather than vendoring
 * a general-purpose third-party JSON library — smaller surface, no
 * supply-chain dependency for something this narrow.
 */

#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type;

typedef struct json_value json_value;

struct json_value {
    json_type type;
    union {
        int boolean;
        char *string;
        struct {
            json_value **items;
            size_t count;
        } array;
        struct {
            char **keys;
            json_value **values;
            size_t count;
        } object;
    } as;
};

/* Parses `text` in place. Returns NULL on malformed input. Caller must
 * free the result with json_free(). */
json_value *json_parse(const char *text);

void json_free(json_value *v);

/* Object/array accessors. Return NULL if not found / wrong type. */
json_value *json_object_get(const json_value *obj, const char *key);
json_value *json_array_get(const json_value *arr, size_t index);
size_t json_array_size(const json_value *arr);

/* Returns v->as.string if v is a JSON_STRING, else `fallback`. */
const char *json_as_string(const json_value *v, const char *fallback);

int json_is_null(const json_value *v);
int json_is_true(const json_value *v);

#ifdef __cplusplus
}
#endif

#endif /* JSON_MINI_H */
