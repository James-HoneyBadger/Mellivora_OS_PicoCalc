# Mellivora PicoCalc User Guide

This guide explains everyday use of Mellivora PicoCalc on real hardware. It is aimed at users who want to work directly from the built-in shell and apps.

## 1. What Mellivora PicoCalc Is

Mellivora PicoCalc is a compact shell environment for PicoCalc-class hardware using RP2040 and Pico 2 class RP2350 boards. It is designed to make the device feel like a tiny portable workstation with:

- a text shell
- mirrored LCD and serial output
- an SD-card-backed filesystem
- built-in productivity tools
- lightweight creative tools and games
- small programming environments for BASIC and Tiny C

## 2. First Things to Know

When the firmware boots, you arrive at the command prompt.

Useful shell behavior:

- Up and Down browse history
- `Ctrl-U` clears the current line
- `Ctrl-L` redraws the screen
- `Ctrl-C` exits many interactive tools
- `help` and `man` show built-in documentation

## 3. Essential System Commands

| Command | What it does |
|---|---|
| `help` | Show the command overview |
| `man TOPIC` | Show a deeper help page |
| `history` | Show or replay recent commands |
| `alias` | Create a persistent shortcut command |
| `unalias` | Remove a saved alias |
| `uname` | Show platform details |
| `uptime` | Show time since boot |
| `battery` | Report battery state when available |
| `backlight N` | Set keyboard backlight from 0 to 255 |
| `clear` | Clear the display and console |
| `reboot` | Restart the device |

## 4. Working with Files and Storage

Before using storage-backed tools, insert your SD card and run:

```text
mount
```

Common file commands:

- `ls` or `dir` to list files
- `cd` to change directory
- `pwd` to print the current path
- `cat FILE` to display a text file
- `touch FILE` to create an empty file
- `write FILE TEXT` to replace a file with text
- `mkdir DIR` to create a directory
- `rm PATH` to remove a file or empty directory
- `cp SRC DST` to copy files
- `mv SRC DST` to move or rename files
- `stat FILE` for quick file details
- `tree` for a recursive directory view
- `du` for directory size totals
- `df` for total, used, and free space
- append `| more` to long output when you want paging on the LCD

## 5. Interactive File Tools

### File browser

Run:

```text
browse
```

Use it to move around the SD card, inspect files, and open content quickly.

### Text editor

Run:

```text
edit /NOTES.TXT
```

This opens the compact line editor for writing notes, scripts, and small source files.

### Hex editor

Run:

```text
hexedit /FILE.BIN
```

Use it to inspect or patch files at the byte level.

## 6. Personal Productivity Apps

Mellivora PicoCalc includes several persistent apps backed by files on the SD card.

### Notes

```text
notes
```

Opens the default notes file in the built-in editor.

### Todo list

```text
todo
```

Use it to manage simple tasks and keep a portable to-do list.

### Planner

```text
planner
```

Stores dated agenda items and helps you review upcoming events.

### Journal

```text
journal
```

Keeps dated journal entries for logs, thoughts, or field notes.

### Habits

```text
habits
```

Tracks recurring routines and completion counts.

### Bookmarks

```text
bookmarks
```

Saves favorite files, paths, and launch targets for quick access.

### Settings

```text
settings
set backlight 180
```

Settings persist across boots and can control behavior such as defaults and startup preferences.

## 7. Launchers and Status Views

### Home launcher

```text
home
```

A simple menu for jumping into the most common tools.

### Dashboard

```text
dashboard
```

Shows system status, path information, and app shortcuts.

### System monitor

```text
sysmon
```

Provides a more detailed status readout.

## 8. Creative and Fun Tools

### Paint

```text
paint
```

A simple on-device drawing mode.

### Sprite editor

```text
sprite
```

A tiny sprite editing utility for pixel patterns and experiments.

### Terminal mode

```text
terminal
```

Switches into a raw terminal view for direct character interaction.

### Games pack

```text
games
snake
dice 2 6
coin 5
guess
```

These commands provide quick play sessions and simple demos.

## 9. Calculator and Scripting

### Calculator

```text
calc 2+2*8
```

Useful for quick arithmetic from the shell.

### Script runner

```text
script /AUTOEXEC.SH
```

Lets you execute command files directly from storage.

### Samples

```text
samples
```

Lists or writes bundled example programs for exploration and learning.

## 10. BASIC

Start BASIC with:

```text
basic
```

Or run a stored program:

```text
basic /HELLO.BAS
```

BASIC supports:

- line-numbered program editing
- `RUN`, `LIST`, and `NEW`
- `PRINT`, `LET`, and `INPUT`
- `IF ... THEN`
- `FOR ... NEXT`
- `GOSUB` and `RETURN`
- `CLS`, `GOTO`, `END`, and `STOP`

Example:

```text
10 FOR A = 1 TO 3
20 PRINT A
30 NEXT A
RUN
```

## 11. Tiny C

Start Tiny C with:

```text
tcc
```

Or run a saved file:

```text
tcc /TEST.TC
```

Tiny C supports integer-oriented experiments such as:

- variable declarations
- small arrays
- arithmetic and bitwise operations
- `if` statements
- `while` loops
- `print`, `puts`, `vars`, and `clear`

Example:

```text
int n = 5;
print(n);
n = n + 1;
vars
```

## 12. Suggested Daily Workflow

A practical device-first session often looks like this:

1. boot the device
2. run `mount`
3. check `dashboard`
4. open `notes`, `todo`, or `planner`
5. browse files with `browse`
6. edit scripts or text with `edit`
7. use `calc`, `basic`, or `tcc` for quick work

## 13. If Something Fails

- run `mount` again if storage commands stop working
- use `pwd` and `ls` to confirm your location
- use `help fs` or `man TOPIC` for syntax reminders
- reboot if an interactive tool becomes unresponsive

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
