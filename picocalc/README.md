# Mellivora PicoCalc Firmware

This directory contains the active RP2040 firmware implementation for Mellivora on PicoCalc-class hardware.

## Firmware capabilities

- native UF2 firmware build output
- shell with history, aliases, and built-in manuals
- FAT-backed SD card file access
- persistent personal apps such as notes, todo, planner, journal, habits, and bookmarks
- creative tools including paint and sprite editing
- mini games including snake, dice, coin, and guess
- on-device calculator, BASIC, and Tiny C environments

## Build

From the repository root:

```bash
make picocalc
```

Main output image:

```text
picocalc/build/mellivora_picocalc.uf2
```

## Runtime experience

The shell supports system control, file browsing and editing, storage management, app launching, dashboards, and programming tools in a handheld-friendly text UI.

Useful entry points:

- `help`
- `man fs`
- `home`
- `dashboard`
- `mount`
- `browse`
- `notes`
- `todo`
- `planner`
- `journal`
- `habits`
- `bookmarks`
- `sprite`
- `games`
- `basic`
- `tcc`

## Documentation

For the full documentation set, see:

- [../README.md](../README.md)
- [../docs/INSTRUCTIONS.md](../docs/INSTRUCTIONS.md)
- [../docs/INSTALL.md](../docs/INSTALL.md)
- [../docs/USER_GUIDE.md](../docs/USER_GUIDE.md)
- [../docs/TUTORIAL.md](../docs/TUTORIAL.md)
- [../docs/PROGRAMMING_GUIDE.md](../docs/PROGRAMMING_GUIDE.md)
- [../docs/TECHNICAL_REFERENCE.md](../docs/TECHNICAL_REFERENCE.md)
- [../ABOUT.md](../ABOUT.md)
