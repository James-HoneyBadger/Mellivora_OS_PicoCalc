# Mellivora PicoCalc

Mellivora PicoCalc is a keyboard-first micro operating environment for RP2040-based PicoCalc hardware. It combines a shell, filesystem tools, persistent personal utilities, simple development environments, and lightweight games into a compact firmware image that runs directly on the device.

![Mellivora PicoCalc preview](picocalc_mock_preview.svg)

## Highlights

- text shell with history, aliases, and built-in help
- FAT-backed SD card storage with file, search, and disk-usage tools
- persistent apps for notes, todo lists, planner, journal, habits, and bookmarks
- interactive utilities including browser, editor, hex editor, paint, sprite editor, and terminal mode
- built-in calculator, BASIC environment, and Tiny C environment
- launcher, dashboard, system monitor, samples, and mini games including snake

## Quick Start

### Build

From the repository root:

```bash
make picocalc
```

Output image:

```text
picocalc/build/mellivora_picocalc.uf2
```

### Flash

1. Put the RP2040 device into BOOTSEL mode.
2. Copy the UF2 file to the mounted drive.
3. Reboot into Mellivora PicoCalc.
4. Insert an SD card and run `mount` to enable filesystem features.

## Documentation

| Document | Purpose |
|---|---|
| [docs/INSTRUCTIONS.md](docs/INSTRUCTIONS.md) | Fast-start setup and first-use instructions |
| [docs/INSTALL.md](docs/INSTALL.md) | Setup, build, flash, and troubleshooting instructions |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Day-to-day usage of the shell, tools, apps, and languages |
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | Guided first session from boot to productive use |
| [docs/PROGRAMMING_GUIDE.md](docs/PROGRAMMING_GUIDE.md) | Extending the firmware and adding new commands or apps |
| [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md) | Internal architecture, subsystems, and runtime behavior |
| [ABOUT.md](ABOUT.md) | GitHub-friendly project summary and repository about text |

## Repository Layout

- `picocalc/` — active firmware source tree
- `picocalc/src/` — shell, hardware, filesystem, and app logic
- `docs/` — documentation set
- `Makefile` — root build entry for the PicoCalc target

## Project Status

This repository now focuses on the PicoCalc firmware target. The maintained artifact is the RP2040 UF2 image, and the system is designed around a practical on-device shell workflow rather than a desktop runtime.

See [picocalc/README.md](picocalc/README.md) for the firmware-local overview.
