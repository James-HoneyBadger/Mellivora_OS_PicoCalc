/*
 * test_launcher.c — categories, fuzzy ranking, and recents for the launcher.
 */

#include "core/launcher.h"
#include "test_util.h"

#include <string.h>

static launcher_item_t _items[] = {
    {"Dashboard", "dashboard", LAUNCHER_CAT_SYSTEM, "*"},
    {"File browser", "browse", LAUNCHER_CAT_FILES, "="},
    {"Notes", "notes", LAUNCHER_CAT_NOTES, "n"},
    {"Calculator", "calc", LAUNCHER_CAT_TOOLS, "+"},
    {"Clock and calendar", "clock", LAUNCHER_CAT_SYSTEM, "o"},
    {"Games", "games", LAUNCHER_CAT_GAMES, "@"},
    {"Terminal", "term", LAUNCHER_CAT_HARDWARE, ">"},
    {"sysinfo", "sysinfo", LAUNCHER_CAT_SYSTEM, "!"},
};

static const size_t ITEM_COUNT = sizeof _items / sizeof _items[0];

static const char *_title_of(uint16_t index) {
    return _items[index].title;
}

static void test_categories(void) {
    CHECK_STR_EQ(launcher_category_name(LAUNCHER_CAT_SYSTEM), "SYSTEM");
    CHECK_STR_EQ(launcher_category_name(LAUNCHER_CAT_LANG), "LANGUAGE");
    CHECK_STR_EQ(launcher_category_name(LAUNCHER_CAT_APPS), "APPS");
    /* Out-of-range categories degrade to an empty string, not garbage. */
    CHECK_STR_EQ(launcher_category_name(-1), "");
    CHECK_STR_EQ(launcher_category_name(LAUNCHER_CAT_COUNT), "");

    /* Tags fit the launcher's right-hand column. */
    CHECK_STR_EQ(launcher_category_tag(LAUNCHER_CAT_FILES), "FILE");
    CHECK_STR_EQ(launcher_category_tag(LAUNCHER_CAT_HARDWARE), "HW");
    CHECK_STR_EQ(launcher_category_tag(99), "");
    for (int i = 0; i < LAUNCHER_CAT_COUNT; i++) {
        CHECK(strlen(launcher_category_tag(i)) <= 4);
        CHECK(strlen(launcher_category_name(i)) <= 8);
    }
}

static void test_match(void) {
    /* An empty needle matches anything, scoring neutrally. */
    CHECK_EQ(launcher_match("", "anything"), 0);
    CHECK_EQ(launcher_match(NULL, "x"), -1);
    CHECK_EQ(launcher_match("x", NULL), -1);

    /* Case-insensitive subsequence. */
    CHECK(launcher_match("dash", "Dashboard") > 0);
    CHECK(launcher_match("DASH", "Dashboard") > 0);
    CHECK(launcher_match("dbd", "Dashboard") > 0);
    CHECK_EQ(launcher_match("zzz", "Dashboard"), -1);

    /* A prefix beats a scattered match. */
    CHECK(launcher_match("note", "Notes") > launcher_match("note", "Noughts and crosses"));
    /* A consecutive run beats the same letters spread out. */
    CHECK(launcher_match("cl", "Clock and calendar") > launcher_match("cl", "Calculator"));
    /* A word-start match beats a mid-word one. */
    CHECK(launcher_match("br", "File browser") > launcher_match("br", "Zebra"));
    /* Order matters: a subsequence in the wrong order does not match.
       ("bd" *would* match Dashboard - b then the final d - so use a pair
       whose characters really are out of order.) */
    CHECK_EQ(launcher_match("sd", "Notes"), -1);
    /* Every character must be consumed exactly once: "Dashboard" has two
       d's, but "ddd" needs three. */
    CHECK_EQ(launcher_match("ddd", "Dashboard"), -1);
}

static void test_filter(void) {
    launcher_hit_t hits[LAUNCHER_MAX_ITEMS];

    /* An empty query lists everything, in the original order. */
    size_t count = launcher_filter(_items, ITEM_COUNT, "", hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, ITEM_COUNT);
    for (size_t i = 0; i < count; i++)
        CHECK_EQ(hits[i].index, i);
    count = launcher_filter(_items, ITEM_COUNT, NULL, hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, ITEM_COUNT);

    /* "cl" matches Clock and Calculator, with Clock first. */
    count = launcher_filter(_items, ITEM_COUNT, "cl", hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, 2);
    CHECK_STR_EQ(_title_of(hits[0].index), "Clock and calendar");
    CHECK_STR_EQ(_title_of(hits[1].index), "Calculator");
    CHECK(hits[0].score > hits[1].score);

    /* A command-name match finds entries whose title does not contain it. */
    count = launcher_filter(_items, ITEM_COUNT, "browse", hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, 1);
    CHECK_STR_EQ(_title_of(hits[0].index), "File browser");

    /* "sys" reaches sysinfo by command and nothing else. */
    count = launcher_filter(_items, ITEM_COUNT, "sys", hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, 1);
    CHECK_STR_EQ(_title_of(hits[0].index), "sysinfo");

    /* No match at all. */
    CHECK_EQ(launcher_filter(_items, ITEM_COUNT, "qqqq", hits, LAUNCHER_MAX_ITEMS), 0);

    /* Capacity is respected and the best matches survive. */
    count = launcher_filter(_items, ITEM_COUNT, "a", hits, 2);
    CHECK_EQ(count, 2);
    CHECK(hits[0].score >= hits[1].score);

    /* Ties keep the original order (stable). */
    launcher_item_t twins[] = {
        {"Alpha", "a1", LAUNCHER_CAT_TOOLS, "1"},
        {"Alpha", "a2", LAUNCHER_CAT_TOOLS, "2"},
        {"Alpha", "a3", LAUNCHER_CAT_TOOLS, "3"},
    };
    count = launcher_filter(twins, 3, "alpha", hits, LAUNCHER_MAX_ITEMS);
    CHECK_EQ(count, 3);
    CHECK_EQ(hits[0].index, 0);
    CHECK_EQ(hits[1].index, 1);
    CHECK_EQ(hits[2].index, 2);

    /* Degenerate arguments. */
    CHECK_EQ(launcher_filter(NULL, ITEM_COUNT, "x", hits, 4), 0);
    CHECK_EQ(launcher_filter(_items, ITEM_COUNT, "x", NULL, 4), 0);
    CHECK_EQ(launcher_filter(_items, ITEM_COUNT, "x", hits, 0), 0);
    CHECK_EQ(launcher_filter(_items, 0, "x", hits, 4), 0);
}

static void test_recents(void) {
    launcher_recents_t recents;
    launcher_recents_clear(&recents);
    CHECK_EQ(recents.count, 0);
    CHECK_EQ(launcher_recents_find(&recents, "notes"), -1);

    /* Most recent first. */
    CHECK(launcher_recents_push(&recents, "notes"));
    CHECK(launcher_recents_push(&recents, "calc"));
    CHECK_EQ(recents.count, 2);
    CHECK_STR_EQ(recents.entry[0].command, "calc");
    CHECK_STR_EQ(recents.entry[1].command, "notes");

    /* Re-running something moves it to the front without duplicating. */
    CHECK(launcher_recents_push(&recents, "notes"));
    CHECK_EQ(recents.count, 2);
    CHECK_STR_EQ(recents.entry[0].command, "notes");
    CHECK_STR_EQ(recents.entry[1].command, "calc");

    /* Pushing what is already first is a no-op. */
    CHECK(!launcher_recents_push(&recents, "notes"));
    CHECK_EQ(recents.count, 2);
    CHECK_EQ(launcher_recents_find(&recents, "notes"), 0);

    /* The list is capped, dropping the oldest. */
    for (int i = 0; i < LAUNCHER_RECENTS + 4; i++) {
        char command[LAUNCHER_COMMAND_MAX];
        snprintf(command, sizeof command, "cmd%d", i);
        CHECK(launcher_recents_push(&recents, command));
    }
    CHECK_EQ(recents.count, LAUNCHER_RECENTS);
    CHECK_STR_EQ(recents.entry[0].command, "cmd9");
    CHECK_EQ(launcher_recents_find(&recents, "notes"), -1);

    /* Oversized and empty commands are refused rather than truncated. */
    char long_command[LAUNCHER_COMMAND_MAX + 8];
    memset(long_command, 'x', sizeof long_command - 1);
    long_command[sizeof long_command - 1] = '\0';
    CHECK(!launcher_recents_push(&recents, long_command));
    CHECK(!launcher_recents_push(&recents, ""));
    CHECK(!launcher_recents_push(&recents, NULL));
    CHECK(!launcher_recents_push(NULL, "x"));
    CHECK_EQ(recents.count, LAUNCHER_RECENTS);

    launcher_recents_clear(&recents);
    CHECK_EQ(recents.count, 0);
}

static void test_persistence(void) {
    launcher_recents_t recents;
    launcher_recents_clear(&recents);
    (void)launcher_recents_push(&recents, "calc");
    (void)launcher_recents_push(&recents, "notes");
    (void)launcher_recents_push(&recents, "games");

    /* One command per line, newest first, NUL terminated. */
    char text[128];
    size_t written = launcher_recents_serialize(&recents, text, sizeof text);
    CHECK_STR_EQ(text, "games\nnotes\ncalc\n");
    CHECK_EQ(written, strlen("games\nnotes\ncalc\n"));

    /* Round trip. */
    launcher_recents_t loaded;
    launcher_recents_parse(&loaded, text, written);
    CHECK_EQ(loaded.count, 3);
    CHECK_STR_EQ(loaded.entry[0].command, "games");
    CHECK_STR_EQ(loaded.entry[2].command, "calc");

    /* CRLF and trailing spaces are tolerated (hand-edited files). */
    const char *crlf = "term\r\n  browse  \n\nnotes";
    launcher_recents_parse(&loaded, crlf, strlen(crlf));
    CHECK_EQ(loaded.count, 3);
    CHECK_STR_EQ(loaded.entry[0].command, "term");
    CHECK_STR_EQ(loaded.entry[1].command, "browse");
    CHECK_STR_EQ(loaded.entry[2].command, "notes");

    /* A missing final newline still yields the last entry. */
    launcher_recents_parse(&loaded, "one\ntwo", 7);
    CHECK_EQ(loaded.count, 2);
    CHECK_STR_EQ(loaded.entry[1].command, "two");

    /* Oversized lines are skipped, not truncated into garbage. */
    char huge[64];
    memset(huge, 'z', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    char mixed[96];
    snprintf(mixed, sizeof mixed, "ok\n%s\nfine\n", huge);
    launcher_recents_parse(&loaded, mixed, strlen(mixed));
    CHECK_EQ(loaded.count, 2);
    CHECK_STR_EQ(loaded.entry[0].command, "ok");
    CHECK_STR_EQ(loaded.entry[1].command, "fine");

    /* The cap applies on load too. */
    char many[256];
    size_t pos = 0;
    for (int i = 0; i < LAUNCHER_RECENTS + 5; i++)
        pos += (size_t)snprintf(many + pos, sizeof many - pos, "c%d\n", i);
    launcher_recents_parse(&loaded, many, pos);
    CHECK_EQ(loaded.count, LAUNCHER_RECENTS);

    /* Empty and degenerate inputs. */
    launcher_recents_parse(&loaded, "", 0);
    CHECK_EQ(loaded.count, 0);
    launcher_recents_parse(&loaded, NULL, 0);
    CHECK_EQ(loaded.count, 0);
    CHECK_EQ(launcher_recents_serialize(NULL, text, sizeof text), 0);
    CHECK_EQ(launcher_recents_serialize(&recents, NULL, sizeof text), 0);
    CHECK_EQ(launcher_recents_serialize(&recents, text, 0), 0);

    /* Too small a buffer writes nothing rather than a partial line. */
    CHECK_EQ(launcher_recents_serialize(&recents, text, 3), 0);
}

int test_launcher(void) {
    test_categories();
    test_match();
    test_filter();
    test_recents();
    test_persistence();
    return 0;
}
