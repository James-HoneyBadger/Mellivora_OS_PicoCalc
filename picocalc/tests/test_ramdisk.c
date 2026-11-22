#include "test_util.h"

#include "ramdisk.h"

#include <string.h>

void test_ramdisk(void) {
    uint32_t length = 0;
    const uint8_t *data;

    CHECK(ramdisk_init());
    CHECK(ramdisk_capacity() >= 2048u);
    CHECK(ramdisk_used() == 0);
    CHECK(ramdisk_file_count() == 0);

    CHECK(ramdisk_write("first", "12345", 5));
    CHECK(ramdisk_write("second", "abc", 3));
    CHECK(ramdisk_file_count() == 2);
    CHECK(ramdisk_used() == 8);

    CHECK(ramdisk_write("first", "longer", 6));
    CHECK(ramdisk_file_count() == 2);
    CHECK(ramdisk_used() == 9);
    data = ramdisk_get("first", &length);
    CHECK(data != NULL && length == 6 && memcmp(data, "longer", 6) == 0);
    data = ramdisk_get("second", &length);
    CHECK(data != NULL && length == 3 && memcmp(data, "abc", 3) == 0);

    CHECK(!ramdisk_write("../bad", "x", 1));
    CHECK(ramdisk_remove("first"));
    CHECK(!ramdisk_remove("first"));
    CHECK(ramdisk_file_count() == 1);
    CHECK(ramdisk_used() == 3);
}
