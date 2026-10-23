/*
 * apps.c — Application registry for Mellivora OS PicoCalc
 *
 * Owns the name -> handler table and startup hooks. The commands
 * themselves live in apps/app_*.c.
 */

#include "apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"

void app_init(void) {
    app_settings_init();
}

void app_boot(void) {
    const app_settings_state_t *st = app_settings_get();
    if (*st->autorun_path) {
        print_line("Running autorun script...");
        app_script(st->autorun_path);
    }
    if (*st->startup_app) {
        char out[96];
        snprintf(out, sizeof out, "Opening startup app: %.48s", st->startup_app);
        print_line(out);
        (void)app_dispatch_named(st->startup_app, "");
    }
}

void app_noop(const char *arg) {
    (void)arg;
}

static int _app_cmp(const void *a, const void *b) {
    return strcmp(((const app_cmd_entry_t *)a)->name,
                  ((const app_cmd_entry_t *)b)->name);
}

bool app_dispatch_named(const char *cmd, const char *arg) {
    /* Table is sorted lazily on first call so future maintainers can append
       entries in any order. After sorting we binary-search every dispatch,
       turning a ~110-strcmp linear scan into ~7 comparisons. */
    static app_cmd_entry_t table[] = {
        {"hello", app_hello}, {"basename", app_basename}, {"dirname", app_dirname},
        {"seq", app_seq}, {"head", app_head}, {"tail", app_tail},
        {"wc", app_wc}, {"cut", app_cut}, {"grep", app_grep},
        {"find", app_find}, {"tree", app_tree}, {"du", app_du},
        {"df", app_df}, {"disk", app_df}, {"space", app_df},
        {"pager", app_pager}, {"less", app_pager}, {"more", app_pager}, {"rev", app_rev},
        {"sort", app_sort}, {"uniq", app_uniq}, {"hexdump", app_hexdump}, {"od", app_od},
        {"hexedit", app_hexedit}, {"hex", app_hexedit},
        {"calc", app_calc}, {"cp", app_cp}, {"mv", app_mv},
        {"stat", app_stat}, {"edit", app_edit}, {"bedit", app_edit},
        {"browse", app_browse}, {"files", app_browse}, {"notes", app_notes},
        {"memo", app_notes}, {"journal", app_journal}, {"diary", app_journal},
        {"habits", app_habits}, {"habit", app_habits},
        {"bookmarks", app_bookmarks}, {"bookmark", app_bookmarks},
        {"favs", app_bookmarks}, {"favorites", app_bookmarks},
        {"tasks-export", app_tasks_export}, {"journal-export", app_journal_export},
        {"habits-export", app_habits_export},
        {"games", app_games}, {"game", app_games}, {"dice", app_dice},
        {"coin", app_coin}, {"guess", app_guess}, {"snake", app_snake},
        {"sprite", app_sprite}, {"terminal", app_terminal}, {"term", app_terminal},
        {"tty", app_terminal}, {"serial", app_terminal},
        {"home", app_home}, {"launcher", app_home},
        {"dashboard", app_dashboard}, {"status", app_dashboard},
        {"sysmon", app_sysmon}, {"monitor", app_sysmon},
        {"settings", app_settings}, {"set", app_set}, {"todo", app_todo}, {"tasks", app_todo},
        {"planner", app_planner}, {"agenda", app_planner}, {"plan", app_planner},
        {"samples", app_samples}, {"demos", app_samples},
        {"clock", app_clock}, {"cal", app_cal}, {"calendar", app_cal},
        {"basic", app_basic}, {"tcc", app_tinyc}, {"tinyc", app_tinyc},
        {"script", app_script}, {"paint", app_paint}, {"sleep", app_sleep_ms},
        {"tr", app_tr}, {"sed", app_sed}, {"md", app_md}, {"markdown", app_md},
        {"id", app_id}, {"true", app_noop}, {"false", app_noop},
        {"stopwatch", app_stopwatch}, {"timer", app_stopwatch},
        {"pomodoro", app_pomodoro},
        /* New utilities */
        {"watch", app_watch}, {"diff", app_diff}, {"env", app_env},
        {"lock", app_lock}, {"xxd", app_xxd}, {"strings", app_strings},
        {"yes", app_yes}, {"tee", app_tee},
        {"base64", app_base64}, {"crc32", app_crc32},
        {"units", app_units}, {"pass", app_pass}, {"password", app_pass},
        /* New programs */
        {"life", app_life}, {"tetris", app_tetris},
        {"mandelbrot", app_mandelbrot}, {"fractal", app_mandelbrot},
        {"piano", app_piano}, {"forth", app_forth},
        {"minesweeper", app_minesweeper}, {"mines", app_minesweeper},
        {"2048", app_2048},
        /* System */
        {"xmodem", app_xmodem}, {"theme", app_theme},
    };
    static const size_t n = sizeof table / sizeof table[0];
    static bool sorted = false;
    if (!sorted) {
        qsort(table, n, sizeof table[0], _app_cmp);
        sorted = true;
    }

    app_cmd_entry_t key = { cmd, NULL };
    app_cmd_entry_t *hit = bsearch(&key, table, n, sizeof table[0], _app_cmp);
    if (hit) { hit->handler(arg); return true; }
    return false;
}

bool app_run(const char *cmd, const char *arg) {
    if (!cmd || !*cmd) return false;
    return app_dispatch_named(cmd, arg);
}
