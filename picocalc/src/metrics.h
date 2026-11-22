#pragma once

#include <stdbool.h>
#include <stdint.h>

#define METRICS_BATTERY_HISTORY 16u

typedef struct {
    uint8_t core0_load_percent;
    uint8_t core1_load_percent;
    uint8_t battery_percent;
    bool battery_charging;
    uint32_t heap_used_bytes;
    uint32_t heap_free_bytes;
    uint8_t heap_fragmentation_percent;
    uint32_t ramdisk_used_bytes;
    uint32_t ramdisk_capacity_bytes;
    uint32_t sd_read_kib_per_second;
    uint32_t sd_write_kib_per_second;
    uint8_t battery_history[METRICS_BATTERY_HISTORY];
    uint8_t battery_history_count;
} runtime_metrics_t;

void metrics_sd_record(bool write, uint32_t bytes);
void metrics_sample_battery(void);
void metrics_snapshot(runtime_metrics_t *out);
