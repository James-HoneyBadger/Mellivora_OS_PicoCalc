# Mellivora OS Architecture

A single-image, bare-metal C11 firmware for the Clockwork PicoCalc. The
RP2040 build runs on one core; RP2350 builds also use the second core for
periodic status-bar refresh. Hardware interfaces include the LCD, keyboard
controller, SD card, and (on Pico 2 W) Wi-Fi.

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
| `apps/text.c`, `apps/fs.c`, `apps/hex.c` | Text, filesystem, and hex utilities |
| `core/`        | Portable logic with host tests: expressions, shell parsing, paths, names, regex, globs, text, history, and date conversion |
| `ui/ui.c/.h`   | Semantic colour roles, themed palettes, header/footer, menus, notifications |
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
| `picocalc_hw.h`| Pin map and board constants                                   |
| `lwipopts.h`  | lwIP build configuration                                       |

## Cores

- **Core 0**: shell, all apps, all I/O. Single-threaded; cooperative.
- **Core 1**: only on RP2350. Refreshes the status bar every 2 seconds so the
  shell never has to repaint it inline.

LCD locking protects display updates shared between the shell and the
RP2350 status-bar task.

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

The shell can run without an SD card, but persistence and storage-backed
apps are unavailable until a card is mounted. Wi-Fi passwords in
`/WIFI.CFG` are not encrypted; backups preserve that file and should be
protected accordingly. Backup snapshots use a `FORMAT.TXT` version marker
and restore only missing files.

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
