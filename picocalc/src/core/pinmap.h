#pragma once
/*
 * core/pinmap.h — PicoCalc GPIO ownership and peripheral placement.
 *
 * Single source of truth for which RP2040/RP2350 pins are already used by the
 * board, which are free, and where a new UART/I2C/SPI instance can be placed
 * without stealing a reserved function. Pure data plus queries so the same
 * answers are unit-tested on the host and shown by the `hw` command.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PINMAP_PIN_COUNT 30 /* GP0..GP29 on the PicoCalc */

typedef enum {
    PIN_ROLE_FREE = 0,
    PIN_ROLE_LCD_SPI,   /* ILI9488 on SPI1 */
    PIN_ROLE_SD_SPI,    /* microSD on SPI0 */
    PIN_ROLE_KBD_I2C,   /* STM32 co-processor on I2C1 */
    PIN_ROLE_AUDIO_PWM, /* left/right PWM channels */
    PIN_ROLE_BOARD,     /* power-stable / radio enable */
    PIN_ROLE_CYW43,     /* CYW43439 radio bus (Pico W only) */
    PIN_ROLE_CONSOLE,   /* UART0 stdio; reclaimable but loses the console */
    PIN_ROLE_COUNT,
} pin_role_t;

typedef struct {
    bool wlan; /* board carries a CYW43439 */
} pinmap_board_t;

/* Select the board variant. Without a radio the CYW43 pins are free. */
void pinmap_select_wlan(bool wlan);
bool pinmap_has_wlan(void);

pin_role_t pinmap_role(int pin);
const char *pinmap_role_name(pin_role_t role);
bool pinmap_is_free(int pin);
bool pinmap_is_reserved(int pin);
bool pinmap_is_console(int pin);
int pinmap_free_count(void);

/* Peripheral placement result: only the fields relevant to the interface
   are set. `uses_console` means the placement borrows the stdio UART pins. */
typedef struct {
    const char *label;
    int tx, rx;              /* UART */
    int sda, scl;            /* I2C */
    int sck, mosi, miso, cs; /* SPI */
    bool uses_console;
} pinmap_placement_t;

/* Return false when the interface cannot be placed without a conflict. */
bool pinmap_uart(int index, pinmap_placement_t *out);
bool pinmap_i2c(int index, pinmap_placement_t *out);
bool pinmap_spi(int index, pinmap_placement_t *out);

/* Short human-readable note for an interface placement, e.g. "GP4/GP5". */
const char *pinmap_serial_port_name(int index);

#ifdef __cplusplus
}
#endif
