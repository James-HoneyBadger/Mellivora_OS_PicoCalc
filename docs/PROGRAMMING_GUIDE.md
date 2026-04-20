# Mellivora PicoCalc Programmer's Guide

This guide is for contributors who want to understand the firmware structure, extend the shell, port new utilities, or maintain the PicoCalc codebase safely.

## 1. Development Philosophy

Mellivora PicoCalc is an embedded system first. The code is optimized for:

- predictable memory use
- bounded buffers
- direct keyboard-and-screen workflows
- robust SD-card file handling
- short feedback loops on real hardware

Whenever possible, features should feel natural in a handheld shell rather than like a cut-down desktop port.

## 2. Source Layout

Important files and responsibilities:

- `picocalc/src/main.c` — boot path, shell loop, built-in command handlers, help, history, aliasing
- `picocalc/src/apps.c` — most user-facing apps and utility ports
- `picocalc/src/fat.c` and `fat.h` — FAT-backed filesystem logic
- `picocalc/src/syscall.c` and related headers — app-facing helper surface
- `picocalc/src/sd.c` — low-level SD card block I/O
- `picocalc/src/lcd.c` — display handling
- `picocalc/src/kbd.c` — keyboard and battery support
- `picocalc/src/net.c` and `net.h` — WiFi, TCP, DNS, ICMP, NTP, HTTP (Pico 2W only)
- `picocalc/src/netapps.c` and `netapps.h` — network application commands (Pico 2W only)
- `picocalc/src/lwipopts.h` — lwIP stack configuration (Pico 2W only)

## 3. Normal Edit and Test Cycle

1. edit code under `picocalc/src/`
2. rebuild from the repository root with:

```bash
make picocalc
```

3. flash the generated UF2 to hardware
4. test the command or app directly in the shell
5. update docs when user-visible behavior changes

## 4. Choosing Between Built-ins and Apps

### Add a shell built-in when:

- the command is core to system management
- it touches boot, battery, backlight, mount, or other platform behavior
- it belongs beside `help`, `uname`, `history`, or `reboot`

### Add an app-layer command when:

- the feature behaves like a utility or small application
- it mostly uses files, text, screen output, or lightweight input loops
- it fits the existing app dispatch table model

Most user-facing expansions belong in the app layer.

## 5. Adding a New Built-in Command

Typical process:

1. implement the handler in `main.c`
2. validate arguments carefully
3. print clear usage text on bad input
4. add entries to `help` and `man`
5. test interactively on target hardware

Built-ins should stay small and focused.

## 6. Adding a New App or Utility

Recommended pattern:

1. create a static function with the signature `app_name(const char *arg)`
2. keep parsing logic small and defensive
3. use the syscall and filesystem helpers instead of bypassing the common interfaces
4. register names and aliases in the app dispatcher table
5. add launcher or dashboard integration if the feature is high-value for users
6. document it in the guides

This is the preferred model for tools such as notes, planner, games, editors, and file utilities.

## 7. Persistent Data Design

Many apps save their state to plain files on the SD card. This approach is recommended because it keeps the system transparent and debuggable.

Examples include:

- settings (`SETTINGS.CFG`)
- todo items (`TODO.TXT`)
- planner entries (`PLANNER.TXT`)
- journal data (`JOURNAL.TXT`)
- habits tracking (`HABITS.TXT`) — uses pipe-delimited format with streak fields
- bookmarks (`BOOKMARKS.CFG`) — stores label, target, and CWD context
- aliases (`ALIASES.CFG`)
- high scores (`HISCORE.TXT`)

Guidelines:

- prefer readable text formats where practical
- use pipe (`|`) delimiters for multi-field records
- tolerate missing or short fields for backwards compatibility
- create sensible defaults on first run
- never assume unlimited path or record length

## 7a. Available Filesystem Functions

The FAT layer (`fat.h`) provides these functions for app development:

| Function | Purpose |
| --- | --- |
| `fat_mount()` | Mount the SD card |
| `fat_ls(path, cb, ctx)` | Enumerate directory entries |
| `fat_open(path, f)` | Open a file for reading |
| `fat_read(f, buf, n)` | Read bytes from an open file |
| `fat_create(path, data, len)` | Create or overwrite a file |
| `fat_append(path, data, len)` | Append data to a file (creates if missing) |
| `fat_rename(old, new)` | Rename a file (same directory) |
| `fat_unlink(path)` | Delete a file or empty directory |
| `fat_mkdir(path)` | Create a directory |
| `fat_is_dir(path)` | Check if path is a directory |
| `fat_get_usage(out)` | Get filesystem capacity info |

## 8. Coding Rules for Embedded Safety

When modifying the firmware:

- prefer fixed-size arrays over dynamic allocation
- bound all copies and concatenations
- validate file paths and user input
- keep loops responsive and escapable
- avoid recursion unless the depth is explicitly constrained
- reduce hidden global state where practical

If a feature needs a large buffer, make the limit explicit and document the behavior.

## 9. UI and Interaction Conventions

The best Mellivora PicoCalc tools:

- work entirely from the keyboard
- show short command summaries or prompts
- provide a clear quit path such as `q` or `Ctrl-C`
- avoid cluttering the display with long, noisy output
- degrade gracefully when storage is unavailable

## 10. Documentation Expectations

Any user-facing change should update at least one of the following:

- the root README for GitHub visitors
- the user guide for day-to-day use
- the tutorial if the feature changes onboarding
- the technical or programmer guide if internal behavior changed

## 11. Writing BASIC and Tiny C Samples

The sample systems are most useful when examples are:

- short
- readable on the device display
- based on integer logic
- safe to run repeatedly

Good examples include counters, loops, conditionals, and small file-oriented experiments.

## 12. Testing Checklist for Contributors

Before considering a change complete:

- build the firmware successfully
- verify the new command appears in help or launcher surfaces if appropriate
- test both success and bad-input cases
- confirm the SD card path still behaves correctly
- make sure the change does not break older commands

## 13. Recommended Future Refactors

As the system grows, the biggest architectural payoff will likely come from:

- splitting `apps.c` into themed modules
- improving automated command documentation generation
- expanding the sample library
- widening interpreter coverage while staying within RP2040 limits
