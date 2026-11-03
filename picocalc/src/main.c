/*
 * main.c — Mellivora OS PicoCalc runtime
 *
 * Bootstraps all PicoCalc hardware, then enters PicoLair — a shell loop
 * that mirrors the Mellivora HB Lair command set as closely as the
 * RP2040 platform allows.
 *
 * Hardware driven here:
 *   - ILI9488 320x320 LCD via SPI1 (lcd.c)
 *   - QWERTY keyboard via I2C1 STM32 co-processor (kbd.c)
 *   - SD card via SPI0 (sd.c)
 *   - USB/UART serial console (Pico SDK stdio)
 *
 * All output goes to BOTH the LCD and the serial console so the device
 * is usable with or without the display initialised.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/uart.h"

#include "picocalc_hw.h"
#include "lcd.h"
#include "kbd.h"
#include "sd.h"
#include "fat.h"
#include "syscall.h"
#include "ui/ui.h"
#include "apps.h"
#include "apps/app_shared.h"
#include "core/expr.h"
#include "core/glob.h"
#include "core/history.h"
#include "core/datetime.h"
#include "core/path_util.h"
#include "core/shell_parse.h"
#include "core/lfn.h"
#include "hal/hal.h"
#include "pipe.h"
#include "shell.h"
#ifdef PICO_CYW43_SUPPORTED
#include "net.h"
#include "netapps.h"
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */
/* Last exit status — set after each command dispatch; $? expands to it. */
static int _last_exit = 0;

#define LINE_BUF        256
#define CWD_MAX         256
#if defined(PICO_RP2350A) || defined(PICO_RP2350)
#define HIST_ENTRIES    64
#define APP_REDIR_MAX   8192
#else
#define HIST_ENTRIES    32
#define APP_REDIR_MAX   4096
#endif
#define HIST_ENTRY_SZ   LINE_BUF
#define ALIAS_MAX       16
#define ALIAS_NAME_SZ   16
#define ALIAS_VALUE_SZ  LINE_BUF
#define ALIAS_FILE      "/ALIASES.CFG"
#define ENV_FILE        "/ENV.CFG"

/* Global interrupt flag — checked by long-running loops.
 * Aliased to _sys_interrupted (declared in syscall.h) so apps in apps.c /
 * apps in subdirs can also check it via sys_interrupted(). */
#define _interrupted _sys_interrupted

/* Status bar flag — declared early so cmd_sysinfo/cmd_status can see it */
static volatile bool _statusbar_enabled = false;
static volatile bool _screensaver_active = false;
static void _draw_status_bar(void);
static void _run_screensaver(void);

/* Confirmation helpers (defined later, near read_input) */
static bool confirm(const char *msg);
static bool parse_force_flag(const char **arg);
static void rm_recurse(const char *path);

#define BANNER \
    "\n" \
    "  Mellivora OS v" MELLIVORA_VERSION "\n" \
    "  ====================================\n" \
    "  PicoCalc bare-metal UNIX shell\n" \
    "  Built: " __DATE__ "\n" \
    "  Type 'help' to list commands.\n" \
    "\n"

/* ANSI escape sequences forwarded to the serial console */
#define ANSI_UP   "\x1b[A"
#define ANSI_DOWN "\x1b[B"

/* ------------------------------------------------------------------ */
/* Output helpers — mirror to both LCD and serial                       */
/* ------------------------------------------------------------------ */

static bool _lcd_ready = false;

/* Forward declarations for output redirection */
static bool   _redir_active;
static void   redir_char(char c);

static void out_char(char c) {
    if (sys_output_capture_char(c)) return;
    if (!_lcd_ready) {
        putchar_raw(c);
        return;
    }
    sys_putchar(c);
}

static void out_str(const char *s) {
    while (*s) out_char(*s++);
}

static void out_role(ui_role_t role, const char *s) {
    uint32_t saved = lcd_get_fg();
    lcd_set_fg(ui_color(role));
    out_str(s);
    lcd_set_fg(saved);
}


static void out_fmt(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out_str(buf);
}

/* ------------------------------------------------------------------ */
/* Current working directory                                            */
/* ------------------------------------------------------------------ */

static char _cwd[CWD_MAX] = "/";
static bool _fat_mounted  = false;

typedef struct {
    bool used;
    char name[ALIAS_NAME_SZ];
    char value[ALIAS_VALUE_SZ];
} alias_entry_t;

static alias_entry_t _aliases[ALIAS_MAX];

/* Shell variables — set NAME=VALUE; expanded as $NAME or ${NAME} */
#define VAR_MAX 16
typedef struct {
    bool used;
    char name[ALIAS_NAME_SZ];
    char value[ALIAS_VALUE_SZ];
} var_entry_t;
static var_entry_t _vars[VAR_MAX];

static const char *var_lookup(const char *name) {
    for (int i = 0; i < VAR_MAX; i++) {
        if (_vars[i].used && strcmp(_vars[i].name, name) == 0)
            return _vars[i].value;
    }
    return NULL;
}

static bool var_set(const char *name, const char *value) {
    if (!name || !*name) return false;
    int slot = -1, free_slot = -1;
    for (int i = 0; i < VAR_MAX; i++) {
        if (_vars[i].used && strcmp(_vars[i].name, name) == 0) { slot = i; break; }
        if (!_vars[i].used && free_slot < 0) free_slot = i;
    }
    if (slot < 0) slot = free_slot;
    if (slot < 0) return false;
    _vars[slot].used = true;
    copy_cstr(_vars[slot].name, sizeof _vars[slot].name, name);
    copy_cstr(_vars[slot].value, sizeof _vars[slot].value, value ? value : "");
    return true;
}

static bool var_unset(const char *name) {
    for (int i = 0; i < VAR_MAX; i++) {
        if (_vars[i].used && strcmp(_vars[i].name, name) == 0) {
            _vars[i].used = false;
            return true;
        }
    }
    return false;
}

static const char *var_lookup_cb(void *ctx, const char *name) {
    (void)ctx;
    return var_lookup(name);
}

/* In-place expansion of $NAME, ${NAME}, and $? in a buffer. */
static void var_expand(char *line, size_t line_sz) {
    shell_expand_vars(line, line_sz, var_lookup_cb, NULL, _last_exit);
}

/*
 * Build an absolute path from cwd + input.
 * Result is stored in out (size CWD_MAX).
 * Handles ".." components.
 */
static void resolve_abs(const char *input, char *out) {
    path_canonicalize(_cwd, input, out, CWD_MAX);
}

static void prompt_append(char *buf, size_t cap, size_t *pos, const char *s) {
    while (s && *s && *pos + 1 < cap) buf[(*pos)++] = *s++;
}

static void prompt_time(char *out, size_t cap) {
    if (sys_rtc_is_set()) {
        int32_t secs = (int32_t)(sys_now_epoch_ms() / 1000);
        int h = (secs / 3600) % 24;
        int m = (secs / 60) % 60;
        snprintf(out, cap, "%02d:%02d", h, m);
    } else {
        uint32_t secs = to_ms_since_boot(get_absolute_time()) / 1000;
        snprintf(out, cap, "%lu:%02lu", (unsigned long)(secs / 3600),
                 (unsigned long)((secs / 60) % 60));
    }
}

/* Render PROMPT with %c (cwd), %t (time), %b (battery), %w (WiFi), %% . */
static void render_prompt(char *buf, size_t cap) {
    const char *fmt = var_lookup("PROMPT");
    if (!fmt || !*fmt) fmt = "%c";

    size_t pos = 0;
    for (const char *p = fmt; *p && pos + 1 < cap; p++) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }
        p++;
        if (*p == '\0') break;

        switch (*p) {
            case 'c':
                prompt_append(buf, cap, &pos, _cwd);
                break;
            case 't': {
                char t[16];
                prompt_time(t, sizeof t);
                prompt_append(buf, cap, &pos, t);
                break;
            }
            case 'b': {
                int bat = kbd_battery_percent();
                char t[12];
                if (bat >= 0) snprintf(t, sizeof t, "%d%%", bat);
                else copy_cstr(t, sizeof t, "?");
                prompt_append(buf, cap, &pos, t);
                break;
            }
            case 'w':
#ifdef PICO_CYW43_SUPPORTED
                prompt_append(buf, cap, &pos, net_is_connected() ? "W" : "-");
#else
                prompt_append(buf, cap, &pos, "-");
#endif
                break;
            case '%':
                buf[pos++] = '%';
                break;
            default:
                if (pos + 2 < cap) {
                    buf[pos++] = '%';
                    buf[pos++] = *p;
                }
                break;
        }
    }
    buf[pos] = '\0';
}

static void out_prompt(void) {
#if !defined(PICO_RP2350A) && !defined(PICO_RP2350)
    /* On Pico 1 (no core1), refresh status bar at each prompt */
    _draw_status_bar();
#endif
    char prompt[96];
    render_prompt(prompt, sizeof prompt);

    const char *fmt = var_lookup("PROMPT");
    uint32_t saved = lcd_get_fg();
    if (!fmt || !*fmt) {
        if (_last_exit != 0) {
            lcd_set_fg(ui_color(UI_ERROR));
            out_str("! ");
        }
        lcd_set_fg(ui_color(UI_ACCENT));
        out_str(*prompt ? prompt : "/");
        lcd_set_fg(ui_color(UI_MUTED));
        out_str(" > ");
    } else {
        lcd_set_fg(ui_color(UI_ACCENT));
        out_str(prompt);
    }
    lcd_set_fg(saved);
}

/* ------------------------------------------------------------------ */
/* Persistent CWD — saved on every successful 'cd', restored at boot   */
/* ------------------------------------------------------------------ */
#define LASTCWD_FILE "/LASTCWD.TXT"

static void cwd_persist(void) {
    if (!_fat_mounted) return;
    /* Best-effort write; do not surface errors (cwd persistence is a nicety). */
    fat_write_atomic(LASTCWD_FILE, (const uint8_t *)_cwd, (uint32_t)strlen(_cwd));
}

static void cwd_restore(void) {
    if (!_fat_mounted) return;
    fat_file_t f;
    if (fat_open(LASTCWD_FILE, &f) != FAT_OK) return;
    char buf[CWD_MAX];
    int32_t n = fat_read(&f, (uint8_t *)buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    /* Strip trailing whitespace */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '  || buf[n - 1] == '\t')) buf[--n] = '\0';
    if (buf[0] != '/') return;
    if (fat_is_dir(buf) != FAT_OK) return; /* dir vanished — keep current cwd */
    strncpy(_cwd, buf, CWD_MAX - 1);
    _cwd[CWD_MAX - 1] = '\0';
    strncpy(_sys_cwd, _cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
}

static void dispatch(char *line);
static void dispatch_ex(char *line, bool record_history);
static void dispatch_single(char *line);
static void dispatch_single_no_glob(char *line);
static void cmd_help(const char *arg);
static void cmd_script(const char *arg);

static void cmd_read(const char *arg);
static void cmd_which(const char *arg);
static void cmd_expr(const char *arg);

/* Forward declarations for the command registry (defined in the tab-completion
   section further below, but used by cmd_help/_cmd_suggest earlier). */
typedef struct { const char *name; const char *group; } cmd_meta_t;
static const cmd_meta_t _cmd_registry[];

/* ------------------------------------------------------------------ */
/* History                                                              */
/* ------------------------------------------------------------------ */

static char _hist_storage[HIST_ENTRIES][HIST_ENTRY_SZ];
static shell_history_t _history = {
    .storage = &_hist_storage[0][0],
    .capacity = HIST_ENTRIES,
    .entry_size = HIST_ENTRY_SZ,
};

static int hist_count(void) {
    return (int)shell_history_count(&_history);
}

static void hist_push(const char *line) {
    shell_history_push(&_history, line);
}

/* Returns pointer to history entry (0 = oldest), or NULL if out of range. */
static const char *hist_get(int offset) {
    if (offset < 0) return NULL;
    return shell_history_get(&_history, (size_t)offset);
}

static void hist_clear(void) {
    shell_history_clear(&_history);
}

/* ----- Persistent history (/HISTORY.LOG) -------------------------- */
#define HISTORY_FILE "/HISTORY.LOG"

static void hist_save(void) {
    if (!_fat_mounted) return;
    /* Build a single buffer of recent entries (newest last) */
    char buf[HIST_ENTRY_SZ * 8 + 8];  /* keep last 8 lines max for write */
    size_t pos = 0;
    int start = (hist_count() > 8) ? hist_count() - 8 : 0;
    for (int i = start; i < hist_count(); i++) {
        const char *h = hist_get(i);
        if (!h) continue;
        size_t l = strlen(h);
        if (pos + l + 2 >= sizeof buf) break;
        memcpy(buf + pos, h, l); pos += l;
        buf[pos++] = '\n';
    }
    if (pos == 0) return;
    buf[pos] = '\0';
    /* Append-on-write: read existing + append + truncate to ~32 lines */
    char existing[HIST_ENTRY_SZ * 32 + 16];
    fat_file_t f;
    int32_t old_n = 0;
    if (fat_open(HISTORY_FILE, &f) == FAT_OK) {
        old_n = fat_read(&f, (uint8_t *)existing, sizeof existing - 1);
        if (old_n < 0) old_n = 0;
    }
    if (old_n + (int)pos > (int)sizeof existing - 1) {
        /* Drop oldest half */
        int keep = (int)sizeof existing / 2;
        int drop = old_n - keep;
        if (drop < 0) drop = 0;
        memmove(existing, existing + drop, old_n - drop);
        old_n -= drop;
    }
    if (old_n + pos < sizeof existing) {
        memcpy(existing + old_n, buf, pos);
        /* Crash-safe replace via temp file + rename. */
        fat_write_atomic(HISTORY_FILE, (const uint8_t *)existing, (uint32_t)(old_n + pos));
    }
}

static void hist_load(void) {
    if (!_fat_mounted) return;
    fat_file_t f;
    if (fat_open(HISTORY_FILE, &f) != FAT_OK) return;
    char buf[HIST_ENTRY_SZ * 32 + 16];
    int32_t n = fat_read(&f, (uint8_t *)buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    char *line_p = buf;
    for (char *p = buf; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            if (*line_p) hist_push(line_p);
            line_p = p + 1;
        }
    }
    if (*line_p) hist_push(line_p);
}

static int alias_find_slot(const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (_aliases[i].used && strcmp(_aliases[i].name, name) == 0) return i;
    }
    return -1;
}

static bool alias_valid_name(const char *name) {
    if (!name || !*name) return false;
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
    }
    return true;
}

static bool alias_save(void) {
    if (!_fat_mounted) return false;

    char buf[2560];
    int pos = 0;
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (!_aliases[i].used) continue;
        int wrote = snprintf(buf + pos, sizeof buf - (size_t)pos,
                             "%s=%s\n", _aliases[i].name, _aliases[i].value);
        if (wrote < 0 || wrote >= (int)(sizeof buf - (size_t)pos)) return false;
        pos += wrote;
    }

    return sys_fwrite(ALIAS_FILE, buf, (uint32_t)pos) >= 0;
}

static bool alias_set_entry(const char *name, const char *value, bool persist, bool verbose) {
    if (!alias_valid_name(name)) {
        if (verbose) out_str("alias: invalid name\n");
        return false;
    }

    int slot = alias_find_slot(name);
    if (slot < 0) {
        for (int i = 0; i < ALIAS_MAX; i++) {
            if (!_aliases[i].used) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        if (verbose) out_str("alias: table full\n");
        return false;
    }

    _aliases[slot].used = true;
    copy_cstr(_aliases[slot].name, sizeof _aliases[slot].name, name);
    copy_cstr(_aliases[slot].value, sizeof _aliases[slot].value, value ? value : "");

    if (persist && _fat_mounted && !alias_save() && verbose) {
        out_str("alias: warning: could not save aliases\n");
    }

    if (verbose) out_fmt("alias %s='%s'\n", _aliases[slot].name, _aliases[slot].value);
    return true;
}

static void alias_load(void) {
    memset(_aliases, 0, sizeof _aliases);
    if (!_fat_mounted) return;

    char buf[1024];
    int n = sys_fread(ALIAS_FILE, buf, sizeof buf - 1);
    if (n < 0) return;
    buf[n] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        trim_right(line);
        char *s = skip_spaces(line);
        if (!s || !*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq++ = '\0';
        trim_right(s);
        eq = skip_spaces(eq);
        if (!eq || !*eq) continue;

        size_t len = strlen(eq);
        if (len >= 2 && ((eq[0] == '"' && eq[len - 1] == '"') || (eq[0] == '\'' && eq[len - 1] == '\''))) {
            eq[len - 1] = '\0';
            eq++;
        }

        (void)alias_set_entry(s, eq, false, false);
    }
}

static void alias_list(void) {
    int shown = 0;
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (!_aliases[i].used) continue;
        out_fmt("alias %s='%s'\n", _aliases[i].name, _aliases[i].value);
        shown++;
    }
    if (shown == 0) out_str("alias: no aliases defined\n");
}

/* ------------------------------------------------------------------ */
/* Persistent environment variables                                     */
/* ------------------------------------------------------------------ */
static bool var_save(void) {
    if (!_fat_mounted) return false;

    char buf[2560];
    int pos = 0;
    for (int i = 0; i < VAR_MAX; i++) {
        if (!_vars[i].used) continue;
        int wrote = snprintf(buf + pos, sizeof buf - (size_t)pos,
                             "%s=%s\n", _vars[i].name, _vars[i].value);
        if (wrote < 0 || wrote >= (int)(sizeof buf - (size_t)pos)) return false;
        pos += wrote;
    }

    return sys_fwrite(ENV_FILE, buf, (uint32_t)pos) >= 0;
}

static void var_load(void) {
    if (!_fat_mounted) return;

    char buf[1024];
    int n = sys_fread(ENV_FILE, buf, sizeof buf - 1);
    if (n < 0) return;
    buf[n] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        trim_right(line);
        char *s = skip_spaces(line);
        if (!s || !*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq++ = '\0';
        trim_right(s);
        eq = skip_spaces(eq);
        if (!s || !*s) continue;

        size_t len = strlen(eq);
        if (len >= 2 && ((eq[0] == '"' && eq[len - 1] == '"') || (eq[0] == '\'' && eq[len - 1] == '\''))) {
            eq[len - 1] = '\0';
            eq++;
        }

        (void)var_set(s, eq);
    }
}

static bool alias_expand_line(char *line, size_t line_sz) {
    char first[ALIAS_NAME_SZ] = {0};
    char *s = skip_spaces(line);
    if (!s || !*s) return false;

    size_t i = 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t' && i + 1 < sizeof first) {
        first[i] = s[i];
        i++;
    }
    first[i] = '\0';

    int slot = alias_find_slot(first);
    if (slot < 0) return false;

    const char *rest = s + i;
    while (*rest == ' ' || *rest == '\t') rest++;

    char expanded[LINE_BUF];
    if (*rest) snprintf(expanded, sizeof expanded, "%s %s", _aliases[slot].value, rest);
    else copy_cstr(expanded, sizeof expanded, _aliases[slot].value);

    if (strcmp(expanded, line) == 0) return false;
    copy_cstr(line, line_sz, expanded);
    return true;
}

/* Expand aliases with a depth limit to prevent infinite recursion. */
static void alias_expand_safe(char *line, size_t line_sz) {
    for (int depth = 0; depth < 8; depth++) {
        if (!alias_expand_line(line, line_sz)) break;
    }
}

static bool history_expand_bang(char *line, size_t line_sz) {
    if (!line || line[0] != '!') return false;

    const char *entry = NULL;
    if (strcmp(line, "!!") == 0) entry = hist_get(hist_count() - 1);
    else {
        int n = atoi(line + 1);
        if (n >= 1) entry = hist_get(n - 1);
    }

    if (!entry) {
        out_str("history: event not found\n");
        line[0] = '\0';
        return true;
    }

    copy_cstr(line, line_sz, entry);
    out_fmt("%s\n", line);
    return true;
}

/* ------------------------------------------------------------------ */
/* Shell commands                                                        */
/* ------------------------------------------------------------------ */

static absolute_time_t g_boot_time;

static void cmd_history(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);

    if (!s || !*s) {
        if (hist_count() == 0) {
            out_str("history: empty\n");
            return;
        }
        for (int i = 0; i < hist_count(); i++) out_fmt("%2d  %s\n", i + 1, hist_get(i));
        return;
    }

    if (strcmp(s, "clear") == 0) {
        hist_clear();
        out_str("history cleared\n");
        return;
    }

    if (strncmp(s, "run ", 4) == 0 || strncmp(s, "exec ", 5) == 0) {
        s = skip_spaces(s + ((s[0] == 'r') ? 4 : 5));
        int n = s ? atoi(s) : 0;
        const char *entry = hist_get(n - 1);
        if (!entry) {
            out_str("history: number out of range\n");
            return;
        }
        char replay[LINE_BUF];
        copy_cstr(replay, sizeof replay, entry);
        out_fmt("%s\n", replay);
        dispatch(replay);
        return;
    }

    int n = atoi(s);
    const char *entry = hist_get(n - 1);
    if (entry) out_fmt("%2d  %s\n", n, entry);
    else out_str("usage: history [clear|run N]\n");
}

static void cmd_alias(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);

    if (!s || !*s) {
        alias_list();
        return;
    }

    char *name = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '=') s++;
    char sep = *s;
    if (*s) *s++ = '\0';
    s = skip_spaces(s);

    if (!sep || !s || !*s) {
        int slot = alias_find_slot(name);
        if (slot < 0) out_str("alias: not found\n");
        else out_fmt("alias %s='%s'\n", _aliases[slot].name, _aliases[slot].value);
        return;
    }

    trim_right(s);
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        s[len - 1] = '\0';
        s++;
    }

    (void)alias_set_entry(name, s, true, true);
}

static void cmd_unalias(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");
    char *name = skip_spaces(tmp);

    if (!name || !*name) {
        out_str("usage: unalias NAME\n");
        return;
    }

    trim_right(name);
    int slot = alias_find_slot(name);
    if (slot < 0) {
        out_str("unalias: not found\n");
        return;
    }

    memset(&_aliases[slot], 0, sizeof _aliases[slot]);
    if (_fat_mounted && !alias_save()) out_str("unalias: warning: could not save aliases\n");
    else out_fmt("unalias: removed %s\n", name);
}

/* set [NAME=VALUE] | set NAME VALUE | set    -> list all */
static void cmd_setvar(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);
    if (!s || !*s) {
        bool any = false;
        for (int i = 0; i < VAR_MAX; i++) {
            if (_vars[i].used) {
                out_fmt("%s=%s\n", _vars[i].name, _vars[i].value);
                any = true;
            }
        }
        if (!any) out_str("(no variables)\n");
        return;
    }
    /* Split NAME=VALUE or NAME VALUE */
    char *name = s;
    char *value = NULL;
    char *eq = strchr(s, '=');
    char *sp = strchr(s, ' ');
    if (eq && (!sp || eq < sp)) { *eq = '\0'; value = eq + 1; }
    else if (sp) { *sp = '\0'; value = skip_spaces(sp + 1); }
    if (!name || !*name) { out_str("usage: set NAME=VALUE\n"); return; }
    /* Strip optional surrounding quotes from value */
    if (value && (*value == '"' || *value == '\'')) {
        char q = *value++;
        char *end = strrchr(value, q);
        if (end) *end = '\0';
    }
    if (!var_set(name, value)) {
        out_str("set: too many variables\n");
        return;
    }
    if (_fat_mounted && !var_save()) out_str("set: warning: could not save variables\n");
}

static void cmd_unsetvar(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);
    trim_right(s);
    if (!s || !*s) { out_str("usage: unset NAME\n"); return; }
    if (!var_unset(s)) {
        out_fmt("unset: %s not found\n", s);
        return;
    }
    if (_fat_mounted && !var_save()) out_str("unset: warning: could not save variables\n");
}

static void cmd_man(const char *arg) {
    char topic[32] = {0};
    if (arg) {
        size_t i = 0;
        while (*arg == ' ' || *arg == '\t') arg++;
        while (arg[i] && !isspace((unsigned char)arg[i]) && i + 1 < sizeof topic) {
            topic[i] = arg[i];
            i++;
        }
        topic[i] = '\0';
    }
    /* Delegate entirely to cmd_help; it handles groups, detail pages, and
       individual command name look-ups via the registry. */
    cmd_help(*topic ? topic : NULL);
}

/* Print all registry commands belonging to a group, 4 per row, indented. */
static void _help_print_group(const char *group) {
    int col = 0;
    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        if (strcmp(c->group, group) != 0) continue;
        if (col == 0) out_str("  ");
        out_fmt("%-14s", c->name);
        col++;
        if (col == 4) { out_char('\n'); col = 0; }
    }
    if (col) out_char('\n');
}

static void cmd_help(const char *arg) {
    const char *topic = arg ? arg : "";
    while (*topic == ' ') topic++;

    if (strncmp(topic, "-k ", 3) == 0 || strncmp(topic, "--search ", 9) == 0) {
        const char *query = topic[1] == 'k' ? topic + 3 : topic + 9;
        while (*query == ' ' || *query == '\t') query++;
        if (!*query) {
            out_str("usage: help -k KEYWORD\n");
            return;
        }

        int matches = 0;
        for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
            if (!str_contains_ci(c->name, query) &&
                !str_contains_ci(c->group, query)) continue;
            out_fmt("  %-16s [%s]\n", c->name, c->group);
            matches++;
        }
        if (matches == 0) out_fmt("help: no commands match '%s'\n", query);
        else out_fmt("%d matching command(s); use 'man COMMAND' for details\n", matches);
        return;
    }

    /* ---- Top-level overview ---- */
    if (!*topic) {
        out_role(UI_ACCENT, "Mellivora OS " MELLIVORA_VERSION);
        out_str("  command overview\n");
        out_role(UI_MUTED, "Use  man TOPIC  for details on any command.\n\n");
        out_role(UI_MUTED, "Search commands with: help -k KEYWORD\n\n");

        out_role(UI_ACCENT, "[system]");
        out_str("  help man uname sysinfo uptime clear battery backlight\n");
        out_str("          echo date reboot screenshot version lock sleep dmesg id\n\n");

        out_role(UI_ACCENT, "[shell]");
        out_str("   history alias unalias set unset env script read\n");
        out_str("          which type expr\n\n");

        out_role(UI_ACCENT, "[fs]");
        out_str("      mount ls cd pwd cat touch write mkdir rm rename cp mv\n");
        out_str("          stat tree du df edit hexedit browse sdinfo sdread\n\n");

        out_role(UI_ACCENT, "[apps]");
        out_str("    notes journal todo planner habits bookmarks\n");
        out_str("          calc basic tcc forth home dashboard sysmon samples\n");
        out_str("          head tail grep sort wc cut rev tr sed md find hexdump\n");
        out_str("          games snake tetris life piano mandelbrot paint sprite\n");
        out_str("          stopwatch pomodoro clock cal theme\n\n");

#ifdef PICO_CYW43_SUPPORTED
        out_role(UI_ACCENT, "[net]");
        out_str("     wifi ping ifconfig ntp dns fetch wget weather irc telnet\n\n");
#endif

        out_role(UI_MUTED, "Keys: UP/DOWN=history  Tab=complete  Ctrl-U=clear  Ctrl-L=redraw\n");
        out_str("Tip:  append  | more  to long commands for paging.\n");
        return;
    }

    /* ---- Dynamic group listing from registry ---- */
    bool found_group = false;
    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        if (strcmp(c->group, topic) == 0) { found_group = true; break; }
    }
    if (found_group) {
        out_role(UI_ACCENT, "[");
        out_role(UI_ACCENT, topic);
        out_str("] commands:\n");
        _help_print_group(topic);
        return;
    }

    /* ---- Per-topic detail pages ---- */
    if (!strcmp(topic, "fs") || !strcmp(topic, "storage")) {
        out_str("Storage and filesystem commands:\n");
        out_str("  mount               Mount the SD FAT volume\n");
        out_str("  ls [path]           List directory\n");
        out_str("  cd [path]           Change directory (persistent)\n");
        out_str("  cat <file>          Show file contents\n");
        out_str("  touch <file>        Create empty file\n");
        out_str("  write <f> <text>    Replace file with text\n");
        out_str("  mkdir <path>        Create directory\n");
        out_str("  rm [-rf] <path>     Remove file or directory\n");
        out_str("  cp SRC DST          Copy file or directory\n");
        out_str("  mv SRC DST          Move or rename\n");
        out_str("  stat <path>         File info\n");
        out_str("  tree [path]         Recursive directory tree\n");
        out_str("  du [path]           Disk usage\n");
        out_str("  df                  Free space\n");
        out_str("  edit <file>         Line editor\n");
        out_str("  hexedit <file>      Byte editor\n");
        out_str("  browse [path]       Visual file browser\n");
        out_str("  diff <a> <b>        Line diff\n");
        out_str("  find [path] [-name PATTERN] [-type f|d]\n");
        out_str("  head/tail [-n N] <file>\n");
        out_str("  grep [-n] [-e] PATTERN <file>\n");
        out_str("  sort [-r] [-n] <file>     wc <file>    cut -dC -fN <file>\n");
        out_str("  rev <file>                hexdump/od <file>\n");
        out_str("  tr [-d] SET1 [SET2] [FILE]   Translate or delete characters\n");
        out_str("  sed s/OLD/NEW/[g] [FILE]    Literal substitution\n");
        out_str("  md <FILE> / markdown        Render Markdown for the LCD\n");
        return;
    }

    if (!strcmp(topic, "lang") || !strcmp(topic, "programming")) {
        out_str("Programming environments:\n");
        out_str("  basic [file.bas]  BASIC: PRINT LET INPUT IF FOR/NEXT GOSUB RUN LIST\n");
        out_str("  tcc [file.tc]     Tiny C: int arrays if while print vars clear\n");
        out_str("  forth             Forth: + - * / dup drop swap . .s words :\n");
        out_str("  calc [expr]       Quick arithmetic REPL (infix)\n");
        out_str("  script <file>     Run a shell script from SD\n");
        return;
    }

    if (!strcmp(topic, "apps") || !strcmp(topic, "productivity")) {
        out_str("Productivity apps (data persisted on SD):\n");
        out_str("  notes             Edit the default notes file\n");
        out_str("  journal           Dated journal entries\n");
        out_str("  todo              Task list with done/undone tracking\n");
        out_str("  planner           Dated agenda\n");
        out_str("  habits            Habit tracker with streaks\n");
        out_str("  bookmarks         Saved path/app bookmarks\n");
        out_str("  settings          Device preferences (backlight, theme, paths)\n");
        return;
    }

    if (!strcmp(topic, "games") || !strcmp(topic, "fun")) {
        out_str("Games and creative tools:\n");
        out_str("  snake             Classic snake (persistent high score)\n");
        out_str("  tetris            Tetris\n");
        out_str("  life              Conway's Game of Life\n");
        out_str("  mandelbrot        Mandelbrot viewer\n");
        out_str("  piano             On-screen piano (PWM audio)\n");
        out_str("  paint             Pixel-art canvas\n");
        out_str("  sprite            16x16 sprite editor\n");
        out_str("  dice [N] [S]      Roll N dice with S sides\n");
        out_str("  coin [N]          Flip N coins\n");
        out_str("  guess             Guess-the-number game\n");
        return;
    }

#ifdef PICO_CYW43_SUPPORTED
    if (!strcmp(topic, "net") || !strcmp(topic, "network") || !strcmp(topic, "wifi")) {
        out_str("Network commands (Pico 2W only):\n");
        out_str("  wifi scan                Scan for networks\n");
        out_str("  wifi connect SSID PASS   Connect to network\n");
        out_str("  wifi status              Connection status\n");
        out_str("  ping HOST                ICMP echo\n");
        out_str("  ifconfig                 IP/gateway/DNS info\n");
        out_str("  dns HOST                 Resolve hostname\n");
        out_str("  ntp [server]             Sync clock from NTP\n");
        out_str("  fetch URL                HTTP GET to display\n");
        out_str("  wget URL                 HTTP GET to file\n");
        out_str("  weather [location]       Fetch weather\n");
        out_str("  irc HOST PORT NICK CHAN  IRC client\n");
        out_str("  telnet HOST PORT         Telnet client\n");
        out_str("  netstat                  Open sockets\n");
        return;
    }
#endif

    if (!strcmp(topic, "shell") || !strcmp(topic, "scripting")) {
        out_str("Shell features:\n");
        out_str("  history [clear|run N]  Show, clear, or replay\n");
        out_str("  !! / !N                Rerun last / numbered command\n");
        out_str("  alias NAME VALUE       Persistent alias\n");
        out_str("  unalias NAME           Remove alias\n");
        out_str("  set NAME=VALUE         Set shell variable\n");
        out_str("  unset NAME             Remove variable\n");
        out_str("  env                    Show variables\n");
        out_str("  read [-p TEXT] NAME... Read a line into variables\n");
        out_str("  which / type CMD       Locate a command, alias, or file\n");
        out_str("  expr EXPR              Evaluate an integer expression\n");
        out_str("  script <file>          Run a script through the shell parser\n");
        out_str("  CMD > FILE             Redirect output\n");
        out_str("  CMD >> FILE            Append output\n");
        out_str("  CMD ; CMD              Chain commands (also && and ||)\n");
        out_str("  CMD | more             Page long output\n");
        out_str("  $NAME  ${NAME}         Variable expansion\n");
        out_str("  $((EXPR))              Arithmetic expansion\n");
        out_str("  *.TXT                  Wildcard expansion (one run per match)\n");
        out_str("  PROMPT='%c> '          %c cwd, %t time, %b battery, %w WiFi\n");
        out_str("  /PROFILE.CFG           Startup script for aliases and variables\n");
        return;
    }

    /* Fall back: look for a single command name match in the registry and show its group */
    const char *matched_group = NULL;
    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        if (strcmp(c->name, topic) == 0) { matched_group = c->group; break; }
    }
    if (matched_group) {
        out_fmt("'%s' is in group [%s]. Try: man %s\n", topic, matched_group, matched_group);
        return;
    }

    out_fmt("No help for '%s'. Try: help  or  man fs|shell|apps|lang|net|games\n", topic);
}

static void cmd_uname(const char *arg) {
    bool all = (arg && (*arg == 'a' || strstr(arg, "-a")));
#ifdef PICO_CYW43_SUPPORTED
    out_str("Mellivora OS v" MELLIVORA_VERSION " - PicoCalc target (RP2350 + CYW43 WiFi)\n");
#elif defined(PICO_RP2350A) || defined(PICO_RP2350)
    out_str("Mellivora OS v" MELLIVORA_VERSION " - PicoCalc target (RP2350)\n");
#else
    out_str("Mellivora OS v" MELLIVORA_VERSION " - PicoCalc target (RP2040)\n");
#endif
    if (!all) return;
    out_fmt("  LCD:  ILI9488 %dx%d via SPI1\n", LCD_WIDTH, LCD_HEIGHT);
    out_str("  KBD:  STM32 co-proc via I2C1\n");
    out_str("  SD:   SPI0 block device\n");
    out_str("  UART: USB CDC + UART0\n");
#ifdef PICO_CYW43_SUPPORTED
    out_str("  WLAN: CYW43439 802.11n\n");
#endif
    out_str("  Built: " __DATE__ " " __TIME__ "\n");
}

static void cmd_version(void) {
    out_str("Mellivora OS v" MELLIVORA_VERSION "\n");
    out_str("Built: " __DATE__ " " __TIME__ "\n");
    out_str("https://github.com/" MELLIVORA_REPOSITORY "\n");
}

static void cmd_uptime(void) {
    int64_t us = absolute_time_diff_us(g_boot_time, get_absolute_time());
    int64_t sec = us / 1000000LL;
    int64_t days  = sec / 86400;
    int64_t hours = (sec % 86400) / 3600;
    int64_t mins  = (sec % 3600) / 60;
    int64_t secs  = sec % 60;
    if (days > 0)
        out_fmt("up %lldd %02lldh %02lldm %02llds\n", days, hours, mins, secs);
    else if (hours > 0)
        out_fmt("up %02lldh %02lldm %02llds\n", hours, mins, secs);
    else
        out_fmt("up %02lldm %02llds\n", mins, secs);
}

/* ------------------------------------------------------------------ */
/* date — software RTC (settable, persisted to /CLOCK.CFG)            */
/* ------------------------------------------------------------------ */
#define CLOCK_FILE "/CLOCK.CFG"

static void clock_persist(void) {
    if (!_fat_mounted) return;
    char buf[40];
    int n = snprintf(buf, sizeof buf, "%lld\n", (long long)sys_now_epoch_ms());
    if (n <= 0) return;
    fat_write_atomic(CLOCK_FILE, (const uint8_t *)buf, (uint32_t)n);
}

static void clock_restore(void) {
    if (!_fat_mounted) return;
    fat_file_t f;
    if (fat_open(CLOCK_FILE, &f) != FAT_OK) return;
    char buf[40];
    int32_t n = fat_read(&f, (uint8_t *)buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    long long ms = atoll(buf);
    if (ms > 0) sys_set_epoch_ms((int64_t)ms);
}

static void cmd_date(const char *arg) {
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    if (arg && *arg) {
        int y, mo, d, hh = 0, mm = 0, ss = 0;
        int got = sscanf(arg, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss);
        if (got < 3 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) {
            out_str("usage: date YYYY-MM-DD [HH:MM[:SS]]\n");
            return;
        }
        sys_set_epoch_ms(datetime_ymd_to_epoch_ms(y, mo, d, hh, mm, ss));
        clock_persist();
    }
    int y, mo, d, hh, mm, ss;
    datetime_epoch_ms_to_ymd(sys_now_epoch_ms(), &y, &mo, &d, &hh, &mm, &ss);
    out_fmt("%04d-%02d-%02d %02d:%02d:%02d%s\n",
            y, mo, d, hh, mm, ss, sys_rtc_is_set() ? "" : " (unset; uptime only)");
}

/* ------------------------------------------------------------------ */
/* dmesg — kernel-style ring buffer of boot/runtime messages          */
/* ------------------------------------------------------------------ */
#define DMESG_LINES 32
#define DMESG_LINE_SZ 96
static char     _dmesg[DMESG_LINES][DMESG_LINE_SZ];
static uint32_t _dmesg_ts[DMESG_LINES];
static int      _dmesg_head = 0;
static int      _dmesg_count = 0;

static void dmesg_log(const char *msg) {
    if (!msg) return;
    int slot = _dmesg_head;
    _dmesg_ts[slot] = sys_time_ms();
    strncpy(_dmesg[slot], msg, DMESG_LINE_SZ - 1);
    _dmesg[slot][DMESG_LINE_SZ - 1] = '\0';
    _dmesg_head = (_dmesg_head + 1) % DMESG_LINES;
    if (_dmesg_count < DMESG_LINES) _dmesg_count++;
}

static void cmd_dmesg(const char *arg) {
    (void)arg;
    int start = (_dmesg_head - _dmesg_count + DMESG_LINES) % DMESG_LINES;
    for (int i = 0; i < _dmesg_count; i++) {
        int idx = (start + i) % DMESG_LINES;
        out_fmt("[%8lu.%03lu] %s\n",
                (unsigned long)(_dmesg_ts[idx] / 1000),
                (unsigned long)(_dmesg_ts[idx] % 1000),
                _dmesg[idx]);
    }
}

static void cmd_sysinfo(void) {
    out_str("  Mellivora OS v" MELLIVORA_VERSION " for PicoCalc\n");
    out_str("  ====================================\n");
#if defined(PICO_CYW43_SUPPORTED)
    out_str("  Platform: RP2350 + CYW43 (Pico 2W)\n");
    out_str("  RAM:      520 KB SRAM\n");
    out_str("  Flash:    4 MB XIP\n");
    out_str("  WiFi:     CYW43439 802.11n 2.4 GHz\n");
#elif defined(PICO_RP2350A) || defined(PICO_RP2350)
    out_str("  Platform: RP2350 (Pico 2)\n");
    out_str("  RAM:      520 KB SRAM\n");
    out_str("  Flash:    4 MB XIP\n");
    out_str("  WiFi:     none\n");
#else
    out_str("  Platform: RP2040 (Pico)\n");
    out_str("  RAM:      264 KB SRAM\n");
    out_str("  Flash:    2 MB XIP\n");
    out_str("  WiFi:     none\n");
#endif
    out_str("  LCD:      ILI9488 320x320 SPI1\n");
    out_str("  KBD:      STM32 co-proc I2C1\n");
    out_str("  SD:       SPI0 FAT\n");
    out_fmt("  Clock:    %lu MHz\n",
            (unsigned long)(clock_get_hz(clk_sys) / 1000000));
    out_fmt("  SD:       %s\n", _fat_mounted ? "mounted" : "not mounted");
    out_fmt("  Status:   bar %s\n", _statusbar_enabled ? "on" : "off");
    int64_t us = absolute_time_diff_us(g_boot_time, get_absolute_time());
    int64_t sec = us / 1000000LL;
    int64_t days = sec / 86400, hours = (sec % 86400) / 3600;
    int64_t mins = (sec % 3600) / 60, secs = sec % 60;
    if (days > 0)
        out_fmt("  Uptime:   %lldd %02lldh %02lldm %02llds\n", days, hours, mins, secs);
    else
        out_fmt("  Uptime:   %02lldh %02lldm %02llds\n", hours, mins, secs);
    out_str("  Built:    " __DATE__ " " __TIME__ "\n");
}

static void cmd_status(const char *arg) {
    if (arg && !strcmp(arg, "off")) {
        _statusbar_enabled = false;
        /* Clear the status bar row */
        lcd_set_cursor(0, 19);
        for (int i = 0; i < 40; i++)
            lcd_draw_cell(i, 19, ' ', LCD_BLACK, LCD_BLACK);
        out_str("Status bar disabled\n");
    } else if (arg && !strcmp(arg, "on")) {
        _statusbar_enabled = true;
        _draw_status_bar();
        out_str("Status bar enabled\n");
    } else {
        out_fmt("Status bar: %s\n", _statusbar_enabled ? "on" : "off");
        out_str("usage: status [on|off]\n");
    }
}

static void cmd_cursor(const char *arg) {
    const char *s = skip_spaces((char *)arg);
    if (!s || !*s) {
        out_fmt("Cursor: %s %s\n",
                lcd_cursor_enabled() ? "on" : "off",
                lcd_cursor_is_block() ? "block" : "underscore");
        out_str("usage: cursor [on|off|block|underscore]\n");
        return;
    }
    if (strcmp(s, "on") == 0 || strcmp(s, "block") == 0) {
        lcd_cursor_enable(true, true);
        out_str("Cursor: on (block)\n");
    } else if (strcmp(s, "underscore") == 0 || strcmp(s, "under") == 0) {
        lcd_cursor_enable(true, false);
        out_str("Cursor: on (underscore)\n");
    } else if (strcmp(s, "off") == 0) {
        lcd_cursor_enable(false, false);
        out_str("Cursor: off\n");
    } else {
        out_str("usage: cursor [on|off|block|underscore]\n");
    }
}

static void cmd_battery(void) {
    int pct = kbd_battery_percent();
    if (pct < 0) out_str("Battery info unavailable\n");
    else {
        const char *status = kbd_is_charging() ? " [CHARGING]" : "";
        out_fmt("Battery: %d%%%s\n", pct, status);
    }
}

static void cmd_backlight(const char *arg) {
    if (!arg || !*arg) { out_str("usage: backlight <0-255>\n"); return; }
    int val = atoi(arg);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    kbd_set_backlight((uint8_t)val);
    out_fmt("Keyboard backlight set to %d\n", val);
}

/* ---- FAT filesystem commands ---- */

static void cmd_mount(void) {
    fat_result_t r = fat_mount();
    _fat_mounted = (r == FAT_OK);
    out_fmt("mount: %s\n", fat_result_str(r));
    _last_exit = (r == FAT_OK) ? 0 : 1;
}

static void cmd_umount(void) {
    if (!_fat_mounted) { out_str("umount: not mounted\n"); _last_exit = 1; return; }
    _fat_mounted = false;
    out_str("umount: SD card unmounted\n");
}

/* Format a byte count as a human-readable string (e.g. "1.2 MB"). */
static void _fmt_size(uint32_t bytes, char *out, size_t out_sz) {
    if (bytes >= 1024UL * 1024UL) {
        unsigned long w = bytes / (1024UL * 1024UL);
        unsigned long f = (bytes % (1024UL * 1024UL)) * 10UL / (1024UL * 1024UL);
        snprintf(out, out_sz, "%lu.%lu MB", w, f);
    } else if (bytes >= 1024UL) {
        unsigned long w = bytes / 1024UL;
        unsigned long f = (bytes % 1024UL) * 10UL / 1024UL;
        snprintf(out, out_sz, "%lu.%lu KB", w, f);
    } else {
        snprintf(out, out_sz, "%lu B", (unsigned long)bytes);
    }
}

static void _ls_entry_ex(const char *short_name, const char *long_name,
                         uint32_t size, bool is_dir, void *ctx) {
    (void)ctx;
    const char *name = long_name ? long_name : short_name;
    uint32_t saved_fg = lcd_get_fg();
    if (is_dir) {
        lcd_set_fg(ui_color(UI_ACCENT));
        out_fmt("  %s/\n", name);
    } else {
        char sz[16];
        _fmt_size(size, sz, sizeof sz);
        lcd_set_fg(ui_color(UI_MUTED));
        out_fmt("  %8s  ", sz);
        lcd_set_fg(ui_color(UI_TEXT));
        out_fmt("%s\n", name);
    }
    lcd_set_fg(saved_fg);
}

static void cmd_ls(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    char abs[CWD_MAX];
    if (arg && *arg) resolve_abs(arg, abs);
    else             strncpy(abs, _cwd, CWD_MAX - 1);
    abs[CWD_MAX - 1] = '\0';
    fat_result_t r = fat_ls_ex(abs, _ls_entry_ex, NULL);
    if (r != FAT_OK) { out_fmt("ls: %s\n", fat_result_str(r)); _last_exit = 1; }
}

static void cmd_cd(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    char abs[CWD_MAX];
    resolve_abs((arg && *arg) ? arg : "/", abs);

    /* Validate: must be a directory */
    fat_result_t r = fat_is_dir(abs);
    if (r != FAT_OK) { out_fmt("cd: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; return; }

    strncpy(_cwd, abs, CWD_MAX - 1);
    _cwd[CWD_MAX - 1] = '\0';
    strncpy(_sys_cwd, _cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
    cwd_persist();
}

static void cmd_pwd(void) {
    out_fmt("%s\n", _cwd);
}

static void cmd_cat(const char *arg) {
    const char *path = skip_ws(arg ? arg : "");

    if (pipe_input_active() && (!*path || (path[0] == '-' && path[1] == '\0'))) {
        char pipe_buf[64];
        char last = '\n';
        int n;
        while ((n = pipe_input_read(pipe_buf, sizeof pipe_buf)) > 0) {
            for (int i = 0; i < n; i++) {
                out_char(pipe_buf[i]);
                last = pipe_buf[i];
            }
        }
        if (last != '\n') out_char('\n');
        return;
    }
    if (path[0] == '-' && path[1] == '\0') {
        out_str("cat: no pipeline input\n");
        return;
    }

    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    if (!*path) { out_str("usage: cat <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(path, abs);
    fat_file_t f;
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) { out_fmt("cat: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; return; }
    uint8_t buf[64];
    char last = '\n';
    int32_t n;
    while ((n = fat_read(&f, buf, sizeof buf)) > 0) {
        for (int32_t i = 0; i < n; i++) {
            out_char((char)buf[i]);
            last = (char)buf[i];
        }
    }
    /* Report a real I/O error; EOF is normal. */
    if (n < 0 && n != FAT_ERR_EOF) {
        out_fmt("\ncat: %s: %s\n", abs, fat_result_str((fat_result_t)n));
        _last_exit = 1;
    } else if (last != '\n') {
        out_char('\n');
    }
}

static void cmd_echo(const char *arg) {
    if (arg && *arg) out_str(arg);
    out_char('\n');
}

static void cmd_touch(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    if (!arg || !*arg) { out_str("usage: touch <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_result_t r = fat_create(abs, NULL, 0);
    if (r == FAT_ERR_EXISTS) return;
    if (r != FAT_OK) { out_fmt("touch: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; }
}

static void cmd_write(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    bool force = parse_force_flag(&arg);
    if (!arg || !*arg) { out_str("usage: write [-f] <path> <text>\n"); return; }

    char tmp[LINE_BUF];
    strncpy(tmp, arg, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = '\0';

    char *text = tmp;
    while (*text && *text != ' ') text++;
    if (*text == '\0') { out_str("usage: write [-f] <path> <text>\n"); return; }
    *text++ = '\0';
    while (*text == ' ') text++;

    char abs[CWD_MAX];
    resolve_abs(tmp, abs);
    if (!force) {
        /* Only prompt if the file exists — touching new files needs no warning. */
        fat_file_t probe;
        if (fat_open(abs, &probe) == FAT_OK) {
            char prompt[CWD_MAX + 24];
            snprintf(prompt, sizeof prompt, "write: overwrite %s?", abs);
            if (!confirm(prompt)) { out_str("write: cancelled\n"); return; }
        }
    }
    fat_unlink(abs);
    fat_result_t r = fat_create(abs, (const uint8_t *)text, (uint32_t)strlen(text));
    if (r != FAT_OK) { out_fmt("write: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; }
}

static void cmd_mkdir(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    if (!arg || !*arg) { out_str("usage: mkdir <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_result_t r = fat_mkdir(abs);
    if (r != FAT_OK) { out_fmt("mkdir: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; }
}

/* Recursive directory removal — uses a 32-entry batch buffer to avoid
 * re-entering _sector_buf during fat_ls callbacks. */
#define RM_BATCH_MAX 32
typedef struct {
    char name[RM_BATCH_MAX][13];
    bool is_dir[RM_BATCH_MAX];
    int n;
    bool overflow;
} _rm_batch_t;

static void _rm_collect_cb(const char *name, uint32_t size, bool is_dir, void *ctx) {
    (void)size;
    _rm_batch_t *b = (_rm_batch_t *)ctx;
    if (_interrupted) return;
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, "..")) return;
    if (b->n >= RM_BATCH_MAX) { b->overflow = true; return; }
    strncpy(b->name[b->n], name, 12); b->name[b->n][12] = '\0';
    b->is_dir[b->n] = is_dir;
    b->n++;
}

static void rm_recurse(const char *path) {
    if (_interrupted) return;
    watchdog_update();
    /* Loop in case directory has more than RM_BATCH_MAX entries */
    for (;;) {
        _rm_batch_t b = { .n = 0, .overflow = false };
        if (fat_ls(path, _rm_collect_cb, &b) != FAT_OK) {
            out_fmt("rm: %s: list failed\n", path);
            return;
        }
        if (b.n == 0) break;
        for (int i = 0; i < b.n; i++) {
            if (_interrupted) return;
            char child[CWD_MAX];
            snprintf(child, sizeof child, "%s/%s", path, b.name[i]);
            if (b.is_dir[i]) {
                rm_recurse(child);
            } else {
                fat_result_t r = fat_unlink(child);
                if (r != FAT_OK) out_fmt("rm: %s: %s\n", child, fat_result_str(r));
            }
        }
        if (!b.overflow) break;
    }
    /* Now remove the (now empty) directory itself */
    fat_result_t r = fat_unlink(path);
    if (r != FAT_OK) out_fmt("rmdir: %s: %s\n", path, fat_result_str(r));
}

static void cmd_rm(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    bool force = parse_force_flag(&arg);
    /* Parse optional -r / -R for recursive */
    bool recursive = false;
    while (arg && *arg == ' ') arg++;
    if (arg && (!strncmp(arg, "-r", 2) || !strncmp(arg, "-R", 2)) &&
        (arg[2] == ' ' || arg[2] == '\0')) {
        recursive = true;
        arg += 2;
        while (*arg == ' ') arg++;
    }
    if (!arg || !*arg) { out_str("usage: rm [-rRf] <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    if (!force) {
        char prompt[CWD_MAX + 32];
        snprintf(prompt, sizeof prompt, "rm: delete %s%s?",
                 abs, recursive ? " (recursive)" : "");
        if (!confirm(prompt)) { out_str("rm: cancelled\n"); return; }
    }

    if (recursive && fat_is_dir(abs) == FAT_OK) {
        rm_recurse(abs);
        return;
    }

    fat_result_t r = fat_unlink(abs);
    if (r != FAT_OK) { out_fmt("rm: %s: %s\n", abs, fat_result_str(r)); _last_exit = 1; }
}

static void cmd_rename(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); _last_exit = 1; return; }
    if (!arg || !*arg) { out_str("usage: rename <old> <new>\n"); return; }
    /* Split arg into old and new names */
    char buf[CWD_MAX * 2];
    strncpy(buf, arg, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    char *p = buf;
    while (*p && *p != ' ') p++;
    if (!*p) { out_str("usage: rename <old> <new>\n"); return; }
    *p++ = '\0';
    while (*p == ' ') p++;
    if (!*p) { out_str("usage: rename <old> <new>\n"); return; }
    char abs_old[CWD_MAX], abs_new[CWD_MAX];
    resolve_abs(buf, abs_old);
    resolve_abs(p, abs_new);
    fat_result_t r = fat_rename(abs_old, abs_new);
    if (r != FAT_OK) { out_fmt("rename: %s\n", fat_result_str(r)); _last_exit = 1; }
}

static void cmd_sdinfo(void) {
    sd_result_t r = sd_init();
    out_fmt("SD init: %s\n", sd_result_str(r));
    if (r != SD_OK) {
        _last_exit = 1;
        return;
    }
    fat_usage_t usage;
    fat_result_t fr = fat_get_usage(&usage);
    if (fr == FAT_OK) {
        out_fmt("Filesystem: FAT%u, %lu bytes/sector, %lu sectors/cluster\n",
                usage.fat32 ? 32U : 16U,
                (unsigned long)usage.bytes_per_sector,
                (unsigned long)usage.sectors_per_cluster);
        out_fmt("Capacity: %llu total, %llu used, %llu free bytes\n",
                (unsigned long long)usage.total_bytes,
                (unsigned long long)usage.used_bytes,
                (unsigned long long)usage.free_bytes);
    } else {
        out_fmt("Filesystem usage unavailable: %s (run 'mount' first)\n",
                fat_result_str(fr));
    }
}

static void cmd_sdcheck(void) {
    uint8_t sector[512];
    sd_result_t sr = sd_init();
    if (sr != SD_OK) {
        out_fmt("sdcheck: card initialization failed: %s\n", sd_result_str(sr));
        _last_exit = 1;
        return;
    }
    sr = sd_read_block(0, sector);
    if (sr != SD_OK) {
        out_fmt("sdcheck: sector 0 read failed: %s\n", sd_result_str(sr));
        _last_exit = 1;
        return;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        out_str("sdcheck: sector 0 is readable, but has no boot signature\n");
        _last_exit = 1;
        return;
    }
    out_str("sdcheck: card initialized; sector 0 read and boot signature valid\n");
    if (_fat_mounted) {
        fat_usage_t usage;
        fat_result_t fr = fat_get_usage(&usage);
        if (fr != FAT_OK) {
            out_fmt("sdcheck: mounted filesystem check failed: %s\n",
                    fat_result_str(fr));
            _last_exit = 1;
            return;
        }
        out_str("sdcheck: mounted FAT geometry and allocation scan succeeded\n");
    } else {
        out_str("sdcheck: filesystem not mounted; run 'mount' for FAT checks\n");
    }
}

static void cmd_sdread(const char *arg) {
    if (!arg || !*arg) { out_str("usage: sdread <lba>\n"); return; }
    uint32_t lba = (uint32_t)strtoul(arg, NULL, 0);
    uint8_t buf[512];
    sd_result_t r = sd_init();
    if (r != SD_OK) { out_fmt("SD init failed: %s\n", sd_result_str(r)); return; }
    r = sd_read_block(lba, buf);
    if (r != SD_OK) { out_fmt("SD read failed: %s\n", sd_result_str(r)); return; }
    out_fmt("LBA %lu:\n", (unsigned long)lba);
    for (int row = 0; row < 32; row++) {
        out_fmt("%04X  ", row * 16);
        for (int col = 0; col < 16; col++) out_fmt("%02X ", buf[row * 16 + col]);
        out_str(" |");
        for (int col = 0; col < 16; col++) {
            uint8_t b = buf[row * 16 + col];
            out_char(isprint(b) ? (char)b : '.');
        }
        out_str("|\n");
    }
}

static void cmd_clear(void) {
    lcd_cls(LCD_BLACK);
    /* ANSI clear for serial */
    out_str("\x1b[2J\x1b[H");
}

static void cmd_reboot(void) {
    out_str("Rebooting...\n");
    sleep_ms(100);
    watchdog_reboot(0, 0, 100);
}

/* Flush all persistent state, blank the display, then halt
 * (deepest sleep we can manage without the BIOS coming back). */
static void cmd_shutdown(void) {
    out_str("Shutting down...\n");
    if (_fat_mounted) {
        hist_save();
        alias_save();
        var_save();
        cwd_persist();
        clock_persist();
    }
    sleep_ms(150);
    lcd_cls(LCD_BLACK);
    kbd_set_backlight(0);
    /* Stop tickling the watchdog and __wfe forever. The watchdog will
     * eventually fire and reboot us, but the user has been warned. */
    out_str("Halted. Press the reset button to power on.\n");
    sleep_ms(50);
    for (;;) __wfi();
}

/* Sleep: dim/blank LCD until any key. Watchdog stays alive (we tickle it). */
static void cmd_sleep(void) {
    out_str("Sleeping (press any key)...\n");
    sleep_ms(100);
    uint8_t saved = 255;
    kbd_set_backlight(0);
    lcd_cls(LCD_BLACK);
    for (;;) {
        watchdog_update();
        int c = kbd_getc();
        if (c >= 0) break;
        sleep_ms(50);
    }
    kbd_set_backlight(saved);
    out_str("\x1b[2J\x1b[H");
}

/* Save the current text-mode display contents to a file (default /SCREEN.TXT). */
static void cmd_screenshot(const char *arg) {
    if (!_fat_mounted) { out_str("screenshot: SD not mounted\n"); return; }
    const char *fname = (arg && *arg) ? arg : "/SCREEN.TXT";
    char abs[CWD_MAX];
    resolve_abs(fname, abs);
    /* One row per cell plus a newline, with small slack. */
    char buf[LCD_ROWS * (LCD_COLS + 1) + 16];
    size_t pos = 0;
    for (int r = 0; r < LCD_ROWS && pos + LCD_COLS + 1 < sizeof buf; r++) {
        for (int c = 0; c < LCD_COLS; c++) {
            char ch = lcd_get_cell(c, r);
            if (ch < 0x20 || ch > 0x7E) ch = ' ';
            buf[pos++] = ch;
        }
        /* trim trailing spaces on the line for compactness */
        while (pos > 0 && buf[pos - 1] == ' ') pos--;
        buf[pos++] = '\n';
    }
    fat_result_t r = fat_create(abs, (const uint8_t *)buf, (uint32_t)pos);
    if (r != FAT_OK) out_fmt("screenshot: %s: %s\n", abs, fat_result_str(r));
    else            out_fmt("screenshot: wrote %s (%u bytes)\n",
                            abs, (unsigned)pos);
}

/* ------------------------------------------------------------------ */
/* Command dispatch                                                     */
/* ------------------------------------------------------------------ */

/* Split "cmd arg" into cmd and (optional) arg in-place.
 * Respects double-quoted strings so 'write "MY FILE" text' works. */
static char *split_arg(char *line) {
    char *p = line;
    /* Skip command word (or quoted first token) */
    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ') p++;
    }
    if (*p == ' ') { *p++ = '\0'; return p; }
    return NULL;
}

/* ---- Output redirection: capture out_str/out_fmt into a buffer ---- */

static char  *_redir_buf   = NULL;
static size_t _redir_pos   = 0;
static size_t _redir_cap   = 0;
static bool   _redir_active = false;

static void redir_char(char c) {
    if (_redir_active && _redir_buf && _redir_pos + 1 < _redir_cap) {
        _redir_buf[_redir_pos++] = c;
        _redir_buf[_redir_pos] = '\0';
    }
}

bool sys_output_capture_char(char c) {
    if (_redir_active) {
        redir_char(c);
        return true;
    }
    if (pipe_capture_active()) {
        pipe_capture_char(c);
        return true;
    }
    return false;
}

bool sys_output_captured(void) {
    return _redir_active || pipe_capture_active();
}

/* ---- Tab completion ---- */

/* cmd_meta_t is forward-declared near the top of the file. */

#define SHELL_COMMAND_LIST(X) \
    X("help", "system") X("man", "system") X("history", "shell") X("alias", "shell") \
    X("unalias", "shell") X("read", "shell") X("which", "shell") X("type", "shell") \
    X("expr", "shell") X("uname", "system") X("uptime", "system") X("clear", "system") \
    X("battery", "system") X("backlight", "system") X("mount", "fs") X("ls", "fs") \
    X("dir", "fs") X("cd", "fs") X("pwd", "fs") X("cat", "fs") X("echo", "system") \
    X("touch", "fs") X("write", "fs") X("mkdir", "fs") X("rm", "fs") X("rename", "fs") \
    X("sdinfo", "fs") X("sdread", "fs") X("sdcheck", "fs") X("backup", "fs") \
    X("reboot", "system") X("date", "time") \
    X("dmesg", "system") X("hello", "apps") X("basename", "apps") X("dirname", "apps") \
    X("seq", "apps") X("head", "apps") X("tail", "apps") X("wc", "apps") X("cut", "apps") \
    X("grep", "apps") X("tr", "apps") X("sed", "apps") X("md", "apps") X("markdown", "apps") \
    X("find", "apps") X("tree", "apps") X("du", "apps") X("df", "apps") \
    X("disk", "apps") X("space", "apps") X("pager", "apps") X("more", "apps") X("rev", "apps") \
    X("sort", "apps") X("hexdump", "apps") X("od", "apps") X("hexedit", "apps") X("hex", "apps") \
    X("calc", "apps") X("cp", "apps") X("mv", "apps") X("stat", "apps") X("edit", "apps") \
    X("bedit", "apps") X("browse", "apps") X("files", "apps") X("notes", "apps") X("memo", "apps") \
    X("journal", "apps") X("diary", "apps") X("habits", "apps") X("habit", "apps") \
    X("bookmarks", "apps") X("bookmark", "apps") X("favs", "apps") X("favorites", "apps") \
    X("tasks-export", "apps") X("journal-export", "apps") X("habits-export", "apps") \
    X("games", "apps") X("game", "apps") X("dice", "apps") X("coin", "apps") X("guess", "apps") \
    X("snake", "apps") X("sprite", "apps") X("terminal", "apps") X("term", "apps") X("tty", "apps") \
    X("serial", "apps") X("home", "apps") X("launcher", "apps") X("dashboard", "apps") \
    X("status", "apps") X("sysmon", "apps") X("monitor", "apps") X("settings", "apps") \
    X("set", "shell") X("todo", "apps") X("tasks", "apps") X("planner", "apps") X("agenda", "apps") \
    X("plan", "apps") X("samples", "apps") X("demos", "apps") X("clock", "time") X("cal", "time") \
    X("calendar", "time") X("basic", "apps") X("tcc", "apps") X("tinyc", "apps") X("script", "shell") \
    X("paint", "apps") X("sleep", "system") X("id", "system") X("true", "system") X("false", "system") \
    X("shutdown", "system") X("halt", "system") X("poweroff", "system") X("version", "system") \
    X("stopwatch", "apps") X("timer", "apps") X("pomodoro", "apps") X("screenshot", "system") \
    X("unset", "shell") X("watch", "apps") X("diff", "apps") X("env", "shell") X("lock", "apps") \
    X("xxd", "apps") X("strings", "apps") X("yes", "apps") X("tee", "apps") X("life", "apps") \
    X("tetris", "apps") X("mandelbrot", "apps") X("fractal", "apps") X("piano", "apps") \
    X("forth", "apps") X("xmodem", "apps") X("theme", "apps") X("sysinfo", "system") \
    X("cursor", "system") \
    X("less", "apps") X("uniq", "apps") X("umount", "fs") X("unmount", "fs") \
    X("time", "shell") X("repeat", "shell") X("printf", "shell") \
    X("base64", "apps") X("crc32", "apps") X("units", "apps") X("pass", "apps") \
    X("password", "apps") X("minesweeper", "apps") X("mines", "apps") \
    X("2048", "apps")

#ifdef PICO_CYW43_SUPPORTED
#define NET_COMMAND_LIST(X) \
    X("wifi", "net") X("ping", "net") X("ifconfig", "net") X("ntp", "net") X("dns", "net") \
    X("fetch", "net") X("wget", "net") X("weather", "net") X("irc", "net") X("netstat", "net") \
    X("telnet", "net")
#else
#define NET_COMMAND_LIST(X)
#endif

#define CMD_META_ENTRY(_name, _group) { _name, _group },

static const cmd_meta_t _cmd_registry[] = {
    SHELL_COMMAND_LIST(CMD_META_ENTRY)
    NET_COMMAND_LIST(CMD_META_ENTRY)
    { NULL, NULL }
};

#undef CMD_META_ENTRY
#undef NET_COMMAND_LIST
#undef SHELL_COMMAND_LIST

/* Return a simple Hamming-style edit distance (substitutions only) up to 3.
   Used for "did you mean?" suggestions. */
static int _cmd_edit_dist(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0 || lb == 0) return (int)(la + lb);
    if (la > lb + 2 || lb > la + 2) return 4; /* length gap too large */
    /* simple substitution count on min-length prefix */
    int diff = (int)(la > lb ? la - lb : lb - la);
    size_t min_len = la < lb ? la : lb;
    for (size_t i = 0; i < min_len; i++)
        if (a[i] != b[i]) diff++;
    return diff;
}

/* Print up to 3 "did you mean?" suggestions for an unknown command. */
static void _cmd_suggest(const char *unknown) {
    const char *best[3] = {NULL, NULL, NULL};
    int bdist[3] = {4, 4, 4};
    size_t ulen = strlen(unknown);

    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        /* Skip very short strings that match everything */
        if (strlen(c->name) < 2) continue;
        /* Prefix match: unknown starts with the command or vice versa */
        size_t clen = strlen(c->name);
        int dist;
        if (ulen >= 2 && strncmp(c->name, unknown, ulen < clen ? ulen : clen) == 0)
            dist = (int)(ulen > clen ? ulen - clen : clen - ulen);
        else
            dist = _cmd_edit_dist(unknown, c->name);
        if (dist < 3) {
            /* Insert into best[3] sorted by dist */
            for (int i = 0; i < 3; i++) {
                if (dist < bdist[i]) {
                    /* Shift down */
                    for (int j = 2; j > i; j--) { best[j] = best[j-1]; bdist[j] = bdist[j-1]; }
                    best[i] = c->name; bdist[i] = dist;
                    break;
                }
            }
        }
    }

    if (!best[0]) return;
    out_str("  Similar: ");
    for (int i = 0; i < 3 && best[i]; i++) {
        if (i) out_str(", ");
        out_str(best[i]);
    }
    out_char('\n');
}


typedef struct {
    const char *prefix;
    size_t plen;
    char   matches[16][13]; /* up to 16 FAT 8.3 names */
    bool   is_dir[16];
    int    count;
} _tab_file_ctx_t;

static void _tab_file_cb(const char *name, uint32_t size, bool is_dir, void *ctx) {
    (void)size;
    _tab_file_ctx_t *fc = (_tab_file_ctx_t *)ctx;
    if (fc->count >= 16) return;
    if (fc->plen == 0 || strncmp(name, fc->prefix, fc->plen) == 0) {
        strncpy(fc->matches[fc->count], name, 12);
        fc->matches[fc->count][12] = '\0';
        fc->is_dir[fc->count] = is_dir;
        fc->count++;
    }
}

typedef struct {
    bool active;
    char base[LINE_BUF];
    char last[LINE_BUF];
    char matches[16][13];
    bool is_dir[16];
    int  count;
    int  index;
} _tab_cycle_t;

static _tab_cycle_t _tab_cycle;

static int _tab_apply(char *line, int *idx) {
    for (int i = 0; i < *idx; i++) out_str("\b \b");

    char completed[LINE_BUF];
    size_t pos = 0;
    for (const char *q = _tab_cycle.base; *q && pos < LINE_BUF - 2; q++)
        completed[pos++] = *q;
    for (const char *q = _tab_cycle.matches[_tab_cycle.index];
         *q && pos < LINE_BUF - 2; q++)
        completed[pos++] = (char)tolower((unsigned char)*q);
    if (pos < LINE_BUF - 1)
        completed[pos++] = _tab_cycle.is_dir[_tab_cycle.index] ? '/' : ' ';
    completed[pos] = '\0';

    memcpy(line, completed, pos + 1);
    *idx = (int)pos;
    out_str(line);
    copy_cstr(_tab_cycle.last, sizeof _tab_cycle.last, line);
    return *idx;
}

static int tab_complete(char *line, int idx) {
    if (idx == 0) return idx;

    /* Tab again on the completed line advances to the next candidate. */
    if (_tab_cycle.active && _tab_cycle.count > 1 &&
        strcmp(_tab_cycle.last, line) == 0) {
        _tab_cycle.index = (_tab_cycle.index + 1) % _tab_cycle.count;
        return _tab_apply(line, &idx);
    }
    _tab_cycle.active = false;

    /* If there's a space, try file path completion on the last token */
    char *space = strchr(line, ' ');
    if (space) {
        /* File path completion */
        if (!_fat_mounted) return idx;
        char *token = space + 1;
        while (*token == ' ') token++;
        if (!*token) return idx;

        /* Determine directory to list and prefix to match */
        char dir[CWD_MAX], prefix_name[64];
        const char *last_slash = NULL;
        for (const char *p = token; *p; p++) {
            if (*p == '/') last_slash = p;
        }

        if (last_slash) {
            size_t dlen = (size_t)(last_slash - token + 1);
            if (dlen >= sizeof dir) return idx;
            memcpy(dir, token, dlen);
            dir[dlen] = '\0';
            strncpy(prefix_name, last_slash + 1, sizeof prefix_name - 1);
            prefix_name[sizeof prefix_name - 1] = '\0';
        } else {
            strncpy(dir, _cwd, sizeof dir - 1);
            dir[sizeof dir - 1] = '\0';
            strncpy(prefix_name, token, sizeof prefix_name - 1);
            prefix_name[sizeof prefix_name - 1] = '\0';
        }

        /* Resolve to absolute path */
        char abs_dir[CWD_MAX];
        if (dir[0] == '/') {
            strncpy(abs_dir, dir, sizeof abs_dir - 1);
            abs_dir[sizeof abs_dir - 1] = '\0';
        } else {
            resolve_abs(dir, abs_dir);
        }

        /* Upper-case prefix for FAT name matching */
        char upper_prefix[64];
        size_t plen = strlen(prefix_name);
        for (size_t i = 0; i < plen && i < sizeof upper_prefix - 1; i++) {
            upper_prefix[i] = (char)toupper((unsigned char)prefix_name[i]);
        }
        upper_prefix[plen] = '\0';

        _tab_file_ctx_t fc;
        fc.prefix = upper_prefix;
        fc.plen = plen;
        fc.count = 0;
        fat_ls(abs_dir, _tab_file_cb, &fc);
        if (fc.count <= 0) return idx;

        /* Build the completion base: command text plus any directory part. */
        size_t cmd_len = (size_t)(token - line);
        size_t pos = 0;
        if (cmd_len >= sizeof _tab_cycle.base) return idx;
        memcpy(_tab_cycle.base, line, cmd_len);
        pos = cmd_len;
        if (last_slash) {
            size_t dlen = (size_t)(last_slash - token + 1);
            if (pos + dlen >= sizeof _tab_cycle.base) return idx;
            memcpy(_tab_cycle.base + pos, token, dlen);
            pos += dlen;
        }
        _tab_cycle.base[pos] = '\0';
        _tab_cycle.count = fc.count;
        _tab_cycle.index = 0;
        for (int i = 0; i < fc.count; i++) {
            copy_cstr(_tab_cycle.matches[i], sizeof _tab_cycle.matches[i],
                      fc.matches[i]);
            _tab_cycle.is_dir[i] = fc.is_dir[i];
        }

        /* List the candidate set once, four per row, in muted text. */
        if (fc.count > 1) {
            out_char('\n');
            uint32_t saved = lcd_get_fg();
            lcd_set_fg(ui_color(UI_MUTED));
            for (int i = 0; i < fc.count; i++) {
                for (const char *q = fc.matches[i]; *q; q++)
                    out_char((char)tolower((unsigned char)*q));
                if (fc.is_dir[i]) out_char('/');
                out_str((i % 4 == 3) ? "\n" : "  ");
            }
            if (fc.count % 4 != 0) out_char('\n');
            lcd_set_fg(saved);
        }

        _tab_cycle.active = !fc.is_dir[0] && fc.count > 1;
        return _tab_apply(line, &idx);
        return idx;
    }

    /* Command name completion (original behavior) */
    const char *prefix = line;
    size_t plen = (size_t)idx;
    const char *match = NULL;
    int match_count = 0;

    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        if (strncmp(c->name, prefix, plen) == 0) {
            match = c->name;
            match_count++;
        }
    }

    if (match_count == 1) {
        size_t mlen = strlen(match);
        if (mlen + 1 < LINE_BUF) {
            for (int i = 0; i < idx; i++) out_str("\b \b");
            memcpy(line, match, mlen);
            line[mlen] = ' ';
            line[mlen + 1] = '\0';
            idx = (int)(mlen + 1);
            out_str(line);
        }
    } else if (match_count > 1) {
        out_char('\n');
        for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
            if (strncmp(c->name, prefix, plen) == 0) {
                out_str(c->name);
                out_str("  ");
            }
        }
        out_char('\n');
        out_prompt();
        out_str(line);
    }
    return idx;
}

static void cmd_time(const char *arg) {
    if (!arg || !*arg) { out_str("usage: time COMMAND\n"); return; }
    char tmp[LINE_BUF];
    strncpy(tmp, arg, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = '\0';
    uint32_t t0 = sys_time_ms();
    dispatch(tmp);
    uint32_t elapsed = sys_time_ms() - t0;
    out_fmt("real\t%lu.%03lu s\n", (unsigned long)(elapsed / 1000UL),
            (unsigned long)(elapsed % 1000UL));
}

static void cmd_repeat(const char *arg) {
    const char *s = skip_ws(arg);
    char count_str[16];
    s = next_token(s, count_str, sizeof count_str);
    if (!*count_str || !s || !*s) { out_str("usage: repeat N COMMAND\n"); return; }
    long n = strtol(count_str, NULL, 10);
    if (n < 1 || n > 1000) { out_str("repeat: N must be 1-1000\n"); return; }
    for (long i = 0; i < n; i++) {
        if (_interrupted) break;
        watchdog_update();
        char tmp[LINE_BUF];
        strncpy(tmp, s, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
        dispatch(tmp);
    }
}

static void cmd_printf(const char *arg) {
    if (!arg || !*arg) { out_str("usage: printf FORMAT [ARGS...]\n"); return; }
    /* Extract format token */
    char fmt[LINE_BUF];
    const char *rest = next_token(arg, fmt, sizeof fmt);
    /* Remove optional surrounding quotes */
    size_t flen = strlen(fmt);
    if (flen >= 2 && fmt[0] == '"' && fmt[flen - 1] == '"') {
        memmove(fmt, fmt + 1, flen - 2);
        fmt[flen - 2] = '\0';
    }
    /* Collect remaining tokens as arguments */
    char argv_buf[8][64];
    int argc = 0;
    while (rest && *rest && argc < 8) {
        rest = next_token(rest, argv_buf[argc], sizeof argv_buf[0]);
        if (*argv_buf[argc]) argc++;
    }
    int argi = 0;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] == '\\') {
            i++;
            switch (fmt[i]) {
                case 'n': out_char('\n'); break;
                case 't': out_char('\t'); break;
                case '\\': out_char('\\'); break;
                default: out_char('\\'); out_char(fmt[i]); break;
            }
        } else if (fmt[i] == '%' && fmt[i + 1]) {
            i++;
            char spec = fmt[i];
            const char *a = "";
            if (argi < argc && argi >= 0) {
                a = argv_buf[argi];  // NOLINT(clang-analyzer-security.ArrayBound)
                argi++;
            }
            if (spec == 'd' || spec == 'i') {
                out_fmt("%ld", strtol(a, NULL, 10));
            } else if (spec == 'u') {
                out_fmt("%lu", strtoul(a, NULL, 10));
            } else if (spec == 'x') {
                out_fmt("%lx", strtoul(a, NULL, 0));
            } else if (spec == 'X') {
                out_fmt("%lX", strtoul(a, NULL, 0));
            } else if (spec == 's') {
                out_str(a);
            } else if (spec == '%') {
                out_char('%');
                argi--;  /* no arg consumed */
            } else {
                out_char('%'); out_char(spec);
            }
        } else {
            out_char(fmt[i]);
        }
    }
}

static void dispatch(char *line) {
    dispatch_ex(line, true);
}

static void dispatch_ex(char *line, bool record_history) {
    /* Strip trailing whitespace */
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }

    if (len == 0) return;

    if (record_history && history_expand_bang(line, LINE_BUF)) {
        len = (int)strlen(line);
        if (len == 0) return;
    }

    (void)alias_expand_safe(line, LINE_BUF);
    var_expand(line, LINE_BUF);

    if (record_history) {
        hist_push(line);
        hist_save();
    }

    /* Split on ';', '&&', '||' outside quotes. */
    char *segments[SHELL_MAX_CHAIN];
    unsigned char sep_kind[SHELL_MAX_CHAIN];
    int nseg = shell_split_chain(line, segments, sep_kind, SHELL_MAX_CHAIN);

    for (int i = 0; i < nseg && !_interrupted; i++) {
        if (i > 0) {
            if (sep_kind[i] == SHELL_SEP_AND && _last_exit != 0) continue;
            if (sep_kind[i] == SHELL_SEP_OR && _last_exit == 0) continue;
        }
        char *seg = skip_spaces(segments[i]);
        trim_right(seg);
        if (*seg) dispatch_single(seg);
    }

    /* Scripts keep the interrupt flag so their line loop can stop; the
       interactive shell clears it for the next prompt. */
    if (record_history) _interrupted = false;
}


bool shell_run_command(const char *cmdline) {
    if (!cmdline || !*cmdline) return true;
    char buf[LINE_BUF];
    copy_cstr(buf, sizeof buf, cmdline);
    dispatch_ex(buf, false);
    return _last_exit == 0;
}

/* ------------------------------------------------------------------ */
/* Wildcard expansion                                                   */
/* ------------------------------------------------------------------ */

#define GLOB_MAX_MATCHES 16
#define GLOB_NAME_MAX    (LFN_MAX_CHARS + 1)

typedef struct {
    const char *pattern;
    char names[GLOB_MAX_MATCHES][GLOB_NAME_MAX];
    int count;
    bool overflow;
} glob_ctx_t;

/* Static: 16 long-name slots would be a large stack frame. */
static glob_ctx_t g_glob;

static void glob_collect_cb(const char *short_name, const char *long_name,
                            uint32_t size, bool is_dir, void *ctx) {
    (void)size;
    (void)is_dir;
    glob_ctx_t *g = (glob_ctx_t *)ctx;
    const char *name = (long_name && *long_name) ? long_name : short_name;
    if (!name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    if (!glob_match(g->pattern, name, true)) return;
    if (g->count >= GLOB_MAX_MATCHES) {
        g->overflow = true;
        return;
    }
    copy_cstr(g->names[g->count], sizeof g->names[0], name);
    g->count++;
}

/* Quote a matched name so spaces/quotes survive tokenization. */
static void glob_quote(const char *src, char *dst, size_t cap) {
    bool need = false;
    for (const char *p = src; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'' || *p == '\\') {
            need = true;
            break;
        }
    }
    if (!need) {
        copy_cstr(dst, cap, src);
        return;
    }

    size_t o = 0;
    if (o + 1 < cap) dst[o++] = '"';
    for (const char *p = src; *p && o + 2 < cap; p++) {
        if (*p == '"' || *p == '\\') dst[o++] = '\\';
        dst[o++] = *p;
    }
    if (o + 1 < cap) dst[o++] = '"';
    dst[o] = '\0';
}

static int glob_name_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/*
 * Expand the first unquoted wildcard argument. A single match is rewritten
 * in place and normal dispatch continues. Multiple matches run the command
 * once per match; a plain ">" becomes ">>" after the first run so combined
 * output is preserved. Returns true when the original line was consumed.
 */
static bool dispatch_globbed(char *line) {
    size_t start = 0, len = 0;
    if (!_fat_mounted || !shell_find_glob_arg(line, &start, &len)) return false;

    char token[LINE_BUF];
    if (len >= sizeof token) return false;
    memcpy(token, line + start, len);
    token[len] = '\0';

    const char *slash = strrchr(token, '/');
    const char *base = slash ? slash + 1 : token;
    size_t dir_prefix = slash ? (size_t)(slash - token + 1) : 0;

    /* Only the basename may contain wildcards. */
    for (const char *p = token; slash && p < slash; p++) {
        if (*p == '*' || *p == '?' || *p == '[') return false;
    }

    char dir[LINE_BUF];
    if (slash) {
        size_t dlen = (size_t)(slash - token);
        if (dlen == 0) {
            copy_cstr(dir, sizeof dir, "/");
        } else {
            if (dlen >= sizeof dir) return false;
            memcpy(dir, token, dlen);
            dir[dlen] = '\0';
        }
    } else {
        copy_cstr(dir, sizeof dir, ".");
    }

    char absdir[CWD_MAX];
    resolve_abs(dir, absdir);

    glob_ctx_t *g = &g_glob;
    g->pattern = base;
    g->count = 0;
    g->overflow = false;
    if (fat_ls_ex(absdir, glob_collect_cb, g) != FAT_OK || g->count == 0) return false;

    qsort(g->names, (size_t)g->count, sizeof g->names[0], glob_name_cmp);

    size_t suffix_off = start + len;

    if (g->count == 1) {
        char quoted[GLOB_NAME_MAX * 2];
        char rebuilt[LINE_BUF + GLOB_NAME_MAX * 2];
        glob_quote(g->names[0], quoted, sizeof quoted);
        int n = snprintf(rebuilt, sizeof rebuilt, "%.*s%.*s%s%s", (int)start, line,
                         (int)dir_prefix, token, quoted, line + suffix_off);
        if (n < 0 || n >= (int)sizeof rebuilt) return false;
        dispatch_single_no_glob(rebuilt);
        return true;
    }

    if (g->overflow) out_str("glob: showing first 16 matches\n");

    for (int i = 0; i < g->count; i++) {
        char quoted[GLOB_NAME_MAX * 2];
        char rebuilt[LINE_BUF + GLOB_NAME_MAX * 2];
        glob_quote(g->names[i], quoted, sizeof quoted);
        int n = snprintf(rebuilt, sizeof rebuilt, "%.*s%.*s%s%s", (int)start, line,
                         (int)dir_prefix, token, quoted, line + suffix_off);
        if (n < 0 || n >= (int)sizeof rebuilt) continue;

        if (i > 0) {
            char *gt = strchr(rebuilt, '>');
            if (gt && gt[1] != '>') {
                size_t used = strlen(rebuilt);
                size_t tail = used - (size_t)(gt - rebuilt);
                if (used + 1 < sizeof rebuilt) {
                    memmove(gt + 2, gt + 1, tail + 1);
                    gt[1] = '>';
                }
            }
        }

        dispatch_single_no_glob(rebuilt);
        if (_interrupted) break;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Scripts                                                              */
/* ------------------------------------------------------------------ */

#define SCRIPT_MAX_DEPTH 4
static int _script_depth = 0;

static bool script_exec_line(char *line) {
    trim_right(line);
    char *s = skip_spaces(line);
    if (!*s || *s == '#') return true;
    dispatch_ex(line, false);
    return _last_exit == 0;
}

bool shell_run_script(const char *path, bool quiet) {
    if (!path || !*path) return false;
    if (!_fat_mounted) {
        if (!quiet) out_str("script: SD card not mounted\n");
        return false;
    }
    if (_script_depth >= SCRIPT_MAX_DEPTH) {
        if (!quiet) out_str("script: nesting too deep\n");
        return false;
    }

    char abs[APP_TOKEN_MAX];
    resolve_abs(path, abs);

    fat_file_t f;
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) {
        if (!quiet) out_fmt("script: cannot open %s (%s)\n", abs, fat_result_str(r));
        return false;
    }

    _script_depth++;
    char line_buf[LINE_BUF];
    uint8_t chunk[512];
    size_t n = 0;
    bool ok = true;
    bool warned = false;
    int line_no = 1;
    int32_t got;

    while ((got = fat_read(&f, chunk, sizeof chunk)) > 0) {
        for (int32_t i = 0; i < got; i++) {
            char ch = (char)chunk[i];
            if (ch == '\n') {
                line_buf[n] = '\0';
                if (!script_exec_line(line_buf)) ok = false;
                n = 0;
                line_no++;
            } else if (ch != '\r') {
                if (n + 1 < sizeof line_buf) {
                    line_buf[n++] = ch;
                } else if (!warned && !quiet) {
                    out_fmt("script: line %d exceeds %d characters\n",
                            line_no, LINE_BUF - 1);
                    warned = true;
                }
            }
            if (_interrupted) break;
        }
        if (_interrupted) break;
    }

    if (got < 0 && got != FAT_ERR_EOF) {
        if (!quiet) out_str("script: read error\n");
        ok = false;
    }

    if (n > 0 && !_interrupted) {
        line_buf[n] = '\0';
        if (!script_exec_line(line_buf)) ok = false;
    }

    _script_depth--;
    _interrupted = false;
    return ok;
}

static void cmd_script(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");

    shell_argv_t av;
    if (shell_tokenize(tmp, &av) < 1 || !av.argv[0][0]) {
        out_str("usage: script FILE\n");
        _last_exit = 1;
        return;
    }

    if (!shell_run_script(av.argv[0], false)) _last_exit = 1;
}

/* ------------------------------------------------------------------ */
/* read / which / expr                                                  */
/* ------------------------------------------------------------------ */

static bool valid_var_name(const char *name) {
    if (!name || !*name) return false;
    if (strlen(name) >= ALIAS_NAME_SZ) return false;
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return false;
    for (const char *p = name + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') return false;
    }
    return true;
}

static void cmd_read(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");

    shell_argv_t av;
    int argc = shell_tokenize(tmp, &av);

    const char *prompt = NULL;
    int first = 0;
    if (argc >= 2 && strcmp(av.argv[0], "-p") == 0) {
        prompt = av.argv[1];
        first = 2;
    }
    if (argc <= first) {
        out_str("usage: read [-p PROMPT] NAME...\n");
        _last_exit = 1;
        return;
    }

    char line_buf[LINE_BUF];
    if (app_read_line(prompt ? prompt : "", line_buf, sizeof line_buf) < 0) {
        _last_exit = 1;
        return;
    }
    trim_right(line_buf);

    int nvars = argc - first;
    if (nvars == 1) {
        const char *name = av.argv[first];
        if (!valid_var_name(name) || !var_set(name, line_buf)) {
            out_str("read: invalid variable name\n");
            _last_exit = 1;
            return;
        }
        _last_exit = 0;
        return;
    }

    shell_argv_t vals;
    int nvals = shell_tokenize(line_buf, &vals);

    for (int i = 0; i < nvars; i++) {
        const char *name = av.argv[first + i];
        char value[LINE_BUF];
        value[0] = '\0';

        if (i < nvals) {
            if (i == nvars - 1) {
                size_t pos = 0;
                for (int k = i; k < nvals; k++) {
                    if (k > i && pos + 1 < sizeof value) value[pos++] = ' ';
                    size_t l = strlen(vals.argv[k]);
                    if (pos + l >= sizeof value) l = sizeof value - 1 - pos;
                    memcpy(value + pos, vals.argv[k], l);
                    pos += l;
                }
                value[pos] = '\0';
            } else {
                copy_cstr(value, sizeof value, vals.argv[i]);
            }
        }

        if (!valid_var_name(name) || !var_set(name, value)) {
            out_str("read: invalid variable name\n");
            _last_exit = 1;
            return;
        }
    }
    _last_exit = 0;
}

static const cmd_meta_t *cmd_find(const char *name) {
    for (const cmd_meta_t *c = _cmd_registry; c->name; c++) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

static void cmd_which(const char *arg) {
    char tmp[LINE_BUF];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");

    shell_argv_t av;
    int argc = shell_tokenize(tmp, &av);
    if (argc == 0) {
        out_str("usage: which COMMAND...\n");
        _last_exit = 1;
        return;
    }

    bool missing = false;
    for (int i = 0; i < argc; i++) {
        const char *name = av.argv[i];

        int slot = alias_find_slot(name);
        if (slot >= 0) {
            out_fmt("%s: aliased to %s\n", name, _aliases[slot].value);
            continue;
        }

        const cmd_meta_t *c = cmd_find(name);
        if (c) {
            out_fmt("%s: %s command\n", name, c->group);
            continue;
        }

        if (_fat_mounted) {
            char abs[APP_TOKEN_MAX];
            resolve_abs(name, abs);
            fat_file_t f;
            if (fat_open(abs, &f) == FAT_OK) {
                out_fmt("%s: %s\n", name, abs);
                continue;
            }
        }

        out_fmt("%s: not found\n", name);
        missing = true;
    }
    _last_exit = missing ? 1 : 0;
}

static void cmd_expr(const char *arg) {
    const char *s = skip_ws(arg ? arg : "");
    if (!*s) {
        out_str("usage: expr EXPR\n");
        _last_exit = 1;
        return;
    }

    bool ok = false;
    int value = expr_eval(s, NULL, NULL, &ok);
    if (!ok) {
        out_str("expr: bad expression\n");
        _last_exit = 1;
        return;
    }
    out_fmt("%d\n", value);
}

#define PIPE_BUF_MAX 4096
static char _pipe_buf[2][PIPE_BUF_MAX];

/*
 * Execute a pipeline. Intermediate stages exchange data through two bounded
 * RAM buffers. `cmd | more` / `cmd | pager` keep the historical streaming
 * pager path so large outputs are not limited by the buffer.
 */
static bool run_pipeline(char *line) {
    char *stages[SHELL_MAX_PIPE];
    int n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    if (n <= 1) return false;

    for (int i = 0; i < n; i++) {
        stages[i] = skip_spaces(stages[i]);
        trim_right(stages[i]);
    }

    if (!*stages[0]) {
        out_str("pipe: empty command\n");
        _last_exit = 1;
        return true;
    }

    char more_cmd[] = "more";
    if (n == 2) {
        if (!*stages[1]) stages[1] = more_cmd;
        if (strcmp(stages[1], "more") == 0 || strcmp(stages[1], "pager") == 0) {
            sys_more_set(true);
            if (!dispatch_globbed(stages[0])) dispatch_single_no_glob(stages[0]);
            bool aborted = _sys_more_abort;
            sys_more_set(false);
            if (aborted) out_char('\n');
            return true;
        }
    }

    char *prev = NULL;
    size_t prev_len = 0;

    for (int i = 0; i < n; i++) {
        bool last = (i == n - 1);
        if (i > 0) pipe_input_set(prev, prev_len);

        char *cap = NULL;
        if (!last) {
            cap = _pipe_buf[i % 2];
            pipe_capture_begin(cap, PIPE_BUF_MAX);
        }

        if (!dispatch_globbed(stages[i])) dispatch_single_no_glob(stages[i]);

        if (!last) {
            bool overflow = pipe_capture_overflowed();
            prev_len = pipe_capture_end();
            prev = cap;
            if (overflow) out_str("pipe: output truncated\n");
        }

        if (_interrupted) break;
    }

    pipe_input_clear();
    return true;
}

static void dispatch_single(char *line) {
    /* Split pipelines first so globs and redirection apply per stage. */
    if (run_pipeline(line)) return;

    /* Expand the first wildcard argument before redirection parsing.
       Multiple matches run the command once per match. */
    if (dispatch_globbed(line)) return;
    dispatch_single_no_glob(line);
}

static void dispatch_single_no_glob(char *line) {

    /* ---- Parse output redirection (>> or >) ---- */
    char *redir_file = NULL;
    bool  redir_append = false;
    (void)shell_parse_redir(line, &redir_file, &redir_append);

    if (line[0] == '\0') return;

    /* Set up redirection capture buffer if needed */
    static char redir_static_buf[APP_REDIR_MAX];
    bool redirecting = (redir_file && *redir_file && _fat_mounted);
    if (redirecting) {
        _redir_buf = redir_static_buf;
        _redir_cap = sizeof redir_static_buf;
        _redir_pos = 0;
        _redir_buf[0] = '\0';
        _redir_active = true;
    }

    /* Separate command from optional argument */
    char *arg = split_arg(line);

    /* Default to success for the chained-operator (&&, ||) bookkeeping.
       Commands that detect a real error are free to overwrite _last_exit. */
    _last_exit = 0;

    if      (!strcmp(line, "help"))      cmd_help(arg);
    else if (!strcmp(line, "man"))       cmd_man(arg);
    else if (!strcmp(line, ".") || !strcmp(line, "script")) cmd_script(arg);
    else if (!strcmp(line, "read"))      cmd_read(arg);
    else if (!strcmp(line, "which") || !strcmp(line, "type")) cmd_which(arg);
    else if (!strcmp(line, "expr"))      cmd_expr(arg);
    else if (!strcmp(line, "history"))   cmd_history(arg);
    else if (!strcmp(line, "alias"))     cmd_alias(arg);
    else if (!strcmp(line, "unalias"))   cmd_unalias(arg);
    /* `set NAME=VALUE` -> shell var; bare `set` -> falls through to settings app */
    else if (!strcmp(line, "set") && arg && strchr(arg, '=')) cmd_setvar(arg);
    else if (!strcmp(line, "unset"))     cmd_unsetvar(arg);
    else if (!strcmp(line, "uname"))     cmd_uname(arg);
    else if (!strcmp(line, "version"))   cmd_version();
    else if (!strcmp(line, "uptime"))    cmd_uptime();
    else if (!strcmp(line, "date"))      cmd_date(arg);
    else if (!strcmp(line, "dmesg"))     cmd_dmesg(arg);
    else if (!strcmp(line, "sysinfo"))   cmd_sysinfo();
    else if (!strcmp(line, "time"))      cmd_time(arg);
    else if (!strcmp(line, "repeat"))    cmd_repeat(arg);
    else if (!strcmp(line, "printf"))    cmd_printf(arg);
    else if (!strcmp(line, "status"))    cmd_status(arg);
    else if (!strcmp(line, "cursor"))    cmd_cursor(arg);
    else if (!strcmp(line, "clear"))     cmd_clear();
    else if (!strcmp(line, "battery"))   cmd_battery();
    else if (!strcmp(line, "backlight")) cmd_backlight(arg);
    else if (!strcmp(line, "mount"))     cmd_mount();
    else if (!strcmp(line, "umount") ||
             !strcmp(line, "unmount"))   cmd_umount();
    else if (!strcmp(line, "ls"))        cmd_ls(arg);
    else if (!strcmp(line, "dir"))       cmd_ls(arg);
    else if (!strcmp(line, "cd"))        cmd_cd(arg);
    else if (!strcmp(line, "pwd"))       cmd_pwd();
    else if (!strcmp(line, "cat"))       cmd_cat(arg);
    else if (!strcmp(line, "echo"))      cmd_echo(arg);
    else if (!strcmp(line, "touch"))     cmd_touch(arg);
    else if (!strcmp(line, "write"))     cmd_write(arg);
    else if (!strcmp(line, "mkdir"))     cmd_mkdir(arg);
    else if (!strcmp(line, "rm"))        cmd_rm(arg);
    else if (!strcmp(line, "rename"))   cmd_rename(arg);
    else if (!strcmp(line, "sdinfo"))    cmd_sdinfo();
    else if (!strcmp(line, "sdread"))    cmd_sdread(arg);
    else if (!strcmp(line, "sdcheck"))   cmd_sdcheck();
    else if (!strcmp(line, "reboot"))    cmd_reboot();
    else if (!strcmp(line, "shutdown") ||
             !strcmp(line, "halt") ||
             !strcmp(line, "poweroff")) cmd_shutdown();
    else if (!strcmp(line, "sleep") && (!arg || !*arg)) cmd_sleep();
    else if (!strcmp(line, "screenshot")) cmd_screenshot(arg);
#ifdef PICO_CYW43_SUPPORTED
    else if (!strcmp(line, "wifi"))       net_app_wifi(arg);
    else if (!strcmp(line, "ping"))       net_app_ping(arg);
    else if (!strcmp(line, "ifconfig"))   net_app_ifconfig(arg);
    else if (!strcmp(line, "ntp"))        net_app_ntp(arg);
    else if (!strcmp(line, "dns"))        net_app_dns(arg);
    else if (!strcmp(line, "fetch"))      net_app_fetch(arg);
    else if (!strcmp(line, "wget"))       net_app_wget(arg);
    else if (!strcmp(line, "weather"))    net_app_weather(arg);
    else if (!strcmp(line, "irc"))        net_app_irc(arg);
    else if (!strcmp(line, "netstat"))    net_app_netstat(arg);
    else if (!strcmp(line, "telnet"))     net_app_telnet(arg);
#endif
    else if (app_run(line, arg)) {
        strncpy(_cwd, _sys_cwd, CWD_MAX - 1);
        _cwd[CWD_MAX - 1] = '\0';
        _last_exit = 0;
    }
    else {
        uint32_t sf = lcd_get_fg();
        lcd_set_fg(ui_color(UI_ERROR));
        out_fmt("unknown: %s\n", line);
        lcd_set_fg(ui_color(UI_MUTED));
        _cmd_suggest(line);
        out_str("  Type  help  to list all commands.\n");
        lcd_set_fg(sf);
        _last_exit = 127;
    }

    /* Finalize output redirection */
    if (redirecting) {
        _redir_active = false;
        char abs[CWD_MAX];
        resolve_abs(redir_file, abs);
        if (redir_append) {
            fat_append(abs, (const uint8_t *)_redir_buf, (uint32_t)_redir_pos);
        } else {
            fat_write_atomic(abs, (const uint8_t *)_redir_buf, (uint32_t)_redir_pos);
        }
        _redir_buf = NULL;
        _redir_pos = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Input: merged keyboard + USB serial + key repeat                     */
/* ------------------------------------------------------------------ */

/*
 * Returns the next character from either the physical keyboard or USB
 * serial.  Non-blocking; returns -1 if nothing is available.
 * Also checks for key repeat.
 */
static int read_input(void) {
    int ch = getchar_timeout_us(0);
    if (ch != PICO_ERROR_TIMEOUT) return ch;
    ch = kbd_getc();
    if (ch >= 0) return ch;
    return kbd_get_repeat();
}

/*
 * Block on the next printable keystroke from any input source. Used by
 * destructive-command confirmation prompts. Returns the character (lower
 * case for letters); returns 0 on Ctrl-C / Esc.
 */
static int read_input_blocking(void) {
    for (;;) {
        int ch = read_input();
        if (ch < 0) { sleep_ms(20); continue; }
        if (ch == 0x03 || ch == 0x1B) return 0; /* Ctrl-C / Esc */
        if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
        return ch;
    }
}

/* Prompt "msg [y/N] " and return true iff the user typed 'y'. Newline is
 * always echoed after the answer for tidy output. */
static bool confirm(const char *msg) {
    out_str(msg);
    out_str(" [y/N] ");
    int ch = read_input_blocking();
    if (ch >= ' ' && ch < 0x7F) out_char((char)ch);
    out_char('\n');
    return ch == 'y';
}

/* Helper used by rm/write: parse a leading "-f" flag. *arg is advanced
 * past the flag and any following whitespace. Returns true if -f present. */
static bool parse_force_flag(const char **arg) {
    if (!*arg) return false;
    const char *p = *arg;
    while (*p == ' ') p++;
    if (p[0] == '-' && p[1] == 'f' && (p[2] == ' ' || p[2] == '\0')) {
        p += 2;
        while (*p == ' ') p++;
        *arg = p;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Status bar — last LCD row shows battery, cwd, uptime                 */
/* ------------------------------------------------------------------ */
static void _draw_status_bar(void) {
    if (!_lcd_ready || !_statusbar_enabled || _screensaver_active) return;
    if (ui_fullscreen()) return;

    /* Throttle redraws — the bar shows uptime in seconds, so refreshing more
       than once per second is wasted work and visible flicker. The two real
       call sites (per-prompt on RP2040, 2 s tick on RP2350-core1) both stay
       responsive enough at 1 Hz. */
    static uint32_t _last_draw_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (_last_draw_ms != 0 && (now - _last_draw_ms) < 1000) return;
    _last_draw_ms = now;

    /* Read battery state via I2C *before* acquiring the LCD lock.
     * kbd_battery_percent() / kbd_is_charging() call i2c_read_blocking()
     * which can stall if the keyboard MCU is slow to respond.  Holding the
     * LCD lock across that wait causes core0 to deadlock whenever it tries
     * to do any LCD output (e.g. during an ls callback). */
    int bat = kbd_battery_percent();
    bool charging = kbd_is_charging();

    /* Acquire the LCD lock so the entire bar redraw is atomic versus core0
       output (recursive mutex — safe even though the lcd_* helpers below
       also acquire it). */
    lcd_lock();

    /* Save current fg/bg */
    uint32_t save_fg = lcd_get_fg();
    uint32_t save_bg = lcd_get_bg();
    int save_col = lcd_get_col();
    int save_row = lcd_get_row();

    lcd_set_fg(LCD_BLACK);
    lcd_set_bg(LCD_GREY);

    /* Status bar: left = devices + battery, middle = cwd or notice, right = clock. */
    char bar[LCD_COLS + 1];
    memset(bar, ' ', LCD_COLS);
    bar[LCD_COLS] = '\0';

    uint32_t bar_bg = ui_color(UI_STATUS_BG);
    uint32_t bar_fg = ui_color(UI_STATUS_TEXT);
    if (bat >= 0 && bat < 15 && !charging) {
        bar_bg = ui_color(UI_ERROR);
        bar_fg = ui_color(UI_SELECT_TEXT);
    } else if (charging) {
        bar_fg = ui_color(UI_SUCCESS);
    }

    const char *notice = ui_notice();
    int pos = 1;

    memcpy(bar + pos, _fat_mounted ? "SD" : "--", 2);
    pos += 2;
    bar[pos++] = ' ';

#ifdef PICO_CYW43_SUPPORTED
    memcpy(bar + pos, net_is_connected() ? "WIFI" : "----", 4);
    pos += 4;
    bar[pos++] = ' ';
#endif

    int cells = (bat < 0) ? 0 : (bat * 8 + 50) / 100;
    bar[pos++] = '[';
    for (int i = 0; i < 8; i++) bar[pos++] = (i < cells) ? '#' : '-';
    bar[pos++] = ']';
    if (charging) bar[pos++] = '+';
    if (bat >= 0) {
        char pct[8];
        int pbat = bat > 100 ? 100 : bat;
        snprintf(pct, sizeof pct, " %3d%%", pbat);
        memcpy(bar + pos, pct, 5);
        pos += 5;
    }

    /* Clock on the right */
    char rt[16];
    if (sys_rtc_is_set()) {
        int64_t epoch = sys_now_epoch_ms();
        int32_t secs = (int32_t)(epoch / 1000);
        int32_t h = (secs / 3600) % 24, m = (secs / 60) % 60;
        snprintf(rt, sizeof rt, "%02ld:%02ld", (long)h, (long)m);
    } else {
        uint32_t secs = to_ms_since_boot(get_absolute_time()) / 1000;
        uint32_t h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
        snprintf(rt, sizeof rt, "%lu:%02lu:%02lu", (unsigned long)h,
                 (unsigned long)m, (unsigned long)s);
    }
    int rlen = (int)strlen(rt);
    memcpy(bar + LCD_COLS - rlen - 1, rt, (size_t)rlen);

    /* Middle: transient notice, otherwise the working directory. */
    int mid_end = LCD_COLS - rlen - 2;
    int mid_start = pos + 1;
    const char *mid = notice ? notice : _cwd;
    if (mid && *mid && mid_start < mid_end) {
        int avail = mid_end - mid_start;
        int len = (int)strlen(mid);
        if (len <= avail) {
            memcpy(bar + mid_start, mid, (size_t)len);
        } else if (avail > 2) {
            bar[mid_start] = '.';
            bar[mid_start + 1] = '.';
            memcpy(bar + mid_start + 2, mid + len - (avail - 2), (size_t)(avail - 2));
        }
    }

    /* Render into the last text row. */
    lcd_lock();
    for (int i = 0; i < LCD_COLS; i++) {
        lcd_draw_cell(i, LCD_ROWS - 1, bar[i], bar_fg, bar_bg);
    }
    lcd_unlock();

    /* Restore state */
    lcd_set_fg(save_fg);
    lcd_set_bg(save_bg);
    lcd_set_cursor(save_col, save_row);

    lcd_unlock();
}

/* ------------------------------------------------------------------ */
/* Idle screensaver — bouncing logo, any key wakes                    */
/* ------------------------------------------------------------------ */
static void _run_screensaver(void) {
    _screensaver_active = true;
    lcd_cursor_enable(false, false);
    lcd_cls(LCD_BLACK);

    const char *word = "Mellivora";
    int wx = (int)strlen(word) * LCD_CHAR_W;
    int wy = LCD_CHAR_H;
    int x = 0, y = 0;
    int dx = 2, dy = 1;
    int frame = 0;

    for (;;) {
        watchdog_update();
        int key = read_input();
        if (key >= 0) { kbd_clear_repeat(); break; }

        /* Erase previous position */
        lcd_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)wx, (uint16_t)wy, LCD_BLACK);

        x += dx;
        y += dy;
        if (x <= 0) { x = 0; dx = -dx; }
        if (x + wx >= LCD_WIDTH) { x = LCD_WIDTH - wx; dx = -dx; }
        if (y <= 0) { y = 0; dy = -dy; }
        if (y + wy >= LCD_HEIGHT) { y = LCD_HEIGHT - wy; dy = -dy; }

        uint32_t colors[] = { LCD_CYAN, LCD_MAGENTA, LCD_YELLOW, LCD_GREEN,
                              LCD_ORANGE, LCD_AMBER, LCD_RED, LCD_BLUE };
        uint32_t fg = colors[(frame / 16) % (sizeof colors / sizeof colors[0])];
        lcd_draw_str((uint16_t)x, (uint16_t)y, word, fg, LCD_BLACK);
        frame++;
        sleep_ms(40);
    }

    lcd_cls(LCD_BLACK);
    _screensaver_active = false;
}

#if defined(PICO_RP2350A) || defined(PICO_RP2350)
/* Core1 entry: periodically refresh the status bar */
static void _core1_entry(void) {
    for (;;) {
        _draw_status_bar();
        sleep_ms(2000);
    }
}
#endif

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    char line[LINE_BUF];
    int idx = 0;

    /* RP2350 can safely run faster than RP2040 */
#if defined(PICO_RP2350A) || defined(PICO_RP2350)
    set_sys_clock_khz(150000, true);
#else
    set_sys_clock_khz(133000, true);
#endif
    stdio_init_all();
    sleep_ms(250);

    hal_init();

    gpio_init(PICO_PIN_PS);
    gpio_set_dir(PICO_PIN_PS, GPIO_OUT);
    gpio_put(PICO_PIN_PS, 1);

    g_boot_time = get_absolute_time();
    strncpy(_sys_cwd, _cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';

    kbd_init();
    kbd_set_backlight(255);
    sleep_ms(20);
    lcd_init();
    kbd_set_backlight(255);
    _lcd_ready = true;

    lcd_cls(LCD_BLACK);
    out_str("\x1b[2J\x1b[H");

    const char *target_name =
#ifdef PICO_CYW43_SUPPORTED
        "Pico 2W";
#elif defined(PICO_RP2350A) || defined(PICO_RP2350)
        "Pico 2";
#else
        "Pico";
#endif
    lcd_splash_show(MELLIVORA_VERSION, target_name, 10);

    /* Match the official PicoCalc boot flow: give the SD card time to settle. */
    sleep_ms(1200);
    lcd_splash_show(MELLIVORA_VERSION, target_name, 40);

    sd_result_t sd_r = sd_init();
    if (sd_r == SD_OK) {
        lcd_splash_show(MELLIVORA_VERSION, target_name, 60);
        dmesg_log("SD: card initialized");
        if (fat_mount() == FAT_OK) {
            _fat_mounted = true;
            dmesg_log("FAT: mounted");
            alias_load();
            var_load();
            /* Load persistent settings (backlight, home, autorun, startup) and
               then restore the last-known cwd if it still exists. */
            app_init();
            /* Sync any chdir done by settings_apply_live back into _cwd. */
            strncpy(_cwd, _sys_cwd, CWD_MAX - 1);
            _cwd[CWD_MAX - 1] = '\0';
            cwd_restore();
            clock_restore();
            hist_load();
            lcd_splash_show(MELLIVORA_VERSION, target_name, 90);
        } else {
            out_str("SD: FAT mount failed\n");
            dmesg_log("FAT: mount failed");
            lcd_splash_show(MELLIVORA_VERSION, target_name, 70);
        }
    } else {
        out_str("SD: card not detected\n");
        dmesg_log("SD: card not detected");
        lcd_splash_show(MELLIVORA_VERSION, target_name, 50);
    }

    sleep_ms(300);
    lcd_splash_show(MELLIVORA_VERSION, target_name, 100);
    sleep_ms(200);

    lcd_cls(LCD_BLACK);
    out_str("\x1b[2J\x1b[H");
    out_str(BANNER);

    /* Optional startup profile: aliases, variables, prompt, startup commands. */
    if (_fat_mounted) shell_run_script("/PROFILE.CFG", true);

#ifdef PICO_CYW43_SUPPORTED
    /* Boot-time WiFi auto-connect + NTP sync (best-effort, fire-and-forget).
     * Bounded by the connect/sync timeouts inside; total ~12s worst case. */
    if (_fat_mounted) {
        out_str("WiFi: auto-connecting to saved network...\n");
        if (net_app_wifi_autoconnect() == 0) {
            out_str("WiFi: connected.\n");
            /* If RTC predates 2020, sync from NTP */
            if (sys_now_epoch_ms() < 1577836800000LL) {
                out_str("Time: syncing from NTP...\n");
                net_app_ntp("pool.ntp.org");
                clock_persist();
            }
        } else {
            out_str("WiFi: no saved network reachable.\n");
        }
    }
#endif

    /* Run autorun script + startup app from settings, if any. */
    if (_fat_mounted) app_boot();
    if (_fat_mounted) app_planner_remind();

    /* Enable status bar on last LCD row */
    _statusbar_enabled = true;
    _draw_status_bar();

#if defined(PICO_RP2350A) || defined(PICO_RP2350)
    /* Pico 2: launch core1 for background status bar refresh */
    multicore_launch_core1(_core1_entry);
#endif

    out_prompt();
    lcd_cursor_enable(true, true);

    memset(line, 0, sizeof line);
    int hist_browse = -1;  /* -1 = not browsing history */
    int cursor = 0;        /* cursor position within line */

    /* Enable hardware watchdog: 8 seconds, pause-on-debug.
     * 8 s gives ample headroom for slow SD ops, rendering, and idle games
     * while still catching genuine hangs. */
    watchdog_enable(8000, true);

    /* Idle backlight dim — saves the LCD/CYW43 a little power */
    uint32_t last_activity_ms = sys_time_ms();
    bool dimmed = false;
    const uint32_t IDLE_DIM_MS = 60000U;        /* 60s idle -> dim */
    const uint32_t IDLE_SCREENSAVER_MS = 180000U; /* 3 min idle -> screensaver */
    uint8_t saved_backlight = 255;

    for (;;) {
        watchdog_update();
        int ch = read_input();
        if (ch < 0) {
            lcd_cursor_tick();
            uint32_t now = sys_time_ms();
            if (!_screensaver_active && (now - last_activity_ms) > IDLE_SCREENSAVER_MS) {
                _run_screensaver();
                last_activity_ms = sys_time_ms();
                /* Restore the shell display */
                lcd_cls(LCD_BLACK);
                out_str("\x1b[2J\x1b[H");
                _draw_status_bar();
                out_prompt();
                for (int i = 0; i < idx; i++) out_char(line[i]);
                for (int i = 0; i < idx - cursor; i++) out_str("\b");
                lcd_cursor_enable(true, true);
                continue;
            }
            if (!dimmed && (now - last_activity_ms) > IDLE_DIM_MS) {
                /* Save a sane "current" brightness; default to 255 */
                kbd_set_backlight(48);
                dimmed = true;
            }
            sleep_ms(20);
            continue;
        }
        if (dimmed) {
            kbd_set_backlight(saved_backlight);
            dimmed = false;
        }
        last_activity_ms = sys_time_ms();

        /* ---- Enter: execute ---- */
        if (ch == '\r' || ch == '\n') {
            out_char('\n');
            lcd_cursor_enable(false, false);
            line[idx] = '\0';
            _interrupted = false;  /* clear stale Ctrl-C from previous cmd */
            if (idx > 0) dispatch(line);
            idx = 0;
            cursor = 0;
            line[0] = '\0';
            hist_browse = -1;
            kbd_clear_repeat();
            out_prompt();
            lcd_cursor_enable(true, true);
            continue;
        }

        /* ---- Backspace / Delete ---- */
        if (ch == 0x08 || ch == 0x7F) {
            if (cursor > 0) {
                memmove(&line[cursor - 1], &line[cursor], (size_t)(idx - cursor + 1));
                idx--;
                cursor--;
                /* Redraw from cursor to end, then clear trailing char */
                out_str("\b");
                for (int i = cursor; i < idx; i++) out_char(line[i]);
                out_char(' ');
                for (int i = 0; i < idx - cursor + 1; i++) out_str("\b");
            }
            continue;
        }

        /* ---- Ctrl+C — cancel ---- */
        if (ch == 0x03) {
            _interrupted = true;
            out_str("^C\n");
            idx = 0;
            cursor = 0;
            line[0] = '\0';
            hist_browse = -1;
            kbd_clear_repeat();
            out_prompt();
            continue;
        }

        /* ---- Ctrl+A — beginning of line ---- */
        if (ch == 0x01) {
            while (cursor > 0) { out_str("\b"); cursor--; }
            continue;
        }

        /* ---- Ctrl+E — end of line ---- */
        if (ch == 0x05) {
            while (cursor < idx) { out_char(line[cursor]); cursor++; }
            continue;
        }

        /* ---- Ctrl+U — clear line ---- */
        if (ch == 0x15) {
            /* Move to start, clear everything */
            while (cursor > 0) { out_str("\b"); cursor--; }
            for (int i = 0; i < idx; i++) out_char(' ');
            for (int i = 0; i < idx; i++) out_str("\b");
            idx = 0;
            cursor = 0;
            line[0] = '\0';
            continue;
        }

        /* ---- Ctrl+K — kill to end of line ---- */
        if (ch == 0x0B) {
            for (int i = cursor; i < idx; i++) out_char(' ');
            for (int i = cursor; i < idx; i++) out_str("\b");
            line[cursor] = '\0';
            idx = cursor;
            continue;
        }

        /* ---- Ctrl+W — delete word backward ---- */
        if (ch == 0x17) {
            if (cursor > 0) {
                int new_cur = cursor;
                /* Skip trailing spaces */
                while (new_cur > 0 && line[new_cur - 1] == ' ') new_cur--;
                /* Skip non-space (word chars) */
                while (new_cur > 0 && line[new_cur - 1] != ' ') new_cur--;
                int deleted = cursor - new_cur;
                memmove(&line[new_cur], &line[cursor], (size_t)(idx - cursor + 1));
                /* Redraw: back up, print rest of line, blank tail */
                for (int i = 0; i < cursor - new_cur; i++) out_str("\b");
                for (int i = new_cur; i < idx - deleted; i++) out_char(line[i]);
                for (int i = 0; i < deleted; i++) out_char(' ');
                for (int i = 0; i < idx - new_cur; i++) out_str("\b");
                idx -= deleted;
                cursor = new_cur;
            }
            continue;
        }

        /* ---- Ctrl+L — redraw screen ---- */
        if (ch == 0x0C) {
            lcd_cls(LCD_BLACK);
            out_str("\x1b[2J\x1b[H");
            out_prompt();
            for (int i = 0; i < idx; i++) out_char(line[i]);
            for (int i = idx; i > cursor; i--) out_str("\b");
            continue;
        }

        /* ---- Tab — completion ---- */
        if (ch == '\t') {
            /* Tab complete only works at end of line */
            while (cursor < idx) { out_char(line[cursor]); cursor++; }
            idx = tab_complete(line, idx);
            cursor = idx;
            continue;
        }

        /* ---- Ctrl+R — reverse incremental history search ---- */
        if (ch == 0x12) {
            if (hist_count() == 0) continue;
            char query[64];
            int qlen = 0;
            query[0] = '\0';
            int match_idx = -1;     /* history offset of current match */
            const char *match_str = NULL;

            /* Erase current line on screen */
            while (cursor > 0) { out_str("\b"); cursor--; }
            for (int i = 0; i < idx; i++) out_char(' ');
            for (int i = 0; i < idx; i++) out_str("\b");
            /* Erase prompt itself */
            const char *p_old = "PicoLair:";
            int promptlen = (int)strlen(p_old) + (int)strlen(_cwd) + 2;
            for (int i = 0; i < promptlen; i++) out_str("\b \b");

            bool done = false, accept = false, cancel = false;
            while (!done) {
                out_char('\r');
                out_fmt("(reverse-i-search)`%s': %s",
                        query, match_str ? match_str : "");
                /* Pad to clear any leftover characters */
                out_str("                ");
                for (int i = 0; i < 16; i++) out_str("\b");

                int rch = -1;
                while (rch < 0) {
                    rch = read_input();
                    if (rch < 0) sleep_ms(20);
                }

                if (rch == '\r' || rch == '\n') { accept = true; done = true; }
                else if (rch == 0x07 || rch == 0x1B || rch == 0x03) { cancel = true; done = true; }
                else if (rch == 0x08 || rch == 0x7F) {
                    if (qlen > 0) { query[--qlen] = '\0'; }
                } else if (rch == 0x12) {
                    /* Find next older match */
                    int start = (match_idx > 0) ? match_idx - 1 : hist_count() - 1;
                    int found = -1;
                    for (int i = start; i >= 0; i--) {
                        const char *h = hist_get(i);
                        if (h && (!*query || strstr(h, query))) { found = i; break; }
                    }
                    if (found >= 0) { match_idx = found; match_str = hist_get(found); }
                } else if (isprint((unsigned char)rch) && qlen < (int)sizeof query - 1) {
                    query[qlen++] = (char)rch;
                    query[qlen] = '\0';
                }

                if (!done) {
                    /* Re-search from newest */
                    int found = -1;
                    for (int i = hist_count() - 1; i >= 0; i--) {
                        const char *h = hist_get(i);
                        if (h && strstr(h, query)) { found = i; break; }
                    }
                    match_idx = found;
                    match_str = (found >= 0) ? hist_get(found) : NULL;
                }
            }

            /* Clear search line */
            out_char('\r');
            for (int i = 0; i < (int)sizeof query + 64; i++) out_char(' ');
            out_char('\r');
            out_prompt();

            if (accept && match_str) {
                strncpy(line, match_str, LINE_BUF - 1);
                line[LINE_BUF - 1] = '\0';
                idx = (int)strlen(line);
                cursor = idx;
                hist_browse = -1;
                out_str(line);
            } else {
                (void)cancel;
                idx = 0; cursor = 0; line[0] = '\0';
            }
            continue;
        }

        /* ---- Arrow keys (STM32 raw codes) ---- */
        /* Up = 0xB5, Down = 0xB4, Left = 0xB6, Right = 0xB7 */
        if (ch == 0xB5) { /* Up — previous history */
            int next = (hist_browse < 0) ? hist_count() - 1 :
                       hist_browse - 1;
            if (next >= 0 && next < hist_count()) {
                const char *h = hist_get(next);
                if (h) {
                    hist_browse = next;
                    int old_len = idx;
                    /* Move cursor to start */
                    while (cursor > 0) { out_str("\b"); cursor--; }
                    strncpy(line, h, LINE_BUF - 1);
                    line[LINE_BUF - 1] = '\0';
                    idx = (int)strlen(line);
                    cursor = idx;
                    /* Redraw */
                    for (int i = 0; i < idx; i++) out_char(line[i]);
                    for (int i = idx; i < old_len; i++) out_char(' ');
                    for (int i = idx; i < old_len; i++) out_str("\b");
                }
            }
            continue;
        }

        if (ch == 0xB4) { /* Down — next history */
            if (hist_browse >= 0) {
                int old_len = idx;
                while (cursor > 0) { out_str("\b"); cursor--; }
                if (hist_browse < hist_count() - 1) {
                    hist_browse++;
                    const char *h = hist_get(hist_browse);
                    if (h) {
                        strncpy(line, h, LINE_BUF - 1);
                        line[LINE_BUF - 1] = '\0';
                    }
                } else {
                    /* Past newest = empty line */
                    hist_browse = -1;
                    line[0] = '\0';
                }
                idx = (int)strlen(line);
                cursor = idx;
                for (int i = 0; i < idx; i++) out_char(line[i]);
                for (int i = idx; i < old_len; i++) out_char(' ');
                for (int i = idx; i < old_len; i++) out_str("\b");
            }
            continue;
        }

        if (ch == 0xB6) { /* Left */
            if (cursor > 0) { cursor--; out_str("\b"); }
            continue;
        }

        if (ch == 0xB7) { /* Right */
            if (cursor < idx) { out_char(line[cursor]); cursor++; }
            continue;
        }

        /* ---- ESC (from USB serial ANSI sequences) ---- */
        if (ch == 0x1B) {
            /* Try to read [ then direction */
            int ch2 = -1;
            for (int w = 0; w < 50 && ch2 < 0; w++) {
                ch2 = getchar_timeout_us(1000);
            }
            if (ch2 == '[') {
                int ch3 = -1;
                for (int w = 0; w < 50 && ch3 < 0; w++) {
                    ch3 = getchar_timeout_us(1000);
                }
                if (ch3 == 'A') { ch = 0xB5; goto handle_arrow; }
                if (ch3 == 'B') { ch = 0xB4; goto handle_arrow; }
                if (ch3 == 'D') { ch = 0xB6; goto handle_arrow; }
                if (ch3 == 'C') { ch = 0xB7; goto handle_arrow; }
            }
            continue;
            handle_arrow:
            /* Re-inject as STM32 arrow code and re-dispatch */
            if (ch == 0xB5 || ch == 0xB4 || ch == 0xB6 || ch == 0xB7) {
                /* Reprocess — copy the arrow handling above */
                if (ch == 0xB5) {
                    int next = (hist_browse < 0) ? hist_count() - 1 : hist_browse - 1;
                    if (next >= 0 && next < hist_count()) {
                        const char *h = hist_get(next);
                        if (h) {
                            hist_browse = next;
                            int old_len = idx;
                            while (cursor > 0) { out_str("\b"); cursor--; }
                            strncpy(line, h, LINE_BUF - 1);
                            line[LINE_BUF - 1] = '\0';
                            idx = (int)strlen(line);
                            cursor = idx;
                            for (int i = 0; i < idx; i++) out_char(line[i]);
                            for (int i = idx; i < old_len; i++) out_char(' ');
                            for (int i = idx; i < old_len; i++) out_str("\b");
                        }
                    }
                } else if (ch == 0xB4) {
                    if (hist_browse >= 0) {
                        int old_len = idx;
                        while (cursor > 0) { out_str("\b"); cursor--; }
                        if (hist_browse < hist_count() - 1) {
                            hist_browse++;
                            const char *h = hist_get(hist_browse);
                            if (h) { strncpy(line, h, LINE_BUF - 1); line[LINE_BUF - 1] = '\0'; }
                        } else { hist_browse = -1; line[0] = '\0'; }
                        idx = (int)strlen(line); cursor = idx;
                        for (int i = 0; i < idx; i++) out_char(line[i]);
                        for (int i = idx; i < old_len; i++) out_char(' ');
                        for (int i = idx; i < old_len; i++) out_str("\b");
                    }
                } else if (ch == 0xB6) {
                    if (cursor > 0) { cursor--; out_str("\b"); }
                } else if (ch == 0xB7) {
                    if (cursor < idx) { out_char(line[cursor]); cursor++; }
                }
            }
            continue;
        }

        /* ---- Printable character — insert at cursor ---- */
        if (isprint((unsigned char)ch) && idx < LINE_BUF - 1) {
            if (cursor < idx) {
                /* Insert in middle */
                memmove(&line[cursor + 1], &line[cursor], (size_t)(idx - cursor));
            }
            line[cursor] = (char)ch;
            idx++;
            line[idx] = '\0';
            /* Print from cursor to end, then move back */
            for (int i = cursor; i < idx; i++) out_char(line[i]);
            cursor++;
            for (int i = 0; i < idx - cursor; i++) out_str("\b");
        }
    }
}
