/*
 * gfx_lcd.c — present a gfx_surface_t to the PicoCalc panel.
 *
 * The bridge between core/gfx.c (portable, indexed, dirty-rectangle) and the
 * ILI9488 driver. The panel window is set once per present and each row is
 * expanded from the palette inside lcd_blit_region's single Core 1 request.
 */

#include "gfx_lcd.h"

#include "lcd.h"

typedef struct {
    const gfx_surface_t *surface;
    uint16_t panel_y0;    /* panel row of the first converted row */
    uint16_t surface_x0;  /* first surface column */
    uint16_t surface_y0;  /* first surface row */
    uint16_t width;       /* columns per row */
} present_ctx_t;

static void _convert_row(uint16_t panel_row, uint8_t *rgb888, void *ctx) {
    present_ctx_t *present = (present_ctx_t *)ctx;
    uint16_t surface_row = (uint16_t)(present->surface_y0 + (panel_row - present->panel_y0));
    (void)gfx_row_to_rgb888(present->surface, surface_row, present->surface_x0,
                            present->width, rgb888);
}

static void _present_region(const gfx_surface_t *surface, int32_t x, int32_t y,
                            uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh) {
    if (!surface || !surface->pixels) return;
    if (rx >= surface->width || ry >= surface->height) return;
    if (rw > (uint16_t)(surface->width - rx)) rw = (uint16_t)(surface->width - rx);
    if (rh > (uint16_t)(surface->height - ry)) rh = (uint16_t)(surface->height - ry);
    if (rw == 0 || rh == 0) return;

    /* Panel window for this region, clamped to the visible area. */
    int64_t px0 = (int64_t)x + rx;
    int64_t py0 = (int64_t)y + ry;
    int64_t px1 = px0 + rw;
    int64_t py1 = py0 + rh;
    if (px1 <= 0 || py1 <= 0 || px0 >= LCD_WIDTH || py0 >= LCD_HEIGHT) return;
    if (px0 < 0) {
        rx = (uint16_t)(rx + (uint16_t)(0 - px0));
        px0 = 0;
    }
    if (py0 < 0) {
        ry = (uint16_t)(ry + (uint16_t)(0 - py0));
        py0 = 0;
    }
    uint16_t width = (uint16_t)((px1 > LCD_WIDTH ? LCD_WIDTH : px1) - px0);
    uint16_t height = (uint16_t)((py1 > LCD_HEIGHT ? LCD_HEIGHT : py1) - py0);
    if (width == 0 || height == 0) return;

    present_ctx_t ctx = {
        .surface = surface,
        .panel_y0 = (uint16_t)py0,
        .surface_x0 = rx,
        .surface_y0 = ry,
        .width = width,
    };
    lcd_blit_region((uint16_t)px0, (uint16_t)py0, width, height, _convert_row, &ctx);
}

void gfx_present_lcd_full(const gfx_surface_t *surface, int32_t x, int32_t y) {
    if (!surface) return;
    _present_region(surface, x, y, 0, 0, surface->width, surface->height);
}

void gfx_present_lcd_dirty(const gfx_surface_t *surface, int32_t x, int32_t y) {
    if (!surface || !surface->dirty_any) return;
    _present_region(surface, x, y, surface->dirty_x0, surface->dirty_y0,
                    (uint16_t)(surface->dirty_x1 - surface->dirty_x0),
                    (uint16_t)(surface->dirty_y1 - surface->dirty_y0));
    gfx_dirty_reset((gfx_surface_t *)surface);
}

void gfx_present_lcd(const gfx_surface_t *surface, int32_t x, int32_t y) {
    if (!surface) return;
    /* A clean surface still presents in full the first time, so an app that
       clears once and then blits a sprite does not show a blank panel. */
    if (surface->dirty_any) gfx_present_lcd_dirty(surface, x, y);
    else gfx_present_lcd_full(surface, x, y);
}
