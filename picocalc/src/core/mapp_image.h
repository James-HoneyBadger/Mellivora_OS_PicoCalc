#pragma once
/*
 * core/mapp_image.h — header validation and relocation for `.mapp` images.
 *
 * The loader in mapp.c also owns the FAT driver, the RAM arena, and the
 * display, so none of it can run on the host. The two decisions that can
 * corrupt memory — "is this header self-consistent?" and "where does this
 * relocation write?" — live here instead, with no dependencies beyond the
 * on-disk format, so they are covered by the host test suite.
 */

#include <stdbool.h>
#include <stdint.h>

#include "mapp.h"

/* Largest image + bss the loader will map, and the relocation table cap.
   Both are RAM budgets, not format limits. */
#define MAPP_IMAGE_ARENA_MAX 16384u
#define MAPP_IMAGE_RELOC_MAX 512u

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t kind;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t relocation_count;
    /* Size of the whole file on the card, used to reject trailing or
       truncated data. Supplied by the caller from the directory entry. */
    uint32_t file_size;
} mapp_image_header_t;

typedef enum {
    MAPP_IMAGE_OK = 0,
    MAPP_IMAGE_BAD_MAGIC,
    MAPP_IMAGE_BAD_VERSION,
    MAPP_IMAGE_BAD_LAYOUT,
} mapp_image_status_t;

/* Decode the little-endian on-disk header. Does not validate it. */
void mapp_image_parse_header(mapp_image_header_t *out, const uint8_t raw[MAPP_HEADER_SIZE]);

/* Check the magic and version, then every size, offset, and count against
   the arena and the file length. */
mapp_image_status_t mapp_image_validate(const mapp_image_header_t *header);

typedef enum {
    MAPP_RELOC_OK = 0,
    MAPP_RELOC_BAD_OFFSET, /* unaligned, or outside the image */
    MAPP_RELOC_BAD_TARGET, /* points outside image + bss */
} mapp_reloc_status_t;

/* Add `base_address` to each relocated word. Offsets and targets are checked
   against 64-bit sums so a crafted header cannot wrap past the arena. */
mapp_reloc_status_t mapp_image_apply_relocations(uint8_t *image, uint32_t image_size,
                                                 uint32_t bss_size, const uint32_t *offsets,
                                                 uint32_t count, uint32_t base_address);
