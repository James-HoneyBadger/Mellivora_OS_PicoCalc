#pragma once
/*
 * test_util.h — minimal assertion helpers for the host test binary.
 */

#include <stdio.h>
#include <string.h>

extern int test_checks;
extern int test_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        test_checks++;                                                                             \
        if (!(cond)) {                                                                             \
            test_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        long long a_ = (long long)(actual);                                                        \
        long long e_ = (long long)(expected);                                                      \
        test_checks++;                                                                             \
        if (a_ != e_) {                                                                            \
            test_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", __FILE__, __LINE__, e_, a_);  \
        }                                                                                          \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                                             \
    do {                                                                                           \
        const char *a_ = (actual);                                                                 \
        const char *e_ = (expected);                                                               \
        test_checks++;                                                                             \
        if (!a_ || !e_ || strcmp(a_, e_) != 0) {                                                   \
            test_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__,       \
                    e_ ? e_ : "(null)", a_ ? a_ : "(null)");                                       \
        }                                                                                          \
    } while (0)

#define RUN(suite)                                                                                 \
    do {                                                                                           \
        printf("== %s ==\n", #suite);                                                              \
        suite();                                                                                   \
    } while (0)
