#pragma once
/*
 * pipe.h — in-memory pipeline input/output for the shell.
 *
 * Intermediate pipeline stages exchange data through bounded RAM buffers.
 * A command reads the upstream stage with pipe_input_* and can use `-` (or
 * no file operand) to mean "read the pipeline".
 */

#include <stdbool.h>
#include <stddef.h>

/* ---- Stage input -------------------------------------------------- */

void pipe_input_set(const char *data, size_t len);
void pipe_input_clear(void);
bool pipe_input_active(void);
int pipe_input_read(void *dst, size_t cap);
size_t pipe_input_remaining(void);

/* ---- Stage output capture ----------------------------------------- */

void pipe_capture_begin(char *buf, size_t cap);
bool pipe_capture_active(void);
void pipe_capture_char(char c);
bool pipe_capture_overflowed(void);
size_t pipe_capture_end(void);
