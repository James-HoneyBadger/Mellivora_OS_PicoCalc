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

- Clockwork PicoCalc or another RP2040-based compatible device
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

From the repository root, run:

```bash
make picocalc
```

When the build succeeds, the UF2 image will be available at:

```text
picocalc/build/mellivora_picocalc.uf2
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

The on-device apps and file tools depend on a FAT-formatted card.

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

## 11. Scope Reminder

This repository is maintained as a PicoCalc firmware project. The active target is the RP2040-based handheld workflow, not the legacy PC boot environment.
