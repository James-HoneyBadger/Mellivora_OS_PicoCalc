#pragma once
/*
 * core/launcher.h — the model behind the home screen.
 *
 * The launcher is mostly a list and a search box, so everything that decides
 * *which* entries appear — categories, fuzzy ranking, and recent commands —
 * lives here and is host-tested. The firmware side only draws what this
 * module returns.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Built-ins plus a full /apps catalogue (mapp_discover caps at 24). */
#define LAUNCHER_MAX_ITEMS 48
#define LAUNCHER_TITLE_MAX 28
#define LAUNCHER_COMMAND_MAX 20
#define LAUNCHER_RECENTS 6

typedef enum {
    LAUNCHER_CAT_SYSTEM = 0,
    LAUNCHER_CAT_FILES,
    LAUNCHER_CAT_NOTES,
    LAUNCHER_CAT_TOOLS,
    LAUNCHER_CAT_LANG,
    LAUNCHER_CAT_GAMES,
    LAUNCHER_CAT_HARDWARE,
    LAUNCHER_CAT_APPS,
    LAUNCHER_CAT_COUNT,
} launcher_category_t;

typedef struct {
    char title[LAUNCHER_TITLE_MAX];
    char command[LAUNCHER_COMMAND_MAX];
    uint8_t category;
    /* One or two characters drawn left of the title. Kept as a pointer so
       the table stays in .rodata rather than being copied into RAM. */
    const char *icon;
} launcher_item_t;

typedef struct {
    char command[LAUNCHER_COMMAND_MAX];
} launcher_recent_t;

typedef struct {
    launcher_recent_t entry[LAUNCHER_RECENTS];
    uint8_t count;
} launcher_recents_t;

/* "SYSTEM", "FILES", ... for the category header strip. */
const char *launcher_category_name(int category);

/* Short tag sized for the launcher's right-hand column: "SYS", "FILES". */
const char *launcher_category_tag(int category);

/* ---- Fuzzy search ---------------------------------------------------- */

/* Score `needle` as a case-insensitive subsequence of `haystack`; -1 when it
   does not match. Higher is better: consecutive characters and matches at a
   word start score more, so "cl" ranks "Clock and calendar" (a run) above
   "Calculator" (scattered). */
int launcher_match(const char *needle, const char *haystack);

typedef struct {
    uint16_t index; /* index into the caller's item array */
    uint16_t score;
} launcher_hit_t;

/* Rank items for `query`, most relevant first. An empty query returns every
   item in its original order. Returns the number written, never more than
   `capacity`. */
size_t launcher_filter(const launcher_item_t *items, size_t count, const char *query,
                       launcher_hit_t *out, size_t capacity);

/* ---- Recents --------------------------------------------------------- */

void launcher_recents_clear(launcher_recents_t *recents);

/* Record a command as the most recent, de-duplicating and capping the list.
   Returns true when the list changed. */
bool launcher_recents_push(launcher_recents_t *recents, const char *command);

/* Index of `command` in the recents, or -1. */
int launcher_recents_find(const launcher_recents_t *recents, const char *command);

/* ---- Persistence ----------------------------------------------------- */

/* One command per line, so the file stays readable and hand-editable.
   Returns the number of bytes written, or 0 when nothing fits. */
size_t launcher_recents_serialize(const launcher_recents_t *recents, char *out, size_t capacity);

/* Load a previously serialized list. Malformed lines are skipped. */
void launcher_recents_parse(launcher_recents_t *recents, const char *text, size_t length);
