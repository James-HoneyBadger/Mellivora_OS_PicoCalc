# Mellivora PicoCalc User Guide

This guide explains everyday use of Mellivora PicoCalc. It is aimed at users who want to work directly from the built-in shell and apps.

## 1. What Mellivora PicoCalc Is

Mellivora PicoCalc is a compact shell environment for PicoCalc-class hardware using RP2040 (Pico), RP2350 (Pico 2), and RP2350 + CYW43 (Pico 2W) boards. It is designed to make the device feel like a tiny portable workstation with:

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
- `Tab` completes command names
- `Ctrl-U` clears the current line
- `Ctrl-L` redraws the screen
- `Ctrl-C` cancels the current line or exits many interactive tools
- `help` and `man` show built-in documentation

Advanced shell features:

- **Command chaining**: separate commands with `;` to run several on one line (e.g. `mount ; ls`)
- **Output redirection**: `cmd > FILE` writes output to a file, `cmd >> FILE` appends
- **Quoted arguments**: wrap an argument in double quotes to preserve spaces (e.g. `echo "hello world"`)
- **Alias expansion**: aliases expand automatically up to 8 levels deep
- **Key repeat**: holding a key generates repeated keystrokes after a short delay

## 3. Essential System Commands

| Command | What it does |
| --- | --- |
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
| `echo TEXT` | Print text to the console |
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
- `cp SRC DST` to copy files (also copies directories one level deep)
- `mv SRC DST` to move or rename files
- `rename OLD NEW` to rename a file in the same directory
- `stat FILE` for quick file details
- `tree` for a recursive directory view
- `du` for directory size totals
- `df` for total, used, and free space
- append `| more` to long output when you want paging on the LCD

Text processing commands:

- `grep [-n] [-e] PATTERN FILE` to search file contents (`-n` shows line numbers, `-e` forces regex)
- `sort [-r] [-n] FILE` to sort lines (`-r` reverse, `-n` numeric order)
- `find [PATH] [-name PATTERN] [-type f|d]` to find files or directories
- `head [-n NUM] FILE` to show the first N lines
- `tail [-n NUM] FILE` to show the last N lines
- `cut [-d CHAR] [-f NUM] FILE` to extract fields
- `wc FILE` for line, word, and byte counts
- `rev FILE` to reverse each line
- `seq N` to print numbers 1 through N
- `od FILE` for octal/hex dump
- `hexdump FILE` for hex + ASCII dump

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

Use it to inspect or patch files at the byte level. The `find` subcommand searches for hex byte sequences within the file.

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

Tracks recurring routines and completion counts. The habits tracker maintains current and best streaks for consecutive daily completions.

### Bookmarks

```text
bookmarks
```

Saves favorite files, paths, and launch targets for quick access. Each bookmark stores the current working directory at creation time so relative paths work correctly when opened later.

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

A simple drawing mode.

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

These commands provide quick play sessions and simple demos. Snake maintains a persistent high score saved to the SD card.

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

## 12. Network Commands (Pico 2W Only)

If you are running the Pico 2W firmware build, the following WiFi and network commands are available.

### Connect to WiFi

```text
wifi scan
wifi connect MyNetwork
```

You will be prompted for the password. Once connected, the device has a full TCP/IP stack.

### Check connection status

```text
wifi status
ifconfig
```

### Useful network tools

| Command | What it does |
| --- | --- |
| `ping HOST` | Send ICMP echo requests to a host |
| `dns HOSTNAME` | Resolve a hostname to an IP address |
| `ntp` | Synchronise time from an NTP server |
| `fetch URL` | HTTP GET and display the response body |
| `wget URL FILE` | HTTP GET and save the response to a file |
| `weather LOCATION` | Fetch weather data for a location |
| `irc` | Minimal IRC client |
| `telnet HOST [PORT]` | Minimal telnet client |
| `netstat` | Show network connection status |

### Disconnect

```text
wifi disconnect
```

## 13. Additional Shell Commands

These smaller built-in commands are useful in scripts and day-to-day work:

| Command | What it does |
| --- | --- |
| `echo TEXT` | Print text to the console |
| `sleep N` | Pause for N seconds |
| `clock` | Show the current time |
| `cal` | Show a calendar |
| `id` | Show the current user identity |
| `basename PATH` | Print the filename part of a path |
| `dirname PATH` | Print the directory part of a path |
| `sdinfo` | Show SD card hardware status |
| `sdread LBA` | Read a raw SD block (debugging) |
| `true` / `false` | Return success or failure (for scripts) |

## 14. Suggested Daily Workflow

A practical device-first session often looks like this:

1. boot the device
2. run `mount`
3. check `dashboard`
4. open `notes`, `todo`, or `planner`
5. browse files with `browse`
6. edit scripts or text with `edit`
7. use `calc`, `basic`, or `tcc` for quick work

## 15. If Something Fails

- run `mount` again if storage commands stop working
- use `pwd` and `ls` to confirm your location
- use `help fs` or `man TOPIC` for syntax reminders
- reboot if an interactive tool becomes unresponsive

## 16. Current Limitations

- Utilities focus on compact embedded behavior rather than full desktop compatibility
- Interactive language support is integer-oriented and intentionally small
- Some commands do not yet support every flag found in larger Unix systems
- File names must follow 8.3 format (uppercase only, no long filenames)
- Maximum file size for in-memory operations is limited by available RAM
