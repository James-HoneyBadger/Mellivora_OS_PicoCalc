/*
 * apps/app_tools.c — File/system tools, media toys, and language extras.
 */
#include "apps.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "audio.h"
#include "lcd.h"
#include "pipe.h"
#include "syscall.h"
#include "shell.h"
#include "ui/ui.h"

#define PAINT_SAVE_MAX   (PAINT_WIDTH * PAINT_HEIGHT + PAINT_HEIGHT + 1)

void app_paint(const char *arg) {
    (void)arg;
    static uint8_t canvas[PAINT_WIDTH * PAINT_HEIGHT]; /* 1 byte per pixel */
    memset(canvas, 0, sizeof canvas);

    int x = PAINT_WIDTH / 2, y = PAINT_HEIGHT / 2;
    bool drawing = false;
    sys_clear();
    sys_print("Paint: arrows move, space draw, c clear, s save, l load, q quit\n");
    sys_print("Cursor: [ ]\n");

    while (1) {
        uint32_t color = canvas[y * PAINT_WIDTH + x] ? LCD_WHITE : LCD_BLACK;
        lcd_draw_pixel(x, y, color ^ 0xFFFFFF);

        int ch = sys_getchar();
        color = canvas[y * PAINT_WIDTH + x] ? LCD_WHITE : LCD_BLACK;
        lcd_draw_pixel(x, y, color);

        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'c' || ch == 'C') {
            memset(canvas, 0, sizeof canvas);
            sys_clear();
            sys_print("Paint: cleared\n");
        } else if (ch == 's' || ch == 'S') {
            char filename[32];
            sys_print("Save to: ");
            if (app_read_line("", filename, sizeof filename) >= 0 && *filename) {
                char abs[APP_TOKEN_MAX];
                app_make_abs(filename, abs, sizeof abs);
                char row[PAINT_WIDTH];
                bool ok = fat_create(abs, NULL, 0) == FAT_OK;

                for (int yy = 0; ok && yy < PAINT_HEIGHT; yy++) {
                    for (int xx = 0; xx < PAINT_WIDTH; xx++)
                        row[xx] = canvas[yy * PAINT_WIDTH + xx] ? '#' : ' ';
                    ok = fat_append(abs, (const uint8_t *)row, PAINT_WIDTH) == FAT_OK &&
                         fat_append(abs, (const uint8_t *)"\n", 1) == FAT_OK;
                }
                if (ok) sys_print("Saved\n");
                else ui_error("paint", "save failed");
            }
        } else if (ch == 'l' || ch == 'L') {
            char filename[32];
            sys_print("Load from: ");
            if (app_read_line("", filename, sizeof filename) >= 0 && *filename) {
                char abs[APP_TOKEN_MAX];
                app_make_abs(filename, abs, sizeof abs);
                fat_file_t f;
                if (fat_open(abs, &f) != FAT_OK) {
                    ui_error("paint", "cannot open file");
                } else {
                    memset(canvas, 0, sizeof canvas);
                    uint8_t chunk[128];
                    int xx = 0, yy = 0;
                    int32_t n;
                    while (yy < PAINT_HEIGHT &&
                           (n = fat_read(&f, chunk, sizeof chunk)) > 0) {
                        for (int32_t i = 0; i < n && yy < PAINT_HEIGHT; i++) {
                            char c = (char)chunk[i];
                            if (c == '\r') continue;
                            if (c == '\n') {
                                yy++;
                                xx = 0;
                            } else {
                                if (xx < PAINT_WIDTH)
                                    canvas[yy * PAINT_WIDTH + xx] = (c == '#');
                                xx++;
                            }
                        }
                    }
                    for (int ry = 0; ry < PAINT_HEIGHT; ry++) {
                        for (int rx = 0; rx < PAINT_WIDTH; rx++) {
                            uint32_t px = canvas[ry * PAINT_WIDTH + rx] ? LCD_WHITE : LCD_BLACK;
                            lcd_draw_pixel(rx, ry, px);
                        }
                    }
                    sys_print("Loaded\n");
                }
            }
        } else if (ch == ' ') {
            drawing = !drawing;
            if (drawing) {
                canvas[y * PAINT_WIDTH + x] = 1;
                lcd_draw_pixel(x, y, LCD_WHITE);
            }
        } else if (ch == 0x1b) {
            int seq = sys_getchar();
            if (seq == '[') {
                int dir = sys_getchar();
                if (dir == 'A' && y > 0) y--;
                else if (dir == 'B' && y < PAINT_HEIGHT - 1) y++;
                else if (dir == 'D' && x > 0) x--;
                else if (dir == 'C' && x < PAINT_WIDTH - 1) x++;
            }
        }
        if (drawing) {
            canvas[y * PAINT_WIDTH + x] = 1;
            lcd_draw_pixel(x, y, LCD_WHITE);
        }
    }
    sys_clear();
}

/* ============================================================
 * New utilities: watch, diff, env, lock, xxd, strings, yes, tee
 * ============================================================ */

/* watch CMD [SEC] — repeat a shell command every N seconds */
void app_watch(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) { print_line("usage: watch CMD [SECONDS]"); return; }

    /* Also accept the legacy interval-first form: watch 5 ls. */
    char first[APP_TOKEN_MAX];
    const char *rest = next_token(p, first, sizeof first);
    int interval = 2;
    char cmd_buf[APP_TOKEN_MAX];

    if (*first && isdigit((unsigned char)first[0])) {
        interval = atoi(first);
        copy_cstr(cmd_buf, sizeof cmd_buf, skip_ws(rest));
    } else {
        copy_cstr(cmd_buf, sizeof cmd_buf, p);
        const char *last = strrchr(cmd_buf, ' ');
        if (last && last[1] && isdigit((unsigned char)last[1])) {
            interval = atoi(last + 1);
            cmd_buf[(size_t)(last - cmd_buf)] = '\0';
        }
    }
    if (interval <= 0) interval = 2;
    if (!*cmd_buf) { print_line("usage: watch CMD [SECONDS]"); return; }

    while (1) {
        sys_clear();
        char hdr[APP_TOKEN_MAX + 32];
        snprintf(hdr, sizeof hdr, "Every %ds: %s", interval, cmd_buf);
        print_line(hdr);
        print_line("---");

        (void)shell_run_command(cmd_buf);

        for (int tick = 0; tick < interval * 10; tick++) {
            watchdog_update();
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 'Q' || ch == 0x03) return;
            sleep_ms(100);
        }
    }
}

/* diff FILE1 FILE2 — simple line-by-line diff */
void app_diff(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) { print_line("usage: diff <file1> <file2>"); return; }
    char f1[256], f2[256];

    const char *tok = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t len1 = (size_t)(p - tok);
    if (len1 >= sizeof f1) len1 = sizeof f1 - 1;
    memcpy(f1, tok, len1);
    f1[len1] = '\0';

    p = skip_ws(p);
    if (!*p) { print_line("usage: diff <file1> <file2>"); return; }
    strncpy(f2, p, sizeof f2 - 1);
    f2[sizeof f2 - 1] = '\0';
    /* Trim trailing whitespace */
    for (int i = (int)strlen(f2) - 1; i >= 0 && isspace((unsigned char)f2[i]); i--)
        f2[i] = '\0';

    static char buf1[APP_READ_MAX + 1];
    static char buf2[APP_READ_MAX + 1];
    int n1 = read_text_file(f1, buf1, sizeof buf1, "diff");
    if (n1 < 0) return;
    int n2 = read_text_file(f2, buf2, sizeof buf2, "diff");
    if (n2 < 0) return;

    /* Compare line by line */
    char *l1 = buf1, *l2 = buf2;
    int lineno = 0;
    bool same = true;
    while (*l1 || *l2) {
        lineno++;
        char *e1 = l1; while (*e1 && *e1 != '\n') e1++;
        char *e2 = l2; while (*e2 && *e2 != '\n') e2++;
        size_t s1 = (size_t)(e1 - l1);
        size_t s2 = (size_t)(e2 - l2);

        if (s1 != s2 || memcmp(l1, l2, s1) != 0) {
            same = false;
            char out[80];
            snprintf(out, sizeof out, "%d:", lineno);
            sys_print(out);
            uint32_t sf = lcd_get_fg();
            lcd_set_fg(ui_color(UI_ERROR));
            sys_print("< ");
            char lineout[64];
            size_t cp = s1 < sizeof lineout - 1 ? s1 : sizeof lineout - 1;
            memcpy(lineout, l1, cp); lineout[cp] = '\0';
            print_line(lineout);
            lcd_set_fg(ui_color(UI_SUCCESS));
            sys_print("> ");
            cp = s2 < sizeof lineout - 1 ? s2 : sizeof lineout - 1;
            memcpy(lineout, l2, cp); lineout[cp] = '\0';
            print_line(lineout);
            lcd_set_fg(sf);
        }

        l1 = *e1 ? e1 + 1 : e1;
        l2 = *e2 ? e2 + 1 : e2;
    }
    if (same) print_line("Files are identical");
}

/* xxd — hex dump with offset+ASCII (like Unix xxd) */
void app_xxd(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path && !pipe_input_active()) { print_line("usage: xxd <file>"); return; }
    char *buf = (char *)g_app_io_buf;
    int n = read_text_file(path, buf, APP_READ_MAX, "xxd");
    if (n < 0) return;

    for (int off = 0; off < n; off += 16) {
        char line[80];
        int pos = snprintf(line, sizeof line, "%08x: ", off);
        int end = off + 16 < n ? off + 16 : n;
        for (int i = off; i < end; i++) {
            pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x",
                           (unsigned char)buf[i]);
            if ((i - off) % 2 == 1 && pos < (int)sizeof line - 1)
                line[pos++] = ' ';
        }
        while (pos < 50 && pos < (int)sizeof line - 1) line[pos++] = ' ';
        for (int i = off; i < end && pos < (int)sizeof line - 1; i++) {
            unsigned char c = (unsigned char)buf[i];
            line[pos++] = (c >= 0x20 && c <= 0x7e) ? (char)c : '.';
        }
        line[pos] = '\0';
        print_line(line);
    }
}

/* strings — print printable strings from a file */
void app_strings(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path && !pipe_input_active()) { print_line("usage: strings <file>"); return; }
    char *buf = (char *)g_app_io_buf;
    int n = read_text_file(path, buf, APP_READ_MAX, "strings");
    if (n < 0) return;

    char cur[80];
    int ci = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c >= 0x20 && c <= 0x7e) {
            if (ci < (int)sizeof cur - 1) cur[ci++] = (char)c;
        } else {
            if (ci >= 4) { cur[ci] = '\0'; print_line(cur); }
            ci = 0;
        }
    }
    if (ci >= 4) { cur[ci] = '\0'; print_line(cur); }
}

/* yes [STRING] — repeatedly print a string */
void app_yes(const char *arg) {
    const char *s = skip_ws(arg);
    if (!*s) s = "y";
    while (1) {
        watchdog_update();
        print_line(s);
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
        if (ch == 'q' || ch == 0x03) return;
    }
}

/* tee FILE — read stdin (line-by-line), write to both screen and file */
void app_tee(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: tee <file>"); return; }

    if (pipe_input_active()) {
        char *data = (char *)g_app_io_buf;
        int n = pipe_input_read(data, APP_READ_MAX);
        if (n < 0) n = 0;
        data[n] = '\0';
        if (sys_fwrite(path, data, (uint32_t)n) < 0) {
            print_line("tee: write failed");
            return;
        }
        for (int i = 0; i < n; i++) sys_putchar(data[i]);
        return;
    }

    char buf[1024];
    int pos = 0;
    print_line("Enter text (Ctrl+D to finish):");
    while (1) {
        int ch = sys_getchar();
        if (ch == 0x04 || ch == 0x03) break; /* Ctrl+D or Ctrl+C */
        if (ch == '\r' || ch == '\n') {
            sys_putchar('\n');
            if (pos < (int)sizeof buf - 1) buf[pos++] = '\n';
            continue;
        }
        sys_putchar((char)ch);
        if (pos < (int)sizeof buf - 1) buf[pos++] = (char)ch;
    }
    buf[pos] = '\0';
    sys_fwrite(path, buf, (uint32_t)pos);
    char msg[64];
    snprintf(msg, sizeof msg, "\nWrote %d bytes to %s", pos, path);
    print_line(msg);
}

/* base64 encode/decode */
static const char _b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void app_base64(const char *arg) {
    const char *s = skip_ws(arg);
    bool decode = false;
    char out_arg[APP_TOKEN_MAX];
    out_arg[0] = '\0';

    /* Parse options: -d (decode) and -o OUTFILE (output path), order-independent */
    while (s[0] == '-' && s[1]) {
        if (s[1] == 'd' && (s[2] == ' ' || s[2] == '\0')) {
            decode = true;
            s = skip_ws(s + 2);
        } else if (s[1] == 'o' && (s[2] == ' ' || s[2] == '\0')) {
            s = skip_ws(s + 2);
            next_token(s, out_arg, sizeof out_arg);
            while (*s && !isspace((unsigned char)*s)) s++;
            s = skip_ws(s);
        } else {
            break;
        }
    }

    char path[APP_TOKEN_MAX];
    next_token(s, path, sizeof path);
    if (!*path && !pipe_input_active()) { print_line("usage: base64 [-d] [-o OUT] FILE"); return; }

    uint8_t *buf = g_app_io_buf;
    int n = read_text_file(path, (char *)buf, APP_READ_MAX, "base64");
    if (n < 0) return;

    if (!decode) {
        /* Encode */
        char line_out[80];
        int lo = 0;
        int col = 0;
        for (int i = 0; i < n; i += 3) {
            uint32_t b0 = (uint8_t)buf[i];
            uint32_t b1 = (i + 1 < n) ? (uint8_t)buf[i + 1] : 0;
            uint32_t b2 = (i + 2 < n) ? (uint8_t)buf[i + 2] : 0;
            uint32_t v  = (b0 << 16) | (b1 << 8) | b2;
            line_out[lo++] = _b64_enc[(v >> 18) & 0x3F];
            line_out[lo++] = _b64_enc[(v >> 12) & 0x3F];
            line_out[lo++] = (i + 1 < n) ? _b64_enc[(v >> 6) & 0x3F] : '=';
            line_out[lo++] = (i + 2 < n) ? _b64_enc[ v       & 0x3F] : '=';
            col += 4;
            if (col >= 76) {
                line_out[lo] = '\0';
                print_line(line_out);
                lo = col = 0;
            }
        }
        if (lo > 0) { line_out[lo] = '\0'; print_line(line_out); }
    } else {
        /* Decode */
        static uint8_t dec_buf[APP_READ_MAX];
        int di = 0;
        uint32_t acc = 0;
        int bits = 0;
        for (int i = 0; i < n && di < APP_READ_MAX; i++) {
            char c = (char)buf[i];
            int v = -1;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') break;
            if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                dec_buf[di++] = (uint8_t)((acc >> bits) & 0xFF);
            }
        }
        /* Decide where to write the decoded output. Never overwrite the
           input file (the original bug). Priority:
             1) explicit -o OUT
             2) strip trailing ".b64"/".B64" from input path
             3) append ".dec" to input path
           If even the derived path equals the input, fall back to printing
           the decoded bytes as text. */
        char out_path[APP_TOKEN_MAX];
        out_path[0] = '\0';
        if (out_arg[0]) {
            strncpy(out_path, out_arg, sizeof out_path - 1);
            out_path[sizeof out_path - 1] = '\0';
        } else {
            size_t plen = strlen(path);
            if (plen > 4 && (strcmp(path + plen - 4, ".b64") == 0 ||
                             strcmp(path + plen - 4, ".B64") == 0)) {
                size_t copy = plen - 4;
                if (copy >= sizeof out_path) copy = sizeof out_path - 1;
                memcpy(out_path, path, copy);
                out_path[copy] = '\0';
            } else if (plen + 4 < sizeof out_path) {
                snprintf(out_path, sizeof out_path, "%s.dec", path);
            }
        }

        bool wrote = false;
        if (out_path[0] && strcmp(out_path, path) != 0) {
            if (sys_fwrite(out_path, (const char *)dec_buf, (uint32_t)di) >= 0) {
                char msg[APP_TOKEN_MAX + 32];
                snprintf(msg, sizeof msg, "Decoded %d bytes -> %s", di, out_path);
                print_line(msg);
                wrote = true;
            } else {
                print_line("base64: cannot write output file");
            }
        }
        if (!wrote) {
            /* Fall back to printing decoded text (NUL-terminated). */
            if (di < APP_READ_MAX) dec_buf[di] = '\0';
            else dec_buf[APP_READ_MAX - 1] = '\0';
            print_line((const char *)dec_buf);
        }
    }
}

/* crc32 FILE — compute CRC-32 checksum */
void app_crc32(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path && !pipe_input_active()) { print_line("usage: crc32 FILE"); return; }

    uint8_t *buf = g_app_io_buf;
    int n = read_text_file(path, (char *)buf, APP_READ_MAX, "crc32");
    if (n < 0) return;

    /* CRC-32/ISO-HDLC polynomial 0xEDB88320 */
    uint32_t crc = 0xFFFFFFFFUL;
    for (int i = 0; i < n; i++) {
        uint32_t b = (uint8_t)buf[i];
        crc ^= b;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1)));
    }
    crc ^= 0xFFFFFFFFUL;

    char out[80];
    snprintf(out, sizeof out, "%08lX  %s", (unsigned long)crc, path);
    print_line(out);
}

/* Mandelbrot fractal renderer */
void app_mandelbrot(const char *arg) {
    (void)arg;
    sys_clear();
    print_line("Mandelbrot set (computing...)");

    /* Render to text grid: 38 cols x 17 rows */
    #define MBW 38
    #define MBH 17
    static char grid[MBH][MBW + 1];

    double x_min = -2.0, x_max = 1.0;
    double y_min = -1.2, y_max = 1.2;
    const char *shade = " .:-=+*#%@";
    int shade_len = 10;
    int max_iter = 50;

    for (int row = 0; row < MBH; row++) {
        for (int col = 0; col < MBW; col++) {
            double cr = x_min + (x_max - x_min) * col / MBW;
            double ci = y_min + (y_max - y_min) * row / MBH;
            double zr = 0, zi = 0;
            int iter = 0;
            while (zr * zr + zi * zi < 4.0 && iter < max_iter) {
                double tmp = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = tmp;
                iter++;
            }
            int idx = iter * (shade_len - 1) / max_iter;
            if (idx >= shade_len) idx = shade_len - 1;
            grid[row][col] = shade[idx];
        }
        grid[row][MBW] = '\0';
    }

    sys_clear();
    print_line("Mandelbrot set [-2,1]x[-1.2,1.2]");
    for (int row = 0; row < MBH; row++) print_line(grid[row]);
    print_line("Press any key...");
    (void)sys_getchar();
    sys_clear();
}

/* Piano — PWM audio using hardware PWM on GPIO 26/27 */
void app_piano(const char *arg) {
    (void)arg;
    sys_clear();
    print_line("Piano — press keys to play notes");
    print_line("  a s d f g h j k = C D E F G A B C'");
    print_line("  q = quit");

    /* Note frequencies (Hz) for one octave */
    static const uint16_t notes[] = {
        262, 294, 330, 349, 392, 440, 494, 523
    };
    static const char keys[] = "asdfghjk";

    audio_init();

    while (1) {
        int ch = sys_getchar();
        if (ch == 'q' || ch == 0x03) break;

        /* Find note */
        const char *k = strchr(keys, ch);
        if (!k) continue;
        int idx = (int)(k - keys);

        audio_beep(notes[idx], 200);
    }
    audio_stop();
    sys_clear();
}

/* Forth interpreter — minimal Forth with a small stack */
#define FORTH_STACK_SZ 64
#define FORTH_DICT_SZ 32
#define FORTH_WORD_MAX 32

typedef struct {
    char name[FORTH_WORD_MAX];
    char body[128];
} forth_word_t;

/* Execute a Forth body (used both for `:`-defined words and recursively for
   nested user-defined word calls). Returns nothing; mutates *psp and stack[].
   `depth` guards against runaway recursion / mutual recursion. */
static void _forth_exec_body(const char *body,
                             int32_t *stack, int *psp,
                             const forth_word_t *dict, int nwords,
                             int depth) {
    if (depth > 16) { print_line("forth: nesting too deep"); return; }
    char body_copy[128];
    strncpy(body_copy, body, sizeof body_copy - 1);
    body_copy[sizeof body_copy - 1] = '\0';
    char *bt = body_copy;
    int sp = *psp;
    while (*bt) {
        while (*bt == ' ') bt++;
        if (!*bt) break;
        char bw[FORTH_WORD_MAX];
        int bwi = 0;
        while (*bt && *bt != ' ' && bwi < FORTH_WORD_MAX - 1) bw[bwi++] = *bt++;
        bw[bwi] = '\0';

        char *endp;
        long val = strtol(bw, &endp, 10);
        if (*endp == '\0' && bwi > 0) {
            if (sp < FORTH_STACK_SZ) stack[sp++] = (int32_t)val;
        } else if (strcmp(bw, ".") == 0) {
            if (sp > 0) { char o[16]; snprintf(o, sizeof o, "%ld ", (long)stack[--sp]); sys_print(o); }
        } else if (strcmp(bw, "+") == 0) { if (sp >= 2) { sp--; stack[sp-1] += stack[sp]; } }
        else if (strcmp(bw, "-") == 0) { if (sp >= 2) { sp--; stack[sp-1] -= stack[sp]; } }
        else if (strcmp(bw, "*") == 0) { if (sp >= 2) { sp--; stack[sp-1] *= stack[sp]; } }
        else if (strcmp(bw, "/") == 0) { if (sp >= 2 && stack[sp-1]) { sp--; stack[sp-1] /= stack[sp]; } }
        else if (strcmp(bw, "mod") == 0) { if (sp >= 2 && stack[sp-1]) { sp--; stack[sp-1] %= stack[sp]; } }
        else if (strcmp(bw, "=") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] == stack[sp]) ? -1 : 0; } }
        else if (strcmp(bw, "<") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] <  stack[sp]) ? -1 : 0; } }
        else if (strcmp(bw, ">") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] >  stack[sp]) ? -1 : 0; } }
        else if (strcmp(bw, "dup") == 0) { if (sp > 0 && sp < FORTH_STACK_SZ) { stack[sp] = stack[sp-1]; sp++; } }
        else if (strcmp(bw, "drop") == 0) { if (sp > 0) sp--; }
        else if (strcmp(bw, "swap") == 0) { if (sp >= 2) { int32_t t = stack[sp-1]; stack[sp-1] = stack[sp-2]; stack[sp-2] = t; } }
        else if (strcmp(bw, "over") == 0) { if (sp >= 2 && sp < FORTH_STACK_SZ) { stack[sp] = stack[sp-2]; sp++; } }
        else if (strcmp(bw, "cr") == 0) { sys_putchar('\n'); }
        else if (strcmp(bw, "emit") == 0) { if (sp > 0) sys_putchar((char)stack[--sp]); }
        else {
            /* Look up nested user-defined word */
            bool found = false;
            for (int i = nwords - 1; i >= 0; i--) {
                if (strcmp(bw, dict[i].name) == 0) {
                    *psp = sp;
                    _forth_exec_body(dict[i].body, stack, psp, dict, nwords, depth + 1);
                    sp = *psp;
                    found = true;
                    break;
                }
            }
            if (!found) {
                char msg[48];
                snprintf(msg, sizeof msg, "%s ?", bw);
                print_line(msg);
            }
        }
    }
    *psp = sp;
}

void app_forth(const char *arg) {
    (void)arg;
    int32_t stack[FORTH_STACK_SZ];
    int sp = 0;
    forth_word_t dict[FORTH_DICT_SZ];
    int nwords = 0;

    sys_clear();
    print_line("Forth interpreter (type 'bye' to exit)");

    char line[128];
    while (1) {
        sys_print("forth> ");
        int li = 0;
        while (1) {
            int ch = sys_getchar();
            if (ch == '\r' || ch == '\n') { sys_putchar('\n'); break; }
            if ((ch == 0x08 || ch == 0x7F) && li > 0) {
                li--; sys_print("\b \b"); continue;
            }
            if (ch == 0x03) { sys_clear(); return; }
            if (isprint((unsigned char)ch) && li < (int)sizeof line - 1) {
                line[li++] = (char)ch;
                sys_putchar((char)ch);
            }
        }
        line[li] = '\0';

        /* Tokenize and execute */
        char *tok = line;
        bool defining = false;
        char def_name[FORTH_WORD_MAX] = {0};
        char def_body[128] = {0};
        int def_bpos = 0;

        while (*tok) {
            while (*tok == ' ') tok++;
            if (!*tok) break;
            char word[FORTH_WORD_MAX];
            int wi = 0;
            while (*tok && *tok != ' ' && wi < FORTH_WORD_MAX - 1)
                word[wi++] = (char)tolower((unsigned char)*tok++);
            word[wi] = '\0';

            if (defining) {
                if (strcmp(word, ";") == 0) {
                    defining = false;
                    if (nwords < FORTH_DICT_SZ) {
                        memcpy(dict[nwords].name, def_name, FORTH_WORD_MAX);
                        dict[nwords].name[FORTH_WORD_MAX - 1] = '\0';
                        memcpy(dict[nwords].body, def_body, sizeof dict[0].body);
                        dict[nwords].body[sizeof dict[0].body - 1] = '\0';
                        nwords++;
                    }
                } else {
                    if (def_bpos > 0 && def_bpos < (int)sizeof def_body - 1)
                        def_body[def_bpos++] = ' ';
                    for (int i = 0; word[i] && def_bpos < (int)sizeof def_body - 1; i++)
                        def_body[def_bpos++] = word[i];
                    def_body[def_bpos] = '\0';
                }
                continue;
            }

            if (strcmp(word, "bye") == 0) { sys_clear(); return; }
            if (strcmp(word, ":") == 0) {
                defining = true;
                def_bpos = 0;
                def_body[0] = '\0';
                /* Next word is the name */
                while (*tok == ' ') tok++;
                wi = 0;
                while (*tok && *tok != ' ' && wi < FORTH_WORD_MAX - 1)
                    def_name[wi++] = (char)tolower((unsigned char)*tok++);
                def_name[wi] = '\0';
                continue;
            }
            if (strcmp(word, ".") == 0) {
                if (sp > 0) {
                    char out[16];
                    snprintf(out, sizeof out, "%ld ", (long)stack[--sp]);
                    sys_print(out);
                } else sys_print("stack underflow ");
                continue;
            }
            if (strcmp(word, ".s") == 0) {
                char out[16];
                snprintf(out, sizeof out, "<%d> ", sp);
                sys_print(out);
                for (int i = 0; i < sp; i++) {
                    snprintf(out, sizeof out, "%ld ", (long)stack[i]);
                    sys_print(out);
                }
                sys_putchar('\n');
                continue;
            }
            if (strcmp(word, "cr") == 0) { sys_putchar('\n'); continue; }
            if (strcmp(word, "dup") == 0) {
                if (sp > 0 && sp < FORTH_STACK_SZ) { stack[sp] = stack[sp-1]; sp++; }
                continue;
            }
            if (strcmp(word, "drop") == 0) { if (sp > 0) sp--; continue; }
            if (strcmp(word, "swap") == 0) {
                if (sp >= 2) { int32_t t = stack[sp-1]; stack[sp-1] = stack[sp-2]; stack[sp-2] = t; }
                continue;
            }
            if (strcmp(word, "over") == 0) {
                if (sp >= 2 && sp < FORTH_STACK_SZ) { stack[sp] = stack[sp-2]; sp++; }
                continue;
            }
            if (strcmp(word, "+") == 0) { if (sp >= 2) { sp--; stack[sp-1] += stack[sp]; } continue; }
            if (strcmp(word, "-") == 0) { if (sp >= 2) { sp--; stack[sp-1] -= stack[sp]; } continue; }
            if (strcmp(word, "*") == 0) { if (sp >= 2) { sp--; stack[sp-1] *= stack[sp]; } continue; }
            if (strcmp(word, "/") == 0) {
                if (sp >= 2 && stack[sp-1] != 0) { sp--; stack[sp-1] /= stack[sp]; }
                else if (sp >= 2) { print_line("div by zero"); sp -= 2; }
                continue;
            }
            if (strcmp(word, "mod") == 0) {
                if (sp >= 2 && stack[sp-1] != 0) { sp--; stack[sp-1] %= stack[sp]; }
                continue;
            }
            if (strcmp(word, "=") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] == stack[sp]) ? -1 : 0; } continue; }
            if (strcmp(word, "<") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] < stack[sp]) ? -1 : 0; } continue; }
            if (strcmp(word, ">") == 0) { if (sp >= 2) { sp--; stack[sp-1] = (stack[sp-1] > stack[sp]) ? -1 : 0; } continue; }
            if (strcmp(word, "emit") == 0) { if (sp > 0) sys_putchar((char)stack[--sp]); continue; }
            if (strcmp(word, "words") == 0) {
                for (int i = 0; i < nwords; i++) { sys_print(dict[i].name); sys_putchar(' '); }
                sys_putchar('\n');
                continue;
            }

            /* Check dictionary */
            bool found = false;
            for (int i = nwords - 1; i >= 0; i--) {
                if (strcmp(word, dict[i].name) == 0) {
                    _forth_exec_body(dict[i].body, stack, &sp, dict, nwords, 0);
                    found = true;
                    break;
                }
            }
            if (found) continue;

            /* Try as number */
            char *endp;
            long val = strtol(word, &endp, 10);
            if (*endp == '\0' && wi > 0) {
                if (sp < FORTH_STACK_SZ) stack[sp++] = (int32_t)val;
            } else {
                char msg[48];
                snprintf(msg, sizeof msg, "%s ?", word);
                print_line(msg);
            }
        }
        if (defining) print_line("  compiled");
    }
}

/* ============================================================
 * System: xmodem, theme
 * ============================================================ */

/* XMODEM receive (128-byte blocks, checksum mode) over USB serial */
#define XMODEM_SOH 0x01
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_CAN 0x18

static int _xmodem_getc(uint32_t timeout_ms) {
    uint32_t start = sys_time_ms();
    while (sys_time_ms() - start < timeout_ms) {
        int ch = getchar_timeout_us(1000);
        if (ch != PICO_ERROR_TIMEOUT) return ch;
    }
    return -1;
}

void app_xmodem(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: xmodem <filename>"); return; }

    print_line("XMODEM receive: send file now...");
    print_line("(Start transfer in your terminal)");

#if defined(PICO_RP2350A) || defined(PICO_RP2350)
    static uint8_t filebuf[65536]; /* 512 blocks on Pico 2 */
#else
    static uint8_t filebuf[16384]; /* 128 blocks on Pico 1 */
#endif
    int total = 0;
    int expected_blk = 1;

    /* Send NAK to initiate */
    putchar_raw(XMODEM_NAK);

    while (1) {
        int header = _xmodem_getc(10000);
        if (header == XMODEM_EOT) {
            putchar_raw(XMODEM_ACK);
            break;
        }
        if (header == XMODEM_CAN) {
            print_line("Transfer cancelled by sender");
            return;
        }
        if (header != XMODEM_SOH) {
            print_line("XMODEM: bad header");
            putchar_raw(XMODEM_NAK);
            continue;
        }

        int blk = _xmodem_getc(1000);
        int blk_inv = _xmodem_getc(1000);
        if (blk < 0 || blk_inv < 0 || (blk + blk_inv) != 0xFF) {
            putchar_raw(XMODEM_NAK);
            continue;
        }

        uint8_t data[128];
        uint8_t cksum = 0;
        bool ok = true;
        for (int i = 0; i < 128; i++) {
            int b = _xmodem_getc(1000);
            if (b < 0) { ok = false; break; }
            data[i] = (uint8_t)b;
            cksum += data[i];
        }
        int recv_ck = _xmodem_getc(1000);
        if (!ok || recv_ck < 0 || (uint8_t)recv_ck != cksum) {
            putchar_raw(XMODEM_NAK);
            continue;
        }

        if (blk == (expected_blk & 0xFF)) {
            if (total + 128 > (int)sizeof filebuf) {
                putchar_raw(XMODEM_CAN);
                putchar_raw(XMODEM_CAN);
                ui_error("xmodem", "file exceeds receive buffer; transfer aborted");
                return;
            }
            memcpy(filebuf + total, data, 128);
            total += 128;
            expected_blk++;
        }
        putchar_raw(XMODEM_ACK);
    }

    /* Write to file */
    sys_fwrite(path, (const char *)filebuf, (uint32_t)total);
    char msg[64];
    snprintf(msg, sizeof msg, "Received %d bytes -> %s", total, path);
    print_line(msg);
}
