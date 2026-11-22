/*
 * core/workbench.c — Hardware Workbench commands.
 *
 * Commands are driven by short argument strings so the host tests can call
 * the same entry points the shell does.
 */

#include "core/workbench.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/i2cdetect.h"
#include "hal/hw_ops.h"

#define WB_LINE 96

void wb_emit(const wb_io_t *io, const char *text) {
    if (io && io->out && text) io->out(io->out_ctx, text);
}

void wb_emitf(const wb_io_t *io, const char *fmt, ...) {
    char buffer[WB_LINE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    wb_emit(io, buffer);
}

/* ------------------------------------------------------------------ */
/* Argument helpers                                                    */
/* ------------------------------------------------------------------ */

static char *_skip_ws(char *text) {
    while (text && (*text == ' ' || *text == '\t'))
        text++;
    return text;
}

static bool _next_arg(char **cursor, char *out, size_t cap) {
    if (!cursor || !*cursor || !out || cap == 0) return false;
    char *p = _skip_ws(*cursor);
    if (!*p) {
        out[0] = '\0';
        *cursor = p;
        return false;
    }
    size_t used = 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (used + 1 < cap) out[used++] = *p;
        p++;
    }
    out[used] = '\0';
    *cursor = p;
    return true;
}

static bool _ci_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static int _parse_int(const char *text, bool *ok) {
    if (ok) *ok = false;
    if (!text || !*text) return 0;
    int value = 0;
    int sign = 1;
    const char *p = text;
    int base = 10;

    if (*p == '-') {
        sign = -1;
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    if (!*p) return 0;

    for (; *p; p++) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f')
            digit = *p - 'a' + 10;
        else if (base == 16 && *p >= 'A' && *p <= 'F')
            digit = *p - 'A' + 10;
        else
            return 0;
        if (value < 1000000) value = value * base + digit;
    }
    if (ok) *ok = true;
    return value * sign;
}

static bool _hardware_ready(const wb_io_t *io) {
    if (g_hw) return true;
    wb_emit(io, "hardware layer is not available on this build\n");
    return false;
}

/* Reject pins owned by the display, storage, keyboard, audio, or radio. */
static bool _pin_allowed(const wb_io_t *io, int pin, bool force) {
    if (pin < 0 || pin >= PINMAP_PIN_COUNT) {
        wb_emitf(io, "invalid GPIO %d (valid range 0-%d)\n", pin, PINMAP_PIN_COUNT - 1);
        return false;
    }
    if (!force && pinmap_is_reserved(pin)) {
        wb_emitf(io, "GP%d is assigned to %s; pass --force to override\n", pin,
                 pinmap_role_name(pinmap_role(pin)));
        return false;
    }
    if (!force && pinmap_is_console(pin)) {
        wb_emitf(io, "GP%d carries the serial console; pass --force to override\n", pin);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* hw pins                                                             */
/* ------------------------------------------------------------------ */

static void _emit_run(const wb_io_t *io, int first, int last, pin_role_t role) {
    char pins[16];
    if (first == last) (void)snprintf(pins, sizeof pins, "GP%d", first);
    else
        (void)snprintf(pins, sizeof pins, "GP%d-%d", first, last);
    wb_emitf(io, "  %-9s %-10s %s\n", pins, pinmap_role_name(role),
             role == PIN_ROLE_LCD_SPI     ? "ILI9488 display"
             : role == PIN_ROLE_SD_SPI    ? "microSD (GP22 detect)"
             : role == PIN_ROLE_KBD_I2C   ? "keyboard co-processor"
             : role == PIN_ROLE_AUDIO_PWM ? "left/right PWM"
             : role == PIN_ROLE_BOARD     ? "power stable / radio enable"
             : role == PIN_ROLE_CYW43     ? "CYW43439 bus"
                                          : "");
}

static void _emit_free_list(const wb_io_t *io) {
    char line[WB_LINE];
    size_t used = 0;
    int n = snprintf(line, sizeof line, "Free:");
    if (n < 0) return;
    used = (size_t)n;
    for (int pin = 0; pin < PINMAP_PIN_COUNT; pin++) {
        if (!pinmap_is_free(pin)) continue;
        n = snprintf(line + used, sizeof line - used, " %d", pin);
        if (n < 0 || (size_t)n >= sizeof line - used) break;
        used += (size_t)n;
    }
    n = snprintf(line + used, sizeof line - used, "  (%d pins)\n", pinmap_free_count());
    if (n > 0 && (size_t)n < sizeof line - used) wb_emit(io, line);
}

int wb_pins(const wb_io_t *io) {
    if (!io) return -1;
    wb_emitf(io, "Pin map (%s)\n", pinmap_has_wlan() ? "pico-w" : "rp2040/rp2350");

    pin_role_t role = pinmap_role(0);
    int run_start = 0;
    for (int pin = 1; pin <= PINMAP_PIN_COUNT; pin++) {
        pin_role_t current = pin < PINMAP_PIN_COUNT ? pinmap_role(pin) : PIN_ROLE_COUNT;
        if (current == role) continue;
        if (role != PIN_ROLE_FREE && role != PIN_ROLE_CONSOLE && role != PIN_ROLE_COUNT)
            _emit_run(io, run_start, pin - 1, role);
        role = current;
        run_start = pin;
    }

    _emit_free_list(io);

    pinmap_placement_t place;
    for (int index = 0; index <= 1; index++) {
        if (!pinmap_uart(index, &place)) continue;
        wb_emitf(io, "UART  %-5s TX GP%d  RX GP%d%s\n", place.label, place.tx, place.rx,
                 place.uses_console ? "  (shares console)" : "");
    }
    for (int index = 0; index <= 1; index++) {
        if (!pinmap_i2c(index, &place)) continue;
        wb_emitf(io, "I2C   %-5s SDA GP%d  SCL GP%d\n", place.label, place.sda, place.scl);
    }
    for (int index = 0; index <= 1; index++) {
        if (!pinmap_spi(index, &place)) continue;
        wb_emitf(io, "SPI   %-5s SCK GP%d  MOSI GP%d  MISO GP%d  CS GP%d%s\n", place.label,
                 place.sck, place.mosi, place.miso, place.cs,
                 place.uses_console ? "  (shares console)" : "");
    }
    if (!pinmap_spi(0, &place) && !pinmap_spi(1, &place))
        wb_emit(io, "SPI   no free controller: every pin set is in use\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* gpio                                                                */
/* ------------------------------------------------------------------ */

static void _gpio_usage(const wb_io_t *io) {
    wb_emit(io, "usage: gpio [--force] read PIN\n"
                "       gpio [--force] PIN high|low|1|0\n"
                "       gpio [--force] mode PIN in|out\n"
                "       gpio [--force] pull PIN up|down|none\n"
                "       gpio [--force] free PIN\n"
                "       gpio list\n");
}

int wb_gpio(const wb_io_t *io, const char *args) {
    if (!io) return -1;
    char buffer[128];
    (void)snprintf(buffer, sizeof buffer, "%s", args ? args : "");
    char *cursor = buffer;
    char token[16];
    bool force = false;

    if (!_next_arg(&cursor, token, sizeof token)) {
        _gpio_usage(io);
        return -1;
    }
    if (_ci_eq(token, "--force")) {
        force = true;
        if (!_next_arg(&cursor, token, sizeof token)) {
            _gpio_usage(io);
            return -1;
        }
    }

    if (_ci_eq(token, "list")) {
        wb_emit(io, "Free GPIO pins:");
        for (int pin = 0; pin < PINMAP_PIN_COUNT; pin++) {
            if (pinmap_is_free(pin)) wb_emitf(io, " %d", pin);
        }
        wb_emit(io, "\n");
        return 0;
    }

    if (_ci_eq(token, "read")) {
        char pin_text[8];
        bool ok = false;
        if (!_next_arg(&cursor, pin_text, sizeof pin_text)) {
            _gpio_usage(io);
            return -1;
        }
        int pin = _parse_int(pin_text, &ok);
        if (!ok || !_pin_allowed(io, pin, force)) return -1;
        if (!_hardware_ready(io)) return -1;
        if (!g_hw->gpio_get) return -1;
        wb_emitf(io, "GP%d = %d\n", pin, g_hw->gpio_get(pin) ? 1 : 0);
        return 0;
    }

    if (_ci_eq(token, "mode") || _ci_eq(token, "claim")) {
        char pin_text[8];
        char mode_text[8];
        bool ok = false;
        if (!_next_arg(&cursor, pin_text, sizeof pin_text) ||
            !_next_arg(&cursor, mode_text, sizeof mode_text)) {
            _gpio_usage(io);
            return -1;
        }
        int pin = _parse_int(pin_text, &ok);
        if (!ok || !_pin_allowed(io, pin, force)) return -1;
        if (!_hardware_ready(io)) return -1;
        if (!g_hw->gpio_claim) return -1;
        bool output = _ci_eq(mode_text, "out") || _ci_eq(mode_text, "output");
        if (!g_hw->gpio_claim(pin, output)) {
            wb_emitf(io, "could not configure GP%d\n", pin);
            return -1;
        }
        wb_emitf(io, "GP%d configured as %s\n", pin, output ? "output" : "input");
        return 0;
    }

    if (_ci_eq(token, "pull")) {
        char pin_text[8];
        char mode_text[8];
        bool ok = false;
        if (!_next_arg(&cursor, pin_text, sizeof pin_text) ||
            !_next_arg(&cursor, mode_text, sizeof mode_text)) {
            _gpio_usage(io);
            return -1;
        }
        int pin = _parse_int(pin_text, &ok);
        if (!ok || !_pin_allowed(io, pin, force)) return -1;
        if (!_hardware_ready(io)) return -1;
        if (!g_hw->gpio_pull) return -1;
        int mode = _ci_eq(mode_text, "up") ? 1 : (_ci_eq(mode_text, "down") ? 2 : 0);
        if (!g_hw->gpio_pull(pin, mode)) {
            wb_emitf(io, "could not set the pull on GP%d\n", pin);
            return -1;
        }
        wb_emitf(io, "GP%d pull %s\n", pin, mode == 1 ? "up" : mode == 2 ? "down" : "off");
        return 0;
    }

    if (_ci_eq(token, "free") || _ci_eq(token, "release")) {
        char pin_text[8];
        bool ok = false;
        if (!_next_arg(&cursor, pin_text, sizeof pin_text)) {
            _gpio_usage(io);
            return -1;
        }
        int pin = _parse_int(pin_text, &ok);
        if (!ok || !_pin_allowed(io, pin, force)) return -1;
        if (!_hardware_ready(io)) return -1;
        if (g_hw->pwm_stop) g_hw->pwm_stop(pin);
        if (g_hw->gpio_release) g_hw->gpio_release(pin);
        wb_emitf(io, "GP%d released\n", pin);
        return 0;
    }

    /* gpio PIN high|low|1|0 */
    {
        char value_text[8];
        bool ok = false;
        int pin = _parse_int(token, &ok);
        if (!ok) {
            _gpio_usage(io);
            return -1;
        }
        if (!_next_arg(&cursor, value_text, sizeof value_text)) {
            _gpio_usage(io);
            return -1;
        }
        if (!_pin_allowed(io, pin, force)) return -1;
        if (!_hardware_ready(io)) return -1;
        bool high = _ci_eq(value_text, "high") || _ci_eq(value_text, "1") ||
                    _ci_eq(value_text, "on") || _ci_eq(value_text, "true");
        if (g_hw->gpio_claim && !g_hw->gpio_claim(pin, true)) {
            wb_emitf(io, "could not claim GP%d\n", pin);
            return -1;
        }
        if (!g_hw->gpio_put) return -1;
        g_hw->gpio_put(pin, high);
        wb_emitf(io, "GP%d = %d\n", pin, high ? 1 : 0);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* pwm                                                                 */
/* ------------------------------------------------------------------ */

int wb_pwm(const wb_io_t *io, const char *args) {
    if (!io) return -1;
    char buffer[128];
    (void)snprintf(buffer, sizeof buffer, "%s", args ? args : "");
    char *cursor = buffer;
    char token[16];
    bool force = false;

    if (!_next_arg(&cursor, token, sizeof token)) {
        wb_emit(io, "usage: pwm [--force] PIN FREQ_HZ DUTY_PERCENT\n"
                    "       pwm [--force] PIN DUTY_PERCENT\n"
                    "       pwm [--force] PIN off\n");
        return -1;
    }
    if (_ci_eq(token, "--force")) {
        force = true;
        if (!_next_arg(&cursor, token, sizeof token)) {
            wb_emit(io, "usage: pwm [--force] PIN FREQ_HZ DUTY_PERCENT\n");
            return -1;
        }
    }

    bool ok = false;
    int pin = _parse_int(token, &ok);
    if (!ok || !_pin_allowed(io, pin, force)) return -1;
    if (!_hardware_ready(io)) return -1;

    char first[12];
    if (!_next_arg(&cursor, first, sizeof first)) {
        wb_emit(io, "pwm: missing frequency or duty\n");
        return -1;
    }
    if (_ci_eq(first, "off")) {
        if (!g_hw->pwm_stop) return -1;
        g_hw->pwm_stop(pin);
        wb_emitf(io, "PWM on GP%d stopped\n", pin);
        return 0;
    }

    char second[12];
    if (!_next_arg(&cursor, second, sizeof second)) {
        /* Two arguments: change the duty cycle only. */
        int duty = _parse_int(first, &ok);
        if (!ok || duty < 0 || duty > 100) {
            wb_emit(io, "pwm: duty must be 0-100\n");
            return -1;
        }
        if (!g_hw->pwm_duty || !g_hw->pwm_duty(pin, (uint8_t)duty)) {
            wb_emitf(io, "PWM is not running on GP%d\n", pin);
            return -1;
        }
        wb_emitf(io, "PWM GP%d duty %d%%\n", pin, duty);
        return 0;
    }

    int freq = _parse_int(first, &ok);
    if (!ok || freq <= 0) {
        wb_emit(io, "pwm: frequency must be a positive integer\n");
        return -1;
    }
    int duty = _parse_int(second, &ok);
    if (!ok || duty < 0 || duty > 100) {
        wb_emit(io, "pwm: duty must be 0-100\n");
        return -1;
    }
    if (!g_hw->pwm_start || !g_hw->pwm_start(pin, (uint32_t)freq, (uint8_t)duty)) {
        wb_emitf(io, "could not start PWM on GP%d\n", pin);
        return -1;
    }
    wb_emitf(io, "PWM GP%d %d Hz at %d%%\n", pin, freq, duty);
    return 0;
}

/* ------------------------------------------------------------------ */
/* i2cdetect                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int bus;
} _i2c_probe_ctx_t;

static bool _i2c_probe_bridge(void *ctx, uint8_t addr) {
    _i2c_probe_ctx_t *probe = (_i2c_probe_ctx_t *)ctx;
    if (!probe || !g_hw || !g_hw->i2c_probe || probe->bus < 0) return false;
    return g_hw->i2c_probe(probe->bus, addr);
}

int wb_i2cdetect(const wb_io_t *io, const char *args) {
    if (!io) return -1;
    int bus = 0;
    uint8_t first = I2C_SCAN_FIRST;
    uint8_t last = I2C_SCAN_LAST;

    char buffer[96];
    (void)snprintf(buffer, sizeof buffer, "%s", args ? args : "");
    char *cursor = buffer;
    char token[16];
    while (_next_arg(&cursor, token, sizeof token)) {
        if (_ci_eq(token, "--bus")) {
            char value[8];
            if (!_next_arg(&cursor, value, sizeof value)) break;
            bool ok = false;
            int parsed = _parse_int(value, &ok);
            if (ok && parsed >= 0 && parsed <= 1) bus = parsed;
        } else {
            char *dash = strchr(token, '-');
            bool ok = false;
            if (dash) {
                *dash = '\0';
                int lo = _parse_int(token, &ok);
                bool ok2 = false;
                int hi = _parse_int(dash + 1, &ok2);
                if (ok && ok2 && lo >= 0 && hi <= 0x7F && lo <= hi) {
                    first = (uint8_t)lo;
                    last = (uint8_t)hi;
                }
            } else {
                int single = _parse_int(token, &ok);
                if (ok && single >= 0 && single <= 0x7F) {
                    first = (uint8_t)single;
                    last = (uint8_t)single;
                }
            }
        }
    }

    if (!_hardware_ready(io)) return -1;
    if (!g_hw->i2c_probe) {
        wb_emit(io, "I2C is not available on this build\n");
        return -1;
    }

    if (!g_hw->i2c_open) {
        wb_emit(io, "I2C is not available on this build\n");
        return -1;
    }
    pinmap_placement_t place;
    if (!pinmap_i2c(bus, &place)) {
        wb_emitf(io, "no free pins for i2c%d\n", bus);
        return -1;
    }
    if (!g_hw->i2c_open(bus, 100000, place.sda, place.scl)) {
        wb_emitf(io, "could not open i2c%d\n", bus);
        return -1;
    }

    i2c_scan_t scan;
    i2c_scan_reset(&scan, first, last);
    _i2c_probe_ctx_t ctx = {.bus = bus};
    int found = i2c_scan_run(&scan, _i2c_probe_bridge, &ctx);
    if (g_hw->i2c_close) g_hw->i2c_close(bus);

    wb_emitf(io, "i2cdetect on i2c%d (SDA GP%d, SCL GP%d)\n", bus, place.sda, place.scl);
    wb_emit(io, i2c_scan_header());
    wb_emit(io, "\n");
    char row[80];
    for (int index = 0; index < 8; index++) {
        if (i2c_scan_format_row(&scan, index, row, sizeof row) < 0) continue;
        wb_emitf(io, "%s\n", row);
    }
    if (found < 0) {
        wb_emit(io, "scan failed\n");
        return -1;
    }
    wb_emitf(io, "%d device(s) found in 0x%02x-0x%02x\n", found, scan.first, scan.last);
    return found;
}
