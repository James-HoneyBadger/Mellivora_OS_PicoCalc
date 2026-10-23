/*
 * test_path_util.c — src/core/path_util.c
 */

#include <string.h>

#include "core/path_util.h"
#include "test_util.h"

static void check_abs(const char *cwd, const char *path, const char *expected) {
    char out[256];
    path_make_abs(cwd, path, out, sizeof out);
    CHECK_STR_EQ(out, expected);
}

static void check_canon(const char *cwd, const char *input, const char *expected) {
    char out[256];
    path_canonicalize(cwd, input, out, sizeof out);
    CHECK_STR_EQ(out, expected);
}

void test_path_util(void) {
    /* Joining */
    check_abs("/", "A.TXT", "/A.TXT");
    check_abs("/", "/A.TXT", "/A.TXT");
    check_abs("/DOCS", "A.TXT", "/DOCS/A.TXT");
    check_abs("/DOCS", "", "/DOCS");
    check_abs("/DOCS", NULL, "/DOCS");
    check_abs("/", "", "/");

    char tiny[4];
    path_make_abs("/", "ABCDEF", tiny, sizeof tiny);
    CHECK_STR_EQ(tiny, "/AB");

    /* Canonicalisation */
    check_canon("/", "A/./B", "/A/B");
    check_canon("/A/B", "../C", "/A/C");
    check_canon("/", "../../X", "/X");
    check_canon("/A", "B//C/", "/A/B/C");
    check_canon("/A", "//B//C/", "/B/C");
    check_canon("/A/B", "..", "/A");
    check_canon("/A", "..", "/");
    check_canon("/A", ".", "/A");
    check_canon("/A", "", "/A");
    check_canon("/A", NULL, "/A");
    check_canon("/", "/", "/");
    check_canon("/", "////", "/");

    char small[4];
    path_canonicalize("/", "/ABCDEF", small, sizeof small);
    CHECK_STR_EQ(small, "/");

    char unused[4] = "abc";
    path_canonicalize("/", "x", unused, 0);
    CHECK_STR_EQ(unused, "abc");

    path_make_abs("/", "x", NULL, 10);
    path_canonicalize("/", "x", NULL, 10);
    CHECK(1);
}
