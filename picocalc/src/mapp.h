#pragma once

#include <stdint.h>

#define MAPP_FORMAT_VERSION 1u
#define MAPP_KIND_THUMB 1u
#define MAPP_KIND_BYTECODE 2u
#define MAPP_API_VERSION 1u
#define MAPP_HEADER_SIZE 60u

#define MAPP_FILE_READ 0u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t kind;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t relocation_count;
    char name[32];
} __attribute__((packed)) mapp_header_t;

#ifdef __cplusplus
static_assert(sizeof(mapp_header_t) == MAPP_HEADER_SIZE, "MAPP header layout changed");
#else
_Static_assert(sizeof(mapp_header_t) == MAPP_HEADER_SIZE, "MAPP header layout changed");
#endif

typedef void (*mapp_dir_entry_fn)(const char *short_name, const char *long_name,
                                  uint32_t size, uint32_t is_dir, void *context);

typedef struct {
    uint32_t size;
    uint32_t version;

    void (*display_clear)(void);
    void (*display_putc)(char ch);
    void (*display_print)(const char *text);
    void (*display_set_cursor)(int32_t x, int32_t y);
    void (*display_set_color)(uint32_t foreground, uint32_t background);
    int32_t (*display_columns)(void);
    int32_t (*display_rows)(void);

    int32_t (*keyboard_read)(void);
    int32_t (*keyboard_try_read)(void);
    int32_t (*keyboard_read_line)(const char *prompt, char *buffer, uint32_t capacity);

    int32_t (*file_open)(const char *path, uint32_t flags);
    int32_t (*file_read)(int32_t handle, void *buffer, uint32_t capacity);
    int32_t (*file_close)(int32_t handle);
    int32_t (*file_list)(const char *path, mapp_dir_entry_fn callback, void *context);
    int32_t (*file_write)(const char *path, const void *buffer, uint32_t length);
    int32_t (*file_remove)(const char *path);
    int32_t (*file_mkdir)(const char *path);

    uint32_t (*timer_millis)(void);
    void (*timer_sleep)(uint32_t milliseconds);

    int32_t (*network_available)(void);
    int32_t (*network_resolve)(const char *hostname, uint32_t *address, int32_t timeout_ms);
    int32_t (*network_http_get)(const char *host, uint16_t port, const char *path,
                                char *buffer, uint32_t capacity, int32_t timeout_ms);
} mapp_api_t;

typedef int32_t (*mapp_entry_fn)(const mapp_api_t *api, int32_t argc, char **argv);

typedef enum {
    MAPP_APP_THUMB = 1,
    MAPP_APP_BASIC,
    MAPP_APP_TINYC,
    MAPP_APP_FORTH,
} mapp_app_kind_t;

typedef struct {
    char filename[256];
    char title[48];
    mapp_app_kind_t kind;
} mapp_app_t;

/* Discovery is refreshed each time the launcher opens. Returns count or a
   negative FAT error. The catalog is owned by the loader and remains valid
   until the next discovery call. */
int32_t mapp_discover(void);
int32_t mapp_catalog_truncated(void);
const mapp_app_t *mapp_catalog_get(uint32_t index);
void mapp_launch(const mapp_app_t *app);

#ifdef __cplusplus
}
#endif
