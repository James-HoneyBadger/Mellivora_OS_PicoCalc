/*
 * path_util.c — path joining and canonicalisation.
 */

#include "core/path_util.h"

#include <stdio.h>
#include <string.h>

#include "apps/app_shared.h"

#define PATH_UTIL_TMP_MAX 1024
#define PATH_COMP_MAX 64

void path_make_abs(const char *cwd, const char *path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!cwd) cwd = "/";

    if (!path || !*path) {
        copy_cstr(out, out_sz, cwd);
    } else if (path[0] == '/') {
        copy_cstr(out, out_sz, path);
    } else if (strcmp(cwd, "/") == 0) {
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, path);
    } else {
        append_cstr(out, out_sz, cwd);
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, path);
    }
}

void path_canonicalize(const char *cwd, const char *input, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!cwd) cwd = "/";

    if (!input || input[0] == '\0') {
        copy_cstr(out, out_sz, cwd);
        return;
    }

    char tmp[PATH_UTIL_TMP_MAX];
    if (input[0] == '/') {
        copy_cstr(tmp, sizeof tmp, input);
    } else if (strcmp(cwd, "/") == 0) {
        snprintf(tmp, sizeof tmp, "/%s", input);
    } else {
        snprintf(tmp, sizeof tmp, "%s/%s", cwd, input);
    }

    /* Canonicalise into a buffer no larger than the caller's output. */
    size_t cap = out_sz < PATH_UTIL_TMP_MAX ? out_sz : PATH_UTIL_TMP_MAX;
    char canon[PATH_UTIL_TMP_MAX];
    canon[0] = '\0';

    const char *p = tmp;
    while (*p == '/') {
        while (*p == '/')
            p++;
        if (*p == '\0') break;

        char comp[PATH_COMP_MAX];
        int ci = 0;
        while (*p && *p != '/') {
            if (ci + 1 < (int)sizeof comp) comp[ci++] = *p;
            p++;
        }
        comp[ci] = '\0';

        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            /* Go up: remove the last component, clamped at the root. */
            char *last = strrchr(canon, '/');
            if (last) *last = '\0';
        } else {
            size_t clen = strlen(canon);
            if (clen + 1 + strlen(comp) < cap) {
                canon[clen++] = '/';
                const char *src = comp;
                while (*src)
                    canon[clen++] = *src++;
                canon[clen] = '\0';
            }
            /* If the component would overflow, drop it: truncating part of a
               name would silently address a different file. */
        }
    }

    if (canon[0] == '\0') {
        canon[0] = '/';
        canon[1] = '\0';
    }

    copy_cstr(out, out_sz, canon);
}
