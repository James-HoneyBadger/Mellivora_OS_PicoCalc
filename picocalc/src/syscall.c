/*
 * syscall.c — Mellivora syscall compatibility shim for PicoCalc
 *
 * Maps the Mellivora-style syscall surface to concrete C function
 * calls on PicoCalc hardware.  Provides console output with paging,
 * ANSI filtering for the LCD, and file I/O wrappers.
 */

#define SYSCALL_IMPLEMENTATION
#include "syscall.h"

bool _sys_more_enabled = false;
bool _sys_more_abort = false;
volatile bool _sys_interrupted = false;
int64_t _sys_epoch_offset_ms = 0;

static bool _sys_lcd_in_ansi = false;
static bool _sys_lcd_in_csi  = false; /* inside ESC [ ... sequence */
static int _sys_more_lines = 0;

static void sys_console_write_raw_char(char c) {
    putchar_raw(c);

    if (_sys_lcd_in_csi) {
        /* Inside CSI (ESC [): suppress until the final byte (0x40–0x7E). */
        if ((unsigned char)c >= 0x40 && (unsigned char)c <= 0x7E) {
            _sys_lcd_in_csi  = false;
            _sys_lcd_in_ansi = false;
        }
        return;
    }
    if (_sys_lcd_in_ansi) {
        /* After bare ESC: if the next char is '[' this is a CSI sequence;
         * otherwise it's a single-char escape and we're done. */
        if (c == '[') {
            _sys_lcd_in_csi = true;
        } else {
            _sys_lcd_in_ansi = false;
        }
        return;
    }
    if ((unsigned char)c == 0x1B) {
        _sys_lcd_in_ansi = true;
        return;
    }
    if (c == '\n' || c == '\r' || c == '\b' || ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E)) {
        lcd_putc(c);
    }
}

void sys_more_reset(void) {
    _sys_more_abort = false;
    _sys_more_lines = 0;
}

void sys_more_set(bool enabled) {
    _sys_more_enabled = enabled;
    sys_more_reset();
}

static void sys_more_prompt(void) {
    static const char prompt[] = " -- MORE -- (Space=page, Enter=line, q=quit) ";
    static const char clear[] = "\r                                                \r";

    for (size_t i = 0; i < sizeof(prompt) - 1; i++) sys_console_write_raw_char(prompt[i]);
    int key = sys_getchar();
    for (size_t i = 0; i < sizeof(clear) - 1; i++) sys_console_write_raw_char(clear[i]);

    if (key == 'q' || key == 'Q' || key == 27) {
        _sys_more_abort = true;
        return;
    }

    if (key == ' ') _sys_more_lines = 0;
    else _sys_more_lines = LCD_ROWS > 2 ? LCD_ROWS - 3 : 0;
}

int sys_console_write_char(char c) {
    if (_sys_more_enabled && _sys_more_abort) return 1;

    sys_console_write_raw_char(c);

    if (_sys_more_enabled && c == '\n') {
        _sys_more_lines++;
        if (_sys_more_lines >= (LCD_ROWS > 2 ? LCD_ROWS - 2 : 1)) {
            sys_more_prompt();
        }
    }
    return 1;
}
