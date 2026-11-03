#include "history.h"

#include <stdint.h>
#include <string.h>

bool shell_history_init(shell_history_t *history, char *storage, size_t capacity,
                        size_t entry_size) {
    if (!history || !storage || capacity == 0 || entry_size < 2 ||
        capacity > SIZE_MAX / entry_size) {
        return false;
    }

    history->storage = storage;
    history->capacity = capacity;
    history->entry_size = entry_size;
    history->count = 0;
    history->head = 0;
    memset(storage, 0, capacity * entry_size);
    return true;
}

void shell_history_push(shell_history_t *history, const char *line) {
    if (!history || !history->storage || !line || !*line) return;

    if (history->count > 0) {
        size_t previous = (history->head + history->capacity - 1) % history->capacity;
        if (strcmp(history->storage + previous * history->entry_size, line) == 0) return;
    }

    size_t len = strlen(line);
    if (len >= history->entry_size) len = history->entry_size - 1;
    char *entry = history->storage + history->head * history->entry_size;
    memcpy(entry, line, len);
    entry[len] = '\0';
    history->head = (history->head + 1) % history->capacity;
    if (history->count < history->capacity) history->count++;
}

const char *shell_history_get(const shell_history_t *history, size_t offset) {
    if (!history || !history->storage || offset >= history->count) return NULL;
    size_t index =
        (history->head + history->capacity - history->count + offset) % history->capacity;
    return history->storage + index * history->entry_size;
}

size_t shell_history_count(const shell_history_t *history) {
    return history ? history->count : 0;
}

void shell_history_clear(shell_history_t *history) {
    if (!history || !history->storage) return;
    memset(history->storage, 0, history->capacity * history->entry_size);
    history->count = 0;
    history->head = 0;
}
