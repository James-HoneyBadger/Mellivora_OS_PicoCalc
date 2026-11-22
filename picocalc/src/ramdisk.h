#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(PICO_RP2350A) || defined(PICO_RP2350)
#define RAMDISK_CAPACITY (64u * 1024u)
#else
#define RAMDISK_CAPACITY (8u * 1024u)
#endif

#define RAMDISK_FILE_MAX 16u
#define RAMDISK_NAME_MAX 32u

typedef void (*ramdisk_list_fn)(const char *name, uint32_t size, void *context);

bool ramdisk_init(void);
uint32_t ramdisk_capacity(void);
uint32_t ramdisk_used(void);
uint32_t ramdisk_file_count(void);
bool ramdisk_write(const char *name, const void *data, uint32_t length);
const uint8_t *ramdisk_get(const char *name, uint32_t *length);
bool ramdisk_remove(const char *name);
void ramdisk_list(ramdisk_list_fn callback, void *context);
