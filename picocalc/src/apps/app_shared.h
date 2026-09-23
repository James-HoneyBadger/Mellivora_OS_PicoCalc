#pragma once
/*
 * apps/app_shared.h — Common helpers used by the shell and app layer.
 *
 * These functions replace duplicate copies that previously lived in
 * main.c and apps.c. They are intentionally simple, bounded, and
 * dependency-free so they can be used anywhere in the firmware.
 */

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Safe bounded string copy. dst is always null-terminated. */
void copy_cstr(char *dst, size_t dst_sz, const char *src);

/* Append src to dst without overrunning. dst is always null-terminated. */
void append_cstr(char *dst, size_t dst_sz, const char *src);

/* Skip leading whitespace. Returns pointer to first non-whitespace char.
 * Never returns NULL: a NULL input yields "". */
const char *skip_ws(const char *s);

/* Mutable variant of skip_ws for in-place parsing of writeable buffers. */
char *skip_spaces(char *s);

/* Trim trailing whitespace (space, tab, CR, LF) in place. */
void trim_right(char *s);

#ifdef __cplusplus
}
#endif
