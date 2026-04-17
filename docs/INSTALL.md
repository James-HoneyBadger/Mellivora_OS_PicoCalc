# Mellivora PicoCalc Installation and Build Instructions

This guide covers setup, building, flashing, and first boot for the PicoCalc-native Mellivora firmware.

## 1. Requirements

Host tools:

- Linux system with standard development tools
- CMake 3.13 or newer
- GNU Make
- ARM embedded GCC toolchain with arm-none-eabi-gcc
- A local checkout of the Raspberry Pi Pico SDK

Hardware:

- Clockwork PicoCalc or compatible RP2040 target
- USB cable for BOOTSEL flashing
- microSD card for filesystem features

## 2. Repository Layout

Important paths:

- Root build entry: Makefile
- Firmware source: picocalc/
- Output image: picocalc/build/mellivora_picocalc.uf2
- Documentation: docs/

## 3. Environment Setup

Export the Pico SDK path before building if it is not already configured:

```bash
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
```

Confirm the ARM compiler is available:

```bash
arm-none-eabi-gcc --version
```

## 4. Build the Firmware

From the repository root:

```bash
make picocalc
```

A successful build produces:

- picocalc/build/mellivora_picocalc.uf2

## 5. Flash to the Device

1. Hold the BOOTSEL button on the RP2040 device.
2. Connect the device over USB.
3. Release the button when the mass-storage drive appears.
4. Copy the UF2 image onto the mounted drive.
5. The device will reboot automatically into Mellivora.

## 6. First Boot Checklist

After startup:

1. Verify the shell banner appears.
2. Run help to see built-in and ported commands.
3. Insert an SD card.
4. Run mount to enable the FAT filesystem.
5. Use ls and pwd to confirm storage access.

## 7. SD Card Preparation

For best results:

- Use FAT16 or FAT32 formatted media
- Use simple 8.3-style names when possible
- Eject the card cleanly before removal

## 8. Troubleshooting

### Build fails because the SDK is missing

Set PICO_SDK_PATH to a valid SDK checkout and rebuild.

### Build fails because the ARM compiler is missing

Install the arm-none-eabi toolchain from your distribution or embedded toolchain package source.

### Device boots but file commands fail

- Ensure the SD card is inserted correctly
- Run mount before ls, cd, cat, write, mkdir, or rm
- Reformat the card as FAT if it is not recognized

### USB flash drive does not appear

Use a data-capable USB cable and re-enter BOOTSEL mode.

## 9. Updating the Firmware

Rebuild with make picocalc and copy the new UF2 to the device in BOOTSEL mode.

## 10. Current Scope

This project is now PicoCalc-only. The supported target is RP2040 firmware rather than the former x86 boot image flow.
