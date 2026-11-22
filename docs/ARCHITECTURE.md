# Mellivora OS Architecture

A single-image, bare-metal C11 firmware for the Clockwork PicoCalc. All builds
use Core 1 for background services; RP2350 builds provide more SRAM for the
heap and RAM disk. Hardware interfaces include the LCD, keyboard controller,
SD card, and (on Pico 2 W) Wi-Fi.

## Boot Flow

```text
power-on / reset
   |
   v
.boot2 (pico-sdk)  -- second-stage bootloader, sets up flash/XIP
   |
   v
runtime0 (pico-sdk) -- vector table, .data copy, .bss zero
   |
   v
main()  [src/main.c]
   |- set_sys_clock_khz()             # 133 MHz on RP2040, 150 MHz on RP2350
   |- stdio_init_all()                # USB-CDC + UART
   |- gpio_init(PICO_PIN_PS) -> 1     # power-stable pin high
   |- kbd_init()                      # I2C1 to STM32 co-processor
   |- lcd_init()                      # SPI to ILI9488 + framebuffer
   |- sleep_ms(1200)                  # allow SD card to settle
   |- sd_init() / fat_mount()         # optional; shell remains available on failure
   |- alias_load() / var_load()       # restore shell configuration
   |- app_init() / cwd_restore() / clock_restore() / hist_load()
   |- run /PROFILE.CFG and app startup hooks when storage is mounted
   |- (Pico 2W) connect to saved Wi-Fi network; sync time if needed
   |- show today's planner entries once when the clock is valid
   |- multicore_launch_core1(_core1_entry)   # status bar refresh every 2 s
   |- watchdog_enable(8000, true)
   |
   v
shell loop:
   read_input -> dispatch -> hist_save -> screen redraw
```

## Module Layout (`picocalc/src/`)

| File           | Purpose                                                       |
|----------------|---------------------------------------------------------------|
| `main.c`       | Shell, command dispatch, REPL, persistence, completion        |
| `apps.c/.h`    | App registry: name table, startup hooks, dispatch              |
| `apps/app_*.c` | App implementations grouped by settings, editor, tools, games, and other features |
| `apps/app_util.c` | Shared formatting, option parsing, file/path, calendar helpers |
| `apps/app_settings.c` | Persistent settings and themes |
| `apps/app_editor.c` | Line editor |
| `apps/app_langs.c` | BASIC, TinyC, calculator, scripts, samples |
| `apps/app_productivity.c` | Notes, tasks, planner, journal, habits, bookmarks |
| `apps/app_games.c` | Games and toys |
| `apps/app_system.c` | Calendar, clock, dashboard, launcher, timers |
| `apps/app_builtins.c` | Small standalone commands, backup, and environment storage |
| `apps/app_tools.c` | File/system tools, media toys, Forth |
| `apps/app_mpkg.c` | Package manager: HTTPS index, SHA-256 verification, SD installs (Pico 2 W only) |
| `apps/app_hw.c` | Hardware Workbench: VT100 terminal, GPIO/PWM/I2C commands, XMODEM/YMODEM |
| `apps/text.c`, `apps/fs.c`, `apps/hex.c` | Text, filesystem, and hex utilities |
| `mapp.c`       | `.mapp` loader: discovery, RAM arena, relocation, and the app ABI jump table |
| `core/`        | Portable logic with host tests: expressions, shell parsing, paths, names, regex, globs, text, history, date conversion, JSON, SHA-256, file hashing, indexed graphics surfaces, package planning, theme files and discovery, `.mapp` image validation, pin map, VT100, XMODEM, i2cdetect, and workbench commands |
| `ui/ui.c/.h`   | Semantic colour roles, built-in and card-loaded palettes, header/footer, menus, notifications |
| `pipe.c/.h`    | Bounded in-RAM pipeline input/output                         |
| `console.c/.h` | Blocking line input shared by apps and the HAL                |
| `audio.c/.h`   | PWM tone output on the stereo pins                            |
| `kbd.c/.h`     | I2C driver for STM32 co-processor; key repeat; backlight       |
| `lcd.c/.h`     | ILI9488 SPI driver; software framebuffer; ANSI parser          |
| `sd.c/.h`      | Raw SPI SD-card block driver (CMD0..CMD58)                     |
| `fat.c/.h`     | FAT16/FAT32 read/write (ls, mkdir, open, write, unlink)        |
| `net.c/.h`     | TCP/UDP/DNS/NTP/HTTP and verified HTTPS over lwIP (CYW43 only) |
| `netapps.c/.h`| User-facing network commands (`wifi`, `ping`, `ntp`, ...)      |
| `syscall.c/.h`| Cross-module facade: `sys_print`, time, RTC, interrupt flag    |
| `hal/hal.h`   | Display, filesystem, time, and audio function table            |
| `hal/hw_ops.h`| Mockable UART/I2C/GPIO/PWM/CDC operation table                 |
| `hal/hw_pico.c`| RP2040/RP2350 implementation of the workbench operations      |
| `picocalc_hw.h`| Pin map and board constants                                   |
| `lwipopts.h`  | lwIP build configuration                                       |

## Cores

- **Core 0**: shell, apps, keyboard input, and SD/FAT access.
- **Core 1**: all board targets. A bounded lock-free SPSC request queue owns
  synchronous CYW43/lwIP calls on Pico 2 W, services queued audio events,
  polls Wi-Fi, and owns LCD rendering and status-bar composition after service
  startup. LCD API calls are marshalled synchronously through the
  queue, preserving call ordering and caller buffer lifetimes.

The Core 1 queue is single-producer/single-consumer: Core 0 submits a request
and waits for completion while Core 1 runs it. Network APIs use this queue so
the polling CYW43/lwIP stack is not concurrently accessed from both cores.
Audio uses a separate asynchronous ring so note playback does not block shell
work. Load figures shown by `sysinfo` are activity estimates based on timed
work and measured idle sleeps, not hardware performance counters.

Memory-backed RAM disk capacity is allocated from the runtime heap: up to
8 KiB on RP2040 and 64 KiB on RP2350, with smaller fallback allocations when
the current heap cannot satisfy the preferred size. The linker supplies the
target-specific heap limit, so RP2350's larger SRAM also increases available
heap without changing the allocator.

## Pipelines

### LCD pipeline

```text
out_char/out_str -> ANSI parser -> framebuffer cell write
                                      |
                                      v
                            lcd_draw_cell(col,row,ch,fg,bg)
                                      |
                                      v
                       SPI DMA blit (8x16 glyph) -> ILI9488
```

### Keyboard pipeline

```text
STM32 MCU -> I2C1 reg 0x09 -> kbd_getc()
                                |
                                v
                    Ctrl/Shift translation
                                |
                                v
        kbd_get_repeat()  (software repeat after delay)
                                |
                                v
                       read_input() -> shell
```

### SD / FAT pipeline

```text
fat_*  -> resolve_path -> read_sector / write_sector
              |                |
              v                v
         dirent walk      sd_read_block / sd_write_block
                                  |
                                  v
                         bit-banged SPI signals to SD card (CS toggled)
```

VFAT long-name entries are accumulated during the directory walk and
validated against the 8.3 checksum; `resolve_path` prefers a long-name
match, then falls back to 8.3.

A single 512-byte `_sector_buf` is shared across reads, so callbacks invoked
from inside `fat_ls` MUST NOT call back into `fat_*`. Helpers like `cp -r`
and `rm -r` snapshot the directory listing first, then iterate.

### Shell pipeline

```text
input line -> alias/var expansion -> ';' / '&&' / '||' split
           -> '|' split -> per-stage glob -> redirection -> dispatch
```

Intermediate pipeline stages exchange up to 4 KiB through two ping-pong RAM
buffers. `cmd | more` bypasses capture and streams through the pager.

### Network pipeline (Pico 2W)

```text
netapps  -> net.c high-level (sync wrappers)
                |
                v
          lwIP TCP/UDP/DNS
                |
                v
        cyw43_arch (CYW43439 driver, SDIO over PIO)
```

## Hardware Workbench

The workbench is split into three layers so the interesting behaviour is
testable without hardware:

```text
core/pinmap.c      which GPIO belongs to which driver, where a new
                   UART/I2C/SPI can be placed
core/workbench.c   the actual commands (gpio, pwm, i2cdetect, hw pins)
core/vt100.c       terminal emulator over a caller-owned cell buffer
core/xmodem.c      XMODEM/YMODEM engines driven entirely by callbacks
core/i2cdetect.c   scan bookkeeping and the i2cdetect grid
        |
        v
hal/hw_ops.h       function-pointer table: uart_*, i2c_*, gpio_*, pwm_*, cdc_*
hal/hw_pico.c      the only file that calls the Pico SDK for these
```

`g_hw` is installed by `hw_init()` in the firmware and by
`tests/mock_peripherals.c` on the host. The mock keeps real state: output
latches, direction bits, pull registers, PWM frequency/duty per pin, UART
FIFOs, and an I2C device table per bus. `test_workbench.c` drives
`wb_gpio("5 high")` and friends and then asserts on those registers, so the
shell-facing logic is covered without a board.

Reserved pins are refused by the command layer (`pinmap_is_reserved`), which
is why `gpio 10 high` fails but `gpio --force 10 high` works. The terminal
renders only the rows reported dirty by the emulator and reserves the last
LCD row for its own status line.

## Persistence

Persistent shell and app state is stored on the SD card when it is mounted:

| File             | Owner / contents                            |
|------------------|---------------------------------------------|
| `/LASTCWD.TXT`   | Last working directory                      |
| `/HISTORY.LOG`   | Recent shell history                        |
| `/ALIASES.CFG`   | Aliases                                     |
| `/ENV.CFG`       | Shell variables                             |
| `/CLOCK.CFG`     | Persisted software clock timestamp          |
| `/SETTINGS.CFG`  | App settings and theme                      |
| `/WIFI.CFG`      | Saved Wi-Fi network credentials (plain text; Pico 2 W) |
| `/PLANNER.TXT`   | Planner entries                             |
| `/NOTES.TXT`     | Notes app                                   |
| `/TODO.TXT`      | Todo app                                    |
| `/JOURNAL.TXT`   | Journal app                                 |
| `/HABITS.CFG`    | Habit tracker                               |
| `/BOOKMARKS.CFG` | Bookmark + CWD pairs                        |
| `/HISCORE.TXT`   | Snake high score                            |
| `/MPKG.CFG`      | Package repository URL (Pico 2 W)           |
| `/apps/repo.json`| Cached package index (Pico 2 W)             |
| `/apps/.mpkg-state.json` | Installed package names, versions, digests |
| `/themes/NAME.thm` | User palettes loaded at boot              |
| `/CA.CRT`        | PEM certificate authority for HTTPS         |

The shell can run without an SD card, but persistence and storage-backed
apps are unavailable until a card is mounted. Wi-Fi passwords in
`/WIFI.CFG` are not encrypted; backups preserve that file and should be
protected accordingly. Backup snapshots use a `FORMAT.TXT` version marker
and restore only missing files.

## Loadable apps (`.mapp`)

`/apps` is enumerated at boot; `.mapp` modules appear in the launcher next
to the `.bas`, `.c`, and `.fth` scripts, which the loader also runs by
delegating to the matching interpreter. A module is never linked into the
firmware, so adding one needs no reflash.

The loader is split so its memory-safety decisions are testable:

- `core/mapp_image.c` decodes and validates the 60-byte header (magic,
  version, sizes, entry point, relocation count) and applies Thumb
  relocations. It owns no FAT, display, or allocator state. Every bounds
  check uses 64-bit arithmetic so a crafted header cannot wrap an offset
  past the arena, and a relocation writes nothing unless the whole table
  validates.
- `mapp.c` does the firmware half: it `malloc`s the 16 KiB arena per
  launch (heap, not `.bss`, so the non-`.mapp` images do not pay for it),
  reads the image, and calls the entry point with the API table.

The ABI is a fixed jump table (`mapp_api_t`) of display, keyboard, file,
timer, and network calls. It grows by appending only: `size` and `version`
let an app discover what the running loader implements, and
`capabilities()` reports `MAPP_CAP_*` bits. Compile-time asserts in
`mapp.h` pin the v1 prefix so a future edit cannot silently move a field
an older binary still reads.

Version 2 added the graphics layer, and version 3 the polyphonic audio:

- `core/gfx.c` implements indexed 8-bit surfaces with a 256-entry palette,
  clipped primitives, masked sprite blits, glyph drawing, and dirty-rectangle
  tracking. It is host-tested and knows nothing about the panel.
- `gfx_lcd.c` bridges surfaces to the panel: `lcd_blit_region()` opens one
  SPI window for the whole region and expands each row through the palette,
  so a present is a single Core 1 request that streams only the dirty
  rectangle. A full 320x320 present would be ~307 KiB of SPI traffic; the
  160x160 mode is 25.6 KiB per surface and is the recommended default.
- `core/mixer.c` is a host-tested 4-voice mixer (square/triangle/noise, AD
  envelopes, per-voice seeds). `audio.c` runs the two PWM slices as an 8-bit
  DAC (carrier ~520 kHz) and drives them from a 16 kHz timer on core 0, which
  also owns the request queue - so all mixer mutation happens in one place.
- `mapp.c` hands out up to four surfaces per app from the heap and releases
  them when `mapp_main` returns, so a leak cannot survive a launch; the ABI
  exposes `gfx_*`, `audio_tone`, and the v3 `audio_note`/`audio_voice_stop`
  voices. `gfxbench` measures the real panel throughput behind those calls.
- BASIC exposes the same layers as statements: `GR`/`TEXT` manage a surface
  and `PSET`/`LINE`/`RECT`/`FRECT`/`CIRCLE`/`FCIRCLE`/`COLOR`/`PEN`/`SHOW`
  draw on it, while `SOUND`/`NOISE`/`NOSOUND` drive the mixer. The
  interpreter presents the dirty region once per program line, so a ten-line
  program is a working animation with no toolchain.

Host-side developers use `tools/sdk/`: `make` produces `hello.mapp` and
`plasma.mapp` from plain C, and the packer turns any linked ELF into the
container format.

## Packages and user themes

`mpkg` is split so the non-Wi-Fi images stay identical in size:

- `core/json.c` parses the repository index and the install state inside a
  caller-provided arena, so a document is released in one step.
- `core/mpkg.c` owns the data model: version ordering, kind-to-directory
  mapping, install-state serialisation, and upgrade planning. It has no
  network, filesystem, or display dependency and is covered by host tests.
- `core/sha256.c` verifies artifacts. It is self-contained rather than
  routed through the TLS library so the digest is independent of the
  mbedTLS configuration. `core/filehash.c` streams a whole file through it,
  which is what `mpkg verify` digests installed artifacts with; the FAT
  driver reports end-of-file as `FAT_ERR_EOF` rather than `0`, so that
  conversion happens in one place.
- `apps/app_mpkg.c` performs the downloads. Its entire body is inside
  `#if defined(PICO_CYW43_SUPPORTED)`; the RP2040 and Pico 2 images link
  only a short stub, and `mpkg` is not advertised in their help registry.

Downloads reuse `net_https_get`, which already runs on the Core 1 network
queue. Buffers are heap allocated per command (arena, index, artifact), so
the package manager adds no permanent SRAM.

User themes are data, not code. `core/theme_file.c` parses
`/themes/NAME.thm` (`role = RRGGBB` lines) and `ui_theme_register()`
appends up to four user palettes after the built-ins. Loading happens in
`app_boot()` because the card must be mounted first; the saved theme is
re-applied afterwards.

`fat_ls_ex()` runs its visitor while the directory walk is borrowing the
FAT driver's shared sector buffer, so a visitor may not read files. The
`theme_scan_visit()` helper in `core/theme_file.c` only records names;
`app_settings_load_user_themes()` loads the palettes once the enumeration
has finished.

## Watchdog & Recovery

A hardware watchdog with an 8-second timeout is enabled after startup. The
shell services it during normal operation; long-running code must continue
to service it. A timeout resets the device.

## Adding a New Command

1. Write `static void cmd_foo(const char *arg) { ... }` in `main.c`.
2. Add a dispatch entry: `else if (!strcmp(line, "foo")) cmd_foo(arg);`
3. Add `"foo"` to the tab-completion array.
4. Document it in [`COMMANDS.md`](COMMANDS.md).
5. Add a CHANGELOG entry.

## Adding a New App

1. Implement `void app_foo(const char *arg)` in a suitable module under
   `src/apps/` and declare it in `apps/app_internal.h`.
2. Register `{"foo", app_foo}` in the app table in `apps.c`.
3. Add the command to the shell's completion metadata in `main.c`.
4. Document and changelog.
5. If the app grows reusable pure logic, move it to `src/core/` and add
   host tests under `picocalc/tests/`.
