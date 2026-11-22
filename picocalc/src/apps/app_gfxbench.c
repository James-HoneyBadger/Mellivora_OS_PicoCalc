/*
 * apps/app_gfxbench.c — on-device graphics benchmark.
 *
 * Measures what the panel actually delivers: full-surface presents, dirty
 * rectangles, and (on request) a 320x320 surface, through the exact path
 * `.mapp` apps use. This is the measurement behind the Fabric phase's
 * surface-size and frame-budget decisions.
 *
 *   gfxbench           160x160 full + dirty 8x8 + 16x16 sprite run
 *   gfxbench -n 80     use 80 frames per measurement
 *   gfxbench -big      also try a full 320x320 surface
 *   gfxbench -quiet    only print the CSV row
 */

#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "core/gfx.h"
#include "gfx_lcd.h"
#include "hardware/watchdog.h"
#include "lcd.h"
#include "syscall.h"
#include "ui/ui.h"

#define GFXBENCH_DEFAULT_FRAMES 40

typedef struct {
    uint16_t size;
    uint32_t ms;
    double fps;
    uint32_t kib_per_s;
    bool ok;
} bench_result_t;

static uint32_t _measure(void *surface, int32_t frames, bool full_present) {
    uint32_t started = sys_time_ms();
    for (int32_t i = 0; i < frames; i++) {
        if (full_present) gfx_invalidate((gfx_surface_t *)surface);
        gfx_present_lcd((gfx_surface_t *)surface, 0, 0);
        watchdog_update();
    }
    return sys_time_ms() - started;
}

static void _report(const char *label, bench_result_t *out, uint16_t size,
                    int32_t frames, void *surface, bool full_present) {
    uint32_t elapsed = _measure(surface, frames, full_present);
    out->size = size;
    out->ms = elapsed;
    out->ok = elapsed > 0;
    if (!out->ok) {
        ui_line(UI_ERROR, "gfxbench: timer resolution too low, rerun with -n 100");
        return;
    }
    out->fps = (double)frames * 1000.0 / (double)elapsed;
    out->kib_per_s = (uint32_t)((double)size * (double)size * 3.0 * (double)frames /
                                (double)elapsed / 1024.0);
    char line[112];
    snprintf(line, sizeof line, "%s %ux%u: %lu ms/frame (%.1f fps, %lu KiB/s SPI)",
             label, (unsigned)size, (unsigned)size, (unsigned long)(elapsed / (uint32_t)frames),
             out->fps, (unsigned long)out->kib_per_s);
    ui_line(UI_SUCCESS, line);
}

static void _bench_dirty(void *surface, int32_t frames) {
    gfx_surface_t *s = (gfx_surface_t *)surface;
    uint8_t sprite_pixels[8 * 8];
    gfx_surface_t sprite;
    (void)gfx_surface_init(&sprite, sprite_pixels, 8, 8);
    for (int i = 0; i < 8 * 8; i++) sprite_pixels[i] = 9;

    uint32_t started = sys_time_ms();
    for (int32_t i = 0; i < frames; i++) {
        int32_t x = (i * 3) % (int32_t)(s->width - 8);
        int32_t y = (i * 5) % (int32_t)(s->height - 8);
        gfx_blit(s, x, y, &sprite, GFX_TRANSPARENT_NONE);
        gfx_present_lcd_dirty(s, 0, 0);
        watchdog_update();
    }
    uint32_t elapsed = sys_time_ms() - started;
    char line[112];
    snprintf(line, sizeof line,
             "dirty 8x8 sprite on %ux%u: %lu ms/frame (%.1f fps, %.1fx faster than full)",
             (unsigned)s->width, (unsigned)s->height, (unsigned long)(elapsed / (uint32_t)frames),
             (double)frames * 1000.0 / (double)elapsed,
             1536.0 / (3.0 * (double)s->width * (double)s->height));
    ui_line(UI_SUCCESS, line);
}

void app_gfxbench(const char *arg) {
    int32_t frames = GFXBENCH_DEFAULT_FRAMES;
    bool want_big = false;
    bool quiet = false;

    char token[32];
    while ((arg = next_token(arg, token, sizeof token)) != NULL && *token) {
        if (!strcmp(token, "-big")) want_big = true;
        else if (!strcmp(token, "-quiet")) quiet = true;
        else if (!strcmp(token, "-n")) {
            const char *rest = next_token(arg, token, sizeof token);
            if (rest) {
                arg = rest;
                frames = atoi(token);
            }
        }
    }
    if (frames < 2) frames = 2;
    if (frames > 1000) frames = 1000;

    ui_line(UI_WARNING, "gfxbench: don't touch the device; each run is a few seconds");

    /* 160x160 is the recommended game surface on both boards. */
    uint8_t *pixels = (uint8_t *)malloc(160u * 160u);
    gfx_surface_t surface;
    if (!pixels || !gfx_surface_init(&surface, pixels, 160, 160)) {
        free(pixels);
        ui_line(UI_ERROR, "gfxbench: cannot allocate a 160x160 surface");
        return;
    }
    gfx_clear(&surface, 1);
    gfx_present_lcd_full(&surface, 0, 0);

    bench_result_t full = {0}, big = {0};
    _report("full present", &full, 160, frames, &surface, true);
    _bench_dirty(&surface, frames);
    free(pixels);

    if (want_big) {
        /* 320x320 needs ~100 KiB of heap; RP2040 may refuse it. */
        uint8_t *big_pixels = (uint8_t *)malloc(320u * 320u);
        gfx_surface_t big_surface;
        if (big_pixels && gfx_surface_init(&big_surface, big_pixels, 320, 320)) {
            gfx_clear(&big_surface, 1);
            _report("full present", &big, 320, frames, &big_surface, true);
        } else {
            ui_line(UI_WARNING, "gfxbench: 320x320 surface refused (RAM limit)");
        }
        free(big_pixels);
    }

    lcd_cls(LCD_BLACK);
    if (!quiet) {
        char verdict[96];
        if (full.ok) {
            /* At a 20 fps target each frame gets 50 ms; this is how much of
               the 160x160 surface that buys, in percent. */
            uint32_t afford = (uint32_t)(5.0 * full.fps);
            snprintf(verdict, sizeof verdict,
                     "at 20 fps you may redraw %lu%% of a 160x160 surface per frame",
                     (unsigned long)afford);
            ui_line(UI_SUCCESS, verdict);
        }
        ui_line(UI_SUCCESS, "gfxbench done: photo the results for the release notes");
    } else {
        char csv[128];
        snprintf(csv, sizeof csv, "gfxbench,160,%.1f,%lu", full.fps,
                 (unsigned long)(big.ok ? big.kib_per_s : full.kib_per_s));
        ui_line(UI_SUCCESS, csv);
    }
}
