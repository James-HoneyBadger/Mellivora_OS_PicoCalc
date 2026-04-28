# Hardware Reference

This page documents the physical interfaces Mellivora OS uses on the
Clockwork PicoCalc.

## Supported Boards

| Board                 | MCU      | Clock  | WiFi | Notes                            |
|-----------------------|----------|--------|------|----------------------------------|
| Raspberry Pi Pico     | RP2040   | 133MHz | No   | Original PicoCalc bundle         |
| Raspberry Pi Pico 2   | RP2350A  | 150MHz | No   | Faster, more RAM (520KB)         |
| Raspberry Pi Pico 2 W | RP2350A  | 150MHz | Yes  | Adds CYW43439 WiFi+BT            |

## Pin Map (PicoCalc v2.0)

### LCD — ILI9488, 320x320, SPI1

| Pin  | Signal      | GPIO |
|------|-------------|------|
| SCK  | clock       |   10 |
| MOSI | data        |   11 |
| MISO | (unused)    |   12 |
| CS   | chip select |   13 |
| DC   | data/cmd    |   14 |
| RST  | reset       |   15 |

SPI1 runs at **25 MHz** on RP2040, **40 MHz** on RP2350.

### Keyboard — STM32 co-processor over I2C1

| Pin | Signal | GPIO |
|-----|--------|------|
| SDA | data   | 6    |
| SCL | clock  | 7    |

- I2C address: `0x1F`
- I2C speed: **10 kHz** (slow because the STM32 firmware is timing-sensitive)
- Key register: `0x09` — 2-byte read returns `(keycode << 8) | status`
- Backlight registers:
  - `0x85` — LCD backlight
  - `0x8A` — keyboard backlight
- Battery register: `0x0B` — high bit indicates charging

### SD Card — SPI0

| Pin   | Signal      | GPIO |
|-------|-------------|------|
| SCK   | clock       | 18   |
| MOSI  | data out    | 19   |
| MISO  | data in     | 16   |
| CS    | chip select | 17   |
| DET   | card detect | 22   |

SPI0 runs at **4 MHz** for compatibility across SD card classes.

### Audio — dual-channel PWM

| Channel | GPIO |
|---------|------|
| Left    | 26   |
| Right   | 27   |

### Power

| Pin  | Function                                                |
|------|---------------------------------------------------------|
| GP23 | `PICO_PIN_PS` — must be driven HIGH for stable operation |

## Power Consumption (rough, full-bright)

| State                              | Current |
|------------------------------------|---------|
| Idle shell, full backlight         | ~80 mA  |
| Idle shell, dimmed backlight (60s) | ~55 mA  |
| `sleep` (LCD off, KBD off)         | ~30 mA  |
| `shutdown` (halted, LCD off)       | ~25 mA  |

Numbers are approximate and depend on SD card, USB connection, and battery
charge state.

## File System Format

The SD card must be formatted as **FAT16** or **FAT32** with a single
partition (or partitionless / "superfloppy" layout). exFAT and other
filesystems are not supported.

## Battery

The PicoCalc co-processor manages battery charging and reports state of
charge as a percentage (0-100) via I2C register `0x0B`. Mellivora reads
this via `kbd_battery_percent()` and shows it in the status bar; warnings
trigger below 15 %.
