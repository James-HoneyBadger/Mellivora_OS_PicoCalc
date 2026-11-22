/*
 * plasma.c — the "hello world" of the v2 graphics ABI.
 *
 * Draws a 160x160 indexed surface, animates it by rotating the palette only
 * (so not a single pixel has to be recomputed each frame), and presents it
 * centred on the 320x320 panel. Press any key to return to the shell.
 *
 * Build:  make -C tools/sdk APP=plasma
 */

#include "mapp.h"

#define SIZE 160
#define ORIGIN ((320 - SIZE) / 2)

/* Triangle wave over 0..255: bright at the ends, dark in the middle. */
static uint8_t ramp(uint8_t value) {
    return (value & 0x80u) ? (uint8_t)(0xFFu - ((value & 0x7Fu) << 1)) : (uint8_t)((value & 0x7Fu) << 1);
}

/* Three ramps offset by a third of the range give a rotating rainbow. */
static uint32_t shade(uint8_t index, uint8_t phase) {
    uint8_t value = (uint8_t)(index + phase);
    return ((uint32_t)ramp((uint8_t)(value + 0)) << 16) |
           ((uint32_t)ramp((uint8_t)(value + 85)) << 8) |
           (uint32_t)ramp((uint8_t)(value + 170));
}

int32_t mapp_main(const mapp_api_t *api, int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!api || api->version < MAPP_API_VERSION_MIN) return -1;
    if (!api->capabilities || !(api->capabilities() & MAPP_CAP_GFX)) {
        api->display_print("This loader has no graphics support.\n");
        return -1;
    }

    void *surface = api->gfx_create(SIZE, SIZE);
    if (!surface) {
        api->display_print("Not enough memory for a 160x160 surface.\n");
        return -1;
    }

    /* Static pattern: integer rings, no floating point or libm. */
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int dx = x - SIZE / 2;
            int dy = y - SIZE / 2;
            int d = (dx * dx + dy * dy) >> 5;
            uint8_t colour = (uint8_t)((d & 0x3F) + (x >> 3) + (y >> 3));
            api->gfx_pixel(surface, x, y, colour);
        }
    }

    api->display_clear();
    uint8_t phase = 0;
    for (;;) {
        for (int i = 0; i < 256; i++) api->gfx_palette((uint8_t)i, shade((uint8_t)i, phase));
        /* Only the palette changed, so ask for a full resend. */
        api->gfx_invalidate(surface);
        api->gfx_present(surface, ORIGIN, ORIGIN);

        if (api->keyboard_try_read() >= 0) break;

        phase++;
        /* A little arpeggio on the mixer voices when the loader has them;
           falls back to a single beep on v2 loaders. */
        if (api->audio_note && api->capabilities &&
            (api->capabilities() & MAPP_CAP_AUDIO_MIXER)) {
            int beat = phase >> 5;
            static const int scale[4] = {262, 330, 392, 523};
            int voice = beat & 3;
            if (voice != 0) api->audio_voice_stop(voice - 1);
            api->audio_note(MAPP_WAVE_TRIANGLE, scale[beat & 3], 120, 160, 4, 30, voice);
        } else if (api->audio_tone && (phase & 0x1F) == 0) {
            api->audio_tone(440 + (phase >> 2), 20);
        }
        if (api->yield) api->yield();
        api->timer_sleep(40);
    }

    if (api->audio_stop) api->audio_stop();
    api->gfx_destroy(surface);
    api->display_clear();
    return 0;
}
