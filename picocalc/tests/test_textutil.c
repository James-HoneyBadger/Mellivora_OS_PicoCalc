/*
 * test_textutil.c — src/core/textutil.c
 */

#include <stddef.h>

#include "core/textutil.h"
#include "test_util.h"

void test_textutil(void) {
    char out[64];

    CHECK_EQ(charset_expand("a-c", out, sizeof out), 3);
    CHECK_STR_EQ(out, "abc");
    CHECK_EQ(charset_expand("a-cA-C", out, sizeof out), 6);
    CHECK_STR_EQ(out, "abcABC");
    CHECK_EQ(charset_expand("0-2x", out, sizeof out), 4);
    CHECK_STR_EQ(out, "012x");

    CHECK_EQ(charset_expand("\\n\\t", out, sizeof out), 2);
    CHECK_EQ(out[0], '\n');
    CHECK_EQ(out[1], '\t');
    CHECK_EQ(charset_expand("\\x41", out, sizeof out), 1);
    CHECK_EQ(out[0], 'A');
    CHECK_EQ(charset_expand("", out, sizeof out), 0);
    CHECK_STR_EQ(out, "");

    char tiny[3];
    CHECK_EQ(charset_expand("a-z", tiny, sizeof tiny), 2);
    CHECK_STR_EQ(tiny, "ab");

    CHECK_EQ(charset_translate("abc", 3, "xyz", 3, 'b'), 'y');
    CHECK_EQ(charset_translate("abc", 3, "xy", 2, 'c'), 'y');
    CHECK_EQ(charset_translate("abc", 3, "xyz", 3, 'q'), 'q');
    CHECK_EQ(charset_translate("abc", 3, "", 0, 'a'), 'a');

    CHECK_EQ(text_unescape("a\\nb", out, sizeof out), 3);
    CHECK_STR_EQ(out, "a\nb");
    CHECK_EQ(text_unescape("\\x41", out, sizeof out), 1);
    CHECK_STR_EQ(out, "A");
    CHECK_EQ(text_unescape("\\\\", out, sizeof out), 1);
    CHECK_STR_EQ(out, "\\");

    CHECK_EQ(text_replace("aXbXc", 5, "X", "-", true, out, sizeof out), 5);
    CHECK_STR_EQ(out, "a-b-c");
    CHECK_EQ(text_replace("aXbXc", 5, "X", "-", false, out, sizeof out), 5);
    CHECK_STR_EQ(out, "a-bXc");
    CHECK_EQ(text_replace("abc", 3, "", "-", true, out, sizeof out), 3);
    CHECK_STR_EQ(out, "abc");

    char small[4];
    CHECK_EQ(text_replace("abc", 3, "a", "XXXX", false, small, sizeof small), (size_t)-1);
}
