/*
 * test_onboarding.c — first-boot setup decisions.
 */

#include "core/onboarding.h"
#include "test_util.h"

#include <string.h>

static void test_name_validation(void) {
    /* Ordinary names pass. */
    CHECK(onboarding_name_valid("PicoCalc"));
    CHECK(onboarding_name_valid("James's PicoCalc"));
    CHECK(onboarding_name_valid("unit-01"));
    CHECK(onboarding_name_valid("desk_top 2"));
    CHECK(onboarding_name_valid("a"));

    /* Empty, NULL, and whitespace-only names are refused. */
    CHECK(!onboarding_name_valid(""));
    CHECK(!onboarding_name_valid(NULL));
    CHECK(!onboarding_name_valid("   "));
    CHECK(!onboarding_name_valid("---"));

    /* A name has to fit the settings field. */
    char longest[ONBOARDING_NAME_MAX];
    memset(longest, 'a', sizeof longest - 1);
    longest[sizeof longest - 1] = '\0';
    CHECK(onboarding_name_valid(longest)); /* exactly max-1 characters */
    char too_long[ONBOARDING_NAME_MAX + 2];
    memset(too_long, 'a', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    CHECK(!onboarding_name_valid(too_long));

    /* Characters that would break the settings file or the display. */
    CHECK(!onboarding_name_valid("bad\nname"));
    CHECK(!onboarding_name_valid("bad=name"));
    CHECK(!onboarding_name_valid("tab\there"));
    CHECK(!onboarding_name_valid("emoji\x01"));
    CHECK(!onboarding_name_valid("semi;colon"));
}

static void test_init_and_name(void) {
    onboarding_state_t state;
    onboarding_init(&state, "Desk Unit", 2, 5, 128);
    CHECK_STR_EQ(state.name, "Desk Unit");
    CHECK_EQ(state.theme_index, 2);
    CHECK_EQ(state.theme_count, 5);
    CHECK_EQ(state.backlight, 128);

    /* An unusable saved name falls back to a neutral default. */
    onboarding_init(&state, "", 0, 5, 100);
    CHECK_STR_EQ(state.name, "PicoCalc");
    onboarding_init(&state, NULL, 0, 5, 100);
    CHECK_STR_EQ(state.name, "PicoCalc");

    /* Out-of-range inputs are clamped rather than trusted. */
    onboarding_init(&state, "x", 99, 3, 999);
    CHECK_EQ(state.theme_index, 2);
    CHECK_EQ(state.backlight, 255);
    onboarding_init(&state, "x", -5, 3, -20);
    CHECK_EQ(state.theme_index, 0);
    CHECK_EQ(state.backlight, 0);
    onboarding_init(&state, "x", 0, 0, 0);
    CHECK_EQ(state.theme_count, 1); /* never zero, so cycling is safe */

    /* Setting a name reports whether it changed. */
    onboarding_init(&state, "Desk", 0, 3, 100);
    CHECK(!onboarding_set_name(&state, "Desk"));
    CHECK(onboarding_set_name(&state, "Shelf"));
    CHECK_STR_EQ(state.name, "Shelf");
    /* An invalid name leaves the previous one alone. */
    CHECK(!onboarding_set_name(&state, "bad\nname"));
    CHECK_STR_EQ(state.name, "Shelf");
    CHECK(!onboarding_set_name(&state, ""));
    CHECK_STR_EQ(state.name, "Shelf");
    CHECK(!onboarding_set_name(NULL, "x"));
}

static void test_theme_cycling(void) {
    onboarding_state_t state;
    onboarding_init(&state, "x", 0, 3, 100);

    onboarding_theme_step(&state, 1);
    CHECK_EQ(state.theme_index, 1);
    /* Wraps forward to the first entry. */
    onboarding_theme_step(&state, 1);
    onboarding_theme_step(&state, 1);
    CHECK_EQ(state.theme_index, 0);

    /* Wraps backward to the last entry, without relying on modulo of a
       negative number. */
    onboarding_theme_step(&state, -1);
    CHECK_EQ(state.theme_index, 2);
    onboarding_theme_step(&state, -1);
    CHECK_EQ(state.theme_index, 1);

    /* A single-theme build stays put. */
    onboarding_init(&state, "x", 0, 1, 100);
    onboarding_theme_step(&state, 1);
    CHECK_EQ(state.theme_index, 0);
    onboarding_theme_step(&state, -1);
    CHECK_EQ(state.theme_index, 0);

    onboarding_theme_step(NULL, 1); /* tolerated */
}

static void test_backlight(void) {
    onboarding_state_t state;
    onboarding_init(&state, "x", 0, 3, 128);

    onboarding_backlight_step(&state, 32);
    CHECK_EQ(state.backlight, 160);
    onboarding_backlight_step(&state, -100);
    CHECK_EQ(state.backlight, 60);
    /* Clamped at both ends. */
    onboarding_backlight_step(&state, -500);
    CHECK_EQ(state.backlight, 0);
    onboarding_backlight_step(&state, 5000);
    CHECK_EQ(state.backlight, 255);
    onboarding_backlight_step(NULL, 10);
}

static void test_progress_and_summary(void) {
    onboarding_plan_t offline = {.network = false};
    onboarding_plan_t online = {.network = true};

    CHECK_EQ(onboarding_step_count(&offline), 4);
    CHECK_EQ(onboarding_step_count(&online), 5);
    CHECK_EQ(onboarding_step_count(NULL), 4);

    char text[ONBOARDING_SUMMARY_MAX];
    onboarding_progress(0, &offline, text, sizeof text);
    CHECK_STR_EQ(text, "1 of 4");
    onboarding_progress(3, &offline, text, sizeof text);
    CHECK_STR_EQ(text, "4 of 4");
    onboarding_progress(0, &online, text, sizeof text);
    CHECK_STR_EQ(text, "1 of 5");
    /* Out-of-range steps clamp instead of printing nonsense. */
    onboarding_progress(-2, &online, text, sizeof text);
    CHECK_STR_EQ(text, "1 of 5");
    onboarding_progress(99, &online, text, sizeof text);
    CHECK_STR_EQ(text, "5 of 5");
    onboarding_progress(0, &offline, NULL, 8);
    onboarding_progress(0, &offline, text, 0);

    onboarding_state_t state;
    onboarding_init(&state, "Desk", 1, 4, 255);
    onboarding_summary(&state, text, sizeof text);
    CHECK_STR_EQ(text, "Desk - theme 2 - light 100%");
    onboarding_init(&state, "Desk", 0, 4, 0);
    onboarding_summary(&state, text, sizeof text);
    CHECK_STR_EQ(text, "Desk - theme 1 - light 0%");
    onboarding_summary(NULL, text, sizeof text);
    CHECK_STR_EQ(text, "Setup complete");
    onboarding_summary(&state, NULL, 8);
    onboarding_summary(&state, text, 0);
}

static void test_settings_output(void) {
    onboarding_state_t state;
    onboarding_init(&state, "Desk Unit", 2, 4, 200);

    char text[96];
    size_t written = onboarding_settings(&state, "ocean", text, sizeof text);
    CHECK_STR_EQ(text, "NAME=Desk Unit\nTHEME=ocean\nBACKLIGHT=200\n");
    CHECK_EQ(written, strlen(text));

    /* A NULL theme falls back rather than writing THEME=. */
    (void)onboarding_settings(&state, NULL, text, sizeof text);
    CHECK_STR_EQ(text, "NAME=Desk Unit\nTHEME=noir\nBACKLIGHT=200\n");

    /* Refuses to emit something the settings parser could not read back. */
    onboarding_init(&state, "ok", 0, 1, 0);
    state.name[0] = '\0';
    CHECK_EQ(onboarding_settings(&state, "noir", text, sizeof text), 0);

    /* A buffer too small for the whole block yields nothing. */
    onboarding_init(&state, "Desk Unit", 0, 1, 128);
    CHECK_EQ(onboarding_settings(&state, "noir", text, 8), 0);
    CHECK_EQ(onboarding_settings(NULL, "noir", text, sizeof text), 0);
    CHECK_EQ(onboarding_settings(&state, "noir", NULL, sizeof text), 0);
    CHECK_EQ(onboarding_settings(&state, "noir", text, 0), 0);
}

int test_onboarding(void) {
    test_name_validation();
    test_init_and_name();
    test_theme_cycling();
    test_backlight();
    test_progress_and_summary();
    test_settings_output();
    return 0;
}
