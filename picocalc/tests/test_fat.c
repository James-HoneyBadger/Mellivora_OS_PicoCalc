#include "fat.h"
#include "sd.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

#define FAKE_SECTOR_COUNT 6000
#define FAT_SIZE_SECTORS 24
#define ROOT_ENTRY_COUNT 512
#define ROOT_DIR_LBA (1 + 2 * FAT_SIZE_SECTORS)

static uint8_t fake_disk[FAKE_SECTOR_COUNT][512];
static sd_result_t fake_init_result = SD_OK;
static int32_t fake_read_error_lba = -1;

sd_result_t sd_init(void) {
    return fake_init_result;
}

sd_result_t sd_read_block(uint32_t lba, uint8_t *buf) {
    if (lba >= FAKE_SECTOR_COUNT || (int32_t)lba == fake_read_error_lba) return SD_ERROR;
    memcpy(buf, fake_disk[lba], 512);
    return SD_OK;
}

sd_result_t sd_write_block(uint32_t lba, const uint8_t *buf) {
    if (lba >= FAKE_SECTOR_COUNT) return SD_ERROR;
    memcpy(fake_disk[lba], buf, 512);
    return SD_OK;
}

static void set_u16(uint8_t *buf, size_t offset, uint16_t value) {
    buf[offset] = (uint8_t)value;
    buf[offset + 1] = (uint8_t)(value >> 8);
}

static void format_fake_fat16(void) {
    memset(fake_disk, 0, sizeof fake_disk);
    uint8_t *boot = fake_disk[0];
    set_u16(boot, 11, 512);
    boot[13] = 1;
    set_u16(boot, 14, 1);
    boot[16] = 2;
    set_u16(boot, 17, ROOT_ENTRY_COUNT);
    set_u16(boot, 19, FAKE_SECTOR_COUNT);
    boot[21] = 0xF8;
    set_u16(boot, 22, FAT_SIZE_SECTORS);
    boot[510] = 0x55;
    boot[511] = 0xAA;
    set_u16(fake_disk[1], 0, 0xFFF8);
    set_u16(fake_disk[1], 2, 0xFFFF);
    set_u16(fake_disk[1 + FAT_SIZE_SECTORS], 0, 0xFFF8);
    set_u16(fake_disk[1 + FAT_SIZE_SECTORS], 2, 0xFFFF);
}

typedef struct {
    int count;
    char name[16];
    uint32_t size;
    bool is_dir;
} list_result_t;

static void capture_entry(const char *name, uint32_t size, bool is_dir, void *ctx) {
    list_result_t *result = ctx;
    result->count++;
    strncpy(result->name, name, sizeof result->name - 1);
    result->name[sizeof result->name - 1] = '\0';
    result->size = size;
    result->is_dir = is_dir;
}

void test_fat(void) {
    uint8_t contents[900];
    const uint8_t extra[] = {0xA1, 0xB2, 0xC3, 0xD4};
    uint8_t received[sizeof contents + sizeof extra];
    for (size_t i = 0; i < sizeof contents; i++)
        contents[i] = (uint8_t)(i * 13);

    fat_file_t file;
    CHECK_EQ(fat_open("/MISSING.TXT", &file), FAT_ERR_NOTMOUNTED);

    fake_init_result = SD_NOCARD;
    CHECK_EQ(fat_mount(), FAT_ERR_IO);
    fake_init_result = SD_OK;
    format_fake_fat16();
    CHECK_EQ(fat_mount(), FAT_OK);
    CHECK_EQ(fat_is_dir("/"), FAT_OK);

    list_result_t listing = {0};
    CHECK_EQ(fat_ls("/", capture_entry, &listing), FAT_OK);
    CHECK_EQ(listing.count, 0);

    CHECK_EQ(fat_create("/HELLO.TXT", contents, sizeof contents), FAT_OK);
    CHECK_EQ(fat_create("/HELLO.TXT", contents, sizeof contents), FAT_ERR_EXISTS);
    CHECK_EQ(fat_open("/HELLO.TXT", &file), FAT_OK);
    CHECK_EQ(file.size, sizeof contents);
    CHECK_EQ(fat_read(&file, received, 512), 512);
    CHECK_EQ(fat_read(&file, received + 512, sizeof contents - 512), sizeof contents - 512);
    CHECK(memcmp(contents, received, sizeof contents) == 0);
    CHECK_EQ(fat_read(&file, received, 1), FAT_ERR_EOF);

    CHECK_EQ(fat_append("/HELLO.TXT", extra, sizeof extra), FAT_OK);
    CHECK_EQ(fat_open("/HELLO.TXT", &file), FAT_OK);
    CHECK_EQ(file.size, sizeof received);
    CHECK_EQ(fat_read(&file, received, sizeof received), sizeof received);
    CHECK(memcmp(contents, received, sizeof contents) == 0);
    CHECK(memcmp(extra, received + sizeof contents, sizeof extra) == 0);

    const uint8_t replacement[] = {'n', 'e', 'w'};
    CHECK_EQ(fat_write_atomic("/HELLO.TXT", replacement, sizeof replacement), FAT_OK);
    CHECK_EQ(fat_open("/HELLO.TXT", &file), FAT_OK);
    CHECK_EQ(file.size, sizeof replacement);
    CHECK_EQ(fat_read(&file, received, sizeof received), sizeof replacement);
    CHECK(memcmp(replacement, received, sizeof replacement) == 0);

    listing = (list_result_t){0};
    CHECK_EQ(fat_ls("/", capture_entry, &listing), FAT_OK);
    CHECK_EQ(listing.count, 1);
    CHECK_STR_EQ(listing.name, "HELLO.TXT");
    CHECK_EQ(listing.size, sizeof replacement);
    CHECK(!listing.is_dir);
    CHECK_EQ(fat_is_dir("/HELLO.TXT"), FAT_ERR_NOTFILE);
    CHECK_EQ(fat_open("/MISSING.TXT", &file), FAT_ERR_NOTFOUND);

    CHECK_EQ(fat_mkdir("/DOCS"), FAT_OK);
    CHECK_EQ(fat_mkdir("/DOCS"), FAT_ERR_EXISTS);
    CHECK_EQ(fat_is_dir("/DOCS"), FAT_OK);
    CHECK_EQ(fat_create("/DOCS/NOTE.TXT", contents, 12), FAT_OK);
    CHECK_EQ(fat_unlink("/DOCS"), FAT_ERR_NOTEMPTY);
    CHECK_EQ(fat_rename("/DOCS/NOTE.TXT", "/DOCS/RENAMED.TXT"), FAT_OK);
    CHECK_EQ(fat_open("/DOCS/RENAMED.TXT", &file), FAT_OK);

    CHECK_EQ(fat_mkdir("/OTHER"), FAT_OK);
    CHECK_EQ(fat_rename("/DOCS/RENAMED.TXT", "/OTHER/RENAMED.TXT"), FAT_ERR_UNSUPPORTED);
    CHECK_EQ(fat_unlink("/DOCS/RENAMED.TXT"), FAT_OK);
    CHECK_EQ(fat_unlink("/DOCS"), FAT_OK);

    fake_read_error_lba = ROOT_DIR_LBA;
    CHECK_EQ(fat_ls("/", capture_entry, &listing), FAT_ERR_IO);
    fake_read_error_lba = -1;
}
