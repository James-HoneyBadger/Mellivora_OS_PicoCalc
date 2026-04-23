/*
 * apps/fs.c -- filesystem utility applications
 *
 * Provides: find, cp, mv, stat, tree, du, df, browse
 */
#include "apps.h"
#include "apps/hex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "syscall.h"
#include "fat.h"

/* Forward declaration for app_edit (implemented in apps.c) */
extern void app_edit(const char *arg);

/* --------------------------------------------------------------------------
 * Private types
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *pattern;
    char root[APP_TOKEN_MAX];
    int count;
    int type_filter;  /* 0=any, 1=file only, 2=dir only */
} find_ctx_t;

typedef struct {
    int count;
    uint32_t total_size;
} dir_stat_t;

typedef struct {
    char name[BROWSE_NAME_MAX];
    bool is_dir;
    uint32_t size;
} browser_entry_t;

typedef struct {
    char cwd[APP_TOKEN_MAX];
    browser_entry_t items[BROWSE_ITEMS_MAX];
    int count;
} browser_state_t;

typedef struct {
    char path[APP_TOKEN_MAX];
    int depth;
    int max_depth;
    int *file_count;
    int *dir_count;
} tree_walk_t;

typedef struct {
    uint32_t total_bytes;
    int files;
    int dirs;
} du_stat_t;

typedef struct {
    char path[APP_TOKEN_MAX];
    du_stat_t stat;
} du_walk_ctx_t;

/* --------------------------------------------------------------------------
 * find
 * -------------------------------------------------------------------------- */

static bool glob_match_star(const char *pattern, const char *str) {
    const char *star_pat = NULL;
    const char *star_str = NULL;

    while (1) {
        if (*pattern == '*') { pattern++; star_pat = pattern; star_str = str; continue; }
        if (*pattern == '\0') {
            if (*str == '\0') return true;
            if (!star_pat) return false;
            if (*star_str == '\0') return false;
            star_str++; str = star_str; pattern = star_pat; continue;
        }
        if (*str != '\0' && *pattern == *str) { pattern++; str++; continue; }
        if (!star_pat) return false;
        if (*star_str == '\0') return false;
        star_str++; str = star_str; pattern = star_pat;
    }
}

static void app_find_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    (void)size;
    find_ctx_t *ctx = (find_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    if (ctx->pattern && !glob_match_star(ctx->pattern, name)) return;
    if (ctx->type_filter == 1 && is_dir) return;
    if (ctx->type_filter == 2 && !is_dir) return;

    char path[APP_TOKEN_MAX];
    path[0] = '\0';
    if (strcmp(ctx->root, "/") == 0) {
        append_cstr(path, sizeof path, "/");
        append_cstr(path, sizeof path, name);
    } else {
        append_cstr(path, sizeof path, ctx->root);
        append_cstr(path, sizeof path, "/");
        append_cstr(path, sizeof path, name);
    }
    print_line(path);
    ctx->count++;
}

void app_find(const char *arg) {
    char tok1[APP_TOKEN_MAX];
    char tok2[APP_TOKEN_MAX];
    char tok3[APP_TOKEN_MAX];
    char root[APP_TOKEN_MAX];
    const char *pattern = NULL;
    const char *s = next_token(arg, tok1, sizeof tok1);

    root[0] = '\0'; tok2[0] = '\0'; tok3[0] = '\0';

    if (!*tok1) {
        copy_cstr(root, sizeof root, _sys_cwd);
    } else if (strcmp(tok1, "-name") == 0) {
        s = next_token(s, tok2, sizeof tok2);
        if (!*tok2) { print_line("usage: find [PATH] [-name PATTERN]"); return; }
        pattern = tok2;
        copy_cstr(root, sizeof root, _sys_cwd);
    } else {
        app_make_abs(tok1, root, sizeof root);
        s = next_token(s, tok2, sizeof tok2);
        if (*tok2) {
            if (strcmp(tok2, "-name") != 0) { print_line("usage: find [PATH] [-name PATTERN]"); return; }
            s = next_token(s, tok3, sizeof tok3);
            if (!*tok3) { print_line("usage: find [PATH] [-name PATTERN]"); return; }
            pattern = tok3;
        }
    }

    if (fat_is_dir(root) != FAT_OK) { print_line("find: path is not a directory"); return; }

    find_ctx_t ctx = { .pattern = pattern, .count = 0, .type_filter = 0 };
    copy_cstr(ctx.root, sizeof ctx.root, root);

    {
        char tflag[APP_TOKEN_MAX];
        const char *scan = s;
        while (scan && *skip_ws(scan)) {
            char tok[APP_TOKEN_MAX];
            scan = next_token(scan, tok, sizeof tok);
            if (strcmp(tok, "-type") == 0) {
                scan = next_token(scan, tflag, sizeof tflag);
                if (tflag[0] == 'f') ctx.type_filter = 1;
                else if (tflag[0] == 'd') ctx.type_filter = 2;
                break;
            }
        }
    }

    if (fat_ls(root, app_find_cb, &ctx) != FAT_OK)
        print_line("find: unable to list directory");
}

/* --------------------------------------------------------------------------
 * cp / mv
 * -------------------------------------------------------------------------- */

typedef struct {
    char src[APP_TOKEN_MAX];
    char dst[APP_TOKEN_MAX];
    int count;
    bool err;
} _cp_dir_ctx_t;

static void _cp_dir_cb(const char *name, uint32_t size, bool is_dir, void *ctx) {
    (void)size;
    _cp_dir_ctx_t *c = (_cp_dir_ctx_t *)ctx;
    char fsrc[APP_TOKEN_MAX + 16], fdst[APP_TOKEN_MAX + 16];
    snprintf(fsrc, sizeof fsrc, "%s/%s", c->src, name);
    snprintf(fdst, sizeof fdst, "%s/%s", c->dst, name);

    if (is_dir) {
        if (fat_mkdir(fdst) == FAT_OK) c->count++;
        else c->err = true;
    } else {
        uint8_t buf[APP_READ_MAX];
        fat_file_t f;
        if (fat_open(fsrc, &f) != FAT_OK) { c->err = true; return; }
        int32_t rd = fat_read(&f, buf, sizeof buf);
        if (rd < 0) { c->err = true; return; }
        if (fat_create(fdst, buf, (uint32_t)rd) == FAT_OK) c->count++;
        else c->err = true;
    }
}

void app_cp(const char *arg) {
    char src[APP_TOKEN_MAX], dst[APP_TOKEN_MAX];
    const char *s = next_token(arg, src, sizeof src);
    next_token(s, dst, sizeof dst);

    if (!*src || !*dst) { print_line("usage: cp SRC DST"); return; }

    char abs_src[APP_TOKEN_MAX], abs_dst[APP_TOKEN_MAX];
    app_make_abs(src, abs_src, sizeof abs_src);
    app_make_abs(dst, abs_dst, sizeof abs_dst);

    if (strcmp(abs_src, abs_dst) == 0) { print_line("cp: source and destination are the same"); return; }

    if (fat_is_dir(abs_src) == FAT_OK) {
        if (fat_mkdir(abs_dst) != FAT_OK && fat_is_dir(abs_dst) != FAT_OK) {
            print_line("cp: cannot create destination directory"); return;
        }
        static _cp_dir_ctx_t cpctx;
        copy_cstr(cpctx.src, sizeof cpctx.src, abs_src);
        copy_cstr(cpctx.dst, sizeof cpctx.dst, abs_dst);
        cpctx.count = 0; cpctx.err = false;
        fat_ls(abs_src, _cp_dir_cb, &cpctx);
        char out[64];
        snprintf(out, sizeof out, "copied %d entries%s",
                 cpctx.count, cpctx.err ? " (with errors)" : "");
        print_line(out);
        return;
    }

    uint8_t buf[APP_READ_MAX];
    uint32_t n = 0;
    if (load_file_bytes(src, buf, sizeof buf, &n, "cp") < 0) return;
    if (sys_fwrite(dst, buf, n) < 0) { print_line("cp: write failed"); return; }

    char out[64];
    snprintf(out, sizeof out, "copied %lu bytes", (unsigned long)n);
    print_line(out);
}

void app_mv(const char *arg) {
    char src[APP_TOKEN_MAX], dst[APP_TOKEN_MAX];
    const char *s = next_token(arg, src, sizeof src);
    next_token(s, dst, sizeof dst);

    if (!*src || !*dst) { print_line("usage: mv SRC DST"); return; }

    char abs_src[APP_TOKEN_MAX], abs_dst[APP_TOKEN_MAX];
    app_make_abs(src, abs_src, sizeof abs_src);
    app_make_abs(dst, abs_dst, sizeof abs_dst);

    if (strcmp(abs_src, abs_dst) == 0) { print_line("mv: source and destination are the same"); return; }
    if (fat_is_dir(abs_src) == FAT_OK) { print_line("mv: directory move not supported yet"); return; }

    uint8_t buf[APP_READ_MAX];
    uint32_t n = 0;
    if (load_file_bytes(src, buf, sizeof buf, &n, "mv") < 0) return;
    if (sys_fwrite(dst, buf, n) < 0) { print_line("mv: write failed"); return; }
    if (sys_unlink(src) < 0) { print_line("mv: destination written but source removal failed"); return; }

    char out[64];
    snprintf(out, sizeof out, "moved %lu bytes", (unsigned long)n);
    print_line(out);
}

/* --------------------------------------------------------------------------
 * stat
 * -------------------------------------------------------------------------- */

static void app_stat_dir_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    dir_stat_t *st = (dir_stat_t *)opaque;
    if (!st || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    st->count++;
    if (!is_dir) st->total_size += size;
}

void app_stat(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) { print_line("usage: stat PATH"); return; }

    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        dir_stat_t st = {0};
        fat_ls(abs, app_stat_dir_cb, &st);
        char out[160];
        snprintf(out, sizeof out,
                 "path: %.96s\ntype: directory\nentries: %d\nfile-bytes: %lu",
                 abs, st.count, (unsigned long)st.total_size);
        print_line(out);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) != FAT_OK) { print_line("stat: not found"); return; }

    char out[160];
    snprintf(out, sizeof out,
             "path: %.96s\ntype: file\nsize: %lu bytes\ncluster: %lu",
             abs, (unsigned long)f.size, (unsigned long)f.first_cluster);
    print_line(out);
}

/* --------------------------------------------------------------------------
 * tree
 * -------------------------------------------------------------------------- */

static void app_tree_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    tree_walk_t *ctx = (tree_walk_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    char line[192];
    int pos = 0;
    int indent = ctx->depth * 2;
    if (indent > 48) indent = 48;
    while (pos < indent && pos + 1 < (int)sizeof line) line[pos++] = ' ';
    pos += snprintf(line + pos, sizeof line - (size_t)pos, "%s%s",
                    is_dir ? "|- " : "- ", name);
    if (is_dir) pos += snprintf(line + pos, sizeof line - (size_t)pos, "/");
    else        pos += snprintf(line + pos, sizeof line - (size_t)pos, " (%lu)", (unsigned long)size);
    if (pos < 0) return;
    line[sizeof line - 1] = '\0';
    print_line(line);

    if (is_dir) {
        if (ctx->dir_count) (*ctx->dir_count)++;
        if (ctx->depth >= ctx->max_depth) return;

        char child[APP_TOKEN_MAX];
        app_join_path(ctx->path, name, child, sizeof child);

        tree_walk_t next = *ctx;
        copy_cstr(next.path, sizeof next.path, child);
        next.depth = ctx->depth + 1;
        (void)fat_ls(child, app_tree_cb, &next);
    } else if (ctx->file_count) {
        (*ctx->file_count)++;
    }
}

void app_tree(const char *arg) {
    char tok1[APP_TOKEN_MAX] = {0};
    char tok2[APP_TOKEN_MAX] = {0};
    char path[APP_TOKEN_MAX] = {0};
    int max_depth = 8;
    const char *s = next_token(arg, tok1, sizeof tok1);

    if (*tok1) {
        if (strcmp(tok1, "-L") == 0) {
            char *endptr = NULL;
            s = next_token(s, tok2, sizeof tok2);
            long depth = strtol(tok2, &endptr, 10);
            if (!*tok2 || endptr == tok2 || depth < 1) {
                print_line("usage: tree [-L DEPTH] [PATH]"); return;
            }
            max_depth = (int)depth;
            next_token(s, path, sizeof path);
        } else {
            copy_cstr(path, sizeof path, tok1);
        }
    }

    if (!*path) copy_cstr(path, sizeof path, _sys_cwd);
    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        int files = 0, dirs = 1;
        print_line(abs);

        tree_walk_t ctx = {
            .depth = 1, .max_depth = max_depth,
            .file_count = &files, .dir_count = &dirs,
        };
        copy_cstr(ctx.path, sizeof ctx.path, abs);

        if (fat_ls(abs, app_tree_cb, &ctx) != FAT_OK) {
            print_line("tree: unable to list directory"); return;
        }
        char out[96];
        snprintf(out, sizeof out, "%d directories, %d files", dirs, files);
        print_line(out);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) != FAT_OK) { print_line("tree: path not found"); return; }

    char out[160];
    snprintf(out, sizeof out, "%.120s (%lu bytes)", abs, (unsigned long)f.size);
    print_line(out);
}

/* --------------------------------------------------------------------------
 * du / df
 * -------------------------------------------------------------------------- */

static uint32_t app_du_path(const char *path, du_stat_t *out);

static void app_du_accum_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    du_walk_ctx_t *ctx = (du_walk_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    if (is_dir) {
        char child[APP_TOKEN_MAX];
        du_stat_t nested = {0};
        app_join_path(ctx->path, name, child, sizeof child);
        (void)app_du_path(child, &nested);
        ctx->stat.total_bytes += nested.total_bytes;
        ctx->stat.files += nested.files;
        ctx->stat.dirs += nested.dirs + 1;
    } else {
        ctx->stat.total_bytes += size;
        ctx->stat.files++;
    }
}

static uint32_t app_du_path(const char *path, du_stat_t *out) {
    du_stat_t zero = {0};
    if (!out) out = &zero;
    memset(out, 0, sizeof *out);

    if (fat_is_dir(path) != FAT_OK) {
        fat_file_t f;
        if (fat_open(path, &f) != FAT_OK) return 0;
        out->total_bytes = f.size;
        out->files = 1;
        return f.size;
    }

    du_walk_ctx_t ctx = {0};
    copy_cstr(ctx.path, sizeof ctx.path, path);
    if (fat_ls(path, app_du_accum_cb, &ctx) != FAT_OK) return 0;
    *out = ctx.stat;
    return ctx.stat.total_bytes;
}

static void app_du_list_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    du_walk_ctx_t *ctx = (du_walk_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    uint32_t bytes = size;
    int files = 0, dirs = 0;

    if (is_dir) {
        char child[APP_TOKEN_MAX];
        du_stat_t nested = {0};
        app_join_path(ctx->path, name, child, sizeof child);
        bytes = app_du_path(child, &nested);
        files = nested.files;
        dirs  = nested.dirs + 1;
    } else {
        files = 1;
    }

    ctx->stat.total_bytes += bytes;
    ctx->stat.files += files;
    ctx->stat.dirs  += dirs;

    char line[160];
    snprintf(line, sizeof line, "%8lu  %s%s",
             (unsigned long)bytes, name, is_dir ? "/" : "");
    print_line(line);
}

void app_du(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) copy_cstr(path, sizeof path, _sys_cwd);

    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        du_walk_ctx_t ctx = {0};
        copy_cstr(ctx.path, sizeof ctx.path, abs);

        print_line("bytes     name");
        if (fat_ls(abs, app_du_list_cb, &ctx) != FAT_OK) {
            print_line("du: unable to list directory"); return;
        }
        char out[128];
        snprintf(out, sizeof out, "total: %lu bytes | files: %d | dirs: %d",
                 (unsigned long)ctx.stat.total_bytes,
                 ctx.stat.files, ctx.stat.dirs + 1);
        print_line(out);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) != FAT_OK) { print_line("du: path not found"); return; }

    char out[160];
    snprintf(out, sizeof out, "%lu bytes  %.120s", (unsigned long)f.size, abs);
    print_line(out);
}

static void app_format_size(uint64_t bytes, char *out, size_t out_sz) {
    if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
        unsigned long long w = bytes / (1024ULL * 1024ULL * 1024ULL);
        unsigned long long f = (bytes % (1024ULL * 1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL * 1024ULL);
        snprintf(out, out_sz, "%llu.%llu GB", w, f);
    } else if (bytes >= (1024ULL * 1024ULL)) {
        unsigned long long w = bytes / (1024ULL * 1024ULL);
        unsigned long long f = (bytes % (1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL);
        snprintf(out, out_sz, "%llu.%llu MB", w, f);
    } else if (bytes >= 1024ULL) {
        unsigned long long w = bytes / 1024ULL;
        unsigned long long f = (bytes % 1024ULL) * 10ULL / 1024ULL;
        snprintf(out, out_sz, "%llu.%llu KB", w, f);
    } else {
        snprintf(out, out_sz, "%llu B", (unsigned long long)bytes);
    }
}

void app_df(const char *arg) {
    (void)arg;
    fat_usage_t usage;
    fat_result_t r = fat_get_usage(&usage);
    if (r != FAT_OK) { print_line("df: filesystem not mounted"); return; }

    char total[32], used[32], free_sp[32], out[160];
    app_format_size(usage.total_bytes, total, sizeof total);
    app_format_size(usage.used_bytes, used, sizeof used);
    app_format_size(usage.free_bytes, free_sp, sizeof free_sp);

    uint32_t used_pct = usage.total_bytes
        ? (uint32_t)((usage.used_bytes * 100ULL) / usage.total_bytes) : 0;

    snprintf(out, sizeof out, "filesystem: FAT%s", usage.fat32 ? "32" : "16");
    print_line(out);
    snprintf(out, sizeof out, "cluster size: %lu bytes",
             (unsigned long)(usage.bytes_per_sector * usage.sectors_per_cluster));
    print_line(out);
    snprintf(out, sizeof out, "total: %s", total); print_line(out);
    snprintf(out, sizeof out, "used:  %s (%lu%%)", used, (unsigned long)used_pct); print_line(out);
    snprintf(out, sizeof out, "free:  %s", free_sp); print_line(out);
    snprintf(out, sizeof out, "clusters: %lu total, %lu used, %lu free",
             (unsigned long)usage.cluster_count,
             (unsigned long)usage.used_clusters,
             (unsigned long)usage.free_clusters);
    print_line(out);
}

/* --------------------------------------------------------------------------
 * browse (interactive file browser)
 * -------------------------------------------------------------------------- */

static void browser_parent_dir(const char *cwd, char *out, size_t out_sz) {
    strncpy(out, cwd, out_sz - 1);
    out[out_sz - 1] = '\0';
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') out[--len] = '\0';
    while (len > 1 && out[len - 1] != '/') out[--len] = '\0';
    if (len == 0) strcpy(out, "/");
    else if (len > 1 && out[len - 1] == '/') out[len - 1] = '\0';
    if (out[0] == '\0') strcpy(out, "/");
}

static void browser_collect_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    browser_state_t *br = (browser_state_t *)opaque;
    if (!br || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    if (br->count >= BROWSE_ITEMS_MAX) return;

    copy_cstr(br->items[br->count].name, sizeof br->items[br->count].name, name);
    br->items[br->count].is_dir = is_dir;
    br->items[br->count].size   = size;
    br->count++;
}

static int browser_entry_cmp(const browser_entry_t *a, const browser_entry_t *b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return str_casecmp_local(a->name, b->name);
}

static void browser_sort(browser_state_t *br) {
    for (int i = 1; i < br->count; i++) {
        browser_entry_t key = br->items[i];
        int j = i - 1;
        while (j >= 0 && browser_entry_cmp(&key, &br->items[j]) < 0) {
            br->items[j + 1] = br->items[j];
            j--;
        }
        br->items[j + 1] = key;
    }
}

static void browser_refresh(browser_state_t *br) {
    br->count = 0;
    if (fat_ls(br->cwd, browser_collect_cb, br) == FAT_OK) browser_sort(br);
}

static void browser_render(browser_state_t *br, int sel) {
    sys_clear();
    sys_print("Mellivora browser\n");
    sys_print(br->cwd);
    sys_print("\n\n");
    sys_print("j/k move, Enter open, e edit, x hex, n new file, m mkdir, d delete, u up, r refresh, q quit\n\n");

    if (strcmp(br->cwd, "/") != 0)
        sys_print(sel == 0 ? "> [DIR] ..\n" : "  [DIR] ..\n");

    int base       = (strcmp(br->cwd, "/") != 0) ? 1 : 0;
    int page_start = 0;
    int visible    = 12;
    if (sel >= visible) page_start = sel - visible + 1;

    for (int disp = 0; disp < visible; disp++) {
        int row = page_start + disp;
        if (row == 0 && base == 1) continue;
        int idx = row - base;
        if (idx < 0 || idx >= br->count) continue;

        char line[80];
        snprintf(line, sizeof line, "%c %s %-20s %6lu\n",
                 (row == sel) ? '>' : ' ',
                 br->items[idx].is_dir ? "[DIR]" : "[FIL]",
                 br->items[idx].name,
                 (unsigned long)br->items[idx].size);
        sys_print(line);
    }

    if (br->count == 0) sys_print("(empty directory)\n");
}

void app_browse(const char *arg) {
    browser_state_t br;
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (*path) app_make_abs(path, br.cwd, sizeof br.cwd);
    else copy_cstr(br.cwd, sizeof br.cwd, _sys_cwd);

    if (fat_is_dir(br.cwd) != FAT_OK) { print_line("browse: path is not a directory"); return; }

    copy_cstr(_sys_cwd, sizeof _sys_cwd, br.cwd);
    browser_refresh(&br);

    int sel = 0;
    while (1) {
        int base  = (strcmp(br.cwd, "/") != 0) ? 1 : 0;
        int total = br.count + base;
        if (sel < 0) sel = 0;
        if (sel >= total && total > 0) sel = total - 1;
        browser_render(&br, sel);

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;

        if (ch == 'j' || ch == 's' || ch == 0x0E) { if (sel + 1 < total) sel++; continue; }
        if (ch == 'k' || ch == 'w' || ch == 0x10) { if (sel > 0) sel--; continue; }
        if (ch == 'r' || ch == 'R') { browser_refresh(&br); continue; }
        if (ch == 'u' || ch == 'U') {
            char parent[APP_TOKEN_MAX];
            browser_parent_dir(br.cwd, parent, sizeof parent);
            copy_cstr(br.cwd, sizeof br.cwd, parent);
            copy_cstr(_sys_cwd, sizeof _sys_cwd, br.cwd);
            browser_refresh(&br); sel = 0; continue;
        }

        if (ch == 'n' || ch == 'N') {
            char name[APP_TOKEN_MAX];
            sys_print("New file: ");
            if (app_read_line("", name, sizeof name) >= 0) {
                rtrim_in_place(name);
                if (*name) {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_fwrite(name, "", 0) < 0) print_line("browse: create failed");
                    else { browser_refresh(&br); sel = total > 0 ? total - 1 : 0; }
                }
            }
            continue;
        }

        if (ch == 'm' || ch == 'M') {
            char name[APP_TOKEN_MAX];
            sys_print("New directory: ");
            if (app_read_line("", name, sizeof name) >= 0) {
                rtrim_in_place(name);
                if (*name) {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_mkdir(name) < 0) print_line("browse: mkdir failed");
                    else browser_refresh(&br);
                }
            }
            continue;
        }

        if (ch == 'd' || ch == 'D') {
            if (sel >= base && total > 0) {
                browser_entry_t *ent = &br.items[sel - base];
                char prompt[96];
                snprintf(prompt, sizeof prompt, "Delete %s? [y/N] ", ent->name);
                sys_print(prompt);
                int ok = sys_getchar();
                sys_putchar('\n');
                if (ok == 'y' || ok == 'Y') {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_delete(ent->name) < 0) print_line("browse: delete failed");
                    else { browser_refresh(&br); if (sel > 0 && sel >= br.count + base) sel--; }
                }
            }
            continue;
        }

        if (ch == 'e' || ch == 'E' || ch == 'x' || ch == 'X') {
            if (sel >= base && total > 0) {
                browser_entry_t *ent = &br.items[sel - base];
                if (!ent->is_dir) {
                    char abs[APP_TOKEN_MAX];
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    app_make_abs(ent->name, abs, sizeof abs);
                    if (ch == 'x' || ch == 'X') app_hexedit(abs);
                    else app_edit(abs);
                }
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (strcmp(br.cwd, "/") != 0 && sel == 0) {
                char parent[APP_TOKEN_MAX];
                browser_parent_dir(br.cwd, parent, sizeof parent);
                strncpy(br.cwd, parent, sizeof br.cwd - 1);
                br.cwd[sizeof br.cwd - 1] = '\0';
                strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                browser_refresh(&br); sel = 0; continue;
            }

            if (sel < base) continue;
            browser_entry_t *ent = &br.items[sel - base];
            strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
            _sys_cwd[sizeof _sys_cwd - 1] = '\0';

            if (ent->is_dir) {
                if (sys_chdir(ent->name) == 0) {
                    strncpy(br.cwd, _sys_cwd, sizeof br.cwd - 1);
                    br.cwd[sizeof br.cwd - 1] = '\0';
                    browser_refresh(&br); sel = 0;
                }
            } else {
                char abs[APP_TOKEN_MAX];
                app_make_abs(ent->name, abs, sizeof abs);
                app_pager(abs);
            }
        }
    }

    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
    sys_clear();
}
