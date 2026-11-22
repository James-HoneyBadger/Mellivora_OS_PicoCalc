#pragma once
/*
 * hal/hw_ops.h — mockable peripheral operations.
 *
 * Deliberately free of Pico SDK includes so the hardware workbench logic can
 * be linked into the host test binary with simulated registers behind it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* ---- UART (index 0 = uart0, 1 = uart1) ---- */
    bool (*uart_open)(int index, uint32_t baud, int tx_pin, int rx_pin);
    void (*uart_close)(int index);
    bool (*uart_set_baud)(int index, uint32_t baud);
    int (*uart_read)(int index, uint8_t *buf, uint32_t cap); /* 0 = none pending */
    int (*uart_write)(int index, const uint8_t *buf, uint32_t len);
    uint32_t (*uart_available)(int index);

    /* ---- I2C ---- */
    bool (*i2c_open)(int index, uint32_t speed_hz, int sda, int scl);
    void (*i2c_close)(int index);
    bool (*i2c_probe)(int index, uint8_t addr);
    int (*i2c_write)(int index, uint8_t addr, const uint8_t *buf, uint32_t len);
    int (*i2c_read)(int index, uint8_t addr, uint8_t *buf, uint32_t len);

    /* ---- GPIO ---- */
    bool (*gpio_claim)(int pin, bool output);
    void (*gpio_release)(int pin);
    void (*gpio_put)(int pin, bool value);
    bool (*gpio_get)(int pin);
    bool (*gpio_pull)(int pin, int mode); /* 0 none, 1 up, 2 down */

    /* ---- PWM ---- */
    bool (*pwm_start)(int pin, uint32_t freq_hz, uint8_t duty_percent);
    bool (*pwm_duty)(int pin, uint8_t duty_percent);
    void (*pwm_stop)(int pin);

    /* ---- USB CDC console byte stream ---- */
    int (*cdc_read)(uint8_t *buf, uint32_t cap);
    int (*cdc_write)(const uint8_t *buf, uint32_t len);
    uint32_t (*cdc_available)(void);
} hw_ops_t;

/* Installed by hal/hw_pico.c in the firmware and by the test mock on the host. */
extern hw_ops_t *g_hw;

/* Install the production implementation (no-op when already installed). */
void hw_init(void);

#ifdef __cplusplus
}
#endif
