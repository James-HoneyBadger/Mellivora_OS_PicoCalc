#pragma once

#include <stdbool.h>

/*
 * Small compatibility layer for ported Mellivora userland utilities.
 * Commands are invoked by name from the PicoLair shell.
 */
bool app_run(const char *cmd, const char *arg);
