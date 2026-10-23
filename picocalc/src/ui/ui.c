/*
 * ui/ui.c — Shared visual language: palettes, primitives, menus, feedback.
 */

#include "ui/ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lcd.h"
#include "syscall.h"

typedef struct {
    const char *name;
    uint32_t    color[UI_ROLE_COUNT];
} ui_theme_t;

#define C(r, g, b) LCD_RGB((r), (g), (b))

static const ui_theme_t _themes[] = {
    {
        "noir",
        {
            C(0x0B, 0x0F, 0x12), C(0xD9, 0xE2, 0xE8), C(0x7A, 0x8B, 0x96),
            C(0x5C, 0xC8, 0xC0), C(0x7B, 0xD8, 0x8F), C(0xE8, 0xC1, 0x5A),
            C(0xFF, 0x6B, 0x6B), C(0x24, 0x30, 0x38), C(0x12, 0x1A, 0x1F),
            C(0xD9, 0xE2, 0xE8), C(0x5C, 0xC8, 0xC0), C(0x0B, 0x0F, 0x12),
            C(0x12, 0x1A, 0x1F), C(0x8F, 0xA3, 0xAF),
        },
    },
    {
        "paper",
        {
            C(0xF7, 0xF5, 0xEF), C(0x1E, 0x23, 0x28), C(0x6B, 0x72, 0x80),
            C(0x1D, 0x6F, 0xB8), C(0x1B, 0x7F, 0x3A), C(0x9A, 0x6B, 0x00),
            C(0xB3, 0x26, 0x1E), C(0xC9, 0xC4, 0xB8), C(0xFF, 0xFF, 0xFF),
            C(0x1E, 0x23, 0x28), C(0x1D, 0x6F, 0xB8), C(0xFF, 0xFF, 0xFF),
            C(0xE4, 0xE0, 0xD5), C(0x3B, 0x41, 0x48),
        },
    },
    {
        "green",
        {
            C(0, 0, 0), C(0x33, 0xFF, 0x66), C(0x1E, 0x9E, 0x45),
            C(0x7C, 0xFF, 0x9E), C(0x33, 0xFF, 0x66), C(0xFF, 0xD2, 0x4A),
            C(0xFF, 0x5C, 0x5C), C(0x13, 0x4F, 0x25), C(0x06, 0x13, 0x06),
            C(0x33, 0xFF, 0x66), C(0x33, 0xFF, 0x66), C(0x00, 0x00, 0x00),
            C(0x06, 0x13, 0x06), C(0x1E, 0x9E, 0x45),
        },
    },
    {
        "amber",
        {
            C(0x10, 0x0A, 0x00), C(0xFF, 0xB0, 0x00), C(0xA6, 0x6B, 0x00),
            C(0xFF, 0xD1, 0x66), C(0xFF, 0xC3, 0x4D), C(0xFF, 0xE0, 0x8A),
            C(0xFF, 0x5C, 0x5C), C(0x4A, 0x2E, 0x00), C(0x1C, 0x12, 0x00),
            C(0xFF, 0xB0, 0x00), C(0xFF, 0xB0, 0x00), C(0x10, 0x0A, 0x00),
            C(0x1C, 0x12, 0x00), C(0xA6, 0x6B, 0x00),
        },
    },
    {
        "cyan",
        {
            C(0x00, 0x0B, 0x10), C(0x00, 0xD8, 0xE8), C(0x00, 0x86, 0x96),
            C(0x6B, 0xE8, 0xFF), C(0x6B, 0xFF, 0xC4), C(0xFF, 0xD2, 0x4A),
            C(0xFF, 0x6B, 0x6B), C(0x00, 0x3A, 0x45), C(0x00, 0x16, 0x1C),
            C(0x00, 0xD8, 0xE8), C(0x00, 0xD8, 0xE8), C(0x00, 0x0B, 0x10),
            C(0x00, 0x16, 0x1C), C(0x00, 0x86, 0x96),
        },
    },
    {
        "blue",
        {
            C(0x08, 0x10, 0x2A), C(0xE8, 0xEE, 0xFF), C(0x9A, 0xAA, 0xD0),
            C(0x7A, 0xB8, 0xFF), C(0x7B, 0xD8, 0x8F), C(0xE8, 0xC1, 0x5A),
            C(0xFF, 0x6B, 0x6B), C(0x25, 0x36, 0x6B), C(0x10, 0x1C, 0x40),
            C(0xE8, 0xEE, 0xFF), C(0x7A, 0xB8, 0xFF), C(0x08, 0x10, 0x2A),
            C(0x10, 0x1C, 0x40), C(0x9A, 0xAA, 0xD0),
        },
    },
    {
        "matrix",
        {
            C(0x00, 0x05, 0x00), C(0x3C, 0xFF, 0x3C), C(0x1B, 0x8A, 0x1B),
            C(0x9C, 0xFF, 0x7A), C(0x3C, 0xFF, 0x3C), C(0xFF, 0xD2, 0x4A),
            C(0xFF, 0x5C, 0x5C), C(0x10, 0x3A, 0x10), C(0x00, 0x0E, 0x00),
            C(0x3C, 0xFF, 0x3C), C(0x3C, 0xFF, 0x3C), C(0x00, 0x05, 0x00),
            C(0x00, 0x0E, 0x00), C(0x1B, 0x8A, 0x1B),
        },
    },
    {
        "white",
        {
            C(0x00, 0x00, 0x00), C(0xF2, 0xF2, 0xF2), C(0x9A, 0x9A, 0x9A),
            C(0xFF, 0xFF, 0xFF), C(0x8C, 0xE0, 0x9A), C(0xE8, 0xC1, 0x5A),
            C(0xFF, 0x6B, 0x6B), C(0x3A, 0x3A, 0x3A), C(0x14, 0x14, 0x14),
            C(0xF2, 0xF2, 0xF2), C(0xF2, 0xF2, 0xF2), C(0x00, 0x00, 0x00),
            C(0x14, 0x14, 0x14), C(0x9A, 0x9A, 0x9A),
        },
    },
    {
        "dark",
        {
            C(0x00, 0x00, 0x00), C(0xC8, 0xC8, 0xC8), C(0x78, 0x78, 0x78),
            C(0xE0, 0xE0, 0xE0), C(0x8C, 0xE0, 0x9A), C(0xE8, 0xC1, 0x5A),
            C(0xFF, 0x6B, 0x6B), C(0x2E, 0x2E, 0x2E), C(0x0C, 0x0C, 0x0C),
            C(0xC8, 0xC8, 0xC8), C(0xE0, 0xE0, 0xE0), C(0x00, 0x00, 0x00),
            C(0x0C, 0x0C, 0x0C), C(0x78, 0x78, 0x78),
        },
    },
    {
        "high-contrast",
        {
            C(0x00, 0x00, 0x00), C(0xFF, 0xFF, 0xFF), C(0xC8, 0xC8, 0xC8),
            C(0xFF, 0xFF, 0x00), C(0x00, 0xFF, 0x66), C(0xFF, 0xC0, 0x00),
            C(0xFF, 0x33, 0x33), C(0x80, 0x80, 0x80), C(0x1A, 0x1A, 0x1A),
            C(0xFF, 0xFF, 0xFF), C(0xFF, 0xFF, 0x00), C(0x00, 0x00, 0x00),
            C(0x1A, 0x1A, 0x1A), C(0xFF, 0xFF, 0xFF),
        },
    },
};

#define THEME_COUNT ((int)(sizeof _themes / sizeof _themes[0]))

static int _current = 0;
static char _notice[48];
static uint32_t _notice_until = 0;
static bool _fullscreen = false;

int ui_theme_count(void) { return THEME_COUNT; }

const char *ui_theme_name(int index) {
    if (index < 0 || index >= THEME_COUNT) return "";
    return _themes[index].name;
}

int ui_theme_current(void) { return _current; }

const char *ui_theme_current_name(void) { return _themes[_current].name; }

uint32_t ui_color(ui_role_t role) {
    if (role >= UI_ROLE_COUNT) return LCD_WHITE;
    return _themes[_current].color[role];
}

uint32_t ui_bg(void) { return ui_color(UI_BG); }

bool ui_theme_set_index(int index) {
    if (index < 0 || index >= THEME_COUNT) return false;
    _current = index;
    lcd_set_fg(ui_color(UI_TEXT));
    lcd_set_bg(ui_color(UI_BG));
    lcd_cls(ui_color(UI_BG));
    return true;
}

bool ui_theme_set(const char *name) {
    if (!name || !*name) return false;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (strcmp(name, _themes[i].name) == 0) return ui_theme_set_index(i);
    }
    return false;
}

/* ---- Printing ---------------------------------------------------------- */

void ui_print(ui_role_t role, const char *s) {
    if (!s) return;
    uint32_t saved = lcd_get_fg();
    lcd_set_fg(ui_color(role));
    sys_print(s);
    lcd_set_fg(saved);
}

void ui_line(ui_role_t role, const char *s) {
    ui_print(role, s ? s : "");
    sys_putchar('\n');
}

void ui_printf(ui_role_t role, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ui_print(role, buf);
}

/* ---- Primitives -------------------------------------------------------- */

void ui_clear(void) { lcd_cls(ui_bg()); }

void ui_rule(int col) {
    char line[LCD_COLS + 1];
    if (col < 1) col = LCD_COLS;
    if (col > LCD_COLS) col = LCD_COLS;
    memset(line, '-', (size_t)col);
    line[col] = '\0';
    ui_line(UI_BORDER, line);
}

void ui_title(const char *title) {
    ui_line(UI_ACCENT, title ? title : "");
    ui_rule(LCD_COLS);
}

void ui_header(const char *title, const char *right) {
    char bar[LCD_COLS + 1];
    memset(bar, ' ', LCD_COLS);
    bar[LCD_COLS] = '\0';

    int len = title ? (int)strlen(title) : 0;
    if (len > LCD_COLS - 2) len = LCD_COLS - 2;
    if (len > 0 && title) memcpy(bar + 1, title, (size_t)len);

    int rlen = right ? (int)strlen(right) : 0;
    if (rlen > LCD_COLS - 2) rlen = LCD_COLS - 2;
    if (rlen > 0 && right) memcpy(bar + LCD_COLS - rlen - 1, right, (size_t)rlen);

    uint32_t fg = ui_color(UI_SELECT_TEXT);
    uint32_t bg = ui_color(UI_ACCENT);
    lcd_lock();
    for (int c = 0; c < LCD_COLS; c++) lcd_draw_cell(c, 0, bar[c], fg, bg);
    lcd_set_cursor(0, 1);
    lcd_unlock();
}

void ui_footer(const char *hint) {
    char bar[LCD_COLS + 1];
    memset(bar, ' ', LCD_COLS);
    bar[LCD_COLS] = '\0';
    int len = hint ? (int)strlen(hint) : 0;
    if (len > LCD_COLS - 2) len = LCD_COLS - 2;
    if (len > 0 && hint) memcpy(bar + 1, hint, (size_t)len);

    uint32_t fg = ui_color(UI_STATUS_TEXT);
    uint32_t bg = ui_color(UI_STATUS_BG);
    lcd_lock();
    for (int c = 0; c < LCD_COLS; c++) {
        lcd_draw_cell(c, LCD_ROWS - 1, bar[c], fg, bg);
    }
    lcd_unlock();
}

void ui_label_value(const char *label, const char *value) {
    char line[LCD_COLS + 1];
    snprintf(line, sizeof line, "  %-14.14s %s", label ? label : "",
             value ? value : "");
    ui_line(UI_TEXT, line);
}

/* ---- Feedback ---------------------------------------------------------- */

void ui_ok(const char *msg) {
    ui_print(UI_SUCCESS, "ok");
    ui_print(UI_MUTED, "  ");
    ui_line(UI_TEXT, msg ? msg : "");
}

void ui_warn(const char *msg) {
    ui_print(UI_WARNING, "warning");
    ui_print(UI_MUTED, "  ");
    ui_line(UI_TEXT, msg ? msg : "");
}

void ui_error(const char *what, const char *detail) {
    ui_print(UI_ERROR, what ? what : "error");
    if (detail && *detail) {
        ui_print(UI_MUTED, ": ");
        ui_line(UI_TEXT, detail);
    } else {
        sys_putchar('\n');
    }
}

void ui_notify(const char *msg) {
    if (!msg) {
        _notice[0] = '\0';
        _notice_until = 0;
        return;
    }
    snprintf(_notice, sizeof _notice, "%s", msg);
    _notice_until = sys_time_ms() + 4000;
}

bool ui_fullscreen(void) { return _fullscreen; }

void ui_set_fullscreen(bool on) { _fullscreen = on; }

const char *ui_notice(void) {
    if (!_notice[0]) return NULL;
    if ((int32_t)(sys_time_ms() - _notice_until) >= 0) {
        _notice[0] = '\0';
        return NULL;
    }
    return _notice;
}

/* ---- Menu -------------------------------------------------------------- */

int ui_menu(const char *title, const char *subtitle,
            const char *const *items, int count, int initial) {
    if (!items || count <= 0) return -1;

    ui_set_fullscreen(true);
    int result = -1;
    const int top_row = 2;
    const int bottom_row = LCD_ROWS - 2;
    const int visible = bottom_row - top_row + 1;
    int sel = initial;
    if (sel < 0) sel = 0;
    if (sel >= count) sel = count - 1;
    int first = 0;

    for (;;) {
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;

        ui_clear();
        ui_header(title ? title : "", subtitle ? subtitle : "");

        for (int row = 0; row < visible; row++) {
            int idx = first + row;
            int y = top_row + row;
            if (idx >= count) break;

            char line[LCD_COLS + 1];
            snprintf(line, sizeof line, "%c %-*.*s", idx == sel ? '>' : ' ',
                     LCD_COLS - 2, LCD_COLS - 2, items[idx]);

            uint32_t fg = (idx == sel) ? ui_color(UI_SELECT_TEXT) : ui_color(UI_TEXT);
            uint32_t bg = (idx == sel) ? ui_color(UI_SELECT_BG) : ui_bg();
            lcd_lock();
            for (int c = 0; c < LCD_COLS; c++) {
                lcd_draw_cell(c, y, line[c], fg, bg);
            }
            lcd_unlock();
        }

        char hint[48];
        snprintf(hint, sizeof hint, "up/down move  enter select  esc back");
        ui_footer(hint);

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x1B || ch == 0x03) break;
        if (ch == 'k' || ch == 'w' || ch == 0x10) {
            if (sel > 0) sel--;
        } else if (ch == 'j' || ch == 's' || ch == 0x0E) {
            if (sel + 1 < count) sel++;
        } else if (ch == '\r' || ch == '\n') {
            result = sel;
            break;
        }
    }

    ui_set_fullscreen(false);
    return result;
}
