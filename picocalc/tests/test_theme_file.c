/*
 * test_theme_file.c — parser for user palettes in /themes.
 */

#include "core/theme_file.h"
#include "test_util.h"

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
}
