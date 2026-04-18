# Mellivora PicoCalc Quick Instructions

This page is the short version of the documentation set. Use it when you want the essentials without reading the full guides.

## Build

From the repository root:

```bash
make picocalc
```

This produces:

```text
picocalc/build/mellivora_picocalc.uf2
```

## Flash

1. Hold BOOTSEL on the RP2040 device.
2. Connect USB.
3. Copy the UF2 to the mounted drive.
4. Wait for the reboot.

## First Commands to Try

```text
help
dashboard
mount
ls
home
notes
```

## Most Useful Daily Commands

- `browse` for files
- `edit` for text editing
- `todo` for task tracking
- `planner` for dated items
- `journal` for notes by date
- `habits` for routine tracking
- `bookmarks` for saved shortcuts
- `calc`, `basic`, and `tcc` for quick computing and experiments

## If You Need More Detail

- full setup: [INSTALL.md](INSTALL.md)
- daily usage: [USER_GUIDE.md](USER_GUIDE.md)
- step-by-step onboarding: [TUTORIAL.md](TUTORIAL.md)
- development internals: [PROGRAMMING_GUIDE.md](PROGRAMMING_GUIDE.md)
- architecture details: [TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md)
