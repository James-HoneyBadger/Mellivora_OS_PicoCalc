# Mellivora PicoCalc Firmware

This directory contains the active RP2040 firmware target for Clockwork PicoCalc-class
hardware.

## Current capabilities

- native UF2 firmware output
- shell with command history and working directories
- FAT-backed file operations on SD media
- bundled Mellivora utility layer for common commands
- interactive BASIC and Tiny C environments

## Build

Prerequisites:

- arm-none-eabi toolchain
- CMake 3.13+
- `PICO_SDK_PATH` pointing at a Pico SDK checkout

```bash
make picocalc
```

Output:

- `picocalc/build/mellivora_picocalc.uf2`

Flash the UF2 by mounting the device in BOOTSEL mode and copying the file.

## Runtime commands

Current shell supports:

- `help`
- `uname`
- `uptime`
- `clear`
- `battery`
- `backlight <n>`
- `mount`
- `ls [path]` / `dir [path]`
- `cd [path]`
- `pwd`
- `cat <path>`
- `echo [text]`
- `hello`
- `touch <path>`
- `write <path> <text>`
- `mkdir <path>`
- `rm <path>`
- `sdinfo`
- `sdread <lba>`
- `reboot`

Ported command set also includes utilities such as `head`, `tail`, `wc`, `cut`, `grep`, `find`, `pager`, `sort`, `hexdump`, plus the `calc`, `cp`, `mv`, `stat`, `edit`, `browse`, `notes`, `home`, `dashboard`, `sysmon`, `script`, `paint`, `samples`, `clock`, `cal`, `basic`, and `tcc` environments.

Arrow keys browse command history and Ctrl-C cancels the current line.

## Additional documentation

See the top-level documentation set for full usage and developer references:

- [docs/INSTALL.md](../docs/INSTALL.md)
- [docs/USER_GUIDE.md](../docs/USER_GUIDE.md)
- [docs/TECHNICAL_REFERENCE.md](../docs/TECHNICAL_REFERENCE.md)
- [docs/PROGRAMMING_GUIDE.md](../docs/PROGRAMMING_GUIDE.md)
