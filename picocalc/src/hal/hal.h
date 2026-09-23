#pragma once
/*
 * hal.h — Hardware Abstraction Layer for Mellivora PicoCalc
 *
 * Thin function-pointer table that decouples apps from direct hardware
 * calls. The production build wires this to lcd.c, kbd.c, fat.c, and
 * syscall.c; test builds can substitute a mock HAL later.
 *
 * This header is intentionally non-invasive for v2.5.0: existing code
 * continues to call sys_* helpers, which forward into the HAL. Over time
 * new code can call hal_* directly.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "fat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* HAL operation table                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Display output */
    void (*clear)(void);
    void (*putchar)(char c);
    void (*print)(const char *s);
    void (*set_cursor)(int x, int y);
    void (*set_color)(uint32_t fg, uint32_t bg);
    int  (*get_cursor_x)(void);
    int  (*get_cursor_y)(void);

    /* Input */
    int  (*read_key)(void);
    int  (*read_line)(const char *prompt, char *buf, size_t sz);

    /* Filesystem (FAT-backed) */
    fat_result_t (*fat_mount)(void);
    fat_result_t (*fat_ls)(const char *path, fat_ls_cb cb, void *ctx);
    fat_result_t (*fat_open)(const char *path, fat_file_t *f);
    int32_t      (*fat_read)(fat_file_t *f, void *buf, uint32_t n);
    fat_result_t (*fat_create)(const char *path, const uint8_t *data, uint32_t len);
    fat_result_t (*fat_append)(const char *path, const uint8_t *data, uint32_t len);
    fat_result_t (*fat_unlink)(const char *path);
    fat_result_t (*fat_mkdir)(const char *path);
    fat_result_t (*fat_rename)(const char *old_path, const char *new_path);

    /* Time / system */
    uint32_t (*time_ms)(void);
    int64_t  (*epoch_ms)(void);
    void     (*set_epoch_ms)(int64_t ms);
    void     (*sleep_ms)(uint32_t ms);

    /* Audio / feedback */
    void (*beep)(int freq_hz, int duration_ms);

    /* Power */
    void (*watchdog_update)(void);
    void (*reboot)(void);
} hal_ops_t;

/* ------------------------------------------------------------------ */
/* Global HAL instance                                                  */
/* ------------------------------------------------------------------ */

extern hal_ops_t *g_hal;

/* Initialize and install the production Pico HAL. */
void hal_init(void);

/* ------------------------------------------------------------------ */
/* Inline wrappers                                                      */
/* ------------------------------------------------------------------ */

static inline void hal_clear(void) {
    if (g_hal && g_hal->clear) g_hal->clear();
}

static inline void hal_putchar(char c) {
    if (g_hal && g_hal->putchar) g_hal->putchar(c);
}

static inline void hal_print(const char *s) {
    if (g_hal && g_hal->print) g_hal->print(s);
}

static inline void hal_set_cursor(int x, int y) {
    if (g_hal && g_hal->set_cursor) g_hal->set_cursor(x, y);
}

static inline void hal_set_color(uint32_t fg, uint32_t bg) {
    if (g_hal && g_hal->set_color) g_hal->set_color(fg, bg);
}

static inline int hal_get_cursor_x(void) {
    return (g_hal && g_hal->get_cursor_x) ? g_hal->get_cursor_x() : 0;
}

static inline int hal_get_cursor_y(void) {
    return (g_hal && g_hal->get_cursor_y) ? g_hal->get_cursor_y() : 0;
}

static inline int hal_read_key(void) {
    return (g_hal && g_hal->read_key) ? g_hal->read_key() : 0;
}

static inline int hal_read_line(const char *prompt, char *buf, size_t sz) {
    return (g_hal && g_hal->read_line) ? g_hal->read_line(prompt, buf, sz) : -1;
}

static inline fat_result_t hal_fat_mount(void) {
    return (g_hal && g_hal->fat_mount) ? g_hal->fat_mount() : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_ls(const char *path, fat_ls_cb cb, void *ctx) {
    return (g_hal && g_hal->fat_ls) ? g_hal->fat_ls(path, cb, ctx) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_open(const char *path, fat_file_t *f) {
    return (g_hal && g_hal->fat_open) ? g_hal->fat_open(path, f) : FAT_ERR_NOTMOUNTED;
}

static inline int32_t hal_fat_read(fat_file_t *f, void *buf, uint32_t n) {
    return (g_hal && g_hal->fat_read) ? g_hal->fat_read(f, buf, n) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_create(const char *path, const uint8_t *data, uint32_t len) {
    return (g_hal && g_hal->fat_create) ? g_hal->fat_create(path, data, len) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_append(const char *path, const uint8_t *data, uint32_t len) {
    return (g_hal && g_hal->fat_append) ? g_hal->fat_append(path, data, len) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_unlink(const char *path) {
    return (g_hal && g_hal->fat_unlink) ? g_hal->fat_unlink(path) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_mkdir(const char *path) {
    return (g_hal && g_hal->fat_mkdir) ? g_hal->fat_mkdir(path) : FAT_ERR_NOTMOUNTED;
}

static inline fat_result_t hal_fat_rename(const char *old_path, const char *new_path) {
    return (g_hal && g_hal->fat_rename) ? g_hal->fat_rename(old_path, new_path) : FAT_ERR_NOTMOUNTED;
}

static inline uint32_t hal_time_ms(void) {
    return (g_hal && g_hal->time_ms) ? g_hal->time_ms() : 0;
}

static inline int64_t hal_epoch_ms(void) {
    return (g_hal && g_hal->epoch_ms) ? g_hal->epoch_ms() : 0;
}

static inline void hal_set_epoch_ms(int64_t ms) {
    if (g_hal && g_hal->set_epoch_ms) g_hal->set_epoch_ms(ms);
}

static inline void hal_sleep_ms(uint32_t ms) {
    if (g_hal && g_hal->sleep_ms) g_hal->sleep_ms(ms);
}

static inline void hal_beep(int freq_hz, int duration_ms) {
    if (g_hal && g_hal->beep) g_hal->beep(freq_hz, duration_ms);
}

static inline void hal_watchdog_update(void) {
    if (g_hal && g_hal->watchdog_update) g_hal->watchdog_update();
}

static inline void hal_reboot(void) {
    if (g_hal && g_hal->reboot) g_hal->reboot();
}

#ifdef __cplusplus
}
#endif
