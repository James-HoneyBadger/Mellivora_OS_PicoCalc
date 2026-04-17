# Mellivora PicoCalc Technical Reference

This document describes the current firmware architecture and subsystem responsibilities.

## 1. Target Platform

Supported hardware target:

- RP2040-based Clockwork PicoCalc class device

Primary hardware interfaces:

- LCD on SPI1
- keyboard co-processor on I2C1
- SD card on SPI0
- USB CDC and UART console output

## 2. Firmware Structure

Top-level firmware tree:

- picocalc/src/main.c — boot, shell loop, command dispatch
- picocalc/src/apps.c — ported Mellivora-style applications and interpreters
- picocalc/src/syscall.c and related headers — app helper interface
- picocalc/src/fat.c — FAT-backed filesystem access
- picocalc/src/sd.c — block device communication
- picocalc/src/lcd.c — display output
- picocalc/src/kbd.c — keyboard and battery access

## 3. Boot and Runtime Flow

1. Hardware is initialized.
2. Console output is mirrored to LCD and serial.
3. The shell enters an input loop.
4. Built-in commands are processed directly.
5. Utility-style commands are dispatched through the app layer.

## 4. Shell Characteristics

Shell properties:

- fixed-size line input buffer
- command history ring
- current working directory tracking
- absolute and relative path resolution
- mirrored character output to both device display and serial console

## 5. Filesystem Model

The filesystem layer currently provides:

- FAT mount support
- directory listing
- file read support
- file creation and overwrite support
- directory creation
- file or empty-directory removal
- directory validation for cd

Users must run mount before filesystem-driven commands are available.

## 6. Built-in Command Reference

| Command | Purpose |
|---|---|
| help | Print shell help |
| uname | Print platform identity |
| uptime | Show milliseconds since boot |
| clear | Clear the LCD/console view |
| battery | Read battery percentage |
| backlight N | Set keyboard backlight |
| mount | Mount SD card FAT volume |
| ls or dir | List directory contents |
| cd | Change working directory |
| pwd | Print working directory |
| cat | Print a text file |
| echo | Output text |
| touch | Create empty file |
| write | Write text into file |
| mkdir | Create directory |
| rm | Remove file or empty directory |
| sdinfo | Report SD card status |
| sdread | Dump raw sector contents |
| reboot | Warm reboot |

## 7. Ported App Layer Reference

The app layer currently contains:

- file and text utilities such as head, tail, wc, cut, grep, sort, pager, rev, hexdump, od, calc, cp, mv, stat, edit, browse, notes, home, dashboard, sysmon, script, paint, samples, clock, and cal
- identity and control helpers such as hello, id, sleep, true, and false
- embedded language runtimes: BASIC and Tiny C

Dispatch is handled centrally through the app runner so shell commands map into a compact, firmware-safe implementation.

## 8. BASIC Runtime Reference

The BASIC runtime is intentionally lightweight.

Supported statements:

- PRINT
- ?
- LET
- INPUT
- IF condition THEN statement
- FOR ... TO ... STEP ... and NEXT
- GOSUB and RETURN
- CLS or CLEAR
- GOTO lineNumber
- LIST
- NEW
- END or STOP

Execution modes:

- interactive REPL
- stored line-numbered program mode
- batch file execution from the filesystem

Data model:

- integer variables A through Z
- integer arithmetic and simple comparisons

## 9. Tiny C Runtime Reference

The Tiny C runtime is a compact interpreter-like command environment rather than a full ISO C compiler.

Supported syntax areas:

- int variable declarations
- small integer arrays
- assignment
- arithmetic and bitwise expressions
- relational comparisons
- print and puts
- if statements
- while loops
- vars state dump and clear

Current constraints:

- integer-only values
- no function definitions
- no heap management
- no preprocessing or separate compilation

## 10. Build Output

The current firmware artifact is:

- picocalc/build/mellivora_picocalc.uf2

## 11. Scope Notes

This repository no longer targets the former PC bootloader and x86 runtime. The maintained product is the RP2040 PicoCalc firmware.
