# Changelog

All notable changes to **Mellivora OS for PicoCalc** are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] — 2.4.0 development

### Changed (2.4.0-dev)

- **Unified visual language** — new `ui/` module provides semantic colour
  roles (text, muted, accent, success, warning, error, border, panel,
  selection, status) mapped through ten themes. The prompt, status bar,
  `app_*` headers, `ls`, `help`, `settings`, editor feedback, and the
  `home` launcher now share one palette and one menu widget. The file
  browser gained a themed title bar, inverted selection row, aligned
  size column, a key-hint footer, and a bounded preview pane for the
  highlighted file or directory; the editor uses muted line numbers
  and a colour-coded modified/saved status. Default theme is `noir`;
  all previous theme names still work.
- **Modular app layer** — `apps.c` is now a small registry; command
  implementations live in `apps/app_util.c`, `app_settings.c`,
  `app_editor.c`, `app_langs.c`, `app_productivity.c`, `app_games.c`,
  `app_system.c`, `app_builtins.c`, and `app_tools.c`, wired through the
  private `apps/app_internal.h` interface. Dead-code elimination now
  drops unused handlers per object file, shrinking firmware by ~3.5 KB.


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
- **Scripting upgrades** — `script FILE` and `. FILE` now execute each line
  through the full shell dispatcher, so variables, aliases, `;`/`&&`/`||`,
  redirection, pipes, and wildcards work inside scripts. Nested scripts are
  limited to four levels.
- **Arithmetic expansion** — `$((EXPR))` is evaluated before dispatch,
  including shell variables and integer comparisons/bitwise operators.
- **Wildcard expansion** — unquoted `*`, `?`, `[abc]`, and `[!abc]` patterns
  run the command once per match (up to 16). `find -name` also accepts these
  patterns.
- **`read [-p TEXT] NAME...`** — read a line into one or more shell
  variables.
- **`which` / `type`** — locate a shell command, app, alias, or file.
- **`expr EXPR`** — evaluate an integer expression from the shell.
- **Startup profile and prompt** — `/PROFILE.CFG` runs at boot, and the
  `PROMPT` variable supports `%c` (cwd), `%t` (time), `%b` (battery),
  `%w` (WiFi), and `%%`.
- **VFAT long file names (read-only)** — `ls` and the `browse` file
  browser display long names, and file/directory paths may use them.
  Lookup matches long names first, then falls back to the 8.3 alias.
  Creating or renaming to a long name still stores an 8.3 alias.
- **General pipelines** — `cmd1 | cmd2 | cmd3` exchanges data through two
  bounded RAM buffers. `cmd | more` keeps the streaming pager for large
  outputs. Text commands accept `-` or an omitted file to read the
  upstream stage.
- **`tr [-d] SET1 [SET2] [FILE]`** — translate or delete characters with
  ranges (`a-z`) and escapes (`\n`, `\t`, `\xNN`).
- **`sed s/OLD/NEW/[g] [FILE]`** — literal line substitution; reads from a
  file or the pipeline.
- **`md` / `markdown`** — render headings, lists, quotes, code fences, and
  links for the LCD.

### Added (engineering)

- **Host unit tests** — `make test` builds the pure modules with the
  system compiler and runs the parser/path/name suites under ASan/UBSan.
- **Static analysis and formatting** — `make lint` (clang-tidy) and
  `make format-check` (clang-format) cover `src/core` and the tests.
- **`make size-baseline` / `make size-gate`** — reproducible size
  baselines and an enforceable regression gate.

### Changed (2.4.0-dev)

- **Testable core modules** — extracted the expression evaluator, shell
  line parsing (`$VAR`, `;`/`&&`/`||`, redirection, pager pipes), path
  joining/canonicalisation, 8.3 name conversion, and the simple regex
  matcher into `picocalc/src/core/`. Firmware behaviour is unchanged.
- **Shared console and audio modules** — `console_read_line()` and
  `audio_beep()` now back both the app layer and the production HAL, so
  the HAL's `read_line` and `beep` slots are no longer `NULL`.
- **Warnings are errors** — every Mellivora source is compiled with
  `-Wall -Wextra -Wshadow -Wundef -Werror` (`-DMELLIVORA_WERROR=OFF`
  opts out). Pico SDK sources are excluded from the strict flags.
- **Real size gate** — CI checks flash and RAM budgets for each target
  and fails on growth beyond the committed baselines.

### Fixed (2.4.0-dev)

- **Redirection captures app output** — output written through
  `sys_print`/`sys_putchar` is now routed through the shell capture hook,
  so `cmd > FILE` and pipeline stages include app output, not just shell
  builtins.
- **RP2350 capacity macros now apply consistently** — `apps.h` and
  `picocalc_hw.h` tested `PICO_RP2350A` before the Pico SDK board header
  defined it, so some translation units silently used the smaller RP2040
  buffer sizes (editor, BASIC store, paint canvas, read buffers) while
  others used the larger RP2350 ones. The checks now also accept the
  platform-level `PICO_RP2350` define, which is present in every compile.
  Pico 2 and Pico 2 W regain their intended capacities and 40 MHz LCD SPI.
- **Pico 2 W freeze with keyboard firmware 1.6** — serialised all
  keyboard-controller I2C access with a cross-core mutex so core0 input
  polling and core1 status-bar battery reads cannot corrupt the I2C
  state machine.  Also hardened bus recovery (de-init, clock SDA free,
  STOP, re-init) and aligned I2C timeouts with the Clockwork reference
  driver.

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
  (epoch < 2020); result persisted to `/CLOCK.CFG`.
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
