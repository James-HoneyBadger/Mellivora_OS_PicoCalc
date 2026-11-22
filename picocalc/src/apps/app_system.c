/*
 * apps/app_system.c — Calendar, clock, dashboard, launcher, and timers.
 */
#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "core/launcher.h"
#include "kbd.h"
#include "lcd.h"
#include "mapp.h"
#include "syscall.h"
#include "ui/ui.h"

void app_terminal(const char *arg) {
    (void)arg;
    sys_clear();
    sys_print("Raw terminal mode\n\n");
    sys_print("Everything typed is echoed directly. Press Ctrl-X or Ctrl-C to exit.\n\n");
    while (1) {
        int ch = sys_getchar();
        if (ch == 0x18 || ch == 0x03) break;
        sys_putchar((char)ch);
    }
    sys_clear();
}

void app_cal(const char *arg) {
    char m_tok[16];
    char y_tok[16];
    const char *s = next_token(arg, m_tok, sizeof m_tok);
    next_token(s, y_tok, sizeof y_tok);

    int month = *m_tok ? atoi(m_tok) : build_month_local();
    int year = *y_tok ? atoi(y_tok) : build_year_local();

    if (month < 1 || month > 12 || year < 1) {
        print_line("usage: cal [month] [year]");
        return;
    }

    char line[80];
    snprintf(line, sizeof line, "%s %d", month_name_local(month), year);
    print_line(line);
    print_line("Su Mo Tu We Th Fr Sa");

    int dow = day_of_week_local(year, month, 1);
    int dim = days_in_month_local(month, year);
    char row[64];
    int pos = 0;
    row[0] = '\0';

    for (int i = 0; i < dow; i++) pos += snprintf(row + pos, sizeof row - pos, "   ");
    for (int d = 1; d <= dim; d++) {
        pos += snprintf(row + pos, sizeof row - pos, "%2d ", d);
        if ((dow + d) % 7 == 0 || d == dim) {
            row[pos] = '\0';
            print_line(row);
            pos = 0;
            row[0] = '\0';
        }
    }
    print_line("No RTC yet; using the build date unless month and year are provided.");
}

void app_clock(const char *arg) {
    (void)arg;
    while (1) {
        uint32_t uptime = sys_time_ms() / 1000U;
        uint32_t hh = (uptime / 3600U) % 24U;
        uint32_t mm = (uptime / 60U) % 60U;
        uint32_t ss = uptime % 60U;
        int ref_month = build_month_local();
        int ref_year = build_year_local();

        sys_clear();
        sys_print("Mellivora clock\n\n");
        char line[96];
        snprintf(line, sizeof line, "Uptime clock: %02lu:%02lu:%02lu\n",
                 (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
        sys_print(line);
        snprintf(line, sizeof line, "Date ref: %s %d build default\n\n",
                 month_name_local(ref_month), ref_year);
        sys_print(line);
        sys_print("Press c for calendar, q to quit, any other key to refresh.\n");

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'c' || ch == 'C') app_cal(NULL);
    }
    sys_clear();
}

void app_dashboard(const char *arg) {
    (void)arg;
    while (1) {
        int pct = kbd_battery_percent();
        uint32_t uptime = sys_time_ms();

        sys_clear();
        const app_settings_state_t *settings = app_settings_get();
        char line[128];
        snprintf(line, sizeof line, "%s\n\n",
                 *settings->device_name ? settings->device_name : "Mellivora dashboard");
        sys_print(line);

        snprintf(line, sizeof line, "Battery: %s\n", (pct >= 0) ? "available" : "unavailable");
        sys_print(line);
        if (pct >= 0) {
            snprintf(line, sizeof line, "Charge:  %d%%\n", pct);
            sys_print(line);
        }
        snprintf(line, sizeof line, "Uptime:  %lu ms\n", (unsigned long)uptime);
        sys_print(line);
        snprintf(line, sizeof line, "Path:    %.96s\n", _sys_cwd);
        sys_print(line);
        snprintf(line, sizeof line, "Screen:  %dx%d chars\n", sys_getscreenw(), sys_getscreenh());
        sys_print(line);

        sys_print("\nShortcuts:\n");
        sys_print("  b browse files\n");
        sys_print("  n notes\n");
        sys_print("  j journal\n");
        sys_print("  u habits\n");
        sys_print("  p planner\n");
        sys_print("  m bookmarks\n");
        sys_print("  g games\n");
        sys_print("  y sprite editor\n");
        sys_print("  z terminal\n");
        sys_print("  c calculator\n");
        sys_print("  s samples\n");
        sys_print("  t clock and calendar\n");
        sys_print("  h home launcher\n");
        sys_print("  r refresh\n");
        sys_print("  q quit\n\n");
        sys_print("Choice: ");

        int ch = sys_getchar();
        sys_putchar('\n');
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'r' || ch == 'R') continue;
        if (ch == 'b' || ch == 'B') app_browse(NULL);
        else if (ch == 'n' || ch == 'N') app_notes(NULL);
        else if (ch == 'j' || ch == 'J') app_journal(NULL);
        else if (ch == 'u' || ch == 'U') app_habits(NULL);
        else if (ch == 'p' || ch == 'P') app_planner(NULL);
        else if (ch == 'm' || ch == 'M') app_bookmarks(NULL);
        else if (ch == 'g' || ch == 'G') app_games(NULL);
        else if (ch == 'y' || ch == 'Y') app_sprite(NULL);
        else if (ch == 'z' || ch == 'Z') app_terminal(NULL);
        else if (ch == 'c' || ch == 'C') app_calc(NULL);
        else if (ch == 's' || ch == 'S') app_samples(NULL);
        else if (ch == 't' || ch == 'T') app_clock(NULL);
        else if (ch == 'h' || ch == 'H') app_home(NULL);
    }
    sys_clear();
}

/* ---- Home launcher ---------------------------------------------------- */

#define RECENTS_PATH "/RECENTS.TXT"

/* Built-in entries. The command is what the launcher dispatches, so it must
   match the app table in apps.c. */
static const launcher_item_t _launcher_builtins[] = {
    { "Dashboard", "dashboard", LAUNCHER_CAT_SYSTEM, "*" },
    { "File browser", "browse", LAUNCHER_CAT_FILES, "/" },
    { "Notes", "notes", LAUNCHER_CAT_NOTES, "n" },
    { "Todo", "todo", LAUNCHER_CAT_NOTES, "t" },
    { "Planner", "planner", LAUNCHER_CAT_NOTES, "p" },
    { "Journal", "journal", LAUNCHER_CAT_NOTES, "j" },
    { "Habits", "habits", LAUNCHER_CAT_NOTES, "h" },
    { "Bookmarks", "bookmarks", LAUNCHER_CAT_NOTES, "b" },
    { "Calculator", "calc", LAUNCHER_CAT_TOOLS, "+" },
    { "Sprite editor", "sprite", LAUNCHER_CAT_TOOLS, "#" },
    { "Samples", "samples", LAUNCHER_CAT_TOOLS, "?" },
    { "System info", "sysinfo", LAUNCHER_CAT_SYSTEM, "!" },
    { "Settings", "settings", LAUNCHER_CAT_SYSTEM, "=" },
    { "BASIC", "basic", LAUNCHER_CAT_LANG, ">" },
    { "Tiny C", "tcc", LAUNCHER_CAT_LANG, ">" },
    { "Forth", "forth", LAUNCHER_CAT_LANG, ">" },
    { "Games", "games", LAUNCHER_CAT_GAMES, "o" },
    { "Clock", "clock", LAUNCHER_CAT_SYSTEM, ":" },
    { "Terminal", "term", LAUNCHER_CAT_HARDWARE, "|" },
    { "Hardware", "hw", LAUNCHER_CAT_HARDWARE, "~" },
    { "Packages", "mpkg", LAUNCHER_CAT_HARDWARE, "%" },
};

static launcher_recents_t _recents;

static void _recents_load(void) {
    launcher_recents_clear(&_recents);
    char text[256];
    int n = sys_fread(RECENTS_PATH, text, sizeof text);
    if (n > 0) launcher_recents_parse(&_recents, text, (size_t)n);
}

static void _recents_save(void) {
    char text[256];
    size_t len = launcher_recents_serialize(&_recents, text, sizeof text);
    if (len > 0) (void)write_text_file(RECENTS_PATH, text, "recents");
}

/* Dispatch a launcher command: an app-table name first, then a file in
   /apps, so both built-ins and external modules launch from one list. */
static bool _dispatch_app(const char *command) {
    if (app_run(command, "")) return true;
    return mapp_run(command);
}

void app_home(const char *arg) {
    (void)arg;
    static launcher_item_t items[LAUNCHER_MAX_ITEMS];
    static launcher_hit_t hits[LAUNCHER_MAX_ITEMS];
    static ui_launcher_row_t rows[LAUNCHER_MAX_ITEMS];

    const size_t builtin_count = sizeof _launcher_builtins / sizeof _launcher_builtins[0];
    size_t count = 0;
    for (size_t i = 0; i < builtin_count && count < LAUNCHER_MAX_ITEMS; i++)
        items[count++] = _launcher_builtins[i];

    /* Append everything discovered in /apps. */
    const int discovered = mapp_discover();
    const uint32_t app_count = discovered > 0 ? (uint32_t)discovered : 0;
    bool truncated = false;
    for (uint32_t i = 0; i < app_count; i++) {
        const mapp_app_t *app = mapp_catalog_get(i);
        if (!app) continue;
        if (count >= LAUNCHER_MAX_ITEMS) {
            truncated = true;
            break;
        }
        launcher_item_t *slot = &items[count++];
        snprintf(slot->title, sizeof slot->title, "%.*s", (int)sizeof slot->title - 1, app->title);
        snprintf(slot->command, sizeof slot->command, "%.*s",
                 (int)sizeof slot->command - 1, app->filename);
        /* Trim the extension so the command matches `mapp_run`. */
        char *dot = strrchr(slot->command, '.');
        if (dot) *dot = '\0';
        slot->category = LAUNCHER_CAT_APPS;
        slot->icon = ".";
    }

    if (discovered < 0) ui_warn("SD /apps unavailable");
    else if (mapp_catalog_truncated() || truncated)
        ui_warn("launcher: some apps are not shown");

    _recents_load();

    char query[24];
    query[0] = '\0';
    int selected = 0;

    for (;;) {
        size_t shown = launcher_filter(items, count, query, hits, LAUNCHER_MAX_ITEMS);

        /* With no query, recently used entries float to the top. With a
           query, the fuzzy ranking is what the user asked for. */
        if (!*query) {
            launcher_hit_t ordered[LAUNCHER_MAX_ITEMS];
            size_t used = 0;
            for (uint8_t r = 0; r < _recents.count; r++) {
                for (size_t i = 0; i < shown; i++) {
                    if (strcmp(items[hits[i].index].command, _recents.entry[r].command) == 0) {
                        ordered[used++] = hits[i];
                        break;
                    }
                }
            }
            for (size_t i = 0; i < shown && used < LAUNCHER_MAX_ITEMS; i++) {
                bool already = false;
                for (size_t j = 0; j < used; j++) {
                    if (ordered[j].index == hits[i].index) {
                        already = true;
                        break;
                    }
                }
                if (!already) ordered[used++] = hits[i];
            }
            memcpy(hits, ordered, used * sizeof ordered[0]);
            shown = used;
        }

        size_t row_count = 0;
        for (size_t i = 0; i < shown && row_count < LAUNCHER_MAX_ITEMS; i++) {
            const launcher_item_t *item = &items[hits[i].index];
            rows[row_count].title = item->title;
            rows[row_count].icon = item->icon;
            rows[row_count].tag = launcher_category_tag(item->category);
            row_count++;
        }
        if (row_count == 0) {
            /* Nothing matches: keep the query visible so the user can edit
               it, with a single explanatory row. */
            rows[0].title = "no match";
            rows[0].icon = " ";
            rows[0].tag = "";
            row_count = 1;
        }

        bool query_changed = false;
        ui_launcher_t view = {
            .row = rows,
            .count = (int)row_count,
            .query = query,
            .selected = selected,
            .query_changed = &query_changed,
            .query_out = query,
            .query_capacity = sizeof query,
        };
        int choice = ui_launcher(&view);
        selected = view.selected;

        if (query_changed) {
            /* The row set changed under the cursor; start from the best
               match rather than an index that may no longer exist. */
            selected = 0;
            continue;
        }
        if (choice < 0) break;
        /* The placeholder row is not an app. */
        if (shown == 0) continue;

        const launcher_item_t *chosen = &items[hits[choice].index];
        char command[LAUNCHER_COMMAND_MAX];
        snprintf(command, sizeof command, "%s", chosen->command);
        if (launcher_recents_push(&_recents, command)) _recents_save();

        sys_clear();
        if (!_dispatch_app(command)) ui_error("home", "that app is not available");
        sys_clear();
        /* Back in the launcher: forget the query but keep the selection. */
        query[0] = '\0';
    }

    sys_clear();
}

void app_sysmon(const char *arg) {
    (void)arg;
    while (1) {
        int pct = kbd_battery_percent();
        uint32_t uptime_ms = sys_time_ms();
        uint32_t uptime_s = uptime_ms / 1000U;

        sys_clear();
        const app_settings_state_t *settings = app_settings_get();
        char line[128];
        snprintf(line, sizeof line, "%s - system\n\n",
                 *settings->device_name ? settings->device_name : "Mellivora");
        sys_print(line);

        snprintf(line, sizeof line, "Battery: %s\n", (pct >= 0) ? "present" : "unknown");
        sys_print(line);
        if (pct >= 0) {
            snprintf(line, sizeof line, "Charge:  %d%%\n", pct);
            sys_print(line);
        }
        snprintf(line, sizeof line, "Uptime:  %lu ms (%lu s)\n",
                 (unsigned long)uptime_ms, (unsigned long)uptime_s);
        sys_print(line);
        snprintf(line, sizeof line, "CWD:     %.96s\n", _sys_cwd);
        sys_print(line);
        snprintf(line, sizeof line, "Cols:    %d\nRows: %d\n", sys_getscreenw(), sys_getscreenh());
        sys_print(line);
        sys_print("\nKeys: r refresh, d dashboard, q quit\n");

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'd' || ch == 'D') app_dashboard(NULL);
    }
    sys_clear();
}

void app_sleep_ms(const char *arg) {
    const char *s = skip_ws(arg);
    char *endptr = NULL;
    long ms = strtol(s, &endptr, 10);
    if (!*s || endptr == s || ms < 0) {
        print_line("usage: sleep MS");
        return;
    }
    sys_sleep((uint32_t)ms);
}

void app_id(const char *arg) {
    (void)arg;
    print_line("uid=0(picocalc) gid=0(picocalc) groups=0(picocalc)");
}

/* Stopwatch — Space=start/stop, L=lap, R=reset, Q=quit */
void app_stopwatch(const char *arg) {
    (void)arg;
    bool running = false;
    uint32_t start_ms = 0;
    uint32_t accum_ms = 0;
    int laps = 0;
    sys_clear();
    sys_print("Mellivora stopwatch\n");
    sys_print("Space=start/stop  L=lap  R=reset  Q=quit\n\n");
    for (;;) {
        uint32_t now = running ? (sys_time_ms() - start_ms + accum_ms) : accum_ms;
        uint32_t mm = now / 60000U;
        uint32_t ss = (now / 1000U) % 60U;
        uint32_t ms = now % 1000U;
        char line[40];
        snprintf(line, sizeof line, "\r%02lu:%02lu.%03lu  %s   ",
                 (unsigned long)mm, (unsigned long)ss, (unsigned long)ms,
                 running ? "RUN " : "STOP");
        sys_print(line);
        int ch = kbd_getc();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == ' ') {
            if (running) { accum_ms = now; running = false; }
            else         { start_ms = sys_time_ms(); running = true; }
        } else if (ch == 'r' || ch == 'R') {
            running = false; accum_ms = 0; laps = 0;
            sys_clear();
            sys_print("Mellivora stopwatch\n");
            sys_print("Space=start/stop  L=lap  R=reset  Q=quit\n\n");
        } else if (ch == 'l' || ch == 'L') {
            laps++;
            sys_putchar('\n');
            char ln[48];
            snprintf(ln, sizeof ln, "  lap %2d  %02lu:%02lu.%03lu\n",
                     laps,
                     (unsigned long)mm, (unsigned long)ss, (unsigned long)ms);
            sys_print(ln);
        }
        sleep_ms(50);
    }
    sys_putchar('\n');
}

/* Pomodoro timer — 25 min work / 5 min break, Q quits early. */
void app_pomodoro(const char *arg) {
    int work_min = 25, break_min = 5;
    if (arg && *arg) {
        char *end = NULL;
        long w = strtol(arg, &end, 10);
        if (w >= 1 && w <= 120) work_min = (int)w;
        if (end && *end) {
            long b = strtol(end, &end, 10);
            if (b >= 1 && b <= 60) break_min = (int)b;
        }
    }
    for (int round = 1;; round++) {
        for (int phase = 0; phase < 2; phase++) {
            int total = (phase == 0 ? work_min : break_min) * 60;
            const char *label = (phase == 0) ? "WORK " : "BREAK";
            sys_clear();
            char hdr[64];
            snprintf(hdr, sizeof hdr,
                     "Pomodoro #%d  %s  %d min total\nQ=quit\n\n",
                     round, label, total / 60);
            sys_print(hdr);
            uint32_t start = sys_time_ms();
            for (;;) {
                uint32_t elapsed = (sys_time_ms() - start) / 1000U;
                if ((int)elapsed >= total) break;
                int rem = total - (int)elapsed;
                char line[40];
                snprintf(line, sizeof line, "\r%s  %02d:%02d remaining ",
                         label, rem / 60, rem % 60);
                sys_print(line);
                int ch = kbd_getc();
                if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                    sys_putchar('\n');
                    return;
                }
                sleep_ms(250);
            }
            sys_putchar('\n');
            /* Beep three times via backlight flash */
            for (int i = 0; i < 3; i++) {
                kbd_set_backlight(0);   sleep_ms(120);
                kbd_set_backlight(255); sleep_ms(120);
            }
            sys_print(phase == 0 ? "** Take a break! **\n"
                                 : "** Back to work! **\n");
            sleep_ms(1500);
        }
    }
}
