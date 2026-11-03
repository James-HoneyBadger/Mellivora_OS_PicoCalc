# Quick Start

This page gets a Clockwork PicoCalc running Mellivora PicoCalc. Use
[INSTALL.md](INSTALL.md) for detailed setup and troubleshooting, or
[USER_GUIDE.md](USER_GUIDE.md) for day-to-day usage.

## 1. Choose a firmware image

Use the image that matches the Raspberry Pi board installed in the
PicoCalc:

| Board | Firmware target | Checked-in image |
| --- | --- | --- |
| Raspberry Pi Pico (RP2040) | `picocalc` | `firmware/mellivora_v2.5.0_pico.uf2` |
| Raspberry Pi Pico 2 (RP2350) | `pico2` | `firmware/mellivora_v2.5.0_pico2.uf2` |
| Raspberry Pi Pico 2 W (RP2350, Wi-Fi) | `pico2w` | `firmware/mellivora_v2.5.0_pico2w.uf2` |

The checked-in images are the v2.5.0 release. Newer releases may be
available from the repository's GitHub Releases page.

## 2. Flash the firmware

1. Disconnect the PicoCalc from USB.
2. Hold **BOOTSEL** while connecting the USB cable.
3. Release **BOOTSEL** after the board's USB mass-storage drive appears.
4. Copy the matching UF2 image to that drive.
5. Wait for the board to reboot.

Use a data-capable USB cable. The Pico 2 W image enables Wi-Fi and should
only be used with a Pico 2 W board.

## 3. Start using the device

At the shell prompt, try:

```text
help
dashboard
mount
ls
home
```

Insert a FAT16- or FAT32-formatted microSD card before running `mount`.
Storage-backed apps, shell history, settings, and files are unavailable
until the card is mounted.

## Build from source

From the repository root, build the image for your board:

```bash
make picocalc   # Pico (RP2040)
make pico2      # Pico 2 (RP2350)
make pico2w     # Pico 2 W (RP2350 + Wi-Fi)
```

The first build fetches the pinned Pico SDK and its required submodules
unless `PICO_SDK_PATH` points to an existing SDK checkout. Build outputs
are written to `picocalc/build/`, `picocalc/build-pico2/`, and
`picocalc/build-pico2w/`.

See [INSTALL.md](INSTALL.md) for prerequisites, test commands, and build
troubleshooting.
