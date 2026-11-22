/*
 * core/i2cdetect.c — I2C bus scan and i2cdetect-style output.
 */

#include "core/i2cdetect.h"

#include <stdio.h>

void i2c_scan_reset(i2c_scan_t *scan, uint8_t first, uint8_t last) {
    if (!scan) return;
    for (size_t i = 0; i < sizeof scan->present; i++)
        scan->present[i] = 0;
    scan->count = 0;
    scan->probed = 0;
    if (last > 0x7F) last = 0x7F;
    if (first > last) {
        first = I2C_SCAN_FIRST;
        last = I2C_SCAN_LAST;
    }
    scan->first = first;
    scan->last = last;
}

bool i2c_scan_present(const i2c_scan_t *scan, uint8_t addr) {
    if (!scan || addr > 0x7F) return false;
    return (scan->present[addr >> 3] & (uint8_t)(1u << (addr & 7u))) != 0;
}

static void _set(i2c_scan_t *scan, uint8_t addr) {
    scan->present[addr >> 3] |= (uint8_t)(1u << (addr & 7u));
}

int i2c_scan_run(i2c_scan_t *scan, i2c_probe_fn probe, void *ctx) {
    if (!scan || !probe) return -1;
    scan->count = 0;
    scan->probed = 0;
    for (int addr = scan->first; addr <= (int)scan->last; addr++) {
        scan->probed++;
        if (!probe(ctx, (uint8_t)addr)) continue;
        _set(scan, (uint8_t)addr);
        scan->count++;
    }
    return scan->count;
}

const char *i2c_scan_header(void) {
    return "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f";
}

int i2c_scan_row_for(uint8_t addr) {
    return addr >> 4;
}

static bool _in_range(const i2c_scan_t *scan, uint8_t addr) {
    return addr >= scan->first && addr <= scan->last;
}

int i2c_scan_format_row(const i2c_scan_t *scan, int row, char *out, size_t cap) {
    if (!scan || !out || cap < 8 || row < 0 || row > 7) return -1;
    int n = snprintf(out, cap, "%02x:", row * 16);
    if (n < 0) return -1;
    size_t used = (size_t)n;

    for (int low = 0; low < 16; low++) {
        uint8_t addr = (uint8_t)(row * 16 + low);
        char cell[4];
        if (!_in_range(scan, addr)) (void)snprintf(cell, sizeof cell, "   ");
        else if (i2c_scan_present(scan, addr))
            (void)snprintf(cell, sizeof cell, " %02x", addr);
        else
            (void)snprintf(cell, sizeof cell, " --");

        size_t len = 3;
        if (used + len + 1 > cap) return -1;
        for (size_t i = 0; i < len; i++)
            out[used + i] = cell[i];
        used += len;
    }
    out[used] = '\0';
    return (int)used;
}
