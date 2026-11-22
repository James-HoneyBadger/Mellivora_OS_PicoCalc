#include "mapp.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "audio.h"
#include "console.h"
#include "core/mixer.h"
#include "core/gfx.h"
#include "gfx_lcd.h"
#include "core/mapp_image.h"
#include "fat.h"
#include "kbd.h"
#include "lcd.h"
#include "net.h"
#include "syscall.h"
#include "ui/ui.h"

#define MAPP_CATALOG_MAX 24u
#define MAPP_ARENA_SIZE MAPP_IMAGE_ARENA_MAX
#define MAPP_RELOC_MAX MAPP_IMAGE_RELOC_MAX
#define MAPP_FILE_HANDLES 4u
#define MAPP_METADATA_BYTES 256u

enum {
    BC_HALT = 0,
    BC_PRINT = 1,
    BC_PUTC = 2,
    BC_READ_KEY = 3,
    BC_TIME_MS = 4,
    BC_SLEEP_MS = 5,
    BC_PUSH_I32 = 6,
    BC_ADD = 7,
    BC_SUB = 8,
    BC_MUL = 9,
    BC_PRINT_I32 = 10,
    BC_JUMP = 11,
    BC_JUMP_ZERO = 12,
    BC_CLEAR = 13,
    BC_DUP = 14,
    BC_DROP = 15,
};

typedef struct {
    bool open;
    fat_file_t file;
} mapp_file_slot_t;

typedef struct {
    mapp_dir_entry_fn callback;
    void *user_context;
} mapp_list_context_t;

static mapp_app_t _catalog[MAPP_CATALOG_MAX];
static uint32_t _catalog_count;
static bool _catalog_overflow;
static uint32_t _relocations[MAPP_RELOC_MAX];
static mapp_file_slot_t _file_slots[MAPP_FILE_HANDLES];

/* Graphics surfaces handed to the app. Pixel buffers come from the heap and
   are released when the app returns, so a misbehaving app cannot leak the
   arena across launches. */
#define MAPP_GFX_SURFACES 4u
#define MAPP_GFX_BYTES_MAX (128u * 1024u)

typedef struct {
    gfx_surface_t surface;
    uint8_t *pixels;
    uint32_t bytes;
} mapp_gfx_slot_t;

static mapp_gfx_slot_t _gfx_slots[MAPP_GFX_SURFACES];
static bool _mapp_running;

static bool _ends_with_ci(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len) return false;
    text += text_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (tolower((unsigned char)text[i]) != tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

static void _copy_string(char *dst, size_t capacity, const char *src) {
    if (capacity == 0) return;
    size_t length = strlen(src);
    if (length >= capacity) length = capacity - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static void _fallback_title(char *out, size_t capacity, const char *filename) {
    _copy_string(out, capacity, filename);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static bool _read_exact(fat_file_t *file, void *buffer, uint32_t length) {
    uint8_t *dst = (uint8_t *)buffer;
    while (length > 0) {
        int32_t n = fat_read(file, dst, length);
        if (n <= 0) return false;
        dst += (uint32_t)n;
        length -= (uint32_t)n;
    }
    return true;
}

static uint16_t _read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t _read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool _read_header_name(const char *filename, char *out, size_t capacity) {
    fat_file_t file;
    if (fat_open(filename, &file) != FAT_OK || file.size < MAPP_HEADER_SIZE) return false;

    uint8_t header[MAPP_HEADER_SIZE];
    if (!_read_exact(&file, header, sizeof header)) return false;
    if (memcmp(header, "MAPP", 4) != 0 ||
        _read_u16(header + 4) != MAPP_FORMAT_VERSION) return false;
    const char *name = (const char *)(header + 28);
    size_t name_len = 0;
    while (name_len < 32 && name[name_len]) name_len++;
    if (name_len == 0) return false;
    if (name_len >= capacity) name_len = capacity - 1;
    memcpy(out, name, name_len);
    out[name_len] = '\0';
    return true;
}

static bool _script_metadata_name(const char *filename, char *out, size_t capacity) {
    fat_file_t file;
    if (fat_open(filename, &file) != FAT_OK) return false;
    uint8_t contents[MAPP_METADATA_BYTES];
    uint32_t amount = file.size < sizeof contents ? file.size : sizeof contents;
    if (amount == 0 || !_read_exact(&file, contents, amount)) return false;

    for (uint32_t start = 0; start < amount;) {
        uint32_t end = start;
        while (end < amount && contents[end] != '\n' && contents[end] != '\r') end++;
        uint32_t p = start;
        while (p < end && (contents[p] == ' ' || contents[p] == '\t')) p++;
        if (p + 1 < end && contents[p] == '/' && contents[p + 1] == '/') p += 2;
        else if (p < end && (contents[p] == '#' || contents[p] == '\\')) p++;
        else if (p + 3 <= end &&
                 tolower(contents[p]) == 'r' &&
                 tolower(contents[p + 1]) == 'e' &&
                 tolower(contents[p + 2]) == 'm') p += 3;
        else {
            start = end + 1;
            continue;
        }
        while (p < end && (contents[p] == ' ' || contents[p] == '\t')) p++;
        static const char key[] = "@app-name:";
        uint32_t k = 0;
        while (k < sizeof key - 1 && p + k < end &&
               tolower(contents[p + k]) == key[k]) k++;
        if (k == sizeof key - 1) {
            p += k;
            while (p < end && (contents[p] == ' ' || contents[p] == '\t')) p++;
            uint32_t length = end - p;
            while (length && (contents[p + length - 1] == ' ' ||
                              contents[p + length - 1] == '\t')) length--;
            if (length == 0) return false;
            if (length >= capacity) length = (uint32_t)capacity - 1u;
            memcpy(out, contents + p, length);
            out[length] = '\0';
            return true;
        }
        start = end + 1;
    }
    return false;
}

static void _file_list_adapter(const char *short_name, const char *long_name,
                               uint32_t size, bool is_dir, void *context) {
    mapp_list_context_t *list_context = (mapp_list_context_t *)context;
    list_context->callback(short_name, long_name, size, is_dir ? 1u : 0u,
                           list_context->user_context);
}

static void _catalog_add(const char *short_name, const char *long_name,
                         uint32_t size, bool is_dir, void *ctx) {
    (void)size;
    (void)ctx;
    if (is_dir) return;
    const char *name = (long_name && *long_name) ? long_name : short_name;
    mapp_app_kind_t kind;
    if (_ends_with_ci(name, ".mapp")) kind = MAPP_APP_THUMB;
    else if (_ends_with_ci(name, ".bas")) kind = MAPP_APP_BASIC;
    else if (_ends_with_ci(name, ".c")) kind = MAPP_APP_TINYC;
    else if (_ends_with_ci(name, ".fth")) kind = MAPP_APP_FORTH;
    else return;

    if (_catalog_count >= MAPP_CATALOG_MAX) {
        _catalog_overflow = true;
        return;
    }
    mapp_app_t *app = &_catalog[_catalog_count++];
    memset(app, 0, sizeof *app);
    if (strlen(name) >= sizeof app->filename) {
        _catalog_count--;
        _catalog_overflow = true;
        return;
    }
    _copy_string(app->filename, sizeof app->filename, name);
    app->kind = kind;
}

int32_t mapp_discover(void) {
    _catalog_count = 0;
    _catalog_overflow = false;
    fat_result_t result = fat_ls_ex("/apps", _catalog_add, NULL);
    if (result != FAT_OK) return result;

    char path[264];
    for (uint32_t i = 0; i < _catalog_count; i++) {
        mapp_app_t *app = &_catalog[i];
        size_t filename_len = strlen(app->filename);
        memcpy(path, "/apps/", 6);
        memcpy(path + 6, app->filename, filename_len + 1);
        bool found = app->kind == MAPP_APP_THUMB
            ? _read_header_name(path, app->title, sizeof app->title)
            : _script_metadata_name(path, app->title, sizeof app->title);
        if (!found) _fallback_title(app->title, sizeof app->title, app->filename);
    }
    return (int32_t)_catalog_count;
}

const mapp_app_t *mapp_catalog_get(uint32_t index) {
    return index < _catalog_count ? &_catalog[index] : NULL;
}

int32_t mapp_catalog_truncated(void) {
    return _catalog_overflow ? 1 : 0;
}

static void _display_clear(void) { sys_clear(); }
static void _display_putc(char ch) { sys_putchar(ch); }
static void _display_print(const char *text) { sys_print(text); }
static void _display_set_cursor(int32_t x, int32_t y) { lcd_set_cursor(x, y); }
static void _display_set_color(uint32_t fg, uint32_t bg) {
    lcd_set_fg(fg);
    lcd_set_bg(bg);
}
static int32_t _display_columns(void) { return LCD_COLS; }
static int32_t _display_rows(void) { return LCD_ROWS; }
static int32_t _keyboard_read(void) { return sys_read_key(); }
static int32_t _keyboard_try_read(void) { return kbd_getc(); }
static int32_t _keyboard_read_line(const char *prompt, char *buffer, uint32_t capacity) {
    if (!buffer || capacity == 0) return -1;
    return console_read_line(prompt, buffer, capacity);
}

static bool _absolute_path(const char *path, char *out, size_t capacity) {
    if (!path || !*path) path = "/";
    if (path[0] == '/') {
        size_t length = strlen(path);
        if (length >= capacity) return false;
        memcpy(out, path, length + 1);
        return true;
    }
    size_t cwd_len = strlen(_sys_cwd);
    size_t path_len = strlen(path);
    bool root = strcmp(_sys_cwd, "/") == 0;
    size_t needed = cwd_len + (root ? 0u : 1u) + path_len + 1u;
    if (needed > capacity) return false;
    memcpy(out, _sys_cwd, cwd_len);
    size_t pos = cwd_len;
    if (!root) out[pos++] = '/';
    memcpy(out + pos, path, path_len + 1);
    return true;
}

static int32_t _file_open(const char *path, uint32_t flags) {
    if (flags != MAPP_FILE_READ) return -1;
    for (uint32_t i = 0; i < MAPP_FILE_HANDLES; i++) {
        if (_file_slots[i].open) continue;
        char abs[256];
        if (!_absolute_path(path, abs, sizeof abs)) return -1;
        if (fat_open(abs, &_file_slots[i].file) != FAT_OK) return -1;
        _file_slots[i].open = true;
        return (int32_t)i;
    }
    return -1;
}

static int32_t _file_read(int32_t handle, void *buffer, uint32_t capacity) {
    if (handle < 0 || (uint32_t)handle >= MAPP_FILE_HANDLES ||
        !_file_slots[handle].open || (!buffer && capacity)) return -1;
    int32_t result = fat_read(&_file_slots[handle].file, buffer, capacity);
    return result == FAT_ERR_EOF ? 0 : result;
}

static int32_t _file_close(int32_t handle) {
    if (handle < 0 || (uint32_t)handle >= MAPP_FILE_HANDLES ||
        !_file_slots[handle].open) return -1;
    _file_slots[handle].open = false;
    memset(&_file_slots[handle].file, 0, sizeof _file_slots[handle].file);
    return 0;
}

static int32_t _file_list(const char *path, mapp_dir_entry_fn callback, void *context) {
    if (!path || !callback) return -1;
    char abs[256];
    if (!_absolute_path(path, abs, sizeof abs)) return -1;
    mapp_list_context_t list_context = { callback, context };
    return (int32_t)fat_ls_ex(abs, _file_list_adapter, &list_context);
}

static int32_t _file_write(const char *path, const void *buffer, uint32_t length) {
    if (!path || (!buffer && length)) return -1;
    char abs[256];
    if (!_absolute_path(path, abs, sizeof abs)) return -1;
    return fat_write_atomic(abs, (const uint8_t *)buffer, length) == FAT_OK
        ? (int32_t)length : -1;
}

static int32_t _file_remove(const char *path) {
    if (!path) return -1;
    char abs[256];
    if (!_absolute_path(path, abs, sizeof abs)) return -1;
    return fat_unlink(abs) == FAT_OK ? 0 : -1;
}

static int32_t _file_mkdir(const char *path) {
    if (!path) return -1;
    char abs[256];
    if (!_absolute_path(path, abs, sizeof abs)) return -1;
    return fat_mkdir(abs) == FAT_OK ? 0 : -1;
}

static uint32_t _timer_millis(void) { return sys_time_ms(); }
static void _timer_sleep(uint32_t milliseconds) { sys_sleep(milliseconds); }

static int32_t _network_available(void) {
#ifdef PICO_CYW43_SUPPORTED
    return net_is_connected() ? 1 : 0;
#else
    return 0;
#endif
}

static int32_t _network_resolve(const char *hostname, uint32_t *address,
                                int32_t timeout_ms) {
#ifdef PICO_CYW43_SUPPORTED
    return net_dns_resolve(hostname, address, timeout_ms);
#else
    (void)hostname;
    (void)address;
    (void)timeout_ms;
    return -1;
#endif
}

static int32_t _network_http_get(const char *host, uint16_t port, const char *path,
                                 char *buffer, uint32_t capacity, int32_t timeout_ms) {
#ifdef PICO_CYW43_SUPPORTED
    return net_http_get(host, port, path, buffer, capacity, timeout_ms);
#else
    (void)host;
    (void)port;
    (void)path;
    (void)buffer;
    (void)capacity;
    (void)timeout_ms;
    return -1;
#endif
}

/* ---- Graphics ------------------------------------------------------- */

static uint32_t _gfx_bytes_in_use(void) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < MAPP_GFX_SURFACES; i++)
        if (_gfx_slots[i].pixels) total += _gfx_slots[i].bytes;
    return total;
}

static mapp_gfx_slot_t *_gfx_slot(void *surface) {
    gfx_surface_t *wanted = (gfx_surface_t *)surface;
    for (uint32_t i = 0; i < MAPP_GFX_SURFACES; i++) {
        if (_gfx_slots[i].pixels && &_gfx_slots[i].surface == wanted) return &_gfx_slots[i];
    }
    return NULL;
}

static void *_gfx_create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0 || width > LCD_WIDTH || height > LCD_HEIGHT) return NULL;
    uint32_t bytes = (uint32_t)width * height;
    if (_gfx_bytes_in_use() + bytes > MAPP_GFX_BYTES_MAX) return NULL;

    for (uint32_t i = 0; i < MAPP_GFX_SURFACES; i++) {
        if (_gfx_slots[i].pixels) continue;
        uint8_t *pixels = malloc(bytes);
        if (!pixels) return NULL;
        memset(pixels, 0, bytes);
        if (!gfx_surface_init(&_gfx_slots[i].surface, pixels, width, height)) {
            free(pixels);
            return NULL;
        }
        _gfx_slots[i].pixels = pixels;
        _gfx_slots[i].bytes = bytes;
        return &_gfx_slots[i].surface;
    }
    return NULL;
}

static void _gfx_destroy(void *surface) {
    mapp_gfx_slot_t *slot = _gfx_slot(surface);
    if (!slot) return;
    free(slot->pixels);
    slot->pixels = NULL;
    slot->bytes = 0;
    memset(&slot->surface, 0, sizeof slot->surface);
}

static void _gfx_release_all(void) {
    for (uint32_t i = 0; i < MAPP_GFX_SURFACES; i++) {
        if (_gfx_slots[i].pixels) _gfx_destroy(&_gfx_slots[i].surface);
    }
}

static void _gfx_clear(void *surface, uint8_t color) { gfx_clear((gfx_surface_t *)surface, color); }

static void _gfx_pixel(void *surface, int32_t x, int32_t y, uint8_t color) {
    gfx_pixel((gfx_surface_t *)surface, x, y, color);
}

static void _gfx_hline(void *surface, int32_t x, int32_t y, int32_t width, uint8_t color) {
    gfx_hline((gfx_surface_t *)surface, x, y, width, color);
}

static void _gfx_vline(void *surface, int32_t x, int32_t y, int32_t height, uint8_t color) {
    gfx_vline((gfx_surface_t *)surface, x, y, height, color);
}

static void _gfx_rect(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                      uint8_t color) {
    gfx_rect((gfx_surface_t *)surface, x, y, width, height, color);
}

static void _gfx_fill_rect(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                           uint8_t color) {
    gfx_fill_rect((gfx_surface_t *)surface, x, y, width, height, color);
}

static void _gfx_char(void *surface, int32_t x, int32_t y, char ch, uint8_t color) {
    const uint8_t *rows = lcd_font_glyph(ch);
    if (rows) gfx_glyph((gfx_surface_t *)surface, x, y, rows, LCD_CHAR_H, color);
}

static void _gfx_blit(void *dst, int32_t x, int32_t y, const void *src, int32_t transparent) {
    gfx_blit((gfx_surface_t *)dst, x, y, (const gfx_surface_t *)src, transparent);
}

static void _gfx_palette(uint8_t index, uint32_t rgb) {
    gfx_palette_set(index, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

static void _gfx_invalidate(void *surface) { gfx_invalidate((gfx_surface_t *)surface); }

static void _gfx_present(void *surface, int32_t x, int32_t y) {
    gfx_present_lcd((gfx_surface_t *)surface, x, y);
}

/* ---- Lifecycle and audio -------------------------------------------- */

static uint32_t _capabilities(void) {
    uint32_t caps = MAPP_CAP_DISPLAY | MAPP_CAP_FILES | MAPP_CAP_KEYS | MAPP_CAP_TIMER |
                    MAPP_CAP_GFX | MAPP_CAP_AUDIO | MAPP_CAP_AUDIO_MIXER | MAPP_CAP_YIELD;
#ifdef PICO_CYW43_SUPPORTED
    caps |= MAPP_CAP_NETWORK;
#endif
    return caps;
}

static void _yield(void) {
    watchdog_update();
    audio_service_poll();
}

static int32_t _audio_tone(int32_t freq_hz, int32_t duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return -1;
    audio_beep((int)freq_hz, (int)duration_ms);
    return 0;
}

static int32_t _audio_note(int32_t wave, int32_t freq_hz, int32_t duration_ms,
                           int32_t volume, int32_t attack_ms, int32_t release_ms,
                           int32_t voice) {
    if (wave < (int32_t)AUDIO_WAVE_SQUARE || wave > (int32_t)AUDIO_WAVE_NOISE) return -1;
    if (freq_hz < 20 || freq_hz > 20000 || duration_ms < 0) return -1;
    if (volume < 0 || volume > 255 || attack_ms < 0 || attack_ms > 65535 ||
        release_ms < 0 || release_ms > 65535) return -1;
    if (voice < 0 || voice >= MIXER_VOICES) return -1;
    audio_note((audio_wave_t)wave, (int)freq_hz, (int)duration_ms, (int)volume,
               (int)attack_ms, (int)release_ms, (int)voice);
    return 0;
}

static void _audio_voice_stop(int32_t voice) {
    if (voice >= 0 && voice < MIXER_VOICES) audio_voice_stop((int)voice);
}

static void _audio_stop(void) { audio_stop(); }

static const mapp_api_t _api = {
    .size = sizeof(mapp_api_t),
    .version = MAPP_API_VERSION,
    .display_clear = _display_clear,
    .display_putc = _display_putc,
    .display_print = _display_print,
    .display_set_cursor = _display_set_cursor,
    .display_set_color = _display_set_color,
    .display_columns = _display_columns,
    .display_rows = _display_rows,
    .keyboard_read = _keyboard_read,
    .keyboard_try_read = _keyboard_try_read,
    .keyboard_read_line = _keyboard_read_line,
    .file_open = _file_open,
    .file_read = _file_read,
    .file_close = _file_close,
    .file_list = _file_list,
    .file_write = _file_write,
    .file_remove = _file_remove,
    .file_mkdir = _file_mkdir,
    .timer_millis = _timer_millis,
    .timer_sleep = _timer_sleep,
    .network_available = _network_available,
    .network_resolve = _network_resolve,
    .network_http_get = _network_http_get,
    .capabilities = _capabilities,
    .yield = _yield,
    .gfx_create = _gfx_create,
    .gfx_destroy = _gfx_destroy,
    .gfx_clear = _gfx_clear,
    .gfx_pixel = _gfx_pixel,
    .gfx_hline = _gfx_hline,
    .gfx_vline = _gfx_vline,
    .gfx_rect = _gfx_rect,
    .gfx_fill_rect = _gfx_fill_rect,
    .gfx_char = _gfx_char,
    .gfx_blit = _gfx_blit,
    .gfx_palette = _gfx_palette,
    .gfx_invalidate = _gfx_invalidate,
    .gfx_present = _gfx_present,
    .audio_tone = _audio_tone,
    .audio_stop = _audio_stop,
    .audio_note = _audio_note,
    .audio_voice_stop = _audio_voice_stop,
};

static bool _bytecode_run(const uint8_t *code, uint32_t length) {
    int32_t stack[32];
    uint32_t sp = 0;
    uint32_t pc = 0;
    while (pc < length) {
        uint8_t op = code[pc++];
        switch (op) {
        case BC_HALT:
            return true;
        case BC_PRINT: {
            if (pc >= length) return false;
            uint32_t count = code[pc++];
            if (count > length - pc) return false;
            for (uint32_t i = 0; i < count; i++) sys_putchar((char)code[pc++]);
            break;
        }
        case BC_PUTC:
            if (pc >= length) return false;
            sys_putchar((char)code[pc++]);
            break;
        case BC_READ_KEY:
            if (sp >= sizeof stack / sizeof stack[0]) return false;
            stack[sp++] = sys_read_key();
            break;
        case BC_TIME_MS:
            if (sp >= sizeof stack / sizeof stack[0]) return false;
            stack[sp++] = (int32_t)sys_time_ms();
            break;
        case BC_SLEEP_MS:
            if (sp == 0) return false;
            sys_sleep((uint32_t)stack[--sp]);
            break;
        case BC_PUSH_I32:
            if (length - pc < 4 || sp >= sizeof stack / sizeof stack[0]) return false;
            stack[sp++] = (int32_t)_read_u32(code + pc);
            pc += 4;
            break;
        case BC_ADD:
        case BC_SUB:
        case BC_MUL:
            if (sp < 2) return false;
            if (op == BC_ADD)
                stack[sp - 2] = (int32_t)((uint32_t)stack[sp - 2] + (uint32_t)stack[sp - 1]);
            else if (op == BC_SUB)
                stack[sp - 2] = (int32_t)((uint32_t)stack[sp - 2] - (uint32_t)stack[sp - 1]);
            else
                stack[sp - 2] = (int32_t)((uint32_t)stack[sp - 2] * (uint32_t)stack[sp - 1]);
            sp--;
            break;
        case BC_PRINT_I32: {
            if (sp == 0) return false;
            char text[16];
            (void)snprintf(text, sizeof text, "%ld", (long)stack[--sp]);
            sys_print(text);
            break;
        }
        case BC_JUMP:
        case BC_JUMP_ZERO: {
            if (length - pc < 2) return false;
            int16_t displacement = (int16_t)_read_u16(code + pc);
            pc += 2;
            bool take = true;
            if (op == BC_JUMP_ZERO) {
                if (sp == 0) return false;
                take = stack[--sp] == 0;
            }
            if (take) {
                int64_t target = (int64_t)pc + displacement;
                if (target < 0 || target >= length) return false;
                pc = (uint32_t)target;
            }
            break;
        }
        case BC_CLEAR:
            sys_clear();
            break;
        case BC_DUP:
            if (sp == 0 || sp >= sizeof stack / sizeof stack[0]) return false;
            stack[sp] = stack[sp - 1];
            sp++;
            break;
        case BC_DROP:
            if (sp == 0) return false;
            sp--;
            break;
        default:
            return false;
        }
        if (_api.timer_millis) {
            static uint32_t last_yield;
            uint32_t now = _api.timer_millis();
            if (now - last_yield >= 250u) {
                watchdog_update();
                last_yield = now;
            }
        }
    }
    return true;
}

static bool _load_mapp(const char *path) {
    uint8_t *arena = malloc(MAPP_ARENA_SIZE);
    if (!arena) return false;

    bool ok = false;
    fat_file_t file;
    if (fat_open(path, &file) != FAT_OK || file.size < MAPP_HEADER_SIZE) goto done;
    uint8_t raw_header[MAPP_HEADER_SIZE];
    if (!_read_exact(&file, raw_header, sizeof raw_header)) goto done;

    mapp_image_header_t image;
    mapp_image_parse_header(&image, raw_header);
    image.file_size = file.size;
    if (mapp_image_validate(&image) != MAPP_IMAGE_OK) goto done;

    uint16_t kind = image.kind;
    uint32_t image_size = image.image_size;
    uint32_t bss_size = image.bss_size;
    uint32_t entry_offset = image.entry_offset;
    uint32_t relocation_count = image.relocation_count;

    uint8_t reloc_raw[MAPP_RELOC_MAX * sizeof(uint32_t)];
    uint32_t reloc_bytes = relocation_count * (uint32_t)sizeof(uint32_t);
    if (reloc_bytes && !_read_exact(&file, reloc_raw, reloc_bytes)) goto done;
    for (uint32_t i = 0; i < relocation_count; i++)
        _relocations[i] = _read_u32(reloc_raw + i * sizeof(uint32_t));
    if (!_read_exact(&file, arena, image_size)) goto done;
    memset(arena + image_size, 0, bss_size);

    if (kind == MAPP_KIND_BYTECODE) {
        ok = _bytecode_run(arena + entry_offset, image_size - entry_offset);
        goto done;
    }

    if (mapp_image_apply_relocations(arena, image_size, bss_size, _relocations,
                                     relocation_count,
                                     (uint32_t)(uintptr_t)arena) != MAPP_RELOC_OK)
        goto done;

    mapp_entry_fn entry = (mapp_entry_fn)(uintptr_t)
        ((uintptr_t)(arena + entry_offset) | (uintptr_t)1u);
    char *argv[1];
    argv[0] = (char *)path;
    (void)entry(&_api, 1, argv);
    ok = true;

done:
    free(arena);
    return ok;
}

void mapp_launch(const mapp_app_t *app) {
    if (!app) return;
    char path[264];
    (void)snprintf(path, sizeof path, "/apps/%s", app->filename);
    switch (app->kind) {
    case MAPP_APP_THUMB:
        if (_mapp_running) {
            ui_error("app", "another module is already running");
            return;
        }
        memset(_file_slots, 0, sizeof _file_slots);
        _mapp_running = true;
        gfx_palette_reset();
        if (!_load_mapp(path)) ui_error("app", "invalid or incompatible .mapp file");
        for (uint32_t i = 0; i < MAPP_FILE_HANDLES; i++) {
            if (_file_slots[i].open) (void)_file_close((int32_t)i);
        }
        _gfx_release_all();
        _mapp_running = false;
        break;
    case MAPP_APP_BASIC:
        app_basic(path);
        break;
    case MAPP_APP_TINYC:
        app_tinyc(path);
        break;
    case MAPP_APP_FORTH:
        app_forth(path);
        break;
    }
    if (_catalog_overflow) ui_warn("apps: showing first 24 matching files");
}

bool mapp_run(const char *name) {
    if (!name || !*name) return false;
    int32_t count = mapp_discover();
    if (count <= 0) return false;

    for (int32_t i = 0; i < count; i++) {
        const mapp_app_t *app = mapp_catalog_get((uint32_t)i);
        if (!app || !app->filename[0]) continue;

        /* Match the full filename, then the stem without its extension. */
        if (ci_eq(app->filename, name)) {
            mapp_launch(app);
            return true;
        }
        char stem[256];
        _copy_string(stem, sizeof stem, app->filename);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';
        if (ci_eq(stem, name)) {
            mapp_launch(app);
            return true;
        }
    }
    return false;
}
