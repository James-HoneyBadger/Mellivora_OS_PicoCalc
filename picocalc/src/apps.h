#pragma once

#include <stdbool.h>

/*
 * Small compatibility layer for ported Mellivora userland utilities.
 * Commands are invoked by name from the PicoLair shell.
 */
void app_init(void);
void app_boot(void);
bool app_run(const char *cmd, const char *arg);
