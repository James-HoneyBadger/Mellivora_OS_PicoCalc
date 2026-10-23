#pragma once
/*
 * glob.h — wildcard matching for FAT 8.3 names.
 *
 * Supports `*`, `?`, `[abc]`, `[a-z]`, `[!abc]`, and backslash escapes.
 * Pure C, covered by host unit tests.
 */

#include <stdbool.h>

/*
 * Match `text` against `pattern`. When `casefold` is true, comparison is
 * case-insensitive (FAT names are uppercase; shell users usually type
 * lowercase).
 */
bool glob_match(const char *pattern, const char *text, bool casefold);
