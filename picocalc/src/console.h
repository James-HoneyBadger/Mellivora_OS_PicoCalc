#pragma once
/*
 * console.h — blocking line input shared by apps and the HAL.
 */

#include <stddef.h>

/*
 * Prompt and read one line, echoing printable characters and handling
 * backspace. Returns the number of characters read, or -1 on Ctrl-C.
 * The buffer is always NUL-terminated when `size` > 0.
 */
int console_read_line(const char *prompt, char *buf, size_t size);
