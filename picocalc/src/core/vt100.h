#pragma once
/*
 * core/vt100.h — VT100/ANSI terminal emulator.
 *
 * Pure state machine over a caller-owned cell buffer. It has no display,
 * timing, or I/O dependency, so the `term` app can render it to the LCD while
 * host tests drive it byte by byte and assert on the resulting cells.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VT100_MAX_COLS 60
#define VT100_MAX_ROWS 24
#define VT100_COLOR_DEFAULT 0xFFu

typedef struct {
    uint8_t ch;
    uint8_t fg; /* 0-7 normal, 8-15 bright, VT100_COLOR_DEFAULT */
    uint8_t bg;
    uint8_t flags; /* VT100_FLAG_* */
} vt100_cell_t;

#define VT100_FLAG_BOLD 0x01u
#define VT100_FLAG_REVERSE 0x02u
#define VT100_FLAG_UNDERLINE 0x04u

/* Called for terminal replies (device status, device attributes). */
typedef void (*vt100_write_fn)(void *ctx, const char *text, size_t len);
typedef void (*vt100_bell_fn)(void *ctx);

typedef struct {
    int cols;
    int rows;
    vt100_cell_t *cells; /* cols * rows, owned by the caller */

    int cx;
    int cy;
    int saved_cx;
    int saved_cy;

    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
    uint8_t saved_fg;
    uint8_t saved_bg;
    uint8_t saved_flags;

    bool autowrap;
    bool wrap_pending;
    bool cursor_visible;

    int scroll_top;
    int scroll_bottom;
    int tab_width;

    uint8_t state;
    char private_marker;
    int params[8];
    int param_count;
    int param_accum;
    bool param_present;

    uint32_t dirty_rows;

    vt100_write_fn write;
    void *write_ctx;
    vt100_bell_fn bell;
    void *bell_ctx;
} vt100_t;

/* `cols`/`rows` are clamped to the VT100_MAX_* limits. */
void vt100_init(vt100_t *vt, vt100_cell_t *cells, int cols, int rows);
void vt100_reset(vt100_t *vt);
void vt100_clear(vt100_t *vt);

void vt100_feed(vt100_t *vt, uint8_t byte);
void vt100_feed_bytes(vt100_t *vt, const void *data, size_t len);

/* Rows changed since the last call; the mask is cleared. */
uint32_t vt100_take_dirty(vt100_t *vt);
void vt100_mark_all_dirty(vt100_t *vt);

const vt100_cell_t *vt100_cell_at(const vt100_t *vt, int col, int row);
int vt100_cursor_col(const vt100_t *vt);
int vt100_cursor_row(const vt100_t *vt);
bool vt100_cursor_visible(const vt100_t *vt);

#ifdef __cplusplus
}
#endif
