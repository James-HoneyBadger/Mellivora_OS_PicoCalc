/*
 * myapp — a Mellivora app.
 *
 * Build for the device with `make`, run on the desktop with `make sim`.
 * The ABI reference is tools/sdk/README.md in the Mellivora repository.
 */

#include "mapp.h"

#define BG 0
#define FG 7
#define ACCENT 11

int32_t mapp_main(const mapp_api_t *api, int32_t argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Compatibility rule: accept this loader or newer, and only touch the
       calls the running loader actually has. */
    if (!api || api->version < MAPP_API_VERSION_MIN) return -1;
    if (api->size < sizeof(mapp_api_t)) return -1;

    if (!api->capabilities || !(api->capabilities() & MAPP_CAP_GFX)) {
        api->display_clear();
        api->display_print("This loader has no graphics support.\n");
        api->display_print("Press any key to return.\n");
        (void)api->keyboard_read();
        return -1;
    }

    api->gfx_palette(ACCENT, 0x40C0FF);

    void *screen = api->gfx_create(160, 160);
    if (!screen) {
        api->display_print("Not enough memory for a 160x160 surface.\n");
        return -1;
    }

    api->display_clear();
    api->gfx_clear(screen, BG);
    api->gfx_fill_rect(screen, 20, 20, 120, 120, ACCENT);
    api->gfx_char(screen, 68, 76, 'H', BG);
    api->gfx_char(screen, 76, 76, 'I', FG);
    api->gfx_present(screen, 80, 80);

    if (api->audio_tone) api->audio_tone(660, 80);

    (void)api->keyboard_read();

    api->gfx_destroy(screen);
    if (api->audio_stop) api->audio_stop();
    api->display_clear();
    return 0;
}
