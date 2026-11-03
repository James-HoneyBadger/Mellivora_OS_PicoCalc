# Installation and Development Setup

This guide covers building Mellivora PicoCalc, flashing a board, preparing
storage, and diagnosing common setup problems.

## Supported hardware

| Board | MCU | Firmware target | Wi-Fi |
| --- | --- | --- | --- |
| Clockwork PicoCalc with Raspberry Pi Pico | RP2040 | `picocalc` | No |
| Clockwork PicoCalc with Raspberry Pi Pico 2 | RP2350 | `pico2` | No |
| Clockwork PicoCalc with Raspberry Pi Pico 2 W | RP2350 | `pico2w` | Yes |

You will also need a data-capable USB cable. A microSD card is optional for
booting but required for filesystem commands and persistent app data.

## Build prerequisites

The build requires CMake, GNU Make, Git, Python 3, and the ARM embedded GCC
toolchain. On Debian or Ubuntu:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git python3 gcc-arm-none-eabi \
  libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

The project fetches Raspberry Pi Pico SDK 2.3.1 at a pinned commit, plus its
required submodules, on the first build. Internet access is needed for that
initial fetch. To use a separately managed SDK instead, set
`PICO_SDK_PATH` before building:

```bash
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
```

The project does not version-check an externally supplied SDK. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the pinned revision and development
workflow.

## Build firmware

Run commands from the repository root:

```bash
make picocalc
make pico2
make pico2w
```

To build all supported variants:

```bash
make all-targets
```

Generated UF2 files:

| Board | Output |
| --- | --- |
| Pico | `picocalc/build/mellivora_picocalc.uf2` |
| Pico 2 | `picocalc/build-pico2/mellivora_picocalc_pico2.uf2` |
| Pico 2 W | `picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2` |

The build configuration reads the project version and repository slug from
the root `VERSION` and `REPOSITORY` files. For the main project source tree,
do not configure CMake directly; use the root Makefile so the SDK and board
settings are consistent.

## Flash a UF2

1. Disconnect the PicoCalc from USB.
2. Hold **BOOTSEL** and reconnect USB.
3. Release **BOOTSEL** after the board's mass-storage drive appears.
4. Copy the UF2 built for that exact board to the drive.
5. Wait for the board to reboot.

Firmware updates do not erase the microSD card. User files and persistent
settings remain on the card unless you explicitly remove them.

## Prepare storage

The firmware supports FAT16 and FAT32 partitions and partitionless FAT
("superfloppy") media. The driver selects the first recognized FAT
partition; using a single FAT partition is recommended. exFAT is not
supported. For best compatibility, use a safely ejected card formatted
with a standard FAT16 or FAT32 filesystem.

After inserting the card, run:

```text
mount
ls
```

If mount reports an error, see [HARDWARE.md](HARDWARE.md) for the SD
interface and [USER_GUIDE.md](USER_GUIDE.md) for storage commands. Wi-Fi
credentials saved by the Pico 2 W firmware are stored in `/WIFI.CFG` on
the card as readable text; do not use a card you cannot physically protect.

## First-boot verification

Run these commands at the shell prompt:

```text
version
help
uname
mount
ls
dashboard
```

`version` reports the firmware build date and project URL. If no SD card is
present, storage-backed features remain unavailable, but the shell and
built-in tools can still be used.

## Host tests and quality checks

The host test suite uses the system C compiler and runs without the Pico
SDK or physical hardware:

```bash
make test
```

The tests enable AddressSanitizer and UndefinedBehaviorSanitizer by default.
Additional contributor checks are documented in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Troubleshooting

### The SDK cannot be found

Run the build from the repository root and allow the first build to fetch
the pinned SDK. If using a separately installed SDK, confirm
`PICO_SDK_PATH` points to its root directory.

### The ARM compiler or libraries are missing

Install the packages listed under [Build prerequisites](#build-prerequisites)
and verify that `arm-none-eabi-gcc` is on your `PATH`.

### The UF2 drive does not appear

- Use a data-capable USB cable and a direct USB port.
- Disconnect and reconnect while holding **BOOTSEL**.
- Confirm that the correct Pico board is installed in the PicoCalc.

### The firmware boots but storage commands fail

- Insert the microSD card fully and run `mount`.
- Confirm the card uses FAT16 or FAT32, not exFAT.
- Try a safely ejected card known to work in another device.
- Re-run `mount` after reinserting the card.

### The display needs to be redrawn

Press `Ctrl-L` or run `clear`.

## Related documentation

- [Quick start](INSTRUCTIONS.md)
- [User guide](USER_GUIDE.md)
- [Hardware reference](HARDWARE.md)
- [Contributor guide](CONTRIBUTING.md)
