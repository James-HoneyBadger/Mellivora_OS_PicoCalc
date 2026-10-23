#pragma once
/*
 * shell.h — script entry point implemented by the shell (main.c).
 */

#include <stdbool.h>

/*
 * Execute a script file line by line through the normal shell dispatch path,
 * so variables, aliases, chaining, redirection, and pipes all work.
 * `quiet` suppresses the "cannot open" message (used for /PROFILE.CFG).
 * Returns false if the file could not be opened or a line reported failure.
 */
bool shell_run_script(const char *path, bool quiet);
