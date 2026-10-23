#pragma once
/*
 * apps/app_internal.h — Shared declarations for the application modules.
 *
 * Public command entry points live in apps.h; this header is private to the
 * apps/ layer and wires the individual command modules together.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apps.h"

/* Persistent settings state, owned by app_settings.c. */
typedef struct {
    int backlight;
    char notes_path[APP_TOKEN_MAX];
    char home_path[APP_TOKEN_MAX];
    char startup_app[APP_TOKEN_MAX];
    char autorun_path[APP_TOKEN_MAX];
    char theme[32];
    bool loaded;
} app_settings_state_t;

const app_settings_state_t *app_settings_get(void);
void app_settings_init(void);

/* App registry entry used by apps.c. */
typedef void (*app_handler_t)(const char *arg);

typedef struct {
    const char *name;
    app_handler_t handler;
} app_cmd_entry_t;

/* Registry lookup used by apps that open other commands. */
bool app_dispatch_named(const char *cmd, const char *arg);
bool app_known(const char *cmd);

/* Shared helpers implemented in app_util.c. */
int         write_text_file(const char *path, const char *buf, const char *label);
bool        is_leap_year(int year);
const char *month_name_local(int month);
int         build_month_local(void);
int         build_year_local(void);
int         build_day_local(void);
int         days_in_month_local(int month, int year);
int         day_of_week_local(int y, int m, int d);

/* Command handlers implemented across the app modules. */
void app_2048(const char *arg);
void app_base64(const char *arg);
void app_basename(const char *arg);
void app_basic(const char *arg);
void app_bookmarks(const char *arg);
void app_cal(const char *arg);
void app_calc(const char *arg);
void app_clock(const char *arg);
void app_coin(const char *arg);
void app_crc32(const char *arg);
void app_dashboard(const char *arg);
void app_dice(const char *arg);
void app_diff(const char *arg);
void app_dirname(const char *arg);
void app_edit(const char *arg);
void app_env(const char *arg);
void app_forth(const char *arg);
void app_games(const char *arg);
void app_guess(const char *arg);
void app_habits(const char *arg);
void app_habits_export(const char *arg);
void app_hello(const char *arg);
void app_home(const char *arg);
void app_id(const char *arg);
void app_journal(const char *arg);
void app_journal_export(const char *arg);
void app_life(const char *arg);
void app_lock(const char *arg);
void app_mandelbrot(const char *arg);
void app_minesweeper(const char *arg);
void app_notes(const char *arg);
void app_paint(const char *arg);
void app_pass(const char *arg);
void app_piano(const char *arg);
void app_planner(const char *arg);
void app_pomodoro(const char *arg);
void app_samples(const char *arg);
void app_script(const char *arg);
void app_seq(const char *arg);
void app_set(const char *arg);
void app_settings(const char *arg);
void app_sleep_ms(const char *arg);
void app_snake(const char *arg);
void app_sprite(const char *arg);
void app_stopwatch(const char *arg);
void app_strings(const char *arg);
void app_sysmon(const char *arg);
void app_tasks_export(const char *arg);
void app_tee(const char *arg);
void app_terminal(const char *arg);
void app_tetris(const char *arg);
void app_theme(const char *arg);
void app_tinyc(const char *arg);
void app_todo(const char *arg);
void app_units(const char *arg);
void app_watch(const char *arg);
void app_xmodem(const char *arg);
void app_xxd(const char *arg);
void app_yes(const char *arg);
