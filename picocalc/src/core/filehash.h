#pragma once
/*
 * core/filehash.h — SHA-256 digest of a whole file on the SD card.
 *
 * `mpkg verify` digests an installed artifact without loading it into RAM.
 * The loop belongs next to the digest helpers so the FAT end-of-file
 * convention is handled once, and so the host tests can drive it against
 * the in-memory card image.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/sha256.h"
#include "fat.h"

/* Returns true when the file was read to the end. `out_size` (optional)
   receives the number of bytes hashed. */
bool filehash_fat_file(fat_file_t *file, uint8_t digest[SHA256_DIGEST_SIZE], uint32_t *out_size);
