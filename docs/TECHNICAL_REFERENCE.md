# Mellivora PicoCalc Technical Reference

This document describes the firmware architecture, subsystem boundaries, runtime flow, persistence model, and current command surface for Mellivora PicoCalc.

## 1. Hardware Target

Supported platforms:

- RP2040 — Raspberry Pi Pico on Clockwork PicoCalc
- RP2350 — Raspberry Pi Pico 2 on Clockwork PicoCalc
- RP2350 + CYW43439 — Raspberry Pi Pico 2 W on Clockwork PicoCalc

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
- `net.c` and `net.h` — WiFi, TCP, DNS, ICMP, NTP, and HTTP networking (Pico 2W only)
- `netapps.c` and `netapps.h` — network application commands (Pico 2W only)

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

- fixed-size line buffer (256 bytes)
- history ring with replay support (`!!`, `!N`)
- persistent alias storage with circular expansion protection (max depth 8)
- tab completion of command names
- command chaining via `;` separator (up to 16 segments per line)
- output redirection: `>` (truncate) and `>>` (append) to SD files
- double-quoted argument grouping to preserve spaces
- Ctrl-C interrupt flag for cancelling the current input line
- relative and absolute path handling
- mirrored output to both device display and serial console
- topic-based built-in help through `help` and `man`
- software key repeat (500ms delay, 100ms rate)

## 5. Filesystem Model

The filesystem layer is FAT16/FAT32-backed and activated by running `mount`.

Provided capabilities include:

- mount detection and initialization
- directory listing
- file opening and sequential reading
- creation and overwrite support
- append support (`fat_append`)
- in-place rename support (`fat_rename`, same directory)
- directory creation
- file removal and empty-directory removal
- recursive single-level directory copy
- tree and usage reporting
- total, used, and free capacity reporting through `df`
- 2-slot FAT sector cache for read performance
- SD block CRC16 validation
- card-detect debounce (3-read majority vote)

Users are expected to mount the SD card before relying on persistent apps or file tools.

## 6. Persistent Application Files

Several applications store state as normal files on the SD card. Important examples include:

- `SETTINGS.CFG` — user preferences
- `TODO.TXT` — task list
- `PLANNER.TXT` — dated agenda items
- `BOOKMARKS.CFG` — saved bookmarks with CWD context
- `ALIASES.CFG` — shell command aliases
- `JOURNAL.TXT` — dated journal entries
- `HABITS.CFG` — habit tracking with streak data
- `HISCORE.TXT` — snake game high score

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

### Network commands (Pico 2W only)

When built for the Pico 2W (`make pico2w`), the firmware includes WiFi networking via the CYW43 radio and lwIP stack:

- `wifi` — connect, scan, check status, or disconnect from WiFi networks
- `ping` — ICMP echo to a host
- `ifconfig` — show IP address, gateway, and DNS configuration
- `ntp` — synchronise time from an NTP server
- `dns` — resolve a hostname to an IP address
- `fetch` — HTTP GET and display response body
- `wget` — HTTP GET and save response to a file on the SD card
- `weather` — fetch weather data for a location
- `irc` — minimal IRC client
- `telnet` — minimal telnet client
- `netstat` — show network connection status

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

## 10. Network Layer (Pico 2W Only)

When built for the Pico 2W board (`make pico2w`), the firmware includes a WiFi networking stack.

Architecture:

- `net.c` implements the low-level network layer on top of the CYW43 WiFi driver and lwIP TCP/IP stack
- `netapps.c` provides user-facing commands that call into `net.c`
- Network code is conditionally compiled using `#ifdef PICO_CYW43_SUPPORTED`
- WiFi credentials are held in memory for the current session (not persisted to SD)

Capabilities:

- WiFi scanning and WPA2 connection management
- ICMP ping with round-trip timing
- DNS hostname resolution
- NTP time synchronisation
- TCP client connections
- HTTP GET with response display or file save
- Minimal IRC and telnet clients
- Network status reporting

The lwIP configuration is in `lwipopts.h` and is tuned for the constrained RAM environment.

## 11. Resource and Design Constraints

This firmware is shaped by embedded constraints:

- limited RAM
- handheld interaction speed
- simple text rendering surface
- bounded buffer design
- minimal dependency footprint

These limits strongly influence implementation style and are a key reason many tools prefer small text formats and simple menus.

## 12. Limits and Constraints

Many limits differ between the RP2040 (Pico) and RP2350 (Pico 2 / Pico 2W) builds because the RP2350 has more RAM (520 KB vs 264 KB).

| Resource | RP2040 | RP2350 | Notes |
| --- | --- | --- | --- |
| Shell line buffer | 256 bytes | 256 bytes | Input line maximum |
| Output format buffer | 512 bytes | 512 bytes | `out_fmt()` single call |
| Output redirection buffer | 4096 bytes | 8192 bytes | Max redirected output per command |
| History entries | 32 | 64 | Circular ring |
| Alias storage | 2560 bytes | 2560 bytes | All aliases combined |
| Alias expansion depth | 8 | 8 | Prevents circular aliases |
| Command chain segments | 16 | 16 | Max `;`-separated commands |
| LCD text grid | 40 cols × 20 rows | 40 cols × 20 rows | 8×16 pixel font on 320×320 |
| Editor lines | 128 | 256 | Max lines per file in editor |
| BASIC program lines | 128 | 256 | Max BASIC line-numbered lines |
| BASIC variables | 26 | 26 | A through Z, integer only |
| Tiny C variables | 64 | 64 | Named variables, integer only |
| Todo items | 64 | 64 | Max todo entries |
| Planner items | 96 | 96 | Max planner entries |
| Habits items | 32 | 32 | Max tracked habits |
| Bookmarks | 32 | 32 | Max saved bookmarks |
| Journal entries | 96 | 96 | Max journal entries |
| File read buffer | 4096 bytes | 8192 bytes | Max file size for in-memory tools |
| FAT sector cache | 2 slots | 2 slots | LRU for FAT table reads |
| SD CRC validation | Per-block | Per-block | CRC16-CCITT on every read |
| File names | 8.3 format | 8.3 format | Uppercase, no long filenames |
| Key repeat delay | 500ms | 500ms | Initial delay before repeat |
| Key repeat rate | 100ms | 100ms | Interval between repeats |

## 13. Build Artifact

The normal firmware output is:

```text
picocalc/build/mellivora_picocalc.uf2
```

For Pico 2 (RP2350):

```text
picocalc/build-pico2/mellivora_picocalc_pico2.uf2
```

For Pico 2W (RP2350 with WiFi):

```text
picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2
```

## 14. Scope and Direction

This repository now centers on the PicoCalc firmware target. The maintained direction is a practical RP2040/RP2350 handheld shell environment with apps and interpreters, not the older desktop-oriented runtime flow.
