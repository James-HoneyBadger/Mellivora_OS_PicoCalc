#pragma once
/*
 * core/gfx.h — indexed-colour surfaces, sprite blits, and dirty rectangles.
 *
 * This is the drawing model behind the `.mapp` graphics ABI. Surfaces are
 * 8-bit indexed and owned by the caller (an app gets its pixel buffer from
 * the loader's allocator), because a 320x320 RGB565 framebuffer would be
 * 200 KiB and neither board can spare it:
 *
 *     320 x 320 x 1 = 100.0 KiB     RP2350 only
 *     160 x 160 x 1 =  25.0 KiB     comfortable on both
 *      80 x  80 x 1 =   6.3 KiB     RP2040 friendly
 *
 * Redrawing is dirty-rectangle driven. `gfx_present` only pushes the region
 * that actually changed, which is what keeps frame times sane given the
 * panel's ~307 KiB full-frame SPI cost.
 *
 * Everything here is portable and host-tested; the LCD driver converts a
 * dirty region to pixels with gfx_row_to_rgb888().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_PALETTE_SIZE 256
#define GFX_CHAR_WIDTH 8
#define GFX_TRANSPARENT_NONE (-1)

typedef struct {
    uint8_t *pixels; /* width * height bytes, row major */
    uint16_t width;
    uint16_t height;
    /* Bounding box of everything drawn since the last gfx_dirty_reset(). */
    bool dirty_any;
    uint16_t dirty_x0, dirty_y0, dirty_x1, dirty_y1; /* exclusive */
} gfx_surface_t;

/* Bind a caller-owned buffer. Returns false when the geometry is empty or
   exceeds the 16-bit coordinate range the panel uses. */
bool gfx_surface_init(gfx_surface_t *surface, uint8_t *pixels, uint16_t width, uint16_t height);

/* ---- Palette (one per running app) ---------------------------------- */

/* xterm-style default: 16 ANSI colours, a 6x6x6 cube, then 24 greys. */
void gfx_palette_reset(void);
void gfx_palette_set(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
bool gfx_palette_get(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b);

/* ---- Drawing (all coordinates clip; nothing draws out of bounds) ----- */

void gfx_clear(gfx_surface_t *surface, uint8_t color);
void gfx_pixel(gfx_surface_t *surface, int32_t x, int32_t y, uint8_t color);

/* Read one pixel; returns 0 (palette index, not "transparent") out of range.
   Apps need this for collision checks against a drawn mask. */
uint8_t gfx_get_pixel(const gfx_surface_t *surface, int32_t x, int32_t y);
void gfx_hline(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, uint8_t color);
void gfx_vline(gfx_surface_t *surface, int32_t x, int32_t y, int32_t height, uint8_t color);
void gfx_rect(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height,
              uint8_t color);
void gfx_fill_rect(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                   uint8_t color);

/* Copy `src` onto `dst` with its top-left at (x, y). `transparent` skips one
   palette index, or pass GFX_TRANSPARENT_NONE for an opaque copy. */
void gfx_blit(gfx_surface_t *dst, int32_t x, int32_t y, const gfx_surface_t *src,
              int32_t transparent);

/* Draw an 8-pixel-wide glyph from `rows` (one byte per row, bit 7 leftmost). */
void gfx_glyph(gfx_surface_t *surface, int32_t x, int32_t y, const uint8_t *rows, int32_t height,
               uint8_t color);

/* ---- Dirty tracking ------------------------------------------------- */

void gfx_dirty_reset(gfx_surface_t *surface);

/* Mark the whole surface dirty. Palette-only animation changes no pixel, so
   it needs this to make the next present resend the frame. */
void gfx_invalidate(gfx_surface_t *surface);

/* Bounding box of changes since the last reset, or false when clean. */
bool gfx_dirty_bounds(const gfx_surface_t *surface, uint16_t *x, uint16_t *y, uint16_t *width,
                      uint16_t *height);

/* ---- Conversion for the panel --------------------------------------- */

/* Expand `width` pixels of row `y`, starting at `x`, to 3 bytes per pixel
   (R, G, B) in `out`. Returns the byte count written, or 0 when out of
   range. `out` must hold at least width * 3 bytes. */
size_t gfx_row_to_rgb888(const gfx_surface_t *surface, uint16_t y, uint16_t x, uint16_t width,
                         uint8_t *out);
