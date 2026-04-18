/*
 * fat.c — Read-only FAT16/FAT32 filesystem driver for PicoCalc SD card
 *
 * Supports:
 *   - MBR partition table (reads first partition, or sector 0 if no MBR)
 *   - FAT16 and FAT32 (detected from BPB)
 *   - 8.3 file names only (upper-case, space-padded)
 *   - Subdirectory traversal
 *   - Sequential file reading
 *
 * NOT supported:
 *   - LFN (long file names)
 *   - Write operations
 *   - Multiple open files simultaneously
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fat.h"
#include "sd.h"

/* ------------------------------------------------------------------
 * On-disk structures (little-endian, packed)
 * ------------------------------------------------------------------ */

#define MBR_SIGNATURE   0xAA55
#define FAT_ATTR_DIR    0x10
#define FAT_ATTR_VOLUME 0x08
#define FAT_EOC16       0xFFF8u
#define FAT_EOC32       0x0FFFFFF8u
#define DATA_START_TOK  0xFE   /* SD data token — reuse name from sd.c scope */

/* MBR partition entry at offset 0x1BE */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} mbr_part_t;

/* BIOS Parameter Block (common fields) */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;   /* 0 for FAT32 */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;        /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} bpb_t;

/* 32-byte FAT directory entry */
typedef struct __attribute__((packed)) {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved[8];
    uint16_t first_cluster_hi;  /* FAT32 only */
    uint8_t  mtime[2];
    uint8_t  mdate[2];
    uint16_t first_cluster_lo;
    uint32_t file_size;
} dirent_t;

/* ------------------------------------------------------------------
 * Filesystem state
 * ------------------------------------------------------------------ */

static bool     _mounted        = false;
static bool     _fat32          = false;
static uint32_t _part_lba       = 0;   /* LBA of BPB sector             */
static uint32_t _fat_lba        = 0;   /* LBA of FAT #1                 */
static uint32_t _root_lba       = 0;   /* LBA of root dir (FAT16)       */
static uint32_t _data_lba       = 0;   /* LBA of cluster 2              */
static uint32_t _root_cluster   = 0;   /* root dir cluster (FAT32)      */
static uint8_t  _spc            = 0;   /* sectors per cluster           */
static uint16_t _root_entries   = 0;   /* max root entries (FAT16)      */
static uint32_t _fat_size       = 0;   /* FAT size in sectors           */
static uint32_t _cluster_count  = 0;   /* total data clusters           */

static uint8_t  _sector_buf[512];

/* ------------------------------------------------------------------
 * Low-level helpers
 * ------------------------------------------------------------------ */

static fat_result_t read_sector(uint32_t lba) {
    if (sd_read_block(lba, _sector_buf) != SD_OK) return FAT_ERR_IO;
    return FAT_OK;
}

static fat_result_t write_sector(uint32_t lba) {
    if (sd_write_block(lba, _sector_buf) != SD_OK) return FAT_ERR_IO;
    return FAT_OK;
}

static uint32_t cluster_lba(uint32_t cluster) {
    return _data_lba + (uint32_t)(cluster - 2) * _spc;
}

static uint32_t fat_entry(uint32_t cluster) {
    /* Read the FAT sector that contains this cluster's entry */
    uint32_t offset, lba;
    uint32_t val = 0;
    if (_fat32) {
        offset = cluster * 4;
        lba    = _fat_lba + offset / 512;
        if (read_sector(lba) != FAT_OK) return 0x0FFFFFFF; /* force EOC */
        uint32_t idx = offset % 512;
        val  = (uint32_t)_sector_buf[idx];
        val |= (uint32_t)_sector_buf[idx + 1] << 8;
        val |= (uint32_t)_sector_buf[idx + 2] << 16;
        val |= (uint32_t)_sector_buf[idx + 3] << 24;
        val &= 0x0FFFFFFF;
    } else {
        offset = cluster * 2;
        lba    = _fat_lba + offset / 512;
        if (read_sector(lba) != FAT_OK) return 0xFFFF;
        uint32_t idx = offset % 512;
        val  = (uint32_t)_sector_buf[idx];
        val |= (uint32_t)_sector_buf[idx + 1] << 8;
    }
    return val;
}

static bool is_eoc(uint32_t entry) {
    if (_fat32) return entry >= FAT_EOC32;
    return (uint16_t)entry >= (uint16_t)FAT_EOC16;
}

/* ------------------------------------------------------------------
 * 8.3 name utilities
 * ------------------------------------------------------------------ */

/* Convert raw 8+3 fields to "NAME.EXT" or "NAME" (no trailing space). */
static void format_83(const uint8_t *name8, const uint8_t *ext3, char *out) {
    int i, len = 0;
    for (i = 0; i < 8; i++) {
        if (name8[i] == ' ') break;
        out[len++] = (char)name8[i];
    }
    /* Check if extension is non-empty */
    bool has_ext = false;
    for (i = 0; i < 3; i++) if (ext3[i] != ' ') { has_ext = true; break; }
    if (has_ext) {
        out[len++] = '.';
        for (i = 0; i < 3; i++) {
            if (ext3[i] == ' ') break;
            out[len++] = (char)ext3[i];
        }
    }
    out[len] = '\0';
}

/* Convert path component to upper-case 8+3 fields (space-padded). */
static void to_83(const char *component, uint8_t *name8, uint8_t *ext3) {
    memset(name8, ' ', 8);
    memset(ext3,  ' ', 3);
    int ni = 0, ei = 0;
    bool in_ext = false;
    for (const char *p = component; *p; p++) {
        if (*p == '.') { in_ext = true; continue; }
        if (in_ext) {
            if (ei < 3) ext3[ei++] = (uint8_t)toupper((unsigned char)*p);
        } else {
            if (ni < 8) name8[ni++] = (uint8_t)toupper((unsigned char)*p);
        }
    }
}

/* ------------------------------------------------------------------
 * Directory iteration
 * ------------------------------------------------------------------ */

/*
 * Walk a directory (by cluster for FAT32, by fixed LBA for FAT16 root).
 * Calls visit() for every valid, non-deleted entry.
 * visit() returns true to continue, false to stop.
 */
typedef bool (*dir_visitor)(const dirent_t *e, void *ctx);

static fat_result_t walk_dir(uint32_t cluster, bool is_root16,
                              dir_visitor visit, void *ctx) {
    uint32_t lba, sector, max_sectors;

    if (is_root16) {
        lba         = _root_lba;
        max_sectors = (_root_entries * 32 + 511) / 512;
        for (sector = 0; sector < max_sectors; sector++) {
            fat_result_t r = read_sector(lba + sector);
            if (r != FAT_OK) return r;
            for (int i = 0; i < 512 / 32; i++) {
                const dirent_t *e = (const dirent_t *)(_sector_buf + i * 32);
                if (e->name[0] == 0x00) return FAT_OK; /* end of dir */
                if (e->name[0] == 0xE5) continue;      /* deleted     */
                if (e->attr & FAT_ATTR_VOLUME) continue;
                if (!visit(e, ctx)) return FAT_OK;
            }
        }
        return FAT_OK;
    }

    /* FAT32 / FAT16 subdirectory: follow cluster chain */
    uint32_t cur = cluster;
    while (cur && !is_eoc(cur)) {
        lba = cluster_lba(cur);
        for (sector = 0; sector < _spc; sector++) {
            fat_result_t r = read_sector(lba + sector);
            if (r != FAT_OK) return r;
            for (int i = 0; i < 512 / 32; i++) {
                const dirent_t *e = (const dirent_t *)(_sector_buf + i * 32);
                if (e->name[0] == 0x00) return FAT_OK;
                if (e->name[0] == 0xE5) continue;
                if (e->attr & FAT_ATTR_VOLUME) continue;
                if (!visit(e, ctx)) return FAT_OK;
            }
        }
        cur = fat_entry(cur);
    }
    return FAT_OK;
}

/* ------------------------------------------------------------------
 * Path resolution
 * ------------------------------------------------------------------ */

typedef struct {
    uint8_t  name8[8];
    uint8_t  ext3[3];
    bool     found;
    dirent_t result;
} find_ctx_t;

static bool find_visitor(const dirent_t *e, void *ctx) {
    find_ctx_t *f = (find_ctx_t *)ctx;
    if (memcmp(e->name, f->name8, 8) == 0 &&
        memcmp(e->ext,  f->ext3,  3) == 0) {
        f->found  = true;
        f->result = *e;
        return false; /* stop */
    }
    return true; /* continue */
}

/*
 * Resolve a path like "/FOO/BAR.TXT" to a directory entry.
 * Returns FAT_OK and fills *out on success.
 * Path must use '/' separators; components must be 8.3.
 */
static fat_result_t resolve_path(const char *path, dirent_t *out) {
    /* Start at root */
    uint32_t cur_cluster = _root_cluster;
    bool     is_root16   = !_fat32;

    /* Skip leading '/' */
    while (*path == '/') path++;

    if (*path == '\0') return FAT_ERR_NOTFOUND; /* root itself, not a file */

    char component[13];
    while (*path) {
        /* Extract next component */
        int ci = 0;
        while (*path && *path != '/' && ci < 12) component[ci++] = *path++;
        component[ci] = '\0';
        while (*path == '/') path++;

        find_ctx_t f;
        to_83(component, f.name8, f.ext3);
        f.found = false;

        fat_result_t r = walk_dir(cur_cluster, is_root16, find_visitor, &f);
        if (r != FAT_OK) return r;
        if (!f.found)    return FAT_ERR_NOTFOUND;

        if (*path == '\0') {
            /* Last component — this is what we want */
            *out = f.result;
            return FAT_OK;
        }

        /* Must be a directory to descend */
        if (!(f.result.attr & FAT_ATTR_DIR)) return FAT_ERR_NOTFILE;

        cur_cluster = ((uint32_t)f.result.first_cluster_hi << 16) |
                       f.result.first_cluster_lo;
        is_root16   = false;
    }
    return FAT_ERR_NOTFOUND;
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

fat_result_t fat_mount(void) {
    _mounted = false;

    if (sd_init() != SD_OK) return FAT_ERR_IO;

    /* Read sector 0 — could be MBR or BPB */
    if (read_sector(0) != FAT_OK) return FAT_ERR_IO;

    uint16_t sig = (uint16_t)(_sector_buf[510]) |
                   (uint16_t)(_sector_buf[511] << 8);

    _part_lba = 0;
    /* Detect MBR: signature 0xAA55 and first partition type != 0 */
    if (sig == MBR_SIGNATURE && _sector_buf[446 + 4] != 0) {
        const mbr_part_t *p = (const mbr_part_t *)(_sector_buf + 446);
        if (p->lba_first != 0) {
            _part_lba = p->lba_first;
            if (read_sector(_part_lba) != FAT_OK) return FAT_ERR_IO;
        }
    }

    const bpb_t *bpb = (const bpb_t *)_sector_buf;

    if (bpb->bytes_per_sector != 512) return FAT_ERR_IO; /* unsupported */
    _spc = bpb->sectors_per_cluster;

    uint32_t fat_size = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    _fat_size   = fat_size;
    _fat_lba    = _part_lba + bpb->reserved_sectors;

    uint32_t root_sectors = ((uint32_t)bpb->root_entry_count * 32 + 511) / 512;
    _root_lba       = _fat_lba + (uint32_t)bpb->num_fats * fat_size;
    _data_lba       = _root_lba + root_sectors;
    _root_entries   = bpb->root_entry_count;

    /* Determine FAT type by cluster count */
    uint32_t total = bpb->total_sectors_16 ? bpb->total_sectors_16
                                           : bpb->total_sectors_32;
    uint32_t data_sectors  = total - (_root_lba - _part_lba) - root_sectors;
    uint32_t cluster_count = data_sectors / _spc;
    _cluster_count = cluster_count;

    if (cluster_count >= 65525) {
        _fat32 = true;
        _root_cluster = bpb->root_cluster;
    } else {
        _fat32 = false;
        _root_cluster = 0;
    }

    _mounted = true;
    return FAT_OK;
}

/* Context for fat_ls */
typedef struct { fat_ls_cb cb; void *ctx; } ls_ctx_t;

static bool ls_visitor(const dirent_t *e, void *ctx) {
    ls_ctx_t *lc = (ls_ctx_t *)ctx;
    char name[13];
    format_83(e->name, e->ext, name);
    lc->cb(name, e->file_size, !!(e->attr & FAT_ATTR_DIR), lc->ctx);
    return true; /* always continue */
}

fat_result_t fat_ls(const char *path, fat_ls_cb cb, void *ctx) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;

    uint32_t cluster  = _root_cluster;
    bool     root16   = !_fat32;

    if (path && *path && strcmp(path, "/") != 0) {
        /* Resolve path to a directory entry */
        dirent_t e;
        fat_result_t r = resolve_path(path, &e);
        if (r != FAT_OK) return r;
        if (!(e.attr & FAT_ATTR_DIR)) return FAT_ERR_NOTFILE;
        cluster = ((uint32_t)e.first_cluster_hi << 16) | e.first_cluster_lo;
        root16  = false;
    }

    ls_ctx_t lc = { cb, ctx };
    return walk_dir(cluster, root16, ls_visitor, &lc);
}

fat_result_t fat_open(const char *path, fat_file_t *f) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;

    dirent_t e;
    fat_result_t r = resolve_path(path, &e);
    if (r != FAT_OK) return r;
    if (e.attr & FAT_ATTR_DIR) return FAT_ERR_NOTFILE;

    f->first_cluster = ((uint32_t)e.first_cluster_hi << 16) | e.first_cluster_lo;
    f->size          = e.file_size;
    f->cur_cluster   = f->first_cluster;
    f->cur_offset    = 0;
    return FAT_OK;
}

int32_t fat_read(fat_file_t *f, void *buf, uint32_t n) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;
    if (f->cur_offset >= f->size) return FAT_ERR_EOF;

    uint8_t *dst    = (uint8_t *)buf;
    uint32_t remain = f->size - f->cur_offset;
    if (n > remain) n = remain;

    uint32_t bytes_read = 0;
    uint32_t cluster_bytes = (uint32_t)_spc * 512;

    while (bytes_read < n) {
        if (is_eoc(f->cur_cluster)) break;

        uint32_t pos_in_cluster = f->cur_offset % cluster_bytes;
        uint32_t sector_in_cluster = pos_in_cluster / 512;
        uint32_t pos_in_sector     = pos_in_cluster % 512;

        uint32_t lba = cluster_lba(f->cur_cluster) + sector_in_cluster;
        if (read_sector(lba) != FAT_OK) return FAT_ERR_IO;

        uint32_t avail = 512 - pos_in_sector;
        uint32_t take  = n - bytes_read;
        if (take > avail) take = avail;

        memcpy(dst + bytes_read, _sector_buf + pos_in_sector, take);
        bytes_read      += take;
        f->cur_offset   += take;

        /* Advance cluster if we hit the cluster boundary */
        if ((f->cur_offset % cluster_bytes) == 0) {
            f->cur_cluster = fat_entry(f->cur_cluster);
        }
    }

    return (int32_t)bytes_read;
}

fat_result_t fat_get_usage(fat_usage_t *out) {
    if (!out) return FAT_ERR_IO;
    memset(out, 0, sizeof *out);
    out->mounted = _mounted;
    if (!_mounted) return FAT_ERR_NOTMOUNTED;

    out->fat32 = _fat32;
    out->bytes_per_sector = 512;
    out->sectors_per_cluster = _spc;
    out->cluster_count = _cluster_count;

    uint32_t free_clusters = 0;
    uint32_t used_clusters = 0;
    uint32_t entries_per_sector = _fat32 ? (512 / 4) : (512 / 2);

    for (uint32_t sector = 0; sector < _fat_size; sector++) {
        fat_result_t r = read_sector(_fat_lba + sector);
        if (r != FAT_OK) return r;

        for (uint32_t i = 0; i < entries_per_sector; i++) {
            uint32_t cluster = sector * entries_per_sector + i;
            if (cluster < 2 || cluster >= _cluster_count + 2) continue;

            uint32_t val = 0;
            if (_fat32) {
                memcpy(&val, _sector_buf + i * 4, 4);
                val &= 0x0FFFFFFF;
            } else {
                uint16_t v16;
                memcpy(&v16, _sector_buf + i * 2, 2);
                val = v16;
            }

            if (val == 0) free_clusters++;
            else used_clusters++;
        }
    }

    out->free_clusters = free_clusters;
    out->used_clusters = used_clusters;

    uint64_t cluster_bytes_u64 = (uint64_t)_spc * 512ULL;
    out->total_bytes = (uint64_t)_cluster_count * cluster_bytes_u64;
    out->used_bytes  = (uint64_t)used_clusters * cluster_bytes_u64;
    out->free_bytes  = (uint64_t)free_clusters * cluster_bytes_u64;
    return FAT_OK;
}

const char *fat_result_str(fat_result_t r) {
    switch (r) {
        case FAT_OK:             return "OK";
        case FAT_ERR_IO:         return "I/O error";
        case FAT_ERR_NOTFOUND:   return "not found";
        case FAT_ERR_NOTMOUNTED: return "not mounted";
        case FAT_ERR_NOTFILE:    return "not a file";
        case FAT_ERR_EOF:        return "end of file";
        case FAT_ERR_EXISTS:     return "already exists";
        case FAT_ERR_NOSPC:      return "no space left";
        case FAT_ERR_RDONLY:     return "read-only";
        case FAT_ERR_NOTEMPTY:   return "directory not empty";
        default:                 return "unknown";
    }
}

/* ------------------------------------------------------------------
 * Write helpers
 * ------------------------------------------------------------------ */

/* Write one FAT entry (FAT16 or FAT32) for the given cluster. */
static fat_result_t write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t offset, lba;
    if (_fat32) {
        offset = cluster * 4;
        lba    = _fat_lba + offset / 512;
        fat_result_t r = read_sector(lba);
        if (r != FAT_OK) return r;
        uint32_t idx = offset % 512;
        /* Preserve top 4 bits (reserved) */
        uint32_t old;
        memcpy(&old, _sector_buf + idx, 4);
        value = (value & 0x0FFFFFFF) | (old & 0xF0000000);
        memcpy(_sector_buf + idx, &value, 4);
    } else {
        offset = cluster * 2;
        lba    = _fat_lba + offset / 512;
        fat_result_t r = read_sector(lba);
        if (r != FAT_OK) return r;
        uint32_t idx = offset % 512;
        uint16_t v16 = (uint16_t)value;
        memcpy(_sector_buf + idx, &v16, 2);
    }
    /* Write to all FAT copies */
    fat_result_t r = write_sector(lba);
    if (r != FAT_OK) return r;
    /* Also update FAT2 (offset by _fat_size sectors) */
    r = read_sector(lba);  /* re-read to flush cache */
    (void)r;
    if (sd_write_block(lba + _fat_size, _sector_buf) != SD_OK)
        return FAT_ERR_IO;
    return FAT_OK;
}

/*
 * Scan FAT for a free cluster (value == 0).
 * Returns the cluster number or 0 on failure.
 */
static uint32_t alloc_cluster(void) {
    if (!_mounted) return 0;

    uint32_t total_lba = _fat_lba + _fat_size;
    uint32_t sector    = _fat_lba;

    for (sector = _fat_lba; sector < _fat_lba + _fat_size; sector++) {
        if (read_sector(sector) != FAT_OK) return 0;
        uint32_t entries = _fat32 ? 512 / 4 : 512 / 2;
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t cluster = (sector - _fat_lba) * entries + i;
            if (cluster < 2) continue;
            uint32_t val = 0;
            if (_fat32) {
                memcpy(&val, _sector_buf + i * 4, 4);
                val &= 0x0FFFFFFF;
            } else {
                uint16_t v16;
                memcpy(&v16, _sector_buf + i * 2, 2);
                val = v16;
            }
            if (val == 0) {
                /* Mark as EOC */
                uint32_t eoc = _fat32 ? 0x0FFFFFFF : 0xFFFF;
                if (write_fat_entry(cluster, eoc) != FAT_OK) return 0;
                return cluster;
            }
        }
    }
    (void)total_lba;
    return 0; /* full */
}

/* Release an entire cluster chain starting at 'start'. */
static fat_result_t free_cluster_chain(uint32_t start) {
    uint32_t cur = start;
    while (cur >= 2 && !is_eoc(cur)) {
        uint32_t next = fat_entry(cur);
        fat_result_t r = write_fat_entry(cur, 0);
        if (r != FAT_OK) return r;
        cur = next;
    }
    return FAT_OK;
}

/*
 * Resolve path to the parent directory cluster and final component name.
 * Returns FAT_OK and fills the parent directory outputs plus component[].
 */
static fat_result_t resolve_parent(const char *path,
                                   uint32_t *parent_cluster,
                                   bool     *parent_is_root16,
                                   char      component[13]) {
    /* Find the last '/' */
    const char *last_slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') last_slash = p; p++; }

    /* Parent path */
    if (last_slash == path || (last_slash == path + 1 && path[0] == '/')) {
        /* Parent is root */
        *parent_cluster  = _root_cluster;
        *parent_is_root16 = !_fat32;
    } else {
        /* Temporarily null-terminate */
        char parent_path[256];
        size_t plen = (size_t)(last_slash - path);
        if (plen >= sizeof parent_path) return FAT_ERR_NOTFOUND;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';

        dirent_t pe;
        fat_result_t r = resolve_path(parent_path, &pe);
        if (r != FAT_OK) return r;
        if (!(pe.attr & FAT_ATTR_DIR)) return FAT_ERR_NOTFILE;
        *parent_cluster   = ((uint32_t)pe.first_cluster_hi << 16) | pe.first_cluster_lo;
        *parent_is_root16 = false;
    }

    /* Component = everything after last '/' */
    const char *comp = last_slash + 1;
    if (*comp == '\0') return FAT_ERR_NOTFOUND;
    size_t clen = strlen(comp);
    if (clen >= 13) return FAT_ERR_NOTFOUND;
    memcpy(component, comp, clen + 1);
    return FAT_OK;
}

/*
 * Append a 32-byte directory entry to the given directory.
 * For FAT16 root the entry is written into the fixed root area.
 * For cluster-based dirs the last cluster is extended if needed.
 */
static fat_result_t append_dirent(uint32_t dir_cluster, bool is_root16,
                                  const dirent_t *entry) {
    if (is_root16) {
        /* Scan root dir for a free slot (0x00 or 0xE5) */
        uint32_t max_sectors = (_root_entries * 32 + 511) / 512;
        for (uint32_t s = 0; s < max_sectors; s++) {
            fat_result_t r = read_sector(_root_lba + s);
            if (r != FAT_OK) return r;
            for (int i = 0; i < 512 / 32; i++) {
                uint8_t first = _sector_buf[i * 32];
                if (first == 0x00 || first == 0xE5) {
                    memcpy(_sector_buf + i * 32, entry, 32);
                    return write_sector(_root_lba + s);
                }
            }
        }
        return FAT_ERR_NOSPC;
    }

    /* Cluster-based directory */
    uint32_t cur  = dir_cluster;
    uint32_t prev = 0;
    while (cur >= 2 && !is_eoc(cur)) {
        uint32_t lba = cluster_lba(cur);
        for (uint32_t s = 0; s < _spc; s++) {
            fat_result_t r = read_sector(lba + s);
            if (r != FAT_OK) return r;
            for (int i = 0; i < 512 / 32; i++) {
                uint8_t first = _sector_buf[i * 32];
                if (first == 0x00 || first == 0xE5) {
                    memcpy(_sector_buf + i * 32, entry, 32);
                    return write_sector(lba + s);
                }
            }
        }
        prev = cur;
        cur  = fat_entry(cur);
    }

    /* No free slot — allocate a new cluster */
    uint32_t new_cluster = alloc_cluster();
    if (new_cluster == 0) return FAT_ERR_NOSPC;

    /* Link previous tail to new cluster */
    if (prev) {
        fat_result_t r = write_fat_entry(prev, new_cluster);
        if (r != FAT_OK) return r;
    }

    /* Zero the new cluster and write entry into first slot */
    uint32_t lba = cluster_lba(new_cluster);
    memset(_sector_buf, 0, 512);
    memcpy(_sector_buf, entry, 32);
    for (uint32_t s = 0; s < _spc; s++) {
        fat_result_t r = write_sector(lba + s);
        if (r != FAT_OK) return r;
        if (s == 0) memset(_sector_buf, 0, 512); /* zero remaining sectors */
    }
    return FAT_OK;
}

/* ------------------------------------------------------------------
 * Public write API
 * ------------------------------------------------------------------ */

fat_result_t fat_is_dir(const char *path) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;
    if (!path || strcmp(path, "/") == 0) return FAT_OK; /* root is a dir */
    dirent_t e;
    fat_result_t r = resolve_path(path, &e);
    if (r != FAT_OK) return r;
    return (e.attr & FAT_ATTR_DIR) ? FAT_OK : FAT_ERR_NOTFILE;
}

fat_result_t fat_mkdir(const char *path) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;
    if (!path || !*path) return FAT_ERR_NOTFOUND;

    /* Check it doesn't already exist */
    dirent_t existing;
    if (resolve_path(path, &existing) == FAT_OK) return FAT_ERR_EXISTS;

    uint32_t parent_cluster;
    bool     parent_root16;
    char     component[13];
    fat_result_t r = resolve_parent(path, &parent_cluster,
                                    &parent_root16, component);
    if (r != FAT_OK) return r;

    /* Allocate one cluster for the new directory */
    uint32_t new_cluster = alloc_cluster();
    if (new_cluster == 0) return FAT_ERR_NOSPC;

    /* Write "." and ".." entries into the new cluster */
    uint32_t new_lba = cluster_lba(new_cluster);
    memset(_sector_buf, 0, 512);

    dirent_t dot;
    memset(&dot, 0, sizeof dot);
    memset(dot.name, ' ', 8);
    memset(dot.ext,  ' ', 3);
    dot.name[0]             = '.';
    dot.attr                = FAT_ATTR_DIR;
    dot.first_cluster_lo    = (uint16_t)(new_cluster & 0xFFFF);
    dot.first_cluster_hi    = (uint16_t)(new_cluster >> 16);
    memcpy(_sector_buf, &dot, 32);

    dirent_t dotdot;
    memset(&dotdot, 0, sizeof dotdot);
    memset(dotdot.name, ' ', 8);
    memset(dotdot.ext,  ' ', 3);
    dotdot.name[0]          = '.';
    dotdot.name[1]          = '.';
    dotdot.attr             = FAT_ATTR_DIR;
    dotdot.first_cluster_lo = (uint16_t)(parent_cluster & 0xFFFF);
    dotdot.first_cluster_hi = (uint16_t)(parent_cluster >> 16);
    memcpy(_sector_buf + 32, &dotdot, 32);

    for (uint32_t s = 0; s < _spc; s++) {
        r = write_sector(new_lba + s);
        if (r != FAT_OK) return r;
        if (s == 0) memset(_sector_buf, 0, 512);
    }

    /* Add entry in parent directory */
    dirent_t entry;
    memset(&entry, 0, sizeof entry);
    to_83(component, entry.name, entry.ext);
    entry.attr             = FAT_ATTR_DIR;
    entry.first_cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    entry.first_cluster_hi = (uint16_t)(new_cluster >> 16);

    return append_dirent(parent_cluster, parent_root16, &entry);
}

fat_result_t fat_unlink(const char *path) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;
    if (!path || !*path) return FAT_ERR_NOTFOUND;

    /* Resolve the entry */
    dirent_t e;
    fat_result_t r = resolve_path(path, &e);
    if (r != FAT_OK) return r;

    /* Refuse to delete a non-empty directory */
    if (e.attr & FAT_ATTR_DIR) {
        uint32_t dcluster = ((uint32_t)e.first_cluster_hi << 16) | e.first_cluster_lo;
        uint32_t cur = dcluster;
        bool non_empty = false;
        while (cur >= 2 && !is_eoc(cur)) {
            uint32_t lba = cluster_lba(cur);
            for (uint32_t s = 0; s < _spc && !non_empty; s++) {
                if (read_sector(lba + s) != FAT_OK) return FAT_ERR_IO;
                for (int i = 0; i < 512 / 32; i++) {
                    const dirent_t *de = (const dirent_t *)(_sector_buf + i * 32);
                    if (de->name[0] == 0x00) goto dir_scan_done;
                    if (de->name[0] == 0xE5) continue;
                    if (de->attr & FAT_ATTR_VOLUME) continue;
                    /* skip . and .. */
                    if (de->name[0] == '.' &&
                        (de->name[1] == ' ' ||
                         (de->name[1] == '.' && de->name[2] == ' '))) continue;
                    non_empty = true;
                    break;
                }
            }
            cur = fat_entry(cur);
        }
dir_scan_done:
        if (non_empty) return FAT_ERR_NOTEMPTY;
    }

    /* Free the cluster chain */
    uint32_t start_cluster = ((uint32_t)e.first_cluster_hi << 16) | e.first_cluster_lo;
    if (start_cluster >= 2) {
        r = free_cluster_chain(start_cluster);
        if (r != FAT_OK) return r;
    }

    /* Find and mark the directory entry deleted (0xE5) */
    uint32_t parent_cluster;
    bool     parent_root16;
    char     component[13];
    r = resolve_parent(path, &parent_cluster, &parent_root16, component);
    if (r != FAT_OK) return r;

    uint8_t target_name8[8], target_ext3[3];
    to_83(component, target_name8, target_ext3);

    bool deleted = false;

    if (parent_root16) {
        uint32_t max_sectors = (_root_entries * 32 + 511) / 512;
        for (uint32_t s = 0; s < max_sectors && !deleted; s++) {
            if (read_sector(_root_lba + s) != FAT_OK) return FAT_ERR_IO;
            bool dirty = false;
            for (int i = 0; i < 512 / 32; i++) {
                dirent_t *de = (dirent_t *)(_sector_buf + i * 32);
                if (de->name[0] == 0x00) goto unlink_done;
                if (de->name[0] == 0xE5) continue;
                if (memcmp(de->name, target_name8, 8) == 0 &&
                    memcmp(de->ext,  target_ext3,  3) == 0) {
                    de->name[0] = 0xE5;
                    dirty   = true;
                    deleted = true;
                    break;
                }
            }
            if (dirty && write_sector(_root_lba + s) != FAT_OK) return FAT_ERR_IO;
        }
    } else {
        uint32_t cur = parent_cluster;
        while (cur >= 2 && !is_eoc(cur) && !deleted) {
            uint32_t lba = cluster_lba(cur);
            for (uint32_t s = 0; s < _spc && !deleted; s++) {
                if (read_sector(lba + s) != FAT_OK) return FAT_ERR_IO;
                bool dirty = false;
                for (int i = 0; i < 512 / 32; i++) {
                    dirent_t *de = (dirent_t *)(_sector_buf + i * 32);
                    if (de->name[0] == 0x00) goto unlink_done;
                    if (de->name[0] == 0xE5) continue;
                    if (memcmp(de->name, target_name8, 8) == 0 &&
                        memcmp(de->ext,  target_ext3,  3) == 0) {
                        de->name[0] = 0xE5;
                        dirty   = true;
                        deleted = true;
                        break;
                    }
                }
                if (dirty && write_sector(lba + s) != FAT_OK) return FAT_ERR_IO;
            }
            cur = fat_entry(cur);
        }
    }
unlink_done:
    return deleted ? FAT_OK : FAT_ERR_NOTFOUND;
}

fat_result_t fat_create(const char *path, const uint8_t *data, uint32_t len) {
    if (!_mounted) return FAT_ERR_NOTMOUNTED;
    if (!path || !*path) return FAT_ERR_NOTFOUND;

    /* Check it doesn't already exist */
    dirent_t existing;
    if (resolve_path(path, &existing) == FAT_OK) return FAT_ERR_EXISTS;

    uint32_t parent_cluster;
    bool     parent_root16;
    char     component[13];
    fat_result_t r = resolve_parent(path, &parent_cluster,
                                    &parent_root16, component);
    if (r != FAT_OK) return r;

    /* Allocate clusters for data (at least one even for zero-length) */
    uint32_t first_cluster = 0;
    if (len > 0) {
        first_cluster = alloc_cluster();
        if (first_cluster == 0) return FAT_ERR_NOSPC;

        uint32_t cluster_bytes = (uint32_t)_spc * 512;
        uint32_t cur    = first_cluster;
        uint32_t written = 0;

        while (written < len) {
            uint32_t lba = cluster_lba(cur);
            for (uint32_t s = 0; s < _spc && written < len; s++) {
                memset(_sector_buf, 0, 512);
                uint32_t take = len - written;
                if (take > 512) take = 512;
                memcpy(_sector_buf, data + written, take);
                if (write_sector(lba + s) != FAT_OK) return FAT_ERR_IO;
                written += take;
            }
            if (written < len) {
                uint32_t next = alloc_cluster();
                if (next == 0) return FAT_ERR_NOSPC;
                if (write_fat_entry(cur, next) != FAT_OK) return FAT_ERR_IO;
                cur = next;
            }
            (void)cluster_bytes;
        }
    }

    /* Add directory entry */
    dirent_t entry;
    memset(&entry, 0, sizeof entry);
    to_83(component, entry.name, entry.ext);
    entry.attr             = 0x20; /* ARCHIVE */
    entry.first_cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    entry.first_cluster_hi = (uint16_t)(first_cluster >> 16);
    entry.file_size        = len;

    return append_dirent(parent_cluster, parent_root16, &entry);
}

