/*
 * apps/text.c -- text processing utilities
 */
#include "apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "syscall.h"

/* Simple regex: '.' matches any char, '*' matches zero-or-more of preceding */
static bool match_re_here(const char *re, const char *text);

static bool match_re_star(char c, const char *re, const char *text) {
    do {
        if (match_re_here(re, text)) return true;
    } while (*text != '\0' && (c == '.' || tolower((unsigned char)*text) == tolower((unsigned char)c)) && text++);
    return false;
}

static bool match_re_here(const char *re, const char *text) {
    if (re[0] == '\0') return true;
    if (re[1] == '*') return match_re_star(re[0], re + 2, text);
    if (re[0] == '.' && *text != '\0') return match_re_here(re + 1, text + 1);
    if (*text != '\0' && tolower((unsigned char)re[0]) == tolower((unsigned char)*text))
        return match_re_here(re + 1, text + 1);
    return false;
}

bool match_simple_re(const char *re, const char *text) {
    if (re[0] == '^') return match_re_here(re + 1, text);
    do {
        if (match_re_here(re, text)) return true;
    } while (*text++ != '\0');
    return false;
}

void app_head(const char *arg) {
    const char *s = skip_ws(arg);
    long num_lines = 10;

    if (strncmp(s, "-n", 2) == 0 && isspace((unsigned char)s[2])) {
        s = skip_ws(s + 2);
        char *endptr = NULL;
        long parsed = strtol(s, &endptr, 10);
        if (endptr == s || parsed < 1) {
            print_line("usage: head [-n NUM] FILE");
            return;
        }
        num_lines = parsed;
        s = skip_ws(endptr);
    }

    if (!*s) {
        print_line("usage: head [-n NUM] FILE");
        return;
    }

    char path[256];
    size_t i = 0;
    while (*s && !isspace((unsigned char)*s) && i < sizeof path - 1) {
        path[i++] = *s++;
    }
    path[i] = '\0';

    char buf[2048 + 1];
    int n = sys_fread(path, buf, 2048);
    if (n < 0) {
        print_line("head: cannot open file");
        return;
    }
    buf[n] = '\0';

    long seen = 0;
    for (int j = 0; j < n; j++) {
        sys_putchar(buf[j]);
        if (buf[j] == '\n' && ++seen >= num_lines) break;
    }
    if (n > 0 && buf[n - 1] != '\n' && seen < num_lines) sys_putchar('\n');
}

void app_grep(const char *arg) {
    const char *s = skip_ws(arg);
    bool show_nums = false;
    bool use_regex = false;

    /* Parse flags */
    while (s && *s == '-') {
        if (s[1] == 'n') { show_nums = true; s = skip_ws(s + 2); }
        else if (s[1] == 'e') { use_regex = true; s = skip_ws(s + 2); }
        else break;
    }

    char pat[APP_TOKEN_MAX];
    char path[APP_TOKEN_MAX];
    s = next_token(s, pat, sizeof pat);
    s = next_token(s, path, sizeof path);

    if (!*pat) {
        print_line("usage: grep [-n] [-e] PATTERN FILE");
        return;
    }
    if (!*path) {
        print_line("grep: stdin mode not implemented on PicoCalc yet");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = sys_fread(path, file_buf, APP_READ_MAX);
    if (n < 0) {
        print_line("grep: cannot open file");
        return;
    }
    file_buf[n] = '\0';

    /* Auto-detect regex if pattern contains . or * */
    if (!use_regex) {
        for (const char *c = pat; *c; c++) {
            if (*c == '.' || *c == '*' || *c == '^' || *c == '$') {
                use_regex = true;
                break;
            }
        }
    }

    int line = 1;
    int matches = 0;
    const char *p = file_buf;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n') p++;

        char line_buf[APP_TOKEN_MAX];
        size_t len = (size_t)(p - start);
        if (len >= sizeof line_buf) len = sizeof line_buf - 1;
        memcpy(line_buf, start, len);
        line_buf[len] = '\0';

        bool hit = use_regex ? match_simple_re(pat, line_buf)
                             : str_contains_ci(line_buf, pat);
        if (hit) {
            if (show_nums) {
                char num[32];
                snprintf(num, sizeof num, "%d:", line);
                sys_print(num);
            }
            sys_print(line_buf);
            sys_putchar('\n');
            matches++;
        }

        if (*p == '\n') p++;
        line++;
    }

    {
        char sum[48];
        snprintf(sum, sizeof sum, "matches: %d", matches);
        print_line(sum);
    }
}

void app_pager(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: pager FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = sys_fread(path, file_buf, APP_READ_MAX);
    if (n < 0) {
        print_line("pager: cannot open file");
        return;
    }
    file_buf[n] = '\0';

    int line_count = 0;
    for (int i = 0; i < n; i++) {
        char c = file_buf[i];
        sys_putchar(c);
        if (c != '\n') continue;

        line_count++;
        if (line_count < APP_PAGE_LINES) continue;

        sys_print(" -- MORE -- (Space=page, Enter=line, q=quit) ");
        int key = sys_getchar();
        sys_print("\r                                                \r");

        if (key == 'q' || key == 'Q' || key == 27) return;
        if (key == ' ') line_count = 0;
        else line_count = APP_PAGE_LINES - 1;
    }

    if (n > 0 && file_buf[n - 1] != '\n') sys_putchar('\n');
}

void app_rev(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: rev FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = sys_fread(path, file_buf, APP_READ_MAX);
    if (n < 0) {
        print_line("rev: cannot open file");
        return;
    }
    file_buf[n] = '\0';

    const char *p = file_buf;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        for (const char *q = p; q > start; ) {
            q--;
            sys_putchar(*q);
        }
        sys_putchar('\n');
        if (*p == '\n') p++;
    }
}

void app_sort(const char *arg) {
    const char *s = skip_ws(arg);
    bool reverse = false;
    bool numeric = false;

    while (s && *s == '-') {
        if (s[1] == 'r') { reverse = true; s = skip_ws(s + 2); }
        else if (s[1] == 'n') { numeric = true; s = skip_ws(s + 2); }
        else break;
    }

    char path[APP_TOKEN_MAX];
    next_token(s, path, sizeof path);
    if (!*path) {
        print_line("usage: sort [-r] [-n] FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = sys_fread(path, file_buf, APP_READ_MAX);
    if (n < 0) {
        print_line("sort: cannot open file");
        return;
    }
    file_buf[n] = '\0';

    char *lines[128];
    int line_count = 0;
    char *p = file_buf;
    while (*p && line_count < 128) {
        lines[line_count++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') {
            *p = '\0';
            p++;
        }
    }

    for (int i = 0; i < line_count; i++) {
        for (int j = i + 1; j < line_count; j++) {
            int cmp;
            if (numeric) {
                long a = strtol(lines[i], NULL, 10);
                long b = strtol(lines[j], NULL, 10);
                cmp = (a > b) ? 1 : (a < b) ? -1 : 0;
            } else {
                cmp = str_casecmp_local(lines[i], lines[j]);
            }
            if (reverse) cmp = -cmp;
            if (cmp > 0) {
                char *tmp = lines[i];
                lines[i] = lines[j];
                lines[j] = tmp;
            }
        }
    }

    for (int i = 0; i < line_count; i++) {
        sys_print(lines[i]);
        sys_putchar('\n');
    }
}

void app_tail(const char *arg) {
    const char *s = skip_ws(arg);
    long num_lines = 10;

    if (strncmp(s, "-n", 2) == 0 && isspace((unsigned char)s[2])) {
        s = skip_ws(s + 2);
        char *endptr = NULL;
        long parsed = strtol(s, &endptr, 10);
        if (endptr == s || parsed < 1) {
            print_line("usage: tail [-n NUM] FILE");
            return;
        }
        num_lines = parsed;
        s = skip_ws(endptr);
    }

    char path[APP_TOKEN_MAX];
    next_token(s, path, sizeof path);
    if (!*path) {
        print_line("usage: tail [-n NUM] FILE");
        return;
    }

    char buf[APP_READ_MAX + 1];
    int n = read_text_file(path, buf, sizeof buf, "tail");
    if (n < 0) return;

    int start = n;
    long seen = 0;
    while (start > 0) {
        start--;
        if (buf[start] == '\n') {
            if (seen++ >= num_lines) {
                start++;
                break;
            }
        }
    }
    if (start < 0) start = 0;
    sys_print(buf + start);
    if (n > 0 && buf[n - 1] != '\n') sys_putchar('\n');
}

void app_wc(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: wc FILE");
        return;
    }

    char buf[APP_READ_MAX + 1];
    int n = read_text_file(path, buf, sizeof buf, "wc");
    if (n < 0) return;

    int lines = 0;
    int words = 0;
    bool in_word = false;
    for (int i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)buf[i];
        if (ch == '\n') lines++;
        if (isspace(ch)) {
            in_word = false;
        } else if (!in_word) {
            words++;
            in_word = true;
        }
    }

    char out[128];
    snprintf(out, sizeof out, "%d %d %d %.80s", lines, words, n, path);
    print_line(out);
}

void app_cut(const char *arg) {
    const char *s = skip_ws(arg);
    char path[APP_TOKEN_MAX] = {0};
    char tok[APP_TOKEN_MAX];
    char val[APP_TOKEN_MAX];
    char delim = ',';
    long field = 1;

    while (s && *s) {
        s = next_token(s, tok, sizeof tok);
        if (!*tok) break;

        if (strcmp(tok, "-d") == 0) {
            s = next_token(s, val, sizeof val);
            if (!*val) {
                print_line("usage: cut [-d CHAR] [-f NUM] FILE");
                return;
            }
            delim = val[0];
        } else if (strcmp(tok, "-f") == 0) {
            s = next_token(s, val, sizeof val);
            if (!*val) {
                print_line("usage: cut [-d CHAR] [-f NUM] FILE");
                return;
            }
            field = strtol(val, NULL, 10);
            if (field < 1) {
                print_line("cut: field number must be >= 1");
                return;
            }
        } else {
            copy_cstr(path, sizeof path, tok);
            break;
        }
    }

    if (!*path) {
        print_line("usage: cut [-d CHAR] [-f NUM] FILE");
        return;
    }

    char buf[APP_READ_MAX + 1];
    int n = read_text_file(path, buf, sizeof buf, "cut");
    if (n < 0) return;

    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        long cur = 1;
        char *start = line;
        char *p = line;
        while (1) {
            if (*p == delim || *p == '\0') {
                if (cur == field) {
                    char saved = *p;
                    *p = '\0';
                    print_line(start);
                    *p = saved;
                    break;
                }
                if (*p == '\0') {
                    sys_putchar('\n');
                    break;
                }
                cur++;
                start = p + 1;
            }
            if (*p == '\0') break;
            p++;
        }

        if (!next) break;
        line = next + 1;
    }
}
