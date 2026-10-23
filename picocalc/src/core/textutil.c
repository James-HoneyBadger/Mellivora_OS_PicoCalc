/*
 * textutil.c — small pure helpers for `tr` and `sed`.
 */

#include "core/textutil.h"

#include <ctype.h>
#include <string.h>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Parse one literal/escape. Returns the next input position. */
static const char *parse_unit(const char *p, char *out) {
    if (*p != '\\') {
        *out = *p;
        return p + 1;
    }

    p++;
    if (*p == '\0') {
        *out = '\\';
        return p;
    }

    switch (*p) {
    case 'n':
        *out = '\n';
        return p + 1;
    case 't':
        *out = '\t';
        return p + 1;
    case 'r':
        *out = '\r';
        return p + 1;
    case '\\':
        *out = '\\';
        return p + 1;
    case 'x': {
        p++;
        int value = 0;
        int digits = 0;
        while (digits < 2 && isxdigit((unsigned char)*p)) {
            value = value * 16 + hex_digit(*p);
            p++;
            digits++;
        }
        if (digits > 0) {
            *out = (char)value;
            return p;
        }
        *out = 'x';
        return p;
    }
    default:
        *out = *p;
        return p + 1;
    }
}

size_t charset_expand(const char *spec, char *out, size_t cap) {
    size_t o = 0;
    if (!spec || !out || cap == 0) return 0;

    const char *p = spec;
    while (*p && o + 1 < cap) {
        char lo;
        p = parse_unit(p, &lo);

        if (*p == '-' && p[1]) {
            p++;
            char hi;
            p = parse_unit(p, &hi);
            for (int c = (unsigned char)lo; c <= (unsigned char)hi && o + 1 < cap; c++) {
                out[o++] = (char)c;
            }
        } else {
            out[o++] = lo;
        }
    }

    out[o] = '\0';
    return o;
}

char charset_translate(const char *set1, size_t len1, const char *set2, size_t len2, char c) {
    if (!set1 || len1 == 0) return c;

    for (size_t i = 0; i < len1; i++) {
        if (set1[i] == c) {
            if (!set2 || len2 == 0) return c;
            return set2[i < len2 ? i : len2 - 1];
        }
    }
    return c;
}

size_t text_unescape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (!in || !out || cap == 0) return 0;

    const char *p = in;
    while (*p && o + 1 < cap) {
        char c;
        p = parse_unit(p, &c);
        out[o++] = c;
    }

    out[o] = '\0';
    return o;
}

size_t text_replace(const char *in, size_t in_len, const char *old, const char *new_text,
                    bool global, char *out, size_t cap) {
    if (!in || !out || cap == 0) return (size_t)-1;
    if (!new_text) new_text = "";

    size_t old_len = old ? strlen(old) : 0;
    size_t new_len = strlen(new_text);
    size_t o = 0;
    const char *p = in;
    size_t remaining = in_len;

    while (remaining > 0) {
        if (old_len > 0 && remaining >= old_len && memcmp(p, old, old_len) == 0) {
            if (o + new_len + 1 > cap) return (size_t)-1;
            memcpy(out + o, new_text, new_len);
            o += new_len;
            p += old_len;
            remaining -= old_len;

            if (!global) {
                if (o + remaining + 1 > cap) return (size_t)-1;
                memcpy(out + o, p, remaining);
                o += remaining;
                remaining = 0;
            }
        } else {
            if (o + 2 > cap) return (size_t)-1;
            out[o++] = *p++;
            remaining--;
        }
    }

    out[o] = '\0';
    return o;
}
