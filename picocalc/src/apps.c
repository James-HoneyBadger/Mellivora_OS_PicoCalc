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
#define EDIT_SAVE_MAX    (EDIT_LINES_MAX * (EDIT_LINE_MAX + 1) + 1)
#define BROWSE_ITEMS_MAX 64
#define BROWSE_NAME_MAX  32
#define PAINT_WIDTH      128
#define PAINT_HEIGHT     64
#define PAINT_SAVE_MAX   (PAINT_WIDTH * PAINT_HEIGHT + PAINT_HEIGHT + 1)
#define SETTINGS_FILE    "/SETTINGS.CFG"
#define TODO_FILE        "/TODO.TXT"
#define PLANNER_FILE     "/PLANNER.TXT"
#define BOOKMARKS_FILE   "/BOOKMARKS.CFG"
#define JOURNAL_FILE     "/JOURNAL.TXT"
#define HABITS_FILE      "/HABITS.CFG"
#define SPRITE_FILE      "/SPRITE.TXT"
#define TODO_ITEMS_MAX   64
#define TODO_TEXT_MAX    96
#define PLANNER_ITEMS_MAX 96
#define JOURNAL_ITEMS_MAX 96
#define BOOKMARKS_MAX    32
#define BOOKMARK_LABEL_MAX 24
#define HABITS_MAX       32
#define HABIT_NAME_MAX   32
#define SPRITE_SIDE      16
#define SNAKE_W          16
#define SNAKE_H          10
#define SNAKE_MAX_CELLS  (SNAKE_W * SNAKE_H)
#define HEXEDIT_BYTES_MAX APP_READ_MAX

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

typedef struct {
    int backlight;
    char notes_path[APP_TOKEN_MAX];
    char home_path[APP_TOKEN_MAX];
    char startup_app[APP_TOKEN_MAX];
    char autorun_path[APP_TOKEN_MAX];
    bool loaded;
} app_settings_state_t;

typedef struct {
    bool done;
    char text[TODO_TEXT_MAX];
} todo_item_t;

typedef struct {
    char date[16];
    char text[TODO_TEXT_MAX];
} planner_item_t;

typedef struct {
    char label[BOOKMARK_LABEL_MAX];
    char target[APP_TOKEN_MAX];
} bookmark_item_t;

typedef struct {
    char date[16];
    char text[TODO_TEXT_MAX];
} journal_item_t;

typedef struct {
    char name[HABIT_NAME_MAX];
    int count;
    char last_date[16];
} habit_item_t;

typedef struct {
    char path[APP_TOKEN_MAX];
    int depth;
    int max_depth;
    int *file_count;
    int *dir_count;
} tree_walk_t;

typedef struct {
    uint32_t total_bytes;
    int files;
    int dirs;
} du_stat_t;

typedef struct {
    char path[APP_TOKEN_MAX];
    du_stat_t stat;
} du_walk_ctx_t;

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
static app_settings_state_t g_settings;

static void app_make_abs(const char *path, char *out, size_t out_sz);
static bool app_dispatch_named(const char *cmd, const char *arg);
static const char *next_token(const char *s, char *tok, size_t tok_sz);
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
static void app_script(const char *arg);
static void app_settings(const char *arg);
static void app_set(const char *arg);
static void app_todo(const char *arg);
static void app_planner(const char *arg);
static void app_journal(const char *arg);
static void app_habits(const char *arg);
static void app_bookmarks(const char *arg);
static void app_games(const char *arg);
static void app_dice(const char *arg);
static void app_coin(const char *arg);
static void app_guess(const char *arg);
static void app_snake(const char *arg);
static void app_sprite(const char *arg);
static void app_terminal(const char *arg);
static void app_tree(const char *arg);
static void app_du(const char *arg);
static void app_df(const char *arg);
static void app_hexedit(const char *arg);
static void app_join_path(const char *root, const char *name, char *out, size_t out_sz);
static void app_tree_cb(const char *name, uint32_t size, bool is_dir, void *opaque);
static void app_du_accum_cb(const char *name, uint32_t size, bool is_dir, void *opaque);
static void app_du_list_cb(const char *name, uint32_t size, bool is_dir, void *opaque);
static uint32_t app_du_path(const char *path, du_stat_t *out);

static const char *skip_ws(const char *s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void copy_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void append_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0 || !src) return;
    size_t len = strlen(dst);
    while (*src && len + 1 < dst_sz) dst[len++] = *src++;
    dst[len] = '\0';
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
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    if (!path || !*path) {
        copy_cstr(out, out_sz, _sys_cwd);
    } else if (path[0] == '/') {
        copy_cstr(out, out_sz, path);
    } else if (strcmp(_sys_cwd, "/") == 0) {
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, path);
    } else {
        append_cstr(out, out_sz, _sys_cwd);
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, path);
    }
}

static void app_join_path(const char *root, const char *name, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    if (!root || !*root || strcmp(root, "/") == 0) {
        append_cstr(out, out_sz, "/");
        if (name) append_cstr(out, out_sz, name);
        return;
    }

    append_cstr(out, out_sz, root);
    if (name && *name) {
        append_cstr(out, out_sz, "/");
        append_cstr(out, out_sz, name);
    }
}

static void settings_reset_defaults(void) {
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.backlight = 128;
    copy_cstr(g_settings.notes_path, sizeof g_settings.notes_path, "/NOTES.TXT");
    copy_cstr(g_settings.home_path, sizeof g_settings.home_path, "/");
}

static void settings_set_path(char *dst, size_t dst_sz, const char *value, const char *fallback) {
    const char *v = skip_ws(value);
    if (!*v || ci_eq(v, "default")) {
        copy_cstr(dst, dst_sz, fallback ? fallback : "");
    } else if (ci_eq(v, "none") || ci_eq(v, "off")) {
        dst[0] = '\0';
    } else {
        app_make_abs(v, dst, dst_sz);
    }
}

static void settings_apply_pair(const char *key, const char *value) {
    const char *v = skip_ws(value ? value : "");
    if (!key || !*key) return;

    if (ci_eq(key, "backlight")) {
        int level = atoi(v);
        if (level < 0) level = 0;
        if (level > 255) level = 255;
        g_settings.backlight = level;
    } else if (ci_eq(key, "notes")) {
        settings_set_path(g_settings.notes_path, sizeof g_settings.notes_path, v, "/NOTES.TXT");
    } else if (ci_eq(key, "home")) {
        char tmp[APP_TOKEN_MAX];
        settings_set_path(tmp, sizeof tmp, v, "/");
        if (fat_is_dir(tmp) == FAT_OK) copy_cstr(g_settings.home_path, sizeof g_settings.home_path, tmp);
        else print_line("settings: home must be an existing directory");
    } else if (ci_eq(key, "startup")) {
        if (!*v || ci_eq(v, "none") || ci_eq(v, "shell")) {
            g_settings.startup_app[0] = '\0';
        } else {
            char token[APP_TOKEN_MAX];
            next_token(v, token, sizeof token);
            copy_cstr(g_settings.startup_app, sizeof g_settings.startup_app, token);
        }
    } else if (ci_eq(key, "autorun")) {
        settings_set_path(g_settings.autorun_path, sizeof g_settings.autorun_path, v, "");
    }
}

static void settings_apply_live(void) {
    if (g_settings.backlight < 0) g_settings.backlight = 0;
    if (g_settings.backlight > 255) g_settings.backlight = 255;
    kbd_set_backlight((uint8_t)g_settings.backlight);
    if (*g_settings.home_path) (void)sys_chdir(g_settings.home_path);
}

static bool settings_load(bool verbose) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(SETTINGS_FILE, buf, APP_READ_MAX);
    if (n < 0) {
        g_settings.loaded = false;
        if (verbose) print_line("settings: using defaults");
        settings_apply_live();
        return false;
    }

    settings_reset_defaults();
    buf[n] = '\0';

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq++ = '\0';
        rtrim_in_place(s);
        settings_apply_pair(s, eq);
    }

    g_settings.loaded = true;
    settings_apply_live();
    if (verbose) print_line("settings: loaded");
    return true;
}

static bool settings_save(bool verbose) {
    char buf[APP_READ_MAX];
    int n = snprintf(buf, sizeof buf,
                     "# Mellivora PicoCalc settings\n"
                     "BACKLIGHT=%d\n"
                     "HOME=%s\n"
                     "NOTES=%s\n"
                     "STARTUP=%s\n"
                     "AUTORUN=%s\n",
                     g_settings.backlight,
                     *g_settings.home_path ? g_settings.home_path : "/",
                     *g_settings.notes_path ? g_settings.notes_path : "/NOTES.TXT",
                     *g_settings.startup_app ? g_settings.startup_app : "none",
                     *g_settings.autorun_path ? g_settings.autorun_path : "none");
    if (n < 0 || n >= (int)sizeof buf) {
        if (verbose) print_line("settings: buffer overflow while saving");
        return false;
    }
    if (sys_fwrite(SETTINGS_FILE, buf, (uint32_t)n) < 0) {
        if (verbose) print_line("settings: save failed");
        return false;
    }
    g_settings.loaded = true;
    if (verbose) print_line("settings: saved");
    return true;
}

static void settings_show(void) {
    char out[160];
    print_line("Persistent settings");
    snprintf(out, sizeof out, "  backlight: %d", g_settings.backlight);
    print_line(out);
    snprintf(out, sizeof out, "  home:      %.96s", *g_settings.home_path ? g_settings.home_path : "/");
    print_line(out);
    snprintf(out, sizeof out, "  notes:     %.96s", *g_settings.notes_path ? g_settings.notes_path : "/NOTES.TXT");
    print_line(out);
    snprintf(out, sizeof out, "  startup:   %.96s", *g_settings.startup_app ? g_settings.startup_app : "(none)");
    print_line(out);
    snprintf(out, sizeof out, "  autorun:   %.96s", *g_settings.autorun_path ? g_settings.autorun_path : "(none)");
    print_line(out);
    print_line("Use: settings set KEY VALUE");
}

void app_init(void) {
    settings_reset_defaults();
    settings_load(false);
    settings_apply_live();
}

void app_boot(void) {
    if (*g_settings.autorun_path) {
        print_line("Running autorun script...");
        app_script(g_settings.autorun_path);
    }
    if (*g_settings.startup_app) {
        char out[96];
        snprintf(out, sizeof out, "Opening startup app: %.48s", g_settings.startup_app);
        print_line(out);
        (void)app_dispatch_named(g_settings.startup_app, "");
    }
}

static void app_settings(const char *arg) {
    char cmd[APP_TOKEN_MAX];
    char key[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);

    if (!*cmd || ci_eq(cmd, "show")) {
        settings_show();
        return;
    }

    if (ci_eq(cmd, "save")) {
        settings_save(true);
        return;
    }
    if (ci_eq(cmd, "load")) {
        settings_load(true);
        settings_show();
        return;
    }
    if (ci_eq(cmd, "reset")) {
        settings_reset_defaults();
        settings_apply_live();
        settings_save(true);
        settings_show();
        return;
    }

    if (ci_eq(cmd, "set")) {
        s = next_token(s, key, sizeof key);
    } else {
        copy_cstr(key, sizeof key, cmd);
    }

    s = skip_ws(s);
    if (!*key || !*s) {
        print_line("usage: settings set [backlight|home|notes|startup|autorun] VALUE");
        return;
    }

    settings_apply_pair(key, s);
    settings_apply_live();
    settings_save(true);
    settings_show();
}

static void app_set(const char *arg) {
    app_settings(arg);
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

    char path[APP_TOKEN_MAX];
    path[0] = '\0';
    if (strcmp(ctx->root, "/") == 0) {
        append_cstr(path, sizeof path, "/");
        append_cstr(path, sizeof path, name);
    } else {
        append_cstr(path, sizeof path, ctx->root);
        append_cstr(path, sizeof path, "/");
        append_cstr(path, sizeof path, name);
    }
    print_line(path);
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

    char out[128];
    snprintf(out, sizeof out, "%d %d %d %.80s", lines, words, n, path);
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
            if (idx < end) pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02X ", buf[idx]);
            else pos += snprintf(line + pos, sizeof line - (size_t)pos, "   ");
        }
        pos += snprintf(line + pos, sizeof line - (size_t)pos, " |" );
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

static void app_hexedit(const char *arg) {
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
        if ((size_t)n == sizeof buf) print_line("hexedit: showing first 4096 bytes of file");
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
            next_token(s, off_tok, sizeof off_tok);
            next_token(next_token(s, off_tok, sizeof off_tok), len_tok, sizeof len_tok);
            size_t start = *off_tok ? (size_t)strtoul(off_tok, NULL, 0) : 0;
            size_t span = *len_tok ? (size_t)strtoul(len_tok, NULL, 0) : 128;
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
            if (!*off_tok) {
                print_line("usage: set OFFSET HH [HH ...]");
                continue;
            }
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
                if (pos >= sizeof buf) {
                    print_line("hexedit: write exceeds buffer limit");
                    break;
                }
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
            if (!*off_tok || !*text) {
                print_line("usage: ascii OFFSET TEXT");
                continue;
            }
            size_t pos = (size_t)strtoul(off_tok, NULL, 0);
            size_t wrote = 0;
            while (*text && pos < sizeof buf) {
                buf[pos++] = (uint8_t)*text++;
                wrote++;
            }
            if (wrote == 0) {
                print_line("hexedit: nothing written");
            } else {
                if (pos > len) len = pos;
                dirty = true;
                print_line("hexedit: text written");
            }
            continue;
        }

        if (ci_eq(cmd, "fill")) {
            char off_tok[32], len_tok[32], byte_tok[16];
            s = next_token(s, off_tok, sizeof off_tok);
            s = next_token(s, len_tok, sizeof len_tok);
            next_token(s, byte_tok, sizeof byte_tok);
            uint8_t value;
            if (!*off_tok || !*len_tok || !parse_hex_byte_token(byte_tok, &value)) {
                print_line("usage: fill OFFSET LEN HH");
                continue;
            }
            size_t pos = (size_t)strtoul(off_tok, NULL, 0);
            size_t span = (size_t)strtoul(len_tok, NULL, 0);
            if (pos >= sizeof buf) {
                print_line("hexedit: offset beyond buffer");
                continue;
            }
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
                print_line("usage: resize N   (max 4096)");
                continue;
            }
            if (new_len > len) memset(buf + len, 0, new_len - len);
            len = new_len;
            dirty = true;
            print_line("hexedit: size updated");
            continue;
        }

        if (ci_eq(cmd, "saveas")) {
            char new_path[APP_TOKEN_MAX];
            next_token(s, new_path, sizeof new_path);
            if (!*new_path) {
                print_line("usage: saveas PATH");
                continue;
            }
            app_make_abs(new_path, target, sizeof target);
            if (sys_fwrite(target, buf, (uint32_t)len) < 0) print_line("hexedit: save failed");
            else {
                dirty = false;
                print_line("hexedit: file saved");
            }
            continue;
        }

        if (ci_eq(cmd, "save") || ci_eq(cmd, "write") || ci_eq(cmd, "w")) {
            if (sys_fwrite(target, buf, (uint32_t)len) < 0) print_line("hexedit: save failed");
            else {
                dirty = false;
                print_line("hexedit: file saved");
            }
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
        copy_cstr(root, sizeof root, _sys_cwd);
    } else if (strcmp(tok1, "-name") == 0) {
        s = next_token(s, tok2, sizeof tok2);
        if (!*tok2) {
            print_line("usage: find [PATH] [-name PATTERN]");
            return;
        }
        pattern = tok2;
        copy_cstr(root, sizeof root, _sys_cwd);
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
    copy_cstr(ctx.root, sizeof ctx.root, root);

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
                 "path: %.96s\ntype: directory\nentries: %d\nfile-bytes: %lu",
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
             "path: %.96s\ntype: file\nsize: %lu bytes\ncluster: %lu",
             abs, (unsigned long)f.size, (unsigned long)f.first_cluster);
    print_line(out);
}

static void app_tree_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    tree_walk_t *ctx = (tree_walk_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    char line[192];
    int pos = 0;
    int indent = ctx->depth * 2;
    if (indent > 48) indent = 48;
    while (pos < indent && pos + 1 < (int)sizeof line) line[pos++] = ' ';
    pos += snprintf(line + pos, sizeof line - (size_t)pos, "%s%s",
                    is_dir ? "|- " : "- ", name);
    if (is_dir) pos += snprintf(line + pos, sizeof line - (size_t)pos, "/");
    else pos += snprintf(line + pos, sizeof line - (size_t)pos, " (%lu)", (unsigned long)size);
    if (pos < 0) return;
    line[sizeof line - 1] = '\0';
    print_line(line);

    if (is_dir) {
        if (ctx->dir_count) (*ctx->dir_count)++;
        if (ctx->depth >= ctx->max_depth) return;

        char child[APP_TOKEN_MAX];
        app_join_path(ctx->path, name, child, sizeof child);

        tree_walk_t next = *ctx;
        copy_cstr(next.path, sizeof next.path, child);
        next.depth = ctx->depth + 1;
        (void)fat_ls(child, app_tree_cb, &next);
    } else if (ctx->file_count) {
        (*ctx->file_count)++;
    }
}

static void app_tree(const char *arg) {
    char tok1[APP_TOKEN_MAX] = {0};
    char tok2[APP_TOKEN_MAX] = {0};
    char path[APP_TOKEN_MAX] = {0};
    int max_depth = 8;
    const char *s = next_token(arg, tok1, sizeof tok1);

    if (*tok1) {
        if (strcmp(tok1, "-L") == 0) {
            char *endptr = NULL;
            s = next_token(s, tok2, sizeof tok2);
            long depth = strtol(tok2, &endptr, 10);
            if (!*tok2 || endptr == tok2 || depth < 1) {
                print_line("usage: tree [-L DEPTH] [PATH]");
                return;
            }
            max_depth = (int)depth;
            next_token(s, path, sizeof path);
        } else {
            copy_cstr(path, sizeof path, tok1);
        }
    }

    if (!*path) copy_cstr(path, sizeof path, _sys_cwd);

    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        int files = 0;
        int dirs = 1;
        print_line(abs);

        tree_walk_t ctx = {
            .depth = 1,
            .max_depth = max_depth,
            .file_count = &files,
            .dir_count = &dirs,
        };
        copy_cstr(ctx.path, sizeof ctx.path, abs);

        if (fat_ls(abs, app_tree_cb, &ctx) != FAT_OK) {
            print_line("tree: unable to list directory");
            return;
        }

        char out[96];
        snprintf(out, sizeof out, "%d directories, %d files", dirs, files);
        print_line(out);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) != FAT_OK) {
        print_line("tree: path not found");
        return;
    }

    char out[160];
    snprintf(out, sizeof out, "%.120s (%lu bytes)", abs, (unsigned long)f.size);
    print_line(out);
}

static void app_du_accum_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    du_walk_ctx_t *ctx = (du_walk_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    if (is_dir) {
        char child[APP_TOKEN_MAX];
        du_stat_t nested = {0};
        app_join_path(ctx->path, name, child, sizeof child);
        (void)app_du_path(child, &nested);
        ctx->stat.total_bytes += nested.total_bytes;
        ctx->stat.files += nested.files;
        ctx->stat.dirs += nested.dirs + 1;
    } else {
        ctx->stat.total_bytes += size;
        ctx->stat.files++;
    }
}

static uint32_t app_du_path(const char *path, du_stat_t *out) {
    du_stat_t zero = {0};
    if (!out) out = &zero;
    memset(out, 0, sizeof *out);

    if (fat_is_dir(path) != FAT_OK) {
        fat_file_t f;
        if (fat_open(path, &f) != FAT_OK) return 0;
        out->total_bytes = f.size;
        out->files = 1;
        return f.size;
    }

    du_walk_ctx_t ctx = {0};
    copy_cstr(ctx.path, sizeof ctx.path, path);
    if (fat_ls(path, app_du_accum_cb, &ctx) != FAT_OK) return 0;
    *out = ctx.stat;
    return ctx.stat.total_bytes;
}

static void app_du_list_cb(const char *name, uint32_t size, bool is_dir, void *opaque) {
    du_walk_ctx_t *ctx = (du_walk_ctx_t *)opaque;
    if (!ctx || !name || !*name) return;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

    uint32_t bytes = size;
    int files = 0;
    int dirs = 0;

    if (is_dir) {
        char child[APP_TOKEN_MAX];
        du_stat_t nested = {0};
        app_join_path(ctx->path, name, child, sizeof child);
        bytes = app_du_path(child, &nested);
        files = nested.files;
        dirs = nested.dirs + 1;
    } else {
        files = 1;
    }

    ctx->stat.total_bytes += bytes;
    ctx->stat.files += files;
    ctx->stat.dirs += dirs;

    char line[160];
    snprintf(line, sizeof line, "%8lu  %s%s",
             (unsigned long)bytes, name, is_dir ? "/" : "");
    print_line(line);
}

static void app_du(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) copy_cstr(path, sizeof path, _sys_cwd);

    char abs[APP_TOKEN_MAX];
    app_make_abs(path, abs, sizeof abs);

    if (fat_is_dir(abs) == FAT_OK) {
        du_walk_ctx_t ctx = {0};
        copy_cstr(ctx.path, sizeof ctx.path, abs);

        print_line("bytes     name");
        if (fat_ls(abs, app_du_list_cb, &ctx) != FAT_OK) {
            print_line("du: unable to list directory");
            return;
        }

        char out[128];
        snprintf(out, sizeof out, "total: %lu bytes | files: %d | dirs: %d",
                 (unsigned long)ctx.stat.total_bytes,
                 ctx.stat.files,
                 ctx.stat.dirs + 1);
        print_line(out);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) != FAT_OK) {
        print_line("du: path not found");
        return;
    }

    char out[160];
    snprintf(out, sizeof out, "%lu bytes  %.120s", (unsigned long)f.size, abs);
    print_line(out);
}

static void app_format_size(uint64_t bytes, char *out, size_t out_sz) {
    if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
        unsigned long long whole = bytes / (1024ULL * 1024ULL * 1024ULL);
        unsigned long long frac = (bytes % (1024ULL * 1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL * 1024ULL);
        snprintf(out, out_sz, "%llu.%llu GB", whole, frac);
    } else if (bytes >= (1024ULL * 1024ULL)) {
        unsigned long long whole = bytes / (1024ULL * 1024ULL);
        unsigned long long frac = (bytes % (1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL);
        snprintf(out, out_sz, "%llu.%llu MB", whole, frac);
    } else if (bytes >= 1024ULL) {
        unsigned long long whole = bytes / 1024ULL;
        unsigned long long frac = (bytes % 1024ULL) * 10ULL / 1024ULL;
        snprintf(out, out_sz, "%llu.%llu KB", whole, frac);
    } else {
        snprintf(out, out_sz, "%llu B", (unsigned long long)bytes);
    }
}

static void app_df(const char *arg) {
    (void)arg;
    fat_usage_t usage;
    fat_result_t r = fat_get_usage(&usage);
    if (r != FAT_OK) {
        print_line("df: filesystem not mounted");
        return;
    }

    char total[32], used[32], free_sp[32], out[160];
    app_format_size(usage.total_bytes, total, sizeof total);
    app_format_size(usage.used_bytes, used, sizeof used);
    app_format_size(usage.free_bytes, free_sp, sizeof free_sp);

    uint32_t used_pct = usage.total_bytes ? (uint32_t)((usage.used_bytes * 100ULL) / usage.total_bytes) : 0;
    snprintf(out, sizeof out, "filesystem: FAT%s", usage.fat32 ? "32" : "16");
    print_line(out);
    snprintf(out, sizeof out, "cluster size: %lu bytes", (unsigned long)(usage.bytes_per_sector * usage.sectors_per_cluster));
    print_line(out);
    snprintf(out, sizeof out, "total: %s", total);
    print_line(out);
    snprintf(out, sizeof out, "used:  %s (%lu%%)", used, (unsigned long)used_pct);
    print_line(out);
    snprintf(out, sizeof out, "free:  %s", free_sp);
    print_line(out);
    snprintf(out, sizeof out, "clusters: %lu total, %lu used, %lu free",
             (unsigned long)usage.cluster_count,
             (unsigned long)usage.used_clusters,
             (unsigned long)usage.free_clusters);
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

static void editor_status(editor_state_t *ed) {
    char out[160];
    const char *path = ed->path[0] ? ed->path : "(unnamed)";
    snprintf(out, sizeof out, "file: %.100s | lines: %d | %s",
             path,
             ed->line_count,
             ed->dirty ? "modified" : "saved");
    print_line(out);
}

static void editor_list(editor_state_t *ed, int start, int count) {
    if (ed->line_count == 0) {
        print_line("(empty buffer)");
        return;
    }

    if (start < 1) start = 1;
    if (count < 1) count = ed->line_count;
    int end = start + count - 1;
    if (end > ed->line_count) end = ed->line_count;

    for (int i = start - 1; i < end; i++) {
        char out[APP_TOKEN_MAX + 16];
        snprintf(out, sizeof out, "%3d  %s", i + 1, ed->lines[i]);
        print_line(out);
    }
}

static void editor_find(editor_state_t *ed, const char *needle) {
    const char *term = skip_ws(needle);
    if (!*term) {
        print_line("usage: find TEXT");
        return;
    }

    int hits = 0;
    for (int i = 0; i < ed->line_count; i++) {
        if (str_contains_ci(ed->lines[i], term)) {
            char out[APP_TOKEN_MAX + 16];
            snprintf(out, sizeof out, "%3d  %s", i + 1, ed->lines[i]);
            print_line(out);
            hits++;
        }
    }
    if (hits == 0) print_line("edit: no matches");
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
    copy_cstr(ed->lines[pos], EDIT_LINE_MAX, text ? text : "");
    ed->line_count++;
    ed->dirty = true;
    return true;
}

static bool editor_save(editor_state_t *ed) {
    char buf[EDIT_SAVE_MAX];
    size_t pos = 0;
    for (int i = 0; i < ed->line_count; i++) {
        size_t len = strlen(ed->lines[i]);
        if (pos + len + 2 >= sizeof buf) {
            print_line("edit: file exceeds editor save limit");
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
    snprintf(out, sizeof out, "saved %.64s (%lu bytes)", ed->path, (unsigned long)pos);
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
    print_line("Commands: status, list, append, ins, set, del, find, save, saveas, quit");
    editor_status(&g_editor);

    while (1) {
        char line[APP_TOKEN_MAX];
        char cmd[APP_TOKEN_MAX];
        if (app_read_line("edit> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        const char *s = next_token(line, cmd, sizeof cmd);
        s = skip_ws(s);

        if (!*cmd) continue;
        if (ci_eq(cmd, "help")) {
            print_line("status              show file path and state");
            print_line("list [S] [N]        show all or a range of lines");
            print_line("append              multi-line append mode");
            print_line("append TEXT         append one line");
            print_line("ins N TEXT          insert before line N");
            print_line("set N TEXT          replace line N");
            print_line("del N               delete line N");
            print_line("find TEXT           search for text in the buffer");
            print_line("save                write file to disk");
            print_line("saveas PATH         write to a different file");
            print_line("quit or quit!       leave editor");
        } else if (ci_eq(cmd, "status") || ci_eq(cmd, "info")) {
            editor_status(&g_editor);
        } else if (ci_eq(cmd, "list") || ci_eq(cmd, "view")) {
            char start_tok[16];
            char count_tok[16];
            int start = 1;
            int count = g_editor.line_count;
            if (*s) {
                s = next_token(s, start_tok, sizeof start_tok);
                next_token(s, count_tok, sizeof count_tok);
                if (*start_tok) start = atoi(start_tok);
                if (*count_tok) count = atoi(count_tok);
            }
            editor_list(&g_editor, start, count);
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
                copy_cstr(g_editor.lines[line_no - 1], EDIT_LINE_MAX, skip_ws(s));
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
        } else if (ci_eq(cmd, "find") || ci_eq(cmd, "grep")) {
            editor_find(&g_editor, s);
        } else if (ci_eq(cmd, "saveas")) {
            char newpath[APP_TOKEN_MAX];
            next_token(s, newpath, sizeof newpath);
            if (!*newpath) {
                print_line("usage: saveas PATH");
            } else {
                app_make_abs(newpath, g_editor.path, sizeof g_editor.path);
                editor_save(&g_editor);
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

    copy_cstr(br->items[br->count].name, sizeof br->items[br->count].name, name);
    br->items[br->count].is_dir = is_dir;
    br->items[br->count].size = size;
    br->count++;
}

static int browser_entry_cmp(const browser_entry_t *a, const browser_entry_t *b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return str_casecmp_local(a->name, b->name);
}

static void browser_sort(browser_state_t *br) {
    for (int i = 1; i < br->count; i++) {
        browser_entry_t key = br->items[i];
        int j = i - 1;
        while (j >= 0 && browser_entry_cmp(&key, &br->items[j]) < 0) {
            br->items[j + 1] = br->items[j];
            j--;
        }
        br->items[j + 1] = key;
    }
}

static void browser_refresh(browser_state_t *br) {
    br->count = 0;
    if (fat_ls(br->cwd, browser_collect_cb, br) == FAT_OK) browser_sort(br);
}

static void browser_render(browser_state_t *br, int sel) {
    sys_clear();
    sys_print("Mellivora browser\n");
    sys_print(br->cwd);
    sys_print("\n\n");
    sys_print("j/k move, Enter open, e edit, x hex, n new file, m mkdir, d delete, u up, r refresh, q quit\n\n");

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
    else copy_cstr(br.cwd, sizeof br.cwd, _sys_cwd);

    if (fat_is_dir(br.cwd) != FAT_OK) {
        print_line("browse: path is not a directory");
        return;
    }

    copy_cstr(_sys_cwd, sizeof _sys_cwd, br.cwd);
    browser_refresh(&br);

    int sel = 0;
    while (1) {
        int base = (strcmp(br.cwd, "/") != 0) ? 1 : 0;
        int total = br.count + base;
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
        if (ch == 'n' || ch == 'N') {
            char name[APP_TOKEN_MAX];
            sys_print("New file: ");
            if (app_read_line("", name, sizeof name) >= 0) {
                rtrim_in_place(name);
                if (*name) {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_fwrite(name, "", 0) < 0) print_line("browse: create failed");
                    else {
                        browser_refresh(&br);
                        sel = total > 0 ? total - 1 : 0;
                    }
                }
            }
            continue;
        }
        if (ch == 'm' || ch == 'M') {
            char name[APP_TOKEN_MAX];
            sys_print("New directory: ");
            if (app_read_line("", name, sizeof name) >= 0) {
                rtrim_in_place(name);
                if (*name) {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_mkdir(name) < 0) print_line("browse: mkdir failed");
                    else browser_refresh(&br);
                }
            }
            continue;
        }
        if (ch == 'd' || ch == 'D') {
            if (sel >= base && total > 0) {
                browser_entry_t *ent = &br.items[sel - base];
                char prompt[96];
                snprintf(prompt, sizeof prompt, "Delete %s? [y/N] ", ent->name);
                sys_print(prompt);
                int ok = sys_getchar();
                sys_putchar('\n');
                if (ok == 'y' || ok == 'Y') {
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    if (sys_delete(ent->name) < 0) print_line("browse: delete failed");
                    else {
                        browser_refresh(&br);
                        if (sel > 0 && sel >= br.count + base) sel--;
                    }
                }
            }
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

        if (ch == 'e' || ch == 'E' || ch == 'x' || ch == 'X') {
            if (sel >= base) {
                browser_entry_t *ent = &br.items[sel - base];
                if (!ent->is_dir) {
                    char abs[APP_TOKEN_MAX];
                    strncpy(_sys_cwd, br.cwd, sizeof _sys_cwd - 1);
                    _sys_cwd[sizeof _sys_cwd - 1] = '\0';
                    app_make_abs(ent->name, abs, sizeof abs);
                    if (ch == 'x' || ch == 'X') app_hexedit(abs);
                    else app_edit(abs);
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

static int todo_load(todo_item_t *items, int max_items) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(TODO_FILE, buf, APP_READ_MAX);
    if (n < 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || count >= max_items) continue;

        bool done = false;
        if (strncmp(s, "[x]", 3) == 0 || strncmp(s, "[X]", 3) == 0) {
            done = true;
            s = (char *)skip_ws(s + 3);
        } else if (strncmp(s, "[ ]", 3) == 0) {
            s = (char *)skip_ws(s + 3);
        }

        items[count].done = done;
        copy_cstr(items[count].text, sizeof items[count].text, s);
        count++;
    }
    return count;
}

static bool todo_save(const todo_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "[%c] %s\n",
                             items[i].done ? 'x' : ' ', items[i].text);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("todo: file too large to save");
            return false;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite(TODO_FILE, buf, (uint32_t)pos) < 0) {
        print_line("todo: save failed");
        return false;
    }
    return true;
}

static void todo_show(const todo_item_t *items, int count) {
    if (count == 0) {
        print_line("todo: no tasks yet");
        return;
    }

    int open_count = 0;
    for (int i = 0; i < count; i++) {
        char out[160];
        snprintf(out, sizeof out, "%2d. [%c] %.100s", i + 1, items[i].done ? 'x' : ' ', items[i].text);
        print_line(out);
        if (!items[i].done) open_count++;
    }

    char out[96];
    snprintf(out, sizeof out, "%d total, %d open", count, open_count);
    print_line(out);
}

static void app_todo(const char *arg) {
    todo_item_t items[TODO_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = todo_load(items, TODO_ITEMS_MAX);

    if (!*cmd || ci_eq(cmd, "list")) {
        todo_show(items, count);
        print_line("Usage: todo add TEXT | done N | undo N | del N | next | purge | edit");
        return;
    }

    if (ci_eq(cmd, "add")) {
        const char *text = skip_ws(s);
        if (!*text) {
            print_line("usage: todo add TEXT");
            return;
        }
        if (count >= TODO_ITEMS_MAX) {
            print_line("todo: task list is full");
            return;
        }
        items[count].done = false;
        copy_cstr(items[count].text, sizeof items[count].text, text);
        if (todo_save(items, count + 1)) print_line("todo: task added");
        return;
    }

    if (ci_eq(cmd, "next")) {
        for (int i = 0; i < count; i++) {
            if (!items[i].done) {
                char out[160];
                snprintf(out, sizeof out, "next: %d. %.100s", i + 1, items[i].text);
                print_line(out);
                return;
            }
        }
        print_line("todo: nothing pending");
        return;
    }

    if (ci_eq(cmd, "edit")) {
        app_edit(TODO_FILE);
        return;
    }

    if (ci_eq(cmd, "purge") || ci_eq(cmd, "clean")) {
        todo_item_t kept[TODO_ITEMS_MAX];
        int kept_count = 0;
        for (int i = 0; i < count; i++) {
            if (!items[i].done && kept_count < TODO_ITEMS_MAX) kept[kept_count++] = items[i];
        }
        if (todo_save(kept, kept_count)) print_line("todo: completed items removed");
        return;
    }

    if (ci_eq(cmd, "done") || ci_eq(cmd, "undo") || ci_eq(cmd, "del") || ci_eq(cmd, "rm")) {
        char numtok[16];
        next_token(s, numtok, sizeof numtok);
        int idx = atoi(numtok) - 1;
        if (idx < 0 || idx >= count) {
            print_line("usage: todo done N | undo N | del N");
            return;
        }

        if (ci_eq(cmd, "done")) {
            items[idx].done = true;
            if (todo_save(items, count)) print_line("todo: task completed");
        } else if (ci_eq(cmd, "undo")) {
            items[idx].done = false;
            if (todo_save(items, count)) print_line("todo: task reopened");
        } else {
            for (int i = idx; i + 1 < count; i++) items[i] = items[i + 1];
            if (todo_save(items, count - 1)) print_line("todo: task deleted");
        }
        return;
    }

    print_line("usage: todo [list|add|done|undo|del|next|purge|edit]");
}

static void app_notes(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) copy_cstr(path, sizeof path, *g_settings.notes_path ? g_settings.notes_path : "/NOTES.TXT");

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
        if (!*path && def) copy_cstr(path, sizeof path, def);
        if (sys_fwrite(path, body, (uint32_t)strlen(body)) < 0) {
            print_line("samples: write failed");
            return;
        }
        char out[128];
        snprintf(out, sizeof out, "saved sample to %.80s (%.16s)", path, kind ? kind : "text");
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

static const char *month_name_local(int month) {
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return "Unknown";
    return months[month - 1];
}

static int build_month_local(void) {
    static const char *abbr[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(__DATE__, abbr[i], 3) == 0) return i + 1;
    }
    return 1;
}

static int build_year_local(void) {
    int year = atoi(__DATE__ + 7);
    return year > 0 ? year : 1970;
}

static int build_day_local(void) {
    int day = atoi(__DATE__ + 4);
    return day > 0 ? day : 1;
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

static void planner_today_string(char *out, size_t out_sz) {
    int year = build_year_local();
    int month = build_month_local();
    int day = build_day_local();
    if (year < 0) year = 0;
    if (year > 9999) year %= 10000;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    snprintf(out, out_sz, "%04d-%02d-%02d", year, month, day);
}

static bool planner_valid_date(const char *date) {
    if (!date || strlen(date) != 10 || date[4] != '-' || date[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)date[i])) return false;
    }
    int year = atoi(date);
    int month = atoi(date + 5);
    int day = atoi(date + 8);
    if (year < 1 || month < 1 || month > 12) return false;
    return day >= 1 && day <= days_in_month_local(month, year);
}

static int planner_load(planner_item_t *items, int max_items) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(PLANNER_FILE, buf, APP_READ_MAX);
    if (n < 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#' || count >= max_items) continue;

        char *sep = strchr(s, '|');
        if (!sep) continue;
        *sep++ = '\0';
        rtrim_in_place(s);
        sep = (char *)skip_ws(sep);
        if (!planner_valid_date(s) || !*sep) continue;

        copy_cstr(items[count].date, sizeof items[count].date, s);
        copy_cstr(items[count].text, sizeof items[count].text, sep);
        count++;
    }
    return count;
}

static void planner_sort(planner_item_t *items, int count) {
    for (int i = 1; i < count; i++) {
        planner_item_t key = items[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = strcmp(key.date, items[j].date);
            if (cmp == 0) cmp = str_casecmp_local(key.text, items[j].text);
            if (cmp >= 0) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

static bool planner_save(planner_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    planner_sort(items, count);

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s\n", items[i].date, items[i].text);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("planner: file too large to save");
            return false;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite(PLANNER_FILE, buf, (uint32_t)pos) < 0) {
        print_line("planner: save failed");
        return false;
    }
    return true;
}

static void planner_show(const planner_item_t *items, int count, const char *prefix) {
    int shown = 0;
    for (int i = 0; i < count; i++) {
        if (prefix && *prefix && strncmp(items[i].date, prefix, strlen(prefix)) != 0) continue;
        char out[160];
        snprintf(out, sizeof out, "%2d. %s  %.96s", i + 1, items[i].date, items[i].text);
        print_line(out);
        shown++;
    }
    if (shown == 0) print_line("planner: no matching entries");
}

static void app_planner(const char *arg) {
    planner_item_t items[PLANNER_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    char today[16];
    planner_today_string(today, sizeof today);
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = planner_load(items, PLANNER_ITEMS_MAX);
    planner_sort(items, count);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "all")) {
        planner_show(items, count, "");
        print_line("Usage: planner add DATE TEXT | today | month YYYY-MM | del N | next | edit");
        return;
    }

    if (ci_eq(cmd, "today")) {
        planner_show(items, count, today);
        return;
    }

    if (ci_eq(cmd, "month")) {
        next_token(s, tok, sizeof tok);
        if (!*tok || strlen(tok) != 7 || tok[4] != '-') {
            print_line("usage: planner month YYYY-MM");
            return;
        }
        planner_show(items, count, tok);
        return;
    }

    if (ci_eq(cmd, "next")) {
        for (int i = 0; i < count; i++) {
            if (strcmp(items[i].date, today) >= 0) {
                char out[160];
                snprintf(out, sizeof out, "next: %.10s  %.96s", items[i].date, items[i].text);
                print_line(out);
                return;
            }
        }
        if (count > 0) {
            char out[160];
            snprintf(out, sizeof out, "latest: %.10s  %.96s", items[count - 1].date, items[count - 1].text);
            print_line(out);
        } else {
            print_line("planner: no entries yet");
        }
        return;
    }

    if (ci_eq(cmd, "edit")) {
        app_edit(PLANNER_FILE);
        return;
    }

    if (ci_eq(cmd, "add") || ci_eq(cmd, "note")) {
        char date[16];
        const char *text = NULL;

        if (ci_eq(cmd, "note")) {
            copy_cstr(date, sizeof date, today);
            text = skip_ws(s);
        } else {
            s = next_token(s, date, sizeof date);
            text = skip_ws(s);
        }

        if (!planner_valid_date(date) || !text || !*text) {
            print_line("usage: planner add YYYY-MM-DD TEXT");
            return;
        }
        if (count >= PLANNER_ITEMS_MAX) {
            print_line("planner: schedule is full");
            return;
        }

        copy_cstr(items[count].date, sizeof items[count].date, date);
        copy_cstr(items[count].text, sizeof items[count].text, text);
        if (planner_save(items, count + 1)) print_line("planner: entry added");
        return;
    }

    if (ci_eq(cmd, "del") || ci_eq(cmd, "rm")) {
        next_token(s, tok, sizeof tok);
        int idx = atoi(tok) - 1;
        if (idx < 0 || idx >= count) {
            print_line("usage: planner del N");
            return;
        }
        for (int i = idx; i + 1 < count; i++) items[i] = items[i + 1];
        if (planner_save(items, count - 1)) print_line("planner: entry deleted");
        return;
    }

    print_line("usage: planner [list|add|today|month|next|del|edit]");
}

static int bookmarks_load(bookmark_item_t *items, int max_items) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(BOOKMARKS_FILE, buf, APP_READ_MAX);
    if (n < 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#' || count >= max_items) continue;

        char *sep = strchr(s, '|');
        if (!sep) continue;
        *sep++ = '\0';
        rtrim_in_place(s);
        sep = (char *)skip_ws(sep);
        if (!*s || !*sep) continue;

        copy_cstr(items[count].label, sizeof items[count].label, s);
        copy_cstr(items[count].target, sizeof items[count].target, sep);
        count++;
    }
    return count;
}

static bool bookmarks_save(bookmark_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s\n", items[i].label, items[i].target);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("bookmarks: file too large to save");
            return false;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite(BOOKMARKS_FILE, buf, (uint32_t)pos) < 0) {
        print_line("bookmarks: save failed");
        return false;
    }
    return true;
}

static void bookmarks_show(const bookmark_item_t *items, int count) {
    if (count == 0) {
        print_line("bookmarks: no saved entries");
        return;
    }
    for (int i = 0; i < count; i++) {
        char out[160];
        snprintf(out, sizeof out, "%2d. %-16.16s -> %.96s", i + 1, items[i].label, items[i].target);
        print_line(out);
    }
}

static int bookmarks_find(const bookmark_item_t *items, int count, const char *key) {
    if (!key || !*key) return -1;
    for (int i = 0; i < count; i++) {
        if (str_casecmp_local(items[i].label, key) == 0) return i;
    }
    return -1;
}

static void bookmarks_open_target(const char *target) {
    char cmdline[APP_TOKEN_MAX];
    char cmd[APP_TOKEN_MAX];
    copy_cstr(cmdline, sizeof cmdline, target ? target : "");
    const char *rest = next_token(cmdline, cmd, sizeof cmd);

    if (*cmd && app_dispatch_named(cmd, rest)) return;

    char abs[APP_TOKEN_MAX];
    app_make_abs(target, abs, sizeof abs);
    if (fat_is_dir(abs) == FAT_OK) {
        app_browse(abs);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) == FAT_OK) {
        app_edit(abs);
        return;
    }

    print_line("bookmarks: target not found");
}

static void app_bookmarks(const char *arg) {
    bookmark_item_t items[BOOKMARKS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = bookmarks_load(items, BOOKMARKS_MAX);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "show")) {
        bookmarks_show(items, count);
        print_line("Usage: bookmarks add NAME TARGET | open N|NAME | del N|NAME | edit");
        return;
    }

    if (ci_eq(cmd, "add")) {
        char label[BOOKMARK_LABEL_MAX];
        s = next_token(s, label, sizeof label);
        const char *target = skip_ws(s);

        if (!*label) {
            print_line("usage: bookmarks add NAME TARGET");
            return;
        }
        if (!*target) target = _sys_cwd;

        int idx = bookmarks_find(items, count, label);
        if (idx < 0) {
            if (count >= BOOKMARKS_MAX) {
                print_line("bookmarks: list is full");
                return;
            }
            idx = count++;
        }
        copy_cstr(items[idx].label, sizeof items[idx].label, label);
        copy_cstr(items[idx].target, sizeof items[idx].target, target);
        if (bookmarks_save(items, count)) print_line("bookmarks: saved");
        return;
    }

    if (ci_eq(cmd, "open") || ci_eq(cmd, "go") || ci_eq(cmd, "run")) {
        next_token(s, tok, sizeof tok);
        if (!*tok) {
            print_line("usage: bookmarks open N|NAME");
            return;
        }
        int idx = atoi(tok) - 1;
        if (idx < 0 || idx >= count) idx = bookmarks_find(items, count, tok);
        if (idx < 0 || idx >= count) {
            print_line("bookmarks: entry not found");
            return;
        }
        bookmarks_open_target(items[idx].target);
        return;
    }

    if (ci_eq(cmd, "del") || ci_eq(cmd, "rm")) {
        next_token(s, tok, sizeof tok);
        if (!*tok) {
            print_line("usage: bookmarks del N|NAME");
            return;
        }
        int idx = atoi(tok) - 1;
        if (idx < 0 || idx >= count) idx = bookmarks_find(items, count, tok);
        if (idx < 0 || idx >= count) {
            print_line("bookmarks: entry not found");
            return;
        }
        for (int i = idx; i + 1 < count; i++) items[i] = items[i + 1];
        if (bookmarks_save(items, count - 1)) print_line("bookmarks: removed");
        return;
    }

    if (ci_eq(cmd, "edit")) {
        app_edit(BOOKMARKS_FILE);
        return;
    }

    print_line("usage: bookmarks [list|add|open|del|edit]");
}

static int journal_load(journal_item_t *items, int max_items) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(JOURNAL_FILE, buf, APP_READ_MAX);
    if (n < 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#' || count >= max_items) continue;

        char *sep = strchr(s, '|');
        if (!sep) continue;
        *sep++ = '\0';
        rtrim_in_place(s);
        sep = (char *)skip_ws(sep);
        if (!planner_valid_date(s) || !*sep) continue;

        copy_cstr(items[count].date, sizeof items[count].date, s);
        copy_cstr(items[count].text, sizeof items[count].text, sep);
        count++;
    }
    return count;
}

static void journal_sort(journal_item_t *items, int count) {
    for (int i = 1; i < count; i++) {
        journal_item_t key = items[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = strcmp(key.date, items[j].date);
            if (cmp == 0) cmp = str_casecmp_local(key.text, items[j].text);
            if (cmp >= 0) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

static bool journal_save(journal_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    journal_sort(items, count);

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s\n", items[i].date, items[i].text);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("journal: file too large to save");
            return false;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite(JOURNAL_FILE, buf, (uint32_t)pos) < 0) {
        print_line("journal: save failed");
        return false;
    }
    return true;
}

static void journal_show(const journal_item_t *items, int count, const char *prefix) {
    int shown = 0;
    for (int i = count - 1; i >= 0; i--) {
        if (prefix && *prefix && strncmp(items[i].date, prefix, strlen(prefix)) != 0) continue;
        char out[160];
        snprintf(out, sizeof out, "%2d. %.10s  %.96s", i + 1, items[i].date, items[i].text);
        print_line(out);
        shown++;
    }
    if (shown == 0) print_line("journal: no matching entries");
}

static void app_journal(const char *arg) {
    journal_item_t items[JOURNAL_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    char today[16];
    planner_today_string(today, sizeof today);
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = journal_load(items, JOURNAL_ITEMS_MAX);
    journal_sort(items, count);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "show")) {
        journal_show(items, count, "");
        print_line("Usage: journal add [DATE] TEXT | today | month YYYY-MM | edit");
        return;
    }

    if (ci_eq(cmd, "today")) {
        journal_show(items, count, today);
        return;
    }

    if (ci_eq(cmd, "month")) {
        next_token(s, tok, sizeof tok);
        if (!*tok || strlen(tok) != 7 || tok[4] != '-') {
            print_line("usage: journal month YYYY-MM");
            return;
        }
        journal_show(items, count, tok);
        return;
    }

    if (ci_eq(cmd, "edit")) {
        app_edit(JOURNAL_FILE);
        return;
    }

    if (ci_eq(cmd, "add") || ci_eq(cmd, "note")) {
        char date[16];
        char maybe_date[16];
        const char *text = NULL;

        const char *after_first = next_token(s, maybe_date, sizeof maybe_date);
        if (planner_valid_date(maybe_date)) {
            copy_cstr(date, sizeof date, maybe_date);
            text = skip_ws(after_first);
        } else {
            copy_cstr(date, sizeof date, today);
            text = skip_ws(s);
        }

        if (!text || !*text) {
            print_line("usage: journal add [YYYY-MM-DD] TEXT");
            return;
        }
        if (count >= JOURNAL_ITEMS_MAX) {
            print_line("journal: log is full");
            return;
        }

        copy_cstr(items[count].date, sizeof items[count].date, date);
        copy_cstr(items[count].text, sizeof items[count].text, text);
        if (journal_save(items, count + 1)) print_line("journal: entry added");
        return;
    }

    print_line("usage: journal [list|add|today|month|edit]");
}

static int habits_load(habit_item_t *items, int max_items) {
    char buf[APP_READ_MAX + 1];
    int n = sys_fread(HABITS_FILE, buf, APP_READ_MAX);
    if (n < 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *s = (char *)skip_ws(line);
        if (!*s || *s == '#' || count >= max_items) continue;

        char *sep1 = strchr(s, '|');
        if (!sep1) continue;
        *sep1++ = '\0';
        char *sep2 = strchr(sep1, '|');
        if (!sep2) continue;
        *sep2++ = '\0';

        copy_cstr(items[count].name, sizeof items[count].name, s);
        items[count].count = atoi(sep1);
        copy_cstr(items[count].last_date, sizeof items[count].last_date, sep2);
        count++;
    }
    return count;
}

static bool habits_save(habit_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%d|%s\n",
                             items[i].name, items[i].count,
                             *items[i].last_date ? items[i].last_date : "none");
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("habits: file too large to save");
            return false;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite(HABITS_FILE, buf, (uint32_t)pos) < 0) {
        print_line("habits: save failed");
        return false;
    }
    return true;
}

static int habits_find(const habit_item_t *items, int count, const char *key) {
    if (!key || !*key) return -1;
    for (int i = 0; i < count; i++) {
        if (str_casecmp_local(items[i].name, key) == 0) return i;
    }
    return -1;
}

static void habits_show(const habit_item_t *items, int count) {
    if (count == 0) {
        print_line("habits: no habits defined");
        return;
    }
    for (int i = 0; i < count; i++) {
        char out[160];
        snprintf(out, sizeof out, "%2d. %-20.20s count=%d last=%.10s",
                 i + 1, items[i].name, items[i].count,
                 *items[i].last_date ? items[i].last_date : "never");
        print_line(out);
    }
}

static void app_habits(const char *arg) {
    habit_item_t items[HABITS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    char today[16];
    planner_today_string(today, sizeof today);
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = habits_load(items, HABITS_MAX);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "show")) {
        habits_show(items, count);
        print_line("Usage: habits add NAME | done N|NAME [DATE] | reset N|NAME | edit");
        return;
    }

    if (ci_eq(cmd, "edit")) {
        app_edit(HABITS_FILE);
        return;
    }

    if (ci_eq(cmd, "add")) {
        const char *name = skip_ws(s);
        if (!*name) {
            print_line("usage: habits add NAME");
            return;
        }
        if (count >= HABITS_MAX) {
            print_line("habits: list is full");
            return;
        }
        if (habits_find(items, count, name) >= 0) {
            print_line("habits: already exists");
            return;
        }
        copy_cstr(items[count].name, sizeof items[count].name, name);
        items[count].count = 0;
        items[count].last_date[0] = '\0';
        if (habits_save(items, count + 1)) print_line("habits: added");
        return;
    }

    if (ci_eq(cmd, "done") || ci_eq(cmd, "check") || ci_eq(cmd, "mark")) {
        char when[16];
        const char *rest = next_token(s, tok, sizeof tok);
        next_token(rest, when, sizeof when);
        if (!*tok) {
            print_line("usage: habits done N|NAME [DATE]");
            return;
        }
        int idx = atoi(tok) - 1;
        if (idx < 0 || idx >= count) idx = habits_find(items, count, tok);
        if (idx < 0 || idx >= count) {
            print_line("habits: entry not found");
            return;
        }
        const char *date = planner_valid_date(when) ? when : today;
        items[idx].count++;
        copy_cstr(items[idx].last_date, sizeof items[idx].last_date, date);
        if (habits_save(items, count)) print_line("habits: completion recorded");
        return;
    }

    if (ci_eq(cmd, "reset") || ci_eq(cmd, "clear")) {
        next_token(s, tok, sizeof tok);
        if (!*tok) {
            print_line("usage: habits reset N|NAME");
            return;
        }
        int idx = atoi(tok) - 1;
        if (idx < 0 || idx >= count) idx = habits_find(items, count, tok);
        if (idx < 0 || idx >= count) {
            print_line("habits: entry not found");
            return;
        }
        items[idx].count = 0;
        items[idx].last_date[0] = '\0';
        if (habits_save(items, count)) print_line("habits: reset");
        return;
    }

    print_line("usage: habits [list|add|done|reset|edit]");
}

static uint32_t games_rand_u32(void) {
    static uint32_t state = 0xA5A5F00Du;
    state ^= (uint32_t)sys_time_ms() + 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void app_dice(const char *arg) {
    char count_tok[16];
    char sides_tok[16];
    const char *s = next_token(arg, count_tok, sizeof count_tok);
    next_token(s, sides_tok, sizeof sides_tok);

    int count = *count_tok ? atoi(count_tok) : 1;
    int sides = *sides_tok ? atoi(sides_tok) : 6;
    if (count < 1 || count > 20 || sides < 2 || sides > 1000) {
        print_line("usage: dice [COUNT] [SIDES]");
        return;
    }

    int total = 0;
    for (int i = 0; i < count; i++) {
        int roll = (int)(games_rand_u32() % (uint32_t)sides) + 1;
        char out[64];
        snprintf(out, sizeof out, "roll %d: %d", i + 1, roll);
        print_line(out);
        total += roll;
    }
    if (count > 1) {
        char out[64];
        snprintf(out, sizeof out, "total: %d", total);
        print_line(out);
    }
}

static void app_coin(const char *arg) {
    char count_tok[16];
    next_token(arg, count_tok, sizeof count_tok);
    int count = *count_tok ? atoi(count_tok) : 1;
    if (count < 1 || count > 32) {
        print_line("usage: coin [COUNT]");
        return;
    }

    int heads = 0;
    for (int i = 0; i < count; i++) {
        bool is_heads = (games_rand_u32() & 1u) != 0;
        if (is_heads) heads++;
        print_line(is_heads ? "HEADS" : "TAILS");
    }
    if (count > 1) {
        char out[64];
        snprintf(out, sizeof out, "heads: %d  tails: %d", heads, count - heads);
        print_line(out);
    }
}

static void app_guess(const char *arg) {
    (void)arg;
    int target = (int)(games_rand_u32() % 100u) + 1;
    int turns = 0;
    print_line("Guess a number from 1 to 100. Type q to quit.");

    while (1) {
        char line[32];
        if (app_read_line("guess> ", line, sizeof line) < 0) break;
        rtrim_in_place(line);
        if (ci_eq(line, "q") || ci_eq(line, "quit") || ci_eq(line, "exit")) break;

        int guess = atoi(line);
        if (guess < 1 || guess > 100) {
            print_line("Enter a number from 1 to 100.");
            continue;
        }

        turns++;
        if (guess < target) print_line("Too low");
        else if (guess > target) print_line("Too high");
        else {
            char out[64];
            snprintf(out, sizeof out, "Correct in %d turns", turns);
            print_line(out);
            return;
        }
    }
}

static void snake_spawn_food(int *food_x, int *food_y, const int *sx, const int *sy, int len) {
    while (1) {
        int x = (int)(games_rand_u32() % (uint32_t)SNAKE_W);
        int y = (int)(games_rand_u32() % (uint32_t)SNAKE_H);
        bool clash = false;
        for (int i = 0; i < len; i++) {
            if (sx[i] == x && sy[i] == y) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            *food_x = x;
            *food_y = y;
            return;
        }
    }
}

static void snake_render(const int *sx, const int *sy, int len, int food_x, int food_y, int score) {
    sys_clear();
    char out[64];
    snprintf(out, sizeof out, "Snake score: %d", score);
    print_line(out);
    print_line("Use w/a/s/d or h/j/k/l to move, Enter to advance, q to quit");

    for (int y = -1; y <= SNAKE_H; y++) {
        char line[64];
        int pos = 0;
        for (int x = -1; x <= SNAKE_W; x++) {
            char ch = ' ';
            if (y < 0 || y >= SNAKE_H || x < 0 || x >= SNAKE_W) {
                ch = '#';
            } else if (x == food_x && y == food_y) {
                ch = '*';
            }
            for (int i = 0; i < len && x >= 0 && y >= 0 && x < SNAKE_W && y < SNAKE_H; i++) {
                if (sx[i] == x && sy[i] == y) {
                    ch = (i == 0) ? '@' : 'o';
                    break;
                }
            }
            if (pos + 1 < (int)sizeof line) line[pos++] = ch;
        }
        line[pos] = '\0';
        print_line(line);
    }
}

static void app_snake(const char *arg) {
    (void)arg;
    int sx[SNAKE_MAX_CELLS];
    int sy[SNAKE_MAX_CELLS];
    int len = 3;
    int dx = 1, dy = 0;
    int food_x = 0, food_y = 0;
    int score = 0;

    sx[0] = 4; sy[0] = 4;
    sx[1] = 3; sy[1] = 4;
    sx[2] = 2; sy[2] = 4;
    snake_spawn_food(&food_x, &food_y, sx, sy, len);

    while (1) {
        snake_render(sx, sy, len, food_x, food_y, score);
        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;

        if ((ch == 'w' || ch == 'k' || ch == 'K') && dy != 1) { dx = 0; dy = -1; }
        else if ((ch == 's' || ch == 'j' || ch == 'J') && dy != -1) { dx = 0; dy = 1; }
        else if ((ch == 'a' || ch == 'h' || ch == 'H') && dx != 1) { dx = -1; dy = 0; }
        else if ((ch == 'd' || ch == 'l' || ch == 'L') && dx != -1) { dx = 1; dy = 0; }

        int nx = sx[0] + dx;
        int ny = sy[0] + dy;
        if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) {
            print_line("snake: wall hit; game over");
            (void)sys_getchar();
            break;
        }
        for (int i = 0; i < len; i++) {
            if (sx[i] == nx && sy[i] == ny) {
                print_line("snake: self collision; game over");
                (void)sys_getchar();
                sys_clear();
                return;
            }
        }

        bool grow = (nx == food_x && ny == food_y);
        int new_len = len + (grow && len < SNAKE_MAX_CELLS ? 1 : 0);
        for (int i = new_len - 1; i > 0; i--) {
            sx[i] = sx[i - 1];
            sy[i] = sy[i - 1];
        }
        sx[0] = nx;
        sy[0] = ny;
        len = new_len;

        if (grow) {
            score++;
            snake_spawn_food(&food_x, &food_y, sx, sy, len);
        }
    }
    sys_clear();
}

static void sprite_render(const uint8_t *pixels, int cur_x, int cur_y, const char *path) {
    sys_clear();
    sys_print("Sprite editor\n");
    if (path && *path) {
        sys_print(path);
        sys_putchar('\n');
    }
    sys_print("w/a/s/d move, x toggle, c clear, p save, l load, q quit\n\n");

    for (int y = 0; y < SPRITE_SIDE; y++) {
        char line[64];
        int pos = 0;
        for (int x = 0; x < SPRITE_SIDE; x++) {
            bool on = pixels[y * SPRITE_SIDE + x] != 0;
            char ch = on ? '#' : '.';
            if (x == cur_x && y == cur_y) ch = on ? '*' : '@';
            line[pos++] = ch;
        }
        line[pos] = '\0';
        print_line(line);
    }
}

static void app_sprite(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) copy_cstr(path, sizeof path, SPRITE_FILE);

    char target[APP_TOKEN_MAX];
    app_make_abs(path, target, sizeof target);

    static uint8_t pixels[SPRITE_SIDE * SPRITE_SIDE];
    int cur_x = 0, cur_y = 0;

    while (1) {
        sprite_render(pixels, cur_x, cur_y, target);
        int ch = sys_getchar();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if ((ch == 'w' || ch == 'W') && cur_y > 0) cur_y--;
        else if ((ch == 's' || ch == 'S') && cur_y < SPRITE_SIDE - 1) cur_y++;
        else if ((ch == 'a' || ch == 'A') && cur_x > 0) cur_x--;
        else if ((ch == 'd' || ch == 'D') && cur_x < SPRITE_SIDE - 1) cur_x++;
        else if (ch == 'x' || ch == 'X' || ch == ' ') {
            size_t idx = (size_t)cur_y * SPRITE_SIDE + (size_t)cur_x;
            pixels[idx] ^= 1u;
        } else if (ch == 'c' || ch == 'C') {
            memset(pixels, 0, sizeof pixels);
            print_line("sprite: cleared");
        } else if (ch == 'p' || ch == 'P') {
            char buf[(SPRITE_SIDE + 1) * SPRITE_SIDE + 1];
            size_t pos = 0;
            for (int y = 0; y < SPRITE_SIDE; y++) {
                for (int x = 0; x < SPRITE_SIDE; x++) {
                    buf[pos++] = pixels[y * SPRITE_SIDE + x] ? '#' : '.';
                }
                buf[pos++] = '\n';
            }
            buf[pos] = '\0';
            if (sys_fwrite(target, buf, (uint32_t)pos) < 0) print_line("sprite: save failed");
            else print_line("sprite: saved");
        } else if (ch == 'l' || ch == 'L') {
            char buf[(SPRITE_SIDE + 1) * SPRITE_SIDE + 1];
            int n = sys_fread(target, buf, (uint32_t)(sizeof buf - 1));
            if (n < 0) {
                print_line("sprite: load failed");
            } else {
                buf[n] = '\0';
                int idx = 0;
                for (int y = 0; y < SPRITE_SIDE; y++) {
                    for (int x = 0; x < SPRITE_SIDE; x++) {
                        while (idx < n && (buf[idx] == '\n' || buf[idx] == '\r')) idx++;
                        pixels[y * SPRITE_SIDE + x] = (idx < n && buf[idx] == '#') ? 1u : 0u;
                        if (idx < n) idx++;
                    }
                }
                print_line("sprite: loaded");
            }
        }
    }
    sys_clear();
}

static void app_terminal(const char *arg) {
    (void)arg;
    sys_clear();
    sys_print("Raw terminal mode\n\n");
    sys_print("Everything typed is echoed directly. Press Ctrl-X or Ctrl-C to exit.\n\n");
    while (1) {
        int ch = sys_getchar();
        if (ch == 0x18 || ch == 0x03) break;
        sys_putchar((char)ch);
    }
    sys_clear();
}

static void app_games(const char *arg) {
    char cmd[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);

    if (!*cmd) {
        print_line("Games pack");
        print_line("  1 dice rolls");
        print_line("  2 coin flips");
        print_line("  3 guess-the-number");
        print_line("  4 snake");
        print_line("  q return");
        int ch = sys_getchar();
        sys_putchar('\n');
        if (ch == '1') app_dice("");
        else if (ch == '2') app_coin("");
        else if (ch == '3') app_guess("");
        else if (ch == '4') app_snake("");
        return;
    }

    if (ci_eq(cmd, "dice")) app_dice(s);
    else if (ci_eq(cmd, "coin")) app_coin(s);
    else if (ci_eq(cmd, "guess")) app_guess(s);
    else if (ci_eq(cmd, "snake")) app_snake(s);
    else print_line("usage: games [dice|coin|guess|snake]");
}

static void app_cal(const char *arg) {
    char m_tok[16];
    char y_tok[16];
    const char *s = next_token(arg, m_tok, sizeof m_tok);
    next_token(s, y_tok, sizeof y_tok);

    int month = *m_tok ? atoi(m_tok) : build_month_local();
    int year = *y_tok ? atoi(y_tok) : build_year_local();

    if (month < 1 || month > 12 || year < 1) {
        print_line("usage: cal [month] [year]");
        return;
    }

    char line[80];
    snprintf(line, sizeof line, "%s %d", month_name_local(month), year);
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
    print_line("No RTC yet; using the build date unless month and year are provided.");
}

static void app_clock(const char *arg) {
    (void)arg;
    while (1) {
        uint32_t uptime = sys_time_ms() / 1000U;
        uint32_t hh = (uptime / 3600U) % 24U;
        uint32_t mm = (uptime / 60U) % 60U;
        uint32_t ss = uptime % 60U;
        int ref_month = build_month_local();
        int ref_year = build_year_local();

        sys_clear();
        sys_print("Mellivora clock\n\n");
        char line[96];
        snprintf(line, sizeof line, "Uptime clock: %02lu:%02lu:%02lu\n",
                 (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
        sys_print(line);
        snprintf(line, sizeof line, "Date ref: %s %d build default\n\n",
                 month_name_local(ref_month), ref_year);
        sys_print(line);
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
        snprintf(line, sizeof line, "Path:    %.96s\n", _sys_cwd);
        sys_print(line);
        snprintf(line, sizeof line, "Screen:  %dx%d chars\n", sys_getscreenw(), sys_getscreenh());
        sys_print(line);

        sys_print("\nShortcuts:\n");
        sys_print("  b browse files\n");
        sys_print("  n notes\n");
        sys_print("  j journal\n");
        sys_print("  u habits\n");
        sys_print("  p planner\n");
        sys_print("  m bookmarks\n");
        sys_print("  g games\n");
        sys_print("  y sprite editor\n");
        sys_print("  z terminal\n");
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
        else if (ch == 'j' || ch == 'J') app_journal(NULL);
        else if (ch == 'u' || ch == 'U') app_habits(NULL);
        else if (ch == 'p' || ch == 'P') app_planner(NULL);
        else if (ch == 'm' || ch == 'M') app_bookmarks(NULL);
        else if (ch == 'g' || ch == 'G') app_games(NULL);
        else if (ch == 'y' || ch == 'Y') app_sprite(NULL);
        else if (ch == 'z' || ch == 'Z') app_terminal(NULL);
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
        sys_print("5  Planner\n");
        sys_print("6  Journal\n");
        sys_print("7  Habits\n");
        sys_print("8  Bookmarks\n");
        sys_print("9  Games\n");
        sys_print("0  Clock and calendar\n");
        sys_print("b  BASIC\n");
        sys_print("c  Tiny C\n");
        sys_print("p  Sprite editor\n");
        sys_print("t  Terminal\n");
        sys_print("s  Samples\n");
        sys_print("q  Return to shell\n\n");
        sys_print("Select: ");

        int ch = sys_getchar();
        sys_putchar('\n');

        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == '1') app_dashboard(NULL);
        else if (ch == '2') app_browse(NULL);
        else if (ch == '3') app_notes(NULL);
        else if (ch == '4') app_calc(NULL);
        else if (ch == '5') app_planner(NULL);
        else if (ch == '6') app_journal(NULL);
        else if (ch == '7') app_habits(NULL);
        else if (ch == '8') app_bookmarks(NULL);
        else if (ch == '9') app_games(NULL);
        else if (ch == '0') app_clock(NULL);
        else if (ch == 'b' || ch == 'B') app_basic(NULL);
        else if (ch == 'c' || ch == 'C') app_tinyc(NULL);
        else if (ch == 'p' || ch == 'P') app_sprite(NULL);
        else if (ch == 't' || ch == 'T') app_terminal(NULL);
        else if (ch == 's' || ch == 'S') app_samples(NULL);
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
        snprintf(line, sizeof line, "CWD:     %.96s\n", _sys_cwd);
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
    char tmp[APP_TOKEN_MAX];
    tmp[0] = '\0';

    const char *s = skip_ws(src);
    if (*s == '(') {
        s++;
        const char *end = strrchr(s, ')');
        if (end) {
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
        const char *cmd_arg = skip_ws(arg_start);
        if (!app_run(cmd, cmd_arg)) {
            char err[64];
            snprintf(err, sizeof err, "script: unknown command '%.24s'", cmd);
            print_line(err);
        }
    }
}

static void app_paint(const char *arg) {
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
                char buf[PAINT_SAVE_MAX];
                int pos = 0;
                for (int yy = 0; yy < PAINT_HEIGHT && pos < (int)sizeof buf - 2; yy++) {
                    for (int xx = 0; xx < PAINT_WIDTH && pos < (int)sizeof buf - 2; xx++) {
                        buf[pos++] = canvas[yy * PAINT_WIDTH + xx] ? '#' : ' ';
                    }
                    buf[pos++] = '\n';
                }
                buf[pos] = '\0';
                if (write_text_file(filename, buf, "paint") >= 0) sys_print("Saved\n");
            }
        } else if (ch == 'l' || ch == 'L') {
            char filename[32];
            sys_print("Load from: ");
            if (app_read_line("", filename, sizeof filename) >= 0 && *filename) {
                char buf[PAINT_SAVE_MAX];
                int n = read_text_file(filename, buf, sizeof buf, "paint");
                if (n >= 0) {
                    memset(canvas, 0, sizeof canvas);
                    int pos = 0;
                    for (int yy = 0; yy < PAINT_HEIGHT; yy++) {
                        for (int xx = 0; xx < PAINT_WIDTH; xx++) {
                            if (pos < n && buf[pos] != '\n') {
                                canvas[yy * PAINT_WIDTH + xx] = (buf[pos] == '#');
                                pos++;
                            } else {
                                break;
                            }
                        }
                        if (pos < n && buf[pos] == '\n') pos++;
                    }
                    for (int yy = 0; yy < PAINT_HEIGHT; yy++) {
                        for (int xx = 0; xx < PAINT_WIDTH; xx++) {
                            uint32_t px = canvas[yy * PAINT_WIDTH + xx] ? LCD_WHITE : LCD_BLACK;
                            lcd_draw_pixel(xx, yy, px);
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

typedef void (*app_handler_t)(const char *arg);

typedef struct {
    const char *name;
    app_handler_t handler;
} app_cmd_entry_t;

static void app_noop(const char *arg) {
    (void)arg;
}

static bool app_dispatch_named(const char *cmd, const char *arg) {
    static const app_cmd_entry_t table[] = {
        {"hello", app_hello}, {"basename", app_basename}, {"dirname", app_dirname},
        {"seq", app_seq}, {"head", app_head}, {"tail", app_tail},
        {"wc", app_wc}, {"cut", app_cut}, {"grep", app_grep},
        {"find", app_find}, {"tree", app_tree}, {"du", app_du},
        {"df", app_df}, {"disk", app_df}, {"space", app_df},
        {"pager", app_pager}, {"more", app_pager}, {"rev", app_rev},
        {"sort", app_sort}, {"hexdump", app_hexdump}, {"od", app_od},
        {"hexedit", app_hexedit}, {"hex", app_hexedit},
        {"calc", app_calc}, {"cp", app_cp}, {"mv", app_mv},
        {"stat", app_stat}, {"edit", app_edit}, {"bedit", app_edit},
        {"browse", app_browse}, {"files", app_browse}, {"notes", app_notes},
        {"memo", app_notes}, {"journal", app_journal}, {"diary", app_journal},
        {"habits", app_habits}, {"habit", app_habits},
        {"bookmarks", app_bookmarks}, {"bookmark", app_bookmarks},
        {"favs", app_bookmarks}, {"favorites", app_bookmarks},
        {"games", app_games}, {"game", app_games}, {"dice", app_dice},
        {"coin", app_coin}, {"guess", app_guess}, {"snake", app_snake},
        {"sprite", app_sprite}, {"terminal", app_terminal}, {"term", app_terminal},
        {"tty", app_terminal}, {"serial", app_terminal},
        {"home", app_home}, {"launcher", app_home},
        {"dashboard", app_dashboard}, {"status", app_dashboard},
        {"sysmon", app_sysmon}, {"monitor", app_sysmon},
        {"settings", app_settings}, {"set", app_set}, {"todo", app_todo}, {"tasks", app_todo},
        {"planner", app_planner}, {"agenda", app_planner}, {"plan", app_planner},
        {"samples", app_samples}, {"demos", app_samples},
        {"clock", app_clock}, {"cal", app_cal}, {"calendar", app_cal},
        {"basic", app_basic}, {"tcc", app_tinyc}, {"tinyc", app_tinyc},
        {"script", app_script}, {"paint", app_paint}, {"sleep", app_sleep_ms},
        {"id", app_id}, {"true", app_noop}, {"false", app_noop},
    };

    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(cmd, table[i].name) == 0) {
            table[i].handler(arg);
            return true;
        }
    }
    return false;
}

bool app_run(const char *cmd, const char *arg) {
    if (!cmd || !*cmd) return false;
    return app_dispatch_named(cmd, arg);
}
