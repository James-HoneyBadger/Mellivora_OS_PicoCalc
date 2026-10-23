#pragma once
/*
 * path_util.h — path joining and canonicalisation.
 *
 * Pure C; shared by the shell (main.c) and the app layer (apps.c).
 */

#include <stddef.h>

/*
 * Build an absolute path from `cwd` and `path`.
 * Mirrors the historical app_make_abs() behaviour: no `.`/`..` processing,
 * and an empty `path` resolves to `cwd`. Output is always terminated.
 */
void path_make_abs(const char *cwd, const char *path, char *out, size_t out_sz);

/*
 * Resolve `input` against `cwd` and canonicalise it: collapse repeated
 * slashes, drop `.` components, and apply `..` (clamped at the root).
 * An empty `input` copies `cwd` verbatim. Components that would overflow
 * `out_sz` are dropped rather than truncated.
 */
void path_canonicalize(const char *cwd, const char *input, char *out, size_t out_sz);
