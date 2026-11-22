/*
 * test_gfx.c — indexed surfaces, blits, and dirty rectangles.
 */

#include "core/gfx.h"
#include "test_util.h"

#include <string.h>

#define W 16
#define H 8

static uint8_t pixels[W * H];
static uint8_t sprite_pixels[4 * 3];
static gfx_surface_t surface;
static gfx_surface_t sprite;

void test_gfx(void) {
    gfx_palette_reset();

    /* The default palette follows the xterm layout the ABI documents. */
    uint8_t r = 0, g = 0, b = 0;
    CHECK(gfx_palette_get(0, &r, &g, &b));
    CHECK_EQ(r, 0);
    CHECK_EQ(g, 0);
    CHECK_EQ(b, 0);
    CHECK(gfx_palette_get(9, &r, &g, &b));
    CHECK_EQ(r, 255); /* bright red */
    CHECK(gfx_palette_get(15, &r, &g, &b));
    CHECK_EQ(b, 255); /* white */
    CHECK(gfx_palette_get(196, &r, &g, &b));
    CHECK_EQ(r, 255); /* 16 + 180: first cube corner is pure red */
    CHECK_EQ(g, 0);
    CHECK_EQ(b, 0);
    CHECK(gfx_palette_get(232, &r, &g, &b));
    CHECK_EQ(r, 8); /* darkest grey */

    gfx_palette_set(3, 1, 2, 3);
    CHECK(gfx_palette_get(3, &r, &g, &b));
    CHECK_EQ(r, 1);
    CHECK_EQ(g, 2);
    CHECK_EQ(b, 3);
    gfx_palette_reset();

    gfx_palette_set(200, 0x12, 0x34, 0x56);
    CHECK(!gfx_palette_get(0, NULL, &g, &b));

    /* ---- Geometry ----------------------------------------------------- */

    memset(pixels, 0, sizeof pixels);
    CHECK(gfx_surface_init(&surface, pixels, W, H));
    CHECK_EQ(surface.width, W);
    CHECK_EQ(surface.height, H);
    CHECK(!surface.dirty_any);
    CHECK(!gfx_surface_init(&surface, pixels, 0, H));
    CHECK(!gfx_surface_init(NULL, pixels, W, H));
    CHECK(!gfx_surface_init(&surface, NULL, W, H));
    CHECK(gfx_surface_init(&surface, pixels, W, H));

    /* A fresh surface is clean, and drawing marks the exact bounding box. */
    gfx_pixel(&surface, 3, 2, 7);
    CHECK(gfx_get_pixel(&surface, 3, 2) == 7);
    uint16_t x, y, w, h;
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 3);
    CHECK_EQ(y, 2);
    CHECK_EQ(w, 1);
    CHECK_EQ(h, 1);

    gfx_pixel(&surface, 10, 6, 7);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 3);
    CHECK_EQ(y, 2);
    CHECK_EQ(w, 8);
    CHECK_EQ(h, 5);

    gfx_dirty_reset(&surface);
    CHECK(!gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK(!surface.dirty_any);

    /* Palette-only animation needs a way to force a full resend. */
    gfx_dirty_reset(&surface);
    gfx_invalidate(&surface);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(w, W);
    CHECK_EQ(h, H);

    /* Out-of-bounds pixels are dropped without marking anything. */
    gfx_dirty_reset(&surface);
    gfx_pixel(&surface, -1, 0, 9);
    gfx_pixel(&surface, W, 0, 9);
    gfx_pixel(&surface, 0, -1, 9);
    gfx_pixel(&surface, 0, H, 9);
    CHECK(!surface.dirty_any);

    /* ---- Lines and rectangles clip to the surface --------------------- */

    gfx_clear(&surface, 1);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(w, W);
    CHECK_EQ(h, H);
    CHECK(gfx_get_pixel(&surface, 0, 0) == 1);
    CHECK(gfx_get_pixel(&surface, W - 1, H - 1) == 1);

    gfx_dirty_reset(&surface);
    gfx_hline(&surface, -4, 3, 6, 5); /* columns 0..1 */
    CHECK(gfx_get_pixel(&surface, 0, 3) == 5);
    CHECK(gfx_get_pixel(&surface, 1, 3) == 5);
    CHECK(gfx_get_pixel(&surface, 2, 3) == 1);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(w, 2);

    gfx_dirty_reset(&surface);
    gfx_hline(&surface, W - 2, 4, 10, 5); /* clipped to 2 columns */
    CHECK(gfx_get_pixel(&surface, W - 1, 4) == 5);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, W - 2);
    CHECK_EQ(w, 2);

    gfx_dirty_reset(&surface);
    gfx_vline(&surface, 2, H - 1, 5, 6); /* one row */
    CHECK(gfx_get_pixel(&surface, 2, H - 1) == 6);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(h, 1);

    gfx_dirty_reset(&surface);
    gfx_fill_rect(&surface, 4, 1, 6, 3, 2);
    for (int row = 1; row < 4; row++)
        for (int col = 4; col < 10; col++)
            CHECK(gfx_get_pixel(&surface, col, row) == 2);
    CHECK(gfx_get_pixel(&surface, 3, 1) == 1);
    CHECK(gfx_get_pixel(&surface, 10, 1) == 1);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 4);
    CHECK_EQ(y, 1);
    CHECK_EQ(w, 6);
    CHECK_EQ(h, 3);

    /* A fully off-screen rectangle must not disturb the surface. */
    gfx_dirty_reset(&surface);
    gfx_fill_rect(&surface, -20, -20, 5, 5, 9);
    gfx_fill_rect(&surface, W + 1, 0, 5, 5, 9);
    gfx_fill_rect(&surface, 0, H + 1, 5, 5, 9);
    gfx_hline(&surface, 0, 1, 0, 9);
    gfx_vline(&surface, 1, 0, -3, 9);
    CHECK(!surface.dirty_any);

    /* Outline covers the border only. */
    gfx_clear(&surface, 0);
    gfx_dirty_reset(&surface);
    gfx_rect(&surface, 2, 1, 5, 4, 3);
    CHECK(gfx_get_pixel(&surface, 2, 1) == 3);
    CHECK(gfx_get_pixel(&surface, 6, 1) == 3);
    CHECK(gfx_get_pixel(&surface, 2, 4) == 3);
    CHECK(gfx_get_pixel(&surface, 6, 4) == 3);
    CHECK(gfx_get_pixel(&surface, 3, 2) == 0); /* hollow centre */

    /* ---- Sprite blits ------------------------------------------------- */

    memset(sprite_pixels, 0, sizeof sprite_pixels);
    CHECK(gfx_surface_init(&sprite, sprite_pixels, 4, 3));
    for (int i = 0; i < 4 * 3; i++)
        sprite_pixels[i] = 4;
    sprite_pixels[0] = 0; /* transparent corner */
    sprite_pixels[5] = 9; /* interior pixel */

    gfx_clear(&surface, 1);
    gfx_dirty_reset(&surface);
    gfx_blit(&surface, 2, 1, &sprite, 0);
    CHECK(gfx_get_pixel(&surface, 2, 1) == 1); /* transparent index kept */
    CHECK(gfx_get_pixel(&surface, 3, 1) == 4);
    CHECK(gfx_get_pixel(&surface, 3, 2) == 9);
    CHECK_EQ(sprite.width, 4);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 2);
    CHECK_EQ(y, 1);
    CHECK_EQ(w, 4);
    CHECK_EQ(h, 3);

    /* Opaque blits copy every pixel, including index 0. */
    gfx_blit(&surface, 2, 1, &sprite, GFX_TRANSPARENT_NONE);
    CHECK(gfx_get_pixel(&surface, 2, 1) == 0);

    /* Clipping on all four sides. Restore the corner so an opaque copy is
       distinguishable from an untouched background. */
    sprite_pixels[0] = 4;
    gfx_clear(&surface, 1);
    gfx_dirty_reset(&surface);
    gfx_blit(&surface, -2, -1, &sprite, GFX_TRANSPARENT_NONE);
    CHECK(gfx_get_pixel(&surface, 0, 0) == 4);
    CHECK(gfx_get_pixel(&surface, 1, 1) == 4);
    CHECK(gfx_get_pixel(&surface, 2, 2) == 1);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(w, 2);
    CHECK_EQ(h, 2);

    gfx_clear(&surface, 1);
    gfx_dirty_reset(&surface);
    gfx_blit(&surface, W - 1, H - 1, &sprite, GFX_TRANSPARENT_NONE);
    CHECK(gfx_get_pixel(&surface, W - 1, H - 1) == 4);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(w, 1);
    CHECK_EQ(h, 1);

    /* A sprite entirely outside changes nothing. */
    gfx_dirty_reset(&surface);
    gfx_blit(&surface, W, 0, &sprite, GFX_TRANSPARENT_NONE);
    gfx_blit(&surface, 0, H, &sprite, GFX_TRANSPARENT_NONE);
    gfx_blit(&surface, -sprite.width, 0, &sprite, GFX_TRANSPARENT_NONE);
    CHECK(!surface.dirty_any);

    /* ---- Glyphs ------------------------------------------------------- */

    gfx_clear(&surface, 0);
    gfx_dirty_reset(&surface);
    static const uint8_t glyph[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    gfx_glyph(&surface, 1, 0, glyph, 8, 5);
    CHECK(gfx_get_pixel(&surface, 1, 0) == 5);
    CHECK(gfx_get_pixel(&surface, 2, 1) == 5);
    CHECK(gfx_get_pixel(&surface, 8, 7) == 5);
    CHECK(gfx_get_pixel(&surface, 2, 0) == 0);
    CHECK(gfx_dirty_bounds(&surface, &x, &y, &w, &h));
    CHECK_EQ(x, 1);
    CHECK_EQ(w, 8);
    CHECK_EQ(h, 8);

    /* ---- Conversion to panel pixels ----------------------------------- */

    uint8_t rgb[GFX_CHAR_WIDTH * 3];
    memset(rgb, 0, sizeof rgb);
    gfx_palette_set(200, 0x12, 0x34, 0x56);
    gfx_clear(&surface, 200);
    CHECK_EQ(gfx_row_to_rgb888(&surface, 0, 0, 2, rgb), 6u);
    CHECK_EQ(rgb[0], 0x12);
    CHECK_EQ(rgb[1], 0x34);
    CHECK_EQ(rgb[2], 0x56);
    CHECK_EQ(rgb[3], 0x12);

    /* Widths clamp at the right edge, and bad rows convert nothing. */
    CHECK_EQ(gfx_row_to_rgb888(&surface, 0, W - 2, 8, rgb), 6u);
    CHECK_EQ(gfx_row_to_rgb888(&surface, H, 0, 2, rgb), 0u);
    CHECK_EQ(gfx_row_to_rgb888(&surface, 0, W, 2, rgb), 0u);
    CHECK_EQ(gfx_row_to_rgb888(&surface, 0, 0, 0, rgb), 0u);
    CHECK_EQ(gfx_row_to_rgb888(NULL, 0, 0, 2, rgb), 0u);
}
