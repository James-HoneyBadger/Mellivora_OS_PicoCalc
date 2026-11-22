#pragma once
/*
 * core/sha256.h — SHA-256 for package integrity checks.
 *
 * Self-contained so package verification does not depend on the TLS
 * configuration, and so it can be unit-tested on the host.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_LEN ((size_t)SHA256_DIGEST_SIZE * 2u)
#define SHA256_HEX_SIZE (SHA256_HEX_LEN + 1u)

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[SHA256_HEX_SIZE]);

/* Parse a 64-character hex digest. Accepts upper or lower case. */
bool sha256_parse_hex(const char *hex, uint8_t out[SHA256_DIGEST_SIZE]);

/* Constant-time comparison of a hex digest string against a raw digest. */
bool sha256_hex_equal(const char *hex, const uint8_t digest[SHA256_DIGEST_SIZE]);
