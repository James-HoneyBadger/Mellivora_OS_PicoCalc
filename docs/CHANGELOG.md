# Changelog

All notable changes to **Mellivora OS for PicoCalc** are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] — 2.4.0 development

### Added (2.4.0-dev)

- **`uniq` command** — filter adjacent duplicate lines from a file.
  Supports `-c` (count), `-d` (only duplicates), `-u` (only unique).
- **`less` command** — alias for `pager`; brings the name up to Unix
  convention (tab-completion and `help` both show it).
- **`umount` / `unmount` commands** — safely mark the SD card unmounted
  so it can be hot-removed without filesystem corruption.
- **Dot-source (`. FILE`)** — execute a script file in the current
  shell session; full alias for `script`.
- **`time COMMAND`** — measure elapsed wall-clock time for any command
  and print `real  N.NNN s` afterwards.
- **`repeat N COMMAND`** — run a command N times (max 1000); honours
  Ctrl-C interruption.
- **`printf FORMAT [ARGS...]`** — formatted output supporting `%d`, `%u`,
  `%x`, `%X`, `%s`, `%%`; recognises `\n` and `\t` in the format string.
- **`$?` last-exit variable** — expands to 0 after a successful command
  or 127 after an unknown command; usable anywhere `$NAME` is accepted.
- **`base64` command** — encode a file to standard Base64 output; `-d`
  flag decodes back.
- **`crc32` command** — compute CRC-32/ISO-HDLC checksum of a file,
  printed as 8 hex digits.
- **Minesweeper game** (`minesweeper` / `mines`) — 9×9 grid, 10 mines.
  Arrow keys to move cursor; Enter to reveal; `f` to toggle flag; `r` to
  restart; `q` to quit.
- **2048 game** (`2048`) — 4×4 sliding-tile puzzle.  Arrow keys slide
  tiles; tiles merge when equal; aim for 2048.
- **New BASIC samples**: `bounce.bas` (bouncing ball), `roman.bas`
  (Roman numerals 1–20), `stats.bas` (mean/min/max/range), `loan.bas`
  (loan amortisation table).
- **New TinyC samples**: `roman.tc`, `stack.tc` (stack via array),
  `sieve2.tc` (primes up to 100).
- **Ctrl-W** word-delete — deletes the word to the left of the cursor in
  the shell input line (complements existing Ctrl-A/E/K/U/L/R bindings).

---

## [2.3.0] - 2026-04-27

First tagged release of the 2.x feature wave covering reliability,
networking, productivity, and CI improvements.

### Added (2.3.0)

- **Versioning** — `MELLIVORA_VERSION` macro baked into builds; new
  `version` command; `uname -a` reports LCD/KBD/SD/UART/WLAN and build
  date; banner shows the active firmware version.
- **Hardware watchdog** (3 s) — tickled in the shell loop to auto-recover
  from lockups.
- **Persistent shell history** — saved to `/HISTORY.LOG` and reloaded at
  boot.
- **`shutdown` / `halt` / `poweroff`** — clean shutdown that flushes
  state and halts the CPU.
- **`sleep`** (no args) — display-off low-power mode; any key wakes.
- **Idle backlight dimming** — after 60 s of inactivity.
- **`rm -r` / `rm -R`** — recursive directory removal with confirmation.
- **NTP RTC sync** — `ntp` command sets the software RTC; **boot-time
  auto-sync** runs after WiFi auto-connect when the RTC is unset
  (epoch < 2020); result persisted to `/CLOCK.TXT`.
- **Status bar low-battery warning** — red `[BAT n%!]` when battery < 15
  % and not charging.
- **Multi-AP WiFi credential store** — saves up to 4 networks to
  `/WIFI.CFG`, automatically reconnects to any reachable saved network
  at boot. New subcommands: `wifi saved`, `wifi forget <SSID>`. LRU
  ordering: most recently connected network is tried first.
- **Shell variables** — `set NAME=VALUE`, `unset NAME`, `set` lists all.
  `$NAME` and `${NAME}` are expanded in command lines (single-quoted
  strings preserved literally). 16-slot store.
- **`screenshot` command** — dumps the LCD text buffer to `/SCREEN.TXT`.
- **Calculator hex/bin/oct literals** — `0x..`, `0b..`, `0o..` accepted
  in expressions, including BASIC and TinyC.
- **Stopwatch app** (`stopwatch` / `timer`) — Space=start/stop, L=lap,
  R=reset, Q=quit. Millisecond resolution.
- **Pomodoro app** (`pomodoro [work] [break]`) — 25/5 min default, custom
  durations, backlight-flash chime between phases.
- **Documentation set** — `docs/CHANGELOG.md`, `docs/CONTRIBUTING.md`,
  `docs/ARCHITECTURE.md`, `docs/HARDWARE.md`, `docs/COMMANDS.md`.
- **CI/CD** — `.github/workflows/build.yml` matrix-builds all three
  targets on every push/PR; `.github/workflows/release.yml` publishes
  UF2s + SHA256SUMS on every `v*.*.*` tag; issue and PR templates;
  `make all-targets` builds every variant.

### Changed (2.3.0)

- `cmd_uname` accepts an optional `-a` for verbose output.
- Tab completion includes `shutdown`, `halt`, `poweroff`, `version`,
  `stopwatch`, `timer`, `pomodoro`, `screenshot`, `set`, `unset`.
- `wifi connect` success path now persists credentials so they survive
  reboot and are eligible for auto-connect.
- README, ABOUT, USER_GUIDE language tightened across the doc set.

### Fixed (2.3.0)

- Markdownlint findings (MD022/MD024/MD032/MD040/MD060) across the
  documentation set.
- `make all` previously built only the RP2040 target; CI release now
  uses the new `make all-targets`.

## [2.1.0] - 2025-04 (prior to changelog)

- Persistent CWD across reboots
- Reverse history search (Ctrl-R)
- Ctrl-C cancellation for long-running commands
- `cp -r` — recursive copy
- `dmesg` — kernel log buffer
- Software RTC (`date`, `clock`)

## [2.0.0] - 2025-03 (prior to changelog)

- Multi-target build (RP2040, RP2350, Pico 2W)
- WiFi stack (HTTP, IRC, NTP, DNS, telnet)
- BASIC and Tiny C interpreters
- Editor with line numbers, paint, sprite editor
- Persistent notes/todo/journal/habits/bookmarks
- FAT16/FAT32 read/write driver

## [1.0.0]

- Initial public release
