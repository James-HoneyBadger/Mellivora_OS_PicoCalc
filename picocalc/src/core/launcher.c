/*
 * core/launcher.c — categories, fuzzy ranking, and recents for the home
 * screen.
 */

#include "core/launcher.h"

#include <string.h>

static const char *const _names[LAUNCHER_CAT_COUNT] = {
    "SYSTEM", "FILES", "NOTES", "TOOLS", "LANGUAGE", "GAMES", "HARDWARE", "APPS",
};

static const char *const _tags[LAUNCHER_CAT_COUNT] = {
    "SYS", "FILE", "NOTE", "TOOL", "LANG", "GAME", "HW", "APP",
};

const char *launcher_category_name(int category) {
    if (category < 0 || category >= LAUNCHER_CAT_COUNT) return "";
    return _names[category];
}

const char *launcher_category_tag(int category) {
    if (category < 0 || category >= LAUNCHER_CAT_COUNT) return "";
    return _tags[category];
}

static unsigned char _lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* Scores, chosen so a consecutive run always beats a scattered match and a
   word-start match always beats a mid-word one. */
#define SCORE_BASE 1
#define SCORE_CONSECUTIVE 8
#define SCORE_WORD_START 6

int launcher_match(const char *needle, const char *haystack) {
    if (!needle || !haystack) return -1;
    if (!*needle) return 0; /* empty query matches everything neutrally */

    int score = 0;
    const char *h = haystack;
    bool previous_matched = false;

    for (const unsigned char *n = (const unsigned char *)needle; *n; n++) {
        unsigned char wanted = _lower(*n);
        bool found = false;

        while (*h) {
            unsigned char candidate = _lower((unsigned char)*h);
            bool at_word_start = (h == haystack) || (h[-1] == ' ') || (h[-1] == '-') ||
                                 (h[-1] == '_') || (h[-1] == '/');
            if (candidate == wanted) {
                score += SCORE_BASE;
                if (previous_matched) score += SCORE_CONSECUTIVE;
                if (at_word_start) score += SCORE_WORD_START;
                h++;
                previous_matched = true;
                found = true;
                break;
            }
            h++;
            previous_matched = false;
        }
        if (!found) return -1;
    }
    return score;
}

size_t launcher_filter(const launcher_item_t *items, size_t count, const char *query,
                       launcher_hit_t *out, size_t capacity) {
    if (!items || !out || capacity == 0) return 0;

    bool empty_query = !query || !*query;
    size_t written = 0;

    for (size_t i = 0; i < count; i++) {
        int score = empty_query ? 0 : launcher_match(query, items[i].title);
        if (!empty_query && score < 0) {
            /* Also try the command name, so "fb" finds `browse` when the
               title is "File browser" but "sys" finds `sysinfo`. */
            int command_score = launcher_match(query, items[i].command);
            if (command_score < 0) continue;
            /* Title matches are what the user reads, so keep them ahead of
               command-only matches of similar strength. */
            score = command_score;
        }
        /* Keep the best capacity hits, including late catalogue entries. */
        if (i > UINT16_MAX) break;
        if (written == capacity && score <= out[written - 1].score) continue;
        /* Insertion sort by score, stable for equal scores. */
        size_t pos = written < capacity ? written : written - 1;
        while (pos > 0 && out[pos - 1].score < (uint16_t)score) {
            out[pos] = out[pos - 1];
            pos--;
        }
        out[pos].index = (uint16_t)i;
        out[pos].score = (uint16_t)score;
        if (written < capacity) written++;
    }
    return written;
}

void launcher_recents_clear(launcher_recents_t *recents) {
    if (!recents) return;
    memset(recents, 0, sizeof *recents);
}

int launcher_recents_find(const launcher_recents_t *recents, const char *command) {
    if (!recents || !command || !*command) return -1;
    for (uint8_t i = 0; i < recents->count && i < LAUNCHER_RECENTS; i++) {
        if (strcmp(recents->entry[i].command, command) == 0) return (int)i;
    }
    return -1;
}

static bool _copy_command(char *dst, const char *src) {
    size_t length = strlen(src);
    if (length == 0 || length >= LAUNCHER_COMMAND_MAX) return false;
    memcpy(dst, src, length);
    dst[length] = '\0';
    return true;
}

bool launcher_recents_push(launcher_recents_t *recents, const char *command) {
    if (!recents || !command || !*command) return false;
    if (strlen(command) >= LAUNCHER_COMMAND_MAX) return false;

    int existing = launcher_recents_find(recents, command);
    if (existing == 0) return false; /* already the most recent */

    uint8_t keep = recents->count;
    if (existing > 0) {
        /* Pull it out; everything above it shifts down. */
        for (int i = existing; i > 0; i--)
            recents->entry[i] = recents->entry[i - 1];
        keep = recents->count;
    } else {
        if (keep < LAUNCHER_RECENTS) keep++;
        for (int i = keep - 1; i > 0; i--)
            recents->entry[i] = recents->entry[i - 1];
    }

    if (!_copy_command(recents->entry[0].command, command)) return false;
    recents->count = keep;
    return true;
}

size_t launcher_recents_serialize(const launcher_recents_t *recents, char *out, size_t capacity) {
    if (!recents || !out || capacity == 0) return 0;

    size_t used = 0;
    for (uint8_t i = 0; i < recents->count && i < LAUNCHER_RECENTS; i++) {
        const char *command = recents->entry[i].command;
        if (!*command) continue;
        size_t length = strlen(command);
        if (used + length + 1 >= capacity) break;
        memcpy(out + used, command, length);
        used += length;
        out[used++] = '\n';
    }
    if (used < capacity) out[used] = '\0';
    return used;
}

void launcher_recents_parse(launcher_recents_t *recents, const char *text, size_t length) {
    if (!recents) return;
    launcher_recents_clear(recents);
    if (!text) return;

    size_t start = 0;
    for (size_t i = 0; i <= length; i++) {
        bool at_end = i == length;
        if (!at_end && text[i] != '\n') continue;

        /* Trim both ends: a hand-edited file may indent its entries. */
        size_t from = start;
        while (from < i && (text[from] == ' ' || text[from] == '\t'))
            from++;
        size_t stop = i;
        while (stop > from &&
               (text[stop - 1] == '\r' || text[stop - 1] == ' ' || text[stop - 1] == '\t'))
            stop--;

        if (stop > from && recents->count < LAUNCHER_RECENTS) {
            size_t span = stop - from;
            if (span < LAUNCHER_COMMAND_MAX) {
                char line[LAUNCHER_COMMAND_MAX];
                memcpy(line, text + from, span);
                line[span] = '\0';
                if (_copy_command(recents->entry[recents->count].command, line)) recents->count++;
            }
        }
        start = i + 1;
        if (at_end) break;
    }
}
