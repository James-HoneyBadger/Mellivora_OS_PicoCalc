#pragma once
/*
 * syscall.h — Mellivora syscall compatibility shim for PicoCalc (RP2040)
 *
 * This header keeps a stable Mellivora-style syscall surface for the
 * PicoCalc firmware by mapping logical syscall numbers to C function calls.
 * The goal is to let the app layer share a familiar interface while running
 * natively on RP2040 hardware.
 *
 * Usage:
 *   #include "syscall.h"
 *   // Call via the wrapper macros:
 *   int fd = sys_open("/README.TXT", O_RDONLY);
 *
 * Only the subset of syscalls that make sense on PicoCalc hardware is
 * provided. Unsupported platform-specific calls are stubbed with a
 * compile-time error or a no-op, depending on severity.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "fat.h"
#include "lcd.h"
#include "kbd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Syscall numbers used by the Mellivora app layer                      */
/* ------------------------------------------------------------------ */

#define SYS_EXIT        0
#define SYS_PUTCHAR     1
#define SYS_GETCHAR     2
#define SYS_PRINT       3
#define SYS_READ_KEY    4
#define SYS_OPEN        5
#define SYS_READ        6
#define SYS_WRITE       7
#define SYS_CLOSE       8
#define SYS_DELETE      9
#define SYS_SEEK        10
#define SYS_STAT        11
#define SYS_MKDIR       12
#define SYS_READDIR     13
#define SYS_SETCURSOR   14
#define SYS_GETTIME     15
#define SYS_SLEEP       16
#define SYS_CLEAR       17
#define SYS_SETCOLOR    18
#define SYS_MALLOC      19
#define SYS_FREE        20
#define SYS_EXEC        21
#define SYS_DISK_READ   22
#define SYS_DISK_WRITE  23
#define SYS_BEEP        24
#define SYS_DATE        25
#define SYS_CHDIR       26
#define SYS_GETCWD      27
#define SYS_SERIAL      28
#define SYS_GETENV      29
#define SYS_FREAD       30
#define SYS_FWRITE      31
#define SYS_GETARGS     32
#define SYS_SERIAL_IN   33
#define SYS_STDIN_READ  34
#define SYS_YIELD       35
#define SYS_MOUSE       36
#define SYS_FRAMEBUF    37
#define SYS_GUI         38

/* ------------------------------------------------------------------ */
/* Open flags                                                           */
/* ------------------------------------------------------------------ */

#define O_RDONLY    0x00
#define O_WRONLY    0x01
#define O_RDWR      0x02
#define O_CREAT     0x04
#define O_TRUNC     0x08

/* ------------------------------------------------------------------ */
/* Internal file descriptor table                                       */
/* ------------------------------------------------------------------ */

#define SYSCALL_MAX_FD  8

typedef struct {
    bool       open;
    fat_file_t fat_f;
    char       path[256];
} _sys_fd_t;

/* Defined in syscall.c (or instantiated in a single translation unit
 * by defining SYSCALL_IMPLEMENTATION before including this header). */
#ifndef SYSCALL_EXTERN
#  define SYSCALL_EXTERN extern
#endif

SYSCALL_EXTERN _sys_fd_t _sys_fds[SYSCALL_MAX_FD];
SYSCALL_EXTERN char      _sys_cwd[256];

/* ------------------------------------------------------------------ */
/* Syscall wrappers                                                     */
/* ------------------------------------------------------------------ */

static inline void _sys_make_abs(const char *path, char *abs, size_t abs_size) {
    if (!path || !*path) {
        strncpy(abs, _sys_cwd, abs_size - 1);
    } else if (path[0] == '/') {
        strncpy(abs, path, abs_size - 1);
    } else if (strcmp(_sys_cwd, "/") == 0) {
        snprintf(abs, abs_size, "/%s", path);
    } else {
        snprintf(abs, abs_size, "%s/%s", _sys_cwd, path);
    }
    abs[abs_size - 1] = '\0';
}

/* Terminate: on PicoCalc there is no process model yet. */
static inline void sys_exit(int code) {
    (void)code;
    for (;;) sleep_ms(1000);
}

/* Raw console output helper. */
static inline int sys_console_write(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        putchar_raw(buf[i]);
        lcd_putc(buf[i]);
    }
    return len;
}

/* Common Mellivora helpers used by simple apps. */
static inline int sys_print(const char *s) {
    return sys_console_write(s, (int)strlen(s));
}

static inline void sys_putchar(char c) {
    putchar_raw(c);
    lcd_putc(c);
}

static inline int sys_getchar(void) {
    int c;
    do {
        c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) c = kbd_getc();
    } while (c < 0);
    return c;
}

static inline int sys_read_key(void) {
    return sys_getchar();
}

static inline void sys_clear(void) {
    lcd_cls(LCD_BLACK);
    sys_console_write("\x1b[2J\x1b[H", 7);
}

static inline void sys_setcolor(uint8_t color) {
    (void)color;
}

static inline int sys_getscreenw(void) { return LCD_COLS; }
static inline int sys_getscreenh(void) { return LCD_ROWS; }
static inline void sys_sleep(uint32_t ms) { sleep_ms(ms); }
static inline uint32_t sys_time_ms(void) { return (uint32_t)to_ms_since_boot(get_absolute_time()); }

/* Open a file. Returns fd >= 0 or -1 on error. */
static inline int sys_open(const char *path, int flags) {
    int fd = -1;
    for (int i = 0; i < SYSCALL_MAX_FD; i++) {
        if (!_sys_fds[i].open) { fd = i; break; }
    }
    if (fd < 0) return -1;

    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);

    fat_result_t r;
    if (flags & O_CREAT) {
        r = fat_open(abs, &_sys_fds[fd].fat_f);
        if (r == FAT_ERR_NOTFOUND) {
            r = fat_create(abs, NULL, 0);
            if (r != FAT_OK) return -1;
            r = fat_open(abs, &_sys_fds[fd].fat_f);
        }
    } else {
        r = fat_open(abs, &_sys_fds[fd].fat_f);
    }
    if (r != FAT_OK) return -1;

    _sys_fds[fd].open = true;
    strncpy(_sys_fds[fd].path, abs, sizeof _sys_fds[fd].path - 1);
    _sys_fds[fd].path[sizeof _sys_fds[fd].path - 1] = '\0';
    return fd;
}

static inline int sys_close(int fd) {
    if (fd < 0 || fd >= SYSCALL_MAX_FD) return -1;
    _sys_fds[fd].open = false;
    _sys_fds[fd].path[0] = '\0';
    return 0;
}

static inline int sys_read(int fd, void *buf, uint32_t len) {
    if (fd < 0 || fd >= SYSCALL_MAX_FD || !_sys_fds[fd].open) return -1;
    int32_t n = fat_read(&_sys_fds[fd].fat_f, buf, len);
    return (n == FAT_ERR_EOF) ? 0 : (int)n;
}

/* Minimal write support: rewrite whole file from the given buffer. */
static inline int sys_write(int fd, const void *buf, uint32_t len) {
    if (fd < 0 || fd >= SYSCALL_MAX_FD || !_sys_fds[fd].open) return -1;
    fat_unlink(_sys_fds[fd].path);
    return (fat_create(_sys_fds[fd].path, (const uint8_t *)buf, len) == FAT_OK) ? (int)len : -1;
}

static inline int sys_mkdir(const char *path) {
    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);
    return (fat_mkdir(abs) == FAT_OK) ? 0 : -1;
}

static inline int sys_unlink(const char *path) {
    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);
    return (fat_unlink(abs) == FAT_OK) ? 0 : -1;
}

static inline int sys_delete(const char *path) {
    return sys_unlink(path);
}

static inline int sys_chdir(const char *path) {
    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);
    if (fat_is_dir(abs) != FAT_OK) return -1;
    strncpy(_sys_cwd, abs, 255);
    _sys_cwd[255] = '\0';
    return 0;
}

static inline const char *sys_getcwd(void) { return _sys_cwd; }

static inline int sys_fread(const char *path, void *buf, uint32_t max_len) {
    fat_file_t f;
    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);
    if (fat_open(abs, &f) != FAT_OK) return -1;
    int32_t n = fat_read(&f, buf, max_len);
    return (n < 0) ? -1 : (int)n;
}

static inline int sys_fwrite(const char *path, const void *buf, uint32_t len) {
    char abs[256];
    _sys_make_abs(path, abs, sizeof abs);
    fat_unlink(abs);
    return (fat_create(abs, (const uint8_t *)buf, len) == FAT_OK) ? (int)len : -1;
}

/* ------------------------------------------------------------------ */
/* Optional: define SYSCALL_IMPLEMENTATION in exactly one .c file to   */
/* instantiate the global fd table and cwd.                             */
/* ------------------------------------------------------------------ */
#ifdef SYSCALL_IMPLEMENTATION
_sys_fd_t _sys_fds[SYSCALL_MAX_FD];
char      _sys_cwd[256] = "/";
#endif

#ifdef __cplusplus
}
#endif
