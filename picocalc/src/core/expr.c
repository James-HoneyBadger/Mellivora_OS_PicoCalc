/*
 * expr.c — recursive-descent integer expression evaluator.
 */

#include "core/expr.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    expr_lookup_fn lookup;
    void *ctx;
    bool error;
    unsigned depth;
} expr_state_t;

static void expr_skip_ws(expr_state_t *st) {
    while (st && st->s && isspace((unsigned char)*st->s))
        st->s++;
}

/* Keep recursive parsing within a predictable embedded stack budget. */
#define EXPR_MAX_DEPTH 16u
static int expr_parse_cmp(expr_state_t *st);
static int expr_parse_unary(expr_state_t *st);
static int checked(expr_state_t *st, int64_t value) {
    if (value < INT_MIN || value > INT_MAX) {
        st->error = true;
        return 0;
    }
    return (int)value;
}
static bool descend(expr_state_t *st) {
    if (st->depth >= EXPR_MAX_DEPTH) {
        st->error = true;
        return false;
    }
    st->depth++;
    return true;
}

static int expr_parse_primary(expr_state_t *st) {
    expr_skip_ws(st);
    if (!st || !st->s || *st->s == '\0') {
        if (st) st->error = true;
        return 0;
    }

    if (*st->s == '(') {
        st->s++;
        if (!descend(st)) return 0;
        int v = expr_parse_cmp(st);
        st->depth--;
        expr_skip_ws(st);
        if (*st->s == ')') st->s++;
        else
            st->error = true;
        return v;
    }

    if (isdigit((unsigned char)*st->s)) {
        char *endptr = NULL;
        long long v;
        const char *digits = st->s;
        errno = 0;
        /* Detect 0x.. (hex), 0b.. (bin), 0o.. (octal) */
        if (st->s[0] == '0' && (st->s[1] == 'x' || st->s[1] == 'X')) {
            digits = st->s + 2;
            v = strtoll(digits, &endptr, 16);
        } else if (st->s[0] == '0' && (st->s[1] == 'b' || st->s[1] == 'B')) {
            digits = st->s + 2;
            v = strtoll(digits, &endptr, 2);
        } else if (st->s[0] == '0' && (st->s[1] == 'o' || st->s[1] == 'O')) {
            digits = st->s + 2;
            v = strtoll(digits, &endptr, 8);
        } else {
            v = strtoll(digits, &endptr, 10);
        }
        if (endptr == digits || errno == ERANGE || v < 0 || v > INT_MAX) {
            st->error = true;
            return 0;
        }
        st->s = endptr;
        return (int)v;
    }

    if (isalpha((unsigned char)*st->s) || *st->s == '_') {
        const char *start = st->s;
        while (isalnum((unsigned char)*st->s) || *st->s == '_')
            st->s++;
        return st->lookup ? st->lookup(st->ctx, start, (size_t)(st->s - start)) : 0;
    }

    st->error = true;
    return 0;
}

static int expr_parse_unary(expr_state_t *st) {
    expr_skip_ws(st);
    if (*st->s == '+') {
        st->s++;
        if (!descend(st)) return 0;
        int v = expr_parse_unary(st);
        st->depth--;
        return v;
    }
    if (*st->s == '-') {
        st->s++;
        if (!descend(st)) return 0;
        int v = expr_parse_unary(st);
        st->depth--;
        return checked(st, -(int64_t)v);
    }
    return expr_parse_primary(st);
}

static int expr_parse_mul(expr_state_t *st) {
    int v = expr_parse_unary(st);
    while (!st->error) {
        expr_skip_ws(st);
        if (*st->s == '*') {
            st->s++;
            v = checked(st, (int64_t)v * expr_parse_unary(st));
        } else if (*st->s == '/') {
            st->s++;
            int rhs = expr_parse_unary(st);
            if (rhs == 0 || (v == INT_MIN && rhs == -1)) {
                st->error = true;
                return 0;
            }
            v /= rhs;
        } else if (*st->s == '%') {
            st->s++;
            int rhs = expr_parse_unary(st);
            if (rhs == 0 || (v == INT_MIN && rhs == -1)) {
                st->error = true;
                return 0;
            }
            v %= rhs;
        } else if (*st->s == '&' && st->s[1] != '&') {
            st->s++;
            v &= expr_parse_unary(st);
        } else if (*st->s == '|' && st->s[1] != '|') {
            st->s++;
            v |= expr_parse_unary(st);
        } else if (*st->s == '^') {
            st->s++;
            v ^= expr_parse_unary(st);
        } else {
            break;
        }
    }
    return v;
}

static int expr_parse_add(expr_state_t *st) {
    int v = expr_parse_mul(st);
    while (!st->error) {
        expr_skip_ws(st);
        if (*st->s == '+') {
            st->s++;
            v = checked(st, (int64_t)v + expr_parse_mul(st));
        } else if (*st->s == '-') {
            st->s++;
            v = checked(st, (int64_t)v - expr_parse_mul(st));
        } else {
            break;
        }
    }
    return v;
}

static int expr_parse_cmp(expr_state_t *st) {
    int v = expr_parse_add(st);
    while (!st->error) {
        expr_skip_ws(st);
        if (strncmp(st->s, "==", 2) == 0) {
            st->s += 2;
            v = (v == expr_parse_add(st));
        } else if (strncmp(st->s, "!=", 2) == 0) {
            st->s += 2;
            v = (v != expr_parse_add(st));
        } else if (strncmp(st->s, "<=", 2) == 0) {
            st->s += 2;
            v = (v <= expr_parse_add(st));
        } else if (strncmp(st->s, ">=", 2) == 0) {
            st->s += 2;
            v = (v >= expr_parse_add(st));
        } else if (*st->s == '<') {
            st->s++;
            v = (v < expr_parse_add(st));
        } else if (*st->s == '>') {
            st->s++;
            v = (v > expr_parse_add(st));
        } else if (*st->s == '=') {
            st->s++;
            v = (v == expr_parse_add(st));
        } else {
            break;
        }
    }
    return v;
}

int expr_eval(const char *expr, expr_lookup_fn lookup, void *ctx, bool *ok) {
    if (!expr) {
        if (ok) *ok = false;
        return 0;
    }
    expr_state_t st = {
        .s = expr,
        .lookup = lookup,
        .ctx = ctx,
        .error = false,
    };
    int v = expr_parse_cmp(&st);
    expr_skip_ws(&st);
    if (*st.s != '\0') st.error = true;
    if (ok) *ok = !st.error;
    return st.error ? 0 : v;
}
