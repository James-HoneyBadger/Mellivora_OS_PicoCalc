#pragma once

/*
 * Clockwork PicoCalc v2.0 hardware pinout
 *
 * Source: clockwork_Mainboard_V2.0_Schematic and
 *         Code/picocalc_helloworld reference drivers.
 */

/* ----- Display: ILI9488 320x320 IPS, SPI1 at 25 MHz ----- */
#define LCD_SPI_PORT    spi1
#define LCD_SPI_SPEED   25000000U

#define LCD_PIN_SCK     10
#define LCD_PIN_MOSI    11
#define LCD_PIN_MISO    12
#define LCD_PIN_CS      13
#define LCD_PIN_DC      14
#define LCD_PIN_RST     15

#define LCD_WIDTH       320
#define LCD_HEIGHT      320

/* ----- Keyboard: I2C1 at 10 kHz via STM32 controller ----- */
#define KBD_I2C_PORT    i2c1
#define KBD_I2C_SPEED   10000U
#define KBD_PIN_SDA     6
#define KBD_PIN_SCL     7
#define KBD_I2C_ADDR    0x1F

/* I2C register selectors sent to the keyboard controller */
#define KBD_REG_KEY     0x09    /* 2-byte key state (keycode | status) */
#define KBD_REG_BKLT    0x8A    /* backlight level (write, bit7 set)   */
#define KBD_REG_BAT     0x0B    /* battery state (read)                */

/* Key status bits returned in low byte of 2-byte read */
#define KBD_STAT_PRESSED  1
#define KBD_CTRL_HELD     0x7E02
#define KBD_CTRL_RELEASE  0x7E03

/* ----- SD card: SPI0 ----- */
#define SD_SPI_PORT     spi0
#define SD_SPI_SPEED    12500000U

#define SD_PIN_SCK      2
#define SD_PIN_MOSI     3
#define SD_PIN_MISO     4
#define SD_PIN_CS       5

/* ----- Audio: dual PWM channels ----- */
#define AUDIO_PIN_L     26
#define AUDIO_PIN_R     27
