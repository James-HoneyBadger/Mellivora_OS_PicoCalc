/*
 * test_pinmap.c — GPIO ownership and peripheral placement.
 */

#include "core/pinmap.h"
#include "test_util.h"

void test_pinmap(void) {
    pinmap_select_wlan(false);

    /* Reserved functions from docs/HARDWARE.md. */
    CHECK_EQ(pinmap_role(6), PIN_ROLE_KBD_I2C);
    CHECK_EQ(pinmap_role(7), PIN_ROLE_KBD_I2C);
    for (int pin = 10; pin <= 15; pin++)
        CHECK_EQ(pinmap_role(pin), PIN_ROLE_LCD_SPI);
    CHECK_EQ(pinmap_role(16), PIN_ROLE_SD_SPI);
    CHECK_EQ(pinmap_role(19), PIN_ROLE_SD_SPI);
    CHECK_EQ(pinmap_role(22), PIN_ROLE_SD_SPI);
    CHECK_EQ(pinmap_role(23), PIN_ROLE_BOARD);
    CHECK_EQ(pinmap_role(26), PIN_ROLE_AUDIO_PWM);
    CHECK_EQ(pinmap_role(27), PIN_ROLE_AUDIO_PWM);
    CHECK_EQ(pinmap_role(0), PIN_ROLE_CONSOLE);
    CHECK_EQ(pinmap_role(1), PIN_ROLE_CONSOLE);

    CHECK(pinmap_is_reserved(10));
    CHECK(!pinmap_is_reserved(4));
    CHECK(pinmap_is_console(0));
    CHECK(!pinmap_is_console(4));

    /* Without a radio GP24/25/29 are usable. */
    CHECK(pinmap_is_free(24));
    CHECK(pinmap_is_free(25));
    CHECK(pinmap_is_free(29));
    CHECK_EQ(pinmap_free_count(), 12);
    CHECK_STR_EQ(pinmap_role_name(PIN_ROLE_SD_SPI), "sd-spi");
    CHECK_STR_EQ(pinmap_role_name(PIN_ROLE_CYW43), "cyw43");

    /* Out-of-range pins are never reported as allocated. */
    CHECK_EQ(pinmap_role(-1), PIN_ROLE_FREE);
    CHECK_EQ(pinmap_role(PINMAP_PIN_COUNT), PIN_ROLE_FREE);
    CHECK(!pinmap_is_reserved(-3));

    /* Pico W: the radio bus takes GP24, GP25 and GP29. */
    pinmap_select_wlan(true);
    CHECK(pinmap_has_wlan());
    CHECK_EQ(pinmap_role(24), PIN_ROLE_CYW43);
    CHECK_EQ(pinmap_role(25), PIN_ROLE_CYW43);
    CHECK_EQ(pinmap_role(29), PIN_ROLE_CYW43);
    CHECK_EQ(pinmap_free_count(), 9);
    pinmap_select_wlan(false);
    CHECK(!pinmap_has_wlan());

    /* UART: uart1 is fully free, uart0 borrows the console. */
    pinmap_placement_t place;
    CHECK(pinmap_uart(1, &place));
    CHECK_EQ(place.tx, 4);
    CHECK_EQ(place.rx, 5);
    CHECK(!place.uses_console);
    CHECK_STR_EQ(place.label, "uart1");
    CHECK(pinmap_uart(0, &place));
    CHECK_EQ(place.tx, 0);
    CHECK_EQ(place.rx, 1);
    CHECK(place.uses_console);
    CHECK(!pinmap_uart(2, &place));
    CHECK(!pinmap_uart(1, NULL));
    CHECK_STR_EQ(pinmap_serial_port_name(0), "uart0");
    CHECK_STR_EQ(pinmap_serial_port_name(1), "uart1");
    CHECK_STR_EQ(pinmap_serial_port_name(7), "");

    /* I2C: GP8/GP9 for bus 0, GP2/GP3 for bus 1. */
    CHECK(pinmap_i2c(0, &place));
    CHECK_EQ(place.sda, 8);
    CHECK_EQ(place.scl, 9);
    CHECK(pinmap_i2c(1, &place));
    CHECK_EQ(place.sda, 2);
    CHECK_EQ(place.scl, 3);
    CHECK(!pinmap_i2c(2, &place));

    /* SPI has no fully free pin set: every SPI1 option is in use. */
    CHECK(!pinmap_spi(1, &place));
    CHECK(pinmap_spi(0, &place));
    CHECK(place.uses_console);
    CHECK_EQ(place.sck, 2);
    CHECK_EQ(place.mosi, 3);
    CHECK_EQ(place.miso, 0);
    CHECK_EQ(place.cs, 1);
}
