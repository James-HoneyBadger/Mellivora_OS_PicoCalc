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

- `main.c` — boot flow, shell core, built-ins, command dispatch, persistence
- `apps.c` — app registry and dispatch; implementations are split across `apps/app_*.c`
- `apps/fs.c`, `apps/text.c`, and `apps/hex.c` — filesystem, text, and hex utilities
- `apps/app_shared.c` and `apps/app_util.c` — shared app helpers
- `core/` — portable, host-tested parsing, path, text, filename, history, and date logic
- `fat.c` and `fat.h` — FAT16/FAT32 filesystem operations
- `syscall.c` and syscall headers — app-facing portability and I/O helpers
- `sd.c` — SD-card block transport
- `lcd.c` — display drawing and console rendering
- `kbd.c` — keyboard input, battery reporting, and backlight control
- `hal/` — hardware abstraction used during platform initialization
- `ui/ui.c` — semantic colors, menus, headers, footers, and notifications
- `net.c` and `net.h` — Wi-Fi, TCP, DNS, ICMP, NTP, HTTP, and verified HTTPS (Pico 2W only)
- `netapps.c` and `netapps.h` — network application commands (Pico 2W only)

## 3. Runtime Flow

The normal runtime sequence is:

1. set the board clock and initialize stdio, HAL, keyboard, and display
2. initialize and mount the SD card when present; storage failure does not
   prevent the shell from starting
3. restore shell aliases, variables, settings, working directory, clock, and
   history when the filesystem is mounted
4. run `/PROFILE.CFG` and configured startup hooks
5. on Pico 2 W, attempt a saved Wi-Fi connection and synchronize time when
   the software clock is not yet valid
6. start the RP2350 status-bar refresh task, enable the watchdog, and enter
   the interactive shell

## 4. Shell Core Characteristics

Notable shell properties:

- fixed-size line buffer (256 bytes)
- history ring with replay support (`!!`, `!N`)
- persistent alias and variable storage on the SD card
- alias expansion protection against recursive expansion
- tab completion of command names
- command chaining via `;`, `&&`, and `||` (up to 16 segments per line)
- pipelines with up to 4 stages and bounded intermediate output
- output redirection: `>` (truncate) and `>>` (append) to SD files
- double-quoted argument grouping to preserve spaces
- Ctrl-C interrupt flag for cancelling the current input line
- relative and absolute path handling
- mirrored output to both device display and serial console
- topic-based built-in help through `help` and `man`
- searchable command lookup through `help -k KEYWORD`
- software key repeat (500ms delay, 100ms rate)

## 5. Filesystem Model

The filesystem layer is FAT16/FAT32-backed and activated by running `mount`.

Provided capabilities include:

- mount detection and initialization
- directory listing
- file opening, sequential reading, creation, and appending
- whole-file replacement through atomic-write support for eligible callers
- in-place rename support (`fat_rename`, same directory)
- directory creation
- file removal and empty-directory removal
- recursive file and directory operations exposed by shell utilities
- tree and usage reporting
- total, used, and free capacity reporting through `df`
- 2-slot FAT sector cache for read performance
- SD block CRC16 validation

Users are expected to mount the SD card before relying on persistent apps or file tools.

## 6. Persistent Application Files

Shell and application state is stored as ordinary files on the SD card.
Important paths include:

- `LASTCWD.TXT` — last working directory
- `HISTORY.LOG` — recent shell history
- `ALIASES.CFG` — shell aliases
- `ENV.CFG` — shell variables
- `CLOCK.CFG` — persisted software clock timestamp
- `SETTINGS.CFG` — user preferences
- `TODO.TXT` — task list
- `PLANNER.TXT` — dated agenda items
- `BOOKMARKS.CFG` — saved bookmarks with CWD context
- `JOURNAL.TXT` — dated journal entries
- `HABITS.CFG` — habit tracking with streak data
- `WIFI.CFG` — saved Pico 2 W network credentials
- `HISCORE.TXT` — snake game high score

Text-based formats make user data accessible from another computer. Wi-Fi
passwords in `WIFI.CFG` are stored as readable text, not encrypted. Back up
important files and protect the card.

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
- sdcheck
- backup

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
- `fetch` — HTTP GET or verified HTTPS GET and display response body
- `wget` — HTTP GET or verified HTTPS GET and save response to the SD card
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
- bounded arrays and an intentionally small syntax surface

## 10. Network Layer (Pico 2W Only)

When built for the Pico 2W board (`make pico2w`), the firmware includes a WiFi networking stack.

Architecture:

- `net.c` implements the low-level network layer on top of the CYW43 WiFi driver and lwIP TCP/IP stack
- HTTPS uses Pico SDK mbedTLS and TLS 1.2 with mandatory CA and hostname
  verification. A user-provided PEM CA certificate at `/CA.CRT` and a valid
  software clock are required; there is no implicit trust-all mode.
- `netapps.c` provides user-facing commands that call into `net.c`
- Network code is conditionally compiled using `#ifdef PICO_CYW43_SUPPORTED`
- saved Wi-Fi credentials are persisted in `/WIFI.CFG` and are readable text

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

The RP2350 builds use larger selected buffers than RP2040 builds. Values
below describe compile-time capacities, not guarantees that every operation
can use the full capacity simultaneously.

| Resource | RP2040 | RP2350 | Notes |
| --- | --- | --- | --- |
| Shell line buffer | 256 bytes | 256 bytes | Input line maximum |
| Output format buffer | 512 bytes | 512 bytes | `out_fmt()` single call |
| Output redirection buffer | 4096 bytes | 8192 bytes | Max redirected output per command |
| History entries | 32 | 64 | Circular ring |
| Alias entries | 16 | 16 | Each name has 16 bytes; each value has a 256-byte buffer |
| Shell variables | 16 | 16 | Each name has 16 bytes; each value has a 256-byte buffer |
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
| FAT sector cache | 2 slots | 2 slots | Round-robin replacement for FAT table reads |
| SD CRC validation | Per-block | Per-block | CRC16-CCITT on every read |
| File names | 8.3 create, LFN read | 8.3 create, LFN read | Long names are read and listed; new files use 8.3 |
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

## 14. Scope

The maintained product is the PicoCalc firmware for RP2040 and RP2350
boards. Host builds are used for tests of portable logic; hardware drivers
and interactive display behavior still require on-device validation.
