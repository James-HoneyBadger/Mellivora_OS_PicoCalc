# Mellivora PicoCalc Programming Guide

This guide explains how to extend Mellivora PicoCalc and how to use the built-in language environments effectively.

## 1. Development Model

Mellivora PicoCalc is a native embedded firmware project. New features are usually added in one of two ways:

- extend the shell with a new built-in command
- add a new utility or interpreter entry in the app layer

## 2. Build Cycle

Typical edit and test cycle:

1. Modify source under picocalc/src/
2. Rebuild from the repository root with make picocalc
3. Flash the resulting UF2 to hardware
4. Test directly in the shell on the device

## 3. Adding a New Shell Command

To add a built-in command:

1. Implement a handler in the shell source.
2. Add help text so the command appears in the command list.
3. Wire the command into the dispatcher.
4. Rebuild and test on target hardware.

Built-ins are best for low-level device and system actions.

## 4. Adding a Ported App

Ported utilities live in the app layer.

Recommended pattern:

1. Implement a small function that accepts the command argument string.
2. Use the syscall helpers for output, keyboard input, and filesystem access.
3. Register the command name in the app dispatcher.
4. Keep memory use bounded and avoid desktop-scale assumptions.

This model is appropriate for commands such as grep, wc, sort, and hexdump.

## 5. Syscall-Style Helpers

The app layer relies on a compact helper interface for portability. Common operations include:

- console output
- character input
- current working directory access
- file reads and writes
- color or terminal-style state where available

These helpers allow Mellivora-style utilities to be ported without depending directly on every hardware module.

## 6. Writing BASIC Programs

BASIC is suited to small interactive programs and experiments.

Example:

```text
10 INPUT A
20 LET B = A * 2
30 PRINT B
40 END
```

Tips:

- use line numbers for stored programs
- use LIST to inspect the program in memory
- use NEW to clear the current program
- use IF ... THEN for branching

## 7. Writing Tiny C Programs

Tiny C is aimed at short integer-driven scripts.

Example:

```text
int n = 1;
print(n);
n = n + 1;
if (n == 2) print(n);
vars
```

Useful commands in the environment:

- help — show quick syntax help
- vars — list current variables
- exit — leave the Tiny C prompt

## 8. Embedded Programming Guidelines

When extending the firmware:

- prefer fixed-size buffers
- validate all user input
- avoid recursion unless clearly bounded
- keep file operations simple and robust
- assume limited RAM and interactive latency constraints

## 9. Porting Guidance from Older Mellivora Apps

When converting an older Mellivora application to PicoCalc:

1. identify the user-facing behavior to preserve
2. replace hardware- or x86-specific logic with syscall helpers
3. simplify unsupported features
4. keep the command syntax recognizable
5. test with real files and real shell input

## 10. Recommended Next Extensions

Good future targets include:

- more text processing utilities
- richer BASIC statements
- more Tiny C syntax coverage
- file editors and interactive tools tuned for the PicoCalc keyboard and display
