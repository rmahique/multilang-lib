#include "json_mini.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
} parser;

static void skip_ws(parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') {
        ps->p++;
    }
}

static json_value *parse_value(parser *ps);

static json_value *alloc_value(json_type type)
{
    json_value *v = calloc(1, sizeof(json_value));
    v->type = type;
    return v;
}

/* Decodes a JSON string starting at the opening quote; leaves ps->p just
 * past the closing quote. Returns a malloc'd, NUL-terminated C string, or
 * NULL on malformed input. */
static char *parse_raw_string(parser *ps)
{
    if (*ps->p != '"') {
        return NULL;
    }
    ps->p++;

    size_t cap = 32, len = 0;
    char *out = malloc(cap);

    while (*ps->p != '"') {
        if (*ps->p == '\0') {
            free(out);
            return NULL; /* unterminated string */
        }
        char c = *ps->p;
        if (c == '\\') {
            ps->p++;
            switch (*ps->p) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                default: c = *ps->p; break; /* \uXXXX not needed by our fixture */
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[len++] = c;
        ps->p++;
    }
    ps->p++; /* closing quote */
    out[len] = '\0';
    return out;
}

static json_value *parse_string(parser *ps)
{
    char *s = parse_raw_string(ps);
    if (s == NULL) {
        return NULL;
    }
    json_value *v = alloc_value(JSON_STRING);
    v->as.string = s;
    return v;
}

static json_value *parse_array(parser *ps)
{
    ps->p++; /* '[' */
    json_value *v = alloc_value(JSON_ARRAY);
    size_t cap = 8;
    v->as.array.items = malloc(cap * sizeof(json_value *));
    v->as.array.count = 0;

    skip_ws(ps);
    if (*ps->p == ']') {
        ps->p++;
        return v;
    }

    while (1) {
        skip_ws(ps);
        json_value *item = parse_value(ps);
        if (item == NULL) {
            json_free(v);
            return NULL;
        }
        if (v->as.array.count >= cap) {
            cap *= 2;
            v->as.array.items = realloc(v->as.array.items, cap * sizeof(json_value *));
        }
        v->as.array.items[v->as.array.count++] = item;

        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            break;
        }
        json_free(v);
        return NULL;
    }
    return v;
}

static json_value *parse_object(parser *ps)
{
    ps->p++; /* '{' */
    json_value *v = alloc_value(JSON_OBJECT);
    size_t cap = 8;
    v->as.object.keys = malloc(cap * sizeof(char *));
    v->as.object.values = malloc(cap * sizeof(json_value *));
    v->as.object.count = 0;

    skip_ws(ps);
    if (*ps->p == '}') {
        ps->p++;
        return v;
    }

    while (1) {
        skip_ws(ps);
        char *key = parse_raw_string(ps);
        if (key == NULL) {
            json_free(v);
            return NULL;
        }
        skip_ws(ps);
        if (*ps->p != ':') {
            free(key);
            json_free(v);
            return NULL;
        }
        ps->p++;
        skip_ws(ps);
        json_value *val = parse_value(ps);
        if (val == NULL) {
            free(key);
            json_free(v);
            return NULL;
        }

        if (v->as.object.count >= cap) {
            cap *= 2;
            v->as.object.keys = realloc(v->as.object.keys, cap * sizeof(char *));
            v->as.object.values = realloc(v->as.object.values, cap * sizeof(json_value *));
        }
        v->as.object.keys[v->as.object.count] = key;
        v->as.object.values[v->as.object.count] = val;
        v->as.object.count++;

        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            break;
        }
        json_free(v);
        return NULL;
    }
    return v;
}

/* Plain integers only (optional leading '-', digits) -- the only numeric
 * shape conformance/cases.json ever uses. */
static json_value *parse_number(parser *ps)
{
    const char *start = ps->p;
    if (*ps->p == '-') {
        ps->p++;
    }
    while (*ps->p >= '0' && *ps->p <= '9') {
        ps->p++;
    }
    if (ps->p == start || (ps->p == start + 1 && start[0] == '-')) {
        return NULL; /* no digits consumed */
    }
    json_value *v = alloc_value(JSON_NUMBER);
    v->as.number = strtol(start, NULL, 10);
    return v;
}

static json_value *parse_value(parser *ps)
{
    skip_ws(ps);
    if (*ps->p == '"') return parse_string(ps);
    if (*ps->p == '{') return parse_object(ps);
    if (*ps->p == '[') return parse_array(ps);
    if (strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return alloc_value(JSON_NULL);
    }
    if (strncmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        json_value *v = alloc_value(JSON_BOOL);
        v->as.boolean = 1;
        return v;
    }
    if (strncmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        json_value *v = alloc_value(JSON_BOOL);
        v->as.boolean = 0;
        return v;
    }
    if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
        return parse_number(ps);
    }
    return NULL;
}

json_value *json_parse(const char *text)
{
    parser ps = {text};
    json_value *v = parse_value(&ps);
    if (v == NULL) {
        return NULL;
    }
    skip_ws(&ps);
    return v;
}

void json_free(json_value *v)
{
    if (v == NULL) {
        return;
    }
    switch (v->type) {
        case JSON_STRING:
            free(v->as.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) {
                json_free(v->as.array.items[i]);
            }
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.object.count; i++) {
                free(v->as.object.keys[i]);
                json_free(v->as.object.values[i]);
            }
            free(v->as.object.keys);
            free(v->as.object.values);
            break;
        default:
            break;
    }
    free(v);
}

json_value *json_object_get(const json_value *obj, const char *key)
{
    if (obj == NULL || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.keys[i], key) == 0) {
            return obj->as.object.values[i];
        }
    }
    return NULL;
}

json_value *json_array_get(const json_value *arr, size_t index)
{
    if (arr == NULL || arr->type != JSON_ARRAY || index >= arr->as.array.count) {
        return NULL;
    }
    return arr->as.array.items[index];
}

size_t json_array_size(const json_value *arr)
{
    return (arr != NULL && arr->type == JSON_ARRAY) ? arr->as.array.count : 0;
}

const char *json_as_string(const json_value *v, const char *fallback)
{
    return (v != NULL && v->type == JSON_STRING) ? v->as.string : fallback;
}

long json_as_int(const json_value *v, long fallback)
{
    return (v != NULL && v->type == JSON_NUMBER) ? v->as.number : fallback;
}

int json_is_null(const json_value *v)
{
    return v == NULL || v->type == JSON_NULL;
}

int json_is_true(const json_value *v)
{
    return v != NULL && v->type == JSON_BOOL && v->as.boolean;
}
