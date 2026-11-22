/*
 * core/sha256.c — FIPS 180-4 SHA-256.
 */

#include "core/sha256.h"

#include <string.h>

static const uint32_t _k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

static uint32_t _rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static void _compress(sha256_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = _rotr(w[i - 15], 7) ^ _rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = _rotr(w[i - 2], 17) ^ _rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t s1 = _rotr(e, 6) ^ _rotr(e, 11) ^ _rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + _k[i] + w[i];
        uint32_t s0 = _rotr(a, 2) ^ _rotr(a, 13) ^ _rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    static const uint32_t iv[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    memcpy(ctx->state, iv, sizeof iv);
    ctx->bit_count = 0;
    ctx->block_len = 0;
    memset(ctx->block, 0, sizeof ctx->block);
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
    if (!ctx || (!data && len)) return;
    const uint8_t *bytes = (const uint8_t *)data;
    ctx->bit_count += (uint64_t)len * 8u;

    while (len > 0) {
        size_t room = sizeof ctx->block - ctx->block_len;
        size_t take = len < room ? len : room;
        memcpy(ctx->block + ctx->block_len, bytes, take);
        ctx->block_len += take;
        bytes += take;
        len -= take;
        if (ctx->block_len == sizeof ctx->block) {
            _compress(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint64_t bits = ctx->bit_count;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);

    uint8_t zero = 0;
    while (ctx->block_len != 56)
        sha256_update(ctx, &zero, 1);

    uint8_t length_be[8];
    for (int i = 0; i < 8; i++)
        length_be[i] = (uint8_t)(bits >> (56 - i * 8));
    /* Feed the length directly: sha256_update would update bit_count again. */
    memcpy(ctx->block + 56, length_be, 8);
    _compress(ctx, ctx->block);
    ctx->block_len = 0;

    for (size_t i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[SHA256_HEX_SIZE]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[SHA256_HEX_LEN] = '\0';
}

static int _hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool sha256_parse_hex(const char *hex, uint8_t out[SHA256_DIGEST_SIZE]) {
    if (!hex || strlen(hex) != SHA256_HEX_LEN) return false;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        int high = _hex_nibble(hex[i * 2]);
        int low = _hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

bool sha256_hex_equal(const char *hex, const uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint8_t parsed[SHA256_DIGEST_SIZE];
    if (!sha256_parse_hex(hex, parsed)) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++)
        diff |= (uint8_t)(parsed[i] ^ digest[i]);
    return diff == 0;
}
