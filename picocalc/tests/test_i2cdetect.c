/*
 * test_i2cdetect.c — scan bookkeeping and grid rendering.
 */

#include "core/i2cdetect.h"
#include "test_util.h"

#include <string.h>

typedef struct {
    const uint8_t *addresses;
    size_t count;
    int probed;
} fake_bus_t;

static bool fake_probe(void *ctx, uint8_t addr) {
    fake_bus_t *bus = (fake_bus_t *)ctx;
    bus->probed++;
    for (size_t i = 0; i < bus->count; i++) {
        if (bus->addresses[i] == addr) return true;
    }
    return false;
}

void test_i2cdetect(void) {
    /* CRC of the address bit helpers. */
    i2c_scan_t scan;
    i2c_scan_reset(&scan, 0x10, 0x60);
    CHECK_EQ(scan.first, 0x10);
    CHECK_EQ(scan.last, 0x60);
    CHECK_EQ(scan.count, 0);
    CHECK(!i2c_scan_present(&scan, 0x20));

    const uint8_t devices[] = {0x20, 0x3C, 0x50};
    fake_bus_t bus = {.addresses = devices, .count = sizeof devices, .probed = 0};
    int found = i2c_scan_run(&scan, fake_probe, &bus);
    CHECK_EQ(found, 3);
    CHECK_EQ(bus.probed, 0x60 - 0x10 + 1);
    CHECK(i2c_scan_present(&scan, 0x20));
    CHECK(i2c_scan_present(&scan, 0x3C));
    CHECK(i2c_scan_present(&scan, 0x50));
    CHECK(!i2c_scan_present(&scan, 0x21));

    /* Out-of-range probes are not reported, even if the bus answers. */
    i2c_scan_reset(&scan, 0x30, 0x3F);
    fake_bus_t bus2 = {.addresses = devices, .count = sizeof devices, .probed = 0};
    CHECK_EQ(i2c_scan_run(&scan, fake_probe, &bus2), 1);
    CHECK(!i2c_scan_present(&scan, 0x20));

    /* Grid rendering matches the classic i2cdetect layout. */
    i2c_scan_reset(&scan, I2C_SCAN_FIRST, I2C_SCAN_LAST);
    fake_bus_t bus3 = {.addresses = devices, .count = sizeof devices, .probed = 0};
    CHECK_EQ(i2c_scan_run(&scan, fake_probe, &bus3), 3);

    char row[80];
    CHECK(i2c_scan_format_row(&scan, 2, row, sizeof row) > 0);
    CHECK_STR_EQ(row, "20: 20 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --");
    CHECK(i2c_scan_format_row(&scan, 3, row, sizeof row) > 0);
    CHECK(strstr(row, "3c") != NULL);
    CHECK(i2c_scan_format_row(&scan, 5, row, sizeof row) > 0);
    CHECK(strstr(row, " 50") != NULL);

    /* Addresses outside the scanned window render as blanks (0x03 is the
     * first scanned address, so 0x00-0x02 stay empty). */
    CHECK(i2c_scan_format_row(&scan, 0, row, sizeof row) > 0);
    CHECK_STR_EQ(row, "00:          -- -- -- -- -- -- -- -- -- -- -- -- --");

    CHECK(i2c_scan_format_row(&scan, 8, row, sizeof row) < 0);
    CHECK(i2c_scan_format_row(&scan, -1, row, sizeof row) < 0);
    CHECK(i2c_scan_format_row(&scan, 0, row, 4) < 0);
    CHECK_EQ(i2c_scan_row_for(0x3C), 3);
    CHECK_STR_EQ(i2c_scan_header(), "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    /* An empty bus still produces the full grid with no devices. */
    i2c_scan_reset(&scan, I2C_SCAN_FIRST, I2C_SCAN_LAST);
    fake_bus_t empty = {.addresses = NULL, .count = 0, .probed = 0};
    CHECK_EQ(i2c_scan_run(&scan, fake_probe, &empty), 0);
    CHECK(i2c_scan_format_row(&scan, 7, row, sizeof row) > 0);
    CHECK(strstr(row, "70: -- -- -- -- -- -- --") != NULL);

    /* Reserved addresses above 0x77 stay blank. */
    CHECK(i2c_scan_format_row(&scan, 7, row, sizeof row) > 0);
    CHECK(row[strlen(row) - 1] == ' ');
}
