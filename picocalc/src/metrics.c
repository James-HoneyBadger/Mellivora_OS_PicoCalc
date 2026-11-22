#include "metrics.h"

#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "core1.h"
#include "kbd.h"
#include "ramdisk.h"
#include "pico/stdlib.h"

static volatile uint32_t _sd_read_bytes;
static volatile uint32_t _sd_write_bytes;
static volatile uint8_t _battery_percent = 0xFF;
static volatile uint8_t _battery_charging;
static volatile uint8_t _battery_history[METRICS_BATTERY_HISTORY];
static volatile uint8_t _battery_head;
static volatile uint8_t _battery_count;
static uint32_t _previous_read_bytes;
static uint32_t _previous_write_bytes;
static uint32_t _previous_sd_ms;

extern char __HeapLimit;

void metrics_sd_record(bool write, uint32_t bytes) {
    if (write)
        __atomic_fetch_add(&_sd_write_bytes, bytes, __ATOMIC_RELAXED);
    else
        __atomic_fetch_add(&_sd_read_bytes, bytes, __ATOMIC_RELAXED);
}

void metrics_sample_battery(void) {
    int percent = kbd_battery_percent();
    if (percent < 0) return;
    __atomic_store_n(&_battery_percent, (uint8_t)percent, __ATOMIC_RELEASE);
    __atomic_store_n(&_battery_charging, kbd_is_charging() ? 1u : 0u, __ATOMIC_RELEASE);
    uint8_t head = __atomic_load_n(&_battery_head, __ATOMIC_RELAXED);
    __atomic_store_n(&_battery_history[head], (uint8_t)percent, __ATOMIC_RELEASE);
    __atomic_store_n(&_battery_head,
                     (uint8_t)((head + 1u) % METRICS_BATTERY_HISTORY),
                     __ATOMIC_RELEASE);
    uint8_t count = __atomic_load_n(&_battery_count, __ATOMIC_RELAXED);
    if (count < METRICS_BATTERY_HISTORY)
        __atomic_store_n(&_battery_count, (uint8_t)(count + 1u), __ATOMIC_RELEASE);
}

static void _heap_snapshot(runtime_metrics_t *out) {
    struct mallinfo info = mallinfo();
    intptr_t remaining = (intptr_t)((uintptr_t)&__HeapLimit - (uintptr_t)sbrk(0));
    if (remaining < 0) remaining = 0;
    out->heap_used_bytes = info.uordblks > 0 ? (uint32_t)info.uordblks : 0;
    out->heap_free_bytes = (info.fordblks > 0 ? (uint32_t)info.fordblks : 0) +
                           (uint32_t)remaining;
    if (info.fordblks > 0 && info.keepcost < info.fordblks) {
        uint32_t fragmented = (uint32_t)(info.fordblks - info.keepcost);
        out->heap_fragmentation_percent =
            (uint8_t)((fragmented * 100u) / (uint32_t)info.fordblks);
    } else {
        out->heap_fragmentation_percent = 0;
    }
}

static void _sd_throughput_snapshot(runtime_metrics_t *out, uint32_t now_ms) {
    uint32_t reads = __atomic_load_n(&_sd_read_bytes, __ATOMIC_RELAXED);
    uint32_t writes = __atomic_load_n(&_sd_write_bytes, __ATOMIC_RELAXED);
    if (_previous_sd_ms != 0 && now_ms != _previous_sd_ms) {
        uint32_t elapsed = now_ms - _previous_sd_ms;
        out->sd_read_kib_per_second =
            (uint32_t)(((uint64_t)(reads - _previous_read_bytes) * 1000u) /
                       ((uint64_t)elapsed * 1024u));
        out->sd_write_kib_per_second =
            (uint32_t)(((uint64_t)(writes - _previous_write_bytes) * 1000u) /
                       ((uint64_t)elapsed * 1024u));
    }
    _previous_read_bytes = reads;
    _previous_write_bytes = writes;
    _previous_sd_ms = now_ms;
}

static void _battery_history_snapshot(runtime_metrics_t *out) {
    uint8_t count = __atomic_load_n(&_battery_count, __ATOMIC_ACQUIRE);
    uint8_t head = __atomic_load_n(&_battery_head, __ATOMIC_ACQUIRE);
    out->battery_history_count = count;
    uint8_t first = count == METRICS_BATTERY_HISTORY ? head : 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t index = (uint8_t)((first + i) % METRICS_BATTERY_HISTORY);
        out->battery_history[i] =
            __atomic_load_n(&_battery_history[index], __ATOMIC_ACQUIRE);
    }
}

void metrics_snapshot(runtime_metrics_t *out) {
    if (!out) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    core1_metrics_get_load(&out->core0_load_percent, &out->core1_load_percent);
    _heap_snapshot(out);
    out->ramdisk_used_bytes = ramdisk_used();
    out->ramdisk_capacity_bytes = ramdisk_capacity();
    out->battery_percent = __atomic_load_n(&_battery_percent, __ATOMIC_ACQUIRE);
    out->battery_charging =
        __atomic_load_n(&_battery_charging, __ATOMIC_ACQUIRE) != 0;
    _battery_history_snapshot(out);
    _sd_throughput_snapshot(out, now);
}
