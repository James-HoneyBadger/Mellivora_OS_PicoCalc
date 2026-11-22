/*
 * core/mapp_image.c — header validation and Thumb relocation for `.mapp`.
 */

#include "core/mapp_image.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

void mapp_image_parse_header(mapp_image_header_t *out, const uint8_t raw[MAPP_HEADER_SIZE]) {
    if (!out || !raw) return;
    memset(out, 0, sizeof *out);
    memcpy(out->magic, raw, sizeof out->magic);
    out->version = read_u16(raw + 4);
    out->kind = read_u16(raw + 6);
    out->header_size = read_u32(raw + 8);
    out->image_size = read_u32(raw + 12);
    out->bss_size = read_u32(raw + 16);
    out->entry_offset = read_u32(raw + 20);
    out->relocation_count = read_u32(raw + 24);
}

mapp_image_status_t mapp_image_validate(const mapp_image_header_t *header) {
    if (!header) return MAPP_IMAGE_BAD_LAYOUT;
    if (memcmp(&header->magic, "MAPP", 4) != 0) return MAPP_IMAGE_BAD_MAGIC;
    if (header->version != MAPP_FORMAT_VERSION) return MAPP_IMAGE_BAD_VERSION;
    if (header->kind != MAPP_KIND_THUMB && header->kind != MAPP_KIND_BYTECODE)
        return MAPP_IMAGE_BAD_LAYOUT;

    if (header->header_size != MAPP_HEADER_SIZE || header->image_size == 0)
        return MAPP_IMAGE_BAD_LAYOUT;
    if ((uint64_t)header->image_size + header->bss_size > MAPP_IMAGE_ARENA_MAX)
        return MAPP_IMAGE_BAD_LAYOUT;
    if (header->relocation_count > MAPP_IMAGE_RELOC_MAX) return MAPP_IMAGE_BAD_LAYOUT;
    if (header->entry_offset >= header->image_size) return MAPP_IMAGE_BAD_LAYOUT;

    uint64_t expected = (uint64_t)header->header_size +
                        (uint64_t)header->relocation_count * sizeof(uint32_t) + header->image_size;
    if (expected != header->file_size) return MAPP_IMAGE_BAD_LAYOUT;

    if (header->kind == MAPP_KIND_THUMB) {
        /* The entry point is called with the Thumb bit forced on, so the
           stored offset must already be even. */
        if ((header->entry_offset & 1u) != 0) return MAPP_IMAGE_BAD_LAYOUT;
        return MAPP_IMAGE_OK;
    }
    if (header->relocation_count != 0 || header->bss_size != 0) return MAPP_IMAGE_BAD_LAYOUT;
    return MAPP_IMAGE_OK;
}

mapp_reloc_status_t mapp_image_apply_relocations(uint8_t *image, uint32_t image_size,
                                                 uint32_t bss_size, const uint32_t *offsets,
                                                 uint32_t count, uint32_t base_address) {
    if (!image) return MAPP_RELOC_BAD_OFFSET;
    if (count != 0 && !offsets) return MAPP_RELOC_BAD_OFFSET;
    uint64_t image_total = (uint64_t)image_size + bss_size;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = offsets[i];
        if ((offset & 3u) != 0 || (uint64_t)offset + sizeof(uint32_t) > image_size)
            return MAPP_RELOC_BAD_OFFSET;
        uint32_t value = read_u32(image + offset);
        if ((uint64_t)value > image_total) return MAPP_RELOC_BAD_TARGET;
        write_u32(image + offset, value + base_address);
    }
    return MAPP_RELOC_OK;
}
