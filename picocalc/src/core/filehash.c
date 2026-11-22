/*
 * core/filehash.c — SHA-256 digest of a whole file on the SD card.
 */

#include "core/filehash.h"

#define FILEHASH_CHUNK 256u

bool filehash_fat_file(fat_file_t *file, uint8_t digest[SHA256_DIGEST_SIZE], uint32_t *out_size) {
    if (!file || !digest) return false;

    sha256_ctx_t hash;
    sha256_init(&hash);
    uint8_t chunk[FILEHASH_CHUNK];
    uint32_t total = 0;

    for (;;) {
        int32_t n = fat_read(file, chunk, sizeof chunk);
        /* Reaching the end of a file is the normal exit; any other failure
           means the digest would cover a truncated file. */
        if (n == FAT_ERR_EOF) break;
        if (n < 0) return false;
        if (n == 0) break;
        sha256_update(&hash, chunk, (size_t)n);
        total += (uint32_t)n;
    }

    sha256_final(&hash, digest);
    if (out_size) *out_size = total;
    return true;
}
