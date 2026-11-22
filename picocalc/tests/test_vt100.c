/*
 * test_vt100.c — terminal emulation: text, cursor, erase, scroll, SGR.
 */

#include "core/vt100.h"
#include "test_util.h"

#include <string.h>

#define TEST_COLS 20
#define TEST_ROWS 5

typedef struct {
    char reply[64];
    size_t len;
    int bells;
} vt_capture_t;

static vt100_cell_t _cells[TEST_COLS * TEST_ROWS];
static vt_capture_t _capture;

static void _on_write(void *ctx, const char *text, size_t len) {
    vt_capture_t *capture = (vt_capture_t *)ctx;
    if (capture->len + len >= sizeof capture->reply) return;
    memcpy(capture->reply + capture->len, text, len);
    capture->len += len;
    capture->reply[capture->len] = '\0';
}

static void _on_bell(void *ctx) {
    ((vt_capture_t *)ctx)->bells++;
}

static void _reset(vt100_t *vt) {
    _capture.reply[0] = '\0';
    _capture.len = 0;
    _capture.bells = 0;
    vt100_init(vt, _cells, TEST_COLS, TEST_ROWS);
    vt->write = _on_write;
    vt->write_ctx = &_capture;
    vt->bell = _on_bell;
    vt->bell_ctx = &_capture;
}

static void _feed(vt100_t *vt, const char *text) {
    vt100_feed_bytes(vt, text, strlen(text));
}

static char _ch(const vt100_t *vt, int col, int row) {
    const vt100_cell_t *cell = vt100_cell_at(vt, col, row);
    return cell ? (char)cell->ch : '?';
}

static char _row_text(const vt100_t *vt, int row, char *out, int cap) {
    int i = 0;
    for (; i < vt->cols && i < cap - 1; i++)
        out[i] = _ch(vt, i, row);
    out[i] = '\0';
    return out[0];
}

void test_vt100(void) {
    vt100_t vt;
    _reset(&vt);

    CHECK_EQ(vt.cols, TEST_COLS);
    CHECK_EQ(vt.rows, TEST_ROWS);
    CHECK_EQ(vt100_cursor_col(&vt), 0);
    CHECK_EQ(vt100_cursor_row(&vt), 0);
    CHECK(vt100_cursor_visible(&vt));
    CHECK(_ch(&vt, 0, 0) == ' ');

    /* Plain text advances the cursor. */
    _feed(&vt, "hello");
    CHECK(_ch(&vt, 0, 0) == 'h');
    CHECK(_ch(&vt, 4, 0) == 'o');
    CHECK_EQ(vt100_cursor_col(&vt), 5);

    /* CR/LF positioning. */
    _feed(&vt, "\r\nAB");
    CHECK_EQ(vt100_cursor_row(&vt), 1);
    CHECK_EQ(vt100_cursor_col(&vt), 2);
    CHECK(_ch(&vt, 0, 1) == 'A');
    CHECK(_ch(&vt, 1, 1) == 'B');

    /* Cursor addressing: CSI row;col H (1-based). */
    _feed(&vt, "\x1b[3;5HX");
    CHECK_EQ(vt100_cursor_row(&vt), 2);
    CHECK_EQ(vt100_cursor_col(&vt), 5);
    CHECK(_ch(&vt, 4, 2) == 'X');

    /* Relative movement clamps at the edges. */
    _feed(&vt, "\x1b[99A");
    CHECK_EQ(vt100_cursor_row(&vt), 0);
    _feed(&vt, "\x1b[99D");
    CHECK_EQ(vt100_cursor_col(&vt), 0);

    /* SGR sets colour and attribute bits on the next cell. */
    _feed(&vt, "\x1b[1;31mR");
    const vt100_cell_t *cell = vt100_cell_at(&vt, 0, 0);
    CHECK(cell != NULL);
    CHECK_EQ(cell->fg, 1);
    CHECK(cell->flags & VT100_FLAG_BOLD);
    _feed(&vt, "\x1b[0m");
    _feed(&vt, "\x1b[97mW");
    cell = vt100_cell_at(&vt, 1, 0);
    CHECK_EQ(cell->fg, 15);
    _feed(&vt, "\x1b[7mI");
    CHECK(vt100_cell_at(&vt, 2, 0)->flags & VT100_FLAG_REVERSE);
    _feed(&vt, "\x1b[27m");

    /* Erase in display clears everything. */
    _feed(&vt, "\x1b[2J");
    CHECK(_ch(&vt, 0, 0) == ' ');
    CHECK(_ch(&vt, 4, 2) == ' ');

    /* Erase from the cursor to the end of the line. */
    _feed(&vt, "\x1b[Habcdef\x1b[3G\x1b[K");
    CHECK(_ch(&vt, 0, 0) == 'a');
    CHECK(_ch(&vt, 1, 0) == 'b');
    CHECK(_ch(&vt, 2, 0) == ' ');
    CHECK(_ch(&vt, 5, 0) == ' ');

    /* Tab stops every eight columns. */
    _feed(&vt, "\x1b[2J\x1b[H\tT");
    CHECK(_ch(&vt, 8, 0) == 'T');

    /* Deferred autowrap and scrolling at the bottom row. */
    _reset(&vt);
    vt.cols = 5;
    vt.rows = 3;
    vt.scroll_bottom = 2;
    vt100_clear(&vt);
    _feed(&vt, "abcdefghijklmnop");
    char row[8];
    (void)_row_text(&vt, 0, row, sizeof row);
    CHECK_STR_EQ(row, "fghij");
    (void)_row_text(&vt, 1, row, sizeof row);
    CHECK_STR_EQ(row, "klmno");
    (void)_row_text(&vt, 2, row, sizeof row);
    CHECK_STR_EQ(row, "p    ");

    /* A scroll region leaves rows outside it alone. */
    _reset(&vt);
    vt.cols = 5;
    vt.rows = 4;
    vt100_clear(&vt);
    _feed(&vt, "top");
    _feed(&vt, "\x1b[2;3r");
    CHECK_EQ(vt.scroll_top, 1);
    CHECK_EQ(vt.scroll_bottom, 2);
    CHECK_EQ(vt100_cursor_row(&vt), 1);
    _feed(&vt, "\x1b[3;1Hline\n");
    (void)_row_text(&vt, 0, row, sizeof row);
    CHECK_STR_EQ(row, "top  ");
    (void)_row_text(&vt, 1, row, sizeof row);
    CHECK_STR_EQ(row, "line ");

    /* Device status report answers with the cursor position. */
    _reset(&vt);
    _feed(&vt, "\x1b[2;3H\x1b[6n");
    CHECK_STR_EQ(_capture.reply, "\x1b[2;3R");
    _capture.len = 0;
    _capture.reply[0] = '\0';
    _feed(&vt, "\x1b[5n");
    CHECK_STR_EQ(_capture.reply, "\x1b[0n");

    /* Device attributes answer identifies a VT100 with AVO. */
    _capture.len = 0;
    _capture.reply[0] = '\0';
    _feed(&vt, "\x1b[c");
    CHECK_STR_EQ(_capture.reply, "\x1b[?1;2c");

    /* OSC titles are consumed without printing. */
    _feed(&vt, "\x1b[2J\x1b[H\x1b]0;my title\x07Z");
    CHECK(_ch(&vt, 0, 0) == 'Z');

    /* BEL calls the bell callback. */
    _feed(&vt, "\x07");
    CHECK_EQ(_capture.bells, 1);

    /* Cursor visibility follows DEC private mode 25. */
    _feed(&vt, "\x1b[?25l");
    CHECK(!vt100_cursor_visible(&vt));
    _feed(&vt, "\x1b[?25h");
    CHECK(vt100_cursor_visible(&vt));

    /* Dirty tracking reports the rows that changed. */
    _reset(&vt);
    (void)vt100_take_dirty(&vt);
    _feed(&vt, "x");
    uint32_t dirty = vt100_take_dirty(&vt);
    CHECK_EQ(dirty & 1u, 1u);
    CHECK_EQ(vt100_take_dirty(&vt), 0u);
    vt100_mark_all_dirty(&vt);
    CHECK_EQ(vt100_take_dirty(&vt), (1u << TEST_ROWS) - 1u);

    /* Save and restore. */
    _reset(&vt);
    _feed(&vt, "\x1b[2;2H"
               "\x1b"
               "7"
               "\x1b[1;1H"
               "\x1b"
               "8Q");
    CHECK(_ch(&vt, 1, 1) == 'Q');

    /* Unknown sequences are ignored rather than printed. */
    _reset(&vt);
    _feed(&vt, "\x1b[?9999h\x1b[999zK");
    CHECK(_ch(&vt, 0, 0) == 'K');
}
