#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RP2350 (Pico 2) has 520 KB RAM — use larger buffers */
#ifdef PICO_RP2350A
#  define APP_TOKEN_MAX    512
#  define APP_READ_MAX     8192
#  define BASIC_LINE_MAX   256
#  define EDIT_LINES_MAX   256
#  define PAINT_WIDTH      256
#  define PAINT_HEIGHT     128
#else
#  define APP_TOKEN_MAX    256
#  define APP_READ_MAX     4096
#  define BASIC_LINE_MAX   128
#  define EDIT_LINES_MAX   128
#  define PAINT_WIDTH      128
#  define PAINT_HEIGHT     64
#endif

#define APP_PAGE_LINES     23
#define BASIC_STACK_MAX    16
#define TINYC_VAR_MAX      64
#define EDIT_LINE_MAX      96
#define EDIT_SAVE_MAX      (EDIT_LINES_MAX * (EDIT_LINE_MAX + 1) + 1)
#define BROWSE_ITEMS_MAX   64
#define BROWSE_NAME_MAX    32
#define HEXEDIT_BYTES_MAX  APP_READ_MAX

/* Sub-module headers */
#include "apps/text.h"
#include "apps/hex.h"
#include "apps/fs.h"

/*
 * Small compatibility layer for ported Mellivora userland utilities.
 * Commands are invoked by name from the PicoLair shell.
 */
void app_init(void);
void app_boot(void);
bool app_run(const char *cmd, const char *arg);

/* Shared internal helpers (implemented in apps.c) */
const char *skip_ws(const char *s);
const char *next_token(const char *s, char *tok, size_t tok_sz);
void        print_line(const char *s);
int         read_text_file(const char *path, char *buf, size_t cap, const char *label);
bool        str_contains_ci(const char *hay, const char *needle);
int         str_casecmp_local(const char *a, const char *b);
bool        ci_eq(const char *a, const char *b);
bool        match_simple_re(const char *re, const char *text);
void        app_make_abs(const char *path, char *out, size_t out_sz);
int         app_read_line(const char *prompt, char *buf, size_t size);
void        rtrim_in_place(char *s);
void        copy_cstr(char *dst, size_t dst_sz, const char *src);
void        append_cstr(char *dst, size_t dst_sz, const char *src);
void        app_join_path(const char *root, const char *name, char *out, size_t out_sz);
int         load_file_bytes(const char *path, uint8_t *buf, size_t cap, uint32_t *out_len, const char *label);

/* ---- Option parsing helpers (Phase 2.5) -------------------------------
   Lightweight, stateless. Operate on the raw arg string passed to a command.
   Tokens are whitespace-separated; no quoting is interpreted here (callers
   that need quoting should pre-tokenize).
   - opt_flag(args, "-d")        -> true if the token "-d" appears
   - opt_value(args, "-o", out, out_sz) -> true if "-o NAME" found; copies NAME
   - opt_strip(args, out, out_sz)       -> copy args with all -X / -X VAL
                                            options removed (positional only)
   ---------------------------------------------------------------------- */
bool opt_flag(const char *args, const char *flag);
bool opt_value(const char *args, const char *opt, char *out, size_t out_sz);
void opt_strip(const char *args, char *out, size_t out_sz);
