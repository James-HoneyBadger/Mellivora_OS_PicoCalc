# Contributing to Mellivora OS

Thank you for your interest! This document explains how to set up a build
environment, the project's coding conventions, and what to include in a pull
request.

## Build Environment

Mellivora OS is a bare-metal C11 firmware built with the official
[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and the
`arm-none-eabi-gcc` toolchain.

The root `Makefile` fetches Pico SDK 2.3.1 at a pinned commit when
`PICO_SDK_PATH` is not set. To build against a separately managed SDK, set
`PICO_SDK_PATH` to its directory; that external checkout is not version-checked
by the project.

`VERSION` and `REPOSITORY` at the repository root are the source of truth for
firmware and OCI metadata. Release tags must use the form `v<VERSION>`.

### Required tools

- `cmake >= 3.13`
- `gcc-arm-none-eabi` (any reasonably recent version)
- `make`
- `git` (for fetching submodules / picotool)
- `python3` (used by some pico-sdk build helpers)

On Debian / Ubuntu:

```bash
sudo apt install cmake gcc-arm-none-eabi build-essential git python3 \
  libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
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
make all-targets     # all three targets
make clean           # remove all build trees
```

Each target produces a UF2 in its respective build directory:

- `picocalc/build/mellivora_picocalc.uf2`
- `picocalc/build-pico2/mellivora_picocalc_pico2.uf2`
- `picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2`

Hold BOOTSEL while plugging in the device, then drag-and-drop the UF2 onto
the `RPI-RP2` (or `RP2350`) drive that appears.

### Host tests, analysis, and size gates

Pure modules in `picocalc/src/core/` (`expr`, `shell_parse`, `path_util`,
`fatname`, `simple_re`, `glob`, `textutil`, `history`, `datetime`) and the FAT
driver compile with the system C compiler. FAT tests provide an in-memory
SD-card backend, so host tests run without hardware or the Pico SDK:

```bash
make test          # unit tests, ASan + UBSan by default
make lint          # clang-tidy over src/core and app_shared
make format-check  # clang-format over core modules and tests
make size-gate     # flash/RAM budgets + baseline regression check
make quality-check # docs/command checks plus host tests
```

The firmware compiles every Mellivora source with
`-Wall -Wextra -Wshadow -Wundef -Werror`. Pass
`-DMELLIVORA_WERROR=OFF` to CMake when experimenting locally.

After an intentional size change, rebuild all targets and refresh the
committed baselines with `make size-baseline`. CI applies `make size-gate`
to each target and fails on a budget overflow or an unapproved regression
(more than 5% growth plus 2 KiB slack).

## Code Style

- **Language**: C11 (GNU extensions allowed where the SDK requires them)
- **Indentation**: 4 spaces, no tabs
- **Braces**: K&R / 1TBS — opening brace on same line for functions and blocks
- **Naming**:
  - Functions: `snake_case`
  - Private implementation helpers: `static` where possible; use descriptive
    `snake_case` names
  - Public (cross-module) APIs: module prefix (e.g. `kbd_`, `lcd_`, `fat_`)
  - Constants / macros: `UPPER_SNAKE_CASE`
- **Headers**: follow the existing `#pragma once` convention
- **Comments**: `/* ... */` for blocks; `//` is fine for single-line
- **Avoid** `malloc`/`free` in firmware code; prefer static / stack buffers
- **Memory**: be explicit about buffer sizes; never assume `strncpy` terminates
- **Ifdef gates**: use `PICO_CYW43_SUPPORTED`, `PICO_RP2350A`, etc.
- **No exceptions / no setjmp** unless absolutely necessary

## Pull Request Checklist

- [ ] All three targets build cleanly (`make all-targets`)
- [ ] No new warnings from files in `picocalc/src/` (`-Werror` is enabled)
- [ ] Host tests, analysis, and formatting pass (`make test lint format-check`)
- [ ] Size gate passes (`make size-gate`), or the baseline is refreshed with
      an explanation in the PR
- [ ] New commands are documented in `docs/COMMANDS.md`
- [ ] User-visible changes added to `docs/CHANGELOG.md`
- [ ] PR description explains *why*, not just *what*
- [ ] Tested on hardware where possible (or noted otherwise)

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
