/*
 * hal_pico.c — Production HAL implementation for Mellivora PicoCalc.
 *
 * Wires the abstract hal_ops_t table to the concrete Pico SDK drivers
 * (lcd.c, kbd.c, fat.c, syscall.c). This file is the only place that
 * should know about hardware details; apps use hal_* wrappers.
 */

#include "hal/hal.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "syscall.h"
#include "lcd.h"
#include "kbd.h"
#include "fat.h"

/* ------------------------------------------------------------------ */
/* Display adapters                                                     */
/* ------------------------------------------------------------------ */

static void _hal_pico_clear(void) { sys_clear(); }
static void _hal_pico_putchar(char c) { sys_putchar(c); }
static void _hal_pico_print(const char *s) { sys_print(s); }
static void _hal_pico_set_cursor(int x, int y) { lcd_set_cursor(x, y); }
static void _hal_pico_set_color(uint32_t fg, uint32_t bg) {
    lcd_set_fg(fg);
    lcd_set_bg(bg);
}
static int  _hal_pico_get_cursor_x(void) { return lcd_get_col(); }
static int  _hal_pico_get_cursor_y(void) { return lcd_get_row(); }

/* ------------------------------------------------------------------ */
/* Input adapters                                                       */
/* ------------------------------------------------------------------ */

static int _hal_pico_read_key(void) { return sys_read_key(); }

/* ------------------------------------------------------------------ */
/* Filesystem adapters (direct FAT; no fd table)                        */
/* ------------------------------------------------------------------ */

static fat_result_t _hal_pico_fat_mount(void) { return fat_mount(); }
static fat_result_t _hal_pico_fat_ls(const char *path, fat_ls_cb cb, void *ctx) {
    return fat_ls(path, cb, ctx);
}
static fat_result_t _hal_pico_fat_open(const char *path, fat_file_t *f) {
    return fat_open(path, f);
}
static int32_t _hal_pico_fat_read(fat_file_t *f, void *buf, uint32_t n) {
    return fat_read(f, buf, n);
}
static fat_result_t _hal_pico_fat_create(const char *path, const uint8_t *data, uint32_t len) {
    return fat_create(path, data, len);
}
static fat_result_t _hal_pico_fat_append(const char *path, const uint8_t *data, uint32_t len) {
    return fat_append(path, data, len);
}
static fat_result_t _hal_pico_fat_unlink(const char *path) { return fat_unlink(path); }
static fat_result_t _hal_pico_fat_mkdir(const char *path) { return fat_mkdir(path); }
static fat_result_t _hal_pico_fat_rename(const char *old_path, const char *new_path) {
    return fat_rename(old_path, new_path);
}

/* ------------------------------------------------------------------ */
/* Time / system adapters                                               */
/* ------------------------------------------------------------------ */

static uint32_t _hal_pico_time_ms(void) { return sys_time_ms(); }
static int64_t  _hal_pico_epoch_ms(void) { return sys_now_epoch_ms(); }
static void     _hal_pico_set_epoch_ms(int64_t ms) { sys_set_epoch_ms(ms); }
static void     _hal_pico_sleep_ms(uint32_t ms) { sys_sleep(ms); }

/* ------------------------------------------------------------------ */
/* Power adapters                                                       */
/* ------------------------------------------------------------------ */

static void _hal_pico_watchdog_update(void) { watchdog_update(); }
static void _hal_pico_reboot(void) { watchdog_reboot(0, 0, 100); }

/* ------------------------------------------------------------------ */
/* HAL instance                                                         */
/* ------------------------------------------------------------------ */

static hal_ops_t _hal_pico_ops = {
    .clear           = _hal_pico_clear,
    .putchar         = _hal_pico_putchar,
    .print           = _hal_pico_print,
    .set_cursor      = _hal_pico_set_cursor,
    .set_color       = _hal_pico_set_color,
    .get_cursor_x    = _hal_pico_get_cursor_x,
    .get_cursor_y    = _hal_pico_get_cursor_y,
    .read_key        = _hal_pico_read_key,
    .read_line       = NULL,     /* TODO: wire app_read_line or similar */
    .fat_mount       = _hal_pico_fat_mount,
    .fat_ls          = _hal_pico_fat_ls,
    .fat_open        = _hal_pico_fat_open,
    .fat_read        = _hal_pico_fat_read,
    .fat_create      = _hal_pico_fat_create,
    .fat_append      = _hal_pico_fat_append,
    .fat_unlink      = _hal_pico_fat_unlink,
    .fat_mkdir       = _hal_pico_fat_mkdir,
    .fat_rename      = _hal_pico_fat_rename,
    .time_ms         = _hal_pico_time_ms,
    .epoch_ms        = _hal_pico_epoch_ms,
    .set_epoch_ms    = _hal_pico_set_epoch_ms,
    .sleep_ms        = _hal_pico_sleep_ms,
    .beep            = NULL,     /* TODO: wire PWM beep when available */
    .watchdog_update = _hal_pico_watchdog_update,
    .reboot          = _hal_pico_reboot,
};

hal_ops_t *g_hal = NULL;

void hal_init(void) {
    g_hal = &_hal_pico_ops;
}
