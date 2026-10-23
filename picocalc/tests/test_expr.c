/*
 * test_expr.c — src/core/expr.c
 */

#include <stdbool.h>
#include <stddef.h>

#include "core/expr.h"
#include "test_util.h"

static int lookup(void *ctx, const char *name, size_t len) {
    (void)ctx;
    if (len == 1 && name[0] == 'X') return 7;
    if (len == 1 && name[0] == 'Y') return 3;
    if (len == 3 && name[0] == 'a' && name[1] == 'n' && name[2] == 's') return 42;
    return 0;
}

static int eval(const char *expr, bool *ok) {
    return expr_eval(expr, lookup, NULL, ok);
}

void test_expr(void) {
    bool ok = false;

    CHECK_EQ(eval("2+3*4", &ok), 14);
    CHECK(ok);
    CHECK_EQ(eval("(2+3)*4", &ok), 20);
    CHECK(ok);
    CHECK_EQ(eval("10/3", &ok), 3);
    CHECK(ok);
    CHECK_EQ(eval("10%3", &ok), 1);
    CHECK(ok);
    CHECK_EQ(eval("-5 + +3", &ok), -2);
    CHECK(ok);
    CHECK_EQ(eval(" 2 * ( 3 + 4 ) ", &ok), 14);
    CHECK(ok);

    /* Literals */
    CHECK_EQ(eval("0xFF", &ok), 255);
    CHECK(ok);
    CHECK_EQ(eval("0b1010", &ok), 10);
    CHECK(ok);
    CHECK_EQ(eval("0o17", &ok), 15);
    CHECK(ok);

    /* Variables */
    CHECK_EQ(eval("X*Y", &ok), 21);
    CHECK(ok);
    CHECK_EQ(eval("ans+1", &ok), 43);
    CHECK(ok);
    CHECK_EQ(eval("unknown", &ok), 0);
    CHECK(ok);

    /* Bitwise operators bind at multiplicative precedence. */
    CHECK_EQ(eval("6 & 3", &ok), 2);
    CHECK(ok);
    CHECK_EQ(eval("6 | 3", &ok), 7);
    CHECK(ok);
    CHECK_EQ(eval("6 ^ 3", &ok), 5);
    CHECK(ok);
    CHECK_EQ(eval("1 + 6 & 3", &ok), 3);
    CHECK(ok);

    /* Comparisons */
    CHECK_EQ(eval("1==1", &ok), 1);
    CHECK(ok);
    CHECK_EQ(eval("1!=1", &ok), 0);
    CHECK(ok);
    CHECK_EQ(eval("2<=2", &ok), 1);
    CHECK(ok);
    CHECK_EQ(eval("2>=3", &ok), 0);
    CHECK(ok);
    CHECK_EQ(eval("1<2", &ok), 1);
    CHECK(ok);
    CHECK_EQ(eval("3>2", &ok), 1);
    CHECK(ok);
    CHECK_EQ(eval("4=4", &ok), 1);
    CHECK(ok);

    /* Error paths */
    CHECK_EQ(eval("1/0", &ok), 0);
    CHECK(!ok);
    CHECK_EQ(eval("1%0", &ok), 0);
    CHECK(!ok);
    CHECK_EQ(eval("2 3", &ok), 0);
    CHECK(!ok);
    CHECK_EQ(eval("(1+2", &ok), 0);
    CHECK(!ok);
    CHECK_EQ(eval("2+", &ok), 0);
    CHECK(!ok);
    CHECK_EQ(eval("", &ok), 0);
    CHECK(!ok);

    /* A NULL ok pointer is allowed. */
    CHECK_EQ(expr_eval("1+1", NULL, NULL, NULL), 2);
}
