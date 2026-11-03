#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *storage;
    size_t capacity;
    size_t entry_size;
    size_t count;
    size_t head;
} shell_history_t;

bool shell_history_init(shell_history_t *history, char *storage, size_t capacity,
                        size_t entry_size);
void shell_history_push(shell_history_t *history, const char *line);
const char *shell_history_get(const shell_history_t *history, size_t offset);
size_t shell_history_count(const shell_history_t *history);
void shell_history_clear(shell_history_t *history);
