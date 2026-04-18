# Mellivora PicoCalc Technical Reference

This document describes the firmware architecture, subsystem boundaries, runtime flow, persistence model, and current command surface for Mellivora PicoCalc.

## 1. Hardware Target

Primary supported platform:

- RP2040-based Clockwork PicoCalc class hardware

Main interfaces:

- LCD via SPI1
- keyboard and battery interface via I2C1
- SD card block access via SPI0
- USB CDC plus UART console output

## 2. Firmware Organization

The active codebase lives under `picocalc/src/`.

Key modules:

- `main.c` — shell core, built-ins, help system, history, aliases, boot flow
- `apps.c` — application layer, utility ports, editor, dashboards, games, interpreters
- `fat.c` and `fat.h` — FAT filesystem operations and capacity reporting
- `syscall.c` and syscall headers — app-facing portability layer
- `sd.c` — block-level storage transport
- `lcd.c` — screen drawing and mirrored output
- `kbd.c` — keyboard input, battery reporting, and backlight control

## 3. Runtime Flow

The normal runtime sequence is:

1. initialize hardware and platform state
2. establish the display and console output path
3. bring up shell state such as current directory and history
4. accept and parse user commands
5. dispatch either to built-in handlers or app-layer handlers
6. write persistent state to SD-backed files where needed

## 4. Shell Core Characteristics

Notable shell properties:

- fixed-size line buffer
- history ring with replay support
- persistent alias storage
- relative and absolute path handling
- mirrored output to both device display and serial console
- topic-based built-in help through `help` and `man`

## 5. Filesystem Model

The filesystem layer is FAT-backed and activated by running `mount`.

Provided capabilities include:

- mount detection and initialization
- directory listing
- file opening and reading
- creation and overwrite support
- directory creation
- file removal and empty-directory removal
- tree and usage reporting
- total, used, and free capacity reporting through `df`

Users are expected to mount the SD card before relying on persistent apps or file tools.

## 6. Persistent Application Files

Several applications store state as normal files on the SD card. Important examples include:

- `SETTINGS.CFG`
- `TODO.TXT`
- `PLANNER.TXT`
- `BOOKMARKS.CFG`
- `ALIASES.CFG`
- journal and habits backing files

This plain-file approach keeps the system transparent and easy to recover manually.

## 7. Built-in Command Families

### Shell and system

- help
- man
- history
- alias
- unalias
- uname
- uptime
- clear
- battery
- backlight
- reboot

### Filesystem and storage

- mount
- ls and dir
- cd
- pwd
- cat
- touch
- write
- mkdir
- rm
- sdinfo
- sdread

### App-layer and utility surface

The app layer currently exposes a broad command set including:

- text tools such as `head`, `tail`, `wc`, `cut`, `grep`, `pager`, `rev`, `sort`, `hexdump`, and `od`
- file tools such as `find`, `tree`, `du`, `df`, `cp`, `mv`, `stat`, `edit`, `hexedit`, and `browse`
- personal tools such as `notes`, `journal`, `habits`, `todo`, `planner`, and `bookmarks`
- launch and status tools such as `home`, `dashboard`, `sysmon`, `settings`, `set`, and `samples`
- creative and interactive tools such as `paint`, `sprite`, and `terminal`
- games and fun commands such as `games`, `snake`, `dice`, `coin`, and `guess`
- language tools such as `calc`, `basic`, and `tcc`

## 8. BASIC Runtime Notes

The BASIC environment is intentionally minimal and device-friendly.

Supported areas include:

- line-numbered program entry
- `RUN`, `LIST`, and `NEW`
- integer variables
- `PRINT`, `LET`, and `INPUT`
- `IF ... THEN`
- `FOR ... NEXT`
- `GOSUB` and `RETURN`
- `CLS`, `GOTO`, `END`, and `STOP`

Typical execution modes:

- live REPL use
- in-memory stored programs
- file-based program execution from the SD card

## 9. Tiny C Runtime Notes

The Tiny C environment is a compact integer-focused scripting environment rather than a full hosted C implementation.

Supported areas include:

- integer declarations
- small arrays
- assignment and arithmetic
- bitwise operations
- comparisons and conditions
- `if` and `while`
- `print`, `puts`, `vars`, and `clear`

Main limits:

- integer-only model
- no heap or dynamic linking
- no separate compilation model
- intentionally small syntax surface

## 10. Resource and Design Constraints

This firmware is shaped by embedded constraints:

- limited RAM
- handheld interaction speed
- simple text rendering surface
- bounded buffer design
- minimal dependency footprint

These limits strongly influence implementation style and are a key reason many tools prefer small text formats and simple menus.

## 11. Build Artifact

The normal firmware output is:

```text
picocalc/build/mellivora_picocalc.uf2
```

## 12. Scope and Direction

This repository now centers on the PicoCalc firmware target. The maintained direction is a practical RP2040 handheld shell environment with apps and interpreters, not the older desktop-oriented runtime flow.
