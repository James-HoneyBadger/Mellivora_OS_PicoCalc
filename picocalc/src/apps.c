/*
 * apps.c — Application layer for Mellivora OS PicoCalc
 *
 * Implements all user-facing apps, utilities, file tools, editors,
 * productivity apps, creative tools, games, and language interpreters.
 * Commands are dispatched by name from the PicoLair shell in main.c.
 */

#include "apps.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syscall.h"
#include "picocalc_hw.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

/* RP2350 sizes are set in apps.h */
/* Private compile-time constants used only by apps.c */
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
    char cwd[APP_TOKEN_MAX];  /* saved CWD context (may be empty) */
} bookmark_item_t;

typedef struct {
    char date[16];
    char text[TODO_TEXT_MAX];
} journal_item_t;

typedef struct {
    char name[HABIT_NAME_MAX];
    int count;
    char last_date[16];
    int streak;
    int best_streak;
} habit_item_t;

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

static bool app_dispatch_named(const char *cmd, const char *arg);
static int expr_lookup_tinyc(void *ctx, const char *name, size_t len);
static void tinyc_set_var(tinyc_env_t *env, const char *name, int value);
static int expr_eval(const char *expr, expr_lookup_fn lookup, void *ctx, bool *ok);
static void tinyc_show_vars(void);
static void app_calc(const char *arg);
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

const char *skip_ws(const char *s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

void copy_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void append_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0 || !src) return;
    size_t len = strlen(dst);
    while (*src && len + 1 < dst_sz) dst[len++] = *src++;
    dst[len] = '\0';
}

void print_line(const char *s) {
    sys_print(s);
    sys_putchar('\n');
}

int read_text_file(const char *path, char *buf, size_t cap, const char *label) {
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

int load_file_bytes(const char *path, uint8_t *buf, size_t cap,
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

void app_make_abs(const char *path, char *out, size_t out_sz) {
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

void app_join_path(const char *root, const char *name, char *out, size_t out_sz) {
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

const char *next_token(const char *s, char *tok, size_t tok_sz) {
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

bool str_contains_ci(const char *hay, const char *needle) {
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

int str_casecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
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
                if (!ok) { print_line("calc: bad expression"); return; }
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
        if (!ok) { print_line("calc: bad expression"); return; }
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
        if (ci_eq(expr, "vars")) { tinyc_show_vars(); continue; }
        if (ci_eq(expr, "clear")) {
            memset(&g_tinyc_env, 0, sizeof g_tinyc_env);
            print_line("calc: variables cleared");
            continue;
        }
        app_calc(expr);
    }
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

void app_edit(const char *arg) {
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
        /* Parse optional CWD field: LABEL|TARGET|CWD */
        char *sep2 = strchr(sep, '|');
        if (sep2) {
            *sep2++ = '\0';
            sep2 = (char *)skip_ws(sep2);
            copy_cstr(items[count].cwd, sizeof items[count].cwd, sep2);
        } else {
            items[count].cwd[0] = '\0';
        }
        copy_cstr(items[count].target, sizeof items[count].target, sep);
        count++;
    }
    return count;
}

static bool bookmarks_save(bookmark_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        int wrote;
        if (*items[i].cwd)
            wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s|%s\n", items[i].label, items[i].target, items[i].cwd);
        else
            wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s\n", items[i].label, items[i].target);
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
        if (*items[i].cwd)
            snprintf(out, sizeof out, "%2d. %-12.12s -> %.60s [%.30s]", i + 1, items[i].label, items[i].target, items[i].cwd);
        else
            snprintf(out, sizeof out, "%2d. %-12.12s -> %.96s", i + 1, items[i].label, items[i].target);
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

static void bookmarks_open_target(const char *target, const char *saved_cwd) {
    /* Restore CWD context if saved */
    char old_cwd[APP_TOKEN_MAX];
    bool restored = false;
    if (saved_cwd && *saved_cwd) {
        copy_cstr(old_cwd, sizeof old_cwd, _sys_cwd);
        if (fat_is_dir(saved_cwd) == FAT_OK) {
            copy_cstr(_sys_cwd, sizeof _sys_cwd, saved_cwd);
            restored = true;
        }
    }

    char cmdline[APP_TOKEN_MAX];
    char cmd[APP_TOKEN_MAX];
    copy_cstr(cmdline, sizeof cmdline, target ? target : "");
    const char *rest = next_token(cmdline, cmd, sizeof cmd);

    if (*cmd && app_dispatch_named(cmd, rest)) {
        if (restored) copy_cstr(_sys_cwd, sizeof _sys_cwd, old_cwd);
        return;
    }

    char abs[APP_TOKEN_MAX];
    app_make_abs(target, abs, sizeof abs);
    if (fat_is_dir(abs) == FAT_OK) {
        app_browse(abs);
        if (restored) copy_cstr(_sys_cwd, sizeof _sys_cwd, old_cwd);
        return;
    }

    fat_file_t f;
    if (fat_open(abs, &f) == FAT_OK) {
        app_edit(abs);
        if (restored) copy_cstr(_sys_cwd, sizeof _sys_cwd, old_cwd);
        return;
    }

    if (restored) copy_cstr(_sys_cwd, sizeof _sys_cwd, old_cwd);
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
        copy_cstr(items[idx].cwd, sizeof items[idx].cwd, _sys_cwd);
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
        bookmarks_open_target(items[idx].target, items[idx].cwd);
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
        /* Parse optional streak fields (backwards-compatible) */
        items[count].streak = 0;
        items[count].best_streak = 0;
        char *sep3 = strchr(sep2, '|');
        if (sep3) {
            *sep3++ = '\0';
            items[count].streak = atoi(sep3);
            char *sep4 = strchr(sep3, '|');
            if (sep4) {
                *sep4++ = '\0';
                items[count].best_streak = atoi(sep4);
            }
        }
        count++;
    }
    return count;
}

static bool habits_save(habit_item_t *items, int count) {
    char buf[APP_READ_MAX + 1];
    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%d|%s|%d|%d\n",
                             items[i].name, items[i].count,
                             *items[i].last_date ? items[i].last_date : "none",
                             items[i].streak, items[i].best_streak);
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
        snprintf(out, sizeof out, "%2d. %-16.16s cnt=%d stk=%d best=%d last=%.10s",
                 i + 1, items[i].name, items[i].count,
                 items[i].streak, items[i].best_streak,
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
        items[count].streak = 0;
        items[count].best_streak = 0;
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
        /* Streak logic: if last_date is yesterday or today, continue streak */
        if (strcmp(items[idx].last_date, date) == 0) {
            /* Same day — don't double-count, just record */
        } else {
            /* Check if this is a consecutive day (simple: compare date strings) */
            bool consecutive = false;
            if (*items[idx].last_date && strlen(items[idx].last_date) == 10) {
                /* Parse YYYY-MM-DD and check if difference is 1 day */
                int ly = 0, lm = 0, ld = 0, cy = 0, cm = 0, cd = 0;
                sscanf(items[idx].last_date, "%d-%d-%d", &ly, &lm, &ld);
                sscanf(date, "%d-%d-%d", &cy, &cm, &cd);
                /* Approximate: same month and day differs by 1, or month transition */
                if (cy == ly && cm == lm && cd == ld + 1) consecutive = true;
                else if (cy == ly && cm == lm + 1 && cd == 1 && ld >= 28) consecutive = true;
                else if (cy == ly + 1 && cm == 1 && lm == 12 && cd == 1 && ld == 31) consecutive = true;
            }
            if (consecutive) {
                items[idx].streak++;
            } else {
                items[idx].streak = 1;
            }
            if (items[idx].streak > items[idx].best_streak)
                items[idx].best_streak = items[idx].streak;
        }
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
        items[idx].streak = 0;
        items[idx].best_streak = 0;
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
    print_line("Use w/a/s/d to move, q to quit");

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
    uint32_t speed_ms = 200; /* ms per tick — gets faster */

    /* Load persistent high score */
    int hi_score = 0;
    {
        char hbuf[32];
        int hn = sys_fread("/HISCORE.TXT", hbuf, sizeof hbuf - 1);
        if (hn > 0) { hbuf[hn] = '\0'; hi_score = atoi(hbuf); }
    }

    sx[0] = 4; sy[0] = 4;
    sx[1] = 3; sy[1] = 4;
    sx[2] = 2; sy[2] = 4;
    snake_spawn_food(&food_x, &food_y, sx, sy, len);
    if (hi_score > 0) {
        char hmsg[48];
        snprintf(hmsg, sizeof hmsg, "High score: %d", hi_score);
        print_line(hmsg);
    }

    while (1) {
        snake_render(sx, sy, len, food_x, food_y, score);

        /* Real-time: poll for input during the tick interval */
        uint32_t start = sys_time_ms();
        int last_dir = -1;
        while (sys_time_ms() - start < speed_ms) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 'Q' || ch == 0x03) goto snake_done;
            if ((ch == 'w' || ch == 'k') && dy != 1)  last_dir = 0;
            else if ((ch == 's' || ch == 'j') && dy != -1) last_dir = 1;
            else if ((ch == 'a' || ch == 'h') && dx != 1)  last_dir = 2;
            else if ((ch == 'd' || ch == 'l') && dx != -1) last_dir = 3;
            /* Arrow keys */
            else if (ch == 0xB5 && dy != 1)  last_dir = 0;
            else if (ch == 0xB4 && dy != -1) last_dir = 1;
            else if (ch == 0xB6 && dx != 1)  last_dir = 2;
            else if (ch == 0xB7 && dx != -1) last_dir = 3;
            sleep_ms(10);
        }

        if (last_dir == 0) { dx = 0; dy = -1; }
        else if (last_dir == 1) { dx = 0; dy = 1; }
        else if (last_dir == 2) { dx = -1; dy = 0; }
        else if (last_dir == 3) { dx = 1; dy = 0; }

        int nx = sx[0] + dx;
        int ny = sy[0] + dy;
        if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) {
            print_line("snake: wall hit; game over");
            goto snake_done;
        }
        for (int i = 0; i < len; i++) {
            if (sx[i] == nx && sy[i] == ny) {
                print_line("snake: self collision; game over");
                goto snake_done;
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
            /* Speed up every 5 points */
            if (speed_ms > 80 && score % 5 == 0) speed_ms -= 20;
        }
    }
snake_done:
    if (score > hi_score) {
        char sbuf[32];
        snprintf(sbuf, sizeof sbuf, "%d", score);
        sys_fwrite("/HISCORE.TXT", sbuf, (uint32_t)strlen(sbuf));
        char msg[48];
        snprintf(msg, sizeof msg, "New high score: %d!", score);
        print_line(msg);
    }
    print_line("Press any key...");
    (void)sys_getchar();
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

/* Stopwatch — Space=start/stop, L=lap, R=reset, Q=quit */
static void app_stopwatch(const char *arg) {
    (void)arg;
    bool running = false;
    uint32_t start_ms = 0;
    uint32_t accum_ms = 0;
    int laps = 0;
    sys_clear();
    sys_print("Mellivora stopwatch\n");
    sys_print("Space=start/stop  L=lap  R=reset  Q=quit\n\n");
    for (;;) {
        uint32_t now = running ? (sys_time_ms() - start_ms + accum_ms) : accum_ms;
        uint32_t mm = now / 60000U;
        uint32_t ss = (now / 1000U) % 60U;
        uint32_t ms = now % 1000U;
        char line[40];
        snprintf(line, sizeof line, "\r%02lu:%02lu.%03lu  %s   ",
                 (unsigned long)mm, (unsigned long)ss, (unsigned long)ms,
                 running ? "RUN " : "STOP");
        sys_print(line);
        int ch = kbd_getc();
        if (ch == 'q' || ch == 'Q' || ch == 0x03) break;
        if (ch == ' ') {
            if (running) { accum_ms = now; running = false; }
            else         { start_ms = sys_time_ms(); running = true; }
        } else if (ch == 'r' || ch == 'R') {
            running = false; accum_ms = 0; laps = 0;
            sys_clear();
            sys_print("Mellivora stopwatch\n");
            sys_print("Space=start/stop  L=lap  R=reset  Q=quit\n\n");
        } else if (ch == 'l' || ch == 'L') {
            laps++;
            sys_putchar('\n');
            char ln[48];
            snprintf(ln, sizeof ln, "  lap %2d  %02lu:%02lu.%03lu\n",
                     laps,
                     (unsigned long)mm, (unsigned long)ss, (unsigned long)ms);
            sys_print(ln);
        }
        sleep_ms(50);
    }
    sys_putchar('\n');
}

/* Pomodoro timer — 25 min work / 5 min break, Q quits early. */
static void app_pomodoro(const char *arg) {
    int work_min = 25, break_min = 5;
    if (arg && *arg) {
        char *end = NULL;
        long w = strtol(arg, &end, 10);
        if (w >= 1 && w <= 120) work_min = (int)w;
        if (end && *end) {
            long b = strtol(end, &end, 10);
            if (b >= 1 && b <= 60) break_min = (int)b;
        }
    }
    for (int round = 1;; round++) {
        for (int phase = 0; phase < 2; phase++) {
            int total = (phase == 0 ? work_min : break_min) * 60;
            const char *label = (phase == 0) ? "WORK " : "BREAK";
            sys_clear();
            char hdr[64];
            snprintf(hdr, sizeof hdr,
                     "Pomodoro #%d  %s  %d min total\nQ=quit\n\n",
                     round, label, total / 60);
            sys_print(hdr);
            uint32_t start = sys_time_ms();
            for (;;) {
                uint32_t elapsed = (sys_time_ms() - start) / 1000U;
                if ((int)elapsed >= total) break;
                int rem = total - (int)elapsed;
                char line[40];
                snprintf(line, sizeof line, "\r%s  %02d:%02d remaining ",
                         label, rem / 60, rem % 60);
                sys_print(line);
                int ch = kbd_getc();
                if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                    sys_putchar('\n');
                    return;
                }
                sleep_ms(250);
            }
            sys_putchar('\n');
            /* Beep three times via backlight flash */
            for (int i = 0; i < 3; i++) {
                kbd_set_backlight(0);   sleep_ms(120);
                kbd_set_backlight(255); sleep_ms(120);
            }
            sys_print(phase == 0 ? "** Take a break! **\n"
                                 : "** Back to work! **\n");
            sleep_ms(1500);
        }
    }
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
        long v;
        /* Detect 0x.. (hex), 0b.. (bin), 0o.. (octal) */
        if (st->s[0] == '0' && (st->s[1] == 'x' || st->s[1] == 'X')) {
            v = strtol(st->s + 2, &endptr, 16);
        } else if (st->s[0] == '0' && (st->s[1] == 'b' || st->s[1] == 'B')) {
            v = strtol(st->s + 2, &endptr, 2);
        } else if (st->s[0] == '0' && (st->s[1] == 'o' || st->s[1] == 'O')) {
            v = strtol(st->s + 2, &endptr, 8);
        } else {
            v = strtol(st->s, &endptr, 10);
        }
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

void rtrim_in_place(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

int app_read_line(const char *prompt, char *buf, size_t size) {
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

bool ci_eq(const char *a, const char *b) {
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
    if (var < 'A' || var > 'Z') {
        print_line("BASIC: invalid variable");
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
        if (var < 'A' || var > 'Z') {
            print_line("usage: INPUT A");
            return;
        }
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
        /* Check for Ctrl+C interrupt */
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
        if (ch == 0x03) { /* Ctrl+C */
            char brk[48];
            snprintf(brk, sizeof brk, "BREAK at line %d", g_basic_program[pc].number);
            print_line(brk);
            break;
        }
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

/* ============================================================
 * New utilities: watch, diff, env, lock, xxd, strings, yes, tee
 * ============================================================ */

/* watch CMD [SEC] — repeat a command every N seconds */
static void app_watch(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) { print_line("usage: watch <seconds> <command>"); return; }
    int interval = atoi(p);
    if (interval <= 0) interval = 2;
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_ws(p);
    if (!*p) { print_line("usage: watch <seconds> <command>"); return; }

    char cmd_buf[APP_TOKEN_MAX];
    strncpy(cmd_buf, p, sizeof cmd_buf - 1);
    cmd_buf[sizeof cmd_buf - 1] = '\0';

    while (1) {
        sys_clear();
        char hdr[APP_TOKEN_MAX + 32];
        snprintf(hdr, sizeof hdr, "Every %ds: %s", interval, cmd_buf);
        print_line(hdr);
        print_line("---");

        /* Execute command by calling app_run */
        char copy[APP_TOKEN_MAX];
        strncpy(copy, cmd_buf, sizeof copy - 1);
        copy[sizeof copy - 1] = '\0';
        char *sp = copy;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        char *warg = "";
        if (*sp) { *sp = '\0'; warg = sp + 1; while (*warg == ' ') warg++; }
        app_run(copy, warg);

        /* Wait interval, checking for quit */
        for (int s = 0; s < interval * 10; s++) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 'Q' || ch == 0x03) return;
            sleep_ms(100);
        }
    }
}

/* diff FILE1 FILE2 — simple line-by-line diff */
static void app_diff(const char *arg) {
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
            lcd_set_fg(LCD_RED);
            sys_print("< ");
            char lineout[64];
            size_t cp = s1 < sizeof lineout - 1 ? s1 : sizeof lineout - 1;
            memcpy(lineout, l1, cp); lineout[cp] = '\0';
            print_line(lineout);
            lcd_set_fg(LCD_GREEN);
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

/* env — show shell environment */
#define ENV_MAX 16
#define ENV_NAME_SZ 16
#define ENV_VALUE_SZ 64
static struct { char name[ENV_NAME_SZ]; char value[ENV_VALUE_SZ]; bool used; } _env[ENV_MAX];

static const char *env_get(const char *name) __attribute__((unused));
static const char *env_get(const char *name) {
    for (int i = 0; i < ENV_MAX; i++) {
        if (_env[i].used && strcmp(_env[i].name, name) == 0)
            return _env[i].value;
    }
    return NULL;
}

static void env_set(const char *name, const char *value) {
    /* Update existing */
    for (int i = 0; i < ENV_MAX; i++) {
        if (_env[i].used && strcmp(_env[i].name, name) == 0) {
            strncpy(_env[i].value, value, ENV_VALUE_SZ - 1);
            _env[i].value[ENV_VALUE_SZ - 1] = '\0';
            return;
        }
    }
    /* Find empty slot */
    for (int i = 0; i < ENV_MAX; i++) {
        if (!_env[i].used) {
            _env[i].used = true;
            memcpy(_env[i].name, name, ENV_NAME_SZ - 1);
            _env[i].name[ENV_NAME_SZ - 1] = '\0';
            memcpy(_env[i].value, value, ENV_VALUE_SZ - 1);
            _env[i].value[ENV_VALUE_SZ - 1] = '\0';
            return;
        }
    }
}

static void app_env(const char *arg) {
    const char *p = skip_ws(arg);
    if (*p) {
        /* set: env NAME=VALUE */
        const char *eq = strchr(p, '=');
        if (!eq) { print_line("usage: env [NAME=VALUE]"); return; }
        char name[ENV_NAME_SZ];
        size_t nlen = (size_t)(eq - p);
        if (nlen >= sizeof name) nlen = sizeof name - 1;
        memcpy(name, p, nlen);
        name[nlen] = '\0';
        env_set(name, eq + 1);
    } else {
        /* list all */
        for (int i = 0; i < ENV_MAX; i++) {
            if (_env[i].used) {
                sys_print(_env[i].name);
                sys_putchar('=');
                print_line(_env[i].value);
            }
        }
    }
}

/* lock — simple PIN screen lock */
static void app_lock(const char *arg) {
    (void)arg;
    print_line("Set 4-digit PIN:");
    char pin[5] = {0};
    int pi = 0;
    while (pi < 4) {
        int ch = sys_getchar();
        if (ch >= '0' && ch <= '9') {
            pin[pi++] = (char)ch;
            sys_putchar('*');
        }
    }
    sys_putchar('\n');

    sys_clear();
    print_line("=== LOCKED ===");
    print_line("Enter PIN to unlock:");

    while (1) {
        char attempt[5] = {0};
        int ai = 0;
        while (ai < 4) {
            int ch = sys_getchar();
            if (ch >= '0' && ch <= '9') {
                attempt[ai++] = (char)ch;
                sys_putchar('*');
            }
        }
        sys_putchar('\n');
        if (memcmp(pin, attempt, 4) == 0) {
            print_line("Unlocked!");
            sleep_ms(500);
            sys_clear();
            return;
        }
        print_line("Wrong PIN. Try again:");
    }
}

/* xxd — hex dump with offset+ASCII (like Unix xxd) */
static void app_xxd(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: xxd <file>"); return; }
    static char buf[APP_READ_MAX];
    int n = read_text_file(path, buf, sizeof buf, "xxd");
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
static void app_strings(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: strings <file>"); return; }
    static char buf[APP_READ_MAX];
    int n = read_text_file(path, buf, sizeof buf, "strings");
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
static void app_yes(const char *arg) {
    const char *s = skip_ws(arg);
    if (!*s) s = "y";
    while (1) {
        print_line(s);
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
        if (ch == 'q' || ch == 0x03) return;
    }
}

/* tee FILE — read stdin (line-by-line), write to both screen and file */
static void app_tee(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: tee <file>"); return; }

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

/* ============================================================
 * New programs: life, tetris, mandelbrot, piano, forth
 * ============================================================ */

/* Conway's Game of Life */
#define LIFE_W 38
#define LIFE_H 16
static void app_life(const char *arg) {
    (void)arg;
    static uint8_t grid[LIFE_H][LIFE_W];
    static uint8_t next[LIFE_H][LIFE_W];

    /* Random seed */
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++)
            grid[y][x] = (games_rand_u32() % 3 == 0) ? 1 : 0;

    int gen = 0;
    while (1) {
        sys_clear();
        char hdr[48];
        snprintf(hdr, sizeof hdr, "Game of Life  gen:%d  q=quit r=reset", gen);
        print_line(hdr);

        for (int y = 0; y < LIFE_H; y++) {
            char row[LIFE_W + 1];
            for (int x = 0; x < LIFE_W; x++)
                row[x] = grid[y][x] ? '#' : '.';
            row[LIFE_W] = '\0';
            print_line(row);
        }

        /* Check for quit or let the gen advance */
        for (int w = 0; w < 3; w++) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 0x03) { sys_clear(); return; }
            if (ch == 'r') {
                for (int y2 = 0; y2 < LIFE_H; y2++)
                    for (int x2 = 0; x2 < LIFE_W; x2++)
                        grid[y2][x2] = (games_rand_u32() % 3 == 0) ? 1 : 0;
                gen = 0;
                goto life_next;
            }
            sleep_ms(100);
        }

        /* Next generation */
        for (int y = 0; y < LIFE_H; y++) {
            for (int x = 0; x < LIFE_W; x++) {
                int neighbors = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int ny = (y + dy + LIFE_H) % LIFE_H;
                        int nx = (x + dx + LIFE_W) % LIFE_W;
                        neighbors += grid[ny][nx];
                    }
                }
                if (grid[y][x])
                    next[y][x] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
                else
                    next[y][x] = (neighbors == 3) ? 1 : 0;
            }
        }
        memcpy(grid, next, sizeof grid);
        gen++;
        life_next:;
    }
}

/* Tetris */
#define TETRIS_W 10
#define TETRIS_H 18
#define TETRIS_TYPES 7

static const uint16_t _tetris_shapes[TETRIS_TYPES][4] = {
    /* I */ {0x0F00, 0x2222, 0x00F0, 0x4444},
    /* O */ {0x6600, 0x6600, 0x6600, 0x6600},
    /* T */ {0x0E40, 0x4C40, 0x4E00, 0x4640},
    /* S */ {0x06C0, 0x8C40, 0x6C00, 0x4620},
    /* Z */ {0x0C60, 0x4C80, 0xC600, 0x2640},
    /* J */ {0x0E80, 0xC440, 0x2E00, 0x44C0},
    /* L */ {0x0E20, 0x44C0, 0x8E00, 0xC440},
};

static bool tetris_cell(uint16_t shape, int r, int c) {
    return (shape & (0x8000u >> (r * 4 + c))) != 0;
}

static void app_tetris(const char *arg) {
    (void)arg;
    static uint8_t board[TETRIS_H][TETRIS_W];
    memset(board, 0, sizeof board);
    int score = 0;
    int type = (int)(games_rand_u32() % TETRIS_TYPES);
    int rot = 0, px = 3, py = 0;
    uint32_t tick_ms = 500;
    bool game_over = false;

    while (!game_over) {
        /* Render */
        sys_clear();
        char hdr[48];
        snprintf(hdr, sizeof hdr, "Tetris  score:%d  q=quit", score);
        print_line(hdr);

        /* Merge current piece temporarily */
        uint8_t disp[TETRIS_H][TETRIS_W];
        memcpy(disp, board, sizeof board);
        uint16_t shape = _tetris_shapes[type][rot % 4];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (tetris_cell(shape, r, c) && py + r >= 0 &&
                    py + r < TETRIS_H && px + c >= 0 && px + c < TETRIS_W)
                    disp[py + r][px + c] = (uint8_t)(type + 1);

        for (int y = 0; y < TETRIS_H; y++) {
            char row[TETRIS_W + 3];
            row[0] = '|';
            for (int x = 0; x < TETRIS_W; x++)
                row[x + 1] = disp[y][x] ? '#' : ' ';
            row[TETRIS_W + 1] = '|';
            row[TETRIS_W + 2] = '\0';
            print_line(row);
        }
        {
            char bot[TETRIS_W + 3];
            bot[0] = '+';
            for (int x = 0; x < TETRIS_W; x++) bot[x + 1] = '-';
            bot[TETRIS_W + 1] = '+';
            bot[TETRIS_W + 2] = '\0';
            print_line(bot);
        }

        /* Wait and collect input */
        uint32_t start = sys_time_ms();
        int move = -1;
        while (sys_time_ms() - start < tick_ms) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) ch = kbd_getc();
            if (ch == 'q' || ch == 0x03) goto tetris_end;
            if (ch == 'a' || ch == 0xB6) move = 0; /* left */
            else if (ch == 'd' || ch == 0xB7) move = 1; /* right */
            else if (ch == 's' || ch == 0xB4) move = 2; /* down */
            else if (ch == 'w' || ch == 0xB5 || ch == ' ') move = 3; /* rotate */
            sleep_ms(20);
        }

        /* Apply move */
        int npx = px, npy = py, nrot = rot;
        if (move == 0) npx--;
        else if (move == 1) npx++;
        else if (move == 2) npy++;
        else if (move == 3) nrot = (rot + 1) % 4;

        /* Collision check for move */
        bool ok = true;
        uint16_t ns = _tetris_shapes[type][nrot % 4];
        for (int r = 0; r < 4 && ok; r++)
            for (int c = 0; c < 4 && ok; c++)
                if (tetris_cell(ns, r, c)) {
                    int ny = npy + r, nx = npx + c;
                    if (nx < 0 || nx >= TETRIS_W || ny >= TETRIS_H) ok = false;
                    else if (ny >= 0 && board[ny][nx]) ok = false;
                }
        if (ok && move != 2) { px = npx; py = npy; rot = nrot; }
        if (ok && move == 2) { py = npy; }

        /* Gravity: try to fall */
        ok = true;
        shape = _tetris_shapes[type][rot % 4];
        for (int r = 0; r < 4 && ok; r++)
            for (int c = 0; c < 4 && ok; c++)
                if (tetris_cell(shape, r, c)) {
                    int ny = py + 1 + r, nx = px + c;
                    if (ny >= TETRIS_H) ok = false;
                    else if (ny >= 0 && board[ny][nx]) ok = false;
                }

        if (ok) {
            py++;
        } else {
            /* Lock piece */
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    if (tetris_cell(shape, r, c) && py + r >= 0 &&
                        py + r < TETRIS_H && px + c >= 0 && px + c < TETRIS_W)
                        board[py + r][px + c] = (uint8_t)(type + 1);

            /* Clear lines */
            for (int y = TETRIS_H - 1; y >= 0; y--) {
                bool full = true;
                for (int x = 0; x < TETRIS_W; x++)
                    if (!board[y][x]) { full = false; break; }
                if (full) {
                    memmove(board[1], board[0], (size_t)y * TETRIS_W);
                    memset(board[0], 0, TETRIS_W);
                    score += 10;
                    y++; /* recheck same row */
                }
            }

            /* New piece */
            type = (int)(games_rand_u32() % TETRIS_TYPES);
            rot = 0; px = 3; py = 0;

            /* Check game over */
            shape = _tetris_shapes[type][0];
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    if (tetris_cell(shape, r, c) && py + r >= 0 && board[py + r][px + c])
                        game_over = true;

            /* Speed up */
            if (tick_ms > 100 && score % 50 == 0) tick_ms -= 30;
        }
    }

tetris_end:
    {
        char msg[48];
        snprintf(msg, sizeof msg, "Game Over! Score: %d", score);
        print_line(msg);
    }
    print_line("Press any key...");
    (void)sys_getchar();
    sys_clear();
}

/* Mandelbrot fractal renderer */
static void app_mandelbrot(const char *arg) {
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
static void app_piano(const char *arg) {
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

    /* Initialize PWM on audio pins */
    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    uint sl_l = pwm_gpio_to_slice_num(AUDIO_PIN_L);
    uint sl_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);
    pwm_set_enabled(sl_l, false);
    pwm_set_enabled(sl_r, false);

    while (1) {
        int ch = sys_getchar();
        if (ch == 'q' || ch == 0x03) break;

        /* Find note */
        const char *k = strchr(keys, ch);
        if (!k) continue;
        int idx = (int)(k - keys);
        uint16_t freq = notes[idx];

        /* Configure PWM for this frequency */
        uint32_t sys_clk = clock_get_hz(clk_sys);
        uint32_t wrap = sys_clk / freq;
        uint16_t div16 = 1;
        while (wrap > 65535) { div16++; wrap = sys_clk / (freq * div16); }

        pwm_set_clkdiv_int_frac(sl_l, div16, 0);
        pwm_set_wrap(sl_l, (uint16_t)wrap);
        pwm_set_chan_level(sl_l, PWM_CHAN_A, (uint16_t)(wrap / 4));
        pwm_set_enabled(sl_l, true);
        pwm_set_clkdiv_int_frac(sl_r, div16, 0);
        pwm_set_wrap(sl_r, (uint16_t)wrap);
        pwm_set_chan_level(sl_r, PWM_CHAN_A, (uint16_t)(wrap / 4));
        pwm_set_enabled(sl_r, true);

        /* Play for 200ms */
        sleep_ms(200);
        pwm_set_enabled(sl_l, false);
        pwm_set_enabled(sl_r, false);
    }
    pwm_set_enabled(sl_l, false);
    pwm_set_enabled(sl_r, false);
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

static void app_forth(const char *arg) {
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
                    /* Execute body — simple recursive call by re-tokenizing */
                    char body_copy[128];
                    strncpy(body_copy, dict[i].body, sizeof body_copy - 1);
                    body_copy[sizeof body_copy - 1] = '\0';
                    /* Push body tokens into a mini evaluator */
                    char *bt = body_copy;
                    while (*bt) {
                        while (*bt == ' ') bt++;
                        if (!*bt) break;
                        char bw[FORTH_WORD_MAX];
                        int bwi = 0;
                        while (*bt && *bt != ' ' && bwi < FORTH_WORD_MAX - 1)
                            bw[bwi++] = *bt++;
                        bw[bwi] = '\0';
                        /* Try as number */
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
                        else if (strcmp(bw, "dup") == 0) { if (sp > 0 && sp < FORTH_STACK_SZ) { stack[sp] = stack[sp-1]; sp++; } }
                        else if (strcmp(bw, "drop") == 0) { if (sp > 0) sp--; }
                        else if (strcmp(bw, "swap") == 0) { if (sp >= 2) { int32_t t = stack[sp-1]; stack[sp-1] = stack[sp-2]; stack[sp-2] = t; } }
                        else if (strcmp(bw, "cr") == 0) { sys_putchar('\n'); }
                        else if (strcmp(bw, "emit") == 0) { if (sp > 0) sys_putchar((char)stack[--sp]); }
                    }
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

static void app_xmodem(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) { print_line("usage: xmodem <filename>"); return; }

    print_line("XMODEM receive: send file now...");
    print_line("(Start transfer in your terminal)");

#ifdef PICO_RP2350A
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
            if (total + 128 <= (int)sizeof filebuf) {
                memcpy(filebuf + total, data, 128);
                total += 128;
            }
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

/* theme — switch color scheme */
static void app_theme(const char *arg) {
    const char *name = skip_ws(arg);

    struct { const char *name; uint32_t fg; uint32_t bg; } themes[] = {
        {"green",  LCD_GREEN,  LCD_BLACK},
        {"amber",  LCD_AMBER,  LCD_BLACK},
        {"white",  LCD_WHITE,  LCD_BLACK},
        {"cyan",   LCD_CYAN,   LCD_BLACK},
        {"blue",   LCD_WHITE,  LCD_BLUE},
        {"matrix", LCD_GREEN,  LCD_BLACK},
        {"paper",  LCD_BLACK,  LCD_WHITE},
    };
    int n = (int)(sizeof themes / sizeof themes[0]);

    if (!*name) {
        print_line("Themes:");
        for (int i = 0; i < n; i++) print_line(themes[i].name);
        return;
    }

    for (int i = 0; i < n; i++) {
        if (strcmp(name, themes[i].name) == 0) {
            lcd_set_fg(themes[i].fg);
            lcd_set_bg(themes[i].bg);
            lcd_cls(themes[i].bg);
            char msg[32];
            snprintf(msg, sizeof msg, "Theme: %s", themes[i].name);
            print_line(msg);
            return;
        }
    }
    print_line("Unknown theme. Type 'theme' for list.");
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
        {"stopwatch", app_stopwatch}, {"timer", app_stopwatch},
        {"pomodoro", app_pomodoro},
        /* New utilities */
        {"watch", app_watch}, {"diff", app_diff}, {"env", app_env},
        {"lock", app_lock}, {"xxd", app_xxd}, {"strings", app_strings},
        {"yes", app_yes}, {"tee", app_tee},
        /* New programs */
        {"life", app_life}, {"tetris", app_tetris},
        {"mandelbrot", app_mandelbrot}, {"fractal", app_mandelbrot},
        {"piano", app_piano}, {"forth", app_forth},
        /* System */
        {"xmodem", app_xmodem}, {"theme", app_theme},
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
