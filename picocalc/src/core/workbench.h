#pragma once
/*
 * core/workbench.h — Hardware Workbench command logic.
 *
 * Everything here is display- and SDK-independent: output goes through a
 * callback and peripherals through hw_ops_t. That keeps the exact shell
 * behaviour under host tests with mocked registers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/pinmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*out)(void *ctx, const char *text);
    void *out_ctx;
} wb_io_t;

void wb_emit(const wb_io_t *io, const char *text);
void wb_emitf(const wb_io_t *io, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* 0 on success, negative on error. All emit their own messages. */
int wb_pins(const wb_io_t *io);
int wb_gpio(const wb_io_t *io, const char *args);
int wb_pwm(const wb_io_t *io, const char *args);
int wb_i2cdetect(const wb_io_t *io, const char *args);

#ifdef __cplusplus
}
#endif
