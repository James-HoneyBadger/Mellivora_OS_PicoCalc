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
