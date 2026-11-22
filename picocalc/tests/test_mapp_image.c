/*
 * test_mapp_image.c — .mapp header validation and Thumb relocation.
 *
 * The loader itself needs the SD card and the display, so these are the
 * guarantees that keep a malformed image from writing outside its arena.
 */

#include "core/mapp_image.h"
#include "test_util.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Build a well-formed Thumb image header: 64-byte image, no relocations. */
static void make_header(uint8_t raw[MAPP_HEADER_SIZE], uint16_t kind, uint32_t image_size,
                        uint32_t bss_size, uint32_t entry_offset, uint32_t relocation_count) {
    memset(raw, 0, MAPP_HEADER_SIZE);
    memcpy(raw, "MAPP", 4);
    put_u16(raw + 4, MAPP_FORMAT_VERSION);
    put_u16(raw + 6, kind);
    put_u32(raw + 8, MAPP_HEADER_SIZE);
    put_u32(raw + 12, image_size);
    put_u32(raw + 16, bss_size);
    put_u32(raw + 20, entry_offset);
    put_u32(raw + 24, relocation_count);
    memcpy(raw + 28, "test", 4);
}

static mapp_image_header_t parse(uint8_t raw[MAPP_HEADER_SIZE], uint32_t file_size) {
    mapp_image_header_t header;
    mapp_image_parse_header(&header, raw);
    header.file_size = file_size;
    return header;
}

void test_mapp_image(void) {
    uint8_t raw[MAPP_HEADER_SIZE];
    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, 0);

    /* Parse decodes every field, including the name area it does not use. */
    mapp_image_header_t header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK(memcmp(header.magic, "MAPP", 4) == 0);
    CHECK_EQ(header.version, MAPP_FORMAT_VERSION);
    CHECK_EQ(header.kind, MAPP_KIND_THUMB);
    CHECK_EQ(header.header_size, MAPP_HEADER_SIZE);
    CHECK_EQ(header.image_size, 64);
    CHECK_EQ(header.bss_size, 0);
    CHECK_EQ(header.entry_offset, 0);
    CHECK_EQ(header.relocation_count, 0);
    CHECK_EQ(header.file_size, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_OK);

    /* A bytecode module carries no relocations and no bss. */
    make_header(raw, MAPP_KIND_BYTECODE, 40, 0, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 40);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_OK);

    /* Magic and version are checked before anything else. */
    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, 0);
    raw[0] = 'm';
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_MAGIC);

    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, 0);
    put_u16(raw + 4, (uint16_t)(MAPP_FORMAT_VERSION + 1));
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_VERSION);

    /* Unknown kind. */
    make_header(raw, 0, 64, 0, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, 3, 64, 0, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* Header size must match the format the loader was built against. */
    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, 0);
    put_u32(raw + 8, MAPP_HEADER_SIZE - 4);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* Empty image, and an entry point past the end. */
    make_header(raw, MAPP_KIND_THUMB, 0, 0, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_THUMB, 64, 0, 64, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* The Thumb entry point is called with bit 0 forced on, so the stored
       offset has to be even. */
    make_header(raw, MAPP_KIND_THUMB, 64, 0, 33, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_THUMB, 64, 0, 32, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_OK);

    /* image + bss must fit the arena; exactly the arena size is allowed. */
    make_header(raw, MAPP_KIND_THUMB, MAPP_IMAGE_ARENA_MAX, 0, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + MAPP_IMAGE_ARENA_MAX);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_OK);

    make_header(raw, MAPP_KIND_THUMB, MAPP_IMAGE_ARENA_MAX, 1, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + MAPP_IMAGE_ARENA_MAX);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_THUMB, 64, MAPP_IMAGE_ARENA_MAX, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* Relocation table is capped, and the file length must account for it. */
    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, MAPP_IMAGE_RELOC_MAX + 1);
    header = parse(raw, MAPP_HEADER_SIZE + 64 + (MAPP_IMAGE_RELOC_MAX + 1) * 4);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_THUMB, 64, 0, 0, 2);
    header = parse(raw, MAPP_HEADER_SIZE + 64 + 8);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_OK);
    header.file_size -= 1; /* truncated */
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);
    header.file_size += 2; /* trailing bytes */
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* Bytecode images may not ask for relocations or bss. */
    make_header(raw, MAPP_KIND_BYTECODE, 64, 4, 0, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_BYTECODE, 64, 0, 0, 1);
    header = parse(raw, MAPP_HEADER_SIZE + 64 + 4);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    make_header(raw, MAPP_KIND_BYTECODE, 64, 0, 64, 0);
    header = parse(raw, MAPP_HEADER_SIZE + 64);
    CHECK_EQ(mapp_image_validate(&header), MAPP_IMAGE_BAD_LAYOUT);

    /* ---- Relocation ---------------------------------------------------- */

    uint8_t image[64];
    memset(image, 0, sizeof image);
    put_u32(image + 0, 0x10); /* pointer into .bss */
    put_u32(image + 4, 64);   /* one past the last image byte */
    put_u32(image + 8, 0x20); /* pointer inside the image */

    uint32_t offsets[3] = {0, 4, 8};
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 16, offsets, 3, 0x20000000u),
             MAPP_RELOC_OK);
    CHECK_EQ(get_u32(image + 0), 0x20000010u);
    CHECK_EQ(get_u32(image + 4), 0x20000040u); /* == image + bss, still valid */
    CHECK_EQ(get_u32(image + 8), 0x20000020u);

    /* A target one byte past image + bss is rejected, and the word is left
       alone so a failing image cannot half-relocate. */
    memset(image, 0, sizeof image);
    put_u32(image + 0, 81); /* image_size(64) + bss(16) = 80 */
    put_u32(image + 4, 0);
    offsets[1] = 4;
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 16, offsets, 2, 0x20000000u),
             MAPP_RELOC_BAD_TARGET);
    CHECK_EQ(get_u32(image + 4), 0u);

    /* Unaligned offsets never reach the write. */
    memset(image, 0, sizeof image);
    uint32_t one[1] = {2};
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 0, one, 1, 0x20000000u),
             MAPP_RELOC_BAD_OFFSET);

    /* An offset at the very end of the 32-bit range used to wrap when the
       bounds were computed in 32 bits, letting the loader write far outside
       the arena. */
    one[0] = 0xFFFFFFFCu;
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 0, one, 1, 0x20000000u),
             MAPP_RELOC_BAD_OFFSET);

    /* The last word of the image is usable. */
    memset(image, 0, sizeof image);
    one[0] = 60;
    put_u32(image + 60, 0);
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 0, one, 1, 0x20000000u),
             MAPP_RELOC_OK);
    CHECK_EQ(get_u32(image + 60), 0x20000000u);

    /* No relocations is a no-op. */
    CHECK_EQ(mapp_image_apply_relocations(image, sizeof image, 0, NULL, 0, 0x20000000u),
             MAPP_RELOC_OK);
}
