# Mellivora PicoCalc

Mellivora is now a PicoCalc-focused RP2040 firmware project.

## Build

Requirements:

- CMake 3.13+
- `arm-none-eabi-gcc`
- `PICO_SDK_PATH` set to a local Pico SDK checkout

Build the firmware with:

```bash
make picocalc
```

Generated output:

```text
picocalc/build/mellivora_picocalc.uf2
```

Copy the UF2 to the device while it is mounted in BOOTSEL mode.

## Layout

- `picocalc/` — active firmware source tree
- `Makefile` — root build entry
- `LICENSE` — project license
- `docs/` — user and developer documentation

## Documentation

- [docs/INSTALL.md](docs/INSTALL.md) — setup, build, flash, troubleshooting
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) — shell usage and language runtime basics
- [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md) — architecture and subsystem reference
- [docs/PROGRAMMING_GUIDE.md](docs/PROGRAMMING_GUIDE.md) — extending Mellivora and writing apps

See `picocalc/README.md` for shell features and runtime details.
