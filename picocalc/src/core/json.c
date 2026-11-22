/*
 * core/json.c — minimal JSON reader.
 *
 * Recursive-descent parser with an explicit depth limit. All nodes, keys and
 * decoded strings live in the caller's arena, so a document is released by
 * re-initialising the arena.
 */

#include "core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 16
#define JSON_ALIGN ((size_t)_Alignof(max_align_t))

static char _error[72];

const char *json_last_error(void) {
    return _error;
}

static void _fail(const char *msg, size_t pos) {
    snprintf(_error, sizeof _error, "%s at byte %lu", msg, (unsigned long)pos);
}

void json_arena_init(json_arena_t *arena, char *buf, size_t cap) {
    if (!arena) return;
    size_t offset = 0;
    if (buf) {
        uintptr_t addr = (uintptr_t)buf;
        size_t misalign = (size_t)(addr % JSON_ALIGN);
        if (misalign) offset = JSON_ALIGN - misalign;
        if (offset >= cap) offset = cap;
    }
    arena->buf = buf ? buf + offset : NULL;
    arena->cap = buf ? cap - offset : 0;
    arena->used = 0;
}

static void *_arena_alloc(json_arena_t *arena, size_t size) {
    if (!arena || !arena->buf) return NULL;
    size = (size + JSON_ALIGN - 1u) & ~(size_t)(JSON_ALIGN - 1u);
    if (size > arena->cap - arena->used) return NULL;
    void *ptr = arena->buf + arena->used;
    arena->used += size;
    return ptr;
}

static json_value_t *_new_value(json_arena_t *arena, json_type_t type) {
    json_value_t *value = _arena_alloc(arena, sizeof *value);
    if (!value) return NULL;
    memset(value, 0, sizeof *value);
    value->type = (uint8_t)type;
    return value;
}

typedef struct {
    const char *text;
    size_t len;
    size_t pos;
    json_arena_t *arena;
    int depth;
} parser_t;

static void _skip_ws(parser_t *p) {
    while (p->pos < p->len) {
        char c = p->text[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else
            break;
    }
}

static bool _peek(parser_t *p, char *out) {
    if (p->pos >= p->len) return false;
    *out = p->text[p->pos];
    return true;
}

static int _hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool _read_hex4(parser_t *p, unsigned *out) {
    if (p->pos + 4 > p->len) return false;
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = _hex_digit(p->text[p->pos + (size_t)i]);
        if (digit < 0) return false;
        value = (value << 4) | (unsigned)digit;
    }
    p->pos += 4;
    *out = value;
    return true;
}

/* Decode one JSON string into the arena. `p->pos` must be at the quote. */
static bool _parse_string(parser_t *p, const char **out_ptr, size_t *out_len) {
    if (p->pos >= p->len || p->text[p->pos] != '"') {
        _fail("expected string", p->pos);
        return false;
    }
    p->pos++;

    /* Worst case the decoded text is no longer than the encoded text. */
    size_t start = p->arena->used;
    char *dst = _arena_alloc(p->arena, p->len - p->pos + 1);
    if (!dst) {
        _fail("arena exhausted", p->pos);
        return false;
    }
    size_t used = 0;

    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->text[p->pos++];
        if (c == '"') {
            dst[used] = '\0';
            *out_ptr = dst;
            *out_len = used;
            /* Reclaim the over-allocation so the arena stays compact. */
            p->arena->used = start + ((used + 1u + JSON_ALIGN - 1u) & ~(size_t)(JSON_ALIGN - 1u));
            return true;
        }
        if (c < 0x20) {
            _fail("control character in string", p->pos - 1);
            return false;
        }
        if (c != '\\') {
            dst[used++] = (char)c;
            continue;
        }
        if (p->pos >= p->len) break;
        char esc = p->text[p->pos++];
        switch (esc) {
        case '"':
            dst[used++] = '"';
            break;
        case '\\':
            dst[used++] = '\\';
            break;
        case '/':
            dst[used++] = '/';
            break;
        case 'b':
            dst[used++] = '\b';
            break;
        case 'f':
            dst[used++] = '\f';
            break;
        case 'n':
            dst[used++] = '\n';
            break;
        case 'r':
            dst[used++] = '\r';
            break;
        case 't':
            dst[used++] = '\t';
            break;
        case 'u': {
            unsigned cp;
            if (!_read_hex4(p, &cp)) {
                _fail("bad unicode escape", p->pos);
                return false;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                unsigned low;
                if (p->pos + 1 >= p->len || p->text[p->pos] != '\\' || p->text[p->pos + 1] != 'u') {
                    _fail("unpaired surrogate", p->pos);
                    return false;
                }
                p->pos += 2;
                if (!_read_hex4(p, &low)) {
                    _fail("bad unicode escape", p->pos);
                    return false;
                }
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
            }
            if (cp < 0x80u) {
                dst[used++] = (char)cp;
            } else if (cp < 0x800u) {
                dst[used++] = (char)(0xC0u | (cp >> 6));
                dst[used++] = (char)(0x80u | (cp & 0x3Fu));
            } else if (cp < 0x10000u) {
                dst[used++] = (char)(0xE0u | (cp >> 12));
                dst[used++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                dst[used++] = (char)(0x80u | (cp & 0x3Fu));
            } else {
                dst[used++] = (char)(0xF0u | (cp >> 18));
                dst[used++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
                dst[used++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                dst[used++] = (char)(0x80u | (cp & 0x3Fu));
            }
            break;
        }
        default:
            _fail("unknown escape", p->pos - 1);
            return false;
        }
    }
    _fail("unterminated string", p->pos);
    return false;
}

static json_value_t *_parse_value(parser_t *p);

static bool _parse_number(parser_t *p, double *out) {
    size_t start = p->pos;
    if (p->pos < p->len && (p->text[p->pos] == '-' || p->text[p->pos] == '+')) p->pos++;
    bool digits = false;
    while (p->pos < p->len && p->text[p->pos] >= '0' && p->text[p->pos] <= '9') {
        p->pos++;
        digits = true;
    }
    if (p->pos < p->len && p->text[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && p->text[p->pos] >= '0' && p->text[p->pos] <= '9') {
            p->pos++;
            digits = true;
        }
    }
    if (!digits) {
        _fail("invalid number", start);
        return false;
    }
    if (p->pos < p->len && (p->text[p->pos] == 'e' || p->text[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->text[p->pos] == '-' || p->text[p->pos] == '+')) p->pos++;
        bool exp_digits = false;
        while (p->pos < p->len && p->text[p->pos] >= '0' && p->text[p->pos] <= '9') {
            p->pos++;
            exp_digits = true;
        }
        if (!exp_digits) {
            _fail("invalid exponent", p->pos);
            return false;
        }
    }

    char tmp[32];
    size_t n = p->pos - start;
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, p->text + start, n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    return true;
}

static bool _match_literal(parser_t *p, const char *literal) {
    size_t n = strlen(literal);
    if (p->pos + n > p->len) return false;
    if (memcmp(p->text + p->pos, literal, n) != 0) return false;
    p->pos += n;
    return true;
}

static json_value_t *_parse_value(parser_t *p) {
    _skip_ws(p);
    if (p->pos >= p->len) {
        _fail("unexpected end of input", p->pos);
        return NULL;
    }
    char c = p->text[p->pos];

    if (c == '{' || c == '[') {
        bool is_object = (c == '{');
        char close = is_object ? '}' : ']';
        p->pos++;
        if (p->depth >= JSON_MAX_DEPTH) {
            _fail("nesting too deep", p->pos);
            return NULL;
        }
        json_value_t *value = _new_value(p->arena, is_object ? JSON_OBJECT : JSON_ARRAY);
        if (!value) {
            _fail("arena exhausted", p->pos);
            return NULL;
        }
        p->depth++;
        json_item_t **tail = &value->u.container.first;

        _skip_ws(p);
        char next;
        if (_peek(p, &next) && next == (is_object ? '}' : ']')) {
            p->pos++;
            p->depth--;
            return value;
        }
        for (;;) {
            json_item_t *item = _arena_alloc(p->arena, sizeof *item);
            if (!item) {
                _fail("arena exhausted", p->pos);
                return NULL;
            }
            memset(item, 0, sizeof *item);
            *tail = item;
            tail = &item->next;

            if (is_object) {
                _skip_ws(p);
                const char *key = NULL;
                size_t key_len = 0;
                if (!_parse_string(p, &key, &key_len)) return NULL;
                (void)key_len;
                item->key = key;
                _skip_ws(p);
                if (p->pos >= p->len || p->text[p->pos] != ':') {
                    _fail("expected ':'", p->pos);
                    return NULL;
                }
                p->pos++;
            }

            item->value = _parse_value(p);
            if (!item->value) return NULL;
            value->u.container.count++;

            _skip_ws(p);
            if (p->pos >= p->len) {
                _fail("unterminated container", p->pos);
                return NULL;
            }
            char sep = p->text[p->pos++];
            if (sep == close) break;
            if (sep != ',') {
                _fail("expected ',' or close", p->pos - 1);
                return NULL;
            }
        }
        p->depth--;
        return value;
    }

    if (c == '"') {
        const char *ptr = NULL;
        size_t len = 0;
        if (!_parse_string(p, &ptr, &len)) return NULL;
        json_value_t *value = _new_value(p->arena, JSON_STRING);
        if (!value) {
            _fail("arena exhausted", p->pos);
            return NULL;
        }
        value->u.string.ptr = ptr;
        value->u.string.len = len;
        return value;
    }

    if (_match_literal(p, "true") || _match_literal(p, "false")) {
        bool flag = p->text[p->pos - 1] == 'e' && p->text[p->pos - 2] == 'u';
        json_value_t *value = _new_value(p->arena, JSON_BOOL);
        if (!value) return NULL;
        value->u.boolean = flag;
        return value;
    }

    if (_match_literal(p, "null")) {
        return _new_value(p->arena, JSON_NULL);
    }

    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
        double number = 0;
        if (!_parse_number(p, &number)) return NULL;
        json_value_t *value = _new_value(p->arena, JSON_NUMBER);
        if (!value) {
            _fail("arena exhausted", p->pos);
            return NULL;
        }
        value->u.number = number;
        return value;
    }

    _fail("unexpected character", p->pos);
    return NULL;
}

json_value_t *json_parse(json_arena_t *arena, const char *text, size_t len) {
    _error[0] = '\0';
    if (!arena || !text) {
        _fail("no input", 0);
        return NULL;
    }
    parser_t p = {.text = text, .len = len, .pos = 0, .arena = arena, .depth = 0};
    arena->used = 0;
    json_value_t *root = _parse_value(&p);
    if (!root) return NULL;

    _skip_ws(&p);
    if (p.pos != p.len) {
        _fail("trailing data", p.pos);
        return NULL;
    }
    return root;
}

json_type_t json_type(const json_value_t *value) {
    if (!value) return JSON_NULL;
    return (json_type_t)value->type;
}

const char *json_string(const json_value_t *value) {
    if (!value || value->type != JSON_STRING) return NULL;
    return value->u.string.ptr;
}

size_t json_string_len(const json_value_t *value) {
    if (!value || value->type != JSON_STRING) return 0;
    return value->u.string.len;
}

double json_number(const json_value_t *value) {
    if (!value || value->type != JSON_NUMBER) return 0.0;
    return value->u.number;
}

bool json_boolean(const json_value_t *value) {
    return value && value->type == JSON_BOOL && value->u.boolean;
}

size_t json_count(const json_value_t *value) {
    if (!value || (value->type != JSON_ARRAY && value->type != JSON_OBJECT)) return 0;
    return value->u.container.count;
}

const json_value_t *json_at(const json_value_t *value, size_t index) {
    if (!value || (value->type != JSON_ARRAY && value->type != JSON_OBJECT)) return NULL;
    const json_item_t *item = value->u.container.first;
    while (item && index > 0) {
        item = item->next;
        index--;
    }
    return item ? item->value : NULL;
}

const char *json_key_at(const json_value_t *value, size_t index) {
    if (!value || value->type != JSON_OBJECT) return NULL;
    const json_item_t *item = value->u.container.first;
    while (item && index > 0) {
        item = item->next;
        index--;
    }
    return item ? item->key : NULL;
}

const json_value_t *json_get(const json_value_t *value, const char *key) {
    if (!value || value->type != JSON_OBJECT || !key) return NULL;
    for (const json_item_t *item = value->u.container.first; item; item = item->next) {
        if (item->key && strcmp(item->key, key) == 0) return item->value;
    }
    return NULL;
}

long long json_int_or(const json_value_t *value, long long fallback) {
    if (!value || value->type != JSON_NUMBER) return fallback;
    return (long long)value->u.number;
}
