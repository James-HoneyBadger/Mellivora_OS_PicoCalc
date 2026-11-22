#pragma once

#include <stddef.h>
#include <stdint.h>

#define MAPP_FORMAT_VERSION 1u
#define MAPP_KIND_THUMB 1u
#define MAPP_KIND_BYTECODE 2u
/* The API table grows by appending. An app compiled against v1 keeps working
   on a v2 loader: it only reads the fields it knows about, and `size` tells
   it how much of the table exists. Never reorder or remove a field. */
#define MAPP_API_VERSION 3u
#define MAPP_API_VERSION_MIN 1u

/* Capability bits reported by mapp_api_t::capabilities. An app must test the
   bit (and the matching function pointer) before using an optional call. */
#define MAPP_CAP_DISPLAY (1u << 0)
#define MAPP_CAP_FILES (1u << 1)
#define MAPP_CAP_KEYS (1u << 2)
#define MAPP_CAP_NETWORK (1u << 3)
#define MAPP_CAP_TIMER (1u << 4)
#define MAPP_CAP_GFX (1u << 5)
#define MAPP_CAP_AUDIO (1u << 6)
#define MAPP_CAP_YIELD (1u << 7)
#define MAPP_CAP_AUDIO_MIXER (1u << 8)

/* audio_note() waveforms. */
#define MAPP_WAVE_SQUARE 0
#define MAPP_WAVE_TRIANGLE 1
#define MAPP_WAVE_NOISE 2

#define MAPP_HEADER_SIZE 60u

#define MAPP_FILE_READ 0u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t kind;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t relocation_count;
    char name[32];
} __attribute__((packed)) mapp_header_t;

#ifdef __cplusplus
static_assert(sizeof(mapp_header_t) == MAPP_HEADER_SIZE, "MAPP header layout changed");
#else
_Static_assert(sizeof(mapp_header_t) == MAPP_HEADER_SIZE, "MAPP header layout changed");
#endif

typedef void (*mapp_dir_entry_fn)(const char *short_name, const char *long_name,
                                  uint32_t size, uint32_t is_dir, void *context);

typedef struct {
    uint32_t size;
    uint32_t version;

    void (*display_clear)(void);
    void (*display_putc)(char ch);
    void (*display_print)(const char *text);
    void (*display_set_cursor)(int32_t x, int32_t y);
    void (*display_set_color)(uint32_t foreground, uint32_t background);
    int32_t (*display_columns)(void);
    int32_t (*display_rows)(void);

    int32_t (*keyboard_read)(void);
    int32_t (*keyboard_try_read)(void);
    int32_t (*keyboard_read_line)(const char *prompt, char *buffer, uint32_t capacity);

    int32_t (*file_open)(const char *path, uint32_t flags);
    int32_t (*file_read)(int32_t handle, void *buffer, uint32_t capacity);
    int32_t (*file_close)(int32_t handle);
    int32_t (*file_list)(const char *path, mapp_dir_entry_fn callback, void *context);
    int32_t (*file_write)(const char *path, const void *buffer, uint32_t length);
    int32_t (*file_remove)(const char *path);
    int32_t (*file_mkdir)(const char *path);

    uint32_t (*timer_millis)(void);
    void (*timer_sleep)(uint32_t milliseconds);

    int32_t (*network_available)(void);
    int32_t (*network_resolve)(const char *hostname, uint32_t *address, int32_t timeout_ms);
    int32_t (*network_http_get)(const char *host, uint16_t port, const char *path,
                                char *buffer, uint32_t capacity, int32_t timeout_ms);

    /* ---- v2: capabilities and lifecycle ------------------------------ */

    /* MAPP_CAP_* bits describing what this loader actually implements. */
    uint32_t (*capabilities)(void);

    /* Service the watchdog and the background workers. Call it at least
       every couple of seconds inside long loops; frame-paced apps that use
       audio or the network should call it every frame. */
    void (*yield)(void);

    /* ---- v2: graphics ------------------------------------------------ */

    /* Allocate an indexed surface. Returns NULL when the arena is exhausted.
       The buffer is freed for you when the app returns; gfx_destroy() only
       releases it early. */
    void *(*gfx_create)(uint16_t width, uint16_t height);
    void (*gfx_destroy)(void *surface);

    void (*gfx_clear)(void *surface, uint8_t color);
    void (*gfx_pixel)(void *surface, int32_t x, int32_t y, uint8_t color);
    void (*gfx_hline)(void *surface, int32_t x, int32_t y, int32_t width, uint8_t color);
    void (*gfx_vline)(void *surface, int32_t x, int32_t y, int32_t height, uint8_t color);
    void (*gfx_rect)(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                     uint8_t color);
    void (*gfx_fill_rect)(void *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                          uint8_t color);
    /* Draw an 8x16 glyph of the built-in font at (x, y). */
    void (*gfx_char)(void *surface, int32_t x, int32_t y, char ch, uint8_t color);
    /* Copy `src` to `dst` at (x, y); `transparent` is a palette index to
       skip, or -1 for an opaque copy. */
    void (*gfx_blit)(void *dst, int32_t x, int32_t y, const void *src, int32_t transparent);
    /* Recolour one palette entry; the index is global to the app. */
    void (*gfx_palette)(uint8_t index, uint32_t rgb);
    /* Mark the whole surface as changed (needed after palette-only animation). */
    void (*gfx_invalidate)(void *surface);
    /* Push only the region changed since the previous present to the panel. */
    void (*gfx_present)(void *surface, int32_t x, int32_t y);

    /* ---- v2: audio --------------------------------------------------- */

    /* Queue a tone; returns 0 when accepted. Non-blocking. */
    int32_t (*audio_tone)(int32_t freq_hz, int32_t duration_ms);
    void (*audio_stop)(void);

    /* ---- v3: polyphonic voices ---------------------------------------- */

    /* Start (or restart) voice 0-3 with an AD envelope; returns 0 on
       success. Test MAPP_CAP_AUDIO_MIXER first. */
    int32_t (*audio_note)(int32_t wave, int32_t freq_hz, int32_t duration_ms, int32_t volume,
                          int32_t attack_ms, int32_t release_ms, int32_t voice);
    /* Begin the release phase of one voice. */
    void (*audio_voice_stop)(int32_t voice);
} mapp_api_t;

/* The v1 prefix must never move. If either assert fires, an old binary would
   read a new table through the wrong offsets. */
/* size + version are two 32-bit words, then the pointer table begins. */
_Static_assert(offsetof(mapp_api_t, display_clear) == 8, "mapp_api_t v1 prefix changed");
_Static_assert(offsetof(mapp_api_t, capabilities) > offsetof(mapp_api_t, network_http_get),
               "mapp_api_t v2 fields must follow the v1 prefix");

typedef int32_t (*mapp_entry_fn)(const mapp_api_t *api, int32_t argc, char **argv);

typedef enum {
    MAPP_APP_THUMB = 1,
    MAPP_APP_BASIC,
    MAPP_APP_TINYC,
    MAPP_APP_FORTH,
} mapp_app_kind_t;

typedef struct {
    char filename[256];
    char title[48];
    mapp_app_kind_t kind;
} mapp_app_t;

/* Discovery is refreshed each time the launcher opens. Returns count or a
   negative FAT error. The catalog is owned by the loader and remains valid
   until the next discovery call. */
int32_t mapp_discover(void);
int32_t mapp_catalog_truncated(void);
const mapp_app_t *mapp_catalog_get(uint32_t index);
void mapp_launch(const mapp_app_t *app);

#ifdef __cplusplus
}
#endif
