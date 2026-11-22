#pragma once
/*
 * core/i2cdetect.h — bus scan bookkeeping and grid formatting.
 *
 * The probe itself is a callback, so the same code drives the real I2C
 * driver, a simulated bus in host tests, or a dry-run banner.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_SCAN_FIRST 0x03
#define I2C_SCAN_LAST 0x77

typedef bool (*i2c_probe_fn)(void *ctx, uint8_t addr);

typedef struct {
    uint8_t present[16]; /* 128 bits, one per 7-bit address */
    int count;
    int probed;
    uint8_t first;
    uint8_t last;
} i2c_scan_t;

void i2c_scan_reset(i2c_scan_t *scan, uint8_t first, uint8_t last);
int i2c_scan_run(i2c_scan_t *scan, i2c_probe_fn probe, void *ctx);
bool i2c_scan_present(const i2c_scan_t *scan, uint8_t addr);

/* "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f" */
const char *i2c_scan_header(void);

/* Format one 16-address row. Returns the length or -1 when out of range. */
int i2c_scan_format_row(const i2c_scan_t *scan, int row, char *out, size_t cap);
int i2c_scan_row_for(uint8_t addr);

#ifdef __cplusplus
}
#endif
