#include "mapp.h"

int32_t mapp_main(const mapp_api_t *api, int32_t argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Compatibility rule: accept this loader or newer, and only touch the
       fields that actually exist in the table it handed us. */
    if (!api || api->version < MAPP_API_VERSION_MIN || api->size < 8) return -1;
    if (api->size < sizeof(mapp_api_t)) return -1;

    api->display_clear();
    api->display_print("Hello from a microSD application!\n");
    api->display_print("Press any key to return to Mellivora.\n");
    (void)api->keyboard_read();
    return 0;
}
