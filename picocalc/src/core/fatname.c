/*
 * fatname.c — FAT 8.3 name conversion.
 */

#include "core/fatname.h"

#include <ctype.h>
#include <string.h>

bool fat_make_83(const char *component, uint8_t name8[8], uint8_t ext3[3]) {
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);

    int ni = 0, ei = 0;
    bool in_ext = false;
    bool truncated = false;

    for (const char *p = component; p && *p; p++) {
        if (*p == '.') {
            in_ext = true;
            continue;
        }
        if (in_ext) {
            if (ei < 3) ext3[ei++] = (uint8_t)toupper((unsigned char)*p);
            else
                truncated = true;
        } else {
            if (ni < 8) name8[ni++] = (uint8_t)toupper((unsigned char)*p);
            else
                truncated = true;
        }
    }
    return !truncated;
}
