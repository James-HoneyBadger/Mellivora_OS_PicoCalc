/* Life Lab: a tiny interactive cellular automaton for the v2 graphics ABI.
 * Fixed-size memory, no heap beyond the indexed surface, no libc or libm.
 * Space pauses, N steps, R reseeds, Q/Escape exits. */
#include "mapp.h"

#define SIDE 32
#define PIXELS 128
static uint8_t cells[2][SIDE * SIDE];
static uint32_t rng = 0x12345678u;

static uint32_t random_word(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static void seed(uint8_t *grid) {
    for (int i = 0; i < SIDE * SIDE; i++)
        grid[i] = (random_word() & 7u) < 3u ? 1 : 0;
}
static void advance(const uint8_t *from, uint8_t *to) {
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            unsigned count = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dy)
                        count += from[((y + dy + SIDE) & (SIDE - 1)) * SIDE +
                                      ((x + dx + SIDE) & (SIDE - 1))] != 0;
            unsigned age = from[y * SIDE + x];
            to[y * SIDE + x] = count == 3 || (count == 2 && age)
                ? (uint8_t)(age < 7 ? age + 1 : 7) : 0;
        }
    }
}
int32_t mapp_main(const mapp_api_t *api, int32_t argc, char **argv) {
    (void)argc; (void)argv;
    if (!api || api->version < 2 || api->size < offsetof(mapp_api_t, audio_tone) ||
        !api->capabilities || (api->capabilities() &
        (MAPP_CAP_GFX | MAPP_CAP_KEYS | MAPP_CAP_TIMER)) !=
        (MAPP_CAP_GFX | MAPP_CAP_KEYS | MAPP_CAP_TIMER) ||
        !api->gfx_create || !api->gfx_destroy || !api->gfx_palette ||
        !api->gfx_fill_rect || !api->gfx_present || !api->keyboard_try_read ||
        !api->timer_sleep || !api->display_clear || !api->display_print) return -1;
    void *surface = api->gfx_create(PIXELS, PIXELS);
    if (!surface) { api->display_print("Life Lab: insufficient graphics memory.\n"); return -1; }
    api->display_clear();
    api->display_print("LIFE LAB / cellular automata\nSPACE pause | N step | R reset | Q quit\n");
    static const uint32_t colors[8] = {0x071019,0x72ffd2,0x39dfaa,0x21b9a0,
                                       0x19878f,0x236e9e,0x445fb1,0x9374d8};
    for (int i = 0; i < 8; i++) api->gfx_palette((uint8_t)i, colors[i]);
    if (api->timer_millis) rng ^= api->timer_millis();
    if (!rng) rng = 1;
    unsigned current = 0;
    bool paused = false, redraw = true;
    seed(cells[current]);
    for (;;) {
        int key = api->keyboard_try_read();
        if (key == 'q' || key == 'Q' || key == 27 || key == 3) break;
        if (key == ' ') paused = !paused;
        if (key == 'r' || key == 'R') { seed(cells[current]); redraw = true; }
        if (redraw) {
            for (int y = 0; y < SIDE; y++)
                for (int x = 0; x < SIDE; x++)
                    api->gfx_fill_rect(surface, x * 4, y * 4, 4, 4, cells[current][y * SIDE + x]);
            api->gfx_present(surface, 96, 96);
            redraw = false;
        }
        if (!paused || key == 'n' || key == 'N') {
            advance(cells[current], cells[current ^ 1u]);
            current ^= 1u; redraw = true;
        }
        if (api->yield) api->yield();
        api->timer_sleep(100);
    }
    api->gfx_destroy(surface);
    api->display_clear();
    return 0;
}
