/*
 * kbd.c — I2C keyboard driver for Clockwork PicoCalc
 *
 * The STM32 co-processor is at I2C address 0x1F on i2c1 (GPIO 6/7).
 * Protocol derived from the Clockwork reference driver (i2ckbd).
 */

#include "kbd.h"
#include "picocalc_hw.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include <stdio.h>

static int _inited = 0;
static int _ctrl_held = 0;
static bool _last_charging = false;

/* Protect I2C1 access: core0 (shell/apps) and core1 (status bar) both
 * touch the keyboard controller on RP2350. */
static mutex_t _kbd_i2c_mtx;

/* Key repeat tracking */
static int      _last_key = -1;
static uint32_t _last_press_ms = 0;
static uint32_t _last_repeat_ms = 0;
static bool     _key_consumed = false;  /* true after kbd_getc fires once for a held key */
#define KEY_REPEAT_DELAY_MS   500
#define KEY_REPEAT_RATE_MS    100

/* Recover stuck I2C bus.  De-init the peripheral, bit-bang SCL until SDA
 * is released, issue a STOP, then re-init the controller. */
static void _kbd_i2c_recover(void) {
    i2c_deinit(KBD_I2C_PORT);

    gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_SIO);
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_SIO);
    gpio_set_dir(KBD_PIN_SDA, GPIO_IN);
    gpio_set_dir(KBD_PIN_SCL, GPIO_OUT);
    gpio_pull_up(KBD_PIN_SDA);
    gpio_pull_up(KBD_PIN_SCL);

    /* Clock SCL until the slave releases SDA. */
    for (int i = 0; i < 9; i++) {
        gpio_put(KBD_PIN_SCL, 0);
        busy_wait_us_32(5);
        gpio_put(KBD_PIN_SCL, 1);
        busy_wait_us_32(5);
        if (gpio_get(KBD_PIN_SDA)) break;
    }

    /* If SDA is free, generate a STOP condition. */
    if (gpio_get(KBD_PIN_SDA)) {
        gpio_set_dir(KBD_PIN_SDA, GPIO_OUT);
        gpio_put(KBD_PIN_SCL, 0);
        busy_wait_us_32(5);
        gpio_put(KBD_PIN_SDA, 0);
        busy_wait_us_32(5);
        gpio_put(KBD_PIN_SCL, 1);
        busy_wait_us_32(5);
        gpio_put(KBD_PIN_SDA, 1);
        busy_wait_us_32(5);
    }

    i2c_init(KBD_I2C_PORT, KBD_I2C_SPEED);
    gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_PIN_SDA);
    gpio_pull_up(KBD_PIN_SCL);
}

static void _kbd_write_reg(uint8_t reg, uint8_t value) {
    if (!_inited) return;
    uint8_t msg[2] = { reg, value };
    i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, msg, 2, false, 500000);
}

void kbd_init(void) {
    mutex_init(&_kbd_i2c_mtx);
    i2c_init(KBD_I2C_PORT, KBD_I2C_SPEED);
    gpio_set_function(KBD_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KBD_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_PIN_SDA);
    gpio_pull_up(KBD_PIN_SCL);
    _inited = 1;
}

int kbd_getc(void) {
    int result = -1;
    if (!_inited) return -1;

    mutex_enter_blocking(&_kbd_i2c_mtx);

    uint8_t reg = KBD_REG_KEY;
    int ret = i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, &reg, 1, false, 500000);
    if (ret < 0) { _kbd_i2c_recover(); goto kbd_getc_done; }

    /* The co-processor needs ~16 ms to prepare the response. */
    sleep_ms(16);

    uint16_t buf = 0;
    ret = i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&buf, 2, false, 500000);
    if (ret < 0) { _kbd_i2c_recover(); goto kbd_getc_done; }
    if (buf == 0) goto kbd_getc_done;

    /* Ctrl key state transitions */
    if (buf == KBD_CTRL_RELEASE) { _ctrl_held = 0; goto kbd_getc_done; }
    if (buf == KBD_CTRL_HELD)    { _ctrl_held = 1; goto kbd_getc_done; }

    /* Low byte is status, high byte is keycode */
    uint8_t status  = (uint8_t)(buf & 0xFF);
    uint8_t keycode = (uint8_t)(buf >> 8);

    if (status != KBD_STAT_PRESSED) {
        /* Key released — reset edge-detect state */
        if (_last_key >= 0) {
            _last_key = -1;
            _key_consumed = false;
        }
        goto kbd_getc_done;
    }

    /* Validate keycode is in printable ASCII or known control range */
    if (keycode == 0 || keycode > 0x7E) goto kbd_getc_done;

    /* Map Ctrl+letter to control character 1-26 */
    if (_ctrl_held && keycode >= 'a' && keycode <= 'z') {
        _last_key = -1; /* don't repeat control chars */
        _key_consumed = false;
        result = keycode - 'a' + 1;
        goto kbd_getc_done;
    }

    /* Edge-detect: only fire once per physical press; repeat is via kbd_get_repeat() */
    if ((int)keycode == _last_key && _key_consumed) goto kbd_getc_done;

    /* Track for key repeat */
    uint32_t now = to_ms_since_boot(get_absolute_time());
    _last_key = (int)keycode;
    _last_press_ms = now;
    _last_repeat_ms = 0;
    _key_consumed = true;
    result = (int)keycode;

kbd_getc_done:
    mutex_exit(&_kbd_i2c_mtx);
    return result;
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
    _key_consumed = false;
}

void kbd_set_backlight(uint8_t level) {
    if (!_inited) return;

    /* PicoCalc exposes two distinct backlights over I2C:
       0x85 -> LCD panel backlight, 0x8A -> keyboard backlight. */
    uint8_t lcd_level = level;
    if (lcd_level > 240) lcd_level = 240;
    if (lcd_level > 0 && lcd_level < 16) lcd_level = 16;

    mutex_enter_blocking(&_kbd_i2c_mtx);
    _kbd_write_reg(KBD_REG_LCD_BKLT, lcd_level);
    _kbd_write_reg(KBD_REG_KBD_BKLT, level);
    mutex_exit(&_kbd_i2c_mtx);
}

int kbd_battery_percent(void) {
    int result = -1;
    if (!_inited) return -1;

    mutex_enter_blocking(&_kbd_i2c_mtx);

    uint8_t reg = KBD_REG_BAT;
    int ret = i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, &reg, 1, false, 500000);
    if (ret < 0) { _kbd_i2c_recover(); goto kbd_bat_done; }

    sleep_ms(16);

    uint16_t buf = 0;
    ret = i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&buf, 2, false, 500000);
    if (ret < 0) { _kbd_i2c_recover(); goto kbd_bat_done; }

    /* High byte holds percentage; bit 7 = charging flag */
    uint8_t pct = (uint8_t)(buf >> 8);
    _last_charging = (pct & 0x80) != 0;
    pct &= 0x7F; /* strip charging bit */
    if (pct > 100) pct = 100;
    result = (int)pct;

kbd_bat_done:
    mutex_exit(&_kbd_i2c_mtx);
    return result;
}

bool kbd_is_charging(void) {
    return _last_charging;
}
