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
#define HIST_ENTRIES    16
#define HIST_ENTRY_SZ   LINE_BUF

#define BANNER \
    "  __  __     _ _ _\n" \
    " |  \\/  |___| | (_)___ ____ _ _ _ __ _\n" \
    " | |\\/| / -_) | | \\ V / _ \\ '_/ _` |\n" \
    " |_|  |_\\___|_|_|_|\\_/\\___/_| \\__,_|\n" \
    "  PicoCalc -- RP2040 native firmware\n\n"

/* ANSI escape sequences forwarded to the serial console */
#define ANSI_UP   "\x1b[A"
#define ANSI_DOWN "\x1b[B"

/* ------------------------------------------------------------------ */
/* Output helpers — mirror to both LCD and serial                       */
/* ------------------------------------------------------------------ */

static void out_char(char c) {
    putchar_raw(c);
    lcd_putc(c);
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

            /* Extract component */
            char comp[13];
            int ci = 0;
            while (*p && *p != '/') comp[ci++] = *p++;
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
                snprintf(canon + clen, CWD_MAX - clen, "/%s", comp);
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
    strncpy(_hist[_hist_head], line, HIST_ENTRY_SZ - 1);
    _hist[_hist_head][HIST_ENTRY_SZ - 1] = '\0';
    _hist_head = (_hist_head + 1) % HIST_ENTRIES;
    if (_hist_count < HIST_ENTRIES) _hist_count++;
}

/* Returns pointer to history entry (0 = oldest), or NULL if out of range. */
static const char *hist_get(int offset) {
    if (offset < 0 || offset >= _hist_count) return NULL;
    int idx = (_hist_head - _hist_count + offset + HIST_ENTRIES * 2) % HIST_ENTRIES;
    return _hist[idx];
}

/* ------------------------------------------------------------------ */
/* Shell commands                                                        */
/* ------------------------------------------------------------------ */

static absolute_time_t g_boot_time;

static void cmd_help(const char *arg) {
    const char *topic = arg ? arg : "";
    while (*topic == ' ') topic++;

    if (!*topic) {
        out_str("System commands:\n");
        out_str("  help [apps|fs|lang]  Show overview or a topic\n");
        out_str("  uname                OS and platform info\n");
        out_str("  uptime               Time since boot (ms)\n");
        out_str("  clear                Clear the screen\n");
        out_str("  battery              Battery charge level\n");
        out_str("  backlight <n>        Keyboard backlight 0-255\n");
        out_str("  reboot               Warm reboot\n");
        out_str("\nStorage commands:\n");
        out_str("  mount ls dir cd pwd cat touch write mkdir rm\n");
        out_str("  sdinfo sdread edit browse\n");
        out_str("\nApp commands:\n");
        out_str("  hello basename dirname seq head tail wc cut grep find pager rev sort\n");
        out_str("  hexdump od calc cp mv stat edit browse notes home dashboard sysmon\n");
        out_str("  samples clock cal script paint sleep id true false basic tcc\n");
        out_str("\nUse UP/DOWN for history, Ctrl-U to clear a line, and Ctrl-L to redraw.\n");
        return;
    }

    if (!strcmp(topic, "fs")) {
        out_str("Filesystem help:\n");
        out_str("  mount               Mount the SD FAT volume\n");
        out_str("  ls [path]           List a directory\n");
        out_str("  cd [path]           Change directory\n");
        out_str("  cat <path>          Show a text file\n");
        out_str("  touch <path>        Create an empty file\n");
        out_str("  write <p> <text>    Replace a file with text\n");
        out_str("  mkdir <path>        Create a directory\n");
        out_str("  rm <path>           Remove a file or empty directory\n");
        out_str("  cp SRC DST          Copy a file\n");
        out_str("  mv SRC DST          Move or rename a file\n");
        out_str("  stat PATH           Show compact file info\n");
        out_str("  edit FILE           Open the line editor for a text file\n");
        out_str("  browse [PATH]       Open the interactive file browser\n");
        out_str("  notes [FILE]        Open the notes editor using NOTES.TXT by default\n");
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
        out_str("  file: find cp mv stat edit browse notes samples basename dirname\n");
        out_str("  misc: hello seq sleep id true false calc home dashboard sysmon script paint clock cal\n");
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

static void dispatch(char *line) {
    /* Strip trailing whitespace */
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
        line[--len] = '\0';

    if (len == 0) return;

    /* Save to history */
    hist_push(line);

    /* Separate command from optional argument */
    char *arg = split_arg(line);

    if      (!strcmp(line, "help"))      cmd_help(arg);
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
    set_sys_clock_khz(133000, true);
    stdio_init_all();
    sleep_ms(500);  /* let USB enumerate */

    g_boot_time = get_absolute_time();
    strncpy(_sys_cwd, _cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';

    /* Init display first so the user sees something immediately */
    lcd_init();
    lcd_cls(LCD_BLACK);

    /* Init keyboard */
    kbd_init();
    kbd_set_backlight(128); /* 50% backlight */

    /* Auto-mount SD card */
    if (fat_mount() == FAT_OK) _fat_mounted = true;

    /* Print banner */
    out_str(BANNER);
    out_fmt("SD: %s\n", _fat_mounted ? "mounted" : "no card / not FAT");
    out_str("Type 'help' for commands.\n\n");
    out_prompt();

    char line[LINE_BUF];
    int  idx      = 0;         /* current cursor position in line */
    int  hist_pos = -1;        /* -1 = live edit; >=0 = browsing history */

    /* ANSI escape state machine: 0=normal, 1=got ESC, 2=got ESC[ */
    int esc_state = 0;

    for (;;) {
        sleep_ms(20);

        int ch = read_input();
        if (ch < 0) continue;

        /* ----- ANSI escape sequence handling ----- */
        if (esc_state == 1) {
            esc_state = (ch == '[') ? 2 : 0;
            continue;
        }
        if (esc_state == 2) {
            esc_state = 0;
            if (ch == 'A') ch = 0x10; /* remap UP    -> Ctrl-P */
            else if (ch == 'B') ch = 0x0E; /* remap DOWN  -> Ctrl-N */
            else continue;
        }
        if (ch == 0x1B) { esc_state = 1; continue; }

        /* ----- Enter ----- */
        if (ch == '\r' || ch == '\n') {
            out_char('\n');
            line[idx] = '\0';
            hist_pos  = -1;
            dispatch(line);
            idx = 0;
            out_prompt();
            continue;
        }

        /* ----- Backspace / DEL ----- */
        if ((ch == 0x08 || ch == 0x7F) && idx > 0) {
            idx--;
            out_str("\b \b");
            continue;
        }

        /* ----- Ctrl-C: cancel line ----- */
        if (ch == 0x03) {
            out_char('\n');
            idx      = 0;
            hist_pos = -1;
            out_prompt();
            continue;
        }

        /* ----- Ctrl-U: clear current line ----- */
        if (ch == 0x15) {
            while (idx-- > 0) out_str("\b \b");
            idx = 0;
            hist_pos = -1;
            line[0] = '\0';
            continue;
        }

        /* ----- Ctrl-L: clear and redraw ----- */
        if (ch == 0x0C) {
            cmd_clear();
            out_prompt();
            line[idx] = '\0';
            out_str(line);
            continue;
        }

        /* ----- Ctrl-P / UP: history previous ----- */
        if (ch == 0x10) {
            int next = (hist_pos < 0) ? _hist_count - 1 : hist_pos - 1;
            if (next < 0) continue;
            const char *entry = hist_get(next);
            if (!entry) continue;
            /* Erase current input */
            while (idx-- > 0) out_str("\b \b");
            idx = 0;
            strncpy(line, entry, LINE_BUF - 1);
            line[LINE_BUF - 1] = '\0';
            idx = (int)strlen(line);
            out_str(line);
            hist_pos = next;
            continue;
        }

        /* ----- Ctrl-N / DOWN: history next ----- */
        if (ch == 0x0E) {
            if (hist_pos < 0) continue;
            int next = hist_pos + 1;
            /* Erase current input */
            while (idx-- > 0) out_str("\b \b");
            idx = 0;
            if (next >= _hist_count) {
                /* Past end: clear line */
                line[0] = '\0';
                hist_pos = -1;
            } else {
                const char *entry = hist_get(next);
                if (entry) {
                    strncpy(line, entry, LINE_BUF - 1);
                    line[LINE_BUF - 1] = '\0';
                }
                hist_pos = next;
            }
            idx = (int)strlen(line);
            out_str(line);
            continue;
        }

        /* ----- Normal printable character ----- */
        if (isprint(ch) && idx + 1 < LINE_BUF) {
            hist_pos    = -1;
            line[idx++] = (char)ch;
            out_char((char)ch);
        }
    }
}
