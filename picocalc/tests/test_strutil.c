/*
 * test_strutil.c — apps/app_shared.c helpers.
 */

#include <stddef.h>
#include <string.h>

#include "apps/app_shared.h"
#include "test_util.h"

void test_strutil(void) {
    char buf[8];

    memset(buf, 'X', sizeof buf);
    copy_cstr(buf, sizeof buf, "abcdef");
    CHECK_STR_EQ(buf, "abcdef");

    copy_cstr(buf, sizeof buf, "abcdefghij");
    CHECK_STR_EQ(buf, "abcdefg");

    copy_cstr(buf, sizeof buf, NULL);
    CHECK_STR_EQ(buf, "");

    copy_cstr(buf, 0, "ignored");
    CHECK(buf[0] == '\0');

    strcpy(buf, "ab");
    append_cstr(buf, sizeof buf, "cdef");
    CHECK_STR_EQ(buf, "abcdef");

    append_cstr(buf, sizeof buf, "ghij");
    CHECK_STR_EQ(buf, "abcdefg");

    append_cstr(buf, sizeof buf, NULL);
    CHECK_STR_EQ(buf, "abcdefg");

    CHECK_STR_EQ(skip_ws("   value"), "value");
    CHECK_STR_EQ(skip_ws(""), "");
    CHECK_STR_EQ(skip_ws(NULL), "");

    char mutable[] = "   value";
    CHECK(skip_spaces(mutable) == mutable + 3);
    CHECK(skip_spaces(NULL) == NULL);

    char trailing[] = "text \t\r\n";
    trim_right(trailing);
    CHECK_STR_EQ(trailing, "text");

    char none[] = "text";
    trim_right(none);
    CHECK_STR_EQ(none, "text");

    trim_right(NULL);
    CHECK(1);
}
