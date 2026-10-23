/*
 * test_glob.c — src/core/glob.c
 */

#include "core/glob.h"
#include "test_util.h"

void test_glob(void) {
    CHECK(glob_match("*", "ABC", true));
    CHECK(glob_match("*", "", true));
    CHECK(glob_match("", "", true));
    CHECK(!glob_match("?", "", true));

    CHECK(glob_match("*.TXT", "A.TXT", true));
    CHECK(glob_match("*.txt", "A.TXT", true));
    CHECK(!glob_match("*.TXT", "A.BAS", true));
    CHECK(glob_match("*.TXT", "A.TXT", false));
    CHECK(!glob_match("*.txt", "A.TXT", false));

    CHECK(glob_match("A?C", "ABC", true));
    CHECK(!glob_match("A?C", "AC", true));

    CHECK(glob_match("[ab]*", "AX", true));
    CHECK(!glob_match("[!ab]*", "AX", true));
    CHECK(glob_match("[!ab]*", "CX", true));
    CHECK(glob_match("[a-c]X", "BX", true));
    CHECK(!glob_match("[a-c]X", "DX", true));

    CHECK(glob_match("????????.???", "LONGFILE.TXT", true));
    CHECK(!glob_match("?????????.???", "LONGFILE.TXT", true));

    CHECK(glob_match("*.*", "A.TXT", true));
    CHECK(glob_match("A*T", "A.TXT", true));
    CHECK(!glob_match("A*Z", "A.TXT", true));

    /* Backslash escapes the next pattern character. */
    CHECK(glob_match("\\*", "*", true));
    CHECK(!glob_match("\\*", "X", true));

    /* An unmatched '[' is treated literally. */
    CHECK(glob_match("[abc", "[abc", true));

    CHECK(!glob_match("ABC", "abc", false));
    CHECK(glob_match("ABC", "abc", true));
}
