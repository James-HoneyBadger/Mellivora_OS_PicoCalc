/*
 * core/gfx.c — indexed-colour surfaces, sprite blits, and dirty rectangles.
 */

#include "core/gfx.h"

#include <string.h>

static uint8_t _palette[GFX_PALETTE_SIZE][3];

static const uint8_t _ansi16[16][3] = {
    {0x00, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0x00, 0x80, 0x00}, {0x80, 0x80, 0x00},
    {0x00, 0x00, 0x80}, {0x80, 0x00, 0x80}, {0x00, 0x80, 0x80}, {0xC0, 0xC0, 0xC0},
    {0x80, 0x80, 0x80}, {0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00}, {0xFF, 0xFF, 0x00},
    {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
};

static const uint8_t _cube6[6] = {0, 95, 135, 175, 215, 255};

void gfx_palette_reset(void) {
    for (int i = 0; i < 16; i++) {
        _palette[i][0] = _ansi16[i][0];
        _palette[i][1] = _ansi16[i][1];
        _palette[i][2] = _ansi16[i][2];
    }
    for (int i = 0; i < 216; i++) {
        _palette[16 + i][0] = _cube6[i / 36];
        _palette[16 + i][1] = _cube6[(i / 6) % 6];
        _palette[16 + i][2] = _cube6[i % 6];
    }
    for (int i = 0; i < 24; i++) {
        uint8_t level = (uint8_t)(8 + i * 10);
        _palette[232 + i][0] = level;
        _palette[232 + i][1] = level;
        _palette[232 + i][2] = level;
    }
}

void gfx_palette_set(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    _palette[index][0] = r;
    _palette[index][1] = g;
    _palette[index][2] = b;
}

bool gfx_palette_get(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!r || !g || !b) return false;
    *r = _palette[index][0];
    *g = _palette[index][1];
    *b = _palette[index][2];
    return true;
}

bool gfx_surface_init(gfx_surface_t *surface, uint8_t *pixels, uint16_t width, uint16_t height) {
    if (!surface || !pixels || width == 0 || height == 0) return false;
    memset(surface, 0, sizeof *surface);
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    return true;
}

static void _mark(gfx_surface_t *surface, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (!surface) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)surface->width) x1 = surface->width;
    if (y1 > (int32_t)surface->height) y1 = surface->height;
    if (x0 >= x1 || y0 >= y1) return;

    if (!surface->dirty_any) {
        surface->dirty_any = true;
        surface->dirty_x0 = (uint16_t)x0;
        surface->dirty_y0 = (uint16_t)y0;
        surface->dirty_x1 = (uint16_t)x1;
        surface->dirty_y1 = (uint16_t)y1;
        return;
    }
    if (x0 < surface->dirty_x0) surface->dirty_x0 = (uint16_t)x0;
    if (y0 < surface->dirty_y0) surface->dirty_y0 = (uint16_t)y0;
    if (x1 > surface->dirty_x1) surface->dirty_x1 = (uint16_t)x1;
    if (y1 > surface->dirty_y1) surface->dirty_y1 = (uint16_t)y1;
}

void gfx_dirty_reset(gfx_surface_t *surface) {
    if (!surface) return;
    surface->dirty_any = false;
    surface->dirty_x0 = surface->dirty_y0 = surface->dirty_x1 = surface->dirty_y1 = 0;
}

void gfx_invalidate(gfx_surface_t *surface) {
    if (!surface) return;
    _mark(surface, 0, 0, surface->width, surface->height);
}

bool gfx_dirty_bounds(const gfx_surface_t *surface, uint16_t *x, uint16_t *y, uint16_t *width,
                      uint16_t *height) {
    if (!surface || !surface->dirty_any) return false;
    if (x) *x = surface->dirty_x0;
    if (y) *y = surface->dirty_y0;
    if (width) *width = (uint16_t)(surface->dirty_x1 - surface->dirty_x0);
    if (height) *height = (uint16_t)(surface->dirty_y1 - surface->dirty_y0);
    return true;
}

void gfx_clear(gfx_surface_t *surface, uint8_t color) {
    if (!surface) return;
    memset(surface->pixels, color, (size_t)surface->width * surface->height);
    _mark(surface, 0, 0, surface->width, surface->height);
}

void gfx_pixel(gfx_surface_t *surface, int32_t x, int32_t y, uint8_t color) {
    if (!surface) return;
    if (x < 0 || y < 0 || x >= (int32_t)surface->width || y >= (int32_t)surface->height) return;
    surface->pixels[(size_t)y * surface->width + (size_t)x] = color;
    _mark(surface, x, y, x + 1, y + 1);
}

uint8_t gfx_get_pixel(const gfx_surface_t *surface, int32_t x, int32_t y) {
    if (!surface || !surface->pixels) return 0;
    if (x < 0 || y < 0 || x >= (int32_t)surface->width || y >= (int32_t)surface->height) return 0;
    return surface->pixels[(size_t)y * surface->width + (size_t)x];
}

void gfx_hline(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, uint8_t color) {
    if (!surface || width <= 0) return;
    if (y < 0 || y >= (int32_t)surface->height) return;
    int32_t x0 = x < 0 ? 0 : x;
    int64_t x1 = (int64_t)x + width;
    if (x1 > surface->width) x1 = surface->width;
    if (x0 >= x1) return;
    memset(surface->pixels + (size_t)y * surface->width + (size_t)x0, color, (size_t)(x1 - x0));
    _mark(surface, x0, y, (int32_t)x1, y + 1);
}

void gfx_vline(gfx_surface_t *surface, int32_t x, int32_t y, int32_t height, uint8_t color) {
    if (!surface || height <= 0) return;
    if (x < 0 || x >= (int32_t)surface->width) return;
    int32_t y0 = y < 0 ? 0 : y;
    int64_t y1 = (int64_t)y + height;
    if (y1 > surface->height) y1 = surface->height;
    if (y0 >= y1) return;
    for (int32_t row = y0; row < (int32_t)y1; row++)
        surface->pixels[(size_t)row * surface->width + (size_t)x] = color;
    _mark(surface, x, y0, x + 1, (int32_t)y1);
}

void gfx_fill_rect(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height,
                   uint8_t color) {
    if (!surface || width <= 0 || height <= 0) return;
    int32_t y0 = y < 0 ? 0 : y;
    int64_t y1 = (int64_t)y + height;
    if (y1 > surface->height) y1 = surface->height;
    for (int32_t row = y0; row < (int32_t)y1; row++)
        gfx_hline(surface, x, row, width, color);
}

void gfx_rect(gfx_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height,
              uint8_t color) {
    if (!surface || width <= 0 || height <= 0) return;
    gfx_hline(surface, x, y, width, color);
    if (height > 1) gfx_hline(surface, x, y + height - 1, width, color);
    gfx_vline(surface, x, y, height, color);
    if (width > 1) gfx_vline(surface, x + width - 1, y, height, color);
}

void gfx_blit(gfx_surface_t *dst, int32_t x, int32_t y, const gfx_surface_t *src,
              int32_t transparent) {
    if (!dst || !src || !src->pixels) return;

    int32_t dx0 = x < 0 ? 0 : x;
    int32_t dy0 = y < 0 ? 0 : y;
    int64_t dx1 = (int64_t)x + src->width;
    int64_t dy1 = (int64_t)y + src->height;
    if (dx1 > dst->width) dx1 = dst->width;
    if (dy1 > dst->height) dy1 = dst->height;
    if (dx0 >= dx1 || dy0 >= dy1) return;

    for (int32_t row = dy0; row < (int32_t)dy1; row++) {
        const uint8_t *src_row = src->pixels + (size_t)(row - y) * src->width + (size_t)(dx0 - x);
        uint8_t *dst_row = dst->pixels + (size_t)row * dst->width + (size_t)dx0;
        if (transparent < 0) {
            memcpy(dst_row, src_row, (size_t)(dx1 - dx0));
            continue;
        }
        for (int32_t col = 0; col < (int32_t)(dx1 - dx0); col++) {
            uint8_t pixel = src_row[col];
            if (pixel != (uint8_t)transparent) dst_row[col] = pixel;
        }
    }
    _mark(dst, dx0, dy0, (int32_t)dx1, (int32_t)dy1);
}

void gfx_glyph(gfx_surface_t *surface, int32_t x, int32_t y, const uint8_t *rows, int32_t height,
               uint8_t color) {
    if (!surface || !rows || height <= 0) return;
    for (int32_t row = 0; row < height; row++) {
        uint8_t bits = rows[row];
        if (!bits) continue;
        for (int32_t col = 0; col < GFX_CHAR_WIDTH; col++) {
            if (bits & (0x80u >> col)) gfx_pixel(surface, x + col, y + row, color);
        }
    }
}

size_t gfx_row_to_rgb888(const gfx_surface_t *surface, uint16_t y, uint16_t x, uint16_t width,
                         uint8_t *out) {
    if (!surface || !out || !surface->pixels) return 0;
    if (y >= surface->height || x >= surface->width) return 0;
    if (width > (uint16_t)(surface->width - x)) width = (uint16_t)(surface->width - x);
    if (width == 0) return 0;

    const uint8_t *src = surface->pixels + (size_t)y * surface->width + x;
    for (uint16_t i = 0; i < width; i++) {
        const uint8_t *rgb = _palette[src[i]];
        out[i * 3 + 0] = rgb[0];
        out[i * 3 + 1] = rgb[1];
        out[i * 3 + 2] = rgb[2];
    }
    return (size_t)width * 3u;
}
