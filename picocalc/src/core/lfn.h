#pragma once
/*
 * lfn.h — VFAT long-filename decoding.
 *
 * Pure helpers for turning a sequence of 32-byte LFN directory entries into
 * an ASCII long name. Non-ASCII code points become '?' because the LCD font
 * is ASCII-only. Writing LFN entries is not supported.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LFN_MAX_CHARS 255
#define LFN_MAX_SLOTS 20

typedef struct {
    char text[LFN_MAX_CHARS + 1];
    size_t len;
    size_t end_pos;
    bool end_seen;
    uint8_t checksum;
    uint8_t max_seq;
    uint32_t seen_mask;
    bool active;
    bool valid;
} lfn_builder_t;

/* Standard VFAT checksum over an 11-byte 8.3 name. */
uint8_t lfn_checksum(const uint8_t short_name[11]);

void lfn_reset(lfn_builder_t *b);

/*
 * Feed one directory entry. Returns true when `raw` was an LFN entry and was
 * consumed. Non-LFN entries return false so the caller can finish the name.
 */
bool lfn_add_entry(lfn_builder_t *b, const uint8_t raw[32]);

/*
 * Validate the accumulated entries against the 8.3 short name and copy the
 * ASCII long name to `out`. Returns false when the set is incomplete, has a
 * checksum mismatch, or decodes to an empty name.
 */
bool lfn_finish(const lfn_builder_t *b, const uint8_t short_name[11], char *out, size_t cap);
