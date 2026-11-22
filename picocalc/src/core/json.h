#pragma once
/*
 * core/json.h — minimal JSON reader for repository indexes and state files.
 *
 * The parser works inside a caller-supplied arena so a document can be
 * dropped in one step without heap churn. Values are immutable once parsed.
 * Accessors walk linked children, which keeps node sizes small on RP2040.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type_t;

typedef struct json_value json_value_t;

typedef struct json_item {
    json_value_t *value;
    const char *key; /* NULL for array elements */
    struct json_item *next;
} json_item_t;

struct json_value {
    uint8_t type;
    union {
        bool boolean;
        double number;
        struct {
            const char *ptr;
            size_t len;
        } string;
        struct {
            json_item_t *first;
            size_t count;
        } container;
    } u;
};

typedef struct {
    char *buf;
    size_t cap;
    size_t used;
} json_arena_t;

void json_arena_init(json_arena_t *arena, char *buf, size_t cap);

/* Parse `text[0..len)`. Returns NULL and sets json_last_error() on failure. */
json_value_t *json_parse(json_arena_t *arena, const char *text, size_t len);

const char *json_last_error(void);

json_type_t json_type(const json_value_t *value);
const char *json_string(const json_value_t *value);
size_t json_string_len(const json_value_t *value);
double json_number(const json_value_t *value);
bool json_boolean(const json_value_t *value);
size_t json_count(const json_value_t *value);
const json_value_t *json_at(const json_value_t *value, size_t index);
const char *json_key_at(const json_value_t *value, size_t index);
const json_value_t *json_get(const json_value_t *value, const char *key);

/* Integer accessor with default when the value is absent or not a number. */
long long json_int_or(const json_value_t *value, long long fallback);
