/*
 * test_shell_parse.c — src/core/shell_parse.c
 */

#include <string.h>

#include "core/shell_parse.h"
#include "test_util.h"

static const char *lookup(void *ctx, const char *name) {
    (void)ctx;
    if (strcmp(name, "X") == 0) return "7";
    if (strcmp(name, "NAME") == 0) return "mellivora";
    return NULL;
}

static void test_expand(void) {
    char line[256];

    strcpy(line, "$X");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "7");

    strcpy(line, "a${NAME}b");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "amellivorab");

    strcpy(line, "$X$X");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "77");

    strcpy(line, "$?");
    shell_expand_vars(line, sizeof line, lookup, NULL, 127);
    CHECK_STR_EQ(line, "127");

    strcpy(line, "'$X'");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "'$X'");

    strcpy(line, "$UNKNOWN");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "");

    strcpy(line, "$");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "$");

    strcpy(line, "$$X");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "$7");

    strcpy(line, "plain");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "plain");

    strcpy(line, "abcdef");
    shell_expand_vars(line, 4, lookup, NULL, 0);
    CHECK_STR_EQ(line, "abc");

    /* No lookup callback: variables vanish, $? still works. */
    strcpy(line, "$X-$?");
    shell_expand_vars(line, sizeof line, NULL, NULL, 5);
    CHECK_STR_EQ(line, "-5");
}

static void test_tokenize(void) {
    shell_argv_t av;

    char line[] = "cmd \"a b\" c\\ d 'e f' \"\" end";
    int n = shell_tokenize(line, &av);
    CHECK_EQ(n, 6);
    CHECK_STR_EQ(av.argv[0], "cmd");
    CHECK_STR_EQ(av.argv[1], "a b");
    CHECK_STR_EQ(av.argv[2], "c d");
    CHECK_STR_EQ(av.argv[3], "e f");
    CHECK_STR_EQ(av.argv[4], "");
    CHECK_STR_EQ(av.argv[5], "end");
    CHECK(av.argv[6] == NULL);

    char concat[] = "ab\"cd\"e";
    n = shell_tokenize(concat, &av);
    CHECK_EQ(n, 1);
    CHECK_STR_EQ(av.argv[0], "abcde");

    char unterminated[] = "\"abc";
    n = shell_tokenize(unterminated, &av);
    CHECK_EQ(n, 1);
    CHECK_STR_EQ(av.argv[0], "abc");

    char many[] = "a b c d e f g h i j k l m n o p q r";
    n = shell_tokenize(many, &av);
    CHECK_EQ(n, SHELL_MAX_ARGS);

    CHECK_EQ(shell_tokenize(NULL, &av), 0);
    char tmp[] = "x";
    CHECK_EQ(shell_tokenize(tmp, NULL), 0);
}

static void test_arith(void) {
    char line[256];

    strcpy(line, "$((2+3))");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "5");

    strcpy(line, "x$((X*2))y");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "x14y");

    strcpy(line, "$((X+1))");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "8");

    strcpy(line, "$(( (2+3) * 4 ))");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "20");

    strcpy(line, "$((1/0))");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "$((1/0))");

    strcpy(line, "'$((2+3))'");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "'$((2+3))'");

    strcpy(line, "$((2+2))$((3+3))");
    shell_expand_vars(line, sizeof line, lookup, NULL, 0);
    CHECK_STR_EQ(line, "46");
}

static void test_glob_arg(void) {
    size_t start = 0;
    size_t len = 0;

    CHECK(!shell_find_glob_arg("ls", &start, &len));
    CHECK(shell_find_glob_arg("ls *.TXT", &start, &len));
    CHECK_EQ(start, 3);
    CHECK_EQ(len, 5);
    CHECK(shell_find_glob_arg("cp *.TXT /DEST", &start, &len));
    CHECK_EQ(start, 3);
    CHECK_EQ(len, 5);
    CHECK(shell_find_glob_arg("echo a?b", &start, &len));
    CHECK_EQ(start, 5);
    CHECK_EQ(len, 3);
    CHECK(!shell_find_glob_arg("ls \"*.TXT\"", &start, &len));
    CHECK(!shell_find_glob_arg("echo 'a*'", &start, &len));
    CHECK(!shell_find_glob_arg("*.CMD arg", &start, &len));
    CHECK(shell_find_glob_arg("ls -l *.B*", &start, &len));
    CHECK_EQ(start, 6);
    CHECK_EQ(len, 4);
}

static void test_chain(void) {
    char line[128];
    char *segs[SHELL_MAX_CHAIN];
    unsigned char kinds[SHELL_MAX_CHAIN];
    int n;

    strcpy(line, "a;b&&c||d");
    n = shell_split_chain(line, segs, kinds, SHELL_MAX_CHAIN);
    CHECK_EQ(n, 4);
    CHECK_STR_EQ(segs[0], "a");
    CHECK_STR_EQ(segs[1], "b");
    CHECK_STR_EQ(segs[2], "c");
    CHECK_STR_EQ(segs[3], "d");
    CHECK_EQ(kinds[0], SHELL_SEP_ALWAYS);
    CHECK_EQ(kinds[1], SHELL_SEP_ALWAYS);
    CHECK_EQ(kinds[2], SHELL_SEP_AND);
    CHECK_EQ(kinds[3], SHELL_SEP_OR);

    strcpy(line, "echo \"a;b\"");
    n = shell_split_chain(line, segs, kinds, SHELL_MAX_CHAIN);
    CHECK_EQ(n, 1);

    strcpy(line, ";;a");
    n = shell_split_chain(line, segs, kinds, SHELL_MAX_CHAIN);
    CHECK_EQ(n, 3);
    CHECK_STR_EQ(segs[0], "");
    CHECK_STR_EQ(segs[1], "");
    CHECK_STR_EQ(segs[2], "a");

    strcpy(line, "a;b;c;d;e");
    n = shell_split_chain(line, segs, kinds, 3);
    CHECK_EQ(n, 3);
    CHECK_STR_EQ(segs[0], "a");
    CHECK_STR_EQ(segs[1], "b");
    CHECK_STR_EQ(segs[2], "c;d;e");

    strcpy(line, "one");
    n = shell_split_chain(line, segs, kinds, SHELL_MAX_CHAIN);
    CHECK_EQ(n, 1);
    CHECK_STR_EQ(segs[0], "one");

    CHECK_EQ(shell_split_chain(NULL, segs, kinds, SHELL_MAX_CHAIN), 0);
    CHECK_EQ(shell_split_chain(line, NULL, kinds, SHELL_MAX_CHAIN), 0);
    CHECK_EQ(shell_split_chain(line, segs, NULL, SHELL_MAX_CHAIN), 0);
    CHECK_EQ(shell_split_chain(line, segs, kinds, 0), 0);
}

static void test_redir(void) {
    char line[128];
    char *file = NULL;
    bool append = true;

    strcpy(line, "echo hi > OUT.TXT");
    CHECK(shell_parse_redir(line, &file, &append));
    CHECK_STR_EQ(line, "echo hi");
    CHECK_STR_EQ(file, "OUT.TXT");
    CHECK(!append);

    strcpy(line, "echo hi>>OUT.TXT");
    CHECK(shell_parse_redir(line, &file, &append));
    CHECK_STR_EQ(line, "echo hi");
    CHECK_STR_EQ(file, "OUT.TXT");
    CHECK(append);

    strcpy(line, "echo \"a > b\"");
    CHECK(!shell_parse_redir(line, &file, &append));
    CHECK_STR_EQ(line, "echo \"a > b\"");

    strcpy(line, "ls >");
    CHECK(shell_parse_redir(line, &file, &append));
    CHECK_STR_EQ(line, "ls");
    CHECK_STR_EQ(file, "");

    strcpy(line, "ls");
    CHECK(!shell_parse_redir(line, &file, &append));
    CHECK_STR_EQ(line, "ls");

    file = NULL;
    append = false;
    strcpy(line, "x > F");
    CHECK(shell_parse_redir(line, NULL, NULL));
    CHECK_STR_EQ(line, "x");
    CHECK(file == NULL);
    CHECK(!append);
}

static void test_pipeline(void) {
    char line[128];
    char *stages[SHELL_MAX_PIPE];
    int n;

    strcpy(line, "ls");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 1);
    CHECK_STR_EQ(stages[0], "ls");

    strcpy(line, "ls | grep TXT");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 2);
    CHECK_STR_EQ(stages[0], "ls ");
    CHECK_STR_EQ(stages[1], " grep TXT");

    strcpy(line, "a|b|c");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 3);
    CHECK_STR_EQ(stages[0], "a");
    CHECK_STR_EQ(stages[1], "b");
    CHECK_STR_EQ(stages[2], "c");

    strcpy(line, "echo \"a|b\"");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 1);

    strcpy(line, "echo 'a|b'");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 1);

    strcpy(line, "a || b");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, 1);

    strcpy(line, "a | b | c | d | e");
    n = shell_split_pipeline(line, stages, SHELL_MAX_PIPE);
    CHECK_EQ(n, SHELL_MAX_PIPE);
    CHECK_STR_EQ(stages[SHELL_MAX_PIPE - 1], " d | e");

    CHECK_EQ(shell_split_pipeline(NULL, stages, SHELL_MAX_PIPE), 0);
    CHECK_EQ(shell_split_pipeline(line, NULL, SHELL_MAX_PIPE), 0);
}

void test_shell_parse(void) {
    test_expand();
    test_tokenize();
    test_arith();
    test_glob_arg();
    test_chain();
    test_redir();
    test_pipeline();
}
