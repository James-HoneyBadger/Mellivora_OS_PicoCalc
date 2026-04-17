# Mellivora PicoCalc User Guide

This guide explains day-to-day use of Mellivora on PicoCalc hardware.

## 1. Overview

Mellivora PicoCalc provides:

- a text shell for local interaction
- LCD and serial mirrored output
- command history with arrow keys
- a writable FAT-backed SD card filesystem
- a set of Mellivora-style utility commands
- built-in BASIC and Tiny C language environments

## 2. Using the Shell

When the device boots, the PicoLair prompt appears. Commands are entered directly from the keyboard.

Helpful shell behaviors:

- Up and down arrows browse history
- Ctrl-C cancels the current line in interactive tools
- pwd prints the current directory
- cd changes the working directory

## 3. Core Commands

### System commands

- help — show command reference
- uname — show platform details
- uptime — show time since boot
- clear — clear the display
- battery — read battery percentage if available
- backlight N — set keyboard backlight from 0 to 255
- reboot — restart the firmware

### Storage commands

- mount — mount the SD card filesystem
- ls [path] — list files
- dir [path] — alias for ls
- cd [path] — change directory
- pwd — print working directory
- cat PATH — display a text file
- touch PATH — create an empty file
- write PATH TEXT — replace a file with text
- mkdir PATH — create a directory
- rm PATH — remove a file or empty directory
- sdinfo — show SD state
- sdread LBA — dump one raw 512-byte block

## 4. Ported Utilities

Available utility-style programs include:

- hello
- basename
- dirname
- seq
- head
- tail
- wc
- cut
- grep
- find
- pager
- rev
- sort
- hexdump
- od
- calc
- cp
- mv
- stat
- edit
- browse
- notes
- home
- dashboard
- sysmon
- script
- paint
- samples
- clock
- cal
- sleep
- id
- true
- false

These are invoked from the shell as normal commands.

## 5. BASIC

Start the interactive BASIC environment with:

```text
basic
```

You can also run a BASIC source file from storage:

```text
basic /path/program.bas
```

Supported features:

- line-numbered program editing
- RUN, LIST, NEW
- PRINT and shorthand ?
- LET variable assignment
- INPUT for integer values
- IF ... THEN ...
- FOR ... NEXT loops
- GOSUB and RETURN
- CLS, GOTO, END, and STOP

Example session:

```text
10 FOR A = 1 TO 5
20 PRINT A
30 NEXT A
40 GOSUB 100
50 END
100 PRINT "DONE"
110 RETURN
RUN
```

Type BYE, EXIT, or QUIT to leave BASIC mode.

## 6. Tiny C

Start the Tiny C environment with:

```text
tcc
```

Or run a source file:

```text
tcc /path/test.tc
```

Supported features:

- integer variables and small arrays
- assignment and arithmetic
- bitwise operators such as &, |, and ^
- print and puts
- if conditions
- while loops
- vars to inspect state
- clear to reset the environment
- exit, quit, or return to leave

Example session:

```text
int x = 1;
int arr[4];
arr[0] = 42;
print(arr[0]);
x = x | 2;
vars
```

## 7. Browsing Files Interactively

Start the browser with:

```text
browse
```

Or open a specific directory:

```text
browse /path
```

Browser controls:

- `j` or `s` — move down
- `k` or `w` — move up
- `Enter` — open directory or view file
- `e` — edit selected file
- `u` — go to parent directory
- `r` — refresh
- `q` — quit browser

## 8. Editing Files On Device

Start the line editor with:

```text
edit /path/file.txt
```

Supported editor commands include:

- list
- append
- ins N TEXT
- set N TEXT
- del N
- save
- quit

This provides a compact on-device workflow for notes, scripts, and source files.

## 9. Notes, Dashboard, and Home Launcher

Quick notes workflow:

```text
notes
```

This opens the default notes file and drops into the on-device editor.

You can also open the launcher with:

```text
home
```

The launcher provides quick access to browsing, notes, the calculator, BASIC, Tiny C, and the dashboard.

You can open the status dashboard with:

```text
dashboard
```

This shows battery, uptime, current path, and quick shortcuts.

For a more detailed live system readout, use:

```text
sysmon
```

You can also use the time tools:

```text
clock
cal 4 2026
```

Bundled demo programs are available with:

```text
samples
```

Useful forms:

- samples list
- samples show hello.bas
- samples write count.bas
- samples run tinyc.tc

## 10. Running Shell Scripts

You can batch commands in a text file and run them with:

```text
script myscript.txt
```

The script file should contain one command per line. Lines starting with # are comments and empty lines are ignored.

Example script file:

```text
# My script
echo Hello from script
ls
calc 2 + 3
```

## 11. Pixel Paint Application

Open the simple drawing app with:

```text
paint
```

Controls:

- Arrow keys: move cursor
- Space: toggle drawing mode
- c: clear screen
- s: save drawing to file
- l: load drawing from file
- q: quit

Drawings are saved as text art with # for pixels.

## 12. Typical Workflow

1. Boot the device.
2. Insert an SD card.
3. Run mount.
4. Use browse to inspect files and notes to keep quick text notes.
5. Use edit, pager, calc, basic, and tcc for interactive work.

## 13. Current Limitations

- Utilities focus on compact embedded behavior rather than full desktop compatibility
- Interactive language support is integer-oriented and intentionally small
- Some commands do not yet support every flag found in larger Unix systems
