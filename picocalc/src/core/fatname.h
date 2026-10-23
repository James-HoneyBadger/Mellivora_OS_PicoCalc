#pragma once
/*
 * fatname.h — FAT 8.3 name conversion.
 *
 * Pure C; used by fat.c and covered by host unit tests.
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * Convert one path component to an 8.3 directory-name pair, uppercase and
 * space-padded. Returns false when the name or extension had to be truncated.
 * Invalid characters are not rejected (matching the historical fat.c helper).
 */
bool fat_make_83(const char *component, uint8_t name8[8], uint8_t ext3[3]);
