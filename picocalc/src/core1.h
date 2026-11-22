#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*core1_task_fn)(void *context);

bool core1_is_worker(void);
bool core1_is_running(void);
void core1_service_set_running(bool running);
void core1_call(core1_task_fn task, void *context);
bool core1_service_process(void);

void core1_metrics_add_core0_busy(uint32_t elapsed_us);
void core1_metrics_add_core1_busy(uint32_t elapsed_us);
void core1_metrics_add_idle(uint32_t elapsed_us);
void core1_metrics_get_load(uint8_t *core0_percent, uint8_t *core1_percent);
