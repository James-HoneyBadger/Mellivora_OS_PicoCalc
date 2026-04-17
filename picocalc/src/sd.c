/*
 * sd.c — Minimal SD card SPI block driver for Clockwork PicoCalc
 *
 * Supports SD v1, v2, and SDHC/SDXC cards.
 * Uses SPI0 with the pin assignments in picocalc_hw.h.
 *
 * Reference: SD Physical Layer Specification v6.00 Section 7 (SPI mode).
 */

#include "sd.h"
#include "picocalc_hw.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal constants                                                   */
/* ------------------------------------------------------------------ */

#define SD_BLOCK_SIZE   512

/* Command tokens */
#define CMD0    (0x40 | 0)   /* GO_IDLE_STATE   */
#define CMD1    (0x40 | 1)   /* SEND_OP_COND (MMC) */
#define CMD8    (0x40 | 8)   /* SEND_IF_COND    */
#define CMD9    (0x40 | 9)   /* SEND_CSD        */
#define CMD16   (0x40 | 16)  /* SET_BLOCKLEN    */
#define CMD17   (0x40 | 17)  /* READ_SINGLE     */
#define CMD24   (0x40 | 24)  /* WRITE_SINGLE    */
#define CMD55   (0x40 | 55)  /* APP_CMD         */
#define CMD58   (0x40 | 58)  /* READ_OCR        */
#define ACMD41  (0x40 | 41)  /* APP_SEND_OP_COND */

/* R1 response bits */
#define R1_IDLE         0x01
#define R1_ILLEGAL_CMD  0x04

/* Data token */
#define DATA_START_TOKEN    0xFE
#define DATA_ACCEPT_TOKEN   0x05

/* Timeouts */
#define SD_INIT_RETRIES     2000
#define SD_READ_TIMEOUT     2000
#define SD_WRITE_TIMEOUT    2000

static int  _sdhc = 0;   /* 1 = SDHC/SDXC (block addressing), 0 = byte addressing */
static int  _inited = 0;

/* ------------------------------------------------------------------ */
/* SPI helpers                                                          */
/* ------------------------------------------------------------------ */

static inline void _cs_low(void)  { gpio_put(SD_PIN_CS, 0); }
static inline void _cs_high(void) { gpio_put(SD_PIN_CS, 1); }

static uint8_t _spi_xchg(uint8_t out) {
    uint8_t in = 0xFF;
    spi_write_read_blocking(SD_SPI_PORT, &out, &in, 1);
    return in;
}

static void _spi_read(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = _spi_xchg(0xFF);
}

/* Flush clock cycles with CS high (as per SD init spec) */
static void _spi_flush(int n) {
    for (int i = 0; i < n; i++) _spi_xchg(0xFF);
}

/* ------------------------------------------------------------------ */
/* SD command layer                                                     */
/* ------------------------------------------------------------------ */

/* Wait until the card is no longer busy (MISO = 0xFF). */
static int _wait_ready(int max_retries) {
    for (int i = 0; i < max_retries; i++) {
        if (_spi_xchg(0xFF) == 0xFF) return 1;
    }
    return 0;
}

/* Send 6-byte command, return R1 response byte. */
static uint8_t _cmd(uint8_t cmd, uint32_t arg) {
    /* Wait for card ready before every command */
    _wait_ready(SD_INIT_RETRIES);

    _spi_xchg(cmd);
    _spi_xchg((uint8_t)(arg >> 24));
    _spi_xchg((uint8_t)(arg >> 16));
    _spi_xchg((uint8_t)(arg >>  8));
    _spi_xchg((uint8_t)(arg      ));

    /* CRC: valid only for CMD0 (0x95) and CMD8 (0x87), else 0xFF dummy */
    uint8_t crc = 0xFF;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    _spi_xchg(crc);

    /* Skip up to 8 bytes looking for a non-0xFF R1 response */
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 8; i++) {
        r1 = _spi_xchg(0xFF);
        if (!(r1 & 0x80)) break;
    }
    return r1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

sd_result_t sd_init(void) {
    /* Low-speed SPI for init: 400 kHz */
    spi_init(SD_SPI_PORT, 400000);
    gpio_set_function(SD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    _cs_high();

    /* ≥74 clock cycles with CS high to enter native SPI mode */
    sleep_ms(1);
    _spi_flush(10);

    /* CMD0: GO_IDLE_STATE */
    _cs_low();
    uint8_t r1 = _cmd(CMD0, 0);
    _cs_high();
    if (r1 != R1_IDLE) return SD_NOCARD;

    /* CMD8: SEND_IF_COND — detect v2 card */
    _cs_low();
    r1 = _cmd(CMD8, 0x000001AA);
    int is_v2 = 0;
    if (r1 == R1_IDLE) {
        uint8_t r7[4];
        _spi_read(r7, 4);
        if (r7[3] == 0xAA) is_v2 = 1;
    }
    _cs_high();

    /* ACMD41 / CMD1 loop: bring card out of idle */
    int ok = 0;
    for (int i = 0; i < SD_INIT_RETRIES; i++) {
        _cs_low();
        _cmd(CMD55, 0);
        _cs_high();

        _cs_low();
        r1 = _cmd(ACMD41, is_v2 ? 0x40000000 : 0);
        _cs_high();

        if (r1 == 0) { ok = 1; break; }
        sleep_us(500);
    }
    if (!ok) return SD_TIMEOUT;

    /* Read OCR to check SDHC bit (CCS) */
    _sdhc = 0;
    if (is_v2) {
        _cs_low();
        r1 = _cmd(CMD58, 0);
        if (r1 == 0) {
            uint8_t ocr[4];
            _spi_read(ocr, 4);
            if (ocr[0] & 0x40) _sdhc = 1;
        }
        _cs_high();
    }

    /* For byte-addressed cards, set block length to 512 */
    if (!_sdhc) {
        _cs_low();
        r1 = _cmd(CMD16, SD_BLOCK_SIZE);
        _cs_high();
        if (r1 != 0) return SD_ERROR;
    }

    /* Raise SPI speed for data transfers */
    spi_set_baudrate(SD_SPI_PORT, SD_SPI_SPEED);

    _inited = 1;
    return SD_OK;
}

sd_result_t sd_read_block(uint32_t lba, uint8_t *buf) {
    if (!_inited) return SD_NOCARD;

    uint32_t addr = _sdhc ? lba : lba * SD_BLOCK_SIZE;

    _cs_low();
    uint8_t r1 = _cmd(CMD17, addr);
    if (r1 != 0) { _cs_high(); return SD_ERROR; }

    /* Wait for data start token */
    int tok_ok = 0;
    for (int i = 0; i < SD_READ_TIMEOUT; i++) {
        uint8_t tok = _spi_xchg(0xFF);
        if (tok == DATA_START_TOKEN) { tok_ok = 1; break; }
    }
    if (!tok_ok) { _cs_high(); return SD_TIMEOUT; }

    _spi_read(buf, SD_BLOCK_SIZE);
    _spi_xchg(0xFF); /* CRC hi */
    _spi_xchg(0xFF); /* CRC lo */
    _cs_high();
    _spi_xchg(0xFF); /* release bus */
    return SD_OK;
}

sd_result_t sd_write_block(uint32_t lba, const uint8_t *buf) {
    if (!_inited) return SD_NOCARD;

    uint32_t addr = _sdhc ? lba : lba * SD_BLOCK_SIZE;

    _cs_low();
    uint8_t r1 = _cmd(CMD24, addr);
    if (r1 != 0) { _cs_high(); return SD_ERROR; }

    _spi_xchg(0xFF); /* preamble */
    _spi_xchg(DATA_START_TOKEN);
    for (int i = 0; i < SD_BLOCK_SIZE; i++) _spi_xchg(buf[i]);
    _spi_xchg(0xFF); /* dummy CRC */
    _spi_xchg(0xFF);

    uint8_t resp = _spi_xchg(0xFF) & 0x1F;
    if (resp != DATA_ACCEPT_TOKEN) { _cs_high(); return SD_ERROR; }

    /* Wait for write to complete */
    int done = _wait_ready(SD_WRITE_TIMEOUT);
    _cs_high();
    _spi_xchg(0xFF);
    return done ? SD_OK : SD_TIMEOUT;
}

const char *sd_result_str(sd_result_t r) {
    switch (r) {
        case SD_OK:      return "OK";
        case SD_ERROR:   return "error";
        case SD_TIMEOUT: return "timeout";
        case SD_NOCARD:  return "no card";
        default:         return "unknown";
    }
}
