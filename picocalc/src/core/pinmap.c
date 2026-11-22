/*
 * core/pinmap.c — GPIO ownership table for the Clockwork PicoCalc v2.0.
 *
 * Sources: docs/HARDWARE.md and picocalc/src/picocalc_hw.h.
 *
 *   GP6/GP7      I2C1  keyboard co-processor (10 kHz)
 *   GP10..GP15   SPI1  ILI9488 display
 *   GP16..GP19   SPI0  microSD (GP22 card detect)
 *   GP22         SD card detect
 *   GP23         board power-stable pin (also WL_ON on Pico W)
 *   GP26/GP27    PWM   audio left/right
 *   GP24/25/29   CYW43439 bus on Pico W only
 *   GP0/GP1      UART0 console (stdio)
 */

#include "core/pinmap.h"

#include <stddef.h>

static const pin_role_t _base_roles[PINMAP_PIN_COUNT] = {
    PIN_ROLE_CONSOLE,   /* 0  UART0 TX */
    PIN_ROLE_CONSOLE,   /* 1  UART0 RX */
    PIN_ROLE_FREE,      /* 2  I2C1 SDA candidate */
    PIN_ROLE_FREE,      /* 3  I2C1 SCL candidate */
    PIN_ROLE_FREE,      /* 4  UART1 TX */
    PIN_ROLE_FREE,      /* 5  UART1 RX */
    PIN_ROLE_KBD_I2C,   /* 6  keyboard SDA */
    PIN_ROLE_KBD_I2C,   /* 7  keyboard SCL */
    PIN_ROLE_FREE,      /* 8  I2C0 SDA candidate */
    PIN_ROLE_FREE,      /* 9  I2C0 SCL candidate */
    PIN_ROLE_LCD_SPI,   /* 10 LCD SCK */
    PIN_ROLE_LCD_SPI,   /* 11 LCD MOSI */
    PIN_ROLE_LCD_SPI,   /* 12 LCD MISO */
    PIN_ROLE_LCD_SPI,   /* 13 LCD CS */
    PIN_ROLE_LCD_SPI,   /* 14 LCD DC */
    PIN_ROLE_LCD_SPI,   /* 15 LCD RST */
    PIN_ROLE_SD_SPI,    /* 16 SD MISO */
    PIN_ROLE_SD_SPI,    /* 17 SD CS */
    PIN_ROLE_SD_SPI,    /* 18 SD SCK */
    PIN_ROLE_SD_SPI,    /* 19 SD MOSI */
    PIN_ROLE_FREE,      /* 20 I2C0 SDA candidate */
    PIN_ROLE_FREE,      /* 21 I2C0 SCL candidate */
    PIN_ROLE_SD_SPI,    /* 22 SD card detect */
    PIN_ROLE_BOARD,     /* 23 power stable / radio enable */
    PIN_ROLE_FREE,      /* 24 */
    PIN_ROLE_FREE,      /* 25 */
    PIN_ROLE_AUDIO_PWM, /* 26 audio left */
    PIN_ROLE_AUDIO_PWM, /* 27 audio right */
    PIN_ROLE_FREE,      /* 28 */
    PIN_ROLE_FREE,      /* 29 */
};

/* Pins the wireless chip needs; unavailable on Pico W builds. */
static const int _cyw43_pins[] = {24, 25, 29};

static bool _wlan = false;

void pinmap_select_wlan(bool wlan) {
    _wlan = wlan;
}

bool pinmap_has_wlan(void) {
    return _wlan;
}

pin_role_t pinmap_role(int pin) {
    if (pin < 0 || pin >= PINMAP_PIN_COUNT) return PIN_ROLE_FREE;
    if (_wlan) {
        for (size_t i = 0; i < sizeof _cyw43_pins / sizeof _cyw43_pins[0]; i++) {
            if (_cyw43_pins[i] == pin) return PIN_ROLE_CYW43;
        }
    }
    return _base_roles[pin];
}

const char *pinmap_role_name(pin_role_t role) {
    switch (role) {
    case PIN_ROLE_FREE:
        return "free";
    case PIN_ROLE_LCD_SPI:
        return "lcd-spi";
    case PIN_ROLE_SD_SPI:
        return "sd-spi";
    case PIN_ROLE_KBD_I2C:
        return "kbd-i2c";
    case PIN_ROLE_AUDIO_PWM:
        return "audio-pwm";
    case PIN_ROLE_BOARD:
        return "board";
    case PIN_ROLE_CYW43:
        return "cyw43";
    case PIN_ROLE_CONSOLE:
        return "console";
    default:
        return "unknown";
    }
}

bool pinmap_is_free(int pin) {
    return pinmap_role(pin) == PIN_ROLE_FREE;
}

bool pinmap_is_console(int pin) {
    return pinmap_role(pin) == PIN_ROLE_CONSOLE;
}

bool pinmap_is_reserved(int pin) {
    pin_role_t role = pinmap_role(pin);
    return role != PIN_ROLE_FREE && role != PIN_ROLE_CONSOLE;
}

int pinmap_free_count(void) {
    int free_pins = 0;
    for (int pin = 0; pin < PINMAP_PIN_COUNT; pin++) {
        if (pinmap_is_free(pin)) free_pins++;
    }
    return free_pins;
}

const char *pinmap_serial_port_name(int index) {
    if (index == 0) return "uart0";
    if (index == 1) return "uart1";
    return "";
}

bool pinmap_uart(int index, pinmap_placement_t *out) {
    if (!out) return false;
    *out = (pinmap_placement_t){0};
    if (index == 0) {
        /* stdio owns UART0; using it externally means giving up the console. */
        if (pinmap_is_reserved(0) || pinmap_is_reserved(1)) return false;
        *out = (pinmap_placement_t){
            .label = "uart0",
            .tx = 0,
            .rx = 1,
            .uses_console = true,
        };
        return true;
    }
    if (index == 1) {
        if (!pinmap_is_free(4) || !pinmap_is_free(5)) return false;
        *out = (pinmap_placement_t){.label = "uart1", .tx = 4, .rx = 5};
        return true;
    }
    return false;
}

bool pinmap_i2c(int index, pinmap_placement_t *out) {
    if (!out) return false;
    *out = (pinmap_placement_t){0};
    if (index == 0) {
        /* I2C0 on GP8/GP9 is the only fully free controller pin pair. */
        if (!pinmap_is_free(8) || !pinmap_is_free(9)) return false;
        *out = (pinmap_placement_t){.label = "i2c0", .sda = 8, .scl = 9};
        return true;
    }
    if (index == 1) {
        /* I2C1's default pair is the keyboard; GP2/GP3 is the free mapping. */
        if (!pinmap_is_free(2) || !pinmap_is_free(3)) return false;
        *out = (pinmap_placement_t){.label = "i2c1", .sda = 2, .scl = 3};
        return true;
    }
    return false;
}

bool pinmap_spi(int index, pinmap_placement_t *out) {
    if (!out) return false;
    *out = (pinmap_placement_t){0};
    if (index == 1) return false; /* every SPI1 pin set is used by LCD/audio */

    /* SPI0 is the SD card (GP16..GP19). The only remaining mapping borrows
       the console UART pins: MISO=0, CS=1, SCK=2, MOSI=3. */
    const int miso = 0;
    const int cs = 1;
    const int sck = 2;
    const int mosi = 3;
    if (pinmap_is_reserved(miso) || pinmap_is_reserved(cs) || pinmap_is_reserved(sck) ||
        pinmap_is_reserved(mosi))
        return false;
    *out = (pinmap_placement_t){
        .label = "spi0",
        .sck = sck,
        .mosi = mosi,
        .miso = miso,
        .cs = cs,
        .uses_console = true,
    };
    return true;
}
