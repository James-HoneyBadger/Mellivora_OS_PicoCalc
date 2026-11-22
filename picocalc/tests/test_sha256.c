/*
 * test_sha256.c — FIPS 180-4 test vectors and digest helpers.
 */

#include "core/sha256.h"
#include "test_util.h"

#include <string.h>

static void digest_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]) {
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    sha256_hex(digest, out);
}

void test_sha256(void) {
    char hex[SHA256_HEX_SIZE];

    digest_hex("", 0, hex);
    CHECK_STR_EQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    digest_hex("abc", 3, hex);
    CHECK_STR_EQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    digest_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK_STR_EQ(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* Incremental updates must match a single-shot digest. */
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, "abcdbcdecdefdefgefgh", 20);
    sha256_update(&ctx, "fghighijhijkijkljklmklmnlmnomnopnopq", 36);
    sha256_final(&ctx, digest);
    sha256_hex(digest, hex);
    CHECK_STR_EQ(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* A million 'a' also checks multi-block padding. */
    sha256_init(&ctx);
    char block[1000];
    memset(block, 'a', sizeof block);
    for (int i = 0; i < 1000; i++)
        sha256_update(&ctx, block, sizeof block);
    sha256_final(&ctx, digest);
    sha256_hex(digest, hex);
    CHECK_STR_EQ(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* Hex helpers. */
    uint8_t parsed[SHA256_DIGEST_SIZE];
    CHECK(sha256_parse_hex("00ff10", parsed) == false);
    CHECK(sha256_parse_hex("CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0",
                           parsed));
    CHECK(parsed[0] == 0xCD && parsed[31] == 0xD0);
    CHECK(sha256_hex_equal("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                           parsed));
    CHECK(!sha256_hex_equal("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd1",
                            parsed));
    CHECK(!sha256_hex_equal("nothex", parsed));
}
