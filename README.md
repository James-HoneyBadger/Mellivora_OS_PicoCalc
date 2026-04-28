# Mellivora PicoCalc

Mellivora PicoCalc is a compact firmware for the Clockwork PicoCalc, built
for the Raspberry Pi Pico, Pico 2, and Pico 2W. It bundles a shell,
filesystem tools, persistent personal utilities, simple development
environments, and lightweight games into a single image.

![Mellivora PicoCalc preview](picocalc_mock_preview.svg)

## Highlights

- text shell with history (persistent across reboots), aliases, shell variables (`set X=Y`, `$X`), tab completion, command chaining (`;`), output redirection (`>` `>>`), and built-in help
- FAT16/FAT32 SD card storage with file, search, and disk-usage tools (`rm -r`, `cp -r`)
- persistent apps for notes, todo lists, planner, journal, habits (with streak tracking), and bookmarks (with CWD context)
- interactive utilities including browser, editor, hex editor (with byte search), paint, sprite editor, and terminal mode
- text processing: grep (with regex), sort, find, head, tail, cut, wc, and more
- built-in calculator (with hex/bin/oct literals), BASIC environment, and Tiny C environment
- WiFi networking with multi-AP credential store, boot-time auto-connect, HTTP, IRC, telnet, NTP (auto-syncs the RTC), and DNS (Pico 2W only)
- productivity timers: `stopwatch` and `pomodoro`
- power management: idle backlight dim, `sleep` command, `shutdown`/`halt`/`poweroff`, low-battery warning
- reliability: hardware watchdog (3 s), persistent CWD/clock, ANSI-aware redraws
- smoother LCD console with optimized scrolling and software key repeat
- launcher, dashboard, system monitor, samples, and mini games including snake (with persistent high scores)

## Quick Start

### Build

From the repository root:

```bash
make picocalc
```

For the Pico 2 variant:

```bash
make pico2
```

For the Pico 2W variant:

```bash
make pico2w
```

Output images:

```text
picocalc/build/mellivora_picocalc.uf2
picocalc/build-pico2/mellivora_picocalc_pico2.uf2
picocalc/build-pico2w/mellivora_picocalc_pico2w.uf2
```

### Flash

1. Put the Pico or Pico 2 device into BOOTSEL mode.
2. Copy the matching UF2 file to the mounted drive.
3. Reboot into Mellivora PicoCalc.
4. Insert an SD card and run `mount` to enable filesystem features.

## Documentation

| Document | Purpose |
| --- | --- |
| [docs/INSTRUCTIONS.md](docs/INSTRUCTIONS.md) | Fast-start setup and first-use instructions |
| [docs/INSTALL.md](docs/INSTALL.md) | Setup, build, flash, and troubleshooting instructions |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Day-to-day usage of the shell, tools, apps, and languages |
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | Guided first session from boot to productive use |
| [docs/QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md) | Single-page command cheatsheet |
| [docs/COMMANDS.md](docs/COMMANDS.md) | Full categorized command reference |
| [docs/PROGRAMMING_GUIDE.md](docs/PROGRAMMING_GUIDE.md) | Extending the firmware and adding new commands or apps |
| [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md) | Internal architecture, subsystems, and runtime behavior |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Boot flow, modules, cores, and pipelines |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Pin map, board variants, and power numbers |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | Release notes (semver) |
| [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) | Build environment, style guide, PR checklist |
| [SECURITY.md](SECURITY.md) | Vulnerability reporting policy |
| [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) | Community standards |
| [ABOUT.md](ABOUT.md) | GitHub-friendly project summary and repository about text |

## Repository Layout

- `picocalc/` — active firmware source tree
- `picocalc/src/` — shell, hardware, filesystem, and app logic
- `docs/` — documentation set
- `Makefile` — root build entry for both Pico and Pico 2 firmware targets

## Project Status

Three first-class build variants are produced from a single source tree:

- **`picocalc`** — Raspberry Pi Pico (RP2040)
- **`pico2`** — Raspberry Pi Pico 2 (RP2350)
- **`pico2w`** — Raspberry Pi Pico 2 W (RP2350 + CYW43439 WiFi)

CI builds and signs all three UF2 images on every push to `main` and on
every release tag.

See [docs/CHANGELOG.md](docs/CHANGELOG.md) for release history and
[picocalc/README.md](picocalc/README.md) for the firmware-local overview.
