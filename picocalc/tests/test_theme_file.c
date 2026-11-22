/*
 * test_theme_file.c — parser for user palettes in /themes.
 */

#include "core/theme_file.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

static const char *THEME_TEXT = "# Ocean palette\n"
                                "name = ocean\n"
                                "\n"
                                "bg = 06121C\n"
                                "text = #D8E8F0\n"
                                "accent = 3FB8C8\r\n"
                                "muted   =   6A7C88  \n"
                                "unknown_key = 112233\n";

void test_theme_file(void) {
    theme_file_t theme;
    CHECK(theme_file_parse(&theme, THEME_TEXT, strlen(THEME_TEXT)));
    CHECK_STR_EQ(theme.name, "ocean");
    CHECK_EQ(theme.set_mask, (1u << THEME_ROLE_BG) | (1u << THEME_ROLE_TEXT) |
                                 (1u << THEME_ROLE_ACCENT) | (1u << THEME_ROLE_MUTED));
    CHECK_EQ(theme.color[THEME_ROLE_BG], 0x06121Cu);
    CHECK_EQ(theme.color[THEME_ROLE_TEXT], 0xD8E8F0u);
    CHECK_EQ(theme.color[THEME_ROLE_ACCENT], 0x3FB8C8u);
    CHECK_EQ(theme.color[THEME_ROLE_MUTED], 0x6A7C88u);
    CHECK_EQ(theme.color[THEME_ROLE_ERROR], 0u);
    CHECK_EQ(theme.errors, 0);

    /* Role lookup is case-insensitive and covers every slot. */
    CHECK_EQ(theme_file_role_index("BG"), THEME_ROLE_BG);
    CHECK_EQ(theme_file_role_index("status_text"), THEME_ROLE_STATUS_TEXT);
    CHECK_EQ(theme_file_role_index("nope"), -1);
    CHECK_STR_EQ(theme_file_role_name(THEME_ROLE_SELECT_BG), "select_bg");

    /* Colour parsing. */
    uint32_t color = 0;
    CHECK(theme_file_parse_color("0A0B0C", &color) && color == 0x0A0B0Cu);
    CHECK(theme_file_parse_color("#aabbcc", &color) && color == 0xAABBCCu);
    CHECK(!theme_file_parse_color("aabb", &color));
    CHECK(!theme_file_parse_color("gggggg", &color));
    CHECK(!theme_file_parse_color("aabbccdd", &color));

    /* Missing name, no colours, and malformed lines. */
    const char *no_name = "bg = 000000\n";
    CHECK(!theme_file_parse(&theme, no_name, strlen(no_name)));

    const char *bad_values = "name = broken\n"
                             "bg = zzzzzz\n"
                             "this line has no equals sign\n"
                             "= 123456\n";
    CHECK(!theme_file_parse(&theme, bad_values, strlen(bad_values)));
    CHECK_EQ(theme.errors, 3);

    /* A partial palette is still usable; callers fill in the remainder. */
    const char *partial = "name=mini\naccent=FF0000\n";
    CHECK(theme_file_parse(&theme, partial, strlen(partial)));
    CHECK_STR_EQ(theme.name, "mini");
    CHECK_EQ(theme.set_mask, (1u << THEME_ROLE_ACCENT));

    /* Excessively long names are truncated rather than overflowing. */
    char long_text[128];
    size_t pos = 0;
    memcpy(long_text, "name = ", 7);
    pos = 7;
    memset(long_text + pos, 'z', 40);
    pos += 40;
    long_text[pos++] = '\n';
    memcpy(long_text + pos, "accent=010203\n", 15);
    pos += 15;
    CHECK(theme_file_parse(&theme, long_text, pos));
    CHECK_EQ(strlen(theme.name), THEME_FILE_NAME_MAX - 1);

    /* ---- Discovery of /themes ------------------------------------------ */

    theme_scan_t scan;
    theme_scan_reset(&scan);
    CHECK_EQ(scan.count, 0);

    /* A palette is queued with its full path and a name derived from the
       stem, so a file without a "name =" line still gets a label. */
    CHECK(theme_scan_visit(&scan, "OCEAN.THM", NULL, false));
    CHECK_EQ(scan.count, 1);
    CHECK_STR_EQ(scan.path[0], "/themes/OCEAN.THM");
    CHECK_STR_EQ(scan.name[0], "OCEAN");

    /* The long name wins when both are present, and the extension match is
       case-insensitive. */
    CHECK(theme_scan_visit(&scan, "OCEAN~1.THM", "Ocean Blue.ThM", false));
    CHECK_EQ(scan.count, 2);
    CHECK_STR_EQ(scan.path[1], "/themes/Ocean Blue.ThM");
    CHECK_STR_EQ(scan.name[1], "Ocean Blue");

    /* Directories and other extensions are ignored. */
    CHECK(!theme_scan_visit(&scan, "PALETTES", "palettes", true));
    CHECK(!theme_scan_visit(&scan, "NOTES.TXT", NULL, false));
    CHECK(!theme_scan_visit(&scan, "THM", NULL, false));
    CHECK(!theme_scan_visit(&scan, "THEME.THMX", NULL, false));
    CHECK_EQ(scan.count, 2);

    /* A name too long for the path buffer is skipped rather than truncated
       into a path that would fail to open. */
    char wide[THEME_SCAN_PATH_MAX];
    memset(wide, 'a', sizeof wide - 5);
    wide[sizeof wide - 5] = '\0';
    strcat(wide, ".thm");
    CHECK(!theme_scan_visit(&scan, wide, NULL, false));
    CHECK_EQ(scan.count, 2);

    /* The table is capped; extra palettes are dropped, not overrun. */
    while (scan.count < THEME_SCAN_MAX) {
        char name[16];
        snprintf(name, sizeof name, "T%d.THM", scan.count);
        CHECK(theme_scan_visit(&scan, name, NULL, false));
    }
    CHECK_EQ(scan.count, THEME_SCAN_MAX);
    CHECK(!theme_scan_visit(&scan, "OVER.THM", NULL, false));
    CHECK_EQ(scan.count, THEME_SCAN_MAX);

    /* Resetting is how the caller restarts a scan. */
    theme_scan_reset(&scan);
    CHECK_EQ(scan.count, 0);
    CHECK(theme_scan_visit(&scan, "A.THM", NULL, false));
    CHECK_STR_EQ(scan.path[0], "/themes/A.THM");

    /* A NULL scan or name is tolerated. */
    CHECK(!theme_scan_visit(NULL, "A.THM", NULL, false));
    CHECK(!theme_scan_visit(&scan, NULL, NULL, false));
}
