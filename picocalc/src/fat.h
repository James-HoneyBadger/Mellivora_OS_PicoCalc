#pragma once
/*
 * fat.h — Read-only FAT16/FAT32 filesystem layer for PicoCalc SD card
 *
 * Sits on top of sd.c and provides directory listing and file reading.
 * Only the features needed by the shell are implemented.
 *
 * Public API
 * ----------
 *   fat_result_t fat_mount(void)
 *       Mount the first partition (or raw BPB at LBA 0).
 *       Must be called before any other fat_ function.
 *
 *   fat_result_t fat_ls(const char *path, fat_ls_cb cb, void *ctx)
 *       Enumerate directory entries in 8.3 format.
 *       Calls cb(name, size, is_dir, ctx) for each non-deleted entry.
 *
 *   fat_result_t fat_open(const char *path, fat_file_t *f)
 *       Open a file by path (8.3 names, '/' separator).
 *
 *   int32_t fat_read(fat_file_t *f, void *buf, uint32_t n)
 *       Read up to n bytes.  Returns bytes read or negative on error.
 *
 *   FAT_OK / FAT_ERR_IO / FAT_ERR_NOTFOUND / FAT_ERR_NOTMOUNTED /
 *   FAT_ERR_NOTFILE / FAT_ERR_EOF
 */

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FAT_OK            =  0,
    FAT_ERR_IO        = -1,
    FAT_ERR_NOTFOUND  = -2,
    FAT_ERR_NOTMOUNTED= -3,
    FAT_ERR_NOTFILE   = -4,
    FAT_ERR_EOF       = -5,
    FAT_ERR_EXISTS    = -6,
    FAT_ERR_NOSPC     = -7,
    FAT_ERR_RDONLY    = -8,
    FAT_ERR_NOTEMPTY  = -9,
    FAT_ERR_UNSUPPORTED = -10,
} fat_result_t;

typedef struct {
    uint32_t first_cluster;   /* starting cluster (0 = root dir on FAT16) */
    uint32_t size;            /* file size in bytes                        */
    uint32_t cur_cluster;     /* current cluster position                  */
    uint32_t cur_offset;      /* byte offset within file                   */
} fat_file_t;

typedef struct {
    bool     mounted;
    bool     fat32;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_count;
    uint32_t used_clusters;
    uint32_t free_clusters;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
} fat_usage_t;

typedef void (*fat_ls_cb)(const char *name, uint32_t size,
                          bool is_dir, void *ctx);

fat_result_t fat_mount(void);
fat_result_t fat_is_dir(const char *path);
fat_result_t fat_ls(const char *path, fat_ls_cb cb, void *ctx);
fat_result_t fat_open(const char *path, fat_file_t *f);
int32_t      fat_read(fat_file_t *f, void *buf, uint32_t n);
fat_result_t fat_get_usage(fat_usage_t *out);

/* Write-side API */
fat_result_t fat_mkdir(const char *path);
fat_result_t fat_unlink(const char *path);
fat_result_t fat_create(const char *path, const uint8_t *data, uint32_t len);

const char  *fat_result_str(fat_result_t r);
