/*
 * apps/app_settings.c — Persistent settings and themes.
 */
#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "kbd.h"
#include "lcd.h"
#include "syscall.h"
#include "ui/ui.h"

#define SETTINGS_FILE    "/SETTINGS.CFG"
static app_settings_state_t g_settings;

static void settings_reset_defaults(void) {
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.backlight = 128;
    copy_cstr(g_settings.notes_path, sizeof g_settings.notes_path, "/NOTES.TXT");
    copy_cstr(g_settings.home_path, sizeof g_settings.home_path, "/");
    copy_cstr(g_settings.theme, sizeof g_settings.theme, "noir");
}

static void settings_set_path(char *dst, size_t dst_sz, const char *value, const char *fallback) {
    const char *v = skip_ws(value);
    if (!*v || ci_eq(v, "default")) {
        copy_cstr(dst, dst_sz, fallback ? fallback : "");
    } else if (ci_eq(v, "none") || ci_eq(v, "off")) {
        dst[0] = '\0';
    } else {
        app_make_abs(v, dst, dst_sz);
    }
}

static void settings_apply_pair(const char *key, const char *value) {
    const char *v = skip_ws(value ? value : "");
    if (!key || !*key) return;

    if (ci_eq(key, "backlight")) {
        int level = atoi(v);
        if (level < 0) level = 0;
        if (level > 255) level = 255;
        g_settings.backlight = level;
    } else if (ci_eq(key, "notes")) {
        settings_set_path(g_settings.notes_path, sizeof g_settings.notes_path, v, "/NOTES.TXT");
    } else if (ci_eq(key, "home")) {
        char tmp[APP_TOKEN_MAX];
        settings_set_path(tmp, sizeof tmp, v, "/");
        if (fat_is_dir(tmp) == FAT_OK) copy_cstr(g_settings.home_path, sizeof g_settings.home_path, tmp);
        else print_line("settings: home must be an existing directory");
    } else if (ci_eq(key, "startup")) {
        if (!*v || ci_eq(v, "none") || ci_eq(v, "shell")) {
            g_settings.startup_app[0] = '\0';
        } else {
            char token[APP_TOKEN_MAX];
            next_token(v, token, sizeof token);
            copy_cstr(g_settings.startup_app, sizeof g_settings.startup_app, token);
        }
    } else if (ci_eq(key, "autorun")) {
        settings_set_path(g_settings.autorun_path, sizeof g_settings.autorun_path, v, "");
    } else if (ci_eq(key, "theme")) {
        copy_cstr(g_settings.theme, sizeof g_settings.theme, v);
    }
}

/* Themes are owned by the UI layer; settings only persists the name. */
static bool theme_apply_by_name(const char *name) {
    if (ui_theme_set(name)) return true;
    return ui_theme_set_index(0); /* fall back to the default palette */
}

static void settings_apply_live(void) {
    if (g_settings.backlight < 0) g_settings.backlight = 0;
    if (g_settings.backlight > 255) g_settings.backlight = 255;
    kbd_set_backlight((uint8_t)g_settings.backlight);
    if (*g_settings.home_path) (void)sys_chdir(g_settings.home_path);
    (void)theme_apply_by_name(*g_settings.theme ? g_settings.theme : "noir");
}

static bool settings_load(bool verbose) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(SETTINGS_FILE, buf, APP_READ_MAX);
    if (n < 0) {
        g_settings.loaded = false;
        if (verbose) print_line("settings: using defaults");
        settings_apply_live();
        return false;
    }

    settings_reset_defaults();
    buf[n] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq++ = '\0';
        rtrim_in_place(s);
        settings_apply_pair(s, eq);
    }

    g_settings.loaded = true;
    settings_apply_live();
    if (verbose) print_line("settings: loaded");
    return true;
}

static bool settings_save(bool verbose) {
    char buf[APP_READ_MAX];
    int n = snprintf(buf, sizeof buf,
                     "# Mellivora PicoCalc settings\n"
                     "BACKLIGHT=%d\n"
                     "HOME=%s\n"
                     "NOTES=%s\n"
                     "STARTUP=%s\n"
                     "AUTORUN=%s\n"
                     "THEME=%s\n",
                     g_settings.backlight,
                     *g_settings.home_path ? g_settings.home_path : "/",
                     *g_settings.notes_path ? g_settings.notes_path : "/NOTES.TXT",
                     *g_settings.startup_app ? g_settings.startup_app : "none",
                     *g_settings.autorun_path ? g_settings.autorun_path : "none",
                     *g_settings.theme ? g_settings.theme : "noir");
    if (n < 0 || n >= (int)sizeof buf) {
        if (verbose) print_line("settings: buffer overflow while saving");
        return false;
    }
    if (sys_fwrite(SETTINGS_FILE, buf, (uint32_t)n) < 0) {
        if (verbose) print_line("settings: save failed");
        return false;
    }
    g_settings.loaded = true;
    if (verbose) ui_ok("settings saved");
    ui_notify("settings saved");
    return true;
}

static void settings_show(void) {
    char value[96];
    ui_title("Settings");

    snprintf(value, sizeof value, "%d", g_settings.backlight);
    ui_label_value("backlight", value);
    ui_label_value("home", *g_settings.home_path ? g_settings.home_path : "/");
    ui_label_value("notes", *g_settings.notes_path ? g_settings.notes_path : "/NOTES.TXT");
    ui_label_value("startup", *g_settings.startup_app ? g_settings.startup_app : "(none)");
    ui_label_value("autorun", *g_settings.autorun_path ? g_settings.autorun_path : "(none)");
    ui_label_value("theme", *g_settings.theme ? g_settings.theme : "noir");
    ui_line(UI_MUTED, "  settings set KEY VALUE");
}

void app_settings(const char *arg) {
    char cmd[APP_TOKEN_MAX];
    char key[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);

    if (!*cmd || ci_eq(cmd, "show")) {
        settings_show();
        return;
    }

    if (ci_eq(cmd, "save")) {
        settings_save(true);
        return;
    }
    if (ci_eq(cmd, "load")) {
        settings_load(true);
        settings_show();
        return;
    }
    if (ci_eq(cmd, "reset")) {
        settings_reset_defaults();
        settings_apply_live();
        settings_save(true);
        settings_show();
        return;
    }

    if (ci_eq(cmd, "set")) {
        s = next_token(s, key, sizeof key);
    } else {
        copy_cstr(key, sizeof key, cmd);
    }

    s = skip_ws(s);
    if (!*key || !*s) {
        print_line("usage: settings set [backlight|home|notes|startup|autorun] VALUE");
        return;
    }

    settings_apply_pair(key, s);
    settings_apply_live();
    settings_save(true);
    settings_show();
}

void app_set(const char *arg) {
    app_settings(arg);
}

/* theme — switch color scheme */
void app_theme(const char *arg) {
    const char *name = skip_ws(arg);

    if (!*name) {
        ui_line(UI_ACCENT, "Themes");
        ui_rule(LCD_COLS);
        for (int i = 0; i < ui_theme_count(); i++) {
            const char *n = ui_theme_name(i);
            bool cur = (i == ui_theme_current());
            ui_printf(cur ? UI_SUCCESS : UI_TEXT, "  %c %s\n",
                      cur ? '*' : ' ', n);
        }
        ui_line(UI_MUTED, "  theme NAME to apply");
        return;
    }

    if (strcmp(name, "default") == 0) name = "noir";

    if (!ui_theme_set(name)) {
        ui_error("theme", "unknown name; run 'theme' for the list");
        return;
    }

    copy_cstr(g_settings.theme, sizeof g_settings.theme, name);
    settings_save(false);
    ui_ok(name);
}

const app_settings_state_t *app_settings_get(void) {
    return &g_settings;
}

void app_settings_init(void) {
    settings_reset_defaults();
    settings_load(false);
    settings_apply_live();
}
