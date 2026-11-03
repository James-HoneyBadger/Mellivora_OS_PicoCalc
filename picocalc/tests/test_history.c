#include "core/history.h"
#include "test_util.h"

void test_history(void) {
    char storage[3][8];
    shell_history_t history;

    CHECK(!shell_history_init(NULL, &storage[0][0], 3, 8));
    CHECK(!shell_history_init(&history, NULL, 3, 8));
    CHECK(!shell_history_init(&history, &storage[0][0], 0, 8));
    CHECK(!shell_history_init(&history, &storage[0][0], 3, 1));
    CHECK(shell_history_init(&history, &storage[0][0], 3, 8));
    CHECK_EQ(shell_history_count(&history), 0);
    CHECK(shell_history_get(&history, 0) == NULL);

    shell_history_push(&history, NULL);
    shell_history_push(&history, "");
    CHECK_EQ(shell_history_count(&history), 0);

    shell_history_push(&history, "first");
    shell_history_push(&history, "first");
    shell_history_push(&history, "second");
    shell_history_push(&history, "third");
    CHECK_EQ(shell_history_count(&history), 3);
    CHECK_STR_EQ(shell_history_get(&history, 0), "first");
    CHECK_STR_EQ(shell_history_get(&history, 1), "second");
    CHECK_STR_EQ(shell_history_get(&history, 2), "third");
    CHECK(shell_history_get(&history, 3) == NULL);

    shell_history_push(&history, "fourth");
    CHECK_EQ(shell_history_count(&history), 3);
    CHECK_STR_EQ(shell_history_get(&history, 0), "second");
    CHECK_STR_EQ(shell_history_get(&history, 2), "fourth");

    shell_history_push(&history, "123456789");
    CHECK_STR_EQ(shell_history_get(&history, 2), "1234567");
    shell_history_clear(&history);
    CHECK_EQ(shell_history_count(&history), 0);
    CHECK(shell_history_get(&history, 0) == NULL);
}
