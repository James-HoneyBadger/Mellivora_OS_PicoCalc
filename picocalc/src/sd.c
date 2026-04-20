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
#define SD_INIT_RETRIES     10000
#define SD_READ_TIMEOUT     100000
#define SD_WRITE_TIMEOUT    100000

static int  _sdhc = 0;   /* 1 = SDHC/SDXC (block addressing), 0 = byte addressing */
static int  _inited = 0;
static int  _use_bitbang = 1;

/* ------------------------------------------------------------------ */
/* SPI helpers                                                          */
/* ------------------------------------------------------------------ */

static inline void _cs_low(void)  { gpio_put(SD_PIN_CS, 0); }
static inline void _cs_high(void) { gpio_put(SD_PIN_CS, 1); }

static void _bitbang_init(void) {
    gpio_init(SD_PIN_SCK);
    gpio_init(SD_PIN_MOSI);
    gpio_init(SD_PIN_MISO);
    gpio_init(SD_PIN_CS);

    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SIO);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SIO);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SIO);
    gpio_set_function(SD_PIN_CS, GPIO_FUNC_SIO);

    gpio_set_dir(SD_PIN_SCK, GPIO_OUT);
    gpio_set_dir(SD_PIN_MOSI, GPIO_OUT);
    gpio_set_dir(SD_PIN_MISO, GPIO_IN);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);

    gpio_set_drive_strength(SD_PIN_SCK, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(SD_PIN_MOSI, GPIO_DRIVE_STRENGTH_8MA);
    gpio_pull_up(SD_PIN_MISO);
    gpio_set_input_hysteresis_enabled(SD_PIN_MISO, true);

    gpio_put(SD_PIN_SCK, 0);
    gpio_put(SD_PIN_MOSI, 1);
    gpio_put(SD_PIN_CS, 1);
}

static uint8_t _spi_xchg(uint8_t out) {
    if (_use_bitbang) {
        uint8_t in = 0;
        for (int i = 0; i < 8; i++) {
            gpio_put(SD_PIN_MOSI, (out & 0x80u) ? 1 : 0);
            busy_wait_us_32(2);
            gpio_put(SD_PIN_SCK, 1);
            in = (uint8_t)((in << 1) | (gpio_get(SD_PIN_MISO) ? 1 : 0));
            busy_wait_us_32(2);
            gpio_put(SD_PIN_SCK, 0);
            out <<= 1;
        }
        return in;
    }

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
        busy_wait_us_32(5);
    }
    return 0;
}

static void _deselect(void) {
    _cs_high();
    busy_wait_us_32(5);
    _spi_xchg(0xFF); /* release bus */
}

static int _select(void) {
    _cs_low();
    busy_wait_us_32(5);
    _spi_xchg(0xFF); /* enable DO */
    if (_wait_ready(SD_INIT_RETRIES)) return 1;
    _deselect();
    return 0;
}

static uint8_t _crc7(const uint8_t *message, unsigned length) {
    const uint8_t poly = 0x89;
    uint8_t crc = 0;
    for (unsigned i = 0; i < length; i++) {
        crc ^= message[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ (poly << 1)) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc >> 1);
}

/* Send 6-byte command, return R1 response byte. */
static uint8_t _cmd(uint8_t cmd, uint32_t arg) {
    /* Wait for card ready before every command */
    _wait_ready(SD_INIT_RETRIES);

    uint8_t packet[5] = {
        cmd,
        (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),
        (uint8_t)(arg)
    };

    for (int i = 0; i < 5; i++) _spi_xchg(packet[i]);
    _spi_xchg((uint8_t)((_crc7(packet, 5) << 1) | 1));

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
    if (_inited) return SD_OK;

    /* Card-detect is useful as a hint only; do not hard-fail on it. */
    bool inserted_hint = true;
    gpio_init(SD_PIN_DET);
    gpio_set_dir(SD_PIN_DET, GPIO_IN);
    gpio_pull_up(SD_PIN_DET);
    inserted_hint = !gpio_get(SD_PIN_DET);

    /* Use the more tolerant PicoCalc-style bit-banged transport. */
    _bitbang_init();
    _deselect();

    /* ≥74 clock cycles with CS high to enter SPI mode. */
    sleep_ms(2);
    _spi_flush(10);

    /* CMD0: GO_IDLE_STATE */
    if (!_select()) return inserted_hint ? SD_TIMEOUT : SD_NOCARD;
    uint8_t r1 = _cmd(CMD0, 0);
    _deselect();
    if (r1 != R1_IDLE) return inserted_hint ? SD_ERROR : SD_NOCARD;

    /* CMD8: SEND_IF_COND — detect v2 card */
    int is_v2 = 0;
    if (_select()) {
        r1 = _cmd(CMD8, 0x000001AA);
        if (r1 == R1_IDLE) {
            uint8_t r7[4];
            _spi_read(r7, 4);
            if (r7[2] == 0x01 && r7[3] == 0xAA) is_v2 = 1;
        }
        _deselect();
    }

    /* ACMD41, with CMD1 fallback for older cards. */
    int ok = 0;
    int use_cmd1 = 0;
    for (int i = 0; i < SD_INIT_RETRIES; i++) {
        if (!use_cmd1) {
            if (!_select()) break;
            r1 = _cmd(CMD55, 0);
            _deselect();
            if (r1 & R1_ILLEGAL_CMD) use_cmd1 = 1;
        }

        if (!_select()) break;
        r1 = _cmd(use_cmd1 ? CMD1 : ACMD41,
                  (is_v2 && !use_cmd1) ? 0x40000000 : 0);
        _deselect();

        if (r1 == 0) { ok = 1; break; }
        busy_wait_us_32(500);
    }
    if (!ok) return SD_TIMEOUT;

    /* Read OCR to check SDHC bit (CCS). */
    _sdhc = 0;
    if (is_v2 && _select()) {
        r1 = _cmd(CMD58, 0);
        if (r1 == 0) {
            uint8_t ocr[4];
            _spi_read(ocr, 4);
            if (ocr[0] & 0x40) _sdhc = 1;
        }
        _deselect();
    }

    /* For byte-addressed cards, set block length to 512. */
    if (!_sdhc) {
        if (!_select()) return SD_TIMEOUT;
        r1 = _cmd(CMD16, SD_BLOCK_SIZE);
        _deselect();
        if (r1 != 0) return SD_ERROR;
    }

    _inited = 1;
    return SD_OK;
}

sd_result_t sd_read_block(uint32_t lba, uint8_t *buf) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!_inited) {
            sd_result_t init_r = sd_init();
            if (init_r != SD_OK) return init_r;
        }

        uint32_t addr = _sdhc ? lba : lba * SD_BLOCK_SIZE;

        if (!_select()) { _inited = 0; continue; }
        uint8_t r1 = _cmd(CMD17, addr);
        if (r1 != 0) { _deselect(); _inited = 0; continue; }

        /* Wait for data start token. */
        int tok_ok = 0;
        for (int i = 0; i < SD_READ_TIMEOUT; i++) {
            uint8_t tok = _spi_xchg(0xFF);
            if (tok == DATA_START_TOKEN) { tok_ok = 1; break; }
            busy_wait_us_32(2);
        }
        if (!tok_ok) { _deselect(); _inited = 0; continue; }

        _spi_read(buf, SD_BLOCK_SIZE);
        _spi_xchg(0xFF); /* CRC hi */
        _spi_xchg(0xFF); /* CRC lo */
        _deselect();
        return SD_OK;
    }
    return SD_TIMEOUT;
}

sd_result_t sd_write_block(uint32_t lba, const uint8_t *buf) {
    if (!_inited) {
        sd_result_t init_r = sd_init();
        if (init_r != SD_OK) return init_r;
    }

    uint32_t addr = _sdhc ? lba : lba * SD_BLOCK_SIZE;

    if (!_select()) return SD_TIMEOUT;
    uint8_t r1 = _cmd(CMD24, addr);
    if (r1 != 0) { _deselect(); return SD_ERROR; }

    _spi_xchg(0xFF); /* preamble */
    _spi_xchg(DATA_START_TOKEN);
    for (int i = 0; i < SD_BLOCK_SIZE; i++) _spi_xchg(buf[i]);
    _spi_xchg(0xFF); /* dummy CRC */
    _spi_xchg(0xFF);

    uint8_t resp = _spi_xchg(0xFF) & 0x1F;
    if (resp != DATA_ACCEPT_TOKEN) { _deselect(); return SD_ERROR; }

    /* Wait for write to complete. */
    int done = _wait_ready(SD_WRITE_TIMEOUT);
    _deselect();
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
