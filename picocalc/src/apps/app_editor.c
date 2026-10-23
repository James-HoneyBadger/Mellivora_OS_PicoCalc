/*
 * apps/app_editor.c — Line editor.
 */
#include "apps.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "syscall.h"
#include "ui/ui.h"

typedef struct {
    char path[APP_TOKEN_MAX];
    char lines[EDIT_LINES_MAX][EDIT_LINE_MAX];
    int line_count;
    bool dirty;
} editor_state_t;
static editor_state_t g_editor;

static void editor_clear(editor_state_t *ed, const char *path) {
    memset(ed, 0, sizeof *ed);
    if (path && *path) app_make_abs(path, ed->path, sizeof ed->path);
}

static bool editor_load(editor_state_t *ed, const char *path) {
    char buf[APP_READ_MAX + 1];
    editor_clear(ed, path);

    fat_file_t f;
    if (fat_open(ed->path, &f) == FAT_OK && f.size > APP_READ_MAX) {
        ui_error("edit", "file is too large for the editor; use pager or hexedit");
        return false;
    }

    int n = sys_fread(path, buf, APP_READ_MAX);
    if (n < 0) return true; /* new file */
    buf[n] = '\0';

    const char *p = buf;
    while (*p && ed->line_count < EDIT_LINES_MAX) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len > 0 && start[len - 1] == '\r') len--;
        if (len >= EDIT_LINE_MAX) {
            char msg[72];
            snprintf(msg, sizeof msg, "line %d is longer than %d characters",
                     ed->line_count + 1, EDIT_LINE_MAX - 1);
            ui_error("edit", msg);
            return false;
        }
        memcpy(ed->lines[ed->line_count], start, len);
        ed->lines[ed->line_count][len] = '\0';
        ed->line_count++;
        if (*p == '\n') p++;
        if (*p == '\0' && n > 0 && buf[n - 1] == '\n' && ed->line_count < EDIT_LINES_MAX) {
            ed->lines[ed->line_count][0] = '\0';
            ed->line_count++;
        }
    }
    if (*p) {
        char msg[64];
        snprintf(msg, sizeof msg, "file has more than %d lines", EDIT_LINES_MAX);
        ui_error("edit", msg);
        return false;
    }
    return true;
}

static void editor_status(editor_state_t *ed) {
    char out[160];
    const char *path = ed->path[0] ? ed->path : "(unnamed)";
    snprintf(out, sizeof out, "%.100s  |  %d lines  |  %s",
             path, ed->line_count, ed->dirty ? "modified" : "saved");
    ui_line(ed->dirty ? UI_WARNING : UI_MUTED, out);
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
        ui_printf(UI_MUTED, "%4d  ", i + 1);
        print_line(ed->lines[i]);
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
            ui_printf(UI_ACCENT, "%4d  ", i + 1);
            print_line(ed->lines[i]);
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
            ui_error("edit", "file exceeds the editor save limit");
            return false;
        }
        memcpy(buf + pos, ed->lines[i], len);
        pos += len;
        if (i + 1 < ed->line_count) buf[pos++] = '\n';
    }
    if (sys_fwrite(ed->path, buf, (uint32_t)pos) < 0) {
        ui_error("edit", "save failed");
        return false;
    }
    ed->dirty = false;
    char out[96];
    snprintf(out, sizeof out, "saved %s (%lu bytes)", ed->path, (unsigned long)pos);
    ui_ok(out);
    ui_notify("file saved");
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

    if (!editor_load(&g_editor, path)) return;
    if (g_editor.path[0] == '\0') app_make_abs(path, g_editor.path, sizeof g_editor.path);

    app_draw_header("Edit");
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
