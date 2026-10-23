/*
 * apps/text.c -- text processing utilities
 */
#include "apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core/shell_parse.h"
#include "core/simple_re.h"
#include "core/textutil.h"
#include "pipe.h"
#include "syscall.h"

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

    char path[APP_TOKEN_MAX];
    next_token(s, path, sizeof path);
    if (!*path && !pipe_input_active()) {
        print_line("usage: head [-n NUM] FILE");
        return;
    }

    char buf[2048 + 1];
    int n = read_text_file(path, buf, sizeof buf, "head");
    if (n < 0) return;

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
    (void)next_token(s, path, sizeof path);

    if (!*pat) {
        print_line("usage: grep [-n] [-e] PATTERN FILE");
        return;
    }
    if (!*path && !pipe_input_active()) {
        print_line("usage: grep [-n] [-e] PATTERN FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = read_text_file(path, file_buf, sizeof file_buf, "grep");
    if (n < 0) return;

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

    if (!sys_output_captured() && !pipe_input_active()) {
        char sum[48];
        snprintf(sum, sizeof sum, "matches: %d", matches);
        print_line(sum);
    }
}

void app_pager(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path && !pipe_input_active()) {
        print_line("usage: pager FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = read_text_file(path, file_buf, sizeof file_buf, "pager");
    if (n < 0) return;

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
    if (!*path && !pipe_input_active()) {
        print_line("usage: rev FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = read_text_file(path, file_buf, sizeof file_buf, "rev");
    if (n < 0) return;

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
    if (!*path && !pipe_input_active()) {
        print_line("usage: sort [-r] [-n] FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = read_text_file(path, file_buf, sizeof file_buf, "sort");
    if (n < 0) return;

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
    if (!*path && !pipe_input_active()) {
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
    if (!*path && !pipe_input_active()) {
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

    if (!*path && !pipe_input_active()) {
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

void app_uniq(const char *arg) {
    const char *s = skip_ws(arg);
    bool count_mode = false;
    bool dup_only   = false;
    bool uniq_only  = false;

    while (s && s[0] == '-') {
        if      (s[1] == 'c') { count_mode = true;  s = skip_ws(s + 2); }
        else if (s[1] == 'd') { dup_only   = true;  s = skip_ws(s + 2); }
        else if (s[1] == 'u') { uniq_only  = true;  s = skip_ws(s + 2); }
        else break;
    }

    char path[APP_TOKEN_MAX];
    next_token(s, path, sizeof path);
    if (!*path && !pipe_input_active()) { print_line("usage: uniq [-c] [-d] [-u] FILE"); return; }

    char buf[APP_READ_MAX + 1];
    int n = read_text_file(path, buf, sizeof buf, "uniq");
    if (n < 0) return;

    char prev[APP_TOKEN_MAX] = {0};
    unsigned run = 0;
    const char *p = buf;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len >= sizeof prev) len = sizeof prev - 1;

        char cur[APP_TOKEN_MAX];
        memcpy(cur, start, len);
        cur[len] = '\0';

        if (run == 0 || strcmp(cur, prev) != 0) {
            if (run > 0) {
                bool emit = (!dup_only && !uniq_only)
                         || (dup_only  && run > 1)
                         || (uniq_only && run == 1);
                if (emit) {
                    if (count_mode) {
                        char out[APP_TOKEN_MAX + 8];
                        snprintf(out, sizeof out, "%4u %s", run, prev);
                        print_line(out);
                    } else {
                        print_line(prev);
                    }
                }
            }
            memcpy(prev, cur, len + 1);
            run = 1;
        } else {
            run++;
        }
        if (*p == '\n') p++;
    }
    /* flush last group */
    if (run > 0) {
        bool emit = (!dup_only && !uniq_only)
                 || (dup_only  && run > 1)
                 || (uniq_only && run == 1);
        if (emit) {
            if (count_mode) {
                char out[APP_TOKEN_MAX + 8];
                snprintf(out, sizeof out, "%4u %s", run, prev);
                print_line(out);
            } else {
                print_line(prev);
            }
        }
    }
}

void app_tr(const char *arg) {
    char tmp[APP_TOKEN_MAX * 2];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");

    shell_argv_t av;
    int argc = shell_tokenize(tmp, &av);

    int i = 0;
    bool delete_mode = false;
    if (i < argc && strcmp(av.argv[i], "-d") == 0) {
        delete_mode = true;
        i++;
    }
    if (i >= argc) {
        print_line("usage: tr [-d] SET1 [SET2] [FILE]");
        return;
    }

    const char *set1_spec = av.argv[i++];
    const char *set2_spec = NULL;
    if (!delete_mode) {
        if (i >= argc) {
            print_line("usage: tr [-d] SET1 SET2 [FILE]");
            return;
        }
        set2_spec = av.argv[i++];
    }

    const char *path = (i < argc) ? av.argv[i] : "";
    if (!*path && !pipe_input_active()) {
        print_line("usage: tr [-d] SET1 [SET2] [FILE]");
        return;
    }

    char set1[128];
    char set2[128];
    size_t len1 = charset_expand(set1_spec, set1, sizeof set1);
    size_t len2 = delete_mode ? 0 : charset_expand(set2_spec, set2, sizeof set2);

    char *buf = (char *)g_app_io_buf;
    int n = read_text_file(path, buf, APP_READ_MAX, "tr");
    if (n < 0) return;

    for (int k = 0; k < n; k++) {
        char c = buf[k];
        if (delete_mode) {
            if (memchr(set1, c, len1)) continue;
            sys_putchar(c);
        } else {
            sys_putchar(charset_translate(set1, len1, set2, len2, c));
        }
    }
}

void app_sed(const char *arg) {
    char tmp[APP_TOKEN_MAX * 2];
    copy_cstr(tmp, sizeof tmp, arg ? arg : "");

    shell_argv_t av;
    int argc = shell_tokenize(tmp, &av);
    if (argc < 1) {
        print_line("usage: sed 's/OLD/NEW/[g]' [FILE]");
        return;
    }

    const char *script = av.argv[0];
    const char *path = (argc >= 2) ? av.argv[1] : "";
    if (!*path && !pipe_input_active()) {
        print_line("usage: sed 's/OLD/NEW/[g]' [FILE]");
        return;
    }

    if (script[0] != 's' || script[1] == '\0') {
        print_line("sed: only s/OLD/NEW/[g] is supported");
        return;
    }

    char delim = script[1];
    const char *p = script + 2;

    const char *old_start = p;
    while (*p && *p != delim) {
        if (*p == '\\' && p[1]) p++;
        p++;
    }
    if (*p != delim) {
        print_line("sed: missing pattern delimiter");
        return;
    }
    size_t old_raw_len = (size_t)(p - old_start);
    p++;

    const char *new_start = p;
    while (*p && *p != delim) {
        if (*p == '\\' && p[1]) p++;
        p++;
    }
    if (*p != delim) {
        print_line("sed: missing replacement delimiter");
        return;
    }
    size_t new_raw_len = (size_t)(p - new_start);
    p++;

    bool global = (*p == 'g');

    char old_raw[APP_TOKEN_MAX];
    char new_raw[APP_TOKEN_MAX];
    if (old_raw_len >= sizeof old_raw) old_raw_len = sizeof old_raw - 1;
    memcpy(old_raw, old_start, old_raw_len);
    old_raw[old_raw_len] = '\0';
    if (new_raw_len >= sizeof new_raw) new_raw_len = sizeof new_raw - 1;
    memcpy(new_raw, new_start, new_raw_len);
    new_raw[new_raw_len] = '\0';

    char old[APP_TOKEN_MAX];
    char new_text[APP_TOKEN_MAX];
    text_unescape(old_raw, old, sizeof old);
    text_unescape(new_raw, new_text, sizeof new_text);

    char *buf = (char *)g_app_io_buf;
    static char line_out[APP_TOKEN_MAX * 4];
    int n = read_text_file(path, buf, APP_READ_MAX, "sed");
    if (n < 0) return;

    const char *line = buf;
    const char *end = buf + n;
    while (line < end) {
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        size_t len = nl ? (size_t)(nl - line) : (size_t)(end - line);

        size_t used = text_replace(line, len, old, new_text, global, line_out, sizeof line_out);
        if (used == (size_t)-1) {
            print_line("sed: output too large");
            return;
        }

        for (size_t k = 0; k < used; k++) sys_putchar(line_out[k]);
        if (nl) sys_putchar('\n');

        line = nl ? nl + 1 : end;
    }
}

static void md_inline(const char *in, char *out, size_t cap) {
    size_t o = 0;
    const char *p = in;

    while (*p && o + 1 < cap) {
        if (*p == '[') {
            const char *close = strchr(p + 1, ']');
            if (close && close[1] == '(') {
                const char *end = strchr(close + 2, ')');
                if (end) {
                    for (const char *q = p + 1; q < close && o + 1 < cap; q++) out[o++] = *q;
                    if (o + 2 < cap) {
                        out[o++] = ' ';
                        out[o++] = '(';
                    }
                    for (const char *q = close + 2; q < end && o + 1 < cap; q++) out[o++] = *q;
                    if (o + 1 < cap) out[o++] = ')';
                    p = end + 1;
                    continue;
                }
            }
        }

        if (*p == '*' || *p == '_' || *p == '`') {
            p++;
            continue;
        }

        out[o++] = *p++;
    }
    out[o] = '\0';
}

void app_md(const char *arg) {
    const char *path = skip_ws(arg ? arg : "");
    if (!*path && !pipe_input_active()) {
        print_line("usage: md FILE");
        return;
    }

    char *buf = (char *)g_app_io_buf;
    int n = read_text_file(path, buf, APP_READ_MAX, "md");
    if (n < 0) return;

    bool in_code = false;
    const char *p = buf;
    const char *end = buf + n;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 0 && p[len - 1] == '\r') len--;

        char line[APP_TOKEN_MAX];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (strncmp(line, "```", 3) == 0) {
            in_code = !in_code;
            print_line(in_code ? "--- code ---" : "------------");
        } else if (in_code) {
            sys_print("  ");
            print_line(line);
        } else {
            const char *s = line;
            while (*s == ' ') s++;

            int heading = 0;
            while (s[heading] == '#') heading++;

            char rendered[APP_TOKEN_MAX];
            if (heading > 0 && s[heading] == ' ') {
                if (heading == 1) print_line("==========");
                md_inline(s + heading + 1, rendered, sizeof rendered);
                print_line(rendered);
                if (heading == 1) print_line("==========");
            } else if (strncmp(s, "- ", 2) == 0 || strncmp(s, "* ", 2) == 0) {
                md_inline(s + 2, rendered, sizeof rendered);
                sys_print("  * ");
                print_line(rendered);
            } else if (strncmp(s, "> ", 2) == 0) {
                md_inline(s + 2, rendered, sizeof rendered);
                sys_print("  | ");
                print_line(rendered);
            } else if (strcmp(s, "---") == 0 || strcmp(s, "***") == 0) {
                print_line("----------------------------------------");
            } else {
                md_inline(s, rendered, sizeof rendered);
                print_line(rendered);
            }
        }

        p = nl ? nl + 1 : end;
    }
}
