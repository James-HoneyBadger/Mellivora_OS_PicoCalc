#pragma once
/*
 * kbd.h — PicoCalc keyboard driver (STM32 co-processor over I2C1)
 *
 * The STM32 co-processor exposes a 2-byte I2C register at 0x09:
 *   byte 0 = ASCII keycode
 *   byte 1 = status (1 = pressed, 0 = released)
 *
 * Ctrl key is signalled as special 2-byte values 0x7E02 / 0x7E03.
 * Backlight is set by writing 2 bytes to register 0x8A.
 * Battery level is read from register 0x0B.
 */
#include <stdint.h>
#include <stdbool.h>

/* Init I2C and keyboard controller.  Call once at startup. */
void kbd_init(void);

/*
 * Read one keypress.  Non-blocking.
 * Returns ASCII value if a key was pressed, -1 if none pending.
 * Ctrl+<letter> is returned as value 1-26 (same as POSIX).
 */
int  kbd_getc(void);

/* Set keyboard backlight brightness (0 = off, 255 = full). */
void kbd_set_backlight(uint8_t level);

/* Read battery percentage (0-100). Returns -1 on error. */
int  kbd_battery_percent(void);

/* Key repeat support: call when no hardware key is pressed to get
 * a synthetic repeat of the last key after the repeat delay.
 * Returns key char or -1 if no repeat due yet. */
int  kbd_get_repeat(void);

/* Clear key repeat state (call when user releases or a new context starts). */
void kbd_clear_repeat(void);

/* Returns true if the device is currently charging (bit 7 of battery register). */
bool kbd_is_charging(void);
