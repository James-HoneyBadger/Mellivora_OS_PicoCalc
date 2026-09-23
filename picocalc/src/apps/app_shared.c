/*
 * apps/app_shared.c — Common helpers used by the shell and app layer.
 */

#include "apps/app_shared.h"

#include <string.h>

void copy_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void append_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0 || !src) return;
    size_t len = strlen(dst);
    while (*src && len + 1 < dst_sz) dst[len++] = *src++;
    dst[len] = '\0';
}

const char *skip_ws(const char *s) {
    if (!s) return "";
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

char *skip_spaces(char *s) {
    if (!s) return NULL;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

void trim_right(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                      s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}
