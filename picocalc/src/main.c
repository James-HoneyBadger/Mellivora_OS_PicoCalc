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

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#define LINE_BUF        128
#define CWD_MAX         256
#define HIST_ENTRIES    32
#define HIST_ENTRY_SZ   LINE_BUF
#define ALIAS_MAX       16
#define ALIAS_NAME_SZ   16
#define ALIAS_VALUE_SZ  LINE_BUF
#define ALIAS_FILE      "/ALIASES.CFG"

#define BANNER \
    "Mellivora OS\n" \
    "PicoCalc ready\n\n"

/* ANSI escape sequences forwarded to the serial console */
#define ANSI_UP   "\x1b[A"
#define ANSI_DOWN "\x1b[B"

/* ------------------------------------------------------------------ */
/* Output helpers — mirror to both LCD and serial                       */
/* ------------------------------------------------------------------ */

static bool _lcd_ready = false;

static void out_char(char c) {
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
    char buf[256];
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

/*
 * Build an absolute path from cwd + input.
 * Result is stored in out (size CWD_MAX).
 * Handles ".." components.
 */
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

    /* Canonicalise: process each component */
    char canon[CWD_MAX];
    char *p = tmp;
    char *dst = canon;
    *dst = '\0';

    while (*p) {
        if (*p == '/') {
            /* Collapse multiple slashes */
            while (*p == '/') p++;
            if (*p == '\0') break;

            /* Extract component safely */
            char comp[CWD_MAX];
            int ci = 0;
            while (*p && *p != '/') {
                if (ci + 1 < CWD_MAX) comp[ci++] = *p;
                p++;
            }
            comp[ci] = '\0';

            if (strcmp(comp, ".") == 0) {
                /* skip */
            } else if (strcmp(comp, "..") == 0) {
                /* Go up: remove last component */
                char *last = strrchr(canon, '/');
                if (last && last > canon) *last = '\0';
                else canon[0] = '\0';
            } else {
                size_t clen = strlen(canon);
                if (clen + 1 < CWD_MAX) {
                    canon[clen++] = '/';
                    const char *src = comp;
                    while (*src && clen + 1 < CWD_MAX) canon[clen++] = *src++;
                    canon[clen] = '\0';
                }
            }
        } else {
            p++;
        }
    }

    if (canon[0] == '\0') strcpy(canon, "/");

    strncpy(out, canon, CWD_MAX - 1);
    out[CWD_MAX - 1] = '\0';
}

static void out_prompt(void) {
    out_fmt("PicoLair:%s> ", _cwd);
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

    char buf[1024];
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
        out_str("  uptime               Time since boot (ms)\n");
        out_str("  clear                Clear the screen\n");
        out_str("  battery              Battery charge level\n");
        out_str("  backlight <n>        Keyboard backlight 0-255\n");
        out_str("  reboot               Warm reboot\n");
        out_str("\nStorage commands:\n");
        out_str("  mount ls dir cd pwd cat touch write mkdir rm\n");
        out_str("  sdinfo sdread edit browse tree du df\n");
        out_str("\nApp commands:\n");
        out_str("  hello basename dirname seq head tail wc cut grep find tree du df pager rev sort\n");
        out_str("  hexdump od hexedit calc cp mv stat edit browse notes journal habits todo planner\n");
        out_str("  bookmarks games snake dice coin guess sprite terminal home dashboard sysmon\n");
        out_str("  samples clock cal script paint settings set sleep id true false basic tcc\n");
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

static void cmd_uname(void) {
    out_str("Mellivora OS - PicoCalc target (RP2040)\n");
    out_fmt("  LCD:  ILI9488 %dx%d via SPI1\n", LCD_WIDTH, LCD_HEIGHT);
    out_str("  KBD:  STM32 co-proc via I2C1\n");
    out_str("  SD:   SPI0 block device\n");
    out_str("  UART: USB CDC + UART0\n");
}

static void cmd_uptime(void) {
    int64_t us = absolute_time_diff_us(g_boot_time, get_absolute_time());
    out_fmt("%lld ms\n", us / 1000LL);
}

static void cmd_battery(void) {
    int pct = kbd_battery_percent();
    if (pct < 0) out_str("Battery info unavailable\n");
    else         out_fmt("Battery: %d%%\n", pct);
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
    if (is_dir) out_fmt("  [DIR]  %s\n", name);
    else        out_fmt("  %8lu  %s\n", (unsigned long)size, name);
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
    if (r != FAT_OK) { out_fmt("cat: %s\n", fat_result_str(r)); return; }
    uint8_t buf[64];
    int32_t n;
    while ((n = fat_read(&f, buf, sizeof buf)) > 0) {
        for (int32_t i = 0; i < n; i++) out_char((char)buf[i]);
    }
    if (n != FAT_ERR_EOF && n < 0)
        out_fmt("\nread error: %s\n", fat_result_str((fat_result_t)n));
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
    if (!arg || !*arg) { out_str("usage: write <path> <text>\n"); return; }

    char tmp[LINE_BUF];
    strncpy(tmp, arg, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = '\0';

    char *text = tmp;
    while (*text && *text != ' ') text++;
    if (*text == '\0') { out_str("usage: write <path> <text>\n"); return; }
    *text++ = '\0';
    while (*text == ' ') text++;

    char abs[CWD_MAX];
    resolve_abs(tmp, abs);
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

static void cmd_rm(const char *arg) {
    if (!_fat_mounted) { out_str("Not mounted. Run 'mount' first.\n"); return; }
    if (!arg || !*arg) { out_str("usage: rm <path>\n"); return; }
    char abs[CWD_MAX];
    resolve_abs(arg, abs);
    fat_result_t r = fat_unlink(abs);
    if (r != FAT_OK) out_fmt("rm: %s: %s\n", abs, fat_result_str(r));
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

/* ------------------------------------------------------------------ */
/* Command dispatch                                                     */
/* ------------------------------------------------------------------ */

/* Split "cmd arg" into cmd and (optional) arg in-place. */
static char *split_arg(char *line) {
    char *p = line;
    while (*p && *p != ' ') p++;
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

    (void)alias_expand_line(line, LINE_BUF);

    /* Save to history */
    hist_push(line);

    bool use_more = parse_pipe_mode(line) > 0;
    if (line[0] == '\0') return;
    if (use_more) sys_more_set(true);

    /* Separate command from optional argument */
    char *arg = split_arg(line);

    if      (!strcmp(line, "help"))      cmd_help(arg);
    else if (!strcmp(line, "man"))       cmd_man(arg);
    else if (!strcmp(line, "history"))   cmd_history(arg);
    else if (!strcmp(line, "alias"))     cmd_alias(arg);
    else if (!strcmp(line, "unalias"))   cmd_unalias(arg);
    else if (!strcmp(line, "uname"))     cmd_uname();
    else if (!strcmp(line, "uptime"))    cmd_uptime();
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
    else if (!strcmp(line, "sdinfo"))    cmd_sdinfo();
    else if (!strcmp(line, "sdread"))    cmd_sdread(arg);
    else if (!strcmp(line, "reboot"))    cmd_reboot();
    else if (app_run(line, arg)) {
        strncpy(_cwd, _sys_cwd, CWD_MAX - 1);
        _cwd[CWD_MAX - 1] = '\0';
    }
    else out_fmt("unknown: %s  (type 'help')\n", line);

    if (use_more) {
        bool aborted = _sys_more_abort;
        sys_more_set(false);
        if (aborted) out_char('\n');
    }
}

/* ------------------------------------------------------------------ */
/* Input: merged keyboard + USB serial + ANSI arrow key handling        */
/* ------------------------------------------------------------------ */

/*
 * Returns the next character from either the physical keyboard or USB
 * serial.  Non-blocking; returns -1 if nothing is available.
 */
static int read_input(void) {
    int ch = getchar_timeout_us(0);
    if (ch != PICO_ERROR_TIMEOUT) return ch;
    return kbd_getc();
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    char line[LINE_BUF];
    int idx = 0;

    set_sys_clock_khz(133000, true);
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

    if (sd_init() == SD_OK && fat_mount() == FAT_OK) {
        _fat_mounted = true;
        alias_load();
    }

    out_str(BANNER);
    out_prompt();

    memset(line, 0, sizeof line);

    for (;;) {
        int ch = read_input();
        if (ch < 0) {
            sleep_ms(20);
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            out_char('\n');
            line[idx] = '\0';
            if (idx > 0) dispatch(line);
            idx = 0;
            line[0] = '\0';
            out_prompt();
            continue;
        }

        if (ch == 0x08 || ch == 0x7F) {
            if (idx > 0) {
                idx--;
                line[idx] = '\0';
                out_str("\b \b");
            }
            continue;
        }

        if (ch == 0x1B || ch == 0xB4 || ch == 0xB5 || ch == 0xB6 || ch == 0xB7) {
            continue;
        }

        if (isprint((unsigned char)ch) && idx < LINE_BUF - 1) {
            line[idx++] = (char)ch;
            line[idx] = '\0';
            out_char((char)ch);
        }
    }
}
