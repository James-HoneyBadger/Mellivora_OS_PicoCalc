#pragma once
/*
 * core/theme_file.h — parser for user theme files (`/themes/NAME.thm`).
 *
 * Kept independent of the display layer so it can be unit-tested on the host.
 * Role order matches ui_role_t; ui.c asserts that at compile time.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THEME_FILE_ROLE_COUNT 14
#define THEME_FILE_NAME_MAX 24

typedef enum {
    THEME_ROLE_BG = 0,
    THEME_ROLE_TEXT,
    THEME_ROLE_MUTED,
    THEME_ROLE_ACCENT,
    THEME_ROLE_SUCCESS,
    THEME_ROLE_WARNING,
    THEME_ROLE_ERROR,
    THEME_ROLE_BORDER,
    THEME_ROLE_PANEL,
    THEME_ROLE_PANEL_TEXT,
    THEME_ROLE_SELECT_BG,
    THEME_ROLE_SELECT_TEXT,
    THEME_ROLE_STATUS_BG,
    THEME_ROLE_STATUS_TEXT,
} theme_role_t;

typedef struct {
    char name[THEME_FILE_NAME_MAX];
    uint32_t color[THEME_FILE_ROLE_COUNT];
    uint16_t set_mask;
    uint8_t errors;
} theme_file_t;

/* Parse "role = RRGGBB" lines. Returns true when a name and at least one
   colour were read. Unknown keys and malformed colours increment errors. */
bool theme_file_parse(theme_file_t *out, const char *text, size_t len);

const char *theme_file_role_name(int index);
int theme_file_role_index(const char *name);

/* Parse "#RRGGBB" or "RRGGBB". */
bool theme_file_parse_color(const char *text, uint32_t *out);

/* Discovery of `/themes`. The visitor used by fat_ls_ex must not read files:
   the directory walk borrows the FAT driver's shared sector buffer, so a read
   inside the callback corrupts the enumeration. These helpers only record
   names; the caller loads them once the walk has finished. */
#define THEME_SCAN_MAX 6
#define THEME_SCAN_PATH_MAX 48

typedef struct {
    char path[THEME_SCAN_MAX][THEME_SCAN_PATH_MAX];
    char name[THEME_SCAN_MAX][THEME_FILE_NAME_MAX];
    int count;
} theme_scan_t;

void theme_scan_reset(theme_scan_t *scan);

/* Queue one directory entry. Returns true when a palette was recorded; the
   extension match is case-insensitive and directories are ignored. */
bool theme_scan_visit(theme_scan_t *scan, const char *short_name, const char *long_name,
                      bool is_dir);
