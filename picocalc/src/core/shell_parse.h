#pragma once
/*
 * shell_parse.h — pure shell line parsing: variables, chaining, redirection.
 *
 * All functions operate in place on a mutable buffer and are free of hardware
 * dependencies, so they are covered by host unit tests.
 */

#include <stdbool.h>
#include <stddef.h>

#define SHELL_MAX_CHAIN 16
#define SHELL_MAX_ARGS 16
#define SHELL_MAX_PIPE 4

/* Operator that precedes a segment. */
enum {
    SHELL_SEP_ALWAYS = 0, /* first segment, or ';' */
    SHELL_SEP_AND = 1,    /* && */
    SHELL_SEP_OR = 2,     /* || */
};

typedef const char *(*shell_var_lookup_fn)(void *ctx, const char *name);

/* Result of shell_tokenize(): argv[argc] is always NULL when argc < SHELL_MAX_ARGS. */
typedef struct {
    char *argv[SHELL_MAX_ARGS + 1];
    int argc;
} shell_argv_t;

/*
 * Split `line` in place into whitespace-separated arguments. Supports single
 * and double quotes and backslash escapes. Adjacent quoted/unquoted parts
 * concatenate (e.g. `ab"cd"` -> `abcd`). Returns the argument count.
 */
int shell_tokenize(char *line, shell_argv_t *out);

/*
 * Expand $NAME, ${NAME}, $?, and $((ARITHMETIC)) in place. Text inside
 * single quotes is left untouched. Unknown variables expand to nothing,
 * and a malformed arithmetic expression is preserved literally. Output is
 * bounded by `line_sz`; input longer than 512 bytes is truncated.
 */
void shell_expand_vars(char *line, size_t line_sz, shell_var_lookup_fn lookup, void *ctx,
                       int last_exit);

/*
 * Find the first unquoted wildcard token among the arguments of `line`
 * (the command word is skipped). Returns true and sets `*start` and `*len`
 * to the token's byte range when found.
 */
bool shell_find_glob_arg(const char *line, size_t *start, size_t *len);

/*
 * Split `line` in place on unquoted ';', '&&', and '||'. Fills `segments`
 * with pointers into `line` and `sep_kind` with SHELL_SEP_* values.
 * Returns the number of segments (at least 1 when max_segments > 0).
 */
int shell_split_chain(char *line, char *segments[], unsigned char sep_kind[], int max_segments);

/*
 * Extract an unquoted trailing `>` or `>>` redirection from `line` in place.
 * Returns true when a redirection was found. `*file` points at the trimmed
 * destination (possibly empty) and `*append` reports `>>`.
 */
bool shell_parse_redir(char *line, char **file, bool *append);

/*
 * Split `line` in place on unquoted single `|` characters. `||` is left
 * intact (it is a chain operator, not a pipe). Returns the stage count.
 */
int shell_split_pipeline(char *line, char *stages[], int max_stages);
