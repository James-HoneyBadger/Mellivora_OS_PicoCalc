/*
 * test_simple_re.c — src/core/simple_re.c
 */

#include "core/simple_re.h"
#include "test_util.h"

void test_simple_re(void) {
    CHECK(match_simple_re("abc", "xxabcxx"));
    CHECK(!match_simple_re("abc", "xxab"));
    CHECK(match_simple_re("^abc", "abcxx"));
    CHECK(!match_simple_re("^abc", "xxabc"));
    CHECK(match_simple_re("a.c", "xxaXc"));
    CHECK(!match_simple_re("a.c", "xxac"));
    CHECK(match_simple_re("a*", "xxaaa"));
    CHECK(match_simple_re("a*", "xx"));
    CHECK(match_simple_re("a*b", "aaab"));
    CHECK(!match_simple_re("a*b", "aaac"));
    CHECK(match_simple_re("", "anything"));
    CHECK(match_simple_re("", ""));
    CHECK(!match_simple_re("x", ""));
    CHECK(!match_simple_re(".", ""));
    CHECK(match_simple_re("^", "text"));
}
