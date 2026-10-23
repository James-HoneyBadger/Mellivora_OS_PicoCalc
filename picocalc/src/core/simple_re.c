/*
 * simple_re.c — tiny case-insensitive regex matcher.
 */

#include "core/simple_re.h"

#include <ctype.h>

static bool match_re_here(const char *re, const char *text);

static bool match_re_star(char c, const char *re, const char *text) {
    for (;;) {
        if (match_re_here(re, text)) return true;
        if (*text == '\0') break;
        if (c != '.' && tolower((unsigned char)*text) != tolower((unsigned char)c)) break;
        text++;
    }
    return false;
}

static bool match_re_here(const char *re, const char *text) {
    if (re[0] == '\0') return true;
    if (re[1] == '*') return match_re_star(re[0], re + 2, text);
    if (re[0] == '.' && *text != '\0') return match_re_here(re + 1, text + 1);
    if (*text != '\0' && tolower((unsigned char)re[0]) == tolower((unsigned char)*text))
        return match_re_here(re + 1, text + 1);
    return false;
}

bool match_simple_re(const char *re, const char *text) {
    if (re[0] == '^') return match_re_here(re + 1, text);
    do {
        if (match_re_here(re, text)) return true;
    } while (*text++ != '\0');
    return false;
}
