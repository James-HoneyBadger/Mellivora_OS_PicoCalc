/*
 * kbd.c — I2C keyboard driver for Clockwork PicoCalc
 *
 * The STM32 co-processor is at I2C address 0x1F on i2c1 (GPIO 6/7).
 * Protocol derived from the Clockwork reference driver (i2ckbd).
 */

#include "kbd.h"
#include "picocalc_hw.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include <stdio.h>

static int _inited = 0;
static int _ctrl_held = 0;
static bool _last_charging = false;

/* Key repeat tracking */
static int      _last_key = -1;
static uint32_t _last_press_ms = 0;
static uint32_t _last_repeat_ms = 0;
#define KEY_REPEAT_DELAY_MS   500
#define KEY_REPEAT_RATE_MS    100

/* Recover stuck I2C bus by pulsing SCL 9 times */
static void _kbd_i2c_recover(void) {
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_SIO);
    gpio_set_dir(KBD_PIN_SCL, GPIO_OUT);
    for (int i = 0; i < 9; i++) {
        gpio_put(KBD_PIN_SCL, 0);
        busy_wait_us_32(5);
        gpio_put(KBD_PIN_SCL, 1);
        busy_wait_us_32(5);
    }
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_PIN_SCL);
}

static void _kbd_write_reg(uint8_t reg, uint8_t value) {
    if (!_inited) return;
    uint8_t msg[2] = { reg, value };
    i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, msg, 2, false, 50000);
    sleep_ms(16);
    uint16_t dummy = 0;
    i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&dummy, 2, false, 50000);
}

void kbd_init(void) {
    i2c_init(KBD_I2C_PORT, KBD_I2C_SPEED);
    gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_PIN_SDA);
    gpio_pull_up(KBD_PIN_SCL);
    _inited = 1;
}

int kbd_getc(void) {
    if (!_inited) return -1;

    uint8_t reg = KBD_REG_KEY;
    int ret = i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, &reg, 1, false, 50000);
    if (ret < 0) { _kbd_i2c_recover(); return -1; }

    /* The co-processor needs ~16 ms to prepare the response. */
    sleep_ms(16);

    uint16_t buf = 0;
    ret = i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&buf, 2, false, 50000);
    if (ret < 0) { _kbd_i2c_recover(); return -1; }
    if (buf == 0) return -1;

    /* Ctrl key state transitions */
    if (buf == KBD_CTRL_RELEASE) { _ctrl_held = 0; return -1; }
    if (buf == KBD_CTRL_HELD)    { _ctrl_held = 1; return -1; }

    /* Low byte is status, high byte is keycode */
    uint8_t status  = (uint8_t)(buf & 0xFF);
    uint8_t keycode = (uint8_t)(buf >> 8);

    if (status != KBD_STAT_PRESSED) return -1;

    /* Validate keycode is in printable ASCII or known control range */
    if (keycode == 0 || keycode > 0x7E) return -1;

    /* Map Ctrl+letter to control character 1-26 */
    if (_ctrl_held && keycode >= 'a' && keycode <= 'z') {
        _last_key = -1; /* don't repeat control chars */
        return keycode - 'a' + 1;
    }

    /* Track for key repeat */
    uint32_t now = to_ms_since_boot(get_absolute_time());
    _last_key = (int)keycode;
    _last_press_ms = now;
    _last_repeat_ms = 0;
    return (int)keycode;
}

int kbd_get_repeat(void) {
    if (_last_key < 0) return -1;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed = now - _last_press_ms;
    if (elapsed < KEY_REPEAT_DELAY_MS) return -1;
    if (_last_repeat_ms == 0 || (now - _last_repeat_ms) >= KEY_REPEAT_RATE_MS) {
        _last_repeat_ms = now;
        return _last_key;
    }
    return -1;
}

void kbd_clear_repeat(void) {
    _last_key = -1;
}

void kbd_set_backlight(uint8_t level) {
    if (!_inited) return;

    /* PicoCalc exposes two distinct backlights over I2C:
       0x85 -> LCD panel backlight, 0x8A -> keyboard backlight. */
    uint8_t lcd_level = level;
    if (lcd_level > 240) lcd_level = 240;
    if (lcd_level > 0 && lcd_level < 16) lcd_level = 16;

    _kbd_write_reg(KBD_REG_LCD_BKLT, lcd_level);
    _kbd_write_reg(KBD_REG_KBD_BKLT, level);
}

int kbd_battery_percent(void) {
    if (!_inited) return -1;

    uint8_t reg = KBD_REG_BAT;
    int ret = i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, &reg, 1, false, 50000);
    if (ret < 0) return -1;

    sleep_ms(16);

    uint16_t buf = 0;
    ret = i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&buf, 2, false, 50000);
    if (ret < 0) return -1;

    /* High byte holds percentage; bit 7 = charging flag */
    uint8_t pct = (uint8_t)(buf >> 8);
    _last_charging = (pct & 0x80) != 0;
    pct &= 0x7F; /* strip charging bit */
    if (pct > 100) pct = 100;
    return (int)pct;
}

bool kbd_is_charging(void) {
    return _last_charging;
}
