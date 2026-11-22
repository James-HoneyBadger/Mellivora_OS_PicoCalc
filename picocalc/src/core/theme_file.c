/*
 * core/theme_file.c — parser for `/themes/NAME.thm` palette files.
 *
 * Format (one assignment per line, '#' starts a comment):
 *
 *     name = ocean
 *     bg = 06121C
 *     text = D8E8F0
 *
 * Colours are RRGGBB. Any role omitted keeps the palette it is applied to.
 */

#include "core/theme_file.h"

#include <string.h>

static const char *const _role_names[THEME_FILE_ROLE_COUNT] = {
    "bg",     "text",  "muted",      "accent",    "success",     "warning",   "error",
    "border", "panel", "panel_text", "select_bg", "select_text", "status_bg", "status_text",
};

const char *theme_file_role_name(int index) {
    if (index < 0 || index >= THEME_FILE_ROLE_COUNT) return "";
    return _role_names[index];
}

static int _cii(const char *a, const char *b) {
    for (;;) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        if (!ca) return 0;
    }
}

int theme_file_role_index(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < THEME_FILE_ROLE_COUNT; i++) {
        if (_cii(name, _role_names[i]) == 0) return i;
    }
    return -1;
}

static int _nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool theme_file_parse_color(const char *text, uint32_t *out) {
    if (!text || !out) return false;
    if (*text == '#') text++;
    uint32_t value = 0;
    int digits = 0;
    while (text[digits]) {
        int nibble = _nibble(text[digits]);
        if (nibble < 0 || digits == 6) return false;
        value = (value << 4) | (uint32_t)nibble;
        digits++;
    }
    if (digits != 6) return false;
    *out = value;
    return true;
}

static void _trim(char **start, char **end) {
    while (*start < *end && (**start == ' ' || **start == '\t' || **start == '\r'))
        (*start)++;
    while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r'))
        (*end)--;
}

bool theme_file_parse(theme_file_t *out, const char *text, size_t len) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!text) return false;

    bool have_name = false;
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && text[pos] != '\n')
            pos++;
        char *start = (char *)text + line_start;
        char *end = (char *)text + pos;
        if (pos < len) pos++;
        _trim(&start, &end);
        if (start >= end) continue;
        if (*start == '#' || *start == ';') continue;

        char *eq = start;
        while (eq < end && *eq != '=')
            eq++;
        if (eq >= end) {
            out->errors++;
            continue;
        }
        char *key_start = start;
        char *key_end = eq;
        char *val_start = eq + 1;
        char *val_end = end;
        _trim(&key_start, &key_end);
        _trim(&val_start, &val_end);

        char key[THEME_FILE_NAME_MAX];
        size_t key_len = (size_t)(key_end - key_start);
        if (key_len == 0 || key_len >= sizeof key) {
            out->errors++;
            continue;
        }
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';

        char value[THEME_FILE_NAME_MAX * 2];
        size_t value_len = (size_t)(val_end - val_start);
        if (value_len >= sizeof value) {
            out->errors++;
            continue;
        }
        memcpy(value, val_start, value_len);
        value[value_len] = '\0';

        if (_cii(key, "name") == 0) {
            if (!value_len) {
                out->errors++;
                continue;
            }
            if (value_len >= sizeof out->name) value_len = sizeof out->name - 1;
            memcpy(out->name, value, value_len);
            out->name[value_len] = '\0';
            have_name = true;
            continue;
        }

        int role = theme_file_role_index(key);
        if (role < 0) continue; /* unknown keys are forwards compatible */

        uint32_t color = 0;
        if (!theme_file_parse_color(value, &color)) {
            out->errors++;
            continue;
        }
        out->color[role] = color;
        out->set_mask |= (uint16_t)(1u << (unsigned)role);
    }
    return have_name && out->set_mask != 0;
}
