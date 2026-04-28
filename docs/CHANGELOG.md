# Changelog

All notable changes to **Mellivora OS for PicoCalc** are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.2.1] - 2025-04-27

### Added (2.2.1)

- **Multi-AP WiFi credential store** — saves up to 4 networks to
  `/WIFI.CFG`, automatically reconnects to any reachable saved network at
  boot. New subcommands: `wifi saved`, `wifi forget <SSID>`. LRU ordering:
  most recently connected network is tried first.
- **Boot-time NTP sync** — after WiFi auto-connect succeeds, if the
  software RTC is unset (epoch < 2020) the system contacts `pool.ntp.org`
  and persists the result to `/CLOCK.TXT`.
- **Shell variables** — `set NAME=VALUE`, `unset NAME`, `set` lists all.
  `$NAME` and `${NAME}` are expanded in command lines (single-quoted
  strings are preserved literally). 16-slot store.
- **`screenshot` command** — dumps the LCD text buffer to `/SCREEN.TXT`.
- **Calculator hex/bin/oct literals** — `0x..`, `0b..`, `0o..` accepted
  in expressions, including BASIC and TinyC.
- **Stopwatch app** (`stopwatch` / `timer`) — Space=start/stop, L=lap,
  R=reset, Q=quit. Millisecond resolution.
- **Pomodoro app** (`pomodoro [work] [break]`) — 25/5 min default, custom
  durations, backlight-flash chime between phases.

### Changed (2.2.1)

- Tab completion now includes `stopwatch`, `timer`, `pomodoro`,
  `screenshot`, `set`, `unset`.

### Fixed (2.2.1)

- `wifi connect` success path now persists credentials so they survive
  reboot and are eligible for auto-connect.

## [2.2.0] - 2025-04-27

### Added (2.2.0)

- Versioned `MELLIVORA_VERSION` macro baked into builds
- `version` command — prints firmware version, build date, and project URL
- `uname -a` — extended system info (LCD, KBD, SD, UART, WLAN, build date)
- Persistent shell history across reboots (saved to `/HISTORY.LOG`)
- Hardware watchdog timer (3 s) — auto-recovers from lockups
- `shutdown` / `halt` / `poweroff` — clean shutdown that flushes state and halts
- `sleep` (no args) — display-off low-power mode (any key wakes)
- Idle backlight dimming after 60 s of inactivity
- `rm -r` / `rm -R` — recursive directory removal with confirmation
- NTP sync now sets the software RTC automatically
- Status bar shows red `[BAT n%!]` warning when battery < 15 %
- Tab-completion knows about new commands (`shutdown`, `version`, etc.)

### Changed (2.2.0)

- `cmd_uname` now accepts an optional `-a` argument for verbose output
- Banner now reports the active firmware version

### Fixed (2.2.0)

- (none — no regressions identified)

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
