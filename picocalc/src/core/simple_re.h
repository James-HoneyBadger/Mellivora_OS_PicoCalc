#pragma once
/*
 * simple_re.h — tiny case-insensitive regex matcher.
 *
 * Supports `.` (any character) and `*` (zero or more of the preceding
 * character), plus a leading `^` anchor. Pure C; covered by host tests.
 */

#include <stdbool.h>

/* Returns true when `re` matches anywhere in `text` (or at the start when
   `re` begins with '^'). */
bool match_simple_re(const char *re, const char *text);
