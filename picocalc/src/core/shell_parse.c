/*
 * shell_parse.c — pure shell line parsing helpers.
 */

#include "core/shell_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/app_shared.h"
#include "core/expr.h"

#define SHELL_EXPAND_MAX 512
#define SHELL_NAME_MAX 16

typedef struct {
    shell_var_lookup_fn lookup;
    void *ctx;
} arith_ctx_t;

static int arith_lookup(void *ctx, const char *name, size_t len) {
    arith_ctx_t *a = (arith_ctx_t *)ctx;
    if (!a || !a->lookup || len == 0 || len >= SHELL_NAME_MAX) return 0;

    char key[SHELL_NAME_MAX];
    memcpy(key, name, len);
    key[len] = '\0';

    const char *val = a->lookup(a->ctx, key);
    return val ? (int)strtol(val, NULL, 0) : 0;
}

int shell_tokenize(char *line, shell_argv_t *out) {
    if (!line || !out) return 0;

    out->argc = 0;
    char *r = line;
    char *w = line;

    while (*r) {
        while (*r == ' ' || *r == '\t')
            r++;
        if (!*r) break;
        if (out->argc >= SHELL_MAX_ARGS) break;

        out->argv[out->argc++] = w;

        while (*r && *r != ' ' && *r != '\t') {
            if (*r == '"' || *r == '\'') {
                char quote = *r++;
                while (*r && *r != quote)
                    *w++ = *r++;
                if (*r == quote) r++;
            } else if (*r == '\\' && r[1]) {
                r++;
                *w++ = *r++;
            } else {
                *w++ = *r++;
            }
        }

        /* Remember the next token before writing the terminator: when no
           quote or escape was used, w and r point at the same delimiter. */
        char *next = r;
        while (*next == ' ' || *next == '\t')
            next++;
        *w++ = '\0';
        r = next;
    }

    out->argv[out->argc] = NULL;
    return out->argc;
}

static const char *lookup_value(shell_var_lookup_fn lookup, void *ctx, const char *name,
                                int last_exit, char status_buf[8]) {
    if (name[0] == '?' && name[1] == '\0') {
        snprintf(status_buf, 8, "%d", last_exit);
        return status_buf;
    }
    return lookup ? lookup(ctx, name) : NULL;
}

void shell_expand_vars(char *line, size_t line_sz, shell_var_lookup_fn lookup, void *ctx,
                       int last_exit) {
    char out[SHELL_EXPAND_MAX];
    size_t oi = 0;
    bool in_squote = false;

    if (line_sz > sizeof out) line_sz = sizeof out;

    for (size_t i = 0; line[i] && oi + 1 < sizeof out; i++) {
        char c = line[i];
        if (c == '\'') {
            in_squote = !in_squote;
            out[oi++] = c;
            continue;
        }

        /* $((ARITHMETIC)) — evaluate with the same variable lookup. */
        if (!in_squote && c == '$' && line[i + 1] == '(' && line[i + 2] == '(') {
            size_t j = i + 3;
            int depth = 0;
            while (line[j]) {
                if (line[j] == '(') {
                    depth++;
                } else if (line[j] == ')') {
                    if (depth > 0) depth--;
                    else if (line[j + 1] == ')')
                        break;
                }
                j++;
            }

            if (line[j] == '\0') {
                out[oi++] = c; /* unmatched: keep the '$' and continue */
                continue;
            }

            char expr[SHELL_EXPAND_MAX];
            size_t elen = j - (i + 3);
            if (elen >= sizeof expr) elen = sizeof expr - 1;
            memcpy(expr, line + i + 3, elen);
            expr[elen] = '\0';
            shell_expand_vars(expr, sizeof expr, lookup, ctx, last_exit);

            arith_ctx_t ac = {lookup, ctx};
            bool ok = false;
            int value = expr_eval(expr, arith_lookup, &ac, &ok);
            if (ok) {
                char num[16];
                int n = snprintf(num, sizeof num, "%d", value);
                if (n < 0) n = 0;
                for (int k = 0; k < n && oi + 1 < sizeof out; k++)
                    out[oi++] = num[k];
            } else {
                for (size_t k = i; k <= j + 1 && oi + 1 < sizeof out; k++)
                    out[oi++] = line[k];
            }
            i = j + 1;
            continue;
        }

        if (in_squote || c != '$') {
            out[oi++] = c;
            continue;
        }

        /* $NAME or ${NAME} */
        size_t j = i + 1;
        bool braced = false;
        if (line[j] == '{') {
            braced = true;
            j++;
        }

        char name[SHELL_NAME_MAX];
        size_t ni = 0;
        /* Special single-char tokens: $? */
        if (!braced && line[j] == '?') {
            name[0] = '?';
            name[1] = '\0';
            ni = 1;
            j++;
        } else {
            while (line[j] && ni + 1 < sizeof name) {
                char nc = line[j];
                if (braced ? (nc == '}') : !(isalnum((unsigned char)nc) || nc == '_')) break;
                name[ni++] = nc;
                j++;
            }
        }
        name[ni] = '\0';
        if (braced && line[j] == '}') j++;
        if (ni == 0) {
            out[oi++] = c; /* lone $ */
            continue;
        }

        char status_buf[8];
        const char *val = lookup_value(lookup, ctx, name, last_exit, status_buf);
        if (val) {
            size_t vl = strlen(val);
            if (oi + vl >= sizeof out) vl = sizeof out - 1 - oi;
            memcpy(out + oi, val, vl);
            oi += vl;
        }
        i = j - 1;
    }
    out[oi] = '\0';
    copy_cstr(line, line_sz, out);
}

int shell_split_chain(char *line, char *segments[], unsigned char sep_kind[], int max_segments) {
    if (!line || !segments || !sep_kind || max_segments <= 0) return 0;

    int nseg = 1;
    segments[0] = line;
    sep_kind[0] = SHELL_SEP_ALWAYS;

    char quote = '\0';
    char *p = line;
    while (*p) {
        if (quote) {
            if (*p == quote) quote = '\0';
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (nseg < max_segments) {
            if (p[0] == '&' && p[1] == '&') {
                *p = '\0';
                segments[nseg] = p + 2;
                sep_kind[nseg] = SHELL_SEP_AND;
                nseg++;
                p++; /* consume second '&' */
            } else if (p[0] == '|' && p[1] == '|') {
                *p = '\0';
                segments[nseg] = p + 2;
                sep_kind[nseg] = SHELL_SEP_OR;
                nseg++;
                p++; /* consume second '|' */
            } else if (*p == ';') {
                *p = '\0';
                segments[nseg] = p + 1;
                sep_kind[nseg] = SHELL_SEP_ALWAYS;
                nseg++;
            }
        }
        p++;
    }
    return nseg;
}

bool shell_parse_redir(char *line, char **file, bool *append) {
    char *redir_file = NULL;
    bool redir_append = false;

    char quote = '\0';
    for (char *p = line; *p; p++) {
        if (quote) {
            if (*p == quote) quote = '\0';
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (p[0] == '>' && p[1] == '>') {
            *p = '\0';
            redir_file = skip_spaces(p + 2);
            redir_append = true;
            break;
        } else if (p[0] == '>') {
            *p = '\0';
            redir_file = skip_spaces(p + 1);
            redir_append = false;
            break;
        }
    }

    if (redir_file) trim_right(redir_file);
    trim_right(line);

    if (file) *file = redir_file;
    if (append) *append = redir_append;
    return redir_file != NULL;
}

bool shell_find_glob_arg(const char *line, size_t *start, size_t *len) {
    if (!line) return false;

    size_t i = 0;

    /* Skip the command word (honouring quotes and escapes). */
    while (line[i] == ' ' || line[i] == '\t')
        i++;
    char quote = '\0';
    while (line[i]) {
        char c = line[i];
        if (quote) {
            if (c == quote) quote = '\0';
            i++;
        } else if (c == '"' || c == '\'') {
            quote = c;
            i++;
        } else if (c == '\\' && line[i + 1]) {
            i += 2;
        } else if (c == ' ' || c == '\t') {
            break;
        } else {
            i++;
        }
    }

    /* Scan argument tokens for an unquoted wildcard. */
    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t')
            i++;
        if (!line[i]) break;

        size_t tok_start = i;
        bool quoted = false;
        bool has_glob = false;
        quote = '\0';

        while (line[i]) {
            char c = line[i];
            if (quote) {
                if (c == quote) quote = '\0';
                i++;
            } else if (c == '"' || c == '\'') {
                quoted = true;
                quote = c;
                i++;
            } else if (c == '\\' && line[i + 1]) {
                i += 2;
            } else if (c == ' ' || c == '\t') {
                break;
            } else {
                if (c == '*' || c == '?' || c == '[') has_glob = true;
                i++;
            }
        }

        if (has_glob && !quoted) {
            if (start) *start = tok_start;
            if (len) *len = i - tok_start;
            return true;
        }
    }

    return false;
}

int shell_split_pipeline(char *line, char *stages[], int max_stages) {
    if (!line || !stages || max_stages <= 0) return 0;

    int n = 1;
    stages[0] = line;

    char quote = '\0';
    char *p = line;
    while (*p) {
        char c = *p;
        if (quote) {
            if (c == quote) quote = '\0';
            p++;
        } else if (c == '"' || c == '\'') {
            quote = c;
            p++;
        } else if (c == '|') {
            if (p[1] == '|') {
                p += 2; /* chain operator, not a pipe */
            } else {
                if (n < max_stages) {
                    *p = '\0';
                    stages[n++] = p + 1;
                }
                p++;
            }
        } else {
            p++;
        }
    }
    return n;
}
