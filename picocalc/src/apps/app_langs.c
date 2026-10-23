/*
 * apps/app_langs.c — BASIC, TinyC, calculator, scripts, and samples.
 */
#include "apps.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_internal.h"
#include "core/expr.h"
#include "shell.h"
#include "syscall.h"

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
    tinyc_var_t vars[TINYC_VAR_MAX];
    bool stop;
} tinyc_env_t;
static int expr_lookup_tinyc(void *ctx, const char *name, size_t len);
static void tinyc_set_var(tinyc_env_t *env, const char *name, int value);
static void tinyc_show_vars(void);

static basic_line_t g_basic_program[BASIC_LINE_MAX];
static int g_basic_count = 0;
static basic_env_t g_basic_env;
static tinyc_env_t g_tinyc_env;

void app_calc(const char *arg) {
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

static const char *sample_body(const char *name, const char **default_path, const char **kind) {
    if (!name || !*name || ci_eq(name, "list")) return NULL;

    if (ci_eq(name, "hello.bas") || ci_eq(name, "hello") || ci_eq(name, "basic-hello")) {
        if (default_path) *default_path = "HELLO.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"HELLO FROM MELLIVORA\"\n"
               "20 LET A = 5\n"
               "30 PRINT A * A\n"
               "40 END\n";
    }
    if (ci_eq(name, "count.bas") || ci_eq(name, "count")) {
        if (default_path) *default_path = "COUNT.BAS";
        if (kind) *kind = "basic";
        return "10 FOR A = 1 TO 10\n"
               "20 PRINT A\n"
               "30 NEXT A\n"
               "40 END\n";
    }
    if (ci_eq(name, "fib.bas") || ci_eq(name, "fibonacci") || ci_eq(name, "fib")) {
        if (default_path) *default_path = "FIB.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"FIBONACCI SEQUENCE\"\n"
               "20 LET A = 0\n"
               "30 LET B = 1\n"
               "40 FOR I = 1 TO 15\n"
               "50 PRINT A\n"
               "60 LET C = A + B\n"
               "70 LET A = B\n"
               "80 LET B = C\n"
               "90 NEXT I\n"
               "100 END\n";
    }
    if (ci_eq(name, "fizz.bas") || ci_eq(name, "fizzbuzz") || ci_eq(name, "fizzbuzz.bas")) {
        if (default_path) *default_path = "FIZZ.BAS";
        if (kind) *kind = "basic";
        return "10 FOR N = 1 TO 30\n"
               "20 LET F = N - (N / 15) * 15\n"
               "30 IF F = 0 THEN PRINT \"FIZZBUZZ\"\n"
               "40 LET F = N - (N / 3) * 3\n"
               "50 IF F = 0 THEN GOTO 90\n"
               "60 LET F = N - (N / 5) * 5\n"
               "70 IF F = 0 THEN PRINT \"BUZZ\"\n"
               "80 GOTO 100\n"
               "90 LET F = N - (N / 5) * 5\n"
               "95 IF F = 0 THEN PRINT \"FIZZBUZZ\" ELSE PRINT \"FIZZ\"\n"
               "100 NEXT N\n"
               "110 END\n";
    }
    if (ci_eq(name, "primes.bas") || ci_eq(name, "primes")) {
        if (default_path) *default_path = "PRIMES.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"PRIMES UP TO 50\"\n"
               "20 FOR N = 2 TO 50\n"
               "30 LET P = 1\n"
               "40 FOR D = 2 TO N - 1\n"
               "50 LET R = N - (N / D) * D\n"
               "60 IF R = 0 THEN LET P = 0\n"
               "70 NEXT D\n"
               "80 IF P = 1 THEN PRINT N\n"
               "90 NEXT N\n"
               "100 END\n";
    }
    if (ci_eq(name, "tinyc.tc") || ci_eq(name, "tinyc") || ci_eq(name, "demo.tc")) {
        if (default_path) *default_path = "DEMO.TC";
        if (kind) *kind = "tinyc";
        return "int x = 1;\n"
               "print(\"Tiny C demo\");\n"
               "print(x);\n"
               "x = x + 4;\n"
               "if (x > 2) print(x);\n"
               "vars\n";
    }
    if (ci_eq(name, "sieve.tc") || ci_eq(name, "sieve")) {
        if (default_path) *default_path = "SIEVE.TC";
        if (kind) *kind = "tinyc";
        return "int n = 30;\n"
               "int i = 2;\n"
               "int j = 0;\n"
               "int a[32];\n"
               "print(\"Primes to 30:\");\n"
               "while (i <= n) { a[i] = 1; i = i + 1; }\n"
               "i = 2;\n"
               "while (i <= n) {\n"
               "  if (a[i] == 1) {\n"
               "    print(i);\n"
               "    j = i + i;\n"
               "    while (j <= n) { a[j] = 0; j = j + i; }\n"
               "  }\n"
               "  i = i + 1;\n"
               "}\n";
    }
    if (ci_eq(name, "bits.tc") || ci_eq(name, "bitwise")) {
        if (default_path) *default_path = "BITS.TC";
        if (kind) *kind = "tinyc";
        return "int x = 0b10110011;\n"
               "int mask = 0x0F;\n"
               "int lo = x & mask;\n"
               "int hi = (x >> 4) & mask;\n"
               "print(\"byte:\"); print(x);\n"
               "print(\"low nibble:\"); print(lo);\n"
               "print(\"high nibble:\"); print(hi);\n"
               "print(\"x OR 0x40:\"); print(x | 0x40);\n"
               "print(\"x XOR 0xFF:\"); print(x ^ 0xFF);\n"
               "vars\n";
    }
    if (ci_eq(name, "calc.txt") || ci_eq(name, "calc") || ci_eq(name, "math")) {
        if (default_path) *default_path = "CALC.TXT";
        if (kind) *kind = "text";
        return "Try these in calc:\n"
               "ans = 7 * 8\n"
               "ans + 10\n"
               "(3 + 4) * 5\n"
               "2 ^ 10\n"
               "sqrt(144)\n"
               "sin(3.14159 / 2)\n";
    }
    if (ci_eq(name, "startup.sh") || ci_eq(name, "startup")) {
        if (default_path) *default_path = "STARTUP.SH";
        if (kind) *kind = "script";
        return "# Example startup script. Run with: script STARTUP.SH\n"
               "# For automatic startup use /PROFILE.CFG or:\n"
               "#   settings autorun STARTUP.SH\n"
               "echo Welcome to Mellivora OS\n"
               "date\n"
               "df\n"
               "echo Type  help  for commands\n";
    }
    /* Additional BASIC samples */
    if (ci_eq(name, "fact.bas") || ci_eq(name, "factorial")) {
        if (default_path) *default_path = "FACT.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"FACTORIALS 1-10\"\n"
               "20 FOR N = 1 TO 10\n"
               "30 LET F = N\n"
               "40 GOSUB 200\n"
               "50 PRINT N; \" != \"; R\n"
               "60 NEXT N\n"
               "70 END\n"
               "200 REM FACTORIAL SUBROUTINE: IN=F, OUT=R\n"
               "210 LET R = 1\n"
               "220 LET I = 1\n"
               "230 IF I > F THEN RETURN\n"
               "240 LET R = R * I\n"
               "250 LET I = I + 1\n"
               "260 GOTO 230\n";
    }
    if (ci_eq(name, "times.bas") || ci_eq(name, "times") || ci_eq(name, "multiply")) {
        if (default_path) *default_path = "TIMES.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"TIMES TABLE (1-5)\"\n"
               "20 FOR A = 1 TO 5\n"
               "30 FOR B = 1 TO 5\n"
               "40 PRINT A; \"x\"; B; \"=\"; A * B\n"
               "50 NEXT B\n"
               "60 NEXT A\n"
               "70 END\n";
    }
    if (ci_eq(name, "guess.bas") || ci_eq(name, "guessing")) {
        if (default_path) *default_path = "GUESS.BAS";
        if (kind) *kind = "basic";
        return "10 LET SECRET = 7\n"
               "20 LET TRIES = 0\n"
               "30 PRINT \"GUESS THE NUMBER (1-10)\"\n"
               "40 INPUT G\n"
               "50 LET TRIES = TRIES + 1\n"
               "60 IF G = SECRET THEN GOTO 100\n"
               "70 IF G < SECRET THEN PRINT \"TOO LOW\"\n"
               "80 IF G > SECRET THEN PRINT \"TOO HIGH\"\n"
               "90 GOTO 40\n"
               "100 PRINT \"CORRECT IN \"; TRIES; \" TRIES!\"\n"
               "110 END\n";
    }
    if (ci_eq(name, "squares.bas") || ci_eq(name, "squares")) {
        if (default_path) *default_path = "SQUARES.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"PERFECT SQUARES\"\n"
               "20 FOR N = 1 TO 12\n"
               "30 PRINT N; \"^2 = \"; N * N\n"
               "40 NEXT N\n"
               "50 END\n";
    }
    if (ci_eq(name, "celsius.bas") || ci_eq(name, "temp") || ci_eq(name, "celsius")) {
        if (default_path) *default_path = "CELSIUS.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"FAHRENHEIT TO CELSIUS\"\n"
               "20 FOR F = 32 TO 212 STEP 20\n"
               "30 LET C = (F - 32) * 5 / 9\n"
               "40 PRINT F; \"F = \"; C; \"C\"\n"
               "50 NEXT F\n"
               "60 END\n";
    }
    if (ci_eq(name, "gcd.bas") || ci_eq(name, "gcd")) {
        if (default_path) *default_path = "GCD.BAS";
        if (kind) *kind = "basic";
        return "10 PRINT \"GCD USING EUCLID'S ALGORITHM\"\n"
               "20 LET A = 48\n"
               "30 LET B = 36\n"
               "40 PRINT \"GCD(\"; A; \",\"; B; \") = \";\n"
               "50 IF B = 0 THEN GOTO 90\n"
               "60 LET T = B\n"
               "70 LET B = A - (A / B) * B\n"
               "80 LET A = T\n"
               "85 GOTO 50\n"
               "90 PRINT A\n"
               "100 END\n";
    }
    /* Additional TinyC samples */
    if (ci_eq(name, "sort.tc") || ci_eq(name, "bubblesort")) {
        if (default_path) *default_path = "SORT.TC";
        if (kind) *kind = "tinyc";
        return "int a[8];\n"
               "a[0]=5; a[1]=2; a[2]=8; a[3]=1;\n"
               "a[4]=9; a[5]=3; a[6]=7; a[7]=4;\n"
               "int n = 8;\n"
               "int i = 0;\n"
               "while (i < n - 1) {\n"
               "  int j = 0;\n"
               "  while (j < n - 1 - i) {\n"
               "    if (a[j] > a[j+1]) {\n"
               "      int t = a[j]; a[j] = a[j+1]; a[j+1] = t;\n"
               "    }\n"
               "    j = j + 1;\n"
               "  }\n"
               "  i = i + 1;\n"
               "}\n"
               "print(\"sorted:\");\n"
               "i = 0;\n"
               "while (i < n) { print(a[i]); i = i + 1; }\n";
    }
    if (ci_eq(name, "fib.tc") || ci_eq(name, "fibc")) {
        if (default_path) *default_path = "FIB.TC";
        if (kind) *kind = "tinyc";
        return "int a = 0;\n"
               "int b = 1;\n"
               "int i = 0;\n"
               "print(\"Fibonacci:\");\n"
               "while (i < 15) {\n"
               "  print(a);\n"
               "  int c = a + b;\n"
               "  a = b;\n"
               "  b = c;\n"
               "  i = i + 1;\n"
               "}\n";
    }
    /* New BASIC samples */
    if (ci_eq(name, "bounce.bas") || ci_eq(name, "bounce")) {
        if (default_path) *default_path = "BOUNCE.BAS";
        if (kind) *kind = "basic";
        return "10 REM Bouncing ball simulation\n"
               "20 LET X = 1\n"
               "30 LET DX = 1\n"
               "40 FOR T = 1 TO 40\n"
               "50   LET S = \"\"\n"
               "60   FOR I = 1 TO X\n"
               "70     LET S = S + \" \"\n"
               "80   NEXT I\n"
               "90   PRINT S + \"O\"\n"
               "100  LET X = X + DX\n"
               "110  IF X > 20 THEN LET DX = -1\n"
               "120  IF X < 1  THEN LET DX = 1\n"
               "130 NEXT T\n"
               "140 END\n";
    }
    if (ci_eq(name, "roman.bas") || ci_eq(name, "roman")) {
        if (default_path) *default_path = "ROMAN.BAS";
        if (kind) *kind = "basic";
        return "10 REM Roman numeral converter (1-20)\n"
               "20 DATA \"I\",\"II\",\"III\",\"IV\",\"V\"\n"
               "30 DATA \"VI\",\"VII\",\"VIII\",\"IX\",\"X\"\n"
               "40 DATA \"XI\",\"XII\",\"XIII\",\"XIV\",\"XV\"\n"
               "50 DATA \"XVI\",\"XVII\",\"XVIII\",\"XIX\",\"XX\"\n"
               "60 DIM R$(20)\n"
               "70 FOR I = 1 TO 20\n"
               "80   READ R$(I)\n"
               "90 NEXT I\n"
               "100 FOR N = 1 TO 20\n"
               "110   PRINT N; \" = \"; R$(N)\n"
               "120 NEXT N\n"
               "130 END\n";
    }
    if (ci_eq(name, "stats.bas") || ci_eq(name, "stats")) {
        if (default_path) *default_path = "STATS.BAS";
        if (kind) *kind = "basic";
        return "10 REM Statistics: mean and range\n"
               "20 DATA 4, 7, 2, 9, 1, 5, 8, 3, 6, 10\n"
               "30 DIM A(10)\n"
               "40 LET SUM = 0\n"
               "50 LET MN = 9999\n"
               "60 LET MX = -9999\n"
               "70 FOR I = 1 TO 10\n"
               "80   READ A(I)\n"
               "90   LET SUM = SUM + A(I)\n"
               "100  IF A(I) < MN THEN LET MN = A(I)\n"
               "110  IF A(I) > MX THEN LET MX = A(I)\n"
               "120 NEXT I\n"
               "130 PRINT \"SUM  = \"; SUM\n"
               "140 PRINT \"MEAN = \"; SUM / 10\n"
               "150 PRINT \"MIN  = \"; MN\n"
               "160 PRINT \"MAX  = \"; MX\n"
               "170 PRINT \"RANGE= \"; MX - MN\n"
               "180 END\n";
    }
    if (ci_eq(name, "loan.bas") || ci_eq(name, "loan")) {
        if (default_path) *default_path = "LOAN.BAS";
        if (kind) *kind = "basic";
        return "10 REM Simple loan payment calculator\n"
               "20 LET P = 10000\n"
               "30 LET R = 0.05\n"
               "40 LET N = 12\n"
               "50 LET M = P * R / (1 - (1 + R)^(-N))\n"
               "60 PRINT \"Principal: \"; P\n"
               "70 PRINT \"Rate:      \"; R * 100; \"%\"\n"
               "80 PRINT \"Months:    \"; N\n"
               "90 PRINT \"Payment:   \"; INT(M * 100 + 0.5) / 100\n"
               "100 LET BAL = P\n"
               "110 FOR I = 1 TO N\n"
               "120   LET INT2 = BAL * R\n"
               "130   LET PRIN = M - INT2\n"
               "140   LET BAL = BAL - PRIN\n"
               "150   PRINT \"Mo \"; I; \": bal=\"; INT(BAL * 100 + 0.5) / 100\n"
               "160 NEXT I\n"
               "170 END\n";
    }
    /* New TinyC samples */
    if (ci_eq(name, "roman.tc") || ci_eq(name, "romantc")) {
        if (default_path) *default_path = "ROMAN.TC";
        if (kind) *kind = "tinyc";
        return "int to_roman(int n) {\n"
               "  int vals[7];\n"
               "  vals[0]=1000; vals[1]=500; vals[2]=100;\n"
               "  vals[3]=50;   vals[4]=10;  vals[5]=5; vals[6]=1;\n"
               "  print(\"Roman numerals 1-20:\");\n"
               "  int i = 1;\n"
               "  while (i <= 20) {\n"
               "    int v = i;\n"
               "    print(v);\n"
               "    i = i + 1;\n"
               "  }\n"
               "  return 0;\n"
               "}\n"
               "to_roman(0);\n";
    }
    if (ci_eq(name, "stack.tc") || ci_eq(name, "stackdemo")) {
        if (default_path) *default_path = "STACK.TC";
        if (kind) *kind = "tinyc";
        return "/* Stack simulation using an array */\n"
               "int stk[16];\n"
               "int top = 0;\n"
               "stk[top] = 10; top = top + 1;\n"
               "stk[top] = 20; top = top + 1;\n"
               "stk[top] = 30; top = top + 1;\n"
               "print(\"Stack (top to bottom):\");\n"
               "while (top > 0) {\n"
               "  top = top - 1;\n"
               "  print(stk[top]);\n"
               "}\n";
    }
    if (ci_eq(name, "sieve2.tc") || ci_eq(name, "primesieve")) {
        if (default_path) *default_path = "SIEVE2.TC";
        if (kind) *kind = "tinyc";
        return "/* Sieve of Eratosthenes up to 100 */\n"
               "int a[101];\n"
               "int i = 0;\n"
               "while (i <= 100) { a[i] = 1; i = i + 1; }\n"
               "a[0] = 0; a[1] = 0;\n"
               "int p = 2;\n"
               "while (p * p <= 100) {\n"
               "  if (a[p]) {\n"
               "    int m = p * p;\n"
               "    while (m <= 100) { a[m] = 0; m = m + p; }\n"
               "  }\n"
               "  p = p + 1;\n"
               "}\n"
               "print(\"Primes up to 100:\");\n"
               "i = 2;\n"
               "while (i <= 100) { if (a[i]) print(i); i = i + 1; }\n";
    }
    return NULL;
}

void app_samples(const char *arg) {
    char mode[APP_TOKEN_MAX];
    char name[APP_TOKEN_MAX];
    char path[APP_TOKEN_MAX];
    const char *s = next_token(arg, mode, sizeof mode);

    if (!*mode || ci_eq(mode, "list")) {
        print_line("Bundled samples:");
        print_line("  BASIC programs:");
        print_line("    hello.bas   - hello world and arithmetic");
        print_line("    count.bas   - FOR/NEXT counting loop");
        print_line("    fib.bas     - Fibonacci sequence");
        print_line("    fizz.bas    - FizzBuzz (1-30)");
        print_line("    primes.bas  - primes up to 50 by trial division");
        print_line("    fact.bas    - factorials 1-10 with GOSUB");
        print_line("    times.bas   - 5x5 multiplication table");
        print_line("    guess.bas   - interactive number guessing game");
        print_line("    squares.bas - perfect squares 1-12");
        print_line("    celsius.bas - Fahrenheit to Celsius table");
        print_line("    gcd.bas     - GCD via Euclid's algorithm");
        print_line("    bounce.bas  - bouncing ball animation");
        print_line("    roman.bas   - Roman numerals 1-20");
        print_line("    stats.bas   - mean, min, max, range of a dataset");
        print_line("    loan.bas    - loan payment calculator");
        print_line("  Tiny C programs:");
        print_line("    tinyc.tc    - variables and conditionals demo");
        print_line("    sieve.tc    - prime sieve (Sieve of Eratosthenes)");
        print_line("    bits.tc     - bitwise AND/OR/XOR/shift demo");
        print_line("    sort.tc     - bubble sort of 8 integers");
        print_line("    fib.tc      - Fibonacci sequence in C");
        print_line("    roman.tc    - Roman numerals demo");
        print_line("    stack.tc    - stack simulation with an array");
        print_line("    sieve2.tc   - primes up to 100 (compact version)");
        print_line("  Other:");
        print_line("    calc.txt    - quick calculator cheat-sheet");
        print_line("    startup.sh  - example startup script");
        print_line("Use: samples show NAME  or  samples write NAME [PATH]  or  samples run NAME");
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
            else if (ci_eq(kind, "script")) app_script(def);
            else print_line(body);
        } else {
            print_line("samples: could not stage sample file");
        }
        return;
    }

    print_line("usage: samples [list|show NAME|write NAME [PATH]|run NAME]");
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
        watchdog_update();  /* long BASIC programs must not trigger the WDT */
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
void app_basic(const char *arg) {
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

void app_tinyc(const char *arg) {
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

void app_script(const char *arg) {
    const char *path = skip_ws(arg);
    if (!*path) {
        print_line("usage: script FILE");
        return;
    }
    (void)shell_run_script(path, false);
}
