#pragma once
/*
 * sd.h — Minimal raw SD card block driver for PicoCalc (SPI0)
 *
 * Implements the bare minimum SD over SPI v1/v2 protocol needed to
 * read and write 512-byte blocks.  Enough to host HBFS or FAT on the SD.
 *
 * Public API
 * ----------
 *   sd_result_t sd_init(void)               — initialise SPI and card
 *   sd_result_t sd_read_block(lba, buf)     — read 512 bytes from LBA
 *   sd_result_t sd_write_block(lba, buf)    — write 512 bytes to LBA
 *   const char *sd_result_str(r)            — human-readable result string
 *
 *   SD_OK      — success
 *   SD_ERROR   — generic error
 *   SD_TIMEOUT — no response from card
 *   SD_NOCARD  — no card detected / init failed
 */
#include <stdint.h>
#include <stddef.h>

typedef enum {
    SD_OK      = 0,
    SD_ERROR   = 1,
    SD_TIMEOUT = 2,
    SD_NOCARD  = 3,
} sd_result_t;

sd_result_t sd_init(void);
sd_result_t sd_read_block(uint32_t lba, uint8_t *buf);
sd_result_t sd_write_block(uint32_t lba, const uint8_t *buf);
const char *sd_result_str(sd_result_t r);
