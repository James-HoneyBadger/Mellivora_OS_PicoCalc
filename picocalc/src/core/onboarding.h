#pragma once
/*
 * core/onboarding.h — first-boot setup decisions.
 *
 * The wizard is mostly a form, so the parts worth testing live here: which
 * steps apply to this build, what a valid device name looks like, how the
 * theme and backlight cycles behave at their limits, and the exact settings
 * lines the firmware writes. The firmware side only draws and reads keys.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ONBOARDING_NAME_MAX 24
#define ONBOARDING_SUMMARY_MAX 64

typedef enum {
    ONBOARDING_STEP_WELCOME = 0,
    ONBOARDING_STEP_NAME,
    ONBOARDING_STEP_STYLE,
    ONBOARDING_STEP_NETWORK,
    ONBOARDING_STEP_DONE,
    ONBOARDING_STEP_COUNT,
} onboarding_step_t;

/* Which optional steps this build should run. Wi-Fi setup is compiled out of
   the RP2040 and plain Pico 2 images, so the wizard must not offer it. */
typedef struct {
    bool network;
} onboarding_plan_t;

typedef struct {
    char name[ONBOARDING_NAME_MAX];
    int theme_index; /* index into the available palettes */
    int theme_count;
    int backlight; /* 0..255 */
    bool network_configured;
} onboarding_state_t;

void onboarding_init(onboarding_state_t *state, const char *device_name, int theme_index,
                     int theme_count, int backlight);

/* A device name is what the dashboard shows. Allow letters, digits, spaces,
   dash, underscore and apostrophe; refuse everything else so the string is
   safe to print and to store in a key=value file. */
bool onboarding_name_valid(const char *name);

/* Copy `text` into the state if it is valid, keeping the previous name
   otherwise. Returns true when the name changed. */
bool onboarding_set_name(onboarding_state_t *state, const char *text);

/* Cycle the palette selection, wrapping at both ends. */
void onboarding_theme_step(onboarding_state_t *state, int delta);

/* Adjust the backlight, clamped to 0..255. */
void onboarding_backlight_step(onboarding_state_t *state, int delta);

/* How many screens this build shows (four, or five with Wi-Fi). */
int onboarding_step_count(const onboarding_plan_t *plan);

/* Human-readable progress such as "2 of 4". */
void onboarding_progress(int step, const onboarding_plan_t *plan, char *out, size_t capacity);

/* One-line summary for the final screen. */
void onboarding_summary(const onboarding_state_t *state, char *out, size_t capacity);

/* The settings lines the wizard writes. `theme_name` is the palette the user
   landed on, because /SETTINGS.CFG stores themes by name. Returns the byte
   count, or 0 when the state or buffer is unusable. */
size_t onboarding_settings(const onboarding_state_t *state, const char *theme_name, char *out,
                           size_t capacity);
