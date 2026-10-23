/*
 * lfn.c — VFAT long-filename decoding.
 */

#include "core/lfn.h"

#include <string.h>

uint8_t lfn_checksum(const uint8_t short_name[11]) {
    uint8_t sum = 0;

    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

void lfn_reset(lfn_builder_t *b) {
    if (!b) return;
    memset(b, 0, sizeof *b);
    b->valid = true;
}

bool lfn_add_entry(lfn_builder_t *b, const uint8_t raw[32]) {
    if (!b || !raw || raw[11] != 0x0F) return false;

    uint8_t seq = (uint8_t)(raw[0] & 0x1F);
    bool last = (raw[0] & 0x40) != 0;

    if (seq == 0 || seq > LFN_MAX_SLOTS) {
        b->valid = false;
        return true;
    }

    if (last) {
        lfn_reset(b);
        b->active = true;
        b->max_seq = seq;
    }

    if (!b->active || seq > b->max_seq) {
        b->valid = false;
        return true;
    }

    if (b->seen_mask == 0) {
        b->checksum = raw[13];
    } else if (b->checksum != raw[13]) {
        b->valid = false;
    }

    if (b->seen_mask & (1u << seq)) b->valid = false;
    b->seen_mask |= 1u << seq;

    /* UTF-16 code-unit byte offsets within an LFN entry. */
    static const uint8_t offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    size_t base = (size_t)(seq - 1) * 13;

    for (int k = 0; k < 13; k++) {
        uint16_t unit = (uint16_t)(raw[offsets[k]] | (raw[offsets[k] + 1] << 8));
        size_t at = base + (size_t)k;

        if (unit == 0x0000) {
            if (!b->end_seen || at < b->end_pos) {
                b->end_pos = at;
                b->end_seen = true;
            }
            break;
        }
        if (unit == 0xFFFF) continue;

        if (at >= LFN_MAX_CHARS) {
            b->valid = false;
            break;
        }

        b->text[at] = (unit >= 0x20 && unit <= 0x7E) ? (char)unit : '?';
        if (at + 1 > b->len) b->len = at + 1;
    }

    return true;
}

bool lfn_finish(const lfn_builder_t *b, const uint8_t short_name[11], char *out, size_t cap) {
    if (!b || !short_name || !out || cap == 0) return false;
    if (!b->active || !b->valid || b->max_seq == 0) return false;

    uint32_t needed = (1u << (b->max_seq + 1)) - 2u; /* bits 1..max_seq */
    if ((b->seen_mask & needed) != needed) return false;
    if (lfn_checksum(short_name) != b->checksum) return false;

    size_t len = b->end_seen ? b->end_pos : b->len;
    if (len >= cap) len = cap - 1;
    if (len == 0) return false;

    memcpy(out, b->text, len);
    out[len] = '\0';
    return true;
}
