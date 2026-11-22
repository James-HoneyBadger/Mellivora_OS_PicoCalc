/*
 * apps/app_hw.c — Hardware Workbench: serial terminal, bus/GPIO tools,
 * file transfer, and the shell-facing wrappers around core/workbench.
 */

#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "audio.h"
#include "core/pinmap.h"
#include "core/vt100.h"
#include "core/workbench.h"
#include "core/xmodem.h"
#include "fat.h"
#include "hal/hw_ops.h"
#include "hardware/watchdog.h"
#include "kbd.h"
#include "lcd.h"
#include "syscall.h"
#include "ui/ui.h"

#define TERM_RX_CHUNK 64
#define TERM_STATUS_ROW (LCD_ROWS - 1)
#define TERM_ROWS (LCD_ROWS - 1)
#define TERM_COLS LCD_COLS

/* ------------------------------------------------------------------ */
/* Shell output adapter                                                */
/* ------------------------------------------------------------------ */

static void _print_line(void *ctx, const char *text) {
    (void)ctx;
    sys_print(text);
}

static wb_io_t _wb_io(void) {
    wb_io_t io = { .out = _print_line, .out_ctx = NULL };
    return io;
}

void app_hw(const char *arg) {
    char sub[16];
    const char *rest = next_token(arg, sub, sizeof sub);
    wb_io_t io = _wb_io();

    if (!*sub || ci_eq(sub, "pins") || ci_eq(sub, "map")) {
        (void)wb_pins(&io);
        return;
    }
    if (ci_eq(sub, "gpio")) {
        (void)wb_gpio(&io, rest);
        return;
    }
    if (ci_eq(sub, "pwm")) {
        (void)wb_pwm(&io, rest);
        return;
    }
    if (ci_eq(sub, "i2c") || ci_eq(sub, "i2cdetect")) {
        (void)wb_i2cdetect(&io, rest);
        return;
    }

    ui_title("Hardware Workbench");
    ui_line(UI_MUTED, "  hw pins            GPIO ownership and free pins");
    ui_line(UI_MUTED, "  term [opts]        full-screen VT100 serial terminal");
    ui_line(UI_MUTED, "  i2cdetect [range]  scan an I2C bus");
    ui_line(UI_MUTED, "  gpio ...           read, write, mode, pull, pwm");
    ui_line(UI_MUTED, "  pwm PIN HZ DUTY    start a PWM output");
    ui_line(UI_MUTED, "  xmodem send|recv   XMODEM transfer over UART or USB");
    ui_line(UI_MUTED, "  ymodem send|recv   YMODEM transfer with file names");
}

void app_gpio(const char *arg) {
    wb_io_t io = _wb_io();
    (void)wb_gpio(&io, arg);
}

void app_pwm(const char *arg) {
    wb_io_t io = _wb_io();
    (void)wb_pwm(&io, arg);
}

void app_i2cdetect(const char *arg) {
    wb_io_t io = _wb_io();
    (void)wb_i2cdetect(&io, arg);
}

/* ------------------------------------------------------------------ */
/* Serial terminal                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    int port;   /* 0/1 = hardware UART, -1 = USB CDC */
    uint32_t baud;
} term_port_t;

static uint32_t _color_for(uint8_t index, uint32_t fallback) {
    static const uint32_t base[8] = {
        LCD_RGB(0, 0, 0),     LCD_RGB(170, 0, 0),   LCD_RGB(0, 170, 0),
        LCD_RGB(170, 85, 0),  LCD_RGB(0, 0, 170),   LCD_RGB(170, 0, 170),
        LCD_RGB(0, 170, 170), LCD_RGB(170, 170, 170),
    };
    static const uint32_t bright[8] = {
        LCD_RGB(85, 85, 85),  LCD_RGB(255, 85, 85),  LCD_RGB(85, 255, 85),
        LCD_RGB(255, 255, 85), LCD_RGB(85, 85, 255), LCD_RGB(255, 85, 255),
        LCD_RGB(85, 255, 255), LCD_RGB(255, 255, 255),
    };
    if (index < 8) return base[index];
    if (index < 16) return bright[index - 8];
    return fallback;
}

typedef struct {
    term_port_t port;
    bool logging;
} term_sink_t;

static void _term_send(term_sink_t *sink, const char *data, size_t len) {
    if (!g_hw || len == 0) return;
    if (sink->port.port < 0) {
        if (g_hw->cdc_write) (void)g_hw->cdc_write((const uint8_t *)data, (uint32_t)len);
        return;
    }
    if (g_hw->uart_write) (void)g_hw->uart_write(sink->port.port, (const uint8_t *)data,
                                                 (uint32_t)len);
}

static void _term_reply(void *ctx, const char *text, size_t len) {
    _term_send((term_sink_t *)ctx, text, len);
}

static void _term_bell(void *ctx) {
    (void)ctx;
    audio_beep(880, 30);
}

static void _render_row(const vt100_t *vt, int row) {
    uint32_t fg_default = ui_color(UI_TEXT);
    uint32_t bg_default = ui_color(UI_BG);
    for (int col = 0; col < vt->cols; col++) {
        const vt100_cell_t *cell = vt100_cell_at(vt, col, row);
        if (!cell) break;

        uint8_t fg_index = cell->fg;
        uint8_t bg_index = cell->bg;
        if (fg_index != VT100_COLOR_DEFAULT && (cell->flags & VT100_FLAG_BOLD) && fg_index < 8)
            fg_index = (uint8_t)(fg_index + 8);
        uint32_t fg = _color_for(fg_index, fg_default);
        uint32_t bg = _color_for(bg_index, bg_default);
        if (cell->flags & VT100_FLAG_REVERSE) {
            uint32_t swap = fg;
            fg = bg;
            bg = swap;
        }
        char ch = (char)cell->ch;
        lcd_draw_cell(col, row, ch, fg, bg);
    }
}

static void _render_status(const term_port_t *port, bool echo, const char *log_path) {
    char line[TERM_COLS + 1];
    char left[24];
    snprintf(left, sizeof left, "%s %lu 8N1", port->port < 0 ? "usb" : "uart",
             (unsigned long)port->baud);
    snprintf(line, sizeof line, "%-20.20s %s%s %s", left, echo ? "echo " : "     ",
             *log_path ? "log" : "   ", "^] exit");

    uint32_t fg = ui_color(UI_STATUS_TEXT);
    uint32_t bg = ui_color(UI_STATUS_BG);
    for (int col = 0; col < TERM_COLS; col++) {
        char ch = line[col] ? line[col] : ' ';
        lcd_draw_cell(col, TERM_STATUS_ROW, ch, fg, bg);
    }
}

static void _term_help(void) {
    ui_line(UI_TEXT, "usage: term [-p uart1|uart0|usb] [-b BAUD] [-e] [-l FILE]");
    ui_line(UI_MUTED, "  -p  port: uart1 (GP4/GP5, default), uart0 (console pins), usb");
    ui_line(UI_MUTED, "  -b  baud rate: 300..921600 (default 115200)");
    ui_line(UI_MUTED, "  -e  local echo (useful when the far end does not echo)");
    ui_line(UI_MUTED, "  -l  append the raw session to FILE on the SD card");
    ui_line(UI_MUTED, "  Exit with Ctrl-] or Ctrl-X.");
}

void app_term(const char *arg) {
    term_port_t port = { .port = 1, .baud = 115200 };
    bool echo = false;
    char log_path[128] = "";

    char token[64];
    const char *cursor = arg;
    for (;;) {
        cursor = next_token(cursor, token, sizeof token);
        if (!*token) break;
        if (ci_eq(token, "-h") || ci_eq(token, "--help") || ci_eq(token, "help")) {
            _term_help();
            return;
        }
        if (ci_eq(token, "-e") || ci_eq(token, "--echo")) {
            echo = true;
        } else if (ci_eq(token, "-p") || ci_eq(token, "-b") || ci_eq(token, "-l")) {
            char value[64];
            cursor = next_token(cursor, value, sizeof value);
            if (!*value) {
                _term_help();
                return;
            }
            if (ci_eq(token, "-p")) {
                if (ci_eq(value, "usb") || ci_eq(value, "cdc")) port.port = -1;
                else if (ci_eq(value, "uart0")) port.port = 0;
                else if (ci_eq(value, "uart1")) port.port = 1;
                else {
                    ui_error("term", "unknown port (uart0, uart1, usb)");
                    return;
                }
            } else if (ci_eq(token, "-b")) {
                int baud = atoi(value);
                if (baud < 300 || baud > 921600) {
                    ui_error("term", "baud must be 300-921600");
                    return;
                }
                port.baud = (uint32_t)baud;
            } else {
                copy_cstr(log_path, sizeof log_path, value);
            }
        } else {
            _term_help();
            return;
        }
    }

    if (!g_hw) {
        ui_error("term", "hardware layer unavailable");
        return;
    }

    term_sink_t sink = { .port = port, .logging = false };
    pinmap_placement_t place;
    if (port.port >= 0) {
        if (!pinmap_uart(port.port, &place)) {
            ui_error("term", "no free pins for that UART");
            return;
        }
        if (!g_hw->uart_open || !g_hw->uart_open(port.port, port.baud, place.tx, place.rx)) {
            ui_error("term", "could not open the UART");
            return;
        }
    }

    if (*log_path) {
        char absolute[160];
        app_make_abs(log_path, absolute, sizeof absolute);
        copy_cstr(log_path, sizeof log_path, absolute);
        (void)fat_unlink(log_path);
        sink.logging = true;
    }

    size_t cell_count = (size_t)TERM_COLS * (size_t)TERM_ROWS;
    vt100_cell_t *cells = calloc(cell_count, sizeof *cells);
    if (!cells) {
        ui_error("term", "out of memory");
        return;
    }

    vt100_t vt;
    vt100_init(&vt, cells, TERM_COLS, TERM_ROWS);
    vt.write = _term_reply;
    vt.write_ctx = &sink;
    vt.bell = _term_bell;
    vt.bell_ctx = NULL;

    ui_set_fullscreen(true);
    lcd_cls(ui_color(UI_BG));
    lcd_cursor_enable(true, true);
    _render_status(&port, echo, log_path);

    uint8_t rx[TERM_RX_CHUNK];
    bool running = true;
    while (running) {
        int got = 0;
        if (port.port < 0) {
            if (g_hw->cdc_read) got = g_hw->cdc_read(rx, sizeof rx);
        } else if (g_hw->uart_read) {
            got = g_hw->uart_read(port.port, rx, sizeof rx);
        }

        if (got > 0) {
            vt100_feed_bytes(&vt, rx, (size_t)got);
            if (sink.logging) (void)fat_append(log_path, rx, (uint32_t)got);

            uint32_t dirty = vt100_take_dirty(&vt);
            for (int row = 0; row < vt.rows; row++) {
                if (dirty & ((uint32_t)1u << (unsigned)row)) _render_row(&vt, row);
            }
            lcd_set_cursor(vt100_cursor_col(&vt), vt100_cursor_row(&vt));
        }

        int key = kbd_getc();
        if (key >= 0) {
            if (key == 0x1D || key == 24) break; /* Ctrl-] or Ctrl-X */
            char ch = (char)key;
            _term_send(&sink, &ch, 1);
            if (echo) {
                vt100_feed(&vt, (uint8_t)key);
                uint32_t dirty = vt100_take_dirty(&vt);
                for (int row = 0; row < vt.rows; row++) {
                    if (dirty & ((uint32_t)1u << (unsigned)row)) _render_row(&vt, row);
                }
                lcd_set_cursor(vt100_cursor_col(&vt), vt100_cursor_row(&vt));
            }
        }

        watchdog_update();
        if (got == 0 && key < 0) sleep_ms(5);
    }

    if (port.port >= 0 && g_hw->uart_close) g_hw->uart_close(port.port);
    lcd_cursor_enable(false, false);
    ui_set_fullscreen(false);
    free(cells);
    ui_clear();
}

/* ------------------------------------------------------------------ */
/* XMODEM / YMODEM                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    term_port_t port;
    const char *path;
    fat_file_t file;
    uint32_t written;
    uint32_t limit;
} xfer_t;

static int _xfer_getc(void *ctx, uint32_t timeout_ms) {
    xfer_t *xfer = (xfer_t *)ctx;
    uint32_t started = sys_time_ms();
    for (;;) {
        uint8_t byte = 0;
        int got = 0;
        if (xfer->port.port < 0) {
            if (g_hw->cdc_read) got = g_hw->cdc_read(&byte, 1);
        } else if (g_hw->uart_read) {
            got = g_hw->uart_read(xfer->port.port, &byte, 1);
        }
        if (got > 0) return byte;
        if (sys_time_ms() - started >= timeout_ms) return -1;
        sleep_ms(2);
    }
}

static int _xfer_putc(void *ctx, uint8_t byte) {
    xfer_t *xfer = (xfer_t *)ctx;
    if (xfer->port.port < 0) {
        if (!g_hw->cdc_write) return -1;
        return g_hw->cdc_write(&byte, 1) == 1 ? 0 : -1;
    }
    if (!g_hw->uart_write) return -1;
    return g_hw->uart_write(xfer->port.port, &byte, 1) == 1 ? 0 : -1;
}

static int _xfer_on_data(void *ctx, const uint8_t *data, uint32_t len) {
    xfer_t *xfer = (xfer_t *)ctx;
    if (xfer->limit) {
        uint32_t room = xfer->limit > xfer->written ? xfer->limit - xfer->written : 0;
        if (len > room) len = room;
    }
    if (len == 0) return 0;
    if (fat_append(xfer->path, data, len) != FAT_OK) return -1;
    xfer->written += len;
    return 0;
}

static int _xfer_read(void *ctx, void *buf, uint32_t len) {
    xfer_t *xfer = (xfer_t *)ctx;
    int32_t got = fat_read(&xfer->file, buf, len);
    if (got < 0) return -1;
    return (int)got;
}

static void app_xfer(const char *arg, bool ymodem) {
    const char *action = (const char *)arg;
    char token[64];
    action = next_token(action, token, sizeof token);
    if (!*token) {
        ui_line(UI_TEXT, ymodem ? "usage: ymodem send|recv [FILE] [-p uart1|uart0|usb] [-b BAUD]"
                                : "usage: xmodem send|recv FILE [-p uart1|uart0|usb] [-b BAUD]");
        return;
    }

    bool sending = ci_eq(token, "send") || ci_eq(token, "s");
    if (!sending && !ci_eq(token, "recv") && !ci_eq(token, "receive") && !ci_eq(token, "r")) {
        ui_error("xfer", "expected send or recv");
        return;
    }

    char path[128] = "";
    char value[64];
    term_port_t port = { .port = 1, .baud = 115200 };
    for (;;) {
        action = next_token(action, token, sizeof token);
        if (!*token) break;
        if (ci_eq(token, "-p")) {
            action = next_token(action, value, sizeof value);
            if (ci_eq(value, "usb") || ci_eq(value, "cdc")) port.port = -1;
            else if (ci_eq(value, "uart0")) port.port = 0;
            else if (ci_eq(value, "uart1")) port.port = 1;
        } else if (ci_eq(token, "-b")) {
            action = next_token(action, value, sizeof value);
            int baud = atoi(value);
            if (baud >= 300 && baud <= 921600) port.baud = (uint32_t)baud;
        } else if (!*path) {
            copy_cstr(path, sizeof path, token);
        }
    }

    if (!g_hw) {
        ui_error("xfer", "hardware layer unavailable");
        return;
    }
    if (sending && !*path) {
        ui_error("xfer", "a file name is required to send");
        return;
    }

    xfer_t xfer;
    memset(&xfer, 0, sizeof xfer);
    xfer.port = port;
    xfer.path = path;

    if (port.port >= 0) {
        pinmap_placement_t place;
        if (!pinmap_uart(port.port, &place) ||
            !g_hw->uart_open || !g_hw->uart_open(port.port, port.baud, place.tx, place.rx)) {
            ui_error("xfer", "could not open the UART");
            return;
        }
    }

    xm_config_t cfg;
    xm_config_default(&cfg);
    cfg.ymodem = ymodem;
    cfg.crc = true;
    cfg.timeout_ms = 10000;

    xm_io_t io = {
        .getc = _xfer_getc,
        .putc = _xfer_putc,
        .on_data = _xfer_on_data,
        .read = _xfer_read,
    };

    int result;
    if (sending) {
        if (fat_open(path, &xfer.file) != FAT_OK) {
            ui_error("xfer", "cannot open the file");
            goto done;
        }
        uint32_t total = xfer.file.size;
        if (ymodem) copy_cstr(cfg.name, sizeof cfg.name, path[0] == '/' ? path + 1 : path);
        ui_line(UI_TEXT, "Waiting for the receiver (start the transfer now)...");
        result = xm_send(&io, &xfer, &cfg, total);
        if (result < 0) ui_error("xfer", "transfer failed");
        else {
            char line[80];
            snprintf(line, sizeof line, "Sent %d bytes", result);
            ui_ok(line);
        }
    } else {
        /* Without a name, stream to a scratch file and rename it from the
           YMODEM metadata afterwards. */
        char scratch[160];
        if (*path) {
            copy_cstr(scratch, sizeof scratch, path);
        } else {
            copy_cstr(scratch, sizeof scratch, "/RECV.TMP");
        }
        xfer.path = scratch;
        (void)fat_unlink(scratch);
        ui_line(UI_TEXT, "Waiting for the sender...");
        result = xm_receive(&io, &xfer, &cfg);
        if (result < 0) {
            ui_error("xfer", "transfer failed");
            (void)fat_unlink(scratch);
        } else {
            if (!*path) {
                if (!cfg.name[0]) {
                    ui_error("xfer", "sender did not announce a file name");
                    (void)fat_unlink(scratch);
                    goto done;
                }
                char named[160];
                snprintf(named, sizeof named, "/%s", cfg.name);
                if (fat_rename(scratch, named) != FAT_OK) {
                    ui_error("xfer", "could not rename the received file");
                    goto done;
                }
                copy_cstr(path, sizeof path, named);
            }
            char line[96];
            snprintf(line, sizeof line, "Received %d bytes -> %s", result, path);
            ui_ok(line);
        }
    }

done:
    if (port.port >= 0 && g_hw->uart_close) g_hw->uart_close(port.port);
}

void app_xmodem(const char *arg) { app_xfer(arg, false); }

void app_ymodem(const char *arg) { app_xfer(arg, true); }

/* ------------------------------------------------------------------ */
/* Language bindings                                                   */
/* ------------------------------------------------------------------ */

static bool _pin_usable(int pin) {
    return pin >= 0 && pin < PINMAP_PIN_COUNT && !pinmap_is_reserved(pin);
}

int hw_binding_gpio_read(int pin) {
    if (!g_hw || !g_hw->gpio_get || !_pin_usable(pin)) return -1;
    return g_hw->gpio_get(pin) ? 1 : 0;
}

int hw_binding_gpio_write(int pin, int value) {
    if (!g_hw || !g_hw->gpio_put || !_pin_usable(pin)) return -1;
    if (g_hw->gpio_claim) (void)g_hw->gpio_claim(pin, true);
    g_hw->gpio_put(pin, value != 0);
    return 0;
}

int hw_binding_pwm(int pin, int freq_hz, int duty) {
    if (!g_hw || !_pin_usable(pin)) return -1;
    if (duty < 0) {
        if (g_hw->pwm_stop) g_hw->pwm_stop(pin);
        return 0;
    }
    if (duty > 100 || freq_hz <= 0) return -1;
    if (!g_hw->pwm_start) return -1;
    return g_hw->pwm_start(pin, (uint32_t)freq_hz, (uint8_t)duty) ? 0 : -1;
}

int hw_binding_i2c_scan(void) {
    wb_io_t io = _wb_io();
    return wb_i2cdetect(&io, "");
}

int hw_binding_xfer(int sending, const char *path, int ymodem) {
    char buffer[160];
    snprintf(buffer, sizeof buffer, "%s %s", sending ? "send" : "recv", path ? path : "");
    app_xfer(buffer, ymodem != 0);
    return 0;
}
