#include "ramdisk.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[RAMDISK_NAME_MAX];
    uint32_t offset;
    uint32_t size;
    bool used;
} ramdisk_entry_t;

static uint8_t *_storage;
static uint32_t _capacity;
static ramdisk_entry_t _entries[RAMDISK_FILE_MAX];
static volatile uint32_t _used;
static uint32_t _files;

static int _find_entry(const char *name) {
    for (uint32_t i = 0; i < RAMDISK_FILE_MAX; i++) {
        if (_entries[i].used && strcmp(_entries[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool _valid_name(const char *name) {
    if (!name || !*name || strlen(name) >= RAMDISK_NAME_MAX ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || iscntrl((unsigned char)*p)) return false;
    }
    return true;
}

static void _remove_entry_data(uint32_t index) {
    uint32_t offset = _entries[index].offset;
    uint32_t size = _entries[index].size;
    uint32_t used = __atomic_load_n(&_used, __ATOMIC_RELAXED);
    if (size && offset + size < used)
        memmove(_storage + offset, _storage + offset + size, used - offset - size);
    for (uint32_t i = 0; i < RAMDISK_FILE_MAX; i++) {
        if (_entries[i].used && i != index && _entries[i].offset > offset)
            _entries[i].offset -= size;
    }
    __atomic_store_n(&_used, used - size, __ATOMIC_RELEASE);
    memset(&_entries[index], 0, sizeof _entries[index]);
    _files--;
}

uint32_t ramdisk_capacity(void) {
    return _capacity;
}

bool ramdisk_init(void) {
    if (_storage) return true;
    uint32_t capacity = RAMDISK_CAPACITY;
    while (capacity >= 2048u) {
        _storage = (uint8_t *)malloc(capacity);
        if (_storage) {
            _capacity = capacity;
            return true;
        }
        capacity /= 2u;
    }
    return false;
}

uint32_t ramdisk_used(void) {
    return __atomic_load_n(&_used, __ATOMIC_ACQUIRE);
}

uint32_t ramdisk_file_count(void) {
    return _files;
}

bool ramdisk_write(const char *name, const void *data, uint32_t length) {
    if (!_storage || !_valid_name(name) || (!data && length) || length > _capacity)
        return false;
    int existing = _find_entry(name);
    uint32_t used = ramdisk_used();
    uint32_t old_size = existing >= 0 ? _entries[existing].size : 0;
    if (used - old_size + length > _capacity) return false;

    int slot = existing;
    if (existing >= 0) {
        _remove_entry_data((uint32_t)existing);
        _files++;
    } else {
        for (uint32_t i = 0; i < RAMDISK_FILE_MAX; i++) {
            if (!_entries[i].used) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) return false;
        _files++;
    }

    uint32_t offset = ramdisk_used();
    if (length) memcpy(_storage + offset, data, length);
    _entries[slot].used = true;
    strncpy(_entries[slot].name, name, sizeof _entries[slot].name - 1);
    _entries[slot].name[sizeof _entries[slot].name - 1] = '\0';
    _entries[slot].offset = offset;
    _entries[slot].size = length;
    __atomic_store_n(&_used, offset + length, __ATOMIC_RELEASE);
    return true;
}

const uint8_t *ramdisk_get(const char *name, uint32_t *length) {
    if (!name) return NULL;
    int index = _find_entry(name);
    if (index < 0) return NULL;
    if (length) *length = _entries[index].size;
    return _storage + _entries[index].offset;
}

bool ramdisk_remove(const char *name) {
    if (!name) return false;
    int index = _find_entry(name);
    if (index < 0) return false;
    _remove_entry_data((uint32_t)index);
    return true;
}

void ramdisk_list(ramdisk_list_fn callback, void *context) {
    if (!callback) return;
    for (uint32_t i = 0; i < RAMDISK_FILE_MAX; i++) {
        if (_entries[i].used) callback(_entries[i].name, _entries[i].size, context);
    }
}
