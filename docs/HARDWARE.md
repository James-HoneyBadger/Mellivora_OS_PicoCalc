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

The SD driver bit-bangs the SPI0 pins; the configured nominal clock is
**4 MHz**. Actual transfer speed depends on the MCU and GPIO timing.

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

## Optional Serial Smoke Test

After flashing firmware and connecting the PicoCalc over USB, run the
non-destructive console checks from the repository root:

```bash
python3 tools/hardware/smoke_test.py /dev/ttyACM0
```

The script sends shell echo, searchable help, `sdcheck`, and date commands
and verifies that their responses arrive over USB serial. It does not write
to the SD card. The selected serial port must be available to the current
user; adjust the device path for the host operating system. This checks
console and command dispatch, not the physical LCD or keyboard wiring. It
waits for the default shell prompt before testing; use `--ready-marker` if
the `PROMPT` variable has been customized.

## File System Format

The SD card must be formatted as **FAT16** or **FAT32** with one supported
partition, or in a partitionless ("superfloppy") layout. exFAT and other
filesystems are not supported. Persistent app data and saved Wi-Fi
credentials are stored on the card; credential files are not encrypted.

## Spare Pins and Peripheral Placement

Everything in the pin map above is owned by a driver. The remaining GPIOs are
available to the Hardware Workbench (`hw pins`, `gpio`, `pwm`, `i2cdetect`):

| Board | Free GPIO |
| --- | --- |
| Pico / Pico 2 | GP2, GP3, GP4, GP5, GP8, GP9, GP20, GP21, GP24, GP25, GP28, GP29 |
| Pico 2 W | GP2, GP3, GP4, GP5, GP8, GP9, GP20, GP21, GP28 |

On Pico 2 W the radio owns GP24, GP25 and GP29. GP23 is shared: it is the
board's power-stable pin and the radio enable line.

`hw pins` reports the placement of each interface, including when a placement
borrows the console:

| Interface | Pins | Notes |
| --- | --- | --- |
| UART1 | TX GP4, RX GP5 | Fully free; used by `term` and file transfer by default |
| UART0 | TX GP0, RX GP1 | The stdio console; reusing it drops shell output over UART |
| I2C0 | SDA GP8, SCL GP9 | Fully free |
| I2C1 | SDA GP2, SCL GP3 | Free mapping; the keyboard keeps GP6/GP7 at 10 kHz |
| SPI0 | SCK GP2, MOSI GP3, MISO GP0, CS GP1 | Only free mapping; shares the console pins. SD keeps GP16-GP19 |
| SPI1 | — | Every SPI1 pin set is used by the display or audio |

Because all SPI0 and SPI1 pin options are consumed by the display, the SD
card, and the audio PWM outputs, an external SPI device has to reuse the
console UART pins. `hw pins` says so explicitly instead of silently
reconfiguring them.

## Serial Terminal

`term` opens a full-screen VT100/ANSI terminal on UART1 (GP4/GP5) at 115200
baud by default:

```text
term                 # uart1, 115200, no echo
term -b 9600 -e      # 9600 baud with local echo
term -p usb          # talk to the USB-CDC port instead
term -l /TERM.LOG    # append the raw session to the card
```

Supported sequences include cursor addressing, erase/insert/delete, scroll
regions, SGR colour and attributes, save/restore, device status replies, and
OSC title suppression. Exit with **Ctrl-]** or **Ctrl-X**; every other key,
including Ctrl-C, is sent to the far end.

## SD Card Layout

The starter pack (`sd_starter_pack.zip`, attached to each release) creates the
directories the firmware looks for:

| Path | Contents |
| --- | --- |
| `/apps` | `.mapp` modules and `.bas`, `.c`, `.fth` scripts shown in the launcher |
| `/themes` | `NAME.thm` palettes read at boot and selectable with `theme NAME` |
| `/bin` | Shell scripts installed by `mpkg` |
| `/docs` | Offline copies of the user documentation |

`mpkg` (Pico 2 W only) writes into `/apps`, `/themes`, and `/bin`, caches the
repository index at `/apps/repo.json`, and keeps its install state in
`/apps/.mpkg-state.json`. HTTPS needs a PEM certificate authority at
`/CA.CRT`.

## Browser Installer

[docs/flasher/index.html](flasher/index.html) is a self-contained page that
validates a UF2 (magic values, block count, family ID), shows its SHA-256
digest, can copy it straight to the mounted `RPI-RP2` volume through the File
System Access API, and opens a 115200-baud Web Serial console. It performs no
network requests and works offline from the repository or a GitHub Pages
deployment.

## Battery

The PicoCalc co-processor manages battery charging and reports state of
charge as a percentage (0-100) via I2C register `0x0B`. Mellivora reads
this via `kbd_battery_percent()` and shows it in the status bar; warnings
trigger below 15 %.
