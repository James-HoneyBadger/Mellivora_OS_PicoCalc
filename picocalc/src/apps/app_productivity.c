/*
 * apps/app_productivity.c — Notes, tasks, planner, journal, habits, bookmarks.
 */
#include "apps.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "apps/app_internal.h"
#include "syscall.h"
#include "ui/ui.h"

#define TODO_FILE        "/TODO.TXT"
#define PLANNER_FILE     "/PLANNER.TXT"
#define BOOKMARKS_FILE   "/BOOKMARKS.CFG"
#define JOURNAL_FILE     "/JOURNAL.TXT"
#define HABITS_FILE      "/HABITS.CFG"
#define TODO_ITEMS_MAX   64
#define TODO_TEXT_MAX    96
#define PLANNER_ITEMS_MAX 96
#define JOURNAL_ITEMS_MAX 96
#define JOURNAL_TEXT_MAX_ENTRY 128
#define BOOKMARKS_MAX    32
#define BOOKMARK_LABEL_MAX 24
#define HABITS_MAX       32
#define HABIT_NAME_MAX   32
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

static int habits_load(habit_item_t *items, int max_items);

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
    char buf[APP_READ_MAX + 1] = {0};
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

void app_todo(const char *arg) {
    todo_item_t items[TODO_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = todo_load(items, TODO_ITEMS_MAX);

    if (!*cmd || ci_eq(cmd, "list")) {
        app_draw_header("Todo");
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

void app_notes(const char *arg) {
    char path[APP_TOKEN_MAX];
    next_token(arg, path, sizeof path);
    if (!*path) {
        const app_settings_state_t *st = app_settings_get();
        copy_cstr(path, sizeof path, *st->notes_path ? st->notes_path : "/NOTES.TXT");
    }

    app_draw_header("Notes");
    print_line("Opening notes file...");
    app_edit(path);
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
    char buf[APP_READ_MAX + 1] = {0};
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

static int planner_import_file(const char *path, planner_item_t *items,
                              int *count) {
    fat_file_t file;
    char buf[APP_READ_MAX + 1];
    if (fat_open(path, &file) != FAT_OK) return -1;
    if (file.size > APP_READ_MAX) return -2;

    int32_t n = fat_read(&file, buf, APP_READ_MAX);
    if (n < 0 || (uint32_t)n != file.size) return -1;
    if (memchr(buf, '\0', (size_t)n)) return -3;
    buf[n] = '\0';

    int imported = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        rtrim_in_place(line);
        char *record = (char *)skip_ws(line);
        if (!*record || *record == '#') continue;
        char *sep = strchr(record, '|');
        if (!sep) return -3;
        *sep++ = '\0';
        rtrim_in_place(record);
        sep = (char *)skip_ws(sep);
        if (!planner_valid_date(record) || !*sep ||
            strlen(sep) >= sizeof items[0].text) return -3;

        bool duplicate = false;
        for (int i = 0; i < *count; i++) {
            if (strcmp(items[i].date, record) == 0 &&
                strcmp(items[i].text, sep) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        if (*count >= PLANNER_ITEMS_MAX) return -4;
        copy_cstr(items[*count].date, sizeof items[*count].date, record);
        copy_cstr(items[*count].text, sizeof items[*count].text, sep);
        (*count)++;
        imported++;
    }
    return imported;
}

void app_planner_remind(void) {
    static char reminded_date[16];
    if (!sys_rtc_is_set()) return;

    char today[16];
    planner_today_string(today, sizeof today);
    if (strcmp(today, reminded_date) == 0) return;

    planner_item_t items[PLANNER_ITEMS_MAX];
    int count = planner_load(items, PLANNER_ITEMS_MAX);
    int due = 0;
    for (int i = 0; i < count; i++)
        if (strcmp(items[i].date, today) == 0) due++;
    copy_cstr(reminded_date, sizeof reminded_date, today);
    if (due == 0) return;

    print_line("Planner reminder: today's entries");
    for (int i = 0; i < count; i++) {
        if (strcmp(items[i].date, today) != 0) continue;
        char out[160];
        snprintf(out, sizeof out, "  %s", items[i].text);
        print_line(out);
    }
}

void app_planner(const char *arg) {
    planner_item_t items[PLANNER_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    char today[16];
    planner_today_string(today, sizeof today);
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = planner_load(items, PLANNER_ITEMS_MAX);
    planner_sort(items, count);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "all")) {
        app_draw_header("Planner");
        planner_show(items, count, "");
        print_line("Usage: planner add DATE TEXT | today | month YYYY-MM | del N | next | import FILE | export [FILE] | edit");
        return;
    }

    if (ci_eq(cmd, "today")) {
        planner_show(items, count, today);
        return;
    }

    if (ci_eq(cmd, "remind")) {
        if (!sys_rtc_is_set()) {
            print_line("planner: set the clock or sync time before reminders can run");
            return;
        }
        planner_show(items, count, today);
        return;
    }

    if (ci_eq(cmd, "export")) {
        char path[APP_TOKEN_MAX];
        const char *extra = next_token(s, path, sizeof path);
        if (*skip_ws(extra)) {
            print_line("usage: planner export [FILE]");
            return;
        }
        if (!*path) copy_cstr(path, sizeof path, "/PLANNER-EXPORT.TXT");
        char abs[APP_TOKEN_MAX];
        app_make_abs(path, abs, sizeof abs);

        char buf[APP_READ_MAX + 1];
        size_t pos = 0;
        for (int i = 0; i < count; i++) {
            int wrote = snprintf(buf + pos, sizeof buf - pos, "%s|%s\n",
                                 items[i].date, items[i].text);
            if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
                print_line("planner: export is too large");
                return;
            }
            pos += (size_t)wrote;
        }
        if (sys_fwrite(abs, buf, (uint32_t)pos) < 0) {
            print_line("planner: export write failed");
            return;
        }
        char msg[APP_TOKEN_MAX + 48];
        snprintf(msg, sizeof msg, "planner: exported %d entries to %s", count, abs);
        print_line(msg);
        return;
    }

    if (ci_eq(cmd, "import")) {
        char path[APP_TOKEN_MAX];
        const char *extra = next_token(s, path, sizeof path);
        if (!*path) {
            print_line("usage: planner import FILE");
            return;
        }
        if (*skip_ws(extra)) {
            print_line("usage: planner import FILE");
            return;
        }
        char abs[APP_TOKEN_MAX];
        app_make_abs(path, abs, sizeof abs);
        int imported = planner_import_file(abs, items, &count);
        if (imported < 0) {
            if (imported == -2) print_line("planner: import file exceeds the supported size");
            else if (imported == -3) print_line("planner: import rejected; expected DATE|TEXT records");
            else if (imported == -4) print_line("planner: import would exceed the schedule limit");
            else print_line("planner: cannot read import file");
            return;
        }
        if (!planner_save(items, count)) return;
        char msg[96];
        snprintf(msg, sizeof msg, "planner: imported %d new entr%s",
                 imported, imported == 1 ? "y" : "ies");
        print_line(msg);
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

    print_line("usage: planner [list|add|today|remind|month|next|import|export|del|edit]");
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
    char buf[APP_READ_MAX + 1] = {0};
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

void app_bookmarks(const char *arg) {
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

void app_journal(const char *arg) {
    journal_item_t items[JOURNAL_ITEMS_MAX];
    char cmd[APP_TOKEN_MAX];
    char tok[APP_TOKEN_MAX];
    char today[16];
    planner_today_string(today, sizeof today);
    const char *s = next_token(arg, cmd, sizeof cmd);
    int count = journal_load(items, JOURNAL_ITEMS_MAX);
    journal_sort(items, count);

    if (!*cmd || ci_eq(cmd, "list") || ci_eq(cmd, "show")) {
        app_draw_header("Journal");
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

        char record[JOURNAL_TEXT_MAX_ENTRY];
        int rlen = snprintf(record, sizeof record, "%s|%.96s\n", date, text);
        if (rlen < 0 || rlen >= (int)sizeof record) {
            print_line("journal: entry too long");
            return;
        }
        fat_result_t wr = fat_append(JOURNAL_FILE, (const uint8_t *)record,
                                     (uint32_t)rlen);
        if (wr == FAT_ERR_NOTFOUND)
            wr = fat_create(JOURNAL_FILE, (const uint8_t *)record, (uint32_t)rlen);
        if (wr != FAT_OK) {
            ui_error("journal", "could not write entry");
            return;
        }
        ui_ok("journal: entry added");
        return;
    }

    print_line("usage: journal [list|add|today|month|edit]");
}

void app_tasks_export(const char *arg) {
    (void)arg;
    todo_item_t items[TODO_ITEMS_MAX];
    int count = todo_load(items, TODO_ITEMS_MAX);
    
    if (count == 0) {
        print_line("tasks-export: no tasks to export");
        return;
    }

    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    
    /* CSV header */
    int wrote = snprintf(buf + pos, sizeof buf - pos, "Status,Task\n");
    if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
        print_line("tasks-export: buffer too small");
        return;
    }
    pos += (size_t)wrote;

    /* CSV rows (escape quotes in text) */
    for (int i = 0; i < count; i++) {
        const char *status = items[i].done ? "Done" : "Open";
        const char *text = items[i].text;
        
        /* Simple CSV: wrap text in quotes and escape internal quotes */
        wrote = snprintf(buf + pos, sizeof buf - pos, "%s,\"", status);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("tasks-export: buffer too small");
            return;
        }
        pos += (size_t)wrote;

        for (int j = 0; text[j] && pos < sizeof buf - 2; j++) {
            if (text[j] == '"') {
                buf[pos++] = '"';
                buf[pos++] = '"';
            } else {
                buf[pos++] = text[j];
            }
        }
        if (pos >= sizeof buf - 3) {
            print_line("tasks-export: buffer too small");
            return;
        }
        buf[pos++] = '"';
        buf[pos++] = '\n';
    }

    if (sys_fwrite("/TASKS-EXPORT.CSV", buf, (uint32_t)pos) < 0) {
        print_line("tasks-export: write failed");
        return;
    }
    
    char msg[96];
    snprintf(msg, sizeof msg, "tasks-export: exported %d task(s) to /TASKS-EXPORT.CSV", count);
    print_line(msg);
}

void app_journal_export(const char *arg) {
    (void)arg;
    journal_item_t items[JOURNAL_ITEMS_MAX];
    int count = journal_load(items, JOURNAL_ITEMS_MAX);
    
    if (count == 0) {
        print_line("journal-export: no entries to export");
        return;
    }
    
    /* Sort by date (newest first for markdown) */
    journal_sort(items, count);

    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    
    /* Markdown header */
    int wrote = snprintf(buf + pos, sizeof buf - pos, "# Journal Export\n\n");
    if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
        print_line("journal-export: buffer too small");
        return;
    }
    pos += (size_t)wrote;

    /* Markdown entries (newest first) */
    for (int i = count - 1; i >= 0; i--) {
        wrote = snprintf(buf + pos, sizeof buf - pos, "## %s\n\n%s\n\n", 
                        items[i].date, items[i].text);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("journal-export: buffer too small");
            return;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite("/JOURNAL-EXPORT.MD", buf, (uint32_t)pos) < 0) {
        print_line("journal-export: write failed");
        return;
    }
    
    char msg[96];
    snprintf(msg, sizeof msg, "journal-export: exported %d entries to /JOURNAL-EXPORT.MD", count);
    print_line(msg);
}

void app_habits_export(const char *arg) {
    (void)arg;
    habit_item_t items[HABITS_MAX];
    int count = habits_load(items, HABITS_MAX);
    
    if (count == 0) {
        print_line("habits-export: no habits to export");
        return;
    }

    char buf[APP_READ_MAX + 1];
    size_t pos = 0;
    
    /* CSV header */
    int wrote = snprintf(buf + pos, sizeof buf - pos, "Habit,Count,LastDate,Streak,BestStreak\n");
    if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
        print_line("habits-export: buffer too small");
        return;
    }
    pos += (size_t)wrote;

    /* CSV rows */
    for (int i = 0; i < count; i++) {
        wrote = snprintf(buf + pos, sizeof buf - pos, "\"%s\",%d,%s,%d,%d\n",
                        items[i].name, items[i].count, items[i].last_date,
                        items[i].streak, items[i].best_streak);
        if (wrote < 0 || (size_t)wrote >= sizeof buf - pos) {
            print_line("habits-export: buffer too small");
            return;
        }
        pos += (size_t)wrote;
    }

    if (sys_fwrite("/HABITS-EXPORT.CSV", buf, (uint32_t)pos) < 0) {
        print_line("habits-export: write failed");
        return;
    }
    
    char msg[96];
    snprintf(msg, sizeof msg, "habits-export: exported %d habit(s) to /HABITS-EXPORT.CSV", count);
    print_line(msg);
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
    char buf[APP_READ_MAX + 1] = {0};
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

void app_habits(const char *arg) {
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
