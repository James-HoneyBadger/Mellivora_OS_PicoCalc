# Mellivora OS Architecture

A single-image, bare-metal C11 firmware for the Clockwork PicoCalc.
Two cores, three peripherals (LCD, keyboard MCU, SD), an optional WiFi stack,
and a shell on top.

## Boot Flow

```
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
   |- sleep_ms(1500)                  # SD settle
   |- sd_init() / fat_mount()         # optional, may fail gracefully
   |- alias_load() / hist_load()      # restore persistent state
   |- cwd_restore() / clock_restore()
   |- app_init()                      # register apps & dispatcher
   |- (Pico 2W) cyw43_arch_init() -> wifi auto-connect
   |- multicore_launch_core1(_core1_entry)   # status bar tick (RP2350)
   |- watchdog_enable(3000, true)
   |
   v
shell loop:
   read_input -> dispatch -> hist_save -> screen redraw
```

## Module Layout (`picocalc/src/`)

| File           | Purpose                                                       |
|----------------|---------------------------------------------------------------|
| `main.c`       | Shell, command dispatch, REPL, persistence, completion        |
| `apps.c/.h`    | All built-in apps (calc, edit, snake, planner, etc.)          |
| `kbd.c/.h`    | I2C driver for STM32 co-processor; key repeat; backlight       |
| `lcd.c/.h`    | ILI9488 SPI driver; software framebuffer; ANSI parser          |
| `sd.c/.h`     | Raw SPI SD-card block driver (CMD0..CMD58)                     |
| `fat.c/.h`    | FAT16/FAT32 read/write (ls, mkdir, open, write, unlink)        |
| `net.c/.h`    | TCP/UDP/DNS/NTP/HTTP wrappers over lwIP (CYW43 only)           |
| `netapps.c/.h`| User-facing network commands (`wifi`, `ping`, `ntp`, ...)      |
| `syscall.c/.h`| Cross-module facade: `sys_print`, time, RTC, interrupt flag    |
| `picocalc_hw.h`| Pin map and board constants                                   |
| `lwipopts.h`  | lwIP build configuration                                       |

## Cores

- **Core 0**: shell, all apps, all I/O. Single-threaded; cooperative.
- **Core 1**: only on RP2350. Drives the status bar at 1 Hz so the
  shell never has to repaint it inline.

A recursive mutex (`lcd_lock` / `lcd_unlock`) protects every pixel write so
core 1's status-bar updates can never tear into core 0's text output.

## Pipelines

### LCD pipeline

```
out_char/out_str -> ANSI parser -> framebuffer cell write
                                      |
                                      v
                            lcd_draw_cell(col,row,ch,fg,bg)
                                      |
                                      v
                       SPI DMA blit (8x16 glyph) -> ILI9488
```

### Keyboard pipeline

```
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

```
fat_*  -> resolve_path -> read_sector / write_sector
              |                |
              v                v
         dirent walk      sd_read_block / sd_write_block
                                  |
                                  v
                         SPI0 to SD card (CS toggled)
```

A single 512-byte `_sector_buf` is shared across reads, so callbacks invoked
from inside `fat_ls` MUST NOT call back into `fat_*`. Helpers like `cp -r`
and `rm -r` snapshot the directory listing first, then iterate.

### Network pipeline (Pico 2W)

```
netapps  -> net.c high-level (sync wrappers)
                |
                v
          lwIP TCP/UDP/DNS
                |
                v
        cyw43_arch (CYW43439 driver, SDIO over PIO)
```

## Persistence

All persistent state lives on the SD card:

| File             | Owner / contents                            |
|------------------|---------------------------------------------|
| `/CWD.CFG`       | Last working directory                      |
| `/HISTORY.LOG`   | Shell history (last ~32 entries)            |
| `/ALIAS.CFG`     | Aliases                                     |
| `/CLOCK.CFG`     | Software RTC offset                         |
| `/SETTINGS.CFG`  | App settings (fg/bg, theme)                 |
| `/WIFI.CFG`      | Saved WiFi credentials                      |
| `/NOTES.TXT`     | Notes app                                   |
| `/TODO.TXT`      | Todo app                                    |
| `/JOURNAL.TXT`   | Journal app                                 |
| `/HABITS.CFG`    | Habit tracker                               |
| `/BOOKMARKS.CFG` | Bookmark + CWD pairs                        |
| `/SNAKE.HI`      | Snake high score                            |

If no SD card is present, all persistence is skipped silently and the system
falls back to RAM-only state.

## Watchdog & Recovery

A 3-second hardware watchdog is enabled at boot. The shell loop tickles it
every iteration. Long-running apps that legitimately block (e.g. NTP sync,
HTTP fetch) tickle the watchdog explicitly. If anything truly hangs, the
device reboots cleanly within 3 seconds.

## Adding a New Command

1. Write `static void cmd_foo(const char *arg) { ... }` in `main.c`.
2. Add a dispatch entry: `else if (!strcmp(line, "foo")) cmd_foo(arg);`
3. Add `"foo"` to the tab-completion array.
4. Document it in [`COMMANDS.md`](COMMANDS.md).
5. Add a CHANGELOG entry.

## Adding a New App

1. Implement `static void app_foo(const char *arg)` in `apps.c`.
2. Add `{"foo", app_foo}` to the dispatch table near line 4535.
3. Add `"foo"` to the completion array in `main.c`.
4. Document and changelog.
