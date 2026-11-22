/*
 * apps/app_mpkg.c — mpkg, the Mellivora package manager.
 *
 * Fetches a JSON repository index over certificate-verified HTTPS, verifies
 * each artifact against its SHA-256 digest, and installs it onto the SD card:
 *
 *   kind=mapp   -> /apps
 *   kind=theme  -> /themes
 *   kind=script -> /bin
 *
 * The whole implementation is compiled only for Wi-Fi builds. RP2040 and
 * plain Pico 2 images keep a ~100-byte stub so the command still answers with
 * a clear explanation, and no TLS, JSON, or download code is linked in.
 */

#include "apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "core/filehash.h"
#include "core/json.h"
#include "core/mpkg.h"
#include "core/sha256.h"
#include "syscall.h"
#include "ui/ui.h"

#if defined(PICO_CYW43_SUPPORTED)

#include "net.h"

#define MPKG_INDEX_PATH  "/apps/repo.json"
#define MPKG_STATE_PATH  "/apps/.mpkg-state.json"
#define MPKG_CONFIG_PATH "/MPKG.CFG"
#define MPKG_CA_PATH     "/CA.CRT"

/* The Pico 2 W image has roughly 40 KiB of heap after lwIP and mbedTLS.
   Everything here is allocated per command and released afterwards. */
#define MPKG_ARENA_BYTES   (10u * 1024u)
#define MPKG_TEXT_BYTES    (8u * 1024u)
#define MPKG_CA_BYTES      (4u * 1024u)
#define MPKG_HTTP_TIMEOUT  20000

/* Configured at build time; `mpkg repo` can override it on the card. */
#ifndef MELLIVORA_MPKG_REPO
#define MELLIVORA_MPKG_REPO ""
#endif

typedef struct {
    json_arena_t arena;
    char *arena_buf;
    mpkg_index_t index;
    mpkg_state_t state;
    bool index_loaded;
} mpkg_ctx_t;

typedef struct {
    bool tls;
    char host[96];
    char path[160];
    uint16_t port;
} mpkg_url_t;

/* ------------------------------------------------------------------ */
/* URL and transport                                                    */
/* ------------------------------------------------------------------ */

static bool mpkg_parse_url(const char *url, mpkg_url_t *out) {
    if (!url || !out) return false;
    memset(out, 0, sizeof *out);

    const char *rest;
    if (!strncmp(url, "https://", 8)) {
        out->tls = true;
        out->port = 443;
        rest = url + 8;
    } else if (!strncmp(url, "http://", 7)) {
        out->port = 80;
        rest = url + 7;
    } else {
        return false;
    }

    const char *slash = strchr(rest, '/');
    size_t host_len = slash ? (size_t)(slash - rest) : strlen(rest);
    if (host_len == 0 || host_len >= sizeof out->host) return false;
    memcpy(out->host, rest, host_len);
    out->host[host_len] = '\0';

    char *colon = strrchr(out->host, ':');
    if (colon) {
        char *end = NULL;
        unsigned long parsed = strtoul(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0' || parsed == 0 || parsed > 65535) return false;
        out->port = (uint16_t)parsed;
        *colon = '\0';
    }
    if (!out->host[0]) return false;

    const char *path = (slash && slash[1]) ? slash : "/";
    if (strlen(path) >= sizeof out->path) return false;
    strcpy(out->path, path);
    return true;
}

/* Fetch a URL body into `buf`. Returns body length or -1.
   The CA certificate is read per fetch so it never occupies static RAM. */
static int mpkg_fetch(const char *url, char *buf, size_t cap) {
    mpkg_url_t parsed;
    if (!mpkg_parse_url(url, &parsed)) {
        ui_error("mpkg", "invalid repository URL");
        return -1;
    }
    if (!parsed.tls) {
        ui_error("mpkg", "refusing plain HTTP; use an https:// URL");
        return -1;
    }
    if (!sys_rtc_is_set()) {
        ui_error("mpkg", "set the clock first (run 'date' or 'ntp')");
        return -1;
    }

    char *ca = malloc(MPKG_CA_BYTES + 1);
    if (!ca) {
        ui_error("mpkg", "out of memory for the CA certificate");
        return -1;
    }
    int ca_len = sys_fread(MPKG_CA_PATH, ca, MPKG_CA_BYTES);
    if (ca_len <= 0) {
        free(ca);
        ui_error("mpkg", "HTTPS needs a PEM CA certificate at /CA.CRT");
        return -1;
    }
    ca[ca_len] = '\0';
    int result = net_https_get(parsed.host, parsed.port, parsed.path,
                               ca, (size_t)ca_len, buf, cap, MPKG_HTTP_TIMEOUT);
    free(ca);
    return result;
}

/* Artifact sink: hashes and writes each chunk straight to the card, so a
   download never has to fit in RAM. */
typedef struct {
    const char *path;
    sha256_ctx_t hash;
    uint32_t written;
    uint32_t expected; /* 0 when the index did not declare a size */
} mpkg_sink_t;

static int mpkg_sink_write(void *ctx, const uint8_t *data, size_t len) {
    mpkg_sink_t *sink = (mpkg_sink_t *)ctx;
    if (!sink || !len) return 0;
    if (sink->expected && sink->written + len > sink->expected) return -1;
    if (fat_append(sink->path, data, (uint32_t)len) != FAT_OK) return -1;
    sha256_update(&sink->hash, data, len);
    sink->written += (uint32_t)len;
    return 0;
}

/* Stream a URL into `path`. Returns the number of bytes written or -1. */
static int mpkg_fetch_to_file(const char *url, const char *path, uint32_t expected,
                              uint8_t out_digest[SHA256_DIGEST_SIZE]) {
    mpkg_url_t parsed;
    if (!mpkg_parse_url(url, &parsed)) {
        ui_error("mpkg", "invalid package URL");
        return -1;
    }
    if (!parsed.tls) {
        ui_error("mpkg", "refusing plain HTTP; use an https:// URL");
        return -1;
    }
    if (!sys_rtc_is_set()) {
        ui_error("mpkg", "set the clock first (run 'date' or 'ntp')");
        return -1;
    }

    char *ca = malloc(MPKG_CA_BYTES + 1);
    if (!ca) {
        ui_error("mpkg", "out of memory for the CA certificate");
        return -1;
    }
    int ca_len = sys_fread(MPKG_CA_PATH, ca, MPKG_CA_BYTES);
    if (ca_len <= 0) {
        free(ca);
        ui_error("mpkg", "HTTPS needs a PEM CA certificate at /CA.CRT");
        return -1;
    }
    ca[ca_len] = '\0';

    (void)fat_unlink(path); /* discard any stale copy before downloading */
    mpkg_sink_t sink = { .path = path, .expected = expected };
    sha256_init(&sink.hash);

    int len = net_https_get_stream(parsed.host, parsed.port, parsed.path,
                                   ca, (size_t)ca_len, mpkg_sink_write, &sink,
                                   MPKG_HTTP_TIMEOUT);
    free(ca);

    if (len < 0 || (uint32_t)len != sink.written) {
        (void)fat_unlink(path);
        return -1;
    }
    sha256_final(&sink.hash, out_digest);
    return len;
}

/* Hash a file from the card without loading it into RAM. */
static bool mpkg_hash_file(const char *path, uint8_t out_digest[SHA256_DIGEST_SIZE],
                           uint32_t *out_size) {
    fat_file_t file;
    if (fat_open(path, &file) != FAT_OK) return false;
    return filehash_fat_file(&file, out_digest, out_size);
}

/* ------------------------------------------------------------------ */
/* Repository configuration                                             */
/* ------------------------------------------------------------------ */

static void mpkg_repo_url(char *out, size_t cap) {
    out[0] = '\0';
    char cfg[512];
    int n = sys_fread(MPKG_CONFIG_PATH, cfg, sizeof cfg - 1);
    if (n > 0) {
        cfg[n] = '\0';
        for (char *line = cfg; line && *line;) {
            char *next = strchr(line, '\n');
            if (next) *next++ = '\0';
            char *s = (char *)skip_ws(line);
            if (!*s || *s == '#') { line = next; continue; }
            char *eq = strchr(s, '=');
            if (eq) {
                *eq++ = '\0';
                if (ci_eq((char *)skip_ws(s), "repo")) {
                    copy_cstr(out, cap, (char *)skip_ws(eq));
                    return;
                }
            }
            line = next;
        }
    }
    copy_cstr(out, cap, MELLIVORA_MPKG_REPO);
}

static bool mpkg_save_repo(const char *url) {
    char buf[320];
    int n = snprintf(buf, sizeof buf, "# Mellivora mpkg configuration\nREPO=%s\n", url);
    if (n < 0 || (size_t)n >= sizeof buf) return false;
    return sys_fwrite(MPKG_CONFIG_PATH, buf, (uint32_t)n) >= 0;
}

/* ------------------------------------------------------------------ */
/* Context loading                                                      */
/* ------------------------------------------------------------------ */

static void mpkg_ctx_free(mpkg_ctx_t *ctx) {
    free(ctx->arena_buf);
    ctx->arena_buf = NULL;
    free(ctx);
}

static mpkg_ctx_t *mpkg_ctx_new(void) {
    mpkg_ctx_t *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return NULL;
    ctx->arena_buf = malloc(MPKG_ARENA_BYTES);
    if (!ctx->arena_buf) {
        free(ctx);
        return NULL;
    }
    json_arena_init(&ctx->arena, ctx->arena_buf, MPKG_ARENA_BYTES);
    mpkg_state_load(&ctx->state, NULL, NULL, 0);
    return ctx;
}

static bool mpkg_load_state(mpkg_ctx_t *ctx) {
    char *text = malloc(MPKG_TEXT_BYTES);
    if (!text) return false;
    int n = sys_fread(MPKG_STATE_PATH, text, MPKG_TEXT_BYTES - 1);
    bool ok = true;
    if (n > 0) {
        text[n] = '\0';
        ok = mpkg_state_load(&ctx->state, &ctx->arena, text, (size_t)n);
    } else {
        mpkg_state_load(&ctx->state, NULL, NULL, 0);
    }
    free(text);
    return ok;
}

static bool mpkg_save_state(mpkg_ctx_t *ctx) {
    char *text = malloc(MPKG_TEXT_BYTES);
    if (!text) return false;
    int n = mpkg_state_serialize(&ctx->state, text, MPKG_TEXT_BYTES);
    bool ok = n > 0 && sys_fwrite(MPKG_STATE_PATH, text, (uint32_t)n) >= 0;
    free(text);
    return ok;
}

static bool mpkg_load_index(mpkg_ctx_t *ctx) {
    if (ctx->index_loaded) return true;
    char *text = malloc(MPKG_TEXT_BYTES);
    if (!text) return false;
    int n = sys_fread(MPKG_INDEX_PATH, text, MPKG_TEXT_BYTES - 1);
    if (n <= 0) {
        free(text);
        return false;
    }
    text[n] = '\0';
    json_arena_init(&ctx->arena, ctx->arena_buf, MPKG_ARENA_BYTES);
    ctx->index_loaded = mpkg_index_load(&ctx->index, &ctx->arena, text, (size_t)n);
    free(text);
    return ctx->index_loaded;
}

/* ------------------------------------------------------------------ */
/* Install / remove                                                     */
/* ------------------------------------------------------------------ */

static bool mpkg_ensure_dir(mpkg_kind_t kind) {
    const char *dir = mpkg_kind_dir(kind);
    if (!*dir) return false;
    fat_result_t r = fat_is_dir(dir);
    if (r == FAT_OK) return true;
    r = fat_mkdir(dir);
    return r == FAT_OK || r == FAT_ERR_EXISTS;
}

static int mpkg_install_one(mpkg_ctx_t *ctx, const mpkg_package_t *pkg, bool force) {
    char path[MPKG_PATH_MAX];
    if (!mpkg_package_path(pkg, path, sizeof path)) {
        ui_error("mpkg", "package has an unusable file name");
        return -1;
    }

    const mpkg_installed_t *installed = mpkg_state_find(&ctx->state, pkg->name);
    if (installed && !force) {
        int ordering = 0;
        if (mpkg_version_compare(pkg->version, installed->version, &ordering) &&
            ordering == 0) {
            char line[96];
            snprintf(line, sizeof line, "%s %s already installed", pkg->name, pkg->version);
            ui_line(UI_MUTED, line);
            return 0;
        }
    }

    char line[128];
    snprintf(line, sizeof line, "Fetching %s %s...", pkg->name, pkg->version);
    ui_line(UI_TEXT, line);

    if (!mpkg_ensure_dir(pkg->kind)) {
        ui_error("mpkg", "cannot create the destination directory");
        return -1;
    }

    uint8_t digest[SHA256_DIGEST_SIZE];
    int len = mpkg_fetch_to_file(pkg->url, path, pkg->size, digest);
    if (len < 0) {
        ui_error("mpkg", pkg->size ? "download failed or size mismatch" : "download failed");
        return -1;
    }
    if (pkg->size && (uint32_t)len != pkg->size) {
        (void)fat_unlink(path);
        ui_error("mpkg", "downloaded size does not match the index");
        return -1;
    }
    if (!sha256_hex_equal(pkg->sha256, digest)) {
        (void)fat_unlink(path);
        char actual[SHA256_HEX_SIZE];
        sha256_hex(digest, actual);
        ui_error("mpkg", "SHA-256 mismatch; nothing installed");
        ui_line(UI_MUTED, actual);
        return -1;
    }

    if (!mpkg_state_record(&ctx->state, pkg, path)) {
        ui_error("mpkg", "install state is full");
        return -1;
    }
    mpkg_save_state(ctx);

    snprintf(line, sizeof line, "Installed %.16s %.12s -> %.40s (%d bytes)",
             pkg->name, pkg->version, path, len);
    ui_ok(line);
    return 0;
}

static int mpkg_remove_one(mpkg_ctx_t *ctx, const char *name) {
    const mpkg_installed_t *entry = mpkg_state_find(&ctx->state, name);
    if (!entry) {
        ui_error("mpkg", "package is not installed");
        return -1;
    }
    char path[MPKG_PATH_MAX];
    copy_cstr(path, sizeof path, entry->path);
    if (fat_unlink(path) != FAT_OK) {
        ui_error("mpkg", "could not delete the installed file");
        return -1;
    }
    mpkg_state_remove(&ctx->state, name);
    mpkg_save_state(ctx);
    char line[96];
    snprintf(line, sizeof line, "Removed %s", name);
    ui_ok(line);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */

static void mpkg_print_usage(void) {
    ui_title("mpkg");
    ui_line(UI_MUTED, "  mpkg update             refresh the repository index");
    ui_line(UI_MUTED, "  mpkg list [text]        available packages");
    ui_line(UI_MUTED, "  mpkg installed          packages on this card");
    ui_line(UI_MUTED, "  mpkg info NAME          package details");
    ui_line(UI_MUTED, "  mpkg install NAME       fetch, verify, install");
    ui_line(UI_MUTED, "  mpkg upgrade [NAME|--all]");
    ui_line(UI_MUTED, "  mpkg remove NAME        delete an installed package");
    ui_line(UI_MUTED, "  mpkg verify             re-check installed checksums");
    ui_line(UI_MUTED, "  mpkg repo [URL]         show or set the index URL");
}

static void mpkg_cmd_repo(const char *arg) {
    const char *value = skip_ws(arg);
    if (!*value) {
        char url[MPKG_URL_MAX];
        mpkg_repo_url(url, sizeof url);
        if (*url) ui_line(UI_TEXT, url);
        else ui_line(UI_MUTED, "(no repository configured; mpkg repo URL)");
        return;
    }
    if (!mpkg_save_repo(value)) {
        ui_error("mpkg", "could not write /MPKG.CFG");
        return;
    }
    ui_ok("repository saved");
}

static void mpkg_cmd_update(void) {
    char url[MPKG_URL_MAX];
    mpkg_repo_url(url, sizeof url);
    if (!*url) {
        ui_error("mpkg", "set a repository first: mpkg repo https://host/index.json");
        return;
    }
    if (!net_is_connected()) {
        ui_error("mpkg", "not connected to Wi-Fi");
        return;
    }

    char *body = malloc(MPKG_TEXT_BYTES);
    if (!body) {
        ui_error("mpkg", "out of memory");
        return;
    }
    ui_line(UI_TEXT, "Downloading repository index...");
    int len = mpkg_fetch(url, body, MPKG_TEXT_BYTES);
    if (len < 0) {
        free(body);
        ui_error("mpkg", "index download failed");
        ui_line(UI_MUTED, "index must be under 8 KiB and the card needs room");
        return;
    }

    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) {
        free(body);
        ui_error("mpkg", "out of memory");
        return;
    }
    if (!mpkg_index_load(&ctx->index, &ctx->arena, body, (size_t)len)) {
        free(body);
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "repository index rejected");
        ui_line(UI_MUTED, json_last_error());
        return;
    }
    (void)mpkg_ensure_dir(MPKG_KIND_MAPP);
    if (sys_fwrite(MPKG_INDEX_PATH, body, (uint32_t)len) < 0) {
        free(body);
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "could not cache the index on the SD card");
        return;
    }
    free(body);

    char line[96];
    snprintf(line, sizeof line, "%s: %u package(s)", ctx->index.name,
             (unsigned)ctx->index.count);
    ui_ok(line);
    if (ctx->index.rejected) {
        snprintf(line, sizeof line, "%u entr(ies) skipped as invalid",
                 (unsigned)ctx->index.rejected);
        ui_line(UI_WARNING, line);
    }
    mpkg_ctx_free(ctx);
}

static void mpkg_list_row(const mpkg_package_t *pkg, const mpkg_state_t *state) {
    const mpkg_installed_t *installed = mpkg_state_find(state, pkg->name);
    const char *mark = " ";
    if (installed) {
        int ordering = 0;
        if (mpkg_version_compare(pkg->version, installed->version, &ordering) && ordering > 0)
            mark = "+"; /* upgrade available */
        else
            mark = "*"; /* installed */
    }
    char row[112];
    snprintf(row, sizeof row, " %s %-14.14s %-10.10s %-6s %s",
             mark, pkg->name, pkg->version, mpkg_kind_name(pkg->kind),
             pkg->description);
    ui_line(installed ? UI_SUCCESS : UI_TEXT, row);
}

static void mpkg_cmd_list(const char *filter) {
    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) {
        ui_error("mpkg", "out of memory");
        return;
    }
    if (!mpkg_load_index(ctx) || !mpkg_load_state(ctx)) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "no index cached; run 'mpkg update'");
        return;
    }

    size_t shown = 0;
    for (size_t i = 0; i < ctx->index.count; i++) {
        const mpkg_package_t *pkg = &ctx->index.packages[i];
        if (*filter && !str_contains_ci(pkg->name, filter) &&
            !str_contains_ci(pkg->description, filter))
            continue;
        mpkg_list_row(pkg, &ctx->state);
        shown++;
    }
    char line[64];
    snprintf(line, sizeof line, "%u of %u package(s) shown", (unsigned)shown,
             (unsigned)ctx->index.count);
    ui_line(UI_MUTED, line);
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_info(const char *name) {
    if (!*name) {
        mpkg_print_usage();
        return;
    }
    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) return;
    if (!mpkg_load_index(ctx) || !mpkg_load_state(ctx)) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "no index cached; run 'mpkg update'");
        return;
    }
    const mpkg_package_t *pkg = mpkg_index_find(&ctx->index, name);
    if (!pkg) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "package not found in the index");
        return;
    }
    ui_title(pkg->name);
    ui_label_value("version", pkg->version);
    ui_label_value("kind", mpkg_kind_name(pkg->kind));
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lu", (unsigned long)pkg->size);
    ui_label_value("size", pkg->size ? tmp : "unknown");
    ui_label_value("destination", mpkg_kind_dir(pkg->kind));
    ui_line(UI_MUTED, pkg->description);
    ui_line(UI_MUTED, pkg->sha256);

    mpkg_action_t action = mpkg_plan(&ctx->index, &ctx->state, name, NULL);
    ui_printf(UI_TEXT, "status: %s\n", mpkg_action_name(action));
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_install(const char *args) {
    char name[MPKG_NAME_MAX];
    const char *rest = next_token(args, name, sizeof name);
    bool force = opt_flag(args, "--force");
    (void)rest;
    if (!*name) {
        mpkg_print_usage();
        return;
    }

    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) {
        ui_error("mpkg", "out of memory");
        return;
    }
    if (!mpkg_load_index(ctx) || !mpkg_load_state(ctx)) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "no index cached; run 'mpkg update'");
        return;
    }
    const mpkg_package_t *pkg = mpkg_index_find(&ctx->index, name);
    if (!pkg) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "package not found in the index");
        return;
    }
    if (!net_is_connected()) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "not connected to Wi-Fi");
        return;
    }
    (void)mpkg_install_one(ctx, pkg, force);
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_upgrade(const char *args) {
    bool all = opt_flag(args, "--all");
    char name[MPKG_NAME_MAX];
    (void)next_token(args, name, sizeof name);

    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) return;
    if (!mpkg_load_index(ctx) || !mpkg_load_state(ctx)) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "no index cached; run 'mpkg update'");
        return;
    }
    if (!net_is_connected()) {
        mpkg_ctx_free(ctx);
        ui_error("mpkg", "not connected to Wi-Fi");
        return;
    }

    int changed = 0;
    if (*name && !all) {
        const mpkg_package_t *pkg = NULL;
        mpkg_action_t action = mpkg_plan(&ctx->index, &ctx->state, name, &pkg);
        if (action != MPKG_ACTION_UPGRADE && action != MPKG_ACTION_INSTALL) {
            char line[96];
            snprintf(line, sizeof line, "%s is %s", name, mpkg_action_name(action));
            ui_line(UI_MUTED, line);
        } else if (mpkg_install_one(ctx, pkg, false) == 0) {
            changed++;
        }
    } else {
        for (size_t i = 0; i < ctx->index.count; i++) {
            const mpkg_package_t *pkg = &ctx->index.packages[i];
            mpkg_action_t action = mpkg_plan(&ctx->index, &ctx->state, pkg->name, NULL);
            if (action != MPKG_ACTION_UPGRADE) continue;
            if (mpkg_install_one(ctx, pkg, false) == 0) changed++;
        }
    }

    char line[64];
    snprintf(line, sizeof line, "%d package(s) updated", changed);
    ui_line(changed ? UI_SUCCESS : UI_MUTED, line);
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_installed(void) {
    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) return;
    (void)mpkg_load_state(ctx);
    if (ctx->state.count == 0) {
        ui_line(UI_MUTED, "No packages installed.");
        mpkg_ctx_free(ctx);
        return;
    }
    ui_title("Installed");
    for (size_t i = 0; i < ctx->state.count; i++) {
        const mpkg_installed_t *entry = &ctx->state.entries[i];
        char row[112];
        snprintf(row, sizeof row, " %-14.14s %-10.10s %-6s %s", entry->name,
                 entry->version, mpkg_kind_name(entry->kind), entry->path);
        ui_line(UI_TEXT, row);
    }
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_verify(void) {
    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) return;
    (void)mpkg_load_state(ctx);
    if (ctx->state.count == 0) {
        ui_line(UI_MUTED, "Nothing to verify.");
        mpkg_ctx_free(ctx);
        return;
    }

    int bad = 0;
    for (size_t i = 0; i < ctx->state.count; i++) {
        const mpkg_installed_t *entry = &ctx->state.entries[i];
        uint8_t digest[SHA256_DIGEST_SIZE];
        bool ok = mpkg_hash_file(entry->path, digest, NULL) &&
                  sha256_hex_equal(entry->sha256, digest);
        char row[112];
        if (ok) {
            snprintf(row, sizeof row, " ok   %s", entry->name);
            ui_line(UI_SUCCESS, row);
        } else {
            snprintf(row, sizeof row, " FAIL %s (%s)", entry->name, entry->path);
            ui_line(UI_ERROR, row);
            bad++;
        }
    }
    char line[64];
    snprintf(line, sizeof line, "%d of %u package(s) failed verification", bad,
             (unsigned)ctx->state.count);
    ui_line(bad ? UI_ERROR : UI_SUCCESS, line);
    mpkg_ctx_free(ctx);
}

static void mpkg_cmd_remove(const char *name) {
    if (!*name) {
        mpkg_print_usage();
        return;
    }
    mpkg_ctx_t *ctx = mpkg_ctx_new();
    if (!ctx) return;
    (void)mpkg_load_state(ctx);
    (void)mpkg_remove_one(ctx, name);
    mpkg_ctx_free(ctx);
}

void app_mpkg(const char *arg) {
    char cmd[APP_TOKEN_MAX];
    const char *rest = next_token(arg, cmd, sizeof cmd);

    if (!*cmd || ci_eq(cmd, "help") || ci_eq(cmd, "?")) {
        mpkg_print_usage();
        return;
    }
    if (ci_eq(cmd, "repo")) { mpkg_cmd_repo(rest); return; }
    if (ci_eq(cmd, "update")) { mpkg_cmd_update(); return; }
    if (ci_eq(cmd, "list") || ci_eq(cmd, "search")) { mpkg_cmd_list(skip_ws(rest)); return; }
    if (ci_eq(cmd, "installed")) { mpkg_cmd_installed(); return; }
    if (ci_eq(cmd, "info")) { mpkg_cmd_info(skip_ws(rest)); return; }
    if (ci_eq(cmd, "install") || ci_eq(cmd, "add")) { mpkg_cmd_install(rest); return; }
    if (ci_eq(cmd, "upgrade") || ci_eq(cmd, "update-all")) { mpkg_cmd_upgrade(rest); return; }
    if (ci_eq(cmd, "remove") || ci_eq(cmd, "uninstall")) { mpkg_cmd_remove(skip_ws(rest)); return; }
    if (ci_eq(cmd, "verify")) { mpkg_cmd_verify(); return; }

    mpkg_print_usage();
}

#else /* !PICO_CYW43_SUPPORTED */

/*
 * Non-Wi-Fi builds link only this stub: no JSON, SHA-256, TLS, or download
 * code is pulled in, so the RP2040 and plain Pico 2 images stay unchanged.
 */
void app_mpkg(const char *arg) {
    (void)arg;
    ui_error("mpkg", "needs the Pico 2 W firmware (Wi-Fi + TLS)");
    ui_line(UI_MUTED, "  Build/flash mellivora_*_pico2w.uf2 to install packages.");
}

#endif /* PICO_CYW43_SUPPORTED */
