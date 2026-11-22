/*
 * apps/app_setup.c — first-boot setup wizard.
 *
 * A short, keyboard-driven walkthrough that names the device, picks a
 * palette, sets the backlight, and optionally configures Wi-Fi. All of the
 * decision logic lives in core/onboarding.c; this file only draws screens
 * and reads keys.
 *
 * `setup` runs it again at any time. On a card with no /SETTINGS.CFG it runs
 * automatically during app_boot().
 */

#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "apps/app_internal.h"
#include "core/onboarding.h"
#include "kbd.h"
#include "lcd.h"
#include "syscall.h"
#include "ui/ui.h"

#ifdef PICO_CYW43_SUPPORTED
#include "netapps.h"
#endif

#define SETUP_MARKER "/SETUP.DONE"

static onboarding_state_t _state;
static onboarding_plan_t _plan;

static void _draw_lines(const char *title, const char *progress, const char *const *lines,
                        int count) {
    ui_clear();
    ui_header(title, progress);
    for (int i = 0; i < count && i < LCD_ROWS - 4; i++) ui_line(UI_TEXT, lines[i]);
    ui_footer("enter continue  esc skip");
}

/* Draw the phrase the user is typing, with a caret. */
static void _draw_name_editor(const char *progress) {
    char line[LCD_COLS + 1];
    snprintf(line, sizeof line, "> %s_", _state.name);
    const char *const lines[] = {
        "",
        "What should this device be called?",
        "",
        line,
        "",
        "Letters, digits, spaces, - _ and '",
    };
    _draw_lines("Name your PicoCalc", progress, lines, (int)(sizeof lines / sizeof lines[0]));
}

static void _step_welcome(void) {
    const char *const lines[] = {
        "",
        "Welcome to Mellivora OS.",
        "",
        "A few questions and you are ready.",
        "Press Esc at any time to keep",
        "the defaults and go straight to",
        "the shell.",
    };
    char progress[12];
    onboarding_progress(ONBOARDING_STEP_WELCOME, &_plan, progress, sizeof progress);
    _draw_lines("Setup", progress, lines, (int)(sizeof lines / sizeof lines[0]));
}

static void _step_name(void) {
    char progress[12];
    onboarding_progress(ONBOARDING_STEP_NAME, &_plan, progress, sizeof progress);

    for (;;) {
        _draw_name_editor(progress);
        int ch = sys_getchar();
        if (ch == '\r' || ch == '\n') return;
        if (ch == 0x1B || ch == 0x03) return; /* keep the current name */
        if (ch == 0x7F || ch == 0x08) {
            size_t len = strlen(_state.name);
            if (len > 0) _state.name[len - 1] = '\0';
            continue;
        }
        if (ch < 0x20 || ch > 0x7E) continue;

        size_t len = strlen(_state.name);
        if (len + 2 >= sizeof _state.name) continue;
        char candidate[ONBOARDING_NAME_MAX];
        snprintf(candidate, sizeof candidate, "%s%c", _state.name, (char)ch);
        /* Only commit characters that keep the name valid, so the field can
           never hold something the settings file would reject. */
        if (onboarding_name_valid(candidate)) {
            snprintf(_state.name, sizeof _state.name, "%s", candidate);
        }
    }
}

static void _step_style(void) {
    char progress[12];
    onboarding_progress(ONBOARDING_STEP_STYLE, &_plan, progress, sizeof progress);

    for (;;) {
        const char *theme_name = ui_theme_name(_state.theme_index);
        char theme_line[LCD_COLS + 1];
        snprintf(theme_line, sizeof theme_line, "  Palette   < %s >",
                 theme_name ? theme_name : "default");
        char light_line[LCD_COLS + 1];
        snprintf(light_line, sizeof light_line, "  Backlight < %d%% >",
                 (_state.backlight * 100) / 255);

        char bar[LCD_COLS + 1];
        int filled = (LCD_COLS - 20) * _state.backlight / 255;
        memset(bar, ' ', LCD_COLS);
        bar[LCD_COLS] = '\0';
        memset(bar + 10, '#', (size_t)(filled > 0 ? filled : 0));

        const char *const lines[] = {
            "",
            "Make it yours.",
            "",
            theme_line,
            light_line,
            "",
            bar,
            "",
            "left/right palette, up/down light",
        };
        _draw_lines("Appearance", progress, lines, (int)(sizeof lines / sizeof lines[0]));

        int ch = sys_getchar();
        if (ch == '\r' || ch == '\n' || ch == 0x1B || ch == 0x03) return;
        if (ch == 0x11 || ch == 'd' || ch == 'l') {
            onboarding_theme_step(&_state, 1);
        } else if (ch == 0x12 || ch == 'a' || ch == 'h') {
            onboarding_theme_step(&_state, -1);
        } else if (ch == 0x10 || ch == 'w' || ch == 'k') {
            onboarding_backlight_step(&_state, 32);
        } else if (ch == 0x0E || ch == 's' || ch == 'j') {
            onboarding_backlight_step(&_state, -32);
        } else if (ch == '=' || ch == '+') {
            onboarding_backlight_step(&_state, 16);
        } else if (ch == '-' || ch == '_') {
            onboarding_backlight_step(&_state, -16);
        }

        /* Live preview: apply the palette and brightness as they change. */
        const char *name = ui_theme_name(_state.theme_index);
        if (name) (void)ui_theme_set(name);
        kbd_set_backlight((uint8_t)_state.backlight);
    }
}

#ifdef PICO_CYW43_SUPPORTED
static void _step_network(void) {
    char progress[12];
    onboarding_progress(ONBOARDING_STEP_NETWORK, &_plan, progress, sizeof progress);

    const char *const lines[] = {
        "",
        "Connect to Wi-Fi now?",
        "",
        "Needed for mpkg, NTP, IRC,",
        "Telnet and HTTPS downloads.",
        "",
        "y = scan and connect,  n = later",
    };
    _draw_lines("Network", progress, lines, (int)(sizeof lines / sizeof lines[0]));

    int ch = sys_getchar();
    if (ch == 'y' || ch == 'Y') {
        sys_clear();
        /* The network app owns its own prompts, including the password. */
        net_app_wifi("scan");
        _state.network_configured = true;
    }
}
#endif

static void _step_done(void) {
    char progress[12];
    onboarding_progress(ONBOARDING_STEP_DONE, &_plan, progress, sizeof progress);

    char summary[ONBOARDING_SUMMARY_MAX];
    onboarding_summary(&_state, summary, sizeof summary);

    const char *const lines[] = {
        "",
        "All set.",
        "",
        summary,
        "",
        "Press Enter to open the shell.",
        "Run `setup` any time to revisit",
        "these choices.",
    };
    _draw_lines("Ready", progress, lines, (int)(sizeof lines / sizeof lines[0]));
    (void)sys_getchar();
}

static void _finish(void) {
    const char *theme_name = ui_theme_name(_state.theme_index);
    if (!app_settings_apply_onboarding(_state.name, theme_name, _state.backlight))
        ui_error("setup", "settings could not be saved; changes apply until reboot");
    (void)write_text_file(SETUP_MARKER, "1\n", "setup");
}

void app_setup(const char *arg) {
    (void)arg;
    const app_settings_state_t *settings = app_settings_get();

#ifdef PICO_CYW43_SUPPORTED
    _plan.network = true;
#else
    _plan.network = false;
#endif

    onboarding_init(&_state, settings->device_name, ui_theme_current(), ui_theme_count(),
                    settings->backlight);
    _state.network_configured = false;

    _step_welcome();
    _step_name();
    _step_style();
#ifdef PICO_CYW43_SUPPORTED
    if (_plan.network) _step_network();
#endif
    _step_done();
    _finish();

    sys_clear();
    ui_ok("Setup complete");
}
