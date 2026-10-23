/*
 * test_lfn.c — src/core/lfn.c
 */

#include <stdint.h>
#include <string.h>

#include "core/lfn.h"
#include "test_util.h"

static const uint8_t SHORT_NAME[11] = {'L', 'O', 'N', 'G', 'F', 'I', '~', '1', 'T', 'X', 'T'};

static void make_entry(uint8_t raw[32], uint8_t seq, const uint16_t units[13], uint8_t checksum) {
    static const uint8_t offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

    memset(raw, 0xFF, 32);
    raw[0] = seq;
    raw[11] = 0x0F;
    raw[12] = 0;
    raw[13] = checksum;

    for (int i = 0; i < 13; i++) {
        raw[offsets[i]] = (uint8_t)(units[i] & 0xFF);
        raw[offsets[i] + 1] = (uint8_t)(units[i] >> 8);
    }
}

static void make_longfile_entries(uint8_t e2[32], uint8_t e1[32], uint8_t checksum) {
    static const uint16_t units1[13] = {'L', 'o', 'n', 'g', 'F', 'i', 'l',
                                        'e', 'N', 'a', 'm', 'e', '.'};
    static const uint16_t units2[13] = {'t',    'x',    't',    0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
                                        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

    make_entry(e2, 0x42, units2, checksum);
    make_entry(e1, 0x01, units1, checksum);
}

void test_lfn(void) {
    /* Fixed checksum vectors from the VFAT specification algorithm. */
    CHECK_EQ(lfn_checksum((const uint8_t *)"FILE    TXT"), 0x19);
    CHECK_EQ(lfn_checksum((const uint8_t *)"ABCDEFGHIJK"), 0xA4);
    CHECK_EQ(lfn_checksum((const uint8_t *)"LONGFI~1TXT"), 0xD4);

    uint8_t checksum = lfn_checksum(SHORT_NAME);
    uint8_t e2[32];
    uint8_t e1[32];
    lfn_builder_t b;
    char out[64];

    lfn_reset(&b);
    make_longfile_entries(e2, e1, checksum);
    CHECK(lfn_add_entry(&b, e2));
    CHECK(lfn_add_entry(&b, e1));
    CHECK(lfn_finish(&b, SHORT_NAME, out, sizeof out));
    CHECK_STR_EQ(out, "LongFileName.txt");

    /* Checksum mismatch. */
    lfn_reset(&b);
    make_longfile_entries(e2, e1, (uint8_t)(checksum + 1));
    CHECK(lfn_add_entry(&b, e2));
    CHECK(lfn_add_entry(&b, e1));
    CHECK(!lfn_finish(&b, SHORT_NAME, out, sizeof out));

    /* Missing first entry. */
    lfn_reset(&b);
    make_longfile_entries(e2, e1, checksum);
    CHECK(lfn_add_entry(&b, e2));
    CHECK(!lfn_finish(&b, SHORT_NAME, out, sizeof out));

    /* Duplicate sequence number. */
    lfn_reset(&b);
    make_longfile_entries(e2, e1, checksum);
    CHECK(lfn_add_entry(&b, e2));
    CHECK(lfn_add_entry(&b, e2));
    CHECK(!lfn_finish(&b, SHORT_NAME, out, sizeof out));

    /* Non-LFN entry is not consumed. */
    uint8_t plain[32] = {0};
    plain[11] = 0x20; /* archive */
    lfn_reset(&b);
    CHECK(!lfn_add_entry(&b, plain));

    /* Non-ASCII code points become '?'. */
    uint16_t units1[13] = {'L', 'o', 'n', 'g', 0x00E9, 'i', 'l', 'e', 'N', 'a', 'm', 'e', '.'};
    uint16_t units2[13] = {'t',    'x',    't',    0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
                           0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    make_entry(e2, 0x42, units2, checksum);
    make_entry(e1, 0x01, units1, checksum);
    lfn_reset(&b);
    CHECK(lfn_add_entry(&b, e2));
    CHECK(lfn_add_entry(&b, e1));
    CHECK(lfn_finish(&b, SHORT_NAME, out, sizeof out));
    CHECK_STR_EQ(out, "Long?ileName.txt");

    /* Empty long name. */
    uint16_t empty[13] = {0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                          0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    make_entry(e2, 0x41, empty, checksum);
    lfn_reset(&b);
    CHECK(lfn_add_entry(&b, e2));
    CHECK(!lfn_finish(&b, SHORT_NAME, out, sizeof out));
}
