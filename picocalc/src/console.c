/*
 * console.c — blocking line input shared by apps and the HAL.
 */

#include "console.h"

#include <ctype.h>

#include "syscall.h"

int console_read_line(const char *prompt, char *buf, size_t size) {
    size_t idx = 0;
    if (prompt) sys_print(prompt);
    while (1) {
        int ch = sys_getchar();
        if (ch == '\r' || ch == '\n') {
            sys_putchar('\n');
            break;
        }
        if ((ch == 0x08 || ch == 0x7F) && idx > 0) {
            idx--;
            sys_print("\b \b");
            continue;
        }
        if (ch == 0x03) {
            sys_putchar('\n');
            if (size) buf[0] = '\0';
            return -1;
        }
        if (isprint((unsigned char)ch) && idx + 1 < size) {
            buf[idx++] = (char)ch;
            sys_putchar((char)ch);
        }
    }
    if (size) buf[idx] = '\0';
    return (int)idx;
}
