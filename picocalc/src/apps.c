#include "apps.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syscall.h"

#define APP_TOKEN_MAX    256
#define APP_READ_MAX     4096
#define APP_PAGE_LINES   23
#define BASIC_LINE_MAX   128
#define BASIC_STACK_MAX  16
#define TINYC_VAR_MAX    64
#define EDIT_LINES_MAX   96
#define EDIT_LINE_MAX    96
#define BROWSE_ITEMS_MAX 64
#define BROWSE_NAME_MAX  32

typedef struct {
    const char *pattern;
    char root[APP_TOKEN_MAX];
    int count;
} find_ctx_t;

typedef struct {
    int count;
    uint32_t total_size;
} dir_stat_t;

typedef struct {
    int number;
    char text[APP_TOKEN_MAX];
} basic_line_t;

typedef struct {
    int var_index;
    int limit;
    int step;
    int for_pc;
} basic_for_t;

typedef struct {
    int vars[26];
    bool stop;
    basic_for_t loops[BASIC_STACK_MAX];
    int loop_top;
    int gosub_stack[BASIC_STACK_MAX];
    int gosub_top;
} basic_env_t;

typedef struct {
    bool used;
    char name[16];
    int value;
    bool is_array;
    int array[16];
    int array_len;
} tinyc_var_t;

typedef struct {
    char path[APP_TOKEN_MAX];
    char lines[EDIT_LINES_MAX][EDIT_LINE_MAX];
    int line_count;
    bool dirty;
} editor_state_t;

typedef struct {
    char name[BROWSE_NAME_MAX];
    bool is_dir;
    uint32_t size;
} browser_entry_t;

typedef struct {
    char cwd[APP_TOKEN_MAX];
    browser_entry_t items[BROWSE_ITEMS_MAX];
    int count;
} browser_state_t;

typedef struct {
    tinyc_var_t vars[TINYC_VAR_MAX];
    bool stop;
} tinyc_env_t;

typedef int (*expr_lookup_fn)(void *ctx, const char *name, size_t len);

typedef struct {
    const char *s;
    expr_lookup_fn lookup;
    void *ctx;
    bool error;
} expr_state_t;

static basic_line_t g_basic_program[BASIC_LINE_MAX];
static int g_basic_count = 0;
static basic_env_t g_basic_env;
static tinyc_env_t g_tinyc_env;
static editor_state_t g_editor;

static void app_make_abs(const char *path, char *out, size_t out_sz);
static int expr_lookup_tinyc(void *ctx, const char *name, size_t len);
static void tinyc_set_var(tinyc_env_t *env, const char *name, int value);
static int expr_eval(const char *expr, expr_lookup_fn lookup, void *ctx, bool *ok);
static void rtrim_in_place(char *s);
static int app_read_line(const char *prompt, char *buf, size_t size);
static bool ci_eq(const char *a, const char *b);
static void tinyc_show_vars(void);
static void app_basic(const char *arg);
static void app_tinyc(const char *arg);
static void app_home(const char *arg);

static const char *skip_ws(const char *s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void print_line(const char *s) {
    sys_print(s);
    sys_putchar('\n');
}

static int read_text_file(const char *path, char *buf, size_t cap, const char *label) {
    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }
    if (cap == 0) return -1;

    int n = sys_fread(path, buf, cap - 1);
    if (n < 0) {
        sys_print(label);
        sys_print(": cannot open file\n");
        return -1;
    }
    buf[n] = '\0';
    return n;
}

static int write_text_file(const char *path, const char *buf, const char *label) {
    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }
    if (!buf) return -1;

    int n = sys_fwrite(path, buf, strlen(buf));
    if (n < 0) {
        sys_print(label);
        sys_print(": cannot write file\n");
        return -1;
    }
    return n;
}

static int load_file_bytes(const char *path, uint8_t *buf, size_t cap,
                           uint32_t *out_len, const char *label) {
    char abs[APP_TOKEN_MAX];
    fat_file_t f;

    if (!path || !*path) {
        sys_print(label);
        sys_print(": missing file operand\n");
        return -1;
    }

    app_make_abs(path, abs, sizeof abs);
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) {
        sys_print(label);
        sys_print(": cannot open file\n");
        return -1;
    }
    if (f.size > cap) {
        sys_print(label);
        sys_print(": file too large for current buffer\n");
        return -1;
    }

    uint32_t total = 0;
    while (total < f.size) {
        int32_t n = fat_read(&f, buf + total, f.size - total);
        if (n == FAT_ERR_EOF) break;
        if (n < 0) {
            sys_print(label);
            sys_print(": read error\n");
            return -1;
        }
        total += (uint32_t)n;
    }

    if (out_len) *out_len = total;
    return (int)total;
}

static void app_make_abs(const char *path, char *out, size_t out_sz) {
    if (!path || !*path) {
        strncpy(out, _sys_cwd, out_sz - 1);
    } else if (path[0] == '/') {
        strncpy(out, path, out_sz - 1);
    } else if (strcmp(_sys_cwd, "/") == 0) {
        snprintf(out, out_sz, "/%s", path);
    } else {
        snprintf(out, out_sz, "%s/%s", _sys_cwd, path);
    }
    out[out_sz - 1] = '\0';
}

static const char *next_token(const char *s, char *tok, size_t tok_sz) {
    size_t i = 0;
    s = skip_ws(s);
    if (!s || !*s) {
        tok[0] = '\0';
        return s;
    }
    while (*s && !isspace((unsigned char)*s) && i < tok_sz - 1) {
        tok[i++] = *s++;
    }
    tok[i] = '\0';
    while (*s && !isspace((unsigned char)*s)) s++;
    return s;
}

static bool str_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n) return true;
    }
    return false;
}

static int str_casecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool glob_match_star(const char *pattern, const char *str) {
    const char *star_pat = NULL;
    const char *star_str = NULL;

    while (1) {
        if (*pattern == '*') {
            pattern++;
            star_pat = pattern;
            star_str = str;
            continue;
        }
        if (*pattern == '\0') {
            if (*str == '\0') return true;
            if (!star_pat) return false;
            if (*star_str == '\0') return false;
            star_str++;
            str = star_str;
            pattern = star_pat;
            continue;
        }
        if (*str != '\0' && *pattern == *str) {
            pattern++;
            str++;
            continue;
        }
        if (!star_pat) return false;
        if (*star_str == '\0') return false;
        star_str++;
        str = star_str;
        pattern = star_pat;
    }
}

static void app_find_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    (void)size;
    (void)is_dir;
    find_ctx_t *ctx = (find_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    if (ctx->pattern && !glob_match_star(ctx->pattern, name)) return;

    if (strcmp(ctx->root, "/") == 0) {
        char path[APP_TOKEN_MAX];
        snprintf(path, sizeof path, "/%s", name);
        print_line(path);
    } else {
        char path[APP_TOKEN_MAX];
        snprintf(path, sizeof path, "%s/%s", ctx->root, name);
        print_line(path);
    }
    ctx->count++;
}

static void app_hello(const char *arg) {
    (void)arg;
    sys_setcolor(0x0F);
    sys_print("Hello, World! Welcome to Mellivora OS!\n");
    sys_setcolor(0x0A);
    sys_print("This sample app is now running on the PicoCalc target.\n");
    sys_setcolor(0x07);
}

static void app_basename(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) {
        print_line("usage: basename PATH");
        return;
    }

    const char *end = path;
    while (*end && !isspace((unsigned char)*end)) end++;

    const char *base = path;
    for (const char *p = path; p < end; p++) {
        if (*p == '/') base = p + 1;
    }

    while (base < end) sys_putchar(*base++);
    sys_putchar('\n');
}

static void app_dirname(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) {
        print_line("usage: dirname PATH");
        return;
    }

    const char *end = path;
    while (*end && !isspace((unsigned char)*end)) end++;

    const char *last = NULL;
    for (const char *p = path; p < end; p++) {
        if (*p == '/') last = p;
    }

    if (!last) {
        print_line(".");
        return;
    }

    if (last == path) {
        print_line("/");
        return;
    }

    for (const char *p = path; p < last; p++) sys_putchar(*p);
    sys_putchar('\n');
}

static void app_seq(const char *arg) {
    const char *s = skip_ws(arg);
    char *endptr = NULL;
    long limit = strtol(s, &endptr, 10);
    if (!*s || endptr == s || limit < 1) {
        print_line("usage: seq N");
        return;
    }

    char buf[32];
    for (long i = 1; i <= limit; i++) {
        snprintf(buf, sizeof buf, "%ld\n", i);
        sys_print(buf);
    }
}

static void app_head(const char *arg) {
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

static void app_grep(const char *arg) {
    char pat[APP_TOKEN_MAX];
    char path[APP_TOKEN_MAX];
    const char *s = next_token(arg, pat, sizeof pat);
    s = next_token(s, path, sizeof path);

    if (!*pat) {
        print_line("usage: grep PATTERN FILE");
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

        if (str_contains_ci(line_buf, pat)) {
            char num[32];
            snprintf(num, sizeof num, "%d:", line);
            sys_print(num);
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

static void app_pager(const char *arg) {
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

static void app_rev(const char *arg) {
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

static void app_sort(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: sort FILE");
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
            if (str_casecmp_local(lines[i], lines[j]) > 0) {
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

static void app_tail(const char *arg) {
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

static void app_wc(const char *arg) {
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

    char out[96];
    snprintf(out, sizeof out, "%d %d %d %s", lines, words, n, path);
    print_line(out);
}

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
            if (offset + i < n) {
                pos += snprintf(line + pos, sizeof line - pos, "%02X ",
                                (unsigned char)buf[offset + i]);
            } else {
                pos += snprintf(line + pos, sizeof line - pos, "   ");
            }
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

static void app_hexdump(const char *arg) {
    app_hexdump_core(arg, "hexdump");
}

static void app_od(const char *arg) {
    app_hexdump_core(arg, "od");
}

static void app_cut(const char *arg) {
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
            strncpy(path, tok, sizeof path - 1);
            path[sizeof path - 1] = '\0';
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

static void app_find(const char *arg) {
    char tok1[APP_TOKEN_MAX];
    char tok2[APP_TOKEN_MAX];
    char tok3[APP_TOKEN_MAX];
    char root[APP_TOKEN_MAX];
    const char *pattern = NULL;
    const char *s = next_token(arg, tok1, sizeof tok1);

    root[0] = '\0';
    tok2[0] = '\0';
    tok3[0] = '\0';

    if (!*tok1) {
        strncpy(root, _sys_cwd, sizeof root - 1);
        root[sizeof root - 1] = '\0';
    } else if (strcmp(tok1, "-name") == 0) {
        s = next_token(s, tok2, sizeof tok2);
        if (!*tok2) {
            print_line("usage: find [PATH] [-name PATTERN]");
            return;
        }
        pattern = tok2;
        strncpy(root, _sys_cwd, sizeof root - 1);
        root[sizeof root - 1] = '\0';
    } else {
        app_make_abs(tok1, root, sizeof root);
        s = next_token(s, tok2, sizeof tok2);
        if (*tok2) {
            if (strcmp(tok2, "-name") != 0) {
                print_line("usage: find [PATH] [-name PATTERN]");
                return;
            }
            s = next_token(s, tok3, sizeof tok3);
            if (!*tok3) {
                print_line("usage: find [PATH] [-name PATTERN]");
                return;
            }
            pattern = tok3;
        }
    }

    if (fat_is_dir(root) != FAT_OK) {
        print_line("find: path is not a directory");
        return;
    }

    find_ctx_t ctx = {
        .pattern = pattern,
        .count = 0,
    };
    strncpy(ctx.root, root, sizeof ctx.root - 1);
    ctx.root[sizeof ctx.root - 1] = '\0';

    if (fat_ls(root, app_find_cb, &ctx) != FAT_OK) {
        print_line("find: unable to list directory");
    }
}

static void app_calc(const char *arg) {
    const char *s = skip_ws(arg);
    if (*s) {
        char name[16] = {0};
        const char *p = s;
        if (isalpha((unsigned char)*p) || *p == '_') {
            size_t i = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && i + 1 < sizeof name) {
                name[i++] = *p++;
            }
            name[i] = '\0';
            p = skip_ws(p);
            if (*p == '=' && p[1] != '=') {
                bool ok = false;
                int value = expr_eval(p + 1, expr_lookup_tinyc, &g_tinyc_env, &ok);
                if (!ok) {
                    print_line("calc: bad expression");
                    return;
                }
                tinyc_set_var(&g_tinyc_env, name, value);
                tinyc_set_var(&g_tinyc_env, "ans", value);
                char out[64];
                snprintf(out, sizeof out, "%s = %d", name, value);
                print_line(out);
                return;
            }
        }

        bool ok = false;
        int value = expr_eval(s, expr_lookup_tinyc, &g_tinyc_env, &ok);
        if (!ok) {
            print_line("calc: bad expression");
            return;
        }
        tinyc_set_var(&g_tinyc_env, "ans", value);
        char out[48];
        snprintf(out, sizeof out, "%d", value);
        print_line(out);
        return;
    }

    print_line("Mellivora calc");
    print_line("Enter EXPR or NAME = EXPR. Commands: vars, clear, exit");
    while (1) {
        char line[APP_TOKEN_MAX];
        if (app_read_line("calc> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        const char *expr = skip_ws(line);
        if (!*expr) continue;
        if (ci_eq(expr, "exit") || ci_eq(expr, "quit") || ci_eq(expr, "bye")) break;
        if (ci_eq(expr, "vars")) {
            tinyc_show_vars();
            continue;
        }
        if (ci_eq(expr, "clear")) {
            memset(&g_tinyc_env, 0, sizeof g_tinyc_env);
            print_line("calc: variables cleared");
            continue;
        }
        app_calc(expr);
    }
}

static void app_cp(const char *arg) {
    char src[APP_TOKEN_MAX];
    char dst[APP_TOKEN_MAX];
    const char *s = next_token(arg, src, sizeof src);
    next_token(s, dst, sizeof dst);

    if (!*src || !*dst) {
        print_line("usage: cp SRC DST");
        return;
    }

    char abs_src[APP_TOKEN_MAX];
    char abs_dst[APP_TOKEN_MAX];
    app_make_abs(src, abs_src, sizeof abs_src);
    app_make_abs(dst, abs_dst, sizeof abs_dst);

    if (strcmp(abs_src, abs_dst) == 0) {
        print_line("cp: source and destination are the same");
        return;
    }
    if (fat_is_dir(abs_src) == FAT_OK) {
        print_line("cp: directory copy not supported yet");
        return;
    }

    uint8_t buf[APP_READ_MAX];
    uint32_t n = 0;
    if (load_file_bytes(src, buf, sizeof buf, &n, "cp") < 0) return;
    if (sys_fwrite(dst, buf, n) < 0) {
        print_line("cp: write failed");
        return;
    }

    char out[64];
    snprintf(out, sizeof out, "copied %lu bytes", (unsigned long)n);
    print_line(out);
}

static void app_mv(const char *arg) {
    char src[APP_TOKEN_MAX];
    char dst[APP_TOKEN_MAX];
    const char *s = next_token(arg, src, sizeof src);
    next_token(s, dst, sizeof dst);

    if (!*src || !*dst) {
        print_line("usage: mv SRC DST");
        return;
    }

    char abs_src[APP_TOKEN_MAX];
    char abs_dst[APP_TOKEN_MAX];
    app_make_abs(src, abs_src, sizeof abs_src);
    app_make_abs(dst, abs_dst, sizeof abs_dst);

    if (strcmp(abs_src, abs_dst) == 0) {
        print_line("mv: source and destination are the same");
        return;
    }
    if (fat_is_dir(abs_src) == FAT_OK) {
        print_line("mv: directory move not supported yet");
        return;
    }

    uint8_t buf[APP_READ_MAX];
    uint32_t n = 0;
    if (load_file_bytes(src, buf, sizeof buf, &n, "mv") < 0) return;
    if (sys_fwrite(dst, buf, n) < 0) {
        print_line("mv: write failed");
        return;
    }
    if (sys_unlink(src) < 0) {
        print_line("mv: destination written but source removal failed");
        return;
    }

    char out[64];
    snprintf(out, sizeof out, "moved %lu bytes", (unsigned long)n);
    print_line(out);
}

static void app_stat_dir_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    dir_stat_t *st = (dir_stat_t *)opaque;
    if (!st || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    st->count++;
    if (!is_dir) st->total_size += size;
}

static void app_stat(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: stat PATH");
        return;
    }

    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        dir_stat_t st = {0};
        fat_ls(abs, app_stat_dir_cb, &st);
        char out[160];
        snprintf(out, sizeof out,
                 "path: %s\ntype: directory\nentries: %d\nfile-bytes: %lu",
                 abs, st.count, (unsigned long)st.total_size);
        print_line(out);
        return;
    }

    fat_file_t f;
    fat_result_t r = fat_open(abs, &f);
    if (r != FAT_OK) {
        print_line("stat: not found");
        return;
    }

    char out[160];
    snprintf(out, sizeof out,
             "path: %s\ntype: file\nsize: %lu bytes\ncluster: %lu",
             abs, (unsigned long)f.size, (unsigned long)f.first_cluster);
    print_line(out);
}

static void editor_clear(editor_state_t *ed, const char *path) {
    memset(ed, 0, sizeof *ed);
    if (path && *path) app_make_abs(path, ed->path, sizeof ed->path);
}

static void editor_load(editor_state_t *ed, const char *path) {
    char buf[APP_READ_MAX + 1];
    editor_clear(ed, path);

    int n = sys_fread(path, buf, APP_READ_MAX);
    if (n < 0) return;
    buf[n] = '\0';

    const char *p = buf;
    while (*p && ed->line_count < EDIT_LINES_MAX) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len > 0 && start[len - 1] == '\r') len--;
        if (len >= EDIT_LINE_MAX) len = EDIT_LINE_MAX - 1;
        memcpy(ed->lines[ed->line_count], start, len);
        ed->lines[ed->line_count][len] = '\0';
        ed->line_count++;
        if (*p == '\n') p++;
        if (*p == '\0' && n > 0 && buf[n - 1] == '\n' && ed->line_count < EDIT_LINES_MAX) {
            ed->lines[ed->line_count][0] = '\0';
            ed->line_count++;
        }
    }
}

static void editor_list(editor_state_t *ed) {
    if (ed->line_count == 0) {
        print_line("(empty buffer)");
        return;
    }
    for (int i = 0; i < ed->line_count; i++) {
        char out[APP_TOKEN_MAX + 16];
        snprintf(out, sizeof out, "%3d  %s", i + 1, ed->lines[i]);
        print_line(out);
    }
}

static bool editor_insert_line(editor_state_t *ed, int pos, const char *text) {
    if (pos < 0) pos = 0;
    if (pos > ed->line_count) pos = ed->line_count;
    if (ed->line_count >= EDIT_LINES_MAX) {
        print_line("edit: buffer full");
        return false;
    }
    memmove(&ed->lines[pos + 1], &ed->lines[pos],
            (size_t)(ed->line_count - pos) * sizeof ed->lines[0]);
    strncpy(ed->lines[pos], text ? text : "", EDIT_LINE_MAX - 1);
    ed->lines[pos][EDIT_LINE_MAX - 1] = '\0';
    ed->line_count++;
    ed->dirty = true;
    return true;
}

static bool editor_save(editor_state_t *ed) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    for (int i = 0; i < ed->line_count; i++) {
        size_t len = strlen(ed->lines[i]);
        if (pos + len + 2 >= sizeof buf) {
            print_line("edit: file exceeds 4 KB save limit");
            return false;
        }
        memcpy(buf + pos, ed->lines[i], len);
        pos += len;
        if (i + 1 < ed->line_count) buf[pos++] = '\n';
    }
    if (sys_fwrite(ed->path, buf, (uint32_t)pos) < 0) {
        print_line("edit: save failed");
        return false;
    }
    ed->dirty = false;
    char out[96];
    snprintf(out, sizeof out, "saved %s (%lu bytes)", ed->path, (unsigned long)pos);
    print_line(out);
    return true;
}

static void editor_append_mode(editor_state_t *ed) {
    print_line("append mode: enter lines, single '.' to finish");
    while (ed->line_count < EDIT_LINES_MAX) {
        char line[EDIT_LINE_MAX];
        if (app_read_line("+ ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        if (strcmp(line, ".") == 0) break;
        editor_insert_line(ed, ed->line_count, line);
    }
}

static void app_edit(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        print_line("usage: edit FILE");
        return;
    }

    editor_load(&g_editor, path);
    if (g_editor.path[0] == '\0') app_make_abs(path, g_editor.path, sizeof g_editor.path);

    print_line("Mellivora line editor");
    print_line("Commands: list, append, ins N TEXT, set N TEXT, del N, save, quit, help");

    while (1) {
        char line[APP_TOKEN_MAX];
        char cmd[APP_TOKEN_MAX];
        if (app_read_line("edit> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        const char *s = next_token(line, cmd, sizeof cmd);
        s = skip_ws(s);

        if (!*cmd) continue;
        if (ci_eq(cmd, "help")) {
            print_line("list                show buffer");
            print_line("append              multi-line append mode");
            print_line("append TEXT         append one line");
            print_line("ins N TEXT          insert before line N");
            print_line("set N TEXT          replace line N");
            print_line("del N               delete line N");
            print_line("save                write file to disk");
            print_line("quit or quit!       leave editor");
        } else if (ci_eq(cmd, "list") || ci_eq(cmd, "view")) {
            editor_list(&g_editor);
        } else if (ci_eq(cmd, "append") || ci_eq(cmd, "a")) {
            if (*s) editor_insert_line(&g_editor, g_editor.line_count, s);
            else editor_append_mode(&g_editor);
        } else if (ci_eq(cmd, "ins") || ci_eq(cmd, "insert")) {
            char numtok[16];
            s = next_token(s, numtok, sizeof numtok);
            int line_no = atoi(numtok);
            if (line_no < 1 || !*s) {
                print_line("usage: ins N TEXT");
            } else {
                editor_insert_line(&g_editor, line_no - 1, skip_ws(s));
            }
        } else if (ci_eq(cmd, "set")) {
            char numtok[16];
            s = next_token(s, numtok, sizeof numtok);
            int line_no = atoi(numtok);
            if (line_no < 1 || line_no > g_editor.line_count || !*s) {
                print_line("usage: set N TEXT");
            } else {
                strncpy(g_editor.lines[line_no - 1], skip_ws(s), EDIT_LINE_MAX - 1);
                g_editor.lines[line_no - 1][EDIT_LINE_MAX - 1] = '\0';
                g_editor.dirty = true;
            }
        } else if (ci_eq(cmd, "del") || ci_eq(cmd, "delete")) {
            int line_no = atoi(s);
            if (line_no < 1 || line_no > g_editor.line_count) {
                print_line("usage: del N");
            } else {
                memmove(&g_editor.lines[line_no - 1], &g_editor.lines[line_no],
                        (size_t)(g_editor.line_count - line_no) * sizeof g_editor.lines[0]);
                g_editor.line_count--;
                g_editor.dirty = true;
            }
        } else if (ci_eq(cmd, "save") || ci_eq(cmd, "write") || ci_eq(cmd, "w")) {
            editor_save(&g_editor);
        } else if (ci_eq(cmd, "quit!") || ci_eq(cmd, "q!")) {
            break;
        } else if (ci_eq(cmd, "quit") || ci_eq(cmd, "exit") || ci_eq(cmd, "q")) {
            if (g_editor.dirty) print_line("edit: unsaved changes; use save or quit!");
            else break;
        } else {
            print_line("edit: unknown command (type help)");
        }
    }
}

static void browser_parent_dir(const char *cwd, char *out, size_t out_sz) {
    strncpy(out, cwd, out_sz - 1);
    out[out_sz - 1] = '\0';
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') out[--len] = '\0';
    while (len > 1 && out[len - 1] != '/') out[--len] = '\0';
    if (len == 0) strcpy(out, "/");
    else if (len > 1 && out[len - 1] == '/') out[len - 1] = '\0';
    if (out[0] == '\0') strcpy(out, "/");
}

static void browser_collect_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    browser_state_t *br = (browser_state_t *)opaque;
    if (!br || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    if (br->count >= BROWSE_ITEMS_MAX) return;

    strncpy(br->items[br->count].name, name, sizeof br->items[br->count].name - 1);
    br->items[br->count].name[sizeof br->items[br->count].name - 1] = '\0';
    br->items[br->count].is_dir = is_dir;
    br->items[br->count].size = size;
    br->count++;
}

static void browser_refresh(browser_state_t *br) {
    br->count = 0;
    fat_ls(br->cwd, browser_collect_cb, br);
}

static void browser_render(browser_state_t *br, int sel) {
    sys_clear();
    sys_print("Mellivora browser\n");
    sys_print(br->cwd);
    sys_print("\n\n");
    sys_print("j/k or s/w move, Enter open, e edit, u up, r refresh, q quit\n\n");

    if (strcmp(br->cwd, "/") != 0) {
        sys_print(sel == 0 ? "> [DIR] ..\n" : "  [DIR] ..\n");
    }

    int base = (strcmp(br->cwd, "/") != 0) ? 1 : 0;
    int page_start = 0;
    int visible = 12;
    if (sel >= visible) page_start = sel - visible + 1;

    for (int disp = 0; disp < visible; disp++) {
        int row = page_start + disp;
        if (row == 0 && base == 1) continue;
        int idx = row - base;
        if (idx < 0 || idx >= br->count) continue;

        char line[80];
        snprintf(line, sizeof line, "%c %s %-20s %6lu\n",
                 (row == sel) ? '>' : ' ',
                 br->items[idx].is_dir ? "[DIR]" : "[FIL]",
                 br->items[idx].name,
                 (unsigned long)br->items[idx].size);
        sys_print(line);
    }

    if (br->count == 0) sys_print("(empty directory)\n");
}

static void app_browse(const char *arg) {
    browser_state_t br;
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (*path) app_make_abs(path, br.cwd, sizeof br.cwd);
    else strncpy(br.cwd, _sys_cwd, sizeof br.cwd - 1);
    br.cwd[sizeof br.cwd - 1] = '\0';

    if (fat_is_dir(br.cwd) != FAT_OK) {
        print_line("browse: path is not a directory");
        return;
    }

    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
    browser_refresh(&br);

    int sel = 0;
    while (1) {
        int total = br.count + ((strcmp(br.cwd, "/") != 0) ? 1 : 0);
        if (sel < 0) sel = 0;
        if (sel >= total && total > 0) sel = total - 1;
        browser_render(&br, sel);

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'j' || ch == 's' || ch == 0x0E) {
            if (sel + 1 < total) sel++;
            continue;
        }
        if (ch == 'k' || ch == 'w' || ch == 0x10) {
            if (sel > 0) sel--;
            continue;
        }
        if (ch == 'r' || ch == 'R') {
            browser_refresh(&br);
            continue;
        }
        if (ch == 'u' || ch == 'h' || ch == 0x08 || ch == 0x7F) {
            if (strcmp(br.cwd, "/") != 0) {
                char parent[APP_TOKEN_MAX];
                browser_parent_dir(br.cwd, parent, sizeof parent);
                strncpy(br.cwd, parent, sizeof br.cwd - 1);
                br.cwd[sizeof br.cwd - 1] = '\0';
                strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                browser_refresh(&br);
                sel = 0;
            }
            continue;
        }

        if (total == 0) continue;

        if (ch == 'e' || ch == 'E') {
            int base = (strcmp(br.cwd, "/") != 0) ? 1 : 0;
            if (sel >= base) {
                browser_entry_t *ent = &br.items[sel - base];
                if (!ent->is_dir) {
                    char abs[APP_TOKEN_MAX];
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    app_make_abs(ent->name, abs, sizeof abs);
                    app_edit(abs);
                }
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (strcmp(br.cwd, "/") != 0 && sel == 0) {
                char parent[APP_TOKEN_MAX];
                browser_parent_dir(br.cwd, parent, sizeof parent);
                strncpy(br.cwd, parent, sizeof br.cwd - 1);
                br.cwd[sizeof br.cwd - 1] = '\0';
                strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                browser_refresh(&br);
                sel = 0;
                continue;
            }

            int base = (strcmp(br.cwd, "/") != 0) ? 1 : 0;
            if (sel < base) continue;
            browser_entry_t *ent = &br.items[sel - base];
            strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
            _sys_cwd[sizeof _sys_cwd - 1] = '\0';

            if (ent->is_dir) {
                if (sys_chdir(ent->name) == 0) {
                    strncpy(br.cwd, _sys_cwd, sizeof br.cwd - 1);
                    br.cwd[sizeof br.cwd - 1] = '\0';
                    browser_refresh(&br);
                    sel = 0;
                }
            } else {
                char abs[APP_TOKEN_MAX];
                app_make_abs(ent->name, abs, sizeof abs);
                app_pager(abs);
            }
        }
    }

    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
    sys_clear();
}

static void app_notes(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) strcpy(path, "NOTES.TXT");

    print_line("Opening notes file...");
    app_edit(path);
}

static const char *sample_body(const char *name, const char **default_path, const char **kind) {
    if (!name || !*name || ci_eq(name, "list")) return NULL;

    if (ci_eq(name, "hello.bas") || ci_eq(name, "hello") || ci_eq(name, "basic-hello")) {
        if (default_path) *default_path = "HELLO.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"HELLO FROM MELLIVORA\"\n20 LET A = 5\n30 PRINT A * A\n40 END\n";
    }
    if (ci_eq(name, "count.bas") || ci_eq(name, "count")) {
        if (default_path) *default_path = "COUNT.BAS";
        if (kind) *kind = "basic";
        return "10 FOR A = 1 TO 10\n20 PRINT A\n30 NEXT A\n40 END\n";
    }
    if (ci_eq(name, "tinyc.tc") || ci_eq(name, "tinyc") || ci_eq(name, "demo.tc")) {
        if (default_path) *default_path = "DEMO.TC";
        if (kind) *kind = "tinyc";
        return "int x = 1;\nprint(\"Tiny C demo\");\nprint(x);\nx = x + 4;\nif (x > 2) print(x);\nvars\n";
    }
    if (ci_eq(name, "calc.txt") || ci_eq(name, "calc") || ci_eq(name, "math")) {
        if (default_path) *default_path = "CALC.TXT";
        if (kind) *kind = "text";
        return "Try these in calc:\nans = 7 * 8\nans + 10\n(3 + 4) * 5\n";
    }
    return NULL;
}

static void app_samples(const char *arg) {
    char mode[APP_TOKEN_MAX];
    char name[APP_TOKEN_MAX];
    char path[APP_TOKEN_MAX];
    const char *s = next_token(arg, mode, sizeof mode);

    if (!*mode || ci_eq(mode, "list")) {
        print_line("Bundled samples:");
        print_line("  hello.bas   - basic hello and arithmetic demo");
        print_line("  count.bas   - basic counting loop example");
        print_line("  tinyc.tc    - tiny c variable and print demo");
        print_line("  calc.txt    - quick calculator hints");
        print_line("Use: samples show NAME  or  samples write NAME [PATH]");
        return;
    }

    if (ci_eq(mode, "show")) {
        next_token(s, name, sizeof name);
        const char *def = NULL;
        const char *kind = NULL;
        const char *body = sample_body(name, &def, &kind);
        if (!body) {
            print_line("samples: unknown sample");
            return;
        }
        print_line(body);
        return;
    }

    if (ci_eq(mode, "write") || ci_eq(mode, "save") || ci_eq(mode, "install")) {
        s = next_token(s, name, sizeof name);
        next_token(s, path, sizeof path);
        const char *def = NULL;
        const char *kind = NULL;
        const char *body = sample_body(name, &def, &kind);
        if (!body) {
            print_line("samples: unknown sample");
            return;
        }
        if (!*path && def) strncpy(path, def, sizeof path - 1);
        path[sizeof path - 1] = '\0';
        if (sys_fwrite(path, body, (uint32_t)strlen(body)) < 0) {
            print_line("samples: write failed");
            return;
        }
        char out[128];
        snprintf(out, sizeof out, "saved sample to %s (%s)", path, kind ? kind : "text");
        print_line(out);
        return;
    }

    if (ci_eq(mode, "run")) {
        next_token(s, name, sizeof name);
        const char *def = NULL;
        const char *kind = NULL;
        const char *body = sample_body(name, &def, &kind);
        if (!body) {
            print_line("samples: unknown sample");
            return;
        }
        if (!kind) return;
        if (def && sys_fwrite(def, body, (uint32_t)strlen(body)) >= 0) {
            if (ci_eq(kind, "basic")) app_basic(def);
            else if (ci_eq(kind, "tinyc")) app_tinyc(def);
            else print_line(body);
        } else {
            print_line("samples: could not stage sample file");
        }
        return;
    }

    print_line("usage: samples [list|show NAME|write NAME [PATH]|run NAME]");
}

static bool is_leap_year(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int days_in_month_local(int month, int year) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) return days[month - 1] + (is_leap_year(year) ? 1 : 0);
    if (month >= 1 && month <= 12) return days[month - 1];
    return 30;
}

static int day_of_week_local(int y, int m, int d) {
    if (m < 3) {
        m += 12;
        y--;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

static void app_cal(const char *arg) {
    char m_tok[16];
    char y_tok[16];
    const char *s = next_token(arg, m_tok, sizeof m_tok);
    next_token(s, y_tok, sizeof y_tok);

    int month = *m_tok ? atoi(m_tok) : 4;
    int year = *y_tok ? atoi(y_tok) : 2026;
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    if (month < 1 || month > 12 || year < 1) {
        print_line("usage: cal [month] [year]");
        return;
    }

    char line[80];
    snprintf(line, sizeof line, "%s %d", months[month - 1], year);
    print_line(line);
    print_line("Su Mo Tu We Th Fr Sa");

    int dow = day_of_week_local(year, month, 1);
    int dim = days_in_month_local(month, year);
    char row[64];
    int pos = 0;
    row[0] = '\0';

    for (int i = 0; i < dow; i++) pos += snprintf(row + pos, sizeof row - pos, "   ");
    for (int d = 1; d <= dim; d++) {
        pos += snprintf(row + pos, sizeof row - pos, "%2d ", d);
        if ((dow + d) % 7 == 0 || d == dim) {
            row[pos] = '\0';
            print_line(row);
            pos = 0;
            row[0] = '\0';
        }
    }
    print_line("No RTC yet; default month/year can be overridden.");
}

static void app_clock(const char *arg) {
    (void)arg;
    while (1) {
        uint32_t uptime = sys_time_ms() / 1000U;
        uint32_t hh = (uptime / 3600U) % 24U;
        uint32_t mm = (uptime / 60U) % 60U;
        uint32_t ss = uptime % 60U;

        sys_clear();
        sys_print("Mellivora clock\n\n");
        char line[96];
        snprintf(line, sizeof line, "Uptime clock: %02lu:%02lu:%02lu\n",
                 (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
        sys_print(line);
        sys_print("Date: April 2026 default calendar reference\n\n");
        sys_print("Press c for calendar, q to quit, any other key to refresh.\n");

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'c' || ch == 'C') app_cal(NULL);
    }
    sys_clear();
}

static void app_dashboard(const char *arg) {
    (void)arg;
    while (1) {
        int pct = kbd_battery_percent();
        uint32_t uptime = sys_time_ms();

        sys_clear();
        sys_print("Mellivora dashboard\n\n");

        char line[128];
        snprintf(line, sizeof line, "Battery: %s\n", (pct >= 0) ? "available" : "unavailable");
        sys_print(line);
        if (pct >= 0) {
            snprintf(line, sizeof line, "Charge:  %d%%\n", pct);
            sys_print(line);
        }
        snprintf(line, sizeof line, "Uptime:  %lu ms\n", (unsigned long)uptime);
        sys_print(line);
        snprintf(line, sizeof line, "Path:    %s\n", _sys_cwd);
        sys_print(line);
        snprintf(line, sizeof line, "Screen:  %dx%d chars\n", sys_getscreenw(), sys_getscreenh());
        sys_print(line);

        sys_print("\nShortcuts:\n");
        sys_print("  b browse files\n");
        sys_print("  n notes\n");
        sys_print("  c calculator\n");
        sys_print("  s samples\n");
        sys_print("  t clock and calendar\n");
        sys_print("  h home launcher\n");
        sys_print("  r refresh\n");
        sys_print("  q quit\n\n");
        sys_print("Choice: ");

        int ch = sys_getchar();
        sys_putchar('\n');
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'r' || ch == 'R') continue;
        if (ch == 'b' || ch == 'B') app_browse(NULL);
        else if (ch == 'n' || ch == 'N') app_notes(NULL);
        else if (ch == 'c' || ch == 'C') app_calc(NULL);
        else if (ch == 's' || ch == 'S') app_samples(NULL);
        else if (ch == 't' || ch == 'T') app_clock(NULL);
        else if (ch == 'h' || ch == 'H') app_home(NULL);
    }
    sys_clear();
}

static void app_home(const char *arg) {
    (void)arg;
    while (1) {
        sys_clear();
        sys_print("Mellivora home\n\n");
        sys_print("1  Dashboard\n");
        sys_print("2  File browser\n");
        sys_print("3  Notes\n");
        sys_print("4  Calculator\n");
        sys_print("5  Clock and calendar\n");
        sys_print("6  BASIC\n");
        sys_print("7  Tiny C\n");
        sys_print("8  Samples\n");
        sys_print("q  Return to shell\n\n");
        sys_print("Select: ");

        int ch = sys_getchar();
        sys_putchar('\n');

        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == '1') app_dashboard(NULL);
        else if (ch == '2') app_browse(NULL);
        else if (ch == '3') app_notes(NULL);
        else if (ch == '4') app_calc(NULL);
        else if (ch == '5') app_clock(NULL);
        else if (ch == '6') app_basic(NULL);
        else if (ch == '7') app_tinyc(NULL);
        else if (ch == '8') app_samples(NULL);
    }
    sys_clear();
}

static void app_sysmon(const char *arg) {
    (void)arg;
    while (1) {
        int pct = kbd_battery_percent();
        uint32_t uptime_ms = sys_time_ms();
        uint32_t uptime_s = uptime_ms / 1000U;

        sys_clear();
        sys_print("Mellivora system monitor\n\n");

        char line[128];
        snprintf(line, sizeof line, "Battery: %s\n", (pct >= 0) ? "present" : "unknown");
        sys_print(line);
        if (pct >= 0) {
            snprintf(line, sizeof line, "Charge:  %d%%\n", pct);
            sys_print(line);
        }
        snprintf(line, sizeof line, "Uptime:  %lu ms (%lu s)\n",
                 (unsigned long)uptime_ms, (unsigned long)uptime_s);
        sys_print(line);
        snprintf(line, sizeof line, "CWD:     %s\n", _sys_cwd);
        sys_print(line);
        snprintf(line, sizeof line, "Cols:    %d\nRows: %d\n", sys_getscreenw(), sys_getscreenh());
        sys_print(line);
        sys_print("\nKeys: r refresh, d dashboard, q quit\n");

        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'd' || ch == 'D') app_dashboard(NULL);
    }
    sys_clear();
}

static void app_sleep_ms(const char *arg) {
    const char *s = skip_ws(arg);
    char *endptr = NULL;
    long ms = strtol(s, &endptr, 10);
    if (!*s || endptr == s || ms < 0) {
        print_line("usage: sleep MS");
        return;
    }
    sys_sleep((uint32_t)ms);
}

static void app_id(const char *arg) {
    (void)arg;
    print_line("uid=0(picocalc) gid=0(picocalc) groups=0(picocalc)");
}

static int expr_lookup_basic(void *ctx, const char *name, size_t len) {
    (void)len;
    basic_env_t *env = (basic_env_t *)ctx;
    if (!env || !name || !isalpha((unsigned char)name[0])) return 0;
    return env->vars[toupper((unsigned char)name[0]) - 'A'];
}

static int tinyc_find_var(tinyc_env_t *env, const char *name, size_t len) {
    if (!env || !name || len == 0) return -1;
    for (int i = 0; i < TINYC_VAR_MAX; i++) {
        if (!env->vars[i].used) continue;
        if (strncmp(env->vars[i].name, name, len) == 0 && env->vars[i].name[len] == '\0')
            return i;
    }
    return -1;
}

static int tinyc_get_value(tinyc_env_t *env, const char *token) {
    char name[16];
    const char *br = strchr(token, '[');
    if (br) {
        size_t len = (size_t)(br - token);
        if (len >= sizeof name) len = sizeof name - 1;
        memcpy(name, token, len);
        name[len] = '\0';
        int idx = tinyc_find_var(env, name, strlen(name));
        if (idx < 0 || !env->vars[idx].is_array) return 0;
        int slot = atoi(br + 1);
        if (slot < 0 || slot >= env->vars[idx].array_len) return 0;
        return env->vars[idx].array[slot];
    }

    int idx = tinyc_find_var(env, token, strlen(token));
    return (idx >= 0) ? env->vars[idx].value : 0;
}

static int expr_lookup_tinyc(void *ctx, const char *name, size_t len) {
    tinyc_env_t *env = (tinyc_env_t *)ctx;
    char token[32];
    if (len >= sizeof token) len = sizeof token - 1;
    memcpy(token, name, len);
    token[len] = '\0';
    return tinyc_get_value(env, token);
}

static void tinyc_set_var(tinyc_env_t *env, const char *name, int value) {
    char base[16];
    const char *br = strchr(name, '[');
    size_t len = br ? (size_t)(br - name) : strlen(name);
    if (len >= sizeof base) len = sizeof base - 1;
    memcpy(base, name, len);
    base[len] = '\0';

    int idx = tinyc_find_var(env, base, strlen(base));
    if (idx < 0) {
        for (int i = 0; i < TINYC_VAR_MAX; i++) {
            if (!env->vars[i].used) {
                env->vars[i].used = true;
                strncpy(env->vars[i].name, base, sizeof env->vars[i].name - 1);
                env->vars[i].name[sizeof env->vars[i].name - 1] = '\0';
                env->vars[i].is_array = false;
                env->vars[i].array_len = 0;
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) return;

    if (br) {
        int slot = atoi(br + 1);
        if (slot >= 0 && slot < 16) {
            env->vars[idx].is_array = true;
            if (slot + 1 > env->vars[idx].array_len) env->vars[idx].array_len = slot + 1;
            env->vars[idx].array[slot] = value;
        }
    } else {
        env->vars[idx].value = value;
    }
}

static void expr_skip_ws(expr_state_t *st) {
    while (st && st->s && isspace((unsigned char)*st->s)) st->s++;
}

static int expr_parse_cmp(expr_state_t *st);

static int expr_parse_primary(expr_state_t *st) {
    expr_skip_ws(st);
    if (!st || !st->s || *st->s == '\0') {
        if (st) st->error = true;
        return 0;
    }

    if (*st->s == '(') {
        st->s++;
        int v = expr_parse_cmp(st);
        expr_skip_ws(st);
        if (*st->s == ')') st->s++;
        else st->error = true;
        return v;
    }

    if (isdigit((unsigned char)*st->s)) {
        char *endptr = NULL;
        long v = strtol(st->s, &endptr, 10);
        st->s = endptr;
        return (int)v;
    }

    if (isalpha((unsigned char)*st->s) || *st->s == '_') {
        const char *start = st->s;
        while (isalnum((unsigned char)*st->s) || *st->s == '_') st->s++;
        return st->lookup ? st->lookup(st->ctx, start, (size_t)(st->s - start)) : 0;
    }

    st->error = true;
    return 0;
}

static int expr_parse_unary(expr_state_t *st) {
    expr_skip_ws(st);
    if (*st->s == '+') {
        st->s++;
        return expr_parse_unary(st);
    }
    if (*st->s == '-') {
        st->s++;
        return -expr_parse_unary(st);
    }
    return expr_parse_primary(st);
}

static int expr_parse_mul(expr_state_t *st) {
    int v = expr_parse_unary(st);
    while (1) {
        expr_skip_ws(st);
        if (*st->s == '*') {
            st->s++;
            v *= expr_parse_unary(st);
        } else if (*st->s == '/') {
            st->s++;
            int rhs = expr_parse_unary(st);
            if (rhs == 0) {
                st->error = true;
                return 0;
            }
            v /= rhs;
        } else if (*st->s == '%') {
            st->s++;
            int rhs = expr_parse_unary(st);
            if (rhs == 0) {
                st->error = true;
                return 0;
            }
            v %= rhs;
        } else if (*st->s == '&' && st->s[1] != '&') {
            st->s++;
            v &= expr_parse_unary(st);
        } else if (*st->s == '|' && st->s[1] != '|') {
            st->s++;
            v |= expr_parse_unary(st);
        } else if (*st->s == '^') {
            st->s++;
            v ^= expr_parse_unary(st);
        } else {
            break;
        }
    }
    return v;
}

static int expr_parse_add(expr_state_t *st) {
    int v = expr_parse_mul(st);
    while (1) {
        expr_skip_ws(st);
        if (*st->s == '+') {
            st->s++;
            v += expr_parse_mul(st);
        } else if (*st->s == '-') {
            st->s++;
            v -= expr_parse_mul(st);
        } else {
            break;
        }
    }
    return v;
}

static int expr_parse_cmp(expr_state_t *st) {
    int v = expr_parse_add(st);
    while (1) {
        expr_skip_ws(st);
        if (strncmp(st->s, "==", 2) == 0) {
            st->s += 2;
            v = (v == expr_parse_add(st));
        } else if (strncmp(st->s, "!=", 2) == 0) {
            st->s += 2;
            v = (v != expr_parse_add(st));
        } else if (strncmp(st->s, "<=", 2) == 0) {
            st->s += 2;
            v = (v <= expr_parse_add(st));
        } else if (strncmp(st->s, ">=", 2) == 0) {
            st->s += 2;
            v = (v >= expr_parse_add(st));
        } else if (*st->s == '<') {
            st->s++;
            v = (v < expr_parse_add(st));
        } else if (*st->s == '>') {
            st->s++;
            v = (v > expr_parse_add(st));
        } else if (*st->s == '=') {
            st->s++;
            v = (v == expr_parse_add(st));
        } else {
            break;
        }
    }
    return v;
}

static int expr_eval(const char *expr, expr_lookup_fn lookup, void *ctx, bool *ok) {
    expr_state_t st = {
        .s = expr,
        .lookup = lookup,
        .ctx = ctx,
        .error = false,
    };
    int v = expr_parse_cmp(&st);
    expr_skip_ws(&st);
    if (*st.s != '\0') st.error = true;
    if (ok) *ok = !st.error;
    return st.error ? 0 : v;
}

static void rtrim_in_place(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static int app_read_line(const char *prompt, char *buf, size_t size) {
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

static bool ci_eq(const char *a, const char *b) {
    return str_casecmp_local(a, b) == 0;
}

static const char *find_word_ci(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!hay || !needle || nlen == 0) return NULL;

    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i != nlen) continue;

        char before = (p == hay) ? ' ' : p[-1];
        char after = p[nlen];
        if ((isalnum((unsigned char)before) || before == '_') ||
            (isalnum((unsigned char)after) || after == '_')) {
            continue;
        }
        return p;
    }
    return NULL;
}

static void basic_reset_program(void) {
    memset(g_basic_program, 0, sizeof g_basic_program);
    g_basic_count = 0;
}

static void basic_store_line(int number, const char *text) {
    int pos = 0;
    while (pos < g_basic_count && g_basic_program[pos].number < number) pos++;

    if (pos < g_basic_count && g_basic_program[pos].number == number) {
        if (!text || !*text) {
            memmove(&g_basic_program[pos], &g_basic_program[pos + 1],
                    (size_t)(g_basic_count - pos - 1) * sizeof g_basic_program[0]);
            g_basic_count--;
        } else {
            strncpy(g_basic_program[pos].text, text, sizeof g_basic_program[pos].text - 1);
            g_basic_program[pos].text[sizeof g_basic_program[pos].text - 1] = '\0';
        }
        return;
    }

    if (!text || !*text) return;
    if (g_basic_count >= BASIC_LINE_MAX) {
        print_line("BASIC: program storage full");
        return;
    }

    memmove(&g_basic_program[pos + 1], &g_basic_program[pos],
            (size_t)(g_basic_count - pos) * sizeof g_basic_program[0]);
    g_basic_program[pos].number = number;
    strncpy(g_basic_program[pos].text, text, sizeof g_basic_program[pos].text - 1);
    g_basic_program[pos].text[sizeof g_basic_program[pos].text - 1] = '\0';
    g_basic_count++;
}

static int basic_find_line(int number) {
    for (int i = 0; i < g_basic_count; i++) {
        if (g_basic_program[i].number == number) return i;
    }
    return -1;
}

static void basic_list_program(void) {
    for (int i = 0; i < g_basic_count; i++) {
        char line[APP_TOKEN_MAX + 32];
        snprintf(line, sizeof line, "%d %s", g_basic_program[i].number, g_basic_program[i].text);
        print_line(line);
    }
}

static bool basic_assign(const char *src) {
    const char *s = skip_ws(src);
    if (!isalpha((unsigned char)*s)) return false;

    char var = (char)toupper((unsigned char)*s++);
    while (isalnum((unsigned char)*s) || *s == '_') s++;
    s = skip_ws(s);
    if (*s != '=') return false;
    s++;

    bool ok = false;
    int value = expr_eval(s, expr_lookup_basic, &g_basic_env, &ok);
    if (!ok) {
        print_line("BASIC: bad expression");
        return true;
    }
    g_basic_env.vars[var - 'A'] = value;
    return true;
}

static void basic_exec_stmt(const char *stmt, int *pc) {
    char tok[APP_TOKEN_MAX];
    const char *s = skip_ws(stmt);
    if (!*s || *s == '\'') return;
    if ((toupper((unsigned char)s[0]) == 'R') && (toupper((unsigned char)s[1]) == 'E') &&
        (toupper((unsigned char)s[2]) == 'M')) return;

    if (*s == '?') {
        s = skip_ws(s + 1);
        if (*s == '"') {
            s++;
            while (*s && *s != '"') sys_putchar(*s++);
            sys_putchar('\n');
        } else {
            bool ok = false;
            int v = expr_eval(s, expr_lookup_basic, &g_basic_env, &ok);
            if (!ok) print_line("BASIC: bad expression");
            else {
                char out[48];
                snprintf(out, sizeof out, "%d", v);
                print_line(out);
            }
        }
        return;
    }

    const char *rest = next_token(s, tok, sizeof tok);

    if (ci_eq(tok, "PRINT")) {
        s = skip_ws(rest);
        if (*s == '"') {
            s++;
            while (*s && *s != '"') sys_putchar(*s++);
            sys_putchar('\n');
        } else {
            bool ok = false;
            int v = expr_eval(s, expr_lookup_basic, &g_basic_env, &ok);
            if (!ok) print_line("BASIC: bad expression");
            else {
                char out[48];
                snprintf(out, sizeof out, "%d", v);
                print_line(out);
            }
        }
    } else if (ci_eq(tok, "LET")) {
        if (!basic_assign(rest)) print_line("usage: LET A = EXPR");
    } else if (ci_eq(tok, "INPUT")) {
        const char *p = skip_ws(rest);
        if (!isalpha((unsigned char)*p)) {
            print_line("usage: INPUT A");
            return;
        }
        char var = (char)toupper((unsigned char)*p);
        char line[64];
        if (app_read_line("? ", line, sizeof line) >= 0)
            g_basic_env.vars[var - 'A'] = atoi(line);
    } else if (ci_eq(tok, "IF")) {
        const char *then = find_word_ci(rest, "THEN");
        if (!then) {
            print_line("usage: IF EXPR THEN STATEMENT");
            return;
        }
        char expr[APP_TOKEN_MAX];
        size_t len = (size_t)(then - rest);
        if (len >= sizeof expr) len = sizeof expr - 1;
        memcpy(expr, rest, len);
        expr[len] = '\0';
        bool ok = false;
        int cond = expr_eval(expr, expr_lookup_basic, &g_basic_env, &ok);
        if (!ok) {
            print_line("BASIC: bad IF expression");
            return;
        }
        if (cond) {
            then = skip_ws(then + 4);
            if (isdigit((unsigned char)*then)) {
                int line_no = atoi(then);
                int idx = basic_find_line(line_no);
                if (idx >= 0) *pc = idx - 1;
                else print_line("BASIC: line not found");
            } else {
                basic_exec_stmt(then, pc);
            }
        }
    } else if (ci_eq(tok, "FOR")) {
        const char *p = skip_ws(rest);
        if (!isalpha((unsigned char)*p)) {
            print_line("usage: FOR I = START TO END [STEP N]");
            return;
        }
        int var_index = toupper((unsigned char)*p) - 'A';
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        p = skip_ws(p);
        if (*p != '=') {
            print_line("usage: FOR I = START TO END [STEP N]");
            return;
        }
        p++;

        const char *to_kw = find_word_ci(p, "TO");
        if (!to_kw) {
            print_line("usage: FOR I = START TO END [STEP N]");
            return;
        }

        char start_expr[APP_TOKEN_MAX];
        size_t slen = (size_t)(to_kw - p);
        if (slen >= sizeof start_expr) slen = sizeof start_expr - 1;
        memcpy(start_expr, p, slen);
        start_expr[slen] = '\0';

        const char *after_to = skip_ws(to_kw + 2);
        const char *step_kw = find_word_ci(after_to, "STEP");

        char limit_expr[APP_TOKEN_MAX];
        char step_expr[APP_TOKEN_MAX] = "1";
        size_t llen = step_kw ? (size_t)(step_kw - after_to) : strlen(after_to);
        if (llen >= sizeof limit_expr) llen = sizeof limit_expr - 1;
        memcpy(limit_expr, after_to, llen);
        limit_expr[llen] = '\0';

        if (step_kw) {
            strncpy(step_expr, skip_ws(step_kw + 4), sizeof step_expr - 1);
            step_expr[sizeof step_expr - 1] = '\0';
        }

        bool ok1 = false, ok2 = false, ok3 = false;
        int start = expr_eval(start_expr, expr_lookup_basic, &g_basic_env, &ok1);
        int limit = expr_eval(limit_expr, expr_lookup_basic, &g_basic_env, &ok2);
        int step = expr_eval(step_expr, expr_lookup_basic, &g_basic_env, &ok3);
        if (!ok1 || !ok2 || !ok3 || step == 0) {
            print_line("BASIC: bad FOR expression");
            return;
        }

        g_basic_env.vars[var_index] = start;

        if ((step > 0 && start > limit) || (step < 0 && start < limit)) {
            int depth = 1;
            for (int i = *pc + 1; i < g_basic_count; i++) {
                char scan[APP_TOKEN_MAX];
                next_token(skip_ws(g_basic_program[i].text), scan, sizeof scan);
                if (ci_eq(scan, "FOR")) depth++;
                else if (ci_eq(scan, "NEXT")) {
                    depth--;
                    if (depth == 0) {
                        *pc = i;
                        return;
                    }
                }
            }
            return;
        }

        if (g_basic_env.loop_top >= BASIC_STACK_MAX) {
            print_line("BASIC: FOR stack full");
            return;
        }
        g_basic_env.loops[g_basic_env.loop_top].var_index = var_index;
        g_basic_env.loops[g_basic_env.loop_top].limit = limit;
        g_basic_env.loops[g_basic_env.loop_top].step = step;
        g_basic_env.loops[g_basic_env.loop_top].for_pc = *pc;
        g_basic_env.loop_top++;
    } else if (ci_eq(tok, "NEXT")) {
        if (g_basic_env.loop_top <= 0) {
            print_line("BASIC: NEXT without FOR");
            return;
        }
        basic_for_t *loop = &g_basic_env.loops[g_basic_env.loop_top - 1];
        const char *p = skip_ws(rest);
        if (*p && isalpha((unsigned char)*p)) {
            int want = toupper((unsigned char)*p) - 'A';
            if (want != loop->var_index) {
                print_line("BASIC: NEXT variable mismatch");
                return;
            }
        }
        g_basic_env.vars[loop->var_index] += loop->step;
        int cur = g_basic_env.vars[loop->var_index];
        if ((loop->step > 0 && cur <= loop->limit) || (loop->step < 0 && cur >= loop->limit)) {
            *pc = loop->for_pc;
        } else {
            g_basic_env.loop_top--;
        }
    } else if (ci_eq(tok, "GOSUB")) {
        int line_no = atoi(skip_ws(rest));
        int idx = basic_find_line(line_no);
        if (idx < 0) {
            print_line("BASIC: line not found");
            return;
        }
        if (g_basic_env.gosub_top >= BASIC_STACK_MAX) {
            print_line("BASIC: GOSUB stack full");
            return;
        }
        g_basic_env.gosub_stack[g_basic_env.gosub_top++] = *pc;
        *pc = idx - 1;
    } else if (ci_eq(tok, "RETURN")) {
        if (g_basic_env.gosub_top <= 0) {
            print_line("BASIC: RETURN without GOSUB");
            return;
        }
        *pc = g_basic_env.gosub_stack[--g_basic_env.gosub_top];
    } else if (ci_eq(tok, "GOTO")) {
        int line_no = atoi(skip_ws(rest));
        int idx = basic_find_line(line_no);
        if (idx >= 0) *pc = idx - 1;
        else print_line("BASIC: line not found");
    } else if (ci_eq(tok, "CLS") || ci_eq(tok, "CLEAR")) {
        sys_clear();
    } else if (ci_eq(tok, "LIST")) {
        basic_list_program();
    } else if (ci_eq(tok, "NEW")) {
        basic_reset_program();
        memset(&g_basic_env, 0, sizeof g_basic_env);
    } else if (ci_eq(tok, "END") || ci_eq(tok, "STOP")) {
        g_basic_env.stop = true;
    } else if (!basic_assign(s)) {
        print_line("BASIC: unknown statement");
    }
}

static void basic_run_program(void) {
    g_basic_env.stop = false;
    g_basic_env.loop_top = 0;
    g_basic_env.gosub_top = 0;
    for (int pc = 0; pc < g_basic_count && !g_basic_env.stop; pc++) {
        basic_exec_stmt(g_basic_program[pc].text, &pc);
    }
}
static void app_basic(const char *arg) {
    const char *path = skip_ws(arg);
    if (*path) {
        char file_buf[APP_READ_MAX + 1];
        int n = read_text_file(path, file_buf, sizeof file_buf, "basic");
        if (n < 0) return;

        bool numbered = false;
        for (char *p = file_buf; *p; p++) {
            if (!isspace((unsigned char)*p)) {
                numbered = isdigit((unsigned char)*p);
                break;
            }
        }

        if (numbered) {
            basic_reset_program();
            char *saveptr = NULL;
            for (char *line = strtok_r(file_buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
                while (*line == '\r' || *line == ' ' || *line == '\t') line++;
                if (!isdigit((unsigned char)*line)) continue;
                int number = atoi(line);
                while (isdigit((unsigned char)*line)) line++;
                line = (char *)skip_ws(line);
                rtrim_in_place(line);
                basic_store_line(number, line);
            }
            basic_run_program();
        } else {
            char *saveptr = NULL;
            for (char *line = strtok_r(file_buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
                rtrim_in_place(line);
                int dummy_pc = -1;
                basic_exec_stmt(line, &dummy_pc);
                if (g_basic_env.stop) break;
            }
            g_basic_env.stop = false;
        }
        return;
    }

    print_line("Mellivora BASIC for PicoCalc");
    print_line("Commands: RUN, LIST, NEW, PRINT, LET, INPUT, IF/THEN, FOR/NEXT,");
    print_line("          GOTO, GOSUB, RETURN, CLS, END, BYE");
    while (1) {
        char line[APP_TOKEN_MAX];
        if (app_read_line("BASIC> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        const char *s = skip_ws(line);
        if (!*s) continue;
        if (ci_eq(s, "BYE") || ci_eq(s, "EXIT") || ci_eq(s, "QUIT")) break;
        if (isdigit((unsigned char)*s)) {
            int number = atoi(s);
            while (isdigit((unsigned char)*s)) s++;
            s = skip_ws(s);
            basic_store_line(number, s);
        } else if (ci_eq(s, "RUN")) {
            basic_run_program();
        } else {
            int dummy_pc = -1;
            basic_exec_stmt(s, &dummy_pc);
            g_basic_env.stop = false;
        }
    }
}

static void tinyc_print_arg(const char *src) {
    const char *s = skip_ws(src);
    if (*s == '(') {
        s++;
        const char *end = strrchr(s, ')');
        if (end) {
            char tmp[APP_TOKEN_MAX];
            size_t len = (size_t)(end - s);
            if (len >= sizeof tmp) len = sizeof tmp - 1;
            memcpy(tmp, s, len);
            tmp[len] = '\0';
            s = tmp;
        }
    }

    if (*s == '"') {
        s++;
        while (*s && *s != '"') sys_putchar(*s++);
        sys_putchar('\n');
    } else {
        bool ok = false;
        int v = expr_eval(s, expr_lookup_tinyc, &g_tinyc_env, &ok);
        if (!ok) print_line("TinyC: bad expression");
        else {
            char out[48];
            snprintf(out, sizeof out, "%d", v);
            print_line(out);
        }
    }
}

static void tinyc_show_vars(void) {
    bool any = false;
    for (int i = 0; i < TINYC_VAR_MAX; i++) {
        if (!g_tinyc_env.vars[i].used) continue;
        char line[128];
        if (g_tinyc_env.vars[i].is_array) {
            int pos = snprintf(line, sizeof line, "%s[] =", g_tinyc_env.vars[i].name);
            for (int j = 0; j < g_tinyc_env.vars[i].array_len && pos < (int)sizeof line - 8; j++) {
                pos += snprintf(line + pos, sizeof line - pos, " %d", g_tinyc_env.vars[i].array[j]);
            }
        } else {
            snprintf(line, sizeof line, "%s = %d", g_tinyc_env.vars[i].name, g_tinyc_env.vars[i].value);
        }
        print_line(line);
        any = true;
    }
    if (!any) print_line("(no variables)");
}

static void tinyc_exec_stmt(char *stmt) {
    char name[16];
    char *s = stmt;
    while (*s && isspace((unsigned char)*s)) s++;
    rtrim_in_place(s);
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == ';') s[len - 1] = '\0';
    s = (char *)skip_ws(s);
    if (!*s || (s[0] == '/' && s[1] == '/')) return;

    if (ci_eq(s, "help")) {
        print_line("TinyC: int x = 1; x = x + 1; print(x); if (x > 0) print(x);");
        print_line("       int arr[4]; arr[0] = 42; print(arr[0]); vars; clear; exit;");
        return;
    }
    if (ci_eq(s, "vars")) {
        tinyc_show_vars();
        return;
    }
    if (ci_eq(s, "clear")) {
        memset(&g_tinyc_env, 0, sizeof g_tinyc_env);
        print_line("TinyC: state cleared");
        return;
    }
    if (ci_eq(s, "exit") || ci_eq(s, "quit") || ci_eq(s, "return")) {
        g_tinyc_env.stop = true;
        return;
    }
    if (strncmp(s, "print", 5) == 0 && !isalnum((unsigned char)s[5]) && s[5] != '_') {
        tinyc_print_arg(s + 5);
        return;
    }
    if (strncmp(s, "puts", 4) == 0 && !isalnum((unsigned char)s[4]) && s[4] != '_') {
        tinyc_print_arg(s + 4);
        return;
    }
    if (strncmp(s, "if", 2) == 0 && isspace((unsigned char)s[2])) {
        char *open = strchr(s, '(');
        char *close = strrchr(s, ')');
        if (!open || !close || close <= open) {
            print_line("TinyC: usage: if (expr) statement");
            return;
        }
        char expr[APP_TOKEN_MAX];
        size_t elen = (size_t)(close - open - 1);
        if (elen >= sizeof expr) elen = sizeof expr - 1;
        memcpy(expr, open + 1, elen);
        expr[elen] = '\0';
        bool ok = false;
        int cond = expr_eval(expr, expr_lookup_tinyc, &g_tinyc_env, &ok);
        if (!ok) {
            print_line("TinyC: bad if condition");
            return;
        }
        if (cond) {
            char *body = (char *)skip_ws(close + 1);
            if (*body) tinyc_exec_stmt(body);
        }
        return;
    }
    if (strncmp(s, "while", 5) == 0 && isspace((unsigned char)s[5])) {
        char *open = strchr(s, '(');
        char *close = strrchr(s, ')');
        if (!open || !close || close <= open) {
            print_line("TinyC: usage: while (expr) statement");
            return;
        }
        char expr[APP_TOKEN_MAX];
        char body[APP_TOKEN_MAX];
        size_t elen = (size_t)(close - open - 1);
        if (elen >= sizeof expr) elen = sizeof expr - 1;
        memcpy(expr, open + 1, elen);
        expr[elen] = '\0';
        strncpy(body, skip_ws(close + 1), sizeof body - 1);
        body[sizeof body - 1] = '\0';
        for (int guard = 0; guard < 1024; guard++) {
            bool ok = false;
            int cond = expr_eval(expr, expr_lookup_tinyc, &g_tinyc_env, &ok);
            if (!ok || !cond || g_tinyc_env.stop) break;
            tinyc_exec_stmt(body);
        }
        return;
    }

    if (strncmp(s, "int", 3) == 0 && isspace((unsigned char)s[3])) {
        s = (char *)skip_ws(s + 3);
        if (!isalpha((unsigned char)*s) && *s != '_') {
            print_line("TinyC: expected variable name");
            return;
        }
        size_t i = 0;
        while ((isalnum((unsigned char)*s) || *s == '_' || *s == '[' || *s == ']') && i + 1 < sizeof name) name[i++] = *s++;
        name[i] = '\0';
        s = (char *)skip_ws(s);
        int value = 0;
        if (*s == '=') {
            bool ok = false;
            value = expr_eval(s + 1, expr_lookup_tinyc, &g_tinyc_env, &ok);
            if (!ok) {
                print_line("TinyC: bad initializer");
                return;
            }
        }
        tinyc_set_var(&g_tinyc_env, name, value);
        return;
    }

    if (isalpha((unsigned char)*s) || *s == '_') {
        const char *p = s;
        size_t i = 0;
        while ((isalnum((unsigned char)*p) || *p == '_' || *p == '[' || *p == ']') && i + 1 < sizeof name) name[i++] = *p++;
        name[i] = '\0';
        p = skip_ws(p);
        if (*p == '=' && p[1] != '=') {
            bool ok = false;
            int value = expr_eval(p + 1, expr_lookup_tinyc, &g_tinyc_env, &ok);
            if (!ok) {
                print_line("TinyC: bad assignment");
                return;
            }
            tinyc_set_var(&g_tinyc_env, name, value);
            return;
        }
    }

    print_line("TinyC: unknown statement");
}

static void app_tinyc(const char *arg) {
    const char *path = skip_ws(arg);
    g_tinyc_env.stop = false;

    if (*path) {
        char file_buf[APP_READ_MAX + 1];
        int n = read_text_file(path, file_buf, sizeof file_buf, "tcc");
        if (n < 0) return;
        char *saveptr = NULL;
        for (char *line = strtok_r(file_buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
            rtrim_in_place(line);
            tinyc_exec_stmt(line);
            if (g_tinyc_env.stop) break;
        }
        g_tinyc_env.stop = false;
        return;
    }

    print_line("Mellivora Tiny C for PicoCalc");
    print_line("Type help for syntax, vars to inspect state, clear to reset, and exit to leave.");
    while (!g_tinyc_env.stop) {
        char line[APP_TOKEN_MAX];
        if (app_read_line("TinyC> ", line, sizeof line) < 0) break;
        tinyc_exec_stmt(line);
    }
    g_tinyc_env.stop = false;
}

static void app_script(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) {
        print_line("usage: script FILE");
        return;
    }

    char file_buf[APP_READ_MAX + 1];
    int n = read_text_file(path, file_buf, sizeof file_buf, "script");
    if (n < 0) return;

    char *saveptr = NULL;
    for (char *line = strtok_r(file_buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        if (!*line || *line == '#') continue; // skip empty and comment lines
        char cmd[APP_TOKEN_MAX];
        const char *arg_start = skip_ws(line);
        if (!*arg_start) continue;
        size_t i = 0;
        while (*arg_start && !isspace((unsigned char)*arg_start) && i + 1 < sizeof cmd) {
            cmd[i++] = *arg_start++;
        }
        cmd[i] = '\0';
        const char *arg = skip_ws(arg_start);
        if (!app_run(cmd, arg)) {
            char err[64];
            snprintf(err, sizeof err, "script: unknown command '%s'", cmd);
            print_line(err);
        }
    }
}

static void app_paint(const char *arg) {
    (void)arg;
    static uint8_t canvas[128 * 64]; // 128x64 pixels, 1 bit per pixel
    memset(canvas, 0, sizeof canvas);

    int x = 64, y = 32; // start in center
    bool drawing = false;
    sys_clear();
    sys_print("Paint: arrows move, space draw, c clear, s save, l load, q quit\n");
    sys_print("Cursor: [ ]\n");

    while (1) {
        // draw cursor
        uint32_t color = canvas[y * 128 + x] ? LCD_WHITE : LCD_BLACK;
        lcd_draw_pixel(x, y, color ^ 0xFFFFFF); // invert for cursor

        int ch = sys_getchar();
        // erase cursor
        color = canvas[y * 128 + x] ? LCD_WHITE : LCD_BLACK;
        lcd_draw_pixel(x, y, color);

        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == 'c' || ch == 'C') {
            sys_clear();
            sys_print("Paint: cleared\n");
        } else if (ch == 's' || ch == 'S') {
            // save to file
            char filename[32];
            sys_print("Save to: ");
            if (app_read_line("", filename, sizeof filename) >= 0 && *filename) {
                char buf[4096];
                int pos = 0;
                for (int yy = 0; yy < 64 && pos < (int)sizeof buf - 2; yy++) {
                    for (int xx = 0; xx < 128 && pos < (int)sizeof buf - 2; xx++) {
                        buf[pos++] = canvas[yy * 128 + xx] ? '#' : ' ';
                    }
                    buf[pos++] = '\n';
                }
                buf[pos] = '\0';
                write_text_file(filename, buf, "paint");
                sys_print("Saved\n");
            }
        } else if (ch == 'l' || ch == 'L') {
            // load from file
            char filename[32];
            sys_print("Load from: ");
            if (app_read_line("", filename, sizeof filename) >= 0 && *filename) {
                char buf[4096];
                int n = read_text_file(filename, buf, sizeof buf, "paint");
                if (n >= 0) {
                    memset(canvas, 0, sizeof canvas);
                    int pos = 0;
                    for (int yy = 0; yy < 64; yy++) {
                        for (int xx = 0; xx < 128; xx++) {
                            if (pos < n && buf[pos] != '\n') {
                                canvas[yy * 128 + xx] = (buf[pos] == '#');
                                pos++;
                            } else {
                                break;
                            }
                        }
                        if (pos < n && buf[pos] == '\n') pos++;
                    }
                    // redraw
                    for (int yy = 0; yy < 64; yy++) {
                        for (int xx = 0; xx < 128; xx++) {
                            uint32_t color = canvas[yy * 128 + xx] ? LCD_WHITE : LCD_BLACK;
                            lcd_draw_pixel(xx, yy, color);
                        }
                    }
                    sys_print("Loaded\n");
                }
            }
        } else if (ch == ' ') {
            drawing = !drawing;
            if (drawing) {
                canvas[y * 128 + x] = 1;
                lcd_draw_pixel(x, y, LCD_WHITE);
            }
        } else if (ch == 0x1b) { // escape sequence for arrows
            int seq = sys_getchar();
            if (seq == '[') {
                int dir = sys_getchar();
                if (dir == 'A' && y > 0) y--; // up
                else if (dir == 'B' && y < 63) y++; // down
                else if (dir == 'D' && x > 0) x--; // left
                else if (dir == 'C' && x < 127) x++; // right
            }
        }
        if (drawing) {
            canvas[y * 128 + x] = 1;
            lcd_draw_pixel(x, y, LCD_WHITE);
        }
    }
    sys_clear();
}

bool app_run(const char *cmd, const char *arg) {
    if (!cmd || !*cmd) return false;

    if      (strcmp(cmd, "hello") == 0)    app_hello(arg);
    else if (strcmp(cmd, "basename") == 0) app_basename(arg);
    else if (strcmp(cmd, "dirname") == 0)  app_dirname(arg);
    else if (strcmp(cmd, "seq") == 0)      app_seq(arg);
    else if (strcmp(cmd, "head") == 0)     app_head(arg);
    else if (strcmp(cmd, "tail") == 0)     app_tail(arg);
    else if (strcmp(cmd, "wc") == 0)       app_wc(arg);
    else if (strcmp(cmd, "cut") == 0)      app_cut(arg);
    else if (strcmp(cmd, "grep") == 0)     app_grep(arg);
    else if (strcmp(cmd, "find") == 0)     app_find(arg);
    else if (strcmp(cmd, "pager") == 0)    app_pager(arg);
    else if (strcmp(cmd, "rev") == 0)      app_rev(arg);
    else if (strcmp(cmd, "sort") == 0)     app_sort(arg);
    else if (strcmp(cmd, "hexdump") == 0)  app_hexdump(arg);
    else if (strcmp(cmd, "od") == 0)       app_od(arg);
    else if (strcmp(cmd, "calc") == 0)     app_calc(arg);
    else if (strcmp(cmd, "cp") == 0)       app_cp(arg);
    else if (strcmp(cmd, "mv") == 0)       app_mv(arg);
    else if (strcmp(cmd, "stat") == 0)     app_stat(arg);
    else if (strcmp(cmd, "edit") == 0 || strcmp(cmd, "bedit") == 0) app_edit(arg);
    else if (strcmp(cmd, "browse") == 0 || strcmp(cmd, "files") == 0) app_browse(arg);
    else if (strcmp(cmd, "notes") == 0 || strcmp(cmd, "memo") == 0) app_notes(arg);
    else if (strcmp(cmd, "home") == 0 || strcmp(cmd, "launcher") == 0) app_home(arg);
    else if (strcmp(cmd, "dashboard") == 0 || strcmp(cmd, "status") == 0) app_dashboard(arg);
    else if (strcmp(cmd, "sysmon") == 0 || strcmp(cmd, "monitor") == 0) app_sysmon(arg);
    else if (strcmp(cmd, "samples") == 0 || strcmp(cmd, "demos") == 0) app_samples(arg);
    else if (strcmp(cmd, "clock") == 0)    app_clock(arg);
    else if (strcmp(cmd, "cal") == 0 || strcmp(cmd, "calendar") == 0) app_cal(arg);
    else if (strcmp(cmd, "basic") == 0)    app_basic(arg);
    else if (strcmp(cmd, "tcc") == 0 || strcmp(cmd, "tinyc") == 0) app_tinyc(arg);
    else if (strcmp(cmd, "script") == 0)  app_script(arg);
    else if (strcmp(cmd, "paint") == 0)   app_paint(arg);
    else if (strcmp(cmd, "sleep") == 0)    app_sleep_ms(arg);
    else if (strcmp(cmd, "id") == 0)       app_id(arg);
    else if (strcmp(cmd, "true") == 0)     return true;
    else if (strcmp(cmd, "false") == 0)    return true;
    else return false;

    return true;
}
