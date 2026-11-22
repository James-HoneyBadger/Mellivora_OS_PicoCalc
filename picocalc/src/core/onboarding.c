/*
 * core/onboarding.c — first-boot setup decisions.
 */

#include "core/onboarding.h"

#include <stdio.h>
#include <string.h>

void onboarding_init(onboarding_state_t *state, const char *device_name, int theme_index,
                     int theme_count, int backlight) {
    if (!state) return;
    memset(state, 0, sizeof *state);

    if (device_name && onboarding_name_valid(device_name))
        (void)onboarding_set_name(state, device_name);
    if (!state->name[0]) {
        /* A neutral default that still reads well on the dashboard. */
        snprintf(state->name, sizeof state->name, "PicoCalc");
    }

    state->theme_count = theme_count > 0 ? theme_count : 1;
    state->theme_index = theme_index;
    if (state->theme_index < 0) state->theme_index = 0;
    if (state->theme_index >= state->theme_count) state->theme_index = state->theme_count - 1;

    state->backlight = backlight < 0 ? 0 : (backlight > 255 ? 255 : backlight);
}

bool onboarding_name_valid(const char *name) {
    if (!name) return false;
    size_t length = strlen(name);
    if (length == 0 || length >= ONBOARDING_NAME_MAX) return false;

    bool has_visible = false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool digit = c >= '0' && c <= '9';
        /* A key=value settings file cannot carry a newline, and the name is
           printed on the dashboard, so control characters are refused. */
        bool punctuation = c == ' ' || c == '-' || c == '_' || c == '\'';
        if (!letter && !digit && !punctuation) return false;
        if (letter || digit) has_visible = true;
    }
    /* Reject names made only of spaces or punctuation. */
    return has_visible;
}

bool onboarding_set_name(onboarding_state_t *state, const char *text) {
    if (!state || !onboarding_name_valid(text)) return false;
    if (strcmp(state->name, text) == 0) return false;
    snprintf(state->name, sizeof state->name, "%s", text);
    return true;
}

void onboarding_theme_step(onboarding_state_t *state, int delta) {
    if (!state || state->theme_count <= 0) return;
    int next = state->theme_index + delta;

    /* Wrap explicitly rather than relying on modulo of a negative value. */
    while (next < 0)
        next += state->theme_count;
    while (next >= state->theme_count)
        next -= state->theme_count;
    state->theme_index = next;
}

void onboarding_backlight_step(onboarding_state_t *state, int delta) {
    if (!state) return;
    int next = state->backlight + delta;
    if (next < 0) next = 0;
    if (next > 255) next = 255;
    state->backlight = next;
}

int onboarding_step_count(const onboarding_plan_t *plan) {
    return plan && plan->network ? 5 : 4;
}

void onboarding_progress(int step, const onboarding_plan_t *plan, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    int total = onboarding_step_count(plan);
    if (step < 0) step = 0;
    if (step >= total) step = total - 1;
    snprintf(out, capacity, "%d of %d", step + 1, total);
}

void onboarding_summary(const onboarding_state_t *state, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    if (!state) {
        snprintf(out, capacity, "Setup complete");
        return;
    }
    snprintf(out, capacity, "%s - theme %d - light %d%%", state->name, state->theme_index + 1,
             (state->backlight * 100) / 255);
}

size_t onboarding_settings(const onboarding_state_t *state, const char *theme_name, char *out,
                           size_t capacity) {
    if (!state || !out || capacity == 0) return 0;
    if (!onboarding_name_valid(state->name)) return 0;
    if (!theme_name) theme_name = "noir";

    int written = snprintf(out, capacity, "NAME=%s\nTHEME=%s\nBACKLIGHT=%d\n", state->name,
                           theme_name, state->backlight);
    if (written <= 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}
