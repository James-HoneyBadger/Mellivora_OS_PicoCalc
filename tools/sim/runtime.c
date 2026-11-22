/*
 * tools/sim/runtime.c — host-side implementation of the Mellivora app ABI.
 *
 * Links the very same core/gfx.c and core/mixer.c the firmware uses, so an
 * app that builds for the PicoCalc also builds for the desktop:
 *
 *   make -C tools/sim APP=plasma           -> build/plasma
 *
 * The runtime is headless: there is no window. Graphics surfaces render
 * through the real gfx core, and a virtual key is injected once the app has
 * run long enough (MAPP_AUTOQUIT_MS). When mapp_main returns, the last
 * presented surface is written to shots/NAME.png (PPM-grade PNG written
 * with zlib), which is what CI screenshots for the gallery.
 *
 * Text output goes to stdout, so "hello" still behaves like a terminal
 * program, and files are sandboxed under build/simroot/.
 */

#include "mapp.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "core/gfx.h"
#include "core/mixer.h"

#ifndef MAPP_AUTOQUIT_MS
#define MAPP_AUTOQUIT_MS 4000
#endif

#define MAPP_SHOTS_DIR "shots"
#define SIM_SURFACES 8u
#define SIM_FILES 8u

/* ---- Time ------------------------------------------------------------ */

static uint64_t _boot_ms;

static uint64_t _now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint32_t _sim_millis(void) { return (uint32_t)_now_ms(); }
static void _sim_sleep(uint32_t milliseconds) { usleep((useconds_t)milliseconds * 1000u); }

/* ---- Graphics -------------------------------------------------------- */

typedef struct {
    gfx_surface_t surface;
    uint8_t *pixels;
    int32_t origin_x, origin_y;
} sim_surface_t;

static sim_surface_t _surfaces[SIM_SURFACES];
static sim_surface_t *_presented;

/* Frame snapshot taken at present time, so the screenshot survives the
   app's own gfx_destroy() call before returning. */
static uint8_t *_last_frame;
static uint16_t _last_frame_w, _last_frame_h;

static sim_surface_t *_slot(void *surface) {
    gfx_surface_t *wanted = (gfx_surface_t *)surface;
    for (uint32_t i = 0; i < SIM_SURFACES; i++) {
        if (_surfaces[i].pixels && &_surfaces[i].surface == wanted) return &_surfaces[i];
    }
    return NULL;
}

static void *_sim_gfx_create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return NULL;
    uint32_t bytes = (uint32_t)width * height;
    for (uint32_t i = 0; i < SIM_SURFACES; i++) {
        if (_surfaces[i].pixels) continue;
        uint8_t *pixels = (uint8_t *)malloc(bytes);
        if (!pixels) return NULL;
        memset(pixels, 0, bytes);
        if (!gfx_surface_init(&_surfaces[i].surface, pixels, width, height)) {
            free(pixels);
            return NULL;
        }
        _surfaces[i].pixels = pixels;
        return &_surfaces[i].surface;
    }
    return NULL;
}

static void _sim_gfx_destroy(void *surface) {
    sim_surface_t *slot = _slot(surface);
    if (!slot) return;
    free(slot->pixels);
    slot->pixels = NULL;
    memset(&slot->surface, 0, sizeof slot->surface);
    if (_presented == slot) _presented = NULL;
}

static void _sim_gfx_clear(void *surface, uint8_t color) { gfx_clear((gfx_surface_t *)surface, color); }
static void _sim_gfx_pixel(void *surface, int32_t x, int32_t y, uint8_t color) {
    gfx_pixel((gfx_surface_t *)surface, x, y, color);
}
static void _sim_gfx_hline(void *surface, int32_t x, int32_t y, int32_t width, uint8_t color) {
    gfx_hline((gfx_surface_t *)surface, x, y, width, color);
}
static void _sim_gfx_vline(void *surface, int32_t x, int32_t y, int32_t height, uint8_t color) {
    gfx_vline((gfx_surface_t *)surface, x, y, height, color);
}
static void _sim_gfx_rect(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                          uint8_t color) {
    gfx_rect((gfx_surface_t *)surface, x, y, width, height, color);
}
static void _sim_gfx_fill_rect(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                               uint8_t color) {
    gfx_fill_rect((gfx_surface_t *)surface, x, y, width, height, color);
}
static void _sim_gfx_char(void *surface, int32_t x, int32_t y, char ch, uint8_t color) {
    /* The built-in 8x16 font lives in the firmware LCD driver. The sim uses
       this compact 5x7 set, which is recognisable enough for screenshots. */
    static const uint8_t glyph5x7[96][7] = {
        ['A' - 0x20] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00},
        ['B' - 0x20] = {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00},
        ['C' - 0x20] = {0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x00},
        ['D' - 0x20] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00},
        ['E' - 0x20] = {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00},
        ['F' - 0x20] = {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00},
        ['G' - 0x20] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x0E, 0x00},
        ['H' - 0x20] = {0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00},
        ['I' - 0x20] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x00},
        ['J' - 0x20] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00},
        ['K' - 0x20] = {0x11, 0x12, 0x1C, 0x12, 0x11, 0x11, 0x00},
        ['L' - 0x20] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00},
        ['M' - 0x20] = {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x00},
        ['N' - 0x20] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00},
        ['O' - 0x20] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        ['P' - 0x20] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x00},
        ['Q' - 0x20] = {0x0E, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00},
        ['R' - 0x20] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x00},
        ['S' - 0x20] = {0x0F, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00},
        ['T' - 0x20] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00},
        ['U' - 0x20] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        ['V' - 0x20] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00},
        ['W' - 0x20] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00},
        ['X' - 0x20] = {0x11, 0x0A, 0x04, 0x04, 0x0A, 0x11, 0x00},
        ['Y' - 0x20] = {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00},
        ['Z' - 0x20] = {0x1F, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00},
        ['0' - 0x20] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x0E, 0x00},
        ['1' - 0x20] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00},
        ['2' - 0x20] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x1F, 0x00},
        ['3' - 0x20] = {0x1E, 0x01, 0x06, 0x01, 0x01, 0x1E, 0x00},
        ['4' - 0x20] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x00},
        ['5' - 0x20] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x1E, 0x00},
        ['6' - 0x20] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x0E, 0x00},
        ['7' - 0x20] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00},
        ['8' - 0x20] = {0x0E, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00},
        ['9' - 0x20] = {0x0E, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00},
        [' ' - 0x20] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        ['.' - 0x20] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
        ['-' - 0x20] = {0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00},
        ['!' - 0x20] = {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00},
        [':' - 0x20] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
        ['/' - 0x20] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
    };
    unsigned char uc = (unsigned char)ch;
    if (uc < 0x20 || uc > 0x7E) return;
    const uint8_t *rows = glyph5x7[uc - 0x20];
    gfx_surface_t *s = (gfx_surface_t *)surface;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row] & (0x10u >> col)) gfx_pixel(s, x + col, y + row, color);
        }
    }
}
static void _sim_gfx_blit(void *dst, int32_t x, int32_t y, const void *src, int32_t transparent) {
    gfx_blit((gfx_surface_t *)dst, x, y, (const gfx_surface_t *)src, transparent);
}
static void _sim_gfx_palette(uint8_t index, uint32_t rgb) {
    gfx_palette_set(index, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}
static void _sim_gfx_invalidate(void *surface) { gfx_invalidate((gfx_surface_t *)surface); }
static void _sim_gfx_present(void *surface, int32_t x, int32_t y) {
    sim_surface_t *slot = _slot(surface);
    if (!slot) return;
    slot->origin_x = x;
    slot->origin_y = y;
    _presented = slot;
    if (slot->surface.dirty_any) gfx_dirty_reset(&slot->surface);

    size_t bytes = (size_t)slot->surface.width * slot->surface.height;
    if (_last_frame_w != slot->surface.width || _last_frame_h != slot->surface.height) {
        free(_last_frame);
        _last_frame = (uint8_t *)malloc(bytes);
        _last_frame_w = slot->surface.width;
        _last_frame_h = slot->surface.height;
    }
    if (_last_frame) memcpy(_last_frame, slot->surface.pixels, bytes);
}

/* ---- Keyboard (virtual: a key appears after MAPP_AUTOQUIT_MS) --------- */

static uint64_t _quit_at;

static int32_t _sim_keyboard_read(void) {
    while (_now_ms() < _quit_at) _sim_sleep(5);
    return 'q';
}

static int32_t _sim_keyboard_try_read(void) { return _now_ms() >= _quit_at ? 'q' : -1; }

static int32_t _sim_keyboard_read_line(const char *prompt, char *buffer, uint32_t capacity) {
    (void)prompt;
    (void)capacity;
    if (buffer) buffer[0] = '\0';
    return 0;
}

/* ---- Files (sandboxed under build/simroot) ---------------------------- */

static FILE *_sim_files[SIM_FILES];

static void _sim_path(const char *path, char *out, size_t capacity) {
    if (!path || path[0] != '/') path = "/";
    snprintf(out, capacity, "build/simroot%s", path);
}

static int32_t _sim_file_open(const char *path, uint32_t flags) {
    (void)flags;
    char full[512];
    _sim_path(path, full, sizeof full);
    FILE *file = fopen(full, "rb");
    if (!file) return -1;
    for (uint32_t i = 0; i < SIM_FILES; i++) {
        if (_sim_files[i]) continue;
        _sim_files[i] = file;
        return (int32_t)i;
    }
    fclose(file);
    return -1;
}

static int32_t _sim_file_read(int32_t handle, void *buffer, uint32_t capacity) {
    if (handle < 0 || (uint32_t)handle >= SIM_FILES || !_sim_files[handle] || !buffer) return -1;
    return (int32_t)fread(buffer, 1, capacity, _sim_files[handle]);
}

static int32_t _sim_file_close(int32_t handle) {
    if (handle < 0 || (uint32_t)handle >= SIM_FILES || !_sim_files[handle]) return -1;
    fclose(_sim_files[handle]);
    _sim_files[handle] = NULL;
    return 0;
}

static int32_t _sim_file_write(const char *path, const void *buffer, uint32_t length) {
    char full[512];
    _sim_path(path, full, sizeof full);
    FILE *file = fopen(full, "wb");
    if (!file) return -1;
    size_t written = fwrite(buffer, 1, length, file);
    fclose(file);
    return written == length ? 0 : -1;
}

static int32_t _sim_file_remove(const char *path) {
    char full[512];
    _sim_path(path, full, sizeof full);
    return remove(full) == 0 ? 0 : -1;
}

static int32_t _sim_file_mkdir(const char *path) {
    char full[512];
    _sim_path(path, full, sizeof full);
    char *cursor = full + strlen("build/simroot");
    while (*cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            (void)mkdir(full, 0755);
            *cursor = '/';
        }
        cursor++;
    }
    return mkdir(full, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int32_t _sim_file_list(const char *path, mapp_dir_entry_fn callback, void *context) {
    char full[512];
    _sim_path(path, full, sizeof full);
    DIR *dir = opendir(full);
    if (!dir) return -1;
    struct dirent *entry;
    int32_t count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (callback) callback(entry->d_name, entry->d_name, 0, entry->d_type == DT_DIR, context);
        count++;
    }
    closedir(dir);
    return count;
}

/* ---- Display (text goes to stdout) ------------------------------------ */

static void _sim_display_clear(void) { fputs("\033[2J\033[H", stdout); }
static void _sim_display_putc(char ch) { putchar(ch); }
static void _sim_display_print(const char *text) { fputs(text, stdout); }
static void _sim_display_set_cursor(int32_t x, int32_t y) { (void)x; (void)y; }
static void _sim_display_set_color(uint32_t fg, uint32_t bg) { (void)fg; (void)bg; }
static int32_t _sim_display_columns(void) { return 40; }
static int32_t _sim_display_rows(void) { return 20; }

/* ---- Audio (no device: mixer advances, output discarded) -------------- */

static mixer_t _sim_mixer;

static int32_t _sim_audio_tone(int32_t freq_hz, int32_t duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return -1;
    mixer_note_on(&_sim_mixer, 0, MIXER_WAVE_SQUARE, (uint32_t)freq_hz, (uint32_t)duration_ms, 255,
                  1, 5);
    return 0;
}

static void _sim_audio_stop(void) { mixer_stop_all(&_sim_mixer); }

static int32_t _sim_audio_note(int32_t wave, int32_t freq_hz, int32_t duration_ms,
                               int32_t volume, int32_t attack_ms, int32_t release_ms,
                               int32_t voice) {
    if (wave < MAPP_WAVE_SQUARE || wave > MAPP_WAVE_NOISE) return -1;
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms < 0) return -1;
    if (voice < 0 || voice >= MIXER_VOICES) return -1;
    mixer_note_on(&_sim_mixer, voice, (mixer_wave_t)wave, (uint32_t)freq_hz,
                  (uint32_t)duration_ms, (uint8_t)volume, (uint16_t)attack_ms,
                  (uint16_t)release_ms);
    return 0;
}

static void _sim_audio_voice_stop(int32_t voice) {
    if (voice >= 0 && voice < MIXER_VOICES) mixer_note_off(&_sim_mixer, voice);
}

/* ---- Network: the sim is offline -------------------------------------- */

static int32_t _sim_network_available(void) { return 0; }
static int32_t _sim_network_resolve(const char *hostname, uint32_t *address,
                                    int32_t timeout_ms) {
    (void)hostname;
    (void)address;
    (void)timeout_ms;
    return -1;
}
static int32_t _sim_network_http_get(const char *host, uint16_t port, const char *path,
                                     char *buffer, uint32_t capacity, int32_t timeout_ms) {
    (void)host;
    (void)port;
    (void)path;
    (void)buffer;
    (void)capacity;
    (void)timeout_ms;
    return -1;
}

static uint32_t _sim_capabilities(void) {
    return MAPP_CAP_DISPLAY | MAPP_CAP_FILES | MAPP_CAP_KEYS | MAPP_CAP_TIMER | MAPP_CAP_GFX |
           MAPP_CAP_AUDIO | MAPP_CAP_AUDIO_MIXER | MAPP_CAP_YIELD;
}

static void _sim_yield(void) { (void)mixer_sample(&_sim_mixer); }

/* ---- Screenshot (zlib PNG writer) -------------------------------------- */

static uint32_t _crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void _write_u32be(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void _png_chunk(FILE *file, const char type[4], const uint8_t *data, size_t len) {
    uint8_t header[4];
    _write_u32be(header, (uint32_t)len);
    fwrite(header, 1, 4, file);
    fwrite(type, 1, 4, file);
    fwrite(data, 1, len, file);
    uint32_t crc = _crc32_update(0, (const uint8_t *)type, 4);
    crc = _crc32_update(crc, data, len);
    _write_u32be(header, crc);
    fwrite(header, 1, 4, file);
}

static int _write_png(const char *path, uint16_t width, uint16_t height, const uint8_t *rgb) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;

    size_t stride = (size_t)width * 3u + 1u;
    uint8_t *rows = (uint8_t *)malloc(stride * height);
    if (!rows) {
        fclose(file);
        return -1;
    }
    for (uint32_t y = 0; y < height; y++) {
        rows[y * stride] = 0; /* filter: none */
        memcpy(rows + y * stride + 1, rgb + (size_t)y * width * 3u, (size_t)width * 3u);
    }

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(signature, 1, 8, file);

    uint8_t ihdr[13];
    _write_u32be(ihdr, width);
    _write_u32be(ihdr + 4, height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    _png_chunk(file, "IHDR", ihdr, sizeof ihdr);

    uLongf compressed_len = compressBound((uLong)(stride * height));
    uint8_t *compressed = (uint8_t *)malloc(compressed_len);
    if (!compressed || compress(compressed, &compressed_len, rows, (uLong)(stride * height)) != Z_OK) {
        free(rows);
        free(compressed);
        fclose(file);
        return -1;
    }
    _png_chunk(file, "IDAT", compressed, (size_t)compressed_len);
    _png_chunk(file, "IEND", NULL, 0);

    free(rows);
    free(compressed);
    fclose(file);
    return 0;
}

static void _screenshot(const char *app_name) {
    if (!_last_frame) return;
    gfx_surface_t frame;
    uint8_t rgb[(size_t)_last_frame_w * _last_frame_h * 3u];
    if (!gfx_surface_init(&frame, _last_frame, _last_frame_w, _last_frame_h)) return;
    for (uint16_t y = 0; y < _last_frame_h; y++) {
        (void)gfx_row_to_rgb888(&frame, y, 0, _last_frame_w,
                                rgb + (size_t)y * _last_frame_w * 3u);
    }
    (void)mkdir(MAPP_SHOTS_DIR, 0755);
    char path[512];
    snprintf(path, sizeof path, MAPP_SHOTS_DIR "/%s.png", app_name);
    if (_write_png(path, _last_frame_w, _last_frame_h, rgb) == 0)
        fprintf(stderr, "sim: screenshot %s (%ux%u)\n", path, _last_frame_w, _last_frame_h);
}

/* ---- Entry ------------------------------------------------------------ */

extern int32_t mapp_main(const mapp_api_t *api, int32_t argc, char **argv);

int main(int argc, char **argv) {
    _boot_ms = _now_ms();
    _quit_at = _boot_ms + MAPP_AUTOQUIT_MS;
    gfx_palette_reset();
    mixer_init(&_sim_mixer, 16000);
    (void)mkdir("build", 0755);
    (void)mkdir("build/simroot", 0755);

    static const mapp_api_t api = {
        .size = sizeof(mapp_api_t),
        .version = MAPP_API_VERSION,
        .display_clear = _sim_display_clear,
        .display_putc = _sim_display_putc,
        .display_print = _sim_display_print,
        .display_set_cursor = _sim_display_set_cursor,
        .display_set_color = _sim_display_set_color,
        .display_columns = _sim_display_columns,
        .display_rows = _sim_display_rows,
        .keyboard_read = _sim_keyboard_read,
        .keyboard_try_read = _sim_keyboard_try_read,
        .keyboard_read_line = _sim_keyboard_read_line,
        .file_open = _sim_file_open,
        .file_read = _sim_file_read,
        .file_close = _sim_file_close,
        .file_list = _sim_file_list,
        .file_write = _sim_file_write,
        .file_remove = _sim_file_remove,
        .file_mkdir = _sim_file_mkdir,
        .timer_millis = _sim_millis,
        .timer_sleep = _sim_sleep,
        .network_available = _sim_network_available,
        .network_resolve = _sim_network_resolve,
        .network_http_get = _sim_network_http_get,
        .capabilities = _sim_capabilities,
        .yield = _sim_yield,
        .gfx_create = _sim_gfx_create,
        .gfx_destroy = _sim_gfx_destroy,
        .gfx_clear = _sim_gfx_clear,
        .gfx_pixel = _sim_gfx_pixel,
        .gfx_hline = _sim_gfx_hline,
        .gfx_vline = _sim_gfx_vline,
        .gfx_rect = _sim_gfx_rect,
        .gfx_fill_rect = _sim_gfx_fill_rect,
        .gfx_char = _sim_gfx_char,
        .gfx_blit = _sim_gfx_blit,
        .gfx_palette = _sim_gfx_palette,
        .gfx_invalidate = _sim_gfx_invalidate,
        .gfx_present = _sim_gfx_present,
        .audio_tone = _sim_audio_tone,
        .audio_stop = _sim_audio_stop,
        .audio_note = _sim_audio_note,
        .audio_voice_stop = _sim_audio_voice_stop,
    };

    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];

    int32_t result = mapp_main(&api, argc, argv);
    _screenshot(name);
    fflush(stdout);
    return (int)result;
}
