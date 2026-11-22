#pragma once
/*
 * mock_peripherals.h — simulated RP2040 peripheral registers for host tests.
 *
 * The workbench talks to hardware only through hw_ops_t, so installing this
 * table exercises the real command paths against memory-backed GPIO, PWM,
 * UART, I2C, and CDC "registers" instead of silicon.
 */

#include <stdbool.h>
#include <stdint.h>

#include "hal/hw_ops.h"

#define MOCK_PINS 30
#define MOCK_UART_FIFO 64
#define MOCK_CDC_FIFO 256

typedef struct {
    /* GPIO register block */
    uint32_t out; /* output latch */
    uint32_t oe;  /* output enable */
    uint32_t in;  /* input levels */
    uint32_t pull_up;
    uint32_t pull_down;
    uint32_t claimed;

    /* PWM slices */
    bool pwm_running[MOCK_PINS];
    uint32_t pwm_freq[MOCK_PINS];
    uint8_t pwm_duty[MOCK_PINS];

    /* UART FIFOs and configuration */
    bool uart_open[2];
    uint32_t uart_baud[2];
    int uart_tx[2];
    int uart_rx[2];
    uint8_t uart_fifo[2][MOCK_UART_FIFO];
    uint32_t uart_head[2];
    uint32_t uart_tail[2];
    uint8_t uart_tx_log[256];
    uint32_t uart_tx_len;

    /* I2C bus state */
    bool i2c_open[2];
    uint32_t i2c_speed[2];
    int i2c_sda[2];
    int i2c_scl[2];
    bool i2c_present[2][128];
    uint8_t i2c_last_write[2][8];
    uint32_t i2c_last_write_len[2];

    /* USB CDC byte stream */
    uint8_t cdc_fifo[MOCK_CDC_FIFO];
    uint32_t cdc_head;
    uint32_t cdc_tail;
    uint8_t cdc_tx[256];
    uint32_t cdc_tx_len;

    /* Call counters */
    int gpio_put_calls;
    int pwm_start_calls;
    int pwm_stop_calls;
    int i2c_probe_calls;
    int uart_open_calls;
} mock_hw_t;

void mock_hw_install(mock_hw_t *mock);
void mock_hw_reset(mock_hw_t *mock);

void mock_uart_feed(int index, const uint8_t *data, uint32_t len);
void mock_cdc_feed(mock_hw_t *mock, const uint8_t *data, uint32_t len);
uint32_t mock_cdc_take_tx(mock_hw_t *mock, uint8_t *out, uint32_t cap);
void mock_i2c_add(mock_hw_t *mock, int bus, uint8_t addr);
