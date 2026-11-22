/*
 * hal/hw_pico.c — RP2040/RP2350 implementation of the workbench operations.
 *
 * Only this file touches the Pico SDK for UART/I2C/GPIO/PWM/CDC. The command
 * logic above it stays portable and host-testable.
 */

#include "hal/hw_ops.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "picocalc_hw.h"

static uart_inst_t *_uart(int index) { return index == 0 ? uart0 : uart1; }

static i2c_inst_t *_i2c(int index) { return index == 0 ? i2c0 : i2c1; }

/* ------------------------------------------------------------------ */
/* UART                                                                */
/* ------------------------------------------------------------------ */

static bool _uart_open(int index, uint32_t baud, int tx, int rx) {
    if (index < 0 || index > 1 || baud == 0) return false;
    uart_inst_t *inst = _uart(index);

    gpio_set_function((uint)tx, GPIO_FUNC_UART);
    gpio_set_function((uint)rx, GPIO_FUNC_UART);
    uart_init(inst, baud);
    uart_set_format(inst, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(inst, true);
    return true;
}

static void _uart_close(int index) {
    if (index < 0 || index > 1) return;
    if (index == 0) return; /* UART0 carries the console; leave it running */
    uart_deinit(uart1);
}

static bool _uart_set_baud(int index, uint32_t baud) {
    if (index < 0 || index > 1 || baud == 0) return false;
    uart_set_baudrate(_uart(index), baud);
    return true;
}

static int _uart_read(int index, uint8_t *buf, uint32_t cap) {
    if (index < 0 || index > 1 || !buf) return -1;
    uart_inst_t *inst = _uart(index);
    uint32_t count = 0;
    while (count < cap && uart_is_readable(inst)) buf[count++] = (uint8_t)uart_getc(inst);
    return (int)count;
}

static int _uart_write(int index, const uint8_t *buf, uint32_t len) {
    if (index < 0 || index > 1 || (!buf && len)) return -1;
    uart_write_blocking(_uart(index), buf, len);
    return (int)len;
}

static uint32_t _uart_available(int index) {
    if (index < 0 || index > 1) return 0;
    return uart_is_readable(_uart(index)) ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* I2C                                                                 */
/* ------------------------------------------------------------------ */

static bool _i2c_open(int index, uint32_t speed_hz, int sda, int scl) {
    if (index < 0 || index > 1 || speed_hz == 0) return false;
    i2c_inst_t *inst = _i2c(index);
    gpio_set_function((uint)sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)sda);
    gpio_pull_up((uint)scl);
    i2c_init(inst, speed_hz);
    return true;
}

static void _i2c_close(int index) {
    if (index < 0 || index > 1) return;
    /* I2C1 belongs to the keyboard co-processor; put its bus back exactly as
       the driver expects so the keyboard keeps working after a scan. */
    if (index == 1) {
        gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_I2C);
        gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(KBD_PIN_SDA);
        gpio_pull_up(KBD_PIN_SCL);
        i2c_init(i2c1, KBD_I2C_SPEED);
        return;
    }
    i2c_deinit(i2c0);
}

static bool _i2c_probe(int index, uint8_t addr) {
    if (index < 0 || index > 1 || addr > 0x7F) return false;
    /* A zero-length write returns 0 when the address is acknowledged. */
    return i2c_write_blocking(_i2c(index), addr, NULL, 0, false) >= 0;
}

static int _i2c_write(int index, uint8_t addr, const uint8_t *buf, uint32_t len) {
    if (index < 0 || index > 1 || (!buf && len)) return -1;
    return i2c_write_blocking(_i2c(index), addr, buf, (size_t)len, false);
}

static int _i2c_read(int index, uint8_t addr, uint8_t *buf, uint32_t len) {
    if (index < 0 || index > 1 || !buf) return -1;
    return i2c_read_blocking(_i2c(index), addr, buf, (size_t)len, false);
}

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */

static bool _gpio_claim(int pin, bool output) {
    if (pin < 0 || pin >= 30) return false;
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, output ? GPIO_OUT : GPIO_IN);
    return true;
}

static void _gpio_release(int pin) {
    if (pin < 0 || pin >= 30) return;
    gpio_disable_pulls((uint)pin);
    gpio_set_dir((uint)pin, GPIO_IN);
}

static void _gpio_put(int pin, bool value) {
    if (pin < 0 || pin >= 30) return;
    gpio_put((uint)pin, value);
}

static bool _gpio_get(int pin) {
    if (pin < 0 || pin >= 30) return false;
    return gpio_get((uint)pin);
}

static bool _gpio_pull(int pin, int mode) {
    if (pin < 0 || pin >= 30) return false;
    if (mode == 1) gpio_pull_up((uint)pin);
    else if (mode == 2) gpio_pull_down((uint)pin);
    else gpio_disable_pulls((uint)pin);
    return true;
}

/* ------------------------------------------------------------------ */
/* PWM                                                                 */
/* ------------------------------------------------------------------ */

static bool _pwm_active[30];
static uint32_t _pwm_wrap[30];

static bool _pwm_start(int pin, uint32_t freq_hz, uint8_t duty_percent) {
    if (pin < 0 || pin >= 30 || freq_hz == 0 || freq_hz > 500000u || duty_percent > 100)
        return false;

    uint32_t clk = clock_get_hz(clk_sys);
    uint64_t target = (uint64_t)freq_hz * 65536u;
    uint32_t div16 = (uint32_t)(((uint64_t)clk + target - 1u) / target);
    if (div16 == 0) div16 = 1;
    if (div16 > 255) return false;

    uint32_t wrap = (uint32_t)(clk / ((uint64_t)freq_hz * div16));
    if (wrap == 0) wrap = 1;
    if (wrap > 65536u) wrap = 65536u;

    uint slice = pwm_gpio_to_slice_num((uint)pin);
    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, (uint8_t)div16, 0);
    pwm_set_wrap(slice, (uint16_t)(wrap - 1u));
    _pwm_wrap[pin] = wrap;
    pwm_set_gpio_level((uint)pin, (uint16_t)((wrap * (uint32_t)duty_percent) / 100u));
    pwm_set_enabled(slice, true);
    _pwm_active[pin] = true;
    return true;
}

static bool _pwm_duty(int pin, uint8_t duty_percent) {
    if (pin < 0 || pin >= 30 || duty_percent > 100 || !_pwm_active[pin]) return false;
    uint32_t wrap = _pwm_wrap[pin];
    pwm_set_gpio_level((uint)pin, (uint16_t)((wrap * (uint32_t)duty_percent) / 100u));
    return true;
}

static void _pwm_stop(int pin) {
    if (pin < 0 || pin >= 30) return;
    uint slice = pwm_gpio_to_slice_num((uint)pin);
    pwm_set_gpio_level((uint)pin, 0);
    pwm_set_enabled(slice, false);
    gpio_set_function((uint)pin, GPIO_FUNC_SIO);
    _pwm_active[pin] = false;
}

/* ------------------------------------------------------------------ */
/* USB CDC                                                             */
/* ------------------------------------------------------------------ */

static int _cdc_read(uint8_t *buf, uint32_t cap) {
    if (!buf) return -1;
    uint32_t count = 0;
    while (count < cap) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) break;
        buf[count++] = (uint8_t)ch;
    }
    return (int)count;
}

static int _cdc_write(const uint8_t *buf, uint32_t len) {
    if (!buf && len) return -1;
    for (uint32_t i = 0; i < len; i++) putchar_raw((char)buf[i]);
    return (int)len;
}

static uint32_t _cdc_available(void) {
    return stdio_usb_connected() ? 1u : 0u;
}

static hw_ops_t _hw = {
    .uart_open = _uart_open,
    .uart_close = _uart_close,
    .uart_set_baud = _uart_set_baud,
    .uart_read = _uart_read,
    .uart_write = _uart_write,
    .uart_available = _uart_available,
    .i2c_open = _i2c_open,
    .i2c_close = _i2c_close,
    .i2c_probe = _i2c_probe,
    .i2c_write = _i2c_write,
    .i2c_read = _i2c_read,
    .gpio_claim = _gpio_claim,
    .gpio_release = _gpio_release,
    .gpio_put = _gpio_put,
    .gpio_get = _gpio_get,
    .gpio_pull = _gpio_pull,
    .pwm_start = _pwm_start,
    .pwm_duty = _pwm_duty,
    .pwm_stop = _pwm_stop,
    .cdc_read = _cdc_read,
    .cdc_write = _cdc_write,
    .cdc_available = _cdc_available,
};

hw_ops_t *g_hw = NULL;

void hw_init(void) {
    if (!g_hw) g_hw = &_hw;
}
