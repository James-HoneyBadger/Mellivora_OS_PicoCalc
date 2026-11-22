/*
 * mock_peripherals.c — register-level hardware mock.
 */

#include "mock_peripherals.h"

#include <string.h>

hw_ops_t *g_hw = NULL;

static mock_hw_t *_mock = NULL;

/* ---- GPIO ---- */

static bool _gpio_claim(int pin, bool output) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return false;
    _mock->claimed |= (uint32_t)1u << (unsigned)pin;
    if (output) _mock->oe |= (uint32_t)1u << (unsigned)pin;
    else
        _mock->oe &= ~((uint32_t)1u << (unsigned)pin);
    return true;
}

static void _gpio_release(int pin) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return;
    _mock->claimed &= ~((uint32_t)1u << (unsigned)pin);
    _mock->oe &= ~((uint32_t)1u << (unsigned)pin);
    _mock->pull_up &= ~((uint32_t)1u << (unsigned)pin);
    _mock->pull_down &= ~((uint32_t)1u << (unsigned)pin);
}

static void _gpio_put(int pin, bool value) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return;
    _mock->gpio_put_calls++;
    if (value) _mock->out |= (uint32_t)1u << (unsigned)pin;
    else
        _mock->out &= ~((uint32_t)1u << (unsigned)pin);
    /* An output pin drives its own input latch. */
    if (_mock->oe & ((uint32_t)1u << (unsigned)pin)) {
        if (value) _mock->in |= (uint32_t)1u << (unsigned)pin;
        else
            _mock->in &= ~((uint32_t)1u << (unsigned)pin);
    }
}

static bool _gpio_get(int pin) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return false;
    return (_mock->in & ((uint32_t)1u << (unsigned)pin)) != 0;
}

static bool _gpio_pull(int pin, int mode) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return false;
    uint32_t mask = (uint32_t)1u << (unsigned)pin;
    _mock->pull_up &= ~mask;
    _mock->pull_down &= ~mask;
    if (mode == 1) _mock->pull_up |= mask;
    if (mode == 2) _mock->pull_down |= mask;
    return true;
}

/* ---- PWM ---- */

static bool _pwm_start(int pin, uint32_t freq_hz, uint8_t duty) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS || freq_hz == 0 || duty > 100) return false;
    _mock->pwm_start_calls++;
    _mock->pwm_running[pin] = true;
    _mock->pwm_freq[pin] = freq_hz;
    _mock->pwm_duty[pin] = duty;
    return true;
}

static bool _pwm_duty(int pin, uint8_t duty) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS || !_mock->pwm_running[pin] || duty > 100)
        return false;
    _mock->pwm_duty[pin] = duty;
    return true;
}

static void _pwm_stop(int pin) {
    if (!_mock || pin < 0 || pin >= MOCK_PINS) return;
    _mock->pwm_stop_calls++;
    _mock->pwm_running[pin] = false;
    _mock->pwm_duty[pin] = 0;
}

/* ---- UART ---- */

static bool _uart_open(int index, uint32_t baud, int tx, int rx) {
    if (!_mock || index < 0 || index > 1 || baud == 0) return false;
    _mock->uart_open_calls++;
    _mock->uart_open[index] = true;
    _mock->uart_baud[index] = baud;
    _mock->uart_tx[index] = tx;
    _mock->uart_rx[index] = rx;
    return true;
}

static void _uart_close(int index) {
    if (!_mock || index < 0 || index > 1) return;
    _mock->uart_open[index] = false;
}

static bool _uart_set_baud(int index, uint32_t baud) {
    if (!_mock || index < 0 || index > 1 || !_mock->uart_open[index] || baud == 0) return false;
    _mock->uart_baud[index] = baud;
    return true;
}

static uint32_t _uart_available(int index) {
    if (!_mock || index < 0 || index > 1) return 0;
    return _mock->uart_head[index] - _mock->uart_tail[index];
}

static int _uart_read(int index, uint8_t *buf, uint32_t cap) {
    if (!_mock || index < 0 || index > 1 || !buf) return -1;
    uint32_t count = 0;
    while (count < cap && _mock->uart_tail[index] != _mock->uart_head[index]) {
        buf[count++] = _mock->uart_fifo[index][_mock->uart_tail[index] % MOCK_UART_FIFO];
        _mock->uart_tail[index]++;
    }
    return (int)count;
}

static int _uart_write(int index, const uint8_t *buf, uint32_t len) {
    if (!_mock || index < 0 || index > 1 || !buf) return -1;
    for (uint32_t i = 0; i < len && _mock->uart_tx_len < sizeof _mock->uart_tx_log; i++)
        _mock->uart_tx_log[_mock->uart_tx_len++] = buf[i];
    return (int)len;
}

/* ---- I2C ---- */

static bool _i2c_open(int index, uint32_t speed, int sda, int scl) {
    if (!_mock || index < 0 || index > 1) return false;
    _mock->i2c_open[index] = true;
    _mock->i2c_speed[index] = speed;
    _mock->i2c_sda[index] = sda;
    _mock->i2c_scl[index] = scl;
    return true;
}

static void _i2c_close(int index) {
    if (!_mock || index < 0 || index > 1) return;
    _mock->i2c_open[index] = false;
}

static bool _i2c_probe(int index, uint8_t addr) {
    if (!_mock || index < 0 || index > 1 || addr > 0x7F) return false;
    _mock->i2c_probe_calls++;
    return _mock->i2c_present[index][addr];
}

static int _i2c_write(int index, uint8_t addr, const uint8_t *buf, uint32_t len) {
    if (!_mock || index < 0 || index > 1 || !buf) return -1;
    if (!_mock->i2c_present[index][addr]) return -1;
    uint32_t copy = len < sizeof _mock->i2c_last_write[0] ? len : sizeof _mock->i2c_last_write[0];
    memcpy(_mock->i2c_last_write[index], buf, copy);
    _mock->i2c_last_write_len[index] = copy;
    return (int)len;
}

static int _i2c_read(int index, uint8_t addr, uint8_t *buf, uint32_t len) {
    if (!_mock || index < 0 || index > 1 || !buf) return -1;
    if (!_mock->i2c_present[index][addr]) return -1;
    memset(buf, 0, len);
    return (int)len;
}

/* ---- CDC ---- */

static int _cdc_read(uint8_t *buf, uint32_t cap) {
    if (!_mock || !buf) return -1;
    uint32_t count = 0;
    while (count < cap && _mock->cdc_tail != _mock->cdc_head) {
        buf[count++] = _mock->cdc_fifo[_mock->cdc_tail % MOCK_CDC_FIFO];
        _mock->cdc_tail++;
    }
    return (int)count;
}

static int _cdc_write(const uint8_t *buf, uint32_t len) {
    if (!_mock || !buf) return -1;
    for (uint32_t i = 0; i < len && _mock->cdc_tx_len < sizeof _mock->cdc_tx; i++)
        _mock->cdc_tx[_mock->cdc_tx_len++] = buf[i];
    return (int)len;
}

static uint32_t _cdc_available(void) {
    if (!_mock) return 0;
    return _mock->cdc_head - _mock->cdc_tail;
}

static hw_ops_t _ops = {
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

void mock_hw_reset(mock_hw_t *mock) {
    if (!mock) return;
    memset(mock, 0, sizeof *mock);
    _mock = mock;
}

void mock_hw_install(mock_hw_t *mock) {
    mock_hw_reset(mock);
    g_hw = &_ops;
}

void mock_uart_feed(int index, const uint8_t *data, uint32_t len) {
    if (!_mock || index < 0 || index > 1 || !data) return;
    for (uint32_t i = 0; i < len; i++) {
        if (_mock->uart_head[index] - _mock->uart_tail[index] >= MOCK_UART_FIFO) break;
        _mock->uart_fifo[index][_mock->uart_head[index] % MOCK_UART_FIFO] = data[i];
        _mock->uart_head[index]++;
    }
}

void mock_cdc_feed(mock_hw_t *mock, const uint8_t *data, uint32_t len) {
    if (!mock || !data) return;
    for (uint32_t i = 0; i < len; i++) {
        if (mock->cdc_head - mock->cdc_tail >= MOCK_CDC_FIFO) break;
        mock->cdc_fifo[mock->cdc_head % MOCK_CDC_FIFO] = data[i];
        mock->cdc_head++;
    }
}

uint32_t mock_cdc_take_tx(mock_hw_t *mock, uint8_t *out, uint32_t cap) {
    if (!mock || !out) return 0;
    uint32_t count = mock->cdc_tx_len < cap ? mock->cdc_tx_len : cap;
    memcpy(out, mock->cdc_tx, count);
    mock->cdc_tx_len = 0;
    return count;
}

void mock_i2c_add(mock_hw_t *mock, int bus, uint8_t addr) {
    if (!mock || bus < 0 || bus > 1 || addr > 0x7F) return;
    mock->i2c_present[bus][addr] = true;
}
