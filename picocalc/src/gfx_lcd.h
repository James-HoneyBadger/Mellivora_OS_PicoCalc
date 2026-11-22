#pragma once
/*
 * gfx_lcd.h — present a gfx_surface_t to the PicoCalc panel.
 *
 * The bridge between core/gfx.c (portable, indexed, dirty-rectangle) and
 * the ILI9488 driver. Used by the `.mapp` loader and by `gfxbench`.
 */

#include <stdint.h>

#include "core/gfx.h"

/* Push the surface's dirty region to the panel at (x, y), scaling nothing.
   Sends only the changed rows since the previous call, then clears the
   dirty box. A clean surface presents in full the first time. */
void gfx_present_lcd(const gfx_surface_t *surface, int32_t x, int32_t y);

/* Present only the dirty region. */
void gfx_present_lcd_dirty(const gfx_surface_t *surface, int32_t x, int32_t y);

/* Present the entire surface regardless of dirty state. */
void gfx_present_lcd_full(const gfx_surface_t *surface, int32_t x, int32_t y);
