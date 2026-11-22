/*
 * test_json.c — host tests for the core JSON reader.
 */

#include "core/json.h"
#include "test_util.h"

#include <string.h>

static char _arena_buf[4096];
static json_arena_t _arena;

static json_value_t *parse(const char *text) {
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    return json_parse(&_arena, text, strlen(text));
}

void test_json(void) {
    json_value_t *root = parse("{\"schema\":1,\"name\":\"repo\",\"ok\":true}");
    CHECK(root != NULL);
    CHECK_EQ(json_type(root), JSON_OBJECT);
    CHECK_EQ(json_count(root), 3);
    CHECK_EQ(json_int_or(json_get(root, "schema"), -1), 1);
    CHECK_STR_EQ(json_string(json_get(root, "name")), "repo");
    CHECK(json_boolean(json_get(root, "ok")));
    CHECK(json_get(root, "missing") == NULL);
    CHECK_STR_EQ(json_key_at(root, 1), "name");

    root = parse("[1,2.5,-3,\"x\",null,false]");
    CHECK(root != NULL);
    CHECK_EQ(json_type(root), JSON_ARRAY);
    CHECK_EQ(json_count(root), 6);
    CHECK_EQ(json_type(json_at(root, 0)), JSON_NUMBER);
    CHECK(json_number(json_at(root, 2)) == -3.0);
    CHECK_STR_EQ(json_string(json_at(root, 3)), "x");
    CHECK_EQ(json_type(json_at(root, 4)), JSON_NULL);
    CHECK(!json_boolean(json_at(root, 5)));
    CHECK(json_at(root, 6) == NULL);

    root = parse("\"a\\\"b\\\\c\\n\\u00e9\"");
    CHECK(root != NULL);
    CHECK_STR_EQ(json_string(root), "a\"b\\c\n\xc3\xa9");
    CHECK_EQ(json_string_len(root), 8);

    root = parse("{\"packages\":[{\"name\":\"a\"},{\"name\":\"b\"}]}");
    CHECK(root != NULL);
    const json_value_t *list = json_get(root, "packages");
    CHECK(list != NULL);
    CHECK_EQ(json_count(list), 2);
    CHECK_STR_EQ(json_string(json_get(json_at(list, 1), "name")), "b");

    /* Numbers keep exponent and fraction form. */
    root = parse("1.5e2");
    CHECK(root != NULL);
    CHECK(json_number(root) == 150.0);

    /* Whitespace is insignificant between tokens. */
    root = parse("  {\n\t\"a\" : [ 1 , 2 ]\n}  ");
    CHECK(root != NULL);
    CHECK_EQ(json_count(json_get(root, "a")), 2);

    /* Error cases. */
    CHECK(parse("{\"a\":1} extra") == NULL);
    CHECK(json_last_error() != NULL);
    CHECK(parse("{\"a\":\"unterminated") == NULL);
    CHECK(parse("[1,]") == NULL);
    CHECK(parse("{\"a\" 1}") == NULL);
    CHECK(parse("tru") == NULL);
    CHECK(parse("") == NULL);

    /* Arena exhaustion is reported, not crashed. */
    json_arena_init(&_arena, _arena_buf, 16);
    CHECK(json_parse(&_arena, "{\"a\":\"0123456789abcdef\"}", 24) == NULL);

    /* Deep nesting is rejected rather than recursing without bound. */
    char deep[128];
    size_t pos = 0;
    for (int i = 0; i < 40; i++)
        deep[pos++] = '[';
    for (int i = 0; i < 40; i++)
        deep[pos++] = ']';
    deep[pos] = '\0';
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    CHECK(json_parse(&_arena, deep, pos) == NULL);
}
