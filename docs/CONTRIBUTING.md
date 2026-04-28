# Contributing to Mellivora OS

Thank you for your interest! This document explains how to set up a build
environment, the project's coding conventions, and what to include in a pull
request.

## Build Environment

Mellivora OS is a bare-metal C11 firmware built with the official
[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and the
`arm-none-eabi-gcc` toolchain.

### Required tools

- `cmake >= 3.13`
- `gcc-arm-none-eabi` (any reasonably recent version)
- `make`
- `git` (for fetching submodules / picotool)
- `python3` (used by some pico-sdk build helpers)

On Debian / Ubuntu:

```bash
sudo apt install cmake gcc-arm-none-eabi build-essential git python3
```

On Fedora:

```bash
sudo dnf install cmake arm-none-eabi-gcc-cs arm-none-eabi-newlib make git python3
```

### Building

```bash
make picocalc        # original Raspberry Pi Pico (RP2040)
make pico2           # Raspberry Pi Pico 2 (RP2350)
make pico2w          # Raspberry Pi Pico 2 W (RP2350 + CYW43439)
make all             # all three targets
make clean           # remove all build trees
```

Each target produces a UF2 in its respective build directory:

- `picocalc/build/mellivora_picocalc.uf2`
- `picocalc/build-pico2/mellivora_picocalc_pico2.uf2`
- `picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2`

Hold BOOTSEL while plugging in the device, then drag-and-drop the UF2 onto
the `RPI-RP2` (or `RP2350`) drive that appears.

## Code Style

- **Language**: C11 (GNU extensions allowed where the SDK requires them)
- **Indentation**: 4 spaces, no tabs
- **Braces**: K&R / 1TBS — opening brace on same line for functions and blocks
- **Naming**:
  - Functions: `snake_case`
  - Static helpers: prefixed `_` (e.g. `_draw_status_bar`)
  - Public (cross-module) APIs: module prefix (e.g. `kbd_`, `lcd_`, `fat_`)
  - Constants / macros: `UPPER_SNAKE_CASE`
- **Headers**: prefer `#pragma once` over include guards
- **Comments**: `/* ... */` for blocks; `//` is fine for single-line
- **Avoid** `malloc`/`free` in firmware code; prefer static / stack buffers
- **Memory**: be explicit about buffer sizes; never assume `strncpy` terminates
- **Ifdef gates**: use `PICO_CYW43_SUPPORTED`, `PICO_RP2350A`, etc.
- **No exceptions / no setjmp** unless absolutely necessary

## Pull Request Checklist

- [ ] All three targets build cleanly (`make all`)
- [ ] No new warnings from files in `picocalc/src/`
- [ ] New commands are documented in `docs/COMMANDS.md`
- [ ] User-visible changes added to `docs/CHANGELOG.md`
- [ ] PR description explains *why*, not just *what*
- [ ] Tested on real hardware where possible (or noted otherwise)

## Architecture Overview

See [ARCHITECTURE.md](ARCHITECTURE.md) for boot flow, module layout, and the
core pipelines (LCD, keyboard, SD, networking).

## Filing Issues

Please include:

1. Hardware variant (PicoCalc + Pico / Pico 2 / Pico 2W)
2. Firmware version (`version` command)
3. Steps to reproduce
4. Expected vs. actual behavior

## License

By contributing, you agree that your code will be released under the project's
existing license (see [LICENSE](../LICENSE)).
