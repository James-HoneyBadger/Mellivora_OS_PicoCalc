/*
 * test_main.c — entry point for the Mellivora host unit tests.
 */

#include <stdio.h>

#include "test_util.h"

int test_checks = 0;
int test_failures = 0;

void test_strutil(void);
void test_expr(void);
void test_shell_parse(void);
void test_path_util(void);
void test_fatname(void);
void test_simple_re(void);
void test_glob(void);
void test_textutil(void);
void test_lfn(void);

int main(void) {
    RUN(test_strutil);
    RUN(test_expr);
    RUN(test_shell_parse);
    RUN(test_path_util);
    RUN(test_fatname);
    RUN(test_simple_re);
    RUN(test_glob);
    RUN(test_textutil);
    RUN(test_lfn);

    printf("\n%d checks, %d failure(s)\n", test_checks, test_failures);
    return test_failures == 0 ? 0 : 1;
}
