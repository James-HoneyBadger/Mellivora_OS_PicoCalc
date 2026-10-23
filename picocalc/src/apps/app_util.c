/*
 * apps/app_util.c — Shared helpers for the application layer.
 *
 * Formatting, option parsing, file/path helpers, line input, and calendar
 * helpers used by more than one command module.
 */
#include "apps.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "console.h"
#include "core/path_util.h"
#include "fat.h"
#include "kbd.h"
#include "lcd.h"
#include "pipe.h"
#include "syscall.h"
#include "ui/ui.h"

uint8_t g_app_io_buf[APP_READ_MAX + 1];
/* ---- Option parsing helpers (Phase 2.5) -------------------------------- */
static bool _opt_token_eq(const char *p, const char *tok) {
    size_t n = strlen(tok);
    if (strncmp(p, tok, n) != 0) return false;
    char c = p[n];
    return c == '\0' || c == ' ' || c == '\t';
}

bool opt_flag(const char *args, const char *flag) {
    if (!args || !flag) return false;
    const char *p = skip_ws(args);
    while (*p) {
        if (_opt_token_eq(p, flag)) return true;
        /* skip current token */
        while (*p && *p != ' ' && *p != '\t') p++;
        p = skip_ws(p);
    }
    return false;
}

bool opt_value(const char *args, const char *opt, char *out, size_t out_sz) {
    if (!args || !opt || !out || out_sz == 0) return false;
    out[0] = '\0';
    const char *p = skip_ws(args);
    size_t olen = strlen(opt);
    while (*p) {
        if (strncmp(p, opt, olen) == 0 && (p[olen] == ' ' || p[olen] == '\t')) {
            p = skip_ws(p + olen);
            size_t i = 0;
            while (*p && *p != ' ' && *p != '\t' && i + 1 < out_sz) out[i++] = *p++;
            out[i] = '\0';
            return i > 0;
        }
        while (*p && *p != ' ' && *p != '\t') p++;
        p = skip_ws(p);
    }
    return false;
}

void opt_strip(const char *args, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!args) return;
    const char *p = skip_ws(args);
    size_t oi = 0;
    while (*p) {
        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t tlen = (size_t)(p - tok);
        bool is_opt = (tlen >= 2 && tok[0] == '-');
        if (is_opt) {
            /* If next token doesn't itself start with '-', treat as the value
               for a "-X VAL" form and drop it too. */
            const char *next = skip_ws(p);
            if (*next && *next != '-') {
                while (*p && *p != ' ' && *p != '\t') p++;
                p = next;
                while (*p && *p != ' ' && *p != '\t') p++;
            }
        } else {
            if (oi && oi + 1 < out_sz) out[oi++] = ' ';
            size_t copy = tlen;
            if (oi + copy >= out_sz) copy = out_sz - 1 - oi;
            memcpy(out + oi, tok, copy);
            oi += copy;
            out[oi] = '\0';
        }
        p = skip_ws(p);
    }
}

void print_line(const char *s) {
    sys_print(s);
    sys_putchar('\n');
}

void app_draw_header(const char *title) {
    if (!title) title = "";

    lcd_lock();
    uint32_t save_fg = lcd_get_fg();
    uint32_t save_bg = lcd_get_bg();

    lcd_set_fg(ui_color(UI_SELECT_TEXT));
    lcd_set_bg(ui_color(UI_ACCENT));

    char bar[41];
    memset(bar, ' ', 40);
    bar[40] = '\0';

    /* Title, padded left with a little margin */
    int tlen = (int)strlen(title);
    if (tlen > 34) tlen = 34;
    memcpy(bar + 1, title, (size_t)tlen);

    /* Right side: battery + clock */
    int bat = kbd_battery_percent();
    char rt[12];
    if (sys_rtc_is_set()) {
        int64_t epoch = sys_now_epoch_ms();
        int32_t secs = (int32_t)(epoch / 1000);
        int h = (secs / 3600) % 24;
        int m = (secs / 60) % 60;
        snprintf(rt, sizeof rt, "%02d:%02d", h, m);
    } else {
        snprintf(rt, sizeof rt, "--:--");
    }

    char info[20];
    if (bat >= 0)
        snprintf(info, sizeof info, "%d%% ", bat);
    else
        snprintf(info, sizeof info, "?%% ");
    append_cstr(info, sizeof info, rt);
    int ilen = (int)strlen(info);
    if (ilen > 38) ilen = 38;
    memcpy(bar + 40 - ilen, info, (size_t)ilen);

    for (int i = 0; i < 40; i++)
        lcd_draw_cell(i, 0, bar[i], ui_color(UI_SELECT_TEXT), ui_color(UI_ACCENT));

    lcd_set_fg(save_fg);
    lcd_set_bg(save_bg);
    /* Leave the caller on the first content row */
    lcd_set_cursor(0, 1);
    lcd_unlock();
}

int read_text_file(const char *path, char *buf, size_t cap, const char *label) {
    if (cap == 0) return -1;

    bool from_pipe = pipe_input_active() &&
                     (!path || !*path || (path[0] == '-' && path[1] == '\0'));
    if (from_pipe) {
        int n = pipe_input_read(buf, cap - 1);
        if (n < 0) n = 0;
        buf[n] = '\0';
        return n;
    }
    if (path && path[0] == '-' && path[1] == '\0') {
        sys_print(label);
        sys_print(": no pipeline input\n");
        return -1;
    }
    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }

    int n = sys_fread(path, buf, cap - 1);
    if (n < 0) {
        sys_print(label);
        sys_print(": cannot open file\n");
        return -1;
    }
    buf[n] = '\0';
    return n;
}

int write_text_file(const char *path, const char *buf, const char *label) {
    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }
    if (!buf) return -1;

    int n = sys_fwrite(path, buf, strlen(buf));
    if (n < 0) {
        sys_print(label);
        sys_print(": cannot write file\n");
        return -1;
    }
    return n;
}

int load_file_bytes(const char *path, uint8_t *buf, size_t cap,
                           uint32_t *out_len, const char *label) {
    char abs[APP_TOKEN_MAX];
    fat_file_t f;

    if (cap == 0) {
        if (out_len) *out_len = 0;
        return 0;
    }
    if (pipe_input_active() &&
        (!path || !*path || (path[0] == '-' && path[1] == '\0'))) {
        int n = pipe_input_read(buf, cap);
        if (n < 0) n = 0;
        if (out_len) *out_len = (uint32_t)n;
        return n;
    }
    if (path && path[0] == '-' && path[1] == '\0') {
        sys_print(label);
        sys_print(": no pipeline input\n");
        return -1;
    }
    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }

    app_make_abs(path, abs, sizeof abs);
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) {
        sys_print(label);
        sys_print(": cannot open file\n");
        return -1;
    }
    if (f.size > cap) {
        sys_print(label);
        sys_print(": file too large for current buffer\n");
        return -1;
    }

    uint32_t total = 0;
    while (total < f.size) {
        int32_t n = fat_read(&f, buf + total, f.size - total);
        if (n == FAT_ERR_EOF) break;
        if (n < 0) {
            sys_print(label);
            sys_print(": read error\n");
            return -1;
        }
        total += (uint32_t)n;
    }

    if (out_len) *out_len = total;
    return (int)total;
}

void app_make_abs(const char *path, char *out, size_t out_sz) {
    path_make_abs(_sys_cwd, path, out, out_sz);
}

void app_join_path(const char *root, const char *name, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    if (!root || !*root || strcmp(root, "/") == 0) {
        append_cstr(out, out_sz, "/");
        if (name) append_cstr(out, out_sz, name);
        return;
    }

    append_cstr(out, out_sz, root);
    if (name && *name) {
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, name);
    }
}

const char *next_token(const char *s, char *tok, size_t tok_sz) {
    size_t i = 0;
    s = skip_ws(s);
    if (!s || !*s) {
        tok[0] = '\0';
        return s;
    }
    while (*s && !isspace((unsigned char)*s) && i < tok_sz - 1) {
        tok[i++] = *s++;
    }
    tok[i] = '\0';
    while (*s && !isspace((unsigned char)*s)) s++;
    return s;
}

bool str_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n) return true;
    }
    return false;
}

int str_casecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool is_leap_year(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

const char *month_name_local(int month) {
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return "Unknown";
    return months[month - 1];
}

int build_month_local(void) {
    static const char *abbr[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(__DATE__, abbr[i], 3) == 0) return i + 1;
    }
    return 1;
}

int build_year_local(void) {
    int year = atoi(__DATE__ + 7);
    return year > 0 ? year : 1970;
}

int build_day_local(void) {
    int day = atoi(__DATE__ + 4);
    return day > 0 ? day : 1;
}

int days_in_month_local(int month, int year) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) return days[month - 1] + (is_leap_year(year) ? 1 : 0);
    if (month >= 1 && month <= 12) return days[month - 1];
    return 30;
}

int day_of_week_local(int y, int m, int d) {
    if (m < 3) {
        m += 12;
        y--;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

void rtrim_in_place(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

int app_read_line(const char *prompt, char *buf, size_t size) {
    return console_read_line(prompt, buf, size);
}

bool ci_eq(const char *a, const char *b) {
    return str_casecmp_local(a, b) == 0;
}
