/*
 * pipe.c — in-memory pipeline input/output for the shell.
 */

#include "pipe.h"

#include <string.h>

static const char *_in_data;
static size_t _in_len;
static size_t _in_off;

static char *_cap_buf;
static size_t _cap_len;
static size_t _cap_cap;
static bool _cap_active;
static bool _cap_overflow;

void pipe_input_set(const char *data, size_t len) {
    _in_data = data;
    _in_len = data ? len : 0;
    _in_off = 0;
}

void pipe_input_clear(void) {
    _in_data = NULL;
    _in_len = 0;
    _in_off = 0;
}

bool pipe_input_active(void) {
    return _in_data != NULL;
}

int pipe_input_read(void *dst, size_t cap) {
    if (!_in_data || !dst || cap == 0 || _in_off >= _in_len) return 0;

    size_t n = _in_len - _in_off;
    if (n > cap) n = cap;
    memcpy(dst, _in_data + _in_off, n);
    _in_off += n;
    return (int)n;
}

size_t pipe_input_remaining(void) {
    return (_in_data && _in_off < _in_len) ? _in_len - _in_off : 0;
}

void pipe_capture_begin(char *buf, size_t cap) {
    _cap_buf = buf;
    _cap_len = 0;
    _cap_cap = buf ? cap : 0;
    _cap_active = buf != NULL;
    _cap_overflow = false;
    if (_cap_buf && _cap_cap > 0) _cap_buf[0] = '\0';
}

bool pipe_capture_active(void) {
    return _cap_active;
}

void pipe_capture_char(char c) {
    if (!_cap_active || !_cap_buf) return;
    if (_cap_len + 1 < _cap_cap) {
        _cap_buf[_cap_len++] = c;
        _cap_buf[_cap_len] = '\0';
    } else {
        _cap_overflow = true;
    }
}

bool pipe_capture_overflowed(void) {
    return _cap_overflow;
}

size_t pipe_capture_end(void) {
    _cap_active = false;
    return _cap_len;
}
