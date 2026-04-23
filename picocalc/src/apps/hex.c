/*
 * apps/hex.c -- hex dump and interactive hex editor
 */
#include "apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "syscall.h"

static void app_hexdump_core(const char *arg, const char *label) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        sys_print("usage: ");
        sys_print(label);
        sys_print(" FILE\n");
        return;
    }

    char buf[APP_READ_MAX + 1];
    int n = read_text_file(path, buf, sizeof buf, label);
    if (n < 0) return;

    for (int offset = 0; offset < n; offset += 16) {
        char line[160];
        int pos = snprintf(line, sizeof line, "%08X  ", offset);
        for (int i = 0; i < 16; i++) {
            if (offset + i < n)
                pos += snprintf(line + pos, sizeof line - pos, "%02X ",
                                (unsigned char)buf[offset + i]);
            else
                pos += snprintf(line + pos, sizeof line - pos, "   ");
        }
        pos += snprintf(line + pos, sizeof line - pos, " |");
        for (int i = 0; i < 16 && offset + i < n; i++) {
            unsigned char ch = (unsigned char)buf[offset + i];
            line[pos++] = isprint(ch) ? (char)ch : '.';
        }
        line[pos++] = '|';
        line[pos] = '\0';
        print_line(line);
    }
}

void app_hexdump(const char *arg) {
    app_hexdump_core(arg, "hexdump");
}

void app_od(const char *arg) {
    app_hexdump_core(arg, "od");
}

static bool parse_hex_byte_token(const char *tok, uint8_t *out) {
    if (!tok || !*tok || !out) return false;
    char *endptr = NULL;
    long value = strtol(tok, &endptr, 16);
    if (endptr == tok || *endptr != '\0' || value < 0 || value > 255) return false;
    *out = (uint8_t)value;
    return true;
}

static void app_hexedit_dump(const uint8_t *buf, size_t len, size_t start, size_t span) {
    if (!buf || len == 0) {
        print_line("hexedit: empty buffer");
        return;
    }
    if (start >= len) {
        print_line("hexedit: offset beyond end of file");
        return;
    }

    size_t end = start + span;
    if (end > len || end < start) end = len;

    for (size_t offset = start; offset < end; offset += 16) {
        char line[160];
        int pos = snprintf(line, sizeof line, "%08lX  ", (unsigned long)offset);
        for (int i = 0; i < 16; i++) {
            size_t idx = offset + (size_t)i;
            if (idx < end)
                pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02X ", buf[idx]);
            else
                pos += snprintf(line + pos, sizeof line - (size_t)pos, "   ");
        }
        pos += snprintf(line + pos, sizeof line - (size_t)pos, " |");
        for (int i = 0; i < 16 && offset + (size_t)i < end; i++) {
            unsigned char ch = buf[offset + (size_t)i];
            if (pos + 2 >= (int)sizeof line) break;
            line[pos++] = isprint(ch) ? (char)ch : '.';
        }
        if (pos + 2 < (int)sizeof line) {
            line[pos++] = '|';
            line[pos] = '\0';
        } else {
            line[sizeof line - 1] = '\0';
        }
        print_line(line);
    }
}

void app_hexedit(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: hexedit FILE");
        return;
    }

    char target[APP_TOKEN_MAX];
    app_make_abs(path, target, sizeof target);

    static uint8_t buf[HEXEDIT_BYTES_MAX];
    memset(buf, 0, sizeof buf);

    int n = sys_fread(target, buf, (uint32_t)sizeof buf);
    size_t len = 0;
    bool dirty = false;

    if (n >= 0) {
        len = (size_t)n;
        if ((size_t)n == sizeof buf)
            print_line("hexedit: showing first 4096 bytes of file");
    } else {
        print_line("hexedit: file not found; starting empty buffer");
    }

    print_line("Mellivora hex editor");
    print_line("Commands: view [OFF [LEN]], set OFF HH.., ascii OFF TEXT, fill OFF LEN HH");
    print_line("          resize N, save, saveas PATH, info, quit");
    if (len > 0) app_hexedit_dump(buf, len, 0, 128);

    while (1) {
        char line[APP_TOKEN_MAX];
        char cmd[APP_TOKEN_MAX];
        if (app_read_line("hex> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        const char *s = next_token(line, cmd, sizeof cmd);

        if (!*cmd || ci_eq(cmd, "view") || ci_eq(cmd, "list") || ci_eq(cmd, "ls")) {
            char off_tok[32] = {0};
            char len_tok[32] = {0};
            next_token(next_token(s, off_tok, sizeof off_tok), len_tok, sizeof len_tok);
            size_t start = *off_tok ? (size_t)strtoul(off_tok, NULL, 0) : 0;
            size_t span  = *len_tok ? (size_t)strtoul(len_tok, NULL, 0) : 128;
            if (span == 0) span = 128;
            app_hexedit_dump(buf, len, start, span);
            continue;
        }

        if (ci_eq(cmd, "info") || ci_eq(cmd, "status")) {
            char out[160];
            snprintf(out, sizeof out, "file: %.96s | size: %lu bytes | %s",
                     target, (unsigned long)len, dirty ? "modified" : "saved");
            print_line(out);
            continue;
        }

        if (ci_eq(cmd, "set")) {
            char off_tok[32];
            s = next_token(s, off_tok, sizeof off_tok);
            if (!*off_tok) { print_line("usage: set OFFSET HH [HH ...]"); continue; }
            size_t pos = (size_t)strtoul(off_tok, NULL, 0);
            int changed = 0;
            while (1) {
                char byte_tok[16];
                uint8_t value;
                s = next_token(s, byte_tok, sizeof byte_tok);
                if (!*byte_tok) break;
                if (!parse_hex_byte_token(byte_tok, &value)) {
                    print_line("hexedit: bytes must be hex values like FF or 0A");
                    changed = -1;
                    break;
                }
                if (pos >= sizeof buf) { print_line("hexedit: write exceeds buffer limit"); break; }
                buf[pos++] = value;
                changed++;
            }
            if (changed > 0) {
                if (pos > len) len = pos;
                dirty = true;
                print_line("hexedit: bytes updated");
            } else if (changed == 0) {
                print_line("usage: set OFFSET HH [HH ...]");
            }
            continue;
        }

        if (ci_eq(cmd, "ascii")) {
            char off_tok[32];
            s = next_token(s, off_tok, sizeof off_tok);
            const char *text = skip_ws(s);
            if (!*off_tok || !*text) { print_line("usage: ascii OFFSET TEXT"); continue; }
            size_t pos = (size_t)strtoul(off_tok, NULL, 0);
            size_t wrote = 0;
            while (*text && pos < sizeof buf) { buf[pos++] = (uint8_t)*text++; wrote++; }
            if (wrote == 0) { print_line("hexedit: nothing written"); }
            else { if (pos > len) len = pos; dirty = true; print_line("hexedit: text written"); }
            continue;
        }

        if (ci_eq(cmd, "fill")) {
            char off_tok[32], len_tok[32], byte_tok[16];
            s = next_token(s, off_tok, sizeof off_tok);
            s = next_token(s, len_tok, sizeof len_tok);
            next_token(s, byte_tok, sizeof byte_tok);
            uint8_t value;
            if (!*off_tok || !*len_tok || !parse_hex_byte_token(byte_tok, &value)) {
                print_line("usage: fill OFFSET LEN HH"); continue;
            }
            size_t pos  = (size_t)strtoul(off_tok, NULL, 0);
            size_t span = (size_t)strtoul(len_tok, NULL, 0);
            if (pos >= sizeof buf) { print_line("hexedit: offset beyond buffer"); continue; }
            if (span > sizeof buf - pos) span = sizeof buf - pos;
            memset(buf + pos, value, span);
            if (pos + span > len) len = pos + span;
            dirty = true;
            print_line("hexedit: range filled");
            continue;
        }

        if (ci_eq(cmd, "resize")) {
            char size_tok[32];
            next_token(s, size_tok, sizeof size_tok);
            size_t new_len = (size_t)strtoul(size_tok, NULL, 0);
            if (!*size_tok || new_len > sizeof buf) {
                print_line("usage: resize N   (max 4096)"); continue;
            }
            if (new_len > len) memset(buf + len, 0, new_len - len);
            len = new_len;
            dirty = true;
            print_line("hexedit: size updated");
            continue;
        }

        if (ci_eq(cmd, "find")) {
            uint8_t needle[32];
            int nlen = 0;
            const char *fp = skip_ws(s);
            while (*fp && nlen < (int)sizeof needle) {
                char hb[3] = {0};
                if (!isxdigit((unsigned char)fp[0])) break;
                hb[0] = fp[0];
                hb[1] = (fp[1] && isxdigit((unsigned char)fp[1])) ? fp[1] : '\0';
                needle[nlen++] = (uint8_t)strtoul(hb, NULL, 16);
                fp += hb[1] ? 2 : 1;
                while (*fp == ' ') fp++;
            }
            if (nlen == 0) { print_line("usage: find XX [XX ...]"); continue; }
            bool found = false;
            for (size_t i = 0; i + (size_t)nlen <= len; i++) {
                if (memcmp(buf + i, needle, (size_t)nlen) == 0) {
                    char msg[64];
                    snprintf(msg, sizeof msg, "found at offset 0x%04X (%u)",
                             (unsigned)i, (unsigned)i);
                    print_line(msg);
                    found = true;
                }
            }
            if (!found) print_line("hexedit: pattern not found");
            continue;
        }

        if (ci_eq(cmd, "saveas")) {
            char new_path[APP_TOKEN_MAX];
            next_token(s, new_path, sizeof new_path);
            if (!*new_path) { print_line("usage: saveas PATH"); continue; }
            app_make_abs(new_path, target, sizeof target);
            if (sys_fwrite(target, buf, (uint32_t)len) < 0) print_line("hexedit: save failed");
            else { dirty = false; print_line("hexedit: file saved"); }
            continue;
        }

        if (ci_eq(cmd, "save") || ci_eq(cmd, "write") || ci_eq(cmd, "w")) {
            if (sys_fwrite(target, buf, (uint32_t)len) < 0) print_line("hexedit: save failed");
            else { dirty = false; print_line("hexedit: file saved"); }
            continue;
        }

        if (ci_eq(cmd, "quit!") || ci_eq(cmd, "q!")) break;
        if (ci_eq(cmd, "quit") || ci_eq(cmd, "exit") || ci_eq(cmd, "q")) {
            if (dirty) print_line("hexedit: unsaved changes; use save or quit!");
            else break;
            continue;
        }
        if (ci_eq(cmd, "help") || ci_eq(cmd, "?")) {
            print_line("hexedit commands: view, set, ascii, fill, resize, save, saveas, info, quit");
            continue;
        }

        print_line("hexedit: unknown command (type help)");
    }
}
