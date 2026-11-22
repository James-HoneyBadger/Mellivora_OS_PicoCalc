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
void test_history(void);
void test_fat(void);
void test_datetime(void);
void test_ramdisk(void);
void test_json(void);
void test_sha256(void);
void test_mpkg(void);
void test_theme_file(void);
void test_mapp_image(void);
void test_gfx(void);
void test_mixer(void);
int test_launcher(void);
void test_pinmap(void);
void test_i2cdetect(void);
void test_vt100(void);
void test_xmodem(void);
void test_workbench(void);

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
    RUN(test_history);
    RUN(test_fat);
    RUN(test_datetime);
    RUN(test_ramdisk);
    RUN(test_json);
    RUN(test_sha256);
    RUN(test_mpkg);
    RUN(test_theme_file);
    RUN(test_mapp_image);
    RUN(test_gfx);
    RUN(test_mixer);
    RUN(test_launcher);
    RUN(test_pinmap);
    RUN(test_i2cdetect);
    RUN(test_vt100);
    RUN(test_xmodem);
    RUN(test_workbench);

    printf("\n%d checks, %d failure(s)\n", test_checks, test_failures);
    return test_failures == 0 ? 0 : 1;
}
