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
    if (ret < 0) return -1;

    /* The co-processor needs ~16 ms to prepare the response. */
    sleep_ms(16);

    uint16_t buf = 0;
    ret = i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&buf, 2, false, 50000);
    if (ret < 0) return -1;
    if (buf == 0) return -1;

    /* Ctrl key state transitions */
    if (buf == KBD_CTRL_RELEASE) { _ctrl_held = 0; return -1; }
    if (buf == KBD_CTRL_HELD)    { _ctrl_held = 1; return -1; }

    /* Low byte is status, high byte is keycode */
    uint8_t status  = (uint8_t)(buf & 0xFF);
    uint8_t keycode = (uint8_t)(buf >> 8);

    if (status != KBD_STAT_PRESSED) return -1;

    /* Map Ctrl+letter to control character 1-26 */
    if (_ctrl_held && keycode >= 'a' && keycode <= 'z') {
        return keycode - 'a' + 1;
    }
    return (int)keycode;
}

void kbd_set_backlight(uint8_t level) {
    if (!_inited) return;
    uint8_t msg[2] = { KBD_REG_BKLT, level };
    i2c_write_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, msg, 2, false, 50000);
    sleep_ms(16);
    /* Drain the 2-byte reply from the controller */
    uint16_t dummy = 0;
    i2c_read_timeout_us(KBD_I2C_PORT, KBD_I2C_ADDR, (uint8_t *)&dummy, 2, false, 50000);
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
    pct &= 0x7F; /* strip charging bit */
    return (int)pct;
}
