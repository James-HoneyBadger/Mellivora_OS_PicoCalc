/*
 * glob.c — wildcard matching for FAT 8.3 names.
 */

#include "core/glob.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static char fold_char(char c, bool fold) {
    return fold ? (char)tolower((unsigned char)c) : c;
}

static bool class_match(const char *p, size_t len, char c, bool fold) {
    bool negate = false;
    size_t i = 0;

    if (len > 0 && (p[0] == '!' || p[0] == '^')) {
        negate = true;
        i = 1;
    }

    bool matched = false;
    while (i < len) {
        char lo = p[i];
        if (lo == '\\' && i + 1 < len) lo = p[++i];

        if (i + 2 < len && p[i + 1] == '-') {
            char hi = p[i + 2];
            char cc = fold_char(c, fold);
            char flo = fold_char(lo, fold);
            char fhi = fold_char(hi, fold);
            if (flo <= cc && cc <= fhi) matched = true;
            i += 3;
        } else {
            if (fold_char(lo, fold) == fold_char(c, fold)) matched = true;
            i++;
        }
    }
    return negate ? !matched : matched;
}

bool glob_match(const char *pattern, const char *text, bool casefold) {
    if (!pattern || !text) return false;

    const char *p = pattern;
    const char *t = text;
    const char *star_p = NULL;
    const char *star_t = NULL;

    while (*t) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (*p == '\0') return true;
            star_p = p;
            star_t = t;
            continue;
        }

        if (*p == '?') {
            p++;
            t++;
            continue;
        }

        if (*p == '[') {
            const char *close = strchr(p + 1, ']');
            if (close) {
                if (class_match(p + 1, (size_t)(close - p - 1), *t, casefold)) {
                    p = close + 1;
                    t++;
                    continue;
                }
            } else if (fold_char('[', casefold) == fold_char(*t, casefold)) {
                p++;
                t++;
                continue;
            }
        } else {
            const char *lit = p;
            if (*lit == '\\' && lit[1]) lit++;
            if (fold_char(*lit, casefold) == fold_char(*t, casefold)) {
                p = lit + 1;
                t++;
                continue;
            }
        }

        if (!star_p) return false;
        star_t++;
        t = star_t;
        p = star_p;
    }

    while (*p == '*')
        p++;
    return *p == '\0';
}
