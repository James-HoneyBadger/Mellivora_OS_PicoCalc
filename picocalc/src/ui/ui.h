#pragma once
/*
 * ui/ui.h — Shared visual language for Mellivora OS.
 *
 * Commands never hard-code colours. They pick a semantic role; the active
 * theme maps that role to an RGB value. That keeps every screen consistent
 * and makes new themes a data change rather than a code change.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_BG = 0,      /* screen background                    */
    UI_TEXT,        /* primary text                         */
    UI_MUTED,       /* secondary text, hints, timestamps    */
    UI_ACCENT,      /* titles, prompt, active elements      */
    UI_SUCCESS,     /* confirmations                        */
    UI_WARNING,     /* caution                              */
    UI_ERROR,       /* failures                             */
    UI_BORDER,      /* rules and frames                     */
    UI_PANEL,       /* raised surface                       */
    UI_PANEL_TEXT,  /* text on a panel                      */
    UI_SELECT_BG,   /* menu selection bar                   */
    UI_SELECT_TEXT, /* text on the selection bar            */
    UI_STATUS_BG,   /* status bar background                */
    UI_STATUS_TEXT, /* status bar text                      */
    UI_ROLE_COUNT
} ui_role_t;

/* ---- Palette ---------------------------------------------------------- */

int         ui_theme_count(void);
const char *ui_theme_name(int index);
int         ui_theme_current(void);
const char *ui_theme_current_name(void);
bool        ui_theme_set(const char *name);
bool        ui_theme_set_index(int index);

/* User palettes loaded from /themes (NAME.thm) are appended after the built-ins.
   Registering an existing user name replaces it in place. */
int  ui_theme_register(const char *name, const uint32_t color[UI_ROLE_COUNT]);
void ui_theme_clear_user(void);
int  ui_theme_user_count(void);
bool ui_theme_color(int index, int role, uint32_t *out);

/* ---- Roles ------------------------------------------------------------ */

uint32_t ui_color(ui_role_t role);
uint32_t ui_bg(void);

/* Print with a role's foreground colour. Falls back to the current colour
   when LCD output is not active, so redirection/pipelines stay plain text. */
void ui_print(ui_role_t role, const char *s);
void ui_line(ui_role_t role, const char *s);
void ui_printf(ui_role_t role, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* ---- Small design primitives ------------------------------------------ */

void ui_clear(void);                     /* clear to UI_BG            */
void ui_rule(int col);                   /* dim horizontal rule       */
void ui_title(const char *title);        /* accent row + rule below    */
void ui_header(const char *title, const char *right);
void ui_footer(const char *hint);
void ui_label_value(const char *label, const char *value);

/* ---- Feedback ---------------------------------------------------------- */

void ui_ok(const char *msg);
void ui_warn(const char *msg);
void ui_error(const char *what, const char *detail);

/* Full-screen ownership: suppresses the shell status bar so app chrome
   (footers, headers) is not overwritten by background refreshes. */
bool ui_fullscreen(void);
/* ---- Launcher view --------------------------------------------------- */

typedef struct {
    const char *title;  /* row text */
    const char *icon;   /* 1-2 character glyph, drawn left of the title */
    const char *tag;    /* right-hand category tag such as "FILE" */
} ui_launcher_row_t;

/* A searchable launcher list: a query bar under the header, an icon and a
   right-aligned category tag per row, and the selection highlighted. The
   caller filters the rows before each call, so this view draws what it is
   given. Returns the chosen index, or -1 when the user backs out. */
typedef struct {
    const ui_launcher_row_t *row;
    int count;
    const char *query;   /* initial query text, may be NULL */
    int selected;        /* in/out: initial and final selection */
    bool *query_changed; /* set when the user edited the query */
    char *query_out;     /* receives the edited query */
    size_t query_capacity;
} ui_launcher_t;

int ui_launcher(ui_launcher_t *view);

void ui_set_fullscreen(bool on);

/* Transient message shown in the status bar for a few seconds. */
void        ui_notify(const char *msg);
const char *ui_notice(void);

/* ---- Menus ------------------------------------------------------------- */

/* Draw a selection menu and return the chosen index, or -1 on cancel.
   `initial` selects the row on entry; pass 0 when unsure. */
int ui_menu(const char *title, const char *subtitle,
            const char *const *items, int count, int initial);

#ifdef __cplusplus
}
#endif
