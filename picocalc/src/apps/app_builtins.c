/*
 * apps/app_builtins.c — Small standalone commands and environment storage.
 */
#include "apps.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "syscall.h"
#include "fat.h"

void app_hello(const char *arg) {
    (void)arg;
    sys_setcolor(0x0F);
    sys_print("Hello, World! Welcome to Mellivora OS!\n");
    sys_setcolor(0x0A);
    sys_print("This sample app is now running on the PicoCalc target.\n");
    sys_setcolor(0x07);
}

void app_basename(const char *arg) {
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

void app_dirname(const char *arg) {
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

void app_seq(const char *arg) {
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

static const char *const _backup_files[] = {
    "/ALIASES.CFG", "/ENV.CFG", "/LASTCWD.TXT", "/HISTORY.LOG",
    "/CLOCK.CFG", "/SETTINGS.CFG", "/WIFI.CFG", "/PROFILE.CFG",
    "/NOTES.TXT", "/TODO.TXT", "/PLANNER.TXT", "/JOURNAL.TXT",
    "/HABITS.CFG", "/BOOKMARKS.CFG", "/HISCORE.TXT",
};
#define BACKUP_FORMAT_FILE "FORMAT.TXT"
#define BACKUP_FORMAT_TEXT "MELLIVORA_BACKUP 1\n"

static bool _backup_path(const char *directory, const char *name,
                         char *out, size_t out_size) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    int written = snprintf(out, out_size, "%s/%s", directory, base);
    return written >= 0 && (size_t)written < out_size;
}

static fat_result_t _backup_copy(const char *source, const char *target) {
    fat_file_t in;
    fat_result_t r = fat_open(source, &in);
    if (r != FAT_OK) return r;

    fat_file_t existing;
    r = fat_open(target, &existing);
    if (r == FAT_OK) return FAT_ERR_EXISTS;
    if (r != FAT_ERR_NOTFOUND) return r;

    char temporary[APP_TOKEN_MAX];
    const char *slash = strrchr(target, '/');
    size_t dir_len = slash ? (size_t)(slash - target) : 0;
    if (dir_len + sizeof "/_BKUP.TMP" > sizeof temporary)
        return FAT_ERR_NAMETOOLONG;
    if (dir_len == 0) {
        copy_cstr(temporary, sizeof temporary, "/_BKUP.TMP");
    } else {
        memcpy(temporary, target, dir_len);
        copy_cstr(temporary + dir_len, sizeof temporary - dir_len, "/_BKUP.TMP");
    }

    (void)fat_unlink(temporary);
    uint8_t chunk[512];
    bool first = true;
    int32_t n;
    while ((n = fat_read(&in, chunk, sizeof chunk)) > 0) {
        r = first ? fat_create(temporary, chunk, (uint32_t)n)
                  : fat_append(temporary, chunk, (uint32_t)n);
        if (r != FAT_OK) {
            (void)fat_unlink(temporary);
            return r;
        }
        first = false;
    }
    if (n < 0 && n != FAT_ERR_EOF) {
        (void)fat_unlink(temporary);
        return (fat_result_t)n;
    }
    if (first) {
        r = fat_create(temporary, NULL, 0);
        if (r != FAT_OK) return r;
    }

    r = fat_rename(temporary, target);
    if (r != FAT_OK) (void)fat_unlink(temporary);
    return r;
}

static void _backup_list_cb(const char *short_name, const char *long_name,
                            uint32_t size, bool is_dir, void *ctx) {
    (void)ctx;
    if (strcmp(short_name, ".") == 0 || strcmp(short_name, "..") == 0) return;
    char line[96];
    snprintf(line, sizeof line, "%c %-24.24s %lu bytes",
             is_dir ? 'd' : '-', long_name ? long_name : short_name,
             (unsigned long)size);
    print_line(line);
}

void app_backup(const char *arg) {
    char action[16];
    char directory[APP_TOKEN_MAX];
    const char *rest = next_token(arg, action, sizeof action);
    const char *path = next_token(rest, directory, sizeof directory);
    if (!*action || (strcmp(action, "create") != 0 &&
                     strcmp(action, "restore") != 0 &&
                     strcmp(action, "list") != 0)) {
        print_line("usage: backup create|restore|list [DIRECTORY]");
        return;
    }

    char backup_dir[APP_TOKEN_MAX];
    app_make_abs(*directory ? directory : "/BACKUP", backup_dir, sizeof backup_dir);
    if (*path) {
        print_line("usage: backup create|restore|list [DIRECTORY]");
        return;
    }

    if (strcmp(action, "create") == 0) {
        fat_result_t dr = fat_is_dir(backup_dir);
        if (dr == FAT_ERR_NOTFOUND) dr = fat_mkdir(backup_dir);
        if (dr != FAT_OK) {
            char msg[APP_TOKEN_MAX + 48];
            snprintf(msg, sizeof msg, "backup: cannot use directory: %s",
                     fat_result_str(dr));
            print_line(msg);
            return;
        }
        char format_path[APP_TOKEN_MAX];
        if (!_backup_path(backup_dir, BACKUP_FORMAT_FILE, format_path,
                          sizeof format_path) ||
            sys_fwrite(format_path, BACKUP_FORMAT_TEXT,
                       (uint32_t)strlen(BACKUP_FORMAT_TEXT)) < 0) {
            print_line("backup: could not write the backup format marker");
            return;
        }
        int copied = 0, skipped = 0, failed = 0;
        for (size_t i = 0; i < sizeof _backup_files / sizeof _backup_files[0]; i++) {
            char target[APP_TOKEN_MAX];
            if (!_backup_path(backup_dir, _backup_files[i], target, sizeof target)) {
                failed++;
                continue;
            }
            fat_result_t r = _backup_copy(_backup_files[i], target);
            if (r == FAT_ERR_NOTFOUND) {
                skipped++;
            } else if (r == FAT_ERR_EXISTS) {
                skipped++;
            } else if (r != FAT_OK) {
                char msg[APP_TOKEN_MAX + 64];
                snprintf(msg, sizeof msg, "backup: %s: %s",
                         _backup_files[i], fat_result_str(r));
                print_line(msg);
                failed++;
            } else {
                copied++;
            }
        }
        char msg[96];
        snprintf(msg, sizeof msg, "backup: %d copied, %d skipped, %d failed",
                 copied, skipped, failed);
        print_line(msg);
        if (copied == 0 && failed == 0)
            print_line("backup: no new files; choose another directory to refresh a backup");
        print_line("backup: WIFI.CFG contains readable credentials; protect the card");
        return;
    }

    if (strcmp(action, "list") == 0) {
        fat_result_t r = fat_ls_ex(backup_dir, _backup_list_cb, NULL);
        if (r != FAT_OK) {
            char msg[APP_TOKEN_MAX + 48];
            snprintf(msg, sizeof msg, "backup: cannot list directory: %s",
                     fat_result_str(r));
            print_line(msg);
        }
        return;
    }

    char format_path[APP_TOKEN_MAX];
    char format[32];
    if (!_backup_path(backup_dir, BACKUP_FORMAT_FILE, format_path,
                      sizeof format_path)) {
        print_line("backup: invalid backup directory");
        return;
    }
    fat_file_t format_file;
    fat_result_t format_result = fat_open(format_path, &format_file);
    if (format_result != FAT_OK || format_file.size >= sizeof format) {
        print_line("backup: unsupported or missing backup format marker");
        return;
    }
    int32_t format_len = fat_read(&format_file, format, sizeof format - 1);
    if (format_len < 0 || (uint32_t)format_len != format_file.size) {
        print_line("backup: could not read the backup format marker");
        return;
    }
    format[format_len] = '\0';
    if (strcmp(format, BACKUP_FORMAT_TEXT) != 0) {
        print_line("backup: unsupported backup format version");
        return;
    }

    int restored = 0, skipped = 0, failed = 0;
    for (size_t i = 0; i < sizeof _backup_files / sizeof _backup_files[0]; i++) {
        char source[APP_TOKEN_MAX];
        if (!_backup_path(backup_dir, _backup_files[i], source, sizeof source)) {
            failed++;
            continue;
        }
        fat_file_t existing;
        fat_result_t probe = fat_open(_backup_files[i], &existing);
        if (probe == FAT_OK) {
            skipped++;
            continue;
        }
        if (probe != FAT_ERR_NOTFOUND) {
            char msg[APP_TOKEN_MAX + 64];
            snprintf(msg, sizeof msg, "backup: inspect %s: %s",
                     _backup_files[i], fat_result_str(probe));
            print_line(msg);
            failed++;
            continue;
        }
        fat_result_t r = _backup_copy(source, _backup_files[i]);
        if (r == FAT_ERR_NOTFOUND) skipped++;
        else if (r != FAT_OK) {
            char msg[APP_TOKEN_MAX + 64];
            snprintf(msg, sizeof msg, "backup: restore %s: %s",
                     _backup_files[i], fat_result_str(r));
            print_line(msg);
            failed++;
        } else restored++;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "backup: %d restored, %d skipped, %d failed",
             restored, skipped, failed);
    print_line(msg);
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

void app_env(const char *arg) {
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
void app_lock(const char *arg) {
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

/* units — tiny length/temperature converter */
void app_units(const char *arg) {
    const char *p = skip_ws(arg);
    double v;
    char from[16], to[16];
    if (!p || sscanf(p, "%lf %15s %15s", &v, from, to) != 3) {
        print_line("usage: units VALUE FROM TO");
        print_line("  length: in ft yd mi mm cm m km");
        print_line("  weight: oz lb g kg st");
        print_line("  temp:   c f k");
        return;
    }

    for (char *c = from; *c; c++) *c = (char)tolower((unsigned char)*c);
    for (char *c = to;   *c; c++) *c = (char)tolower((unsigned char)*c);

    double meters = 0.0, grams = 0.0, celsius = 0.0;
    bool is_len = true, is_weight = false;

    if (strcmp(from, "in") == 0) meters = v * 0.0254;
    else if (strcmp(from, "ft") == 0) meters = v * 0.3048;
    else if (strcmp(from, "yd") == 0) meters = v * 0.9144;
    else if (strcmp(from, "mi") == 0) meters = v * 1609.344;
    else if (strcmp(from, "mm") == 0) meters = v / 1000.0;
    else if (strcmp(from, "cm") == 0) meters = v / 100.0;
    else if (strcmp(from, "m")  == 0) meters = v;
    else if (strcmp(from, "km") == 0) meters = v * 1000.0;
    else if (strcmp(from, "oz") == 0) { grams = v * 28.3495; is_len = false; is_weight = true; }
    else if (strcmp(from, "lb") == 0) { grams = v * 453.592; is_len = false; is_weight = true; }
    else if (strcmp(from, "g")  == 0) { grams = v;           is_len = false; is_weight = true; }
    else if (strcmp(from, "kg") == 0) { grams = v * 1000.0;  is_len = false; is_weight = true; }
    else if (strcmp(from, "st") == 0) { grams = v * 6350.29; is_len = false; is_weight = true; }
    else if (strcmp(from, "c")  == 0) { celsius = v;          is_len = false; }
    else if (strcmp(from, "f")  == 0) { celsius = (v - 32.0) * 5.0 / 9.0; is_len = false; }
    else if (strcmp(from, "k")  == 0) { celsius = v - 273.15; is_len = false; }
    else { print_line("units: unknown source unit"); return; }

    double out = 0.0;
    if (is_len) {
        if (strcmp(to, "in") == 0) out = meters / 0.0254;
        else if (strcmp(to, "ft") == 0) out = meters / 0.3048;
        else if (strcmp(to, "yd") == 0) out = meters / 0.9144;
        else if (strcmp(to, "mi") == 0) out = meters / 1609.344;
        else if (strcmp(to, "mm") == 0) out = meters * 1000.0;
        else if (strcmp(to, "cm") == 0) out = meters * 100.0;
        else if (strcmp(to, "m")  == 0) out = meters;
        else if (strcmp(to, "km") == 0) out = meters / 1000.0;
        else { print_line("units: unknown target unit"); return; }
    } else if (is_weight) {
        if (strcmp(to, "oz") == 0) out = grams / 28.3495;
        else if (strcmp(to, "lb") == 0) out = grams / 453.592;
        else if (strcmp(to, "g")  == 0) out = grams;
        else if (strcmp(to, "kg") == 0) out = grams / 1000.0;
        else if (strcmp(to, "st") == 0) out = grams / 6350.29;
        else { print_line("units: unknown target unit"); return; }
    } else {
        if (strcmp(to, "c") == 0) out = celsius;
        else if (strcmp(to, "f") == 0) out = celsius * 9.0 / 5.0 + 32.0;
        else if (strcmp(to, "k") == 0) out = celsius + 273.15;
        else { print_line("units: unknown target unit"); return; }
    }

    char msg[64];
    snprintf(msg, sizeof msg, "%.4g %s = %.4g %s", v, from, out, to);
    print_line(msg);
}

/* pass — generate a random password */
void app_pass(const char *arg) {
    int len = 12;
    if (arg && *arg) {
        len = atoi(arg);
        if (len < 4) len = 4;
        if (len > 64) len = 64;
    }

    static const char upper[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    static const char lower[] = "abcdefghijkmnopqrstuvwxyz";
    static const char digit[] = "23456789";
    static const char symbol[] = "!@#$%^&*-_=+";
    const char *sets[] = {upper, lower, digit, symbol};

    srand((unsigned)sys_time_ms());
    char out[65];

    /* One character from each class, then fill from the combined set. */
    for (int i = 0; i < 4; i++) {
        size_t n = strlen(sets[i]);
        out[i] = sets[i][(size_t)rand() % n];
    }
    static const char all[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*-_=+";
    size_t alln = sizeof all - 1;
    for (int i = 4; i < len; i++) out[i] = all[(size_t)rand() % alln];
    out[len] = '\0';

    /* Fisher-Yates shuffle so the required classes are not always first. */
    for (int i = len - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        char t = out[i];
        out[i] = out[j];
        out[j] = t;
    }
    print_line(out);
}
