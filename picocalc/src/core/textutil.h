#pragma once
/*
 * textutil.h — small pure helpers for `tr` and `sed`.
 */

#include <stdbool.h>
#include <stddef.h>

/* Expand a tr-style character set: ranges (a-z), escapes (\n \t \r \\ \xNN),
   and literals. Returns the expanded length (excluding the NUL). */
size_t charset_expand(const char *spec, char *out, size_t cap);

/* Translate one character through set1 -> set2. When set2 is shorter, its
   last character is reused. Characters absent from set1 are unchanged. */
char charset_translate(const char *set1, size_t len1, const char *set2, size_t len2, char c);

/* Expand escapes in a literal string. Returns the output length. */
size_t text_unescape(const char *in, char *out, size_t cap);

/* Replace literal `old` with `new_text`. When global is false, only the first
   occurrence is replaced. Returns the output length, or (size_t)-1 when the
   result does not fit in `cap`. */
size_t text_replace(const char *in, size_t in_len, const char *old, const char *new_text,
                    bool global, char *out, size_t cap);
