/*
 * test_xmodem.c — CRC/checksum helpers and scripted protocol sessions.
 *
 * Both engines run against a canned byte script, so the whole XMODEM/YMODEM
 * state machine is exercised without threads or real timing.
 */

#include "core/xmodem.h"
#include "test_util.h"

#include <string.h>

#define SCRIPT_MAX 4096

typedef struct {
    uint8_t script[SCRIPT_MAX];
    size_t script_len;
    size_t script_pos;
    uint8_t sent[SCRIPT_MAX];
    size_t sent_len;
    uint32_t received;
    int on_data_calls;
    uint8_t payload[2048];
    /* Outgoing payload consumed by io->read during a send. */
    uint8_t data[2048];
    size_t data_len;
    size_t data_pos;
} xm_fixture_t;

static int _getc(void *ctx, uint32_t timeout_ms) {
    (void)timeout_ms;
    xm_fixture_t *fixture = (xm_fixture_t *)ctx;
    if (fixture->script_pos >= fixture->script_len) return -1;
    return fixture->script[fixture->script_pos++];
}

static int _putc(void *ctx, uint8_t byte) {
    xm_fixture_t *fixture = (xm_fixture_t *)ctx;
    if (fixture->sent_len < sizeof fixture->sent) fixture->sent[fixture->sent_len++] = byte;
    return 0;
}

static int _on_data(void *ctx, const uint8_t *data, uint32_t len) {
    xm_fixture_t *fixture = (xm_fixture_t *)ctx;
    fixture->on_data_calls++;
    for (uint32_t i = 0; i < len; i++) {
        if (fixture->received < sizeof fixture->payload)
            fixture->payload[fixture->received++] = data[i];
    }
    return 0;
}

static int _read(void *ctx, void *buf, uint32_t len) {
    xm_fixture_t *fixture = (xm_fixture_t *)ctx;
    size_t remaining = fixture->data_len - fixture->data_pos;
    size_t take = len < remaining ? len : remaining;
    memcpy(buf, fixture->data + fixture->data_pos, take);
    fixture->data_pos += take;
    return (int)take;
}

static void _fixture(xm_fixture_t *fixture) {
    memset(fixture, 0, sizeof *fixture);
}

static void _script_add(xm_fixture_t *fixture, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len && fixture->script_len < SCRIPT_MAX; i++)
        fixture->script[fixture->script_len++] = data[i];
}

static void _script_byte(xm_fixture_t *fixture, uint8_t byte) {
    _script_add(fixture, &byte, 1);
}

/* Build a complete XMODEM/YMODEM packet into the script. */
static void _script_packet(xm_fixture_t *fixture, int block, const uint8_t *data, int len, bool crc,
                           bool corrupt) {
    _script_byte(fixture, len > 128 ? XM_STX : XM_SOH);
    _script_byte(fixture, (uint8_t)block);
    _script_byte(fixture, (uint8_t)(~block));
    _script_add(fixture, data, (size_t)len);
    if (crc) {
        uint16_t value = xm_crc16(data, (size_t)len);
        if (corrupt) value ^= 0xFFFFu;
        _script_byte(fixture, (uint8_t)(value >> 8));
        _script_byte(fixture, (uint8_t)(value & 0xFF));
    } else {
        uint8_t value = xm_checksum(data, (size_t)len);
        if (corrupt) value = (uint8_t)(value + 1);
        _script_byte(fixture, value);
    }
}

static size_t _count_byte(const xm_fixture_t *fixture, uint8_t byte) {
    size_t total = 0;
    for (size_t i = 0; i < fixture->sent_len; i++) {
        if (fixture->sent[i] == byte) total++;
    }
    return total;
}

void test_xmodem(void) {
    /* Published XMODEM CRC of "123456789". */
    const uint8_t check[] = "123456789";
    CHECK_EQ(xm_crc16(check, sizeof check - 1), 0x31C3);
    CHECK_EQ(xm_crc16(NULL, 0), 0);
    CHECK_EQ(xm_checksum(check, sizeof check - 1), 0xDD);

    xm_config_t cfg;
    xm_config_default(&cfg);
    CHECK(cfg.crc);
    CHECK_EQ(cfg.retries, 10);
    CHECK_EQ(cfg.timeout_ms, 10000u);
    CHECK(!cfg.ymodem);

    /* ---- receive: one good block then EOT ---- */
    xm_fixture_t fixture;
    _fixture(&fixture);
    uint8_t block1[128];
    memset(block1, 'A', sizeof block1);
    _script_packet(&fixture, 1, block1, 128, true, false);
    _script_byte(&fixture, XM_EOT);

    xm_io_t io = {.getc = _getc, .putc = _putc, .on_data = _on_data, .read = _read};
    cfg.crc = true;
    cfg.timeout_ms = 50;
    int received = xm_receive(&io, &fixture, &cfg);
    CHECK_EQ(received, 128);
    CHECK_EQ(fixture.on_data_calls, 1);
    CHECK_EQ(fixture.received, 128u);
    CHECK(fixture.payload[0] == 'A' && fixture.payload[127] == 'A');
    CHECK_EQ(fixture.sent[0], 'C');             /* requested CRC mode */
    CHECK_EQ(_count_byte(&fixture, XM_ACK), 2); /* block + EOT */
    CHECK_EQ(_count_byte(&fixture, XM_NAK), 0);

    /* ---- receive: checksum mode ---- */
    _fixture(&fixture);
    _script_packet(&fixture, 1, block1, 128, false, false);
    _script_byte(&fixture, XM_EOT);
    cfg.crc = false;
    received = xm_receive(&io, &fixture, &cfg);
    CHECK_EQ(received, 128);
    CHECK_EQ(fixture.sent[0], XM_NAK);

    /* ---- receive: corrupt block is NAKed, then a retry succeeds ---- */
    _fixture(&fixture);
    _script_packet(&fixture, 1, block1, 128, true, true);
    _script_packet(&fixture, 1, block1, 128, true, false);
    _script_byte(&fixture, XM_EOT);
    cfg.crc = true;
    received = xm_receive(&io, &fixture, &cfg);
    CHECK_EQ(received, 128);
    CHECK_EQ(_count_byte(&fixture, XM_NAK), 1);
    CHECK_EQ(_count_byte(&fixture, XM_ACK), 2);
    CHECK_EQ(fixture.on_data_calls, 1);

    /* ---- receive: duplicate block is acknowledged but stored once ---- */
    _fixture(&fixture);
    _script_packet(&fixture, 1, block1, 128, true, false);
    _script_packet(&fixture, 1, block1, 128, true, false);
    _script_byte(&fixture, XM_EOT);
    received = xm_receive(&io, &fixture, &cfg);
    CHECK_EQ(received, 128);
    CHECK_EQ(fixture.on_data_calls, 1);
    CHECK_EQ(_count_byte(&fixture, XM_ACK), 3);

    /* ---- receive: cancellation ---- */
    _fixture(&fixture);
    _script_byte(&fixture, XM_CAN);
    CHECK_EQ(xm_receive(&io, &fixture, &cfg), XM_ERR_CANCELLED);

    /* ---- receive: silence times out ---- */
    _fixture(&fixture);
    cfg.retries = 2;
    CHECK_EQ(xm_receive(&io, &fixture, &cfg), XM_ERR_TIMEOUT);

    /* ---- receive: callback refusal aborts and cancels ---- */
    _fixture(&fixture);
    _script_packet(&fixture, 1, block1, 128, true, false);
    xm_io_t refusing = io;
    refusing.on_data = NULL;
    CHECK_EQ(xm_receive(&refusing, &fixture, &cfg), XM_ERR_IO);

    /* ---- receive: YMODEM metadata block ---- */
    _fixture(&fixture);
    uint8_t header[128];
    memset(header, 0, sizeof header);
    memcpy(header, "LOG.TXT", 7);
    memcpy(header + 8, "128", 3);
    _script_packet(&fixture, 0, header, 128, true, false);
    _script_packet(&fixture, 1, block1, 128, true, false);
    _script_byte(&fixture, XM_EOT); /* ACK + 'C' */
    _script_byte(&fixture, XM_EOT); /* end of batch */
    cfg.ymodem = true;
    cfg.retries = 10;
    received = xm_receive(&io, &fixture, &cfg);
    CHECK_EQ(received, 128);
    CHECK_STR_EQ(cfg.name, "LOG.TXT");
    CHECK_EQ(cfg.size, 128u);
    CHECK_EQ(fixture.on_data_calls, 1);

    /* ---- send: 200 bytes become two padded 128-byte blocks ---- */
    uint8_t source[200];
    for (size_t i = 0; i < sizeof source; i++)
        source[i] = (uint8_t)('a' + (i % 26));

    xm_fixture_t sender;
    _fixture(&sender);
    memcpy(sender.data, source, sizeof source);
    sender.data_len = sizeof source;
    /* Receiver script: 'C', ACK block 1, ACK block 2, ACK for EOT. */
    _script_byte(&sender, 'C');
    _script_byte(&sender, XM_ACK);
    _script_byte(&sender, XM_ACK);
    _script_byte(&sender, XM_ACK);

    xm_io_t send_io = {.getc = _getc, .putc = _putc, .on_data = NULL, .read = _read};
    cfg.ymodem = false;
    cfg.crc = true;
    int sent = xm_send(&send_io, &sender, &cfg, (uint32_t)sizeof source);
    CHECK_EQ(sent, 200);

    /* First emitted byte is SOH, then block 1 and its complement. */
    CHECK_EQ(sender.sent[0], XM_SOH);
    CHECK_EQ(sender.sent[1], 1);
    CHECK_EQ(sender.sent[2], 0xFE);
    CHECK_EQ(sender.sent[3], source[0]);
    CHECK_EQ(sender.sent[3 + 127], source[127]);
    /* Second block starts after 3 + 128 + 2 bytes and pads with 0x1A. */
    size_t second = 3 + 128 + 2;
    CHECK_EQ(sender.sent[second], XM_SOH);
    CHECK_EQ(sender.sent[second + 1], 2);
    CHECK_EQ(sender.sent[second + 2], 0xFD);
    CHECK_EQ(sender.sent[second + 3], source[128]);
    CHECK_EQ(sender.sent[second + 3 + 71], source[199]);
    CHECK_EQ(sender.sent[second + 3 + 72], XM_SUB);
    CHECK_EQ(sender.sent[second + 3 + 127], XM_SUB);
    CHECK_EQ(sender.sent[sender.sent_len - 1], XM_EOT);

    /* The emitted CRC trailer must validate against the payload. */
    uint8_t payload[128];
    memcpy(payload, sender.sent + 3, sizeof payload);
    uint16_t expected_crc = xm_crc16(payload, sizeof payload);
    size_t crc_at = 3 + 128;
    CHECK_EQ(sender.sent[crc_at], (uint8_t)(expected_crc >> 8));
    CHECK_EQ(sender.sent[crc_at + 1], (uint8_t)(expected_crc & 0xFF));

    /* ---- send: receiver never answers ---- */
    _fixture(&sender);
    CHECK_EQ(xm_send(&send_io, &sender, &cfg, 8), XM_ERR_TIMEOUT);

    /* ---- send: YMODEM announces the name and size first ---- */
    _fixture(&sender);
    memcpy(sender.data, "hi", 2);
    sender.data_len = 2;
    _script_byte(&sender, 'C');    /* request */
    _script_byte(&sender, XM_ACK); /* metadata block */
    _script_byte(&sender, 'C');    /* ready for data */
    _script_byte(&sender, XM_ACK); /* data block */
    _script_byte(&sender, XM_ACK); /* EOT */
    _script_byte(&sender, 'C');    /* next file? */
    _script_byte(&sender, XM_ACK); /* terminating block 0 */
    xm_config_t ycfg;
    xm_config_default(&ycfg);
    ycfg.ymodem = true;
    strcpy(ycfg.name, "A.TXT");
    CHECK_EQ(xm_send(&send_io, &sender, &ycfg, 2), 2);
    CHECK_EQ(sender.sent[0], XM_SOH);
    CHECK_EQ(sender.sent[1], 0);
    CHECK_EQ(sender.sent[2], 0xFF);
    CHECK(memcmp(sender.sent + 3, "A.TXT", 5) == 0);
    CHECK_EQ(sender.sent[3 + 6], '2'); /* size follows the name's NUL */
}
