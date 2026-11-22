/*
 * core/vt100.c — VT100/ANSI emulator.
 *
 * Supported: C0 controls, cursor movement, erase/insert/delete, scrolling
 * regions (DECSTBM), SGR colours and attributes, save/restore cursor, device
 * status and device attribute replies. Unknown escape sequences are consumed
 * without disturbing the screen, which keeps interactive menus readable.
 */

#include "core/vt100.h"

#include <stdio.h>
#include <string.h>

enum {
    VT_GROUND = 0,
    VT_ESC,
    VT_ESC_CHARSET,
    VT_CSI,
    VT_OSC,
    VT_OSC_ESC,
};

static int _clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void _mark(vt100_t *vt, int row) {
    if (row >= 0 && row < vt->rows) vt->dirty_rows |= (uint32_t)1u << (unsigned)row;
}

static void _blank(vt100_t *vt, int col, int row) {
    if (col < 0 || col >= vt->cols || row < 0 || row >= vt->rows) return;
    vt100_cell_t *cell = &vt->cells[(size_t)row * (size_t)vt->cols + (size_t)col];
    cell->ch = ' ';
    cell->fg = vt->fg;
    cell->bg = vt->bg;
    cell->flags = 0;
}

static void _blank_range(vt100_t *vt, int row, int first, int last) {
    if (row < 0 || row >= vt->rows) return;
    first = _clamp(first, 0, vt->cols - 1);
    last = _clamp(last, 0, vt->cols - 1);
    for (int col = first; col <= last; col++)
        _blank(vt, col, row);
    _mark(vt, row);
}

static void _clear_rows(vt100_t *vt, int first, int last) {
    for (int row = first; row <= last; row++)
        _blank_range(vt, row, 0, vt->cols - 1);
}

void vt100_clear(vt100_t *vt) {
    if (!vt) return;
    for (int row = 0; row < vt->rows; row++) {
        for (int col = 0; col < vt->cols; col++) {
            vt100_cell_t *cell = &vt->cells[(size_t)row * (size_t)vt->cols + (size_t)col];
            cell->ch = ' ';
            cell->fg = VT100_COLOR_DEFAULT;
            cell->bg = 0;
            cell->flags = 0;
        }
    }
    vt->dirty_rows = 0;
}

void vt100_mark_all_dirty(vt100_t *vt) {
    if (!vt) return;
    vt->dirty_rows = 0;
    for (int row = 0; row < vt->rows && row < 32; row++)
        vt->dirty_rows |= (uint32_t)1u << (unsigned)row;
}

void vt100_reset(vt100_t *vt) {
    if (!vt) return;
    vt->cx = 0;
    vt->cy = 0;
    vt->saved_cx = 0;
    vt->saved_cy = 0;
    vt->fg = VT100_COLOR_DEFAULT;
    vt->bg = 0;
    vt->flags = 0;
    vt->saved_fg = VT100_COLOR_DEFAULT;
    vt->saved_bg = 0;
    vt->saved_flags = 0;
    vt->autowrap = true;
    vt->wrap_pending = false;
    vt->cursor_visible = true;
    vt->scroll_top = 0;
    vt->scroll_bottom = vt->rows - 1;
    vt->tab_width = 8;
    vt->state = VT_GROUND;
    vt->private_marker = 0;
    vt->param_count = 0;
    vt->param_accum = 0;
    vt->param_present = false;
    vt100_clear(vt);
    vt100_mark_all_dirty(vt);
}

void vt100_init(vt100_t *vt, vt100_cell_t *cells, int cols, int rows) {
    if (!vt || !cells) return;
    memset(vt, 0, sizeof *vt);
    vt->cells = cells;
    vt->cols = _clamp(cols, 1, VT100_MAX_COLS);
    vt->rows = _clamp(rows, 1, VT100_MAX_ROWS);
    vt100_reset(vt);
}

/* ------------------------------------------------------------------ */
/* Scrolling and cursor                                                */
/* ------------------------------------------------------------------ */

static void _scroll_up(vt100_t *vt, int count) {
    if (count <= 0) return;
    int top = vt->scroll_top;
    int bottom = vt->scroll_bottom;
    int height = bottom - top + 1;
    if (count > height) count = height;

    size_t row_bytes = (size_t)vt->cols * sizeof(vt100_cell_t);
    if (count < height) {
        memmove(&vt->cells[(size_t)top * (size_t)vt->cols],
                &vt->cells[(size_t)(top + count) * (size_t)vt->cols],
                (size_t)(height - count) * row_bytes);
    }
    for (int row = bottom - count + 1; row <= bottom; row++) {
        for (int col = 0; col < vt->cols; col++)
            _blank(vt, col, row);
    }
    for (int row = top; row <= bottom; row++)
        _mark(vt, row);
}

static void _scroll_down(vt100_t *vt, int count) {
    if (count <= 0) return;
    int top = vt->scroll_top;
    int bottom = vt->scroll_bottom;
    int height = bottom - top + 1;
    if (count > height) count = height;

    size_t row_bytes = (size_t)vt->cols * sizeof(vt100_cell_t);
    if (count < height) {
        memmove(&vt->cells[(size_t)(top + count) * (size_t)vt->cols],
                &vt->cells[(size_t)top * (size_t)vt->cols], (size_t)(height - count) * row_bytes);
    }
    for (int row = top; row < top + count; row++) {
        for (int col = 0; col < vt->cols; col++)
            _blank(vt, col, row);
    }
    for (int row = top; row <= bottom; row++)
        _mark(vt, row);
}

static void _index_down(vt100_t *vt) {
    vt->wrap_pending = false;
    if (vt->cy == vt->scroll_bottom) _scroll_up(vt, 1);
    else if (vt->cy + 1 < vt->rows)
        vt->cy++;
}

static void _index_up(vt100_t *vt) {
    vt->wrap_pending = false;
    if (vt->cy == vt->scroll_top) _scroll_down(vt, 1);
    else if (vt->cy > 0)
        vt->cy--;
}

static void _print_char(vt100_t *vt, uint8_t ch) {
    if (vt->wrap_pending) {
        vt->cx = 0;
        _index_down(vt);
        vt->wrap_pending = false;
    }
    int col = vt->cx;
    int row = vt->cy;
    if (col < 0 || col >= vt->cols || row < 0 || row >= vt->rows) return;

    vt100_cell_t *cell = &vt->cells[(size_t)row * (size_t)vt->cols + (size_t)col];
    cell->ch = ch;
    cell->fg = vt->fg;
    cell->bg = vt->bg;
    cell->flags = vt->flags;
    _mark(vt, row);

    if (vt->cx + 1 < vt->cols) vt->cx++;
    else if (vt->autowrap)
        vt->wrap_pending = true;
}

static void _tab(vt100_t *vt) {
    int step = vt->tab_width > 0 ? vt->tab_width : 8;
    int next = ((vt->cx / step) + 1) * step;
    vt->cx = _clamp(next, 0, vt->cols - 1);
    vt->wrap_pending = false;
}

/* ------------------------------------------------------------------ */
/* Sequence execution                                                  */
/* ------------------------------------------------------------------ */

static int _param(const vt100_t *vt, int index, int fallback) {
    if (index < 0 || index >= vt->param_count) return fallback;
    int value = vt->params[index];
    return value == 0 ? fallback : value;
}

static void _reply(vt100_t *vt, const char *text) {
    if (vt->write) vt->write(vt->write_ctx, text, strlen(text));
}

static void _sgr(vt100_t *vt) {
    if (vt->param_count == 0) {
        vt->fg = VT100_COLOR_DEFAULT;
        vt->bg = 0;
        vt->flags = 0;
        return;
    }
    for (int i = 0; i < vt->param_count; i++) {
        int code = vt->params[i];
        switch (code) {
        case 0:
            vt->fg = VT100_COLOR_DEFAULT;
            vt->bg = 0;
            vt->flags = 0;
            break;
        case 1:
            vt->flags |= VT100_FLAG_BOLD;
            break;
        case 4:
            vt->flags |= VT100_FLAG_UNDERLINE;
            break;
        case 7:
            vt->flags |= VT100_FLAG_REVERSE;
            break;
        case 22:
            vt->flags &= (uint8_t)~VT100_FLAG_BOLD;
            break;
        case 24:
            vt->flags &= (uint8_t)~VT100_FLAG_UNDERLINE;
            break;
        case 27:
            vt->flags &= (uint8_t)~VT100_FLAG_REVERSE;
            break;
        case 39:
            vt->fg = VT100_COLOR_DEFAULT;
            break;
        case 49:
            vt->bg = 0;
            break;
        default:
            if (code >= 30 && code <= 37) vt->fg = (uint8_t)(code - 30);
            else if (code >= 40 && code <= 47)
                vt->bg = (uint8_t)(code - 40);
            else if (code >= 90 && code <= 97)
                vt->fg = (uint8_t)(code - 90 + 8);
            else if (code >= 100 && code <= 107)
                vt->bg = (uint8_t)(code - 100 + 8);
            break;
        }
    }
}

static void _erase_display(vt100_t *vt, int mode) {
    switch (mode) {
    case 0:
        _blank_range(vt, vt->cy, vt->cx, vt->cols - 1);
        _clear_rows(vt, vt->cy + 1, vt->rows - 1);
        break;
    case 1:
        _clear_rows(vt, 0, vt->cy - 1);
        _blank_range(vt, vt->cy, 0, vt->cx);
        break;
    default:
        _clear_rows(vt, 0, vt->rows - 1);
        break;
    }
    vt->wrap_pending = false;
}

static void _erase_line(vt100_t *vt, int mode) {
    switch (mode) {
    case 0:
        _blank_range(vt, vt->cy, vt->cx, vt->cols - 1);
        break;
    case 1:
        _blank_range(vt, vt->cy, 0, vt->cx);
        break;
    default:
        _blank_range(vt, vt->cy, 0, vt->cols - 1);
        break;
    }
}

static void _insert_lines(vt100_t *vt, int count) {
    if (vt->cy < vt->scroll_top || vt->cy > vt->scroll_bottom) return;
    int height = vt->scroll_bottom - vt->cy + 1;
    if (count > height) count = height;
    size_t row_bytes = (size_t)vt->cols * sizeof(vt100_cell_t);
    if (count < height) {
        memmove(&vt->cells[(size_t)(vt->cy + count) * (size_t)vt->cols],
                &vt->cells[(size_t)vt->cy * (size_t)vt->cols],
                (size_t)(height - count) * row_bytes);
    }
    for (int row = vt->cy; row < vt->cy + count; row++) {
        for (int col = 0; col < vt->cols; col++)
            _blank(vt, col, row);
    }
    for (int row = vt->cy; row <= vt->scroll_bottom; row++)
        _mark(vt, row);
    vt->cx = 0;
}

static void _delete_lines(vt100_t *vt, int count) {
    if (vt->cy < vt->scroll_top || vt->cy > vt->scroll_bottom) return;
    int height = vt->scroll_bottom - vt->cy + 1;
    if (count > height) count = height;
    size_t row_bytes = (size_t)vt->cols * sizeof(vt100_cell_t);
    if (count < height) {
        memmove(&vt->cells[(size_t)vt->cy * (size_t)vt->cols],
                &vt->cells[(size_t)(vt->cy + count) * (size_t)vt->cols],
                (size_t)(height - count) * row_bytes);
    }
    for (int row = vt->scroll_bottom - count + 1; row <= vt->scroll_bottom; row++) {
        for (int col = 0; col < vt->cols; col++)
            _blank(vt, col, row);
    }
    for (int row = vt->cy; row <= vt->scroll_bottom; row++)
        _mark(vt, row);
    vt->cx = 0;
}

static void _delete_chars(vt100_t *vt, int count) {
    int avail = vt->cols - vt->cx;
    if (count > avail) count = avail;
    if (count <= 0) return;
    size_t cell_bytes = sizeof(vt100_cell_t);
    memmove(&vt->cells[(size_t)vt->cy * (size_t)vt->cols + (size_t)vt->cx],
            &vt->cells[(size_t)vt->cy * (size_t)vt->cols + (size_t)(vt->cx + count)],
            (size_t)(avail - count) * cell_bytes);
    _blank_range(vt, vt->cy, vt->cols - count, vt->cols - 1);
}

static void _insert_chars(vt100_t *vt, int count) {
    int avail = vt->cols - vt->cx;
    if (count > avail) count = avail;
    if (count <= 0) return;
    size_t cell_bytes = sizeof(vt100_cell_t);
    memmove(&vt->cells[(size_t)vt->cy * (size_t)vt->cols + (size_t)(vt->cx + count)],
            &vt->cells[(size_t)vt->cy * (size_t)vt->cols + (size_t)vt->cx],
            (size_t)(avail - count) * cell_bytes);
    _blank_range(vt, vt->cy, vt->cx, vt->cx + count - 1);
}

static void _set_scroll_region(vt100_t *vt, int top, int bottom) {
    if (top < 0) top = 0;
    if (bottom >= vt->rows) bottom = vt->rows - 1;
    if (bottom - top < 1) return;
    vt->scroll_top = top;
    vt->scroll_bottom = bottom;
    vt->cx = 0;
    vt->cy = top;
    vt->wrap_pending = false;
}

static void _execute_csi(vt100_t *vt, char final) {
    switch (final) {
    case '@':
        _insert_chars(vt, _param(vt, 0, 1));
        break;
    case 'A':
        vt->cy = _clamp(vt->cy - _param(vt, 0, 1), 0, vt->rows - 1);
        break;
    case 'B':
        vt->cy = _clamp(vt->cy + _param(vt, 0, 1), 0, vt->rows - 1);
        break;
    case 'C':
        vt->cx = _clamp(vt->cx + _param(vt, 0, 1), 0, vt->cols - 1);
        break;
    case 'D':
        vt->cx = _clamp(vt->cx - _param(vt, 0, 1), 0, vt->cols - 1);
        break;
    case 'E':
        vt->cy = _clamp(vt->cy + _param(vt, 0, 1), 0, vt->rows - 1);
        vt->cx = 0;
        break;
    case 'F':
        vt->cy = _clamp(vt->cy - _param(vt, 0, 1), 0, vt->rows - 1);
        vt->cx = 0;
        break;
    case 'G':
        vt->cx = _clamp(_param(vt, 0, 1) - 1, 0, vt->cols - 1);
        break;
    case 'H':
    case 'f':
        vt->cy = _clamp(_param(vt, 0, 1) - 1, 0, vt->rows - 1);
        vt->cx = _clamp(_param(vt, 1, 1) - 1, 0, vt->cols - 1);
        break;
    case 'J':
        _erase_display(vt, _param(vt, 0, 0));
        break;
    case 'K':
        _erase_line(vt, _param(vt, 0, 0));
        break;
    case 'L':
        _insert_lines(vt, _param(vt, 0, 1));
        break;
    case 'M':
        _delete_lines(vt, _param(vt, 0, 1));
        break;
    case 'P':
        _delete_chars(vt, _param(vt, 0, 1));
        break;
    case 'S':
        _scroll_up(vt, _param(vt, 0, 1));
        break;
    case 'T':
        _scroll_down(vt, _param(vt, 0, 1));
        break;
    case 'X':
        _blank_range(vt, vt->cy, vt->cx, vt->cx + _param(vt, 0, 1) - 1);
        break;
    case 'd':
        vt->cy = _clamp(_param(vt, 0, 1) - 1, 0, vt->rows - 1);
        break;
    case 'r':
        _set_scroll_region(vt, _param(vt, 0, 1) - 1, _param(vt, 1, vt->rows) - 1);
        break;
    case 'h':
    case 'l': {
        bool on = (final == 'h');
        for (int i = 0; i < vt->param_count; i++) {
            if (vt->private_marker == '?' && vt->params[i] == 25) vt->cursor_visible = on;
            else if (vt->private_marker == '?' && vt->params[i] == 7)
                vt->autowrap = on;
        }
        break;
    }
    case 'm':
        _sgr(vt);
        break;
    case 'n': {
        char reply[32];
        int mode = _param(vt, 0, 0);
        if (mode == 5) {
            _reply(vt, "\x1b[0n");
        } else if (mode == 6) {
            snprintf(reply, sizeof reply, "\x1b[%d;%dR", vt->cy + 1, vt->cx + 1);
            _reply(vt, reply);
        }
        break;
    }
    case 'c':
        if (vt->private_marker == 0) _reply(vt, "\x1b[?1;2c");
        break;
    case 's':
        vt->saved_cx = vt->cx;
        vt->saved_cy = vt->cy;
        vt->saved_fg = vt->fg;
        vt->saved_bg = vt->bg;
        vt->saved_flags = vt->flags;
        break;
    case 'u':
        vt->cx = vt->saved_cx;
        vt->cy = vt->saved_cy;
        vt->fg = vt->saved_fg;
        vt->bg = vt->saved_bg;
        vt->flags = vt->saved_flags;
        break;
    default:
        break;
    }
    vt->wrap_pending = false;
}

/* ------------------------------------------------------------------ */
/* Byte pump                                                           */
/* ------------------------------------------------------------------ */

void vt100_feed(vt100_t *vt, uint8_t byte) {
    if (!vt) return;

    switch (vt->state) {
    case VT_ESC:
        if (byte == '[') {
            vt->state = VT_CSI;
            vt->param_count = 0;
            vt->param_accum = 0;
            vt->param_present = false;
            vt->private_marker = 0;
            return;
        }
        if (byte == ']') {
            vt->state = VT_OSC;
            return;
        }
        if (byte == '(' || byte == ')' || byte == '*' || byte == '+') {
            vt->state = VT_ESC_CHARSET;
            return;
        }
        vt->state = VT_GROUND;
        switch (byte) {
        case '7':
            vt->saved_cx = vt->cx;
            vt->saved_cy = vt->cy;
            vt->saved_fg = vt->fg;
            vt->saved_bg = vt->bg;
            vt->saved_flags = vt->flags;
            break;
        case '8':
            vt->cx = vt->saved_cx;
            vt->cy = vt->saved_cy;
            vt->fg = vt->saved_fg;
            vt->bg = vt->saved_bg;
            vt->flags = vt->saved_flags;
            break;
        case 'D':
            _index_down(vt);
            break;
        case 'E':
            vt->cx = 0;
            _index_down(vt);
            break;
        case 'M':
            _index_up(vt);
            break;
        case 'c':
            vt100_reset(vt);
            break;
        default:
            break;
        }
        return;

    case VT_ESC_CHARSET:
        vt->state = VT_GROUND;
        return;

    case VT_CSI:
        if (byte >= '0' && byte <= '9') {
            if (vt->param_accum < 100000) vt->param_accum = vt->param_accum * 10 + (byte - '0');
            vt->param_present = true;
            return;
        }
        if (byte == ';') {
            if (vt->param_count < (int)(sizeof vt->params / sizeof vt->params[0]))
                vt->params[vt->param_count++] = vt->param_accum;
            vt->param_accum = 0;
            vt->param_present = false;
            return;
        }
        if ((byte == '?' || byte == '<' || byte == '>' || byte == '=') && vt->param_count == 0 &&
            !vt->param_present) {
            vt->private_marker = (char)byte;
            return;
        }
        if (byte >= 0x40 && byte <= 0x7E) {
            if (vt->param_present &&
                vt->param_count < (int)(sizeof vt->params / sizeof vt->params[0]))
                vt->params[vt->param_count++] = vt->param_accum;
            vt->state = VT_GROUND;
            _execute_csi(vt, (char)byte);
            vt->private_marker = 0;
            return;
        }
        if (byte == 0x1B) {
            vt->state = VT_ESC;
            return;
        }
        /* Intermediate bytes (0x20-0x2F) are ignored. */
        return;

    case VT_OSC:
        if (byte == 0x07) {
            vt->state = VT_GROUND;
            return;
        }
        if (byte == 0x1B) {
            vt->state = VT_OSC_ESC;
            return;
        }
        return;

    case VT_OSC_ESC:
        vt->state = (byte == '\\') ? VT_GROUND : VT_OSC;
        return;

    case VT_GROUND:
    default:
        break;
    }

    if (byte == 0x1B) {
        vt->state = VT_ESC;
        return;
    }
    switch (byte) {
    case 0x07:
        if (vt->bell) vt->bell(vt->bell_ctx);
        return;
    case 0x08:
        if (vt->cx > 0) vt->cx--;
        vt->wrap_pending = false;
        return;
    case 0x09:
        _tab(vt);
        return;
    case 0x0A:
    case 0x0B:
    case 0x0C:
        _index_down(vt);
        return;
    case 0x0D:
        vt->cx = 0;
        vt->wrap_pending = false;
        return;
    default:
        break;
    }
    if (byte < 0x20 || byte == 0x7F) return;
    _print_char(vt, byte);
}

void vt100_feed_bytes(vt100_t *vt, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        vt100_feed(vt, bytes[i]);
}

uint32_t vt100_take_dirty(vt100_t *vt) {
    if (!vt) return 0;
    uint32_t mask = vt->dirty_rows;
    vt->dirty_rows = 0;
    return mask;
}

const vt100_cell_t *vt100_cell_at(const vt100_t *vt, int col, int row) {
    if (!vt || col < 0 || col >= vt->cols || row < 0 || row >= vt->rows) return NULL;
    return &vt->cells[(size_t)row * (size_t)vt->cols + (size_t)col];
}

int vt100_cursor_col(const vt100_t *vt) {
    return vt ? vt->cx : 0;
}

int vt100_cursor_row(const vt100_t *vt) {
    return vt ? vt->cy : 0;
}

bool vt100_cursor_visible(const vt100_t *vt) {
    return vt ? vt->cursor_visible : false;
}
