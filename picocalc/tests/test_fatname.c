/*
 * test_fatname.c — src/core/fatname.c
 */

#include <stdint.h>
#include <string.h>

#include "core/fatname.h"
#include "test_util.h"

static void check_name(const char *input, const char *name, const char *ext, bool fits) {
    uint8_t n8[8];
    uint8_t e3[3];
    CHECK_EQ(fat_make_83(input, n8, e3), fits);
    CHECK(memcmp(n8, name, 8) == 0);
    CHECK(memcmp(e3, ext, 3) == 0);
}

void test_fatname(void) {
    check_name("FOO.TXT", "FOO     ", "TXT", true);
    check_name("foo.txt", "FOO     ", "TXT", true);
    check_name("README", "README  ", "   ", true);
    check_name("LONGFILENAME.TXT", "LONGFILE", "TXT", false);
    check_name("A.B.C", "A       ", "BC ", true);
    check_name("X.TOOLONG", "X       ", "TOO", false);
    check_name(".HIDDEN", "        ", "HID", false);
    check_name("", "        ", "   ", true);
}
