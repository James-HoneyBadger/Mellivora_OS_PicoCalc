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
#include "apps.h"
#ifdef PICO_CYW43_SUPPORTED
#include "netapps.h"
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#define LINE_BUF        256
#define CWD_MAX         256
#ifdef PICO_RP2350A
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

/* Global interrupt flag — checked by long-running loops.
 * Aliased to _sys_interrupted (declared in syscall.h) so apps in apps.c /
 * apps in subdirs can also check it via sys_interrupted(). */
#define _interrupted _sys_interrupted

/* Status bar flag — declared early so cmd_sysinfo/cmd_status can see it */
static volatile bool _statusbar_enabled = false;
static void _draw_status_bar(void);

/* Confirmation helpers (defined later, near read_input) */
static bool confirm(const char *msg);
static bool parse_force_flag(const char **arg);
static void rm_recurse(const char *path);
static void copy_str(char *dst, size_t dst_sz, const char *src);

#define BANNER \
    "Mellivora OS v" MELLIVORA_VERSION "\n" \
    "PicoCalc ready\n\n"

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
    if (_redir_active) {
        redir_char(c);
        return;
    }
    if (!_lcd_ready) {
        putchar_raw(c);
        return;
    }
    sys_putchar(c);
}

static void out_str(const char *s) {
    while (*s) out_char(*s++);
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
    copy_str(_vars[slot].name, sizeof _vars[slot].name, name);
    copy_str(_vars[slot].value, sizeof _vars[slot].value, value ? value : "");
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

/* In-place expansion of $NAME and ${NAME} in a buffer. */
static void var_expand(char *line, size_t line_sz) {
    char out[LINE_BUF];
    size_t oi = 0;
    bool in_squote = false;
    for (size_t i = 0; line[i] && oi + 1 < sizeof out; i++) {
        char c = line[i];
        if (c == '\'') { in_squote = !in_squote; out[oi++] = c; continue; }
        if (in_squote || c != '$') { out[oi++] = c; continue; }
        /* $NAME or ${NAME} */
        size_t j = i + 1;
        bool braced = false;
        if (line[j] == '{') { braced = true; j++; }
        char name[ALIAS_NAME_SZ]; size_t ni = 0;
        while (line[j] && ni + 1 < sizeof name) {
            char nc = line[j];
            if (braced ? (nc == '}') : !(isalnum((unsigned char)nc) || nc == '_')) break;
            name[ni++] = nc; j++;
        }
        name[ni] = '\0';
        if (braced && line[j] == '}') j++;
        if (ni == 0) { out[oi++] = c; continue; }   /* lone $ */
        const char *val = var_lookup(name);
        if (val) {
            size_t vl = strlen(val);
            if (oi + vl >= sizeof out) vl = sizeof out - 1 - oi;
            memcpy(out + oi, val, vl);
            oi += vl;
        }
        i = j - 1;
    }
    out[oi] = '\0';
    copy_str(line, line_sz, out);
}

/*
 * Build an absolute path from cwd + input.
 * Result is stored in out (size CWD_MAX).
 * Handles ".." components.
 */
/* Maximum length of any single path component (FAT 8.3 needs 12, leave headroom). */
#define PATH_COMP_MAX   64

static void resolve_abs(const char *input, char *out) {
    char tmp[CWD_MAX * 2];

    if (!input || input[0] == '\0') {
        strncpy(out, _cwd, CWD_MAX - 1);
        out[CWD_MAX - 1] = '\0';
        return;
    }

    if (input[0] == '/') {
        /* Already absolute */
        strncpy(tmp, input, sizeof tmp - 1);
        tmp[sizeof tmp - 1] = '\0';
    } else {
        /* Relative — prepend cwd */
        if (strcmp(_cwd, "/") == 0)
            snprintf(tmp, sizeof tmp, "/%s", input);
        else
            snprintf(tmp, sizeof tmp, "%s/%s", _cwd, input);
    }

    /* Canonicalise: process each component. tmp is guaranteed to start with '/'
       above, so the loop only ever encounters the slash branch. */
    char canon[CWD_MAX];
    char *p = tmp;
    canon[0] = '\0';

    while (*p == '/') {
        /* Collapse multiple slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract component into a small bounded buffer */
        char comp[PATH_COMP_MAX];
        int ci = 0;
        while (*p && *p != '/') {
            if (ci + 1 < (int)sizeof comp) comp[ci++] = *p;
            p++;
        }
        comp[ci] = '\0';

        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            /* Go up: remove last component. Stops at root — '..' from '/' is '/'. */
            char *last = strrchr(canon, '/');
            if (last) *last = '\0';
        } else {
            size_t clen = strlen(canon);
            if (clen + 1 + strlen(comp) < CWD_MAX) {
                canon[clen++] = '/';
                const char *src = comp;
                while (*src) canon[clen++] = *src++;
                canon[clen] = '\0';
            }
            /* If component would overflow, silently drop it — safer than truncating
               part of a name and producing a different path. */
        }
    }

    if (canon[0] == '\0') { canon[0] = '/'; canon[1] = '\0'; }

    strncpy(out, canon, CWD_MAX - 1);
    out[CWD_MAX - 1] = '\0';
}

static void out_prompt(void) {
#ifndef PICO_RP2350A
    /* On Pico 1 (no core1), refresh status bar at each prompt */
    _draw_status_bar();
#endif
    uint32_t saved = lcd_get_fg();
    lcd_set_fg(LCD_YELLOW);
    out_fmt("PicoLair:%s> ", _cwd);
    lcd_set_fg(saved);
}

/* ------------------------------------------------------------------ */
/* Persistent CWD — saved on every successful 'cd', restored at boot   */
/* ------------------------------------------------------------------ */
#define LASTCWD_FILE "/LASTCWD.TXT"

static void cwd_persist(void) {
    if (!_fat_mounted) return;
    /* Best-effort write; do not surface errors (cwd persistence is a nicety). */
    fat_unlink(LASTCWD_FILE);
    fat_create(LASTCWD_FILE, (const uint8_t *)_cwd, (uint32_t)strlen(_cwd));
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

static void copy_str(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char *skip_spaces(char *s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s;
}

static void trim_right(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                      s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

static void dispatch(char *line);
static void cmd_help(const char *arg);

/* ------------------------------------------------------------------ */
/* History                                                              */
/* ------------------------------------------------------------------ */

static char _hist[HIST_ENTRIES][HIST_ENTRY_SZ];
static int  _hist_count = 0;
static int  _hist_head  = 0;   /* next write slot (ring) */

static void hist_push(const char *line) {
    if (!line || !*line) return;
    /* Avoid duplicate of last entry */
    if (_hist_count > 0) {
        int prev = (_hist_head - 1 + HIST_ENTRIES) % HIST_ENTRIES;
        if (strcmp(_hist[prev], line) == 0) return;
    }
    size_t len = strlen(line);
    if (len >= HIST_ENTRY_SZ) len = HIST_ENTRY_SZ - 1;
    memcpy(_hist[_hist_head], line, len);
    _hist[_hist_head][len] = '\0';
    _hist_head = (_hist_head + 1) % HIST_ENTRIES;
    if (_hist_count < HIST_ENTRIES) _hist_count++;
}

/* Returns pointer to history entry (0 = oldest), or NULL if out of range. */
static const char *hist_get(int offset) {
    if (offset < 0 || offset >= _hist_count) return NULL;
    int idx = (_hist_head - _hist_count + offset + HIST_ENTRIES * 2) % HIST_ENTRIES;
    return _hist[idx];
}

static void hist_clear(void) {
    memset(_hist, 0, sizeof _hist);
    _hist_count = 0;
    _hist_head = 0;
}

/* ----- Persistent history (/HISTORY.LOG) -------------------------- */
#define HISTORY_FILE "/HISTORY.LOG"

static void hist_save(void) {
    if (!_fat_mounted) return;
    /* Build a single buffer of recent entries (newest last) */
    char buf[HIST_ENTRY_SZ * 8 + 8];  /* keep last 8 lines max for write */
    size_t pos = 0;
    int start = (_hist_count > 8) ? _hist_count - 8 : 0;
    for (int i = start; i < _hist_count; i++) {
        const char *h = hist_get(i);
        if (!h) continue;
        size_t l = strlen(h);
        if (pos + l + 2 >= sizeof buf) break;
        memcpy(buf + pos, h, l); pos += l;
        buf[pos++] = '\n';
    }
    if (pos == 0) return;
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
        fat_unlink(HISTORY_FILE);
        fat_create(HISTORY_FILE, (const uint8_t *)existing, (uint32_t)(old_n + pos));
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
    copy_str(_aliases[slot].name, sizeof _aliases[slot].name, name);
    copy_str(_aliases[slot].value, sizeof _aliases[slot].value, value ? value : "");

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
    else copy_str(expanded, sizeof expanded, _aliases[slot].value);

    if (strcmp(expanded, line) == 0) return false;
    copy_str(line, line_sz, expanded);
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
    if (strcmp(line, "!!") == 0) entry = hist_get(_hist_count - 1);
    else {
        int n = atoi(line + 1);
        if (n >= 1) entry = hist_get(n - 1);
    }

    if (!entry) {
        out_str("history: event not found\n");
        line[0] = '\0';
        return true;
    }

    copy_str(line, line_sz, entry);
    out_fmt("%s\n", line);
    return true;
}

/* ------------------------------------------------------------------ */
/* Shell commands                                                        */
/* ------------------------------------------------------------------ */

static absolute_time_t g_boot_time;

static void cmd_history(const char *arg) {
    char tmp[LINE_BUF];
    copy_str(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);

    if (!s || !*s) {
        if (_hist_count == 0) {
            out_str("history: empty\n");
            return;
        }
        for (int i = 0; i < _hist_count; i++) out_fmt("%2d  %s\n", i + 1, hist_get(i));
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
        copy_str(replay, sizeof replay, entry);
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
    copy_str(tmp, sizeof tmp, arg ? arg : "");
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
    copy_str(tmp, sizeof tmp, arg ? arg : "");
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
    copy_str(tmp, sizeof tmp, arg ? arg : "");
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
    if (!var_set(name, value)) out_str("set: too many variables\n");
}

static void cmd_unsetvar(const char *arg) {
    char tmp[LINE_BUF];
    copy_str(tmp, sizeof tmp, arg ? arg : "");
    char *s = skip_spaces(tmp);
    trim_right(s);
    if (!s || !*s) { out_str("usage: unset NAME\n"); return; }
    if (!var_unset(s)) out_fmt("unset: %s not found\n", s);
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

    if (!*topic) {
        cmd_help(NULL);
        return;
    }

    if (!strcmp(topic, "mount") || !strcmp(topic, "ls") || !strcmp(topic, "cd") ||
        !strcmp(topic, "pwd") || !strcmp(topic, "cat") || !strcmp(topic, "touch") ||
        !strcmp(topic, "write") || !strcmp(topic, "mkdir") || !strcmp(topic, "rm") ||
        !strcmp(topic, "cp") || !strcmp(topic, "mv") || !strcmp(topic, "stat") ||
        !strcmp(topic, "tree") || !strcmp(topic, "du") || !strcmp(topic, "df") || !strcmp(topic, "edit") ||
        !strcmp(topic, "hexedit") || !strcmp(topic, "browse") || !strcmp(topic, "notes") || !strcmp(topic, "todo") ||
        !strcmp(topic, "planner") || !strcmp(topic, "journal") || !strcmp(topic, "habits") ||
        !strcmp(topic, "bookmarks") || !strcmp(topic, "sprite") || !strcmp(topic, "terminal")) {
        cmd_help("fs");
        return;
    }

    if (!strcmp(topic, "basic") || !strcmp(topic, "tcc") || !strcmp(topic, "calc")) {
        cmd_help("lang");
        return;
    }

    if (!strcmp(topic, "games") || !strcmp(topic, "dice") || !strcmp(topic, "coin") ||
        !strcmp(topic, "guess") || !strcmp(topic, "snake")) {
        out_str("Games pack:\n");
        out_str("  games               Open the mini game menu\n");
        out_str("  dice [COUNT] [SIDES]  Roll one or more dice\n");
        out_str("  coin [COUNT]        Flip one or more coins\n");
        out_str("  guess               Play guess-the-number\n");
        out_str("  snake               Play the step-based snake game\n");
        return;
    }

    if (!strcmp(topic, "apps") || !strcmp(topic, "fs") || !strcmp(topic, "lang")) {
        cmd_help(topic);
        return;
    }

    if (!strcmp(topic, "alias") || !strcmp(topic, "unalias") || !strcmp(topic, "history") || !strcmp(topic, "man")) {
        out_str("Shell helpers:\n");
        out_str("  history [clear|run N]  Show, clear, or replay history\n");
        out_str("  !! and !N              Rerun the last or numbered command\n");
        out_str("  alias NAME VALUE       Create a persistent alias\n");
        out_str("  unalias NAME           Remove a saved alias\n");
        out_str("  man TOPIC              Open built-in help\n");
        return;
    }

    out_fmt("No manual entry for %s. Try: man fs, man apps, man lang\n", topic);
}

static void cmd_help(const char *arg) {
    const char *topic = arg ? arg : "";
    while (*topic == ' ') topic++;

    if (!*topic) {
        out_str("System commands:\n");
        out_str("  help [apps|fs|lang]  Show overview or a topic\n");
        out_str("  man TOPIC            Open built-in detailed help\n");
        out_str("  history [clear]      Show or clear shell history\n");
        out_str("  alias [N V]          List or define a persistent alias\n");
        out_str("  unalias NAME         Remove a command alias\n");
        out_str("  uname                OS and platform info\n");
        out_str("  sysinfo              Detailed system information\n");
        out_str("  uptime               Time since boot (ms)\n");
        out_str("  status [on|off]      Toggle status bar\n");
        out_str("  clear                Clear the screen\n");
        out_str("  battery              Battery charge level\n");
        out_str("  backlight <n>        Keyboard backlight 0-255\n");
        out_str("  reboot               Warm reboot\n");
        out_str("\nStorage commands:\n");
        out_str("  mount ls dir cd pwd cat touch write mkdir rm rename mv\n");
        out_str("  sdinfo sdread edit browse tree du df\n");
        out_str("\nApp commands:\n");
        out_str("  hello basename dirname seq head tail wc cut grep find tree du df pager rev sort\n");
        out_str("  hexdump od hexedit calc cp mv stat edit browse notes journal habits todo planner\n");
        out_str("  bookmarks games snake dice coin guess sprite terminal home dashboard sysmon\n");
        out_str("  samples clock cal script paint settings set sleep id true false basic tcc\n");
#ifdef PICO_CYW43_SUPPORTED
        out_str("\nNetwork commands (Pico 2W):\n");
        out_str("  wifi connect|scan|status|disconnect\n");
        out_str("  ping ifconfig ntp dns fetch wget weather irc telnet netstat\n");
#endif
        out_str("\nUse UP/DOWN for history, Ctrl-U to clear a line, and Ctrl-L to redraw.\n");
        out_str("Tip: append | more to long commands for paging.\n");
        return;
    }

    if (!strcmp(topic, "fs")) {
        out_str("Filesystem help:\n");
        out_str("  mount               Mount the SD FAT volume\n");
        out_str("  history [clear|run N]  Show, clear, or replay history\n");
        out_str("  alias NAME VALUE    Create a persistent command alias\n");
        out_str("  unalias NAME        Remove a command alias\n");
        out_str("  ls [path]           List a directory\n");
        out_str("  cd [path]           Change directory\n");
        out_str("  cat <path>          Show a text file\n");
        out_str("  touch <path>        Create an empty file\n");
        out_str("  write <p> <text>    Replace a file with text\n");
        out_str("  mkdir <path>        Create a directory\n");
        out_str("  rm <path>           Remove a file or empty directory\n");
        out_str("  rename OLD NEW      Rename a file (same directory)\n");
        out_str("  tree [-L N] [PATH]  Show a recursive directory tree\n");
        out_str("  du [PATH]           Show recursive disk usage\n");
        out_str("  df                  Show filesystem capacity and free space\n");
        out_str("  cp SRC DST          Copy a file\n");
        out_str("  mv SRC DST          Move or rename a file\n");
        out_str("  stat PATH           Show compact file info\n");
        out_str("  edit FILE           Open the line editor for a text file\n");
        out_str("  hexedit FILE        Open the interactive byte editor\n");
        out_str("  browse [PATH]       Open the interactive file browser\n");
        out_str("  notes [FILE]        Open the notes editor using the configured default\n");
        out_str("  journal ...         Manage a persistent dated journal\n");
        out_str("  habits ...          Track and record habit completions\n");
        out_str("  todo ...            Manage a persistent task list\n");
        out_str("  planner ...         Manage dated agenda and planner entries\n");
        out_str("  bookmarks ...       Save and launch favorite files, paths, or apps\n");
        out_str("  sprite [FILE]       Open the sprite editor\n");
        out_str("  terminal            Open raw terminal mode\n");
        out_str("  games               Open the mini game pack\n");
        out_str("  settings            Show or manage persistent device settings\n");
        out_str("  set K V             Shortcut for settings set KEY VALUE\n");
        out_str("  samples ...         List, show, write, or run bundled demo programs\n");
        out_str("  sysmon              Open the system monitor\n");
        out_str("  script FILE         Run a shell script file\n");
        out_str("  paint               Open the pixel paint application\n");
        out_str("  clock               Open the uptime-based clock view\n");
        out_str("  cal [m] [y]         Show a calendar for a month and year\n");
        return;
    }

    if (!strcmp(topic, "lang")) {
        out_str("Language help:\n");
        out_str("  basic [file.bas]    BASIC with IF, FOR/NEXT, GOSUB, RETURN, CLS\n");
        out_str("  tcc [file.tc]       Tiny C with arrays, bitwise ops, clear, vars\n");
        out_str("  calc [expr]         Quick calculator and expression REPL\n");
        out_str("  home                Open the simple app launcher\n");
        out_str("  dashboard           Open the live status dashboard\n");
        return;
    }

    if (!strcmp(topic, "apps")) {
        out_str("Application help:\n");
        out_str("  text: head tail wc cut grep pager rev sort hexdump od\n");
        out_str("  file: find tree du df cp mv stat edit hexedit browse notes journal habits todo planner bookmarks sprite samples basename dirname\n");
        out_str("  misc: hello seq sleep id true false calc games snake dice coin guess terminal home dashboard sysmon script paint clock cal settings set\n");
        return;
    }

    out_str("Unknown help topic. Try: help, help fs, help apps, help lang\n");
}

static void cmd_uname(const char *arg) {
    bool all = (arg && (*arg == 'a' || strstr(arg, "-a")));
#ifdef PICO_CYW43_SUPPORTED
    out_str("Mellivora OS v" MELLIVORA_VERSION " - PicoCalc target (RP2350 + CYW43 WiFi)\n");
#elif defined(PICO_RP2350A)
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
    out_str("https://github.com/James-HoneyBadger/Mellivora_OS_PicoCalc\n");
}

static void cmd_uptime(void) {
    int64_t us = absolute_time_diff_us(g_boot_time, get_absolute_time());
    out_fmt("%lld ms\n", us / 1000LL);
}

/* ------------------------------------------------------------------ */
/* date — software RTC (settable, persisted to /CLOCK.CFG)            */
/* ------------------------------------------------------------------ */
#define CLOCK_FILE "/CLOCK.CFG"

/* Days-from-civil based on Howard Hinnant's algorithm. */
static int64_t ymd_to_epoch_ms(int y, int m, int d, int hh, int mm, int ss) {
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153U * (unsigned)(m + (m > 2 ? -3 : 9)) + 2U) / 5U + (unsigned)(d - 1);
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    int64_t days = (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
    return (days * 86400LL + (int64_t)hh * 3600LL + (int64_t)mm * 60LL + (int64_t)ss) * 1000LL;
}

static void epoch_ms_to_ymd(int64_t ms, int *y, int *mo, int *d, int *hh, int *mm, int *ss) {
    int64_t s   = ms / 1000;
    int64_t day = s / 86400; if (s < 0 && (s % 86400)) day--;
    int64_t tod = s - day * 86400;
    *hh = (int)(tod / 3600); *mm = (int)((tod / 60) % 60); *ss = (int)(tod % 60);
    int64_t z = day + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int yr = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned mp  = (5*doy + 2)/153;
    unsigned dd  = doy - (153*mp + 2)/5 + 1;
    unsigned mn  = mp + (mp < 10 ? 3 : -9);
    yr += (mn <= 2);
    *y = yr; *mo = (int)mn; *d = (int)dd;
}

static void clock_persist(void) {
    if (!_fat_mounted) return;
    char buf[40];
    int n = snprintf(buf, sizeof buf, "%lld\n", (long long)sys_now_epoch_ms());
    if (n <= 0) return;
    fat_unlink(CLOCK_FILE);
    fat_create(CLOCK_FILE, (const uint8_t *)buf, (uint32_t)n);
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
        sys_set_epoch_ms(ymd_to_epoch_ms(y, mo, d, hh, mm, ss));
        clock_persist();
    }
    int y, mo, d, hh, mm, ss;
    epoch_ms_to_ymd(sys_now_epoch_ms(), &y, &mo, &d, &hh, &mm, &ss);
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
    out_fmt("Mellivora OS for PicoCalc\n");
#ifdef PICO_RP2350A
    out_fmt("Platform: RP2350 (Pico 2)\n");
    out_fmt("RAM:      520 KB\n");
    out_fmt("Flash:    4 MB\n");
    out_fmt("Cores:    2 (core1 active)\n");
#else
    out_fmt("Platform: RP2040 (Pico)\n");
    out_fmt("RAM:      264 KB\n");
    out_fmt("Flash:    2 MB\n");
    out_fmt("Cores:    2 (core1 idle)\n");
#endif
#ifdef PICO_CYW43_SUPPORTED
    out_fmt("WiFi:     CYW43 (Pico 2W)\n");
#else
    out_fmt("WiFi:     none\n");
#endif
    out_fmt("Clock:    %lu MHz\n",
            (unsigned long)(clock_get_hz(clk_sys) / 1000000));
    out_fmt("DMA LCD:  %s\n",
#ifdef PICO_RP2350A
            "enabled"
#else
            "enabled"
#endif
            );
    out_fmt("Status bar: %s\n", _statusbar_enabled ? "on" : "off");
    int64_t us = absolute_time_diff_us(g_boot_time, get_absolute_time());
    uint32_t sec = (uint32_t)(us / 1000000LL);
    out_fmt("Uptime:   %lu:%02lu:%02lu\n",
            (unsigned long)(sec / 3600),
            (unsigned long)((sec / 60) % 60),
            (unsigned long)(sec % 60));
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
}

static void _ls_entry(const char *name, uint32_t size,
                      bool is_dir, void *ctx) {
    (void)ctx;
    uint32_t saved_fg = lcd_get_fg();
    if (is_dir) {
        lcd_set_fg(LCD_CYAN);
        out_fmt("  [DIR]  %s\n", name);
    } else {
        lcd_set_fg(LCD_GREEN);
        out_fmt("  %8lu  %s\n", (unsigned long)size, name);
    }
    lcd_set_fg(saved_fg);
}

static void cmd_ls(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    char abs[CWD_MAX];
    if (arg && *arg) resolve_abs(arg, abs);
    else             strncpy(abs, _cwd, CWD_MAX - 1);
    abs[CWD_MAX - 1] = '\0';
    fat_result_t r = fat_ls(abs, _ls_entry, NULL);
    if (r != FAT_OK) out_fmt("ls: %s\n", fat_result_str(r));
}

static void cmd_cd(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    char abs[CWD_MAX];
    resolve_abs((arg && *arg) ? arg : "/", abs);

    /* Validate: must be a directory */
    fat_result_t r = fat_is_dir(abs);
    if (r != FAT_OK) { out_fmt("cd: %s: %s\n", abs, fat_result_str(r)); return; }

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
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    if (!arg || !*arg) { out_str("usage: cat <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_file_t f;
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) { out_fmt("cat: %s: %s\n", abs, fat_result_str(r)); return; }
    uint8_t buf[64];
    int32_t n;
    while ((n = fat_read(&f, buf, sizeof buf)) > 0) {
        for (int32_t i = 0; i < n; i++) out_char((char)buf[i]);
    }
    /* Ensure file handle is not leaked */
    if (n != FAT_ERR_EOF && n < 0)
        out_fmt("\ncat: %s: %s\n", abs, fat_result_str((fat_result_t)n));
    else
        out_char('\n');
}

static void cmd_echo(const char *arg) {
    if (arg && *arg) out_str(arg);
    out_char('\n');
}

static void cmd_touch(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    if (!arg || !*arg) { out_str("usage: touch <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_result_t r = fat_create(abs, NULL, 0);
    if (r == FAT_ERR_EXISTS) return;
    if (r != FAT_OK) out_fmt("touch: %s: %s\n", abs, fat_result_str(r));
}

static void cmd_write(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
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
    if (r != FAT_OK) out_fmt("write: %s: %s\n", abs, fat_result_str(r));
}

static void cmd_mkdir(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    if (!arg || !*arg) { out_str("usage: mkdir <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_result_t r = fat_mkdir(abs);
    if (r != FAT_OK) out_fmt("mkdir: %s: %s\n", abs, fat_result_str(r));
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
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
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
    if (r != FAT_OK) out_fmt("rm: %s: %s\n", abs, fat_result_str(r));
}

static void cmd_rename(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
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
    if (r != FAT_OK) out_fmt("rename: %s\n", fat_result_str(r));
}

static void cmd_sdinfo(void) {
    sd_result_t r = sd_init();
    out_fmt("SD init: %s\n", sd_result_str(r));
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
    /* 20 rows * (40 cols + 1 newline) + small slack */
    char buf[20 * 41 + 16];
    size_t pos = 0;
    for (int r = 0; r < 20 && pos + 41 < sizeof buf; r++) {
        for (int c = 0; c < 40; c++) {
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

static int parse_pipe_mode(char *line) {
    char *pipe = strchr(line, '|');
    if (!pipe) return 0;

    *pipe++ = '\0';
    trim_right(line);
    pipe = skip_spaces(pipe);
    trim_right(pipe);

    if (!*pipe || strcmp(pipe, "more") == 0 || strcmp(pipe, "pager") == 0) {
        return 1;
    }

    out_str("Only '| more' is supported right now.\n");
    line[0] = '\0';
    return -1;
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

/* ---- Tab completion ---- */

static const char *_shell_cmds[] = {
    "help", "man", "history", "alias", "unalias", "uname", "uptime", "clear",
    "battery", "backlight", "mount", "ls", "dir", "cd", "pwd", "cat", "echo",
    "touch", "write", "mkdir", "rm", "rename", "sdinfo", "sdread", "reboot",
    "date", "dmesg",
    "hello", "basename", "dirname", "seq", "head", "tail", "wc", "cut", "grep",
    "find", "tree", "du", "df", "disk", "space", "pager", "more", "rev", "sort",
    "hexdump", "od", "hexedit", "hex", "calc", "cp", "mv", "stat", "edit",
    "bedit", "browse", "files", "notes", "memo", "journal", "diary", "habits",
    "habit", "bookmarks", "bookmark", "favs", "favorites", "games", "game",
    "dice", "coin", "guess", "snake", "sprite", "terminal", "term", "tty",
    "serial", "home", "launcher", "dashboard", "status", "sysmon", "monitor",
    "settings", "set", "todo", "tasks", "planner", "agenda", "plan", "samples",
    "demos", "clock", "cal", "calendar", "basic", "tcc", "tinyc", "script",
    "paint", "sleep", "id", "true", "false",
    "shutdown", "halt", "poweroff", "version",
    "stopwatch", "timer", "pomodoro", "screenshot", "set", "unset",
    "watch", "diff", "env", "lock", "xxd", "strings", "yes", "tee",
    "life", "tetris", "mandelbrot", "fractal", "piano", "forth",
    "xmodem", "theme",
    "sysinfo",
#ifdef PICO_CYW43_SUPPORTED
    "wifi", "ping", "ifconfig", "ntp", "dns", "fetch", "wget",
    "weather", "irc", "netstat", "telnet",
#endif
    NULL
};

/* File completion context for fat_ls callback */
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

static int tab_complete(char *line, int idx) {
    if (idx == 0) return idx;

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

        if (fc.count == 1) {
            /* Erase line on screen */
            for (int i = 0; i < idx; i++) out_str("\b \b");
            /* Rebuild: command + space + dir prefix + completed name */
            size_t cmd_len = (size_t)(token - line);
            char completed[LINE_BUF];
            memcpy(completed, line, cmd_len);
            size_t pos = cmd_len;
            if (last_slash) {
                size_t dlen = (size_t)(last_slash - token + 1);
                memcpy(completed + pos, token, dlen);
                pos += dlen;
            }
            /* Copy matched name (lowercase for display) */
            for (const char *p = fc.matches[0]; *p && pos < LINE_BUF - 2; p++) {
                completed[pos++] = (char)tolower((unsigned char)*p);
            }
            if (fc.is_dir[0] && pos < LINE_BUF - 1)
                completed[pos++] = '/';
            else if (pos < LINE_BUF - 1)
                completed[pos++] = ' ';
            completed[pos] = '\0';
            memcpy(line, completed, pos + 1);
            idx = (int)pos;
            out_str(line);
        } else {
            out_char('\n');
            for (int i = 0; i < fc.count; i++) {
                for (const char *p = fc.matches[i]; *p; p++)
                    out_char((char)tolower((unsigned char)*p));
                if (fc.is_dir[i]) out_char('/');
                out_str("  ");
            }
            out_char('\n');
            out_prompt();
            out_str(line);
        }
        return idx;
    }

    /* Command name completion (original behavior) */
    const char *prefix = line;
    size_t plen = (size_t)idx;
    const char *match = NULL;
    int match_count = 0;

    for (const char **c = _shell_cmds; *c; c++) {
        if (strncmp(*c, prefix, plen) == 0) {
            match = *c;
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
        for (const char **c = _shell_cmds; *c; c++) {
            if (strncmp(*c, prefix, plen) == 0) {
                out_str(*c);
                out_str("  ");
            }
        }
        out_char('\n');
        out_prompt();
        out_str(line);
    }
    return idx;
}

static void dispatch_single(char *line);

static void dispatch(char *line) {
    /* Strip trailing whitespace */
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }

    if (len == 0) return;

    if (history_expand_bang(line, LINE_BUF)) {
        len = (int)strlen(line);
        if (len == 0) return;
    }

    (void)alias_expand_safe(line, LINE_BUF);
    var_expand(line, LINE_BUF);

    /* Save to history */
    hist_push(line);
    hist_save();

    /* Split on ';' for command chaining (outside quotes) */
    char *segments[16];
    int nseg = 0;
    char *p = line;
    bool in_quotes = false;
    segments[0] = p;
    nseg = 1;
    while (*p) {
        if (*p == '"') in_quotes = !in_quotes;
        else if (*p == ';' && !in_quotes && nseg < 16) {
            *p = '\0';
            segments[nseg++] = p + 1;
        }
        p++;
    }

    for (int i = 0; i < nseg && !_interrupted; i++) {
        char *seg = skip_spaces(segments[i]);
        trim_right(seg);
        if (*seg) dispatch_single(seg);
    }
    _interrupted = false;
}

static void dispatch_single(char *line) {

    /* ---- Parse output redirection (>> or >) ---- */
    char *redir_file = NULL;
    bool  redir_append = false;
    {
        /* Scan for unquoted > or >> */
        bool inq = false;
        for (char *p = line; *p; p++) {
            if (*p == '"') inq = !inq;
            else if (!inq && p[0] == '>' && p[1] == '>') {
                *p = '\0';
                redir_file = skip_spaces(p + 2);
                redir_append = true;
                break;
            } else if (!inq && *p == '>') {
                *p = '\0';
                redir_file = skip_spaces(p + 1);
                redir_append = false;
                break;
            }
        }
        if (redir_file) trim_right(redir_file);
        trim_right(line);
    }

    bool use_more = parse_pipe_mode(line) > 0;
    if (line[0] == '\0') return;
    if (use_more) sys_more_set(true);

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

    if      (!strcmp(line, "help"))      cmd_help(arg);
    else if (!strcmp(line, "man"))       cmd_man(arg);
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
    else if (!strcmp(line, "status"))    cmd_status(arg);
    else if (!strcmp(line, "clear"))     cmd_clear();
    else if (!strcmp(line, "battery"))   cmd_battery();
    else if (!strcmp(line, "backlight")) cmd_backlight(arg);
    else if (!strcmp(line, "mount"))     cmd_mount();
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
    else if (!strcmp(line, "mv"))       cmd_rename(arg);
    else if (!strcmp(line, "sdinfo"))    cmd_sdinfo();
    else if (!strcmp(line, "sdread"))    cmd_sdread(arg);
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
    }
    else {
        uint32_t sf = lcd_get_fg();
        lcd_set_fg(LCD_RED);
        out_fmt("unknown: %s  (type 'help')\n", line);
        lcd_set_fg(sf);
    }

    if (use_more) {
        bool aborted = _sys_more_abort;
        sys_more_set(false);
        if (aborted) out_char('\n');
    }

    /* Finalize output redirection */
    if (redirecting) {
        _redir_active = false;
        char abs[CWD_MAX];
        resolve_abs(redir_file, abs);
        if (redir_append) {
            fat_append(abs, (const uint8_t *)_redir_buf, (uint32_t)_redir_pos);
        } else {
            fat_unlink(abs);
            fat_create(abs, (const uint8_t *)_redir_buf, (uint32_t)_redir_pos);
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
    if (!_lcd_ready || !_statusbar_enabled) return;

    /* Throttle redraws — the bar shows uptime in seconds, so refreshing more
       than once per second is wasted work and visible flicker. The two real
       call sites (per-prompt on RP2040, 2 s tick on RP2350-core1) both stay
       responsive enough at 1 Hz. */
    static uint32_t _last_draw_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (_last_draw_ms != 0 && (now - _last_draw_ms) < 1000) return;
    _last_draw_ms = now;

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

    /* Build status line: "[BAT%] CWD          H:MM:SS" */
    char bar[41];
    memset(bar, ' ', 40);
    bar[40] = '\0';

    int bat = kbd_battery_percent();
    bool charging = kbd_is_charging();

    /* Visual warning for very low battery */
    uint32_t bar_bg = LCD_GREY;
    if (bat >= 0 && bat < 15 && !charging) bar_bg = LCD_RED;
    lcd_set_bg(bar_bg);
    char lft[24];
    if (charging)
        snprintf(lft, sizeof lft, " [%d%%+]", bat);
    else if (bat >= 0 && bat < 15)
        snprintf(lft, sizeof lft, " [BAT %d%%!]", bat);
    else
        snprintf(lft, sizeof lft, " [%d%%]", bat);
    memcpy(bar, lft, strlen(lft));

    /* CWD in the middle */
    int cwd_start = (int)strlen(lft) + 1;
    int cwd_avail = 40 - cwd_start - 9; /* reserve 9 for time */
    if (cwd_avail > 0) {
        int clen = (int)strlen(_cwd);
        if (clen <= cwd_avail) {
            memcpy(bar + cwd_start, _cwd, (size_t)clen);
        } else {
            bar[cwd_start] = '.';
            bar[cwd_start + 1] = '.';
            memcpy(bar + cwd_start + 2, _cwd + clen - (cwd_avail - 2),
                   (size_t)(cwd_avail - 2));
        }
    }

    /* Uptime on the right */
    uint32_t secs = to_ms_since_boot(get_absolute_time()) / 1000;
    uint32_t h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    char rt[16];
    snprintf(rt, sizeof rt, "%lu:%02lu:%02lu", (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
    int rlen = (int)strlen(rt);
    memcpy(bar + 40 - rlen - 1, rt, (size_t)rlen);

    /* Render into last text row (row 19) */
    lcd_set_cursor(0, 19);
    for (int i = 0; i < 40; i++)
        lcd_draw_cell(i, 19, (char)bar[i], LCD_BLACK, bar_bg);

    /* Restore state */
    lcd_set_fg(save_fg);
    lcd_set_bg(save_bg);
    lcd_set_cursor(save_col, save_row);

    lcd_unlock();
}

#ifdef PICO_RP2350A
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
#ifdef PICO_RP2350A
    set_sys_clock_khz(150000, true);
#else
    set_sys_clock_khz(133000, true);
#endif
    stdio_init_all();
    sleep_ms(250);

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

    /* Match the official PicoCalc boot flow: give the SD card time to settle. */
    sleep_ms(1500);

    sd_result_t sd_r = sd_init();
    if (sd_r == SD_OK) {
        dmesg_log("SD: card initialized");
        if (fat_mount() == FAT_OK) {
            _fat_mounted = true;
            dmesg_log("FAT: mounted");
            alias_load();
            /* Load persistent settings (backlight, home, autorun, startup) and
               then restore the last-known cwd if it still exists. */
            app_init();
            /* Sync any chdir done by settings_apply_live back into _cwd. */
            strncpy(_cwd, _sys_cwd, CWD_MAX - 1);
            _cwd[CWD_MAX - 1] = '\0';
            cwd_restore();
            clock_restore();
            hist_load();
        } else {
            out_str("SD: FAT mount failed\n");
            dmesg_log("FAT: mount failed");
        }
    } else {
        out_str("SD: card not detected\n");
        dmesg_log("SD: card not detected");
    }

    out_str(BANNER);

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

    /* Enable status bar on last LCD row */
    _statusbar_enabled = true;
    _draw_status_bar();

#ifdef PICO_RP2350A
    /* Pico 2: launch core1 for background status bar refresh */
    multicore_launch_core1(_core1_entry);
#endif

    out_prompt();

    memset(line, 0, sizeof line);
    int hist_browse = -1;  /* -1 = not browsing history */
    int cursor = 0;        /* cursor position within line */

    /* Enable hardware watchdog: ~3 seconds, pause-on-debug */
    watchdog_enable(3000, true);

    /* Idle backlight dim — saves the LCD/CYW43 a little power */
    uint32_t last_activity_ms = sys_time_ms();
    bool dimmed = false;
    const uint32_t IDLE_DIM_MS = 60000U;   /* 60s idle -> dim */
    uint8_t saved_backlight = 255;

    for (;;) {
        watchdog_update();
        int ch = read_input();
        if (ch < 0) {
            uint32_t now = sys_time_ms();
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
            line[idx] = '\0';
            _interrupted = false;  /* clear stale Ctrl-C from previous cmd */
            if (idx > 0) dispatch(line);
            idx = 0;
            cursor = 0;
            line[0] = '\0';
            hist_browse = -1;
            kbd_clear_repeat();
            out_prompt();
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
            if (_hist_count == 0) continue;
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
                    int start = (match_idx > 0) ? match_idx - 1 : _hist_count - 1;
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
                    for (int i = _hist_count - 1; i >= 0; i--) {
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
            int next = (hist_browse < 0) ? _hist_count - 1 :
                       hist_browse - 1;
            if (next >= 0 && next < _hist_count) {
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
                if (hist_browse < _hist_count - 1) {
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
                    int next = (hist_browse < 0) ? _hist_count - 1 : hist_browse - 1;
                    if (next >= 0 && next < _hist_count) {
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
                        if (hist_browse < _hist_count - 1) {
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
