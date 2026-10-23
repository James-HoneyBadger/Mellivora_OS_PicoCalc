#pragma once
/*
 * expr.h — integer expression evaluator shared by calc, BASIC, and Tiny C.
 *
 * Pure C with no hardware dependencies, so it is covered by host unit tests.
 */

#include <stdbool.h>
#include <stddef.h>

typedef int (*expr_lookup_fn)(void *ctx, const char *name, size_t len);

/*
 * Evaluate an integer expression.
 *
 * Variables are resolved through `lookup` (NULL means every name is 0).
 * Supports parentheses, unary +/-, + - * / %, bitwise & | ^, comparisons,
 * and decimal / 0x / 0b / 0o literals.
 *
 * Returns 0 and sets *ok = false on a parse error, division by zero, or
 * trailing garbage. `ok` may be NULL.
 */
int expr_eval(const char *expr, expr_lookup_fn lookup, void *ctx, bool *ok);
