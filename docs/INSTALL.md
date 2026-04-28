# Mellivora PicoCalc Installation and Build Instructions

This document is the main setup and bring-up guide for Mellivora PicoCalc. It covers host requirements, toolchain setup, firmware builds, flashing, SD card preparation, and first boot validation.

## 1. What You Need

### Host requirements

- Linux development machine
- CMake 3.13 or newer
- GNU Make
- ARM embedded GCC toolchain with `arm-none-eabi-gcc`
- Git
- a local Raspberry Pi Pico SDK checkout or the auto-fetched SDK used by the project build flow

### Hardware requirements

- Clockwork PicoCalc or another compatible device using Raspberry Pi Pico or Pico 2 class hardware
- USB data cable
- microSD card for filesystem-backed commands and persistent apps

## 2. Important Paths

- root build entry: `Makefile`
- firmware tree: `picocalc/`
- firmware sources: `picocalc/src/`
- build output: `picocalc/build/mellivora_picocalc.uf2`
- documentation: `docs/`

## 3. Recommended Linux Setup

On Debian or Ubuntu style systems, the usual prerequisites are:

```bash
sudo apt update
sudo apt install -y build-essential cmake git gcc-arm-none-eabi
```

If you already have the Pico SDK installed elsewhere, export the path:

```bash
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
```

Confirm the toolchain is visible:

```bash
arm-none-eabi-gcc --version
cmake --version
```

## 4. Building the Firmware

From the repository root, run the standard Pico build:

```bash
make picocalc
```

For the Pico 2 variant, run:

```bash
make pico2
```

For the Pico 2W variant, run:

```bash
make pico2w
```

When the builds succeed, the UF2 images will be available at:

```text
picocalc/build/mellivora_picocalc.uf2
picocalc/build-pico2/mellivora_picocalc_pico2.uf2
picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2
```

## 5. Flashing to Hardware

1. Disconnect the device if it is already attached.
2. Hold the BOOTSEL button.
3. Connect the USB cable while holding BOOTSEL.
4. Release the button after the mass-storage drive appears.
5. Copy `mellivora_picocalc.uf2` onto that drive.
6. Wait for the automatic reboot.

## 6. First Boot Checklist

After flashing, confirm the following on the device:

1. the Mellivora shell prompt appears
2. `help` lists the command families
3. `home` opens the launcher
4. `dashboard` shows live status information
5. an inserted SD card mounts correctly with `mount`
6. `ls` and `pwd` work as expected

## 7. Preparing the SD Card

The apps and file tools depend on a FAT-formatted card.

Recommended practice:

- use FAT16 or FAT32
- use a clean, safely ejected card
- create a few test folders and text files before first use
- keep filenames simple if you want maximum compatibility

After inserting the card, run:

```text
mount
ls
```

## 8. Updating the Firmware Later

To ship a new build to the device:

1. pull or edit the repository
2. rebuild with `make picocalc`
3. flash the new UF2 in BOOTSEL mode
4. reboot and verify the shell and storage tools still work

Persistent files stored on the SD card such as notes, planner data, settings, and journal entries remain on the card unless you erase them yourself.

## 9. Troubleshooting

### Build cannot find the Pico SDK

Make sure `PICO_SDK_PATH` is valid or rerun the normal project build flow from the repository root.

### Build cannot find `arm-none-eabi-gcc`

Install the embedded GCC toolchain and verify it is present in your shell path.

### The UF2 flashes but file commands fail

- confirm the SD card is inserted correctly
- run `mount` before using storage features
- reformat the card as FAT16 or FAT32
- try a smaller, known-good SD card if detection is unreliable
- use long command output with `| more` if the display is busy with large listings

### SD Card Compatibility

The firmware communicates with SD cards over bit-banged SPI at 1 MHz. Compatibility notes:

- **Recommended**: SDHC cards 4 GB to 32 GB formatted as FAT32
- **Also supported**: SD cards up to 2 GB formatted as FAT16
- **Not supported**: SDXC (64 GB+) cards with exFAT — reformat as FAT32 if needed
- **Troubleshooting**: If `mount` fails or hangs, try a different card brand. Some ultra-fast UHS-I cards may not work reliably over SPI. The firmware now validates CRC16 checksums on every block read and uses 3-read debounce for card detection.

### The device does not appear in BOOTSEL mode

- use a data-capable USB cable
- try a different USB port
- reconnect while holding BOOTSEL longer

### The shell runs but the display seems stale

Use `clear` or `Ctrl-L` to redraw the screen.

## 10. Verification Commands

Useful post-install checks:

```text
help
uname
mount
ls
settings
samples
```

## 11. Build System Reference

The top-level `Makefile` provides these targets:

| Target | Description |
| --- | --- |
| `make picocalc` | Build firmware for Pico (RP2040) |
| `make pico2` | Build firmware for Pico 2 (RP2350) |
| `make pico2w` | Build firmware for Pico 2W (RP2350 with WiFi) |
| `make clean` | Remove build artifacts |

The build uses CMake internally. Key CMake variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PICO_SDK_PATH` | `picocalc/pico-sdk` | Path to the Pico SDK |
| `PICO_BOARD` | `pico` / `pico2` / `pico2_w` | Target board type |
| `CMAKE_BUILD_TYPE` | `Release` | Build optimization level |

Output files:

- Pico: `picocalc/build/mellivora_picocalc.uf2`
- Pico 2: `picocalc/build-pico2/mellivora_picocalc_pico2.uf2`
- Pico 2W: `picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2`

A `Containerfile` is also provided for reproducible builds in a container environment.

## 12. Scope Reminder

This repository is maintained as a PicoCalc firmware project. The active targets are the RP2040 and RP2350 based handheld workflows.
