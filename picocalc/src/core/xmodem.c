/*
 * core/xmodem.c — XMODEM and YMODEM.
 *
 * XMODEM: 128-byte blocks with either an 8-bit checksum or a CRC-16 trailer.
 * YMODEM: an extra block 0 carrying "name\0size\0", 1024-byte data blocks
 * when the sender uses STX, and a terminating empty block 0.
 */

#include "core/xmodem.h"

#include <stdio.h>
#include <string.h>

void xm_config_default(xm_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);
    cfg->crc = true;
    cfg->retries = 10;
    cfg->timeout_ms = 10000;
}

uint16_t xm_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t xm_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    if (!data) return sum;
    for (size_t i = 0; i < len; i++)
        sum = (uint8_t)(sum + data[i]);
    return sum;
}

static int _put(const xm_io_t *io, void *ctx, uint8_t byte) {
    return io->putc ? io->putc(ctx, byte) : -1;
}

static bool _send_packet(const xm_io_t *io, void *ctx, const uint8_t *data, int len, int block,
                         bool crc) {
    if (_put(io, ctx, len > 128 ? XM_STX : XM_SOH) != 0) return false;
    if (_put(io, ctx, (uint8_t)(block & 0xFF)) != 0) return false;
    if (_put(io, ctx, (uint8_t)(~block & 0xFF)) != 0) return false;
    for (int i = 0; i < len; i++) {
        if (_put(io, ctx, data[i]) != 0) return false;
    }
    if (crc) {
        uint16_t value = xm_crc16(data, (size_t)len);
        if (_put(io, ctx, (uint8_t)(value >> 8)) != 0) return false;
        if (_put(io, ctx, (uint8_t)(value & 0xFF)) != 0) return false;
    } else {
        if (_put(io, ctx, xm_checksum(data, (size_t)len)) != 0) return false;
    }
    return true;
}

/* Read the remainder of a packet whose header byte was already consumed. */
static bool _read_packet(const xm_io_t *io, void *ctx, uint32_t timeout, int header, uint8_t *data,
                         int *out_len, int *out_block, bool crc) {
    int len = header == XM_STX ? 1024 : 128;
    int block = io->getc(ctx, timeout);
    int inverted = io->getc(ctx, timeout);
    if (block < 0 || inverted < 0) return false;
    if (((block ^ inverted) & 0xFF) != 0xFF) return false;

    for (int i = 0; i < len; i++) {
        int byte = io->getc(ctx, timeout);
        if (byte < 0) return false;
        data[i] = (uint8_t)byte;
    }

    if (crc) {
        int high = io->getc(ctx, timeout);
        int low = io->getc(ctx, timeout);
        if (high < 0 || low < 0) return false;
        uint16_t received = (uint16_t)(((uint16_t)high << 8) | (uint16_t)low);
        if (xm_crc16(data, (size_t)len) != received) return false;
    } else {
        int received = io->getc(ctx, timeout);
        if (received < 0) return false;
        if (xm_checksum(data, (size_t)len) != (uint8_t)received) return false;
    }

    *out_len = len;
    *out_block = block & 0xFF;
    return true;
}

/* YMODEM block 0: "name\0size\0". Returns true when a file was announced. */
static bool _parse_metadata(const uint8_t *data, int len, xm_config_t *cfg) {
    size_t name_len = 0;
    while (name_len < (size_t)len && data[name_len] != 0)
        name_len++;
    if (name_len == 0 || name_len >= sizeof cfg->name) return false;

    memcpy(cfg->name, data, name_len);
    cfg->name[name_len] = '\0';

    cfg->size = 0;
    size_t pos = name_len + 1;
    uint32_t value = 0;
    bool digits = false;
    while (pos < (size_t)len && data[pos] >= '0' && data[pos] <= '9') {
        if (value < 100000000u) value = value * 10u + (uint32_t)(data[pos] - '0');
        pos++;
        digits = true;
    }
    if (digits) cfg->size = value;
    return true;
}

int xm_receive(const xm_io_t *io, void *ctx, xm_config_t *cfg) {
    if (!io || !io->getc || !io->putc || !io->on_data || !cfg) return XM_ERR_IO;

    xm_config_t local;
    if (cfg->retries <= 0 || cfg->timeout_ms == 0) {
        xm_config_default(&local);
        local.ymodem = cfg->ymodem;
        local.crc = cfg->crc;
        if (cfg->retries > 0) local.retries = cfg->retries;
        if (cfg->timeout_ms) local.timeout_ms = cfg->timeout_ms;
        cfg = &local;
    }

    uint32_t timeout = cfg->timeout_ms;
    int retries = cfg->retries;
    bool crc = cfg->crc;
    uint8_t request = (uint8_t)(crc ? 'C' : XM_NAK);

    uint8_t buffer[1024];
    uint32_t total = 0;
    int expected = 1; /* XMODEM data blocks start at 1 */
    int failures = 0;
    bool want_metadata = cfg->ymodem;

    if (_put(io, ctx, request) != 0) return XM_ERR_IO;

    for (;;) {
        int header = io->getc(ctx, timeout);
        if (header < 0) {
            if (++failures > retries) return XM_ERR_TIMEOUT;
            if (_put(io, ctx, request) != 0) return XM_ERR_IO;
            continue;
        }

        if (header == XM_CAN) return XM_ERR_CANCELLED;

        if (header == XM_EOT) {
            if (_put(io, ctx, XM_ACK) != 0) return XM_ERR_IO;
            if (!cfg->ymodem) return (int)total;

            /* YMODEM batch: ask for the next file, or finish on EOT. */
            if (_put(io, ctx, 'C') != 0) return XM_ERR_IO;
            int next = io->getc(ctx, timeout);
            if (next == XM_EOT) {
                (void)_put(io, ctx, XM_ACK);
                return (int)total;
            }
            if (next != XM_SOH && next != XM_STX) return (int)total;

            int len = 0;
            int block = 0;
            if (!_read_packet(io, ctx, timeout, next, buffer, &len, &block, crc)) {
                (void)_put(io, ctx, XM_NAK);
                return (int)total;
            }
            if (block == 0) {
                (void)_put(io, ctx, XM_ACK);
                (void)_put(io, ctx, 'C');
                want_metadata = true;
                expected = 0;
                continue;
            }
            return (int)total;
        }

        if (header != XM_SOH && header != XM_STX) {
            if (++failures > retries) return XM_ERR_PROTOCOL;
            (void)_put(io, ctx, XM_NAK);
            continue;
        }

        int len = 0;
        int block = 0;
        if (!_read_packet(io, ctx, timeout, header, buffer, &len, &block, crc)) {
            if (++failures > retries) return XM_ERR_PROTOCOL;
            (void)_put(io, ctx, XM_NAK);
            continue;
        }

        if (block == 0 && want_metadata) {
            if (!_parse_metadata(buffer, len, cfg)) {
                (void)_put(io, ctx, XM_CAN);
                (void)_put(io, ctx, XM_CAN);
                return XM_ERR_PROTOCOL;
            }
            want_metadata = false;
            expected = 1;
            failures = 0;
            if (_put(io, ctx, XM_ACK) != 0) return XM_ERR_IO;
            if (_put(io, ctx, 'C') != 0) return XM_ERR_IO;
            continue;
        }

        if (block == (expected & 0xFF)) {
            if (io->on_data(ctx, buffer, (uint32_t)len) != 0) {
                (void)_put(io, ctx, XM_CAN);
                (void)_put(io, ctx, XM_CAN);
                return XM_ERR_REJECTED;
            }
            total += (uint32_t)len;
            expected = (expected + 1) & 0xFF;
            failures = 0;
            if (_put(io, ctx, XM_ACK) != 0) return XM_ERR_IO;
            if (io->progress) io->progress(ctx, total, cfg->size);
        } else if (block == ((expected - 1) & 0xFF)) {
            /* Duplicate of the last accepted block: acknowledge, do not store. */
            if (_put(io, ctx, XM_ACK) != 0) return XM_ERR_IO;
        } else {
            if (++failures > retries) return XM_ERR_PROTOCOL;
            if (_put(io, ctx, XM_NAK) != 0) return XM_ERR_IO;
        }
    }
}

int xm_send(const xm_io_t *io, void *ctx, const xm_config_t *cfg, uint32_t total) {
    if (!io || !io->getc || !io->putc || !io->read || !cfg) return XM_ERR_IO;

    uint32_t timeout = cfg->timeout_ms ? cfg->timeout_ms : 10000;
    int retries = cfg->retries > 0 ? cfg->retries : 10;
    bool crc = cfg->crc;
    uint8_t buffer[1024];

    /* Wait for the receiver's request: 'C' for CRC or NAK for checksum. */
    int request = io->getc(ctx, timeout);
    if (request < 0) return XM_ERR_TIMEOUT;
    if (request == XM_CAN) return XM_ERR_CANCELLED;
    if (request == 'C') crc = true;
    else if (request == XM_NAK)
        crc = false;

    int block = 1;
    if (cfg->ymodem) {
        memset(buffer, 0, 128);
        const char *name = cfg->name[0] ? cfg->name : "FILE.BIN";
        size_t name_len = strlen(name);
        if (name_len > 100) name_len = 100;
        for (size_t i = 0; i < name_len; i++)
            buffer[i] = (uint8_t)name[i];
        char size_text[24];
        int size_len = snprintf(size_text, sizeof size_text, "%lu", (unsigned long)total);
        if (size_len > 0) {
            for (int i = 0; i < size_len; i++)
                buffer[name_len + 1 + (size_t)i] = (uint8_t)size_text[i];
        }

        bool acknowledged = false;
        for (int attempt = 0; attempt <= retries && !acknowledged; attempt++) {
            if (!_send_packet(io, ctx, buffer, 128, 0, crc)) return XM_ERR_IO;
            int reply = io->getc(ctx, timeout);
            if (reply == XM_ACK) acknowledged = true;
            else if (reply == XM_CAN)
                return XM_ERR_CANCELLED;
        }
        if (!acknowledged) return XM_ERR_TIMEOUT;

        int next = io->getc(ctx, timeout);
        if (next == 'C') crc = true;
        else if (next == XM_NAK)
            crc = false;
        else if (next < 0)
            return XM_ERR_TIMEOUT;
    }

    /* Packets are always sent at full length: 128 bytes for short files,
       1024 once the transfer is large enough to benefit. */
    int block_size = total > 1024u ? 1024 : 128;

    uint32_t sent = 0;
    uint32_t remaining = total;
    while (remaining > 0) {
        int want = remaining > (uint32_t)block_size ? block_size : (int)remaining;
        int got = io->read(ctx, buffer, (uint32_t)want);
        if (got <= 0) return sent > 0 ? (int)sent : XM_ERR_IO;
        memset(buffer + got, XM_SUB, (size_t)block_size - (size_t)got);

        bool acknowledged = false;
        for (int attempt = 0; attempt <= retries && !acknowledged; attempt++) {
            if (!_send_packet(io, ctx, buffer, block_size, block, crc)) return XM_ERR_IO;
            int reply = io->getc(ctx, timeout);
            if (reply == XM_ACK) acknowledged = true;
            else if (reply == XM_CAN)
                return XM_ERR_CANCELLED;
            else if (reply < 0)
                return XM_ERR_TIMEOUT;
        }
        if (!acknowledged) return XM_ERR_TIMEOUT;

        sent += (uint32_t)got;
        remaining -= (uint32_t)got;
        block = (block + 1) & 0xFF;
        if (io->progress) io->progress(ctx, sent, total);
    }

    for (int attempt = 0; attempt <= retries; attempt++) {
        if (_put(io, ctx, XM_EOT) != 0) return XM_ERR_IO;
        int reply = io->getc(ctx, timeout);
        if (reply == XM_ACK) break;
        if (reply == XM_CAN) return XM_ERR_CANCELLED;
        if (attempt == retries) return XM_ERR_TIMEOUT;
    }

    if (cfg->ymodem) {
        /* Empty block 0 closes the batch. */
        memset(buffer, 0, 128);
        int request2 = io->getc(ctx, timeout);
        if (request2 == 'C' || request2 == XM_NAK) {
            for (int attempt = 0; attempt <= retries; attempt++) {
                if (!_send_packet(io, ctx, buffer, 128, 0, crc)) return XM_ERR_IO;
                int reply = io->getc(ctx, timeout);
                if (reply == XM_ACK) break;
                if (reply == XM_CAN) return XM_ERR_CANCELLED;
            }
        }
    }
    return (int)sent;
}
