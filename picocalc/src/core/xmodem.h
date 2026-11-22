#pragma once
/*
 * core/xmodem.h — XMODEM and YMODEM transfer engines.
 *
 * Protocol only: all bytes move through caller-supplied callbacks, so the
 * same code runs over USB-CDC, a hardware UART, or a scripted host-test pipe.
 * Received payload is streamed through `on_data`, so file size is limited by
 * the card, not by RAM.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XM_SOH 0x01
#define XM_STX 0x02
#define XM_EOT 0x04
#define XM_ACK 0x06
#define XM_NAK 0x15
#define XM_CAN 0x18
#define XM_SUB 0x1A

#define XM_ERR_IO (-1)
#define XM_ERR_TIMEOUT (-2)
#define XM_ERR_CANCELLED (-3)
#define XM_ERR_PROTOCOL (-4)
#define XM_ERR_REJECTED (-5)

typedef struct {
    /* Return the next byte, or -1 when `timeout_ms` expires. */
    int (*getc)(void *ctx, uint32_t timeout_ms);
    /* Send one byte. Return 0 on success. */
    int (*putc)(void *ctx, uint8_t byte);
    /* Consume a received block. Return 0 to continue, non-zero to abort. */
    int (*on_data)(void *ctx, const uint8_t *data, uint32_t len);
    /* Fill `buf` for sending. Return bytes read, 0 at EOF, -1 on error. */
    int (*read)(void *ctx, void *buf, uint32_t len);
    /* Optional progress report. */
    void (*progress)(void *ctx, uint32_t done, uint32_t total);
} xm_io_t;

typedef struct {
    bool ymodem;         /* leading metadata block, batch-style termination */
    bool crc;            /* request CRC-16 mode instead of a checksum */
    int retries;         /* attempts per block, default 10 */
    uint32_t timeout_ms; /* per-byte timeout, default 10000 */
    char name[48];       /* out: metadata name when receiving */
    uint32_t size;       /* out: metadata size, 0 when not declared */
} xm_config_t;

void xm_config_default(xm_config_t *cfg);

uint16_t xm_crc16(const uint8_t *data, size_t len);
uint8_t xm_checksum(const uint8_t *data, size_t len);

/* Receive one file. Returns the byte count written, or an XM_ERR_* value. */
int xm_receive(const xm_io_t *io, void *ctx, xm_config_t *cfg);

/* Send `total` bytes pulled from `io->read`. Returns bytes sent or XM_ERR_*. */
int xm_send(const xm_io_t *io, void *ctx, const xm_config_t *cfg, uint32_t total);

#ifdef __cplusplus
}
#endif
