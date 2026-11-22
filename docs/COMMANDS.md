# Command Reference

This is the canonical reference for all built-in commands and apps.
For the user-friendly tour, see [USER_GUIDE.md](USER_GUIDE.md).

## Conventions

- `arg` — required argument
- `[arg]` — optional argument
- Multiple commands can be chained on one line with `;`, `&&`, or `||`
- Output can be redirected with `> file` (truncate) or `>> file` (append)
- Aliases are expanded before dispatch (`alias ll="ls -l"`)
- `ls` and `browse` show VFAT long names when present; paths may use them
- `$NAME`, `${NAME}`, `$?`, and `$((EXPR))` are expanded before dispatch
- Unquoted wildcards (`*`, `?`, `[abc]`) run the command once per match
- Pipelines (`cmd1 | cmd2 | cmd3`) are buffered in RAM; `-` or no file reads the upstream stage.
  Up to 4 stages are supported and each intermediate result is capped at 4 KiB;
  longer output prints `pipe: output truncated`
- Unquoted wildcards match 8.3 and VFAT long names; matched names with spaces are quoted automatically
- `/PROFILE.CFG` runs at boot; use it for aliases, variables, and `PROMPT`
- `Ctrl-C` cancels long-running commands
- `Ctrl-R` searches command history

## System

| Command                    | Description                                            |
|----------------------------|--------------------------------------------------------|
| `version`                  | Print firmware version, build timestamp, project URL   |
| `uname [-a]`               | OS name; with `-a` adds hardware/build details         |
| `sysinfo`                  | Live core, heap, SD throughput, and battery-gauge diagnostics |
| `gfxbench [-n N] [-big] [-quiet]` | Measure full-surface and dirty-rect panel throughput |
| `ramdisk [op ...]`         | In-memory files: `ls`, `cat NAME`, `write NAME TEXT`, `rm NAME` |
| `dmesg`                    | Kernel-style boot log                                  |
| `reboot`                   | Soft reset via watchdog                                |
| `shutdown` / `halt` / `poweroff` | Flush state, blank display, halt CPU             |
| `sleep`                    | Display-off low-power mode (any key wakes)             |
| `sleep MS`                 | Pause for MS milliseconds (script use)                 |
| `clear`                    | Clear screen                                           |
| `battery`                  | Show battery percentage and charging state             |
| `backlight N`              | Set display/keyboard backlight level                   |
| `echo TEXT`                | Print text                                             |
| `id`                       | Always prints `mellivora`                              |
| `true` / `false`           | Exit status helpers (for scripts)                      |
| `lock`                     | Screen lock with a 4-digit PIN (not encryption)         |
| `screenshot [file]`        | Save current LCD text cells to a file                  |
| `cursor [STYLE]`           | Configure shell cursor (`on off block underscore`)   |

## File System

| Command                      | Description                                          |
|------------------------------|------------------------------------------------------|
| `mount` / `umount` / `unmount` | Mount/unmount the SD card                          |
| `ls [-l] [path]`             | List directory                                       |
| `dir [path]`                 | Alias for `ls`                                       |
| `cd [path]`                  | Change working directory (persistent)                |
| `pwd`                        | Print working directory                              |
| `touch <file>`               | Create empty file                                    |
| `mkdir <path>`               | Create directory                                     |
| `rm [-rRf] <path>`           | Remove file (or recursively for directories)         |
| `cp [-r] <src> <dst>`        | Copy file or directory                               |
| `mv <src> <dst>`             | Move a file (streams across directories)             |
| `rename <old> <new>`         | Rename within the same directory                     |
| `cat <file>`                 | Display file contents                                |
| `sdinfo`                     | Show SD-card status and geometry                     |
| `sdread LBA`                 | Read and inspect a raw SD block                      |
| `sdcheck`                    | Read-only check of SD initialization and sector 0    |
| `backup create [DIR]`        | Copy known shell and app data into a versioned directory |
| `backup list [DIR]`          | List a backup directory                              |
| `backup restore [DIR]`       | Restore only missing files; existing files are skipped |
| `more <file>` / `less`       | Paged display                                        |
| `pager <file>`               | Paged display alias                                  |
| `head [-n N] <file>`         | First N (or 10) lines                                |
| `tail [-n N] <file>`         | Last N (or 10) lines                                 |
| `wc [-lwc] <file>`           | Line / word / byte counts                            |
| `cut -dCHR -fN <file>`       | Extract a column                                     |
| `sort <file>`                | Sort lines                                           |
| `rev <file>`                 | Reverse characters per line                          |
| `uniq <file>`                | Collapse adjacent duplicate lines                    |
| `grep [-i] PATTERN <file>`   | Regex search                                         |
| `tr [-d] SET1 [SET2] [file]` | Translate or delete characters (`a-z`, `\n`)       |
| `sed s/OLD/NEW/[g] [file]`  | Literal substitution                                 |
| `md <file>` / `markdown`    | Render Markdown for the LCD                          |
| `find <path> -name PATTERN`  | Find files                                           |
| `tree [path]`                | Recursive tree view                                  |
| `du <path>`                  | Disk usage                                           |
| `df`                         | Free space                                           |
| `disk` / `space`             | Aliases for `df`                                     |
| `stat <file>`                | File metadata                                        |
| `hexdump` / `xxd` / `od`     | Binary dump                                          |
| `hexedit` / `hex`            | Hex editor (files larger than the buffer open view-only) |
| `strings <file>`             | Printable ASCII runs                                 |
| `diff <a> <b>`               | Line-based diff                                      |
| `base64 [-d] [-o OUT] <file>`| Encode (default) or decode (`-d`) a file; never overwrites input |
| `crc32 <file>`               | CRC-32 (ISO-HDLC) checksum of a file                 |
| `tee <file>`                 | Tee stdin to file (and stdout)                       |
| `write <file>`               | Read stdin until `.` line, write to file             |
| `edit <file>` / `bedit`      | Line editor (files up to the RAM buffer; long lines refused) |
| `browse` / `files`           | Visual file browser                                  |

## Shell

| Command                 | Description                                              |
|-------------------------|----------------------------------------------------------|
| `history`               | Show command history                                     |
| `man [topic]`           | Show detailed help pages                                 |
| `help -k KEYWORD`       | Search command names and groups                          |
| `alias [name=value]`    | List or define aliases                                   |
| `unalias <name>`        | Remove alias                                             |
| `env`                   | Show environment / variables                             |
| `set X=Y`               | Set a shell variable                                     |
| `unset NAME`            | Remove a shell variable                                  |
| `read [-p TEXT] NAME...`| Read a line into one or more variables                   |
| `which CMD...` / `type` | Locate a command, alias, or file                         |
| `expr EXPR`             | Evaluate an integer expression                           |
| `script <file>`         | Run a script through the full shell parser (also `. FILE`) |
| `watch CMD [SEC]`       | Re-run a shell command every SEC (default 2) until any key |
| `yes [STRING]`          | Repeatedly print STRING                                  |
| `seq N` / `seq A B`     | Numeric sequence                                         |
| `hello`                 | Greeting (sample command)                                |
| `basename PATH`         | Print final path component                               |
| `dirname PATH`          | Print parent directory component                         |
| `help`                  | Show categorized help                                    |
| `time CMD`              | Run CMD and print wall-clock elapsed time                |
| `repeat N CMD`          | Run CMD N times (1–1000)                                 |
| `printf FORMAT [ARGS...]` | Formatted print with `\n` / `%s` / `%d` / `%x`         |

## Time & Calendar

| Command           | Description                                              |
|-------------------|----------------------------------------------------------|
| `date`            | Show current date/time                                   |
| `date YYYY-MM-DD HH:MM:SS` | Set the software RTC manually                   |
| `uptime`          | Show time since boot                                     |
| `clock`           | Full-screen clock app                                    |
| `cal` / `calendar`| Month / planner views                                    |

## Calculator / Programming

| Command                | Description                                          |
|------------------------|------------------------------------------------------|
| `calc EXPR` / `calc`   | Quick math (RPN + infix)                             |
| `basic [file]`         | BASIC interpreter                                    |
| `tcc` / `tinyc`        | Tiny C interpreter                                   |
| `forth`                | Forth interpreter                                    |

## Networking (Pico 2W only)

| Command                          | Description                                  |
|----------------------------------|----------------------------------------------|
| `wifi scan` / `wifi connect SSID PASS` | Manage WiFi                            |
| `wifi status` / `wifi forget`    | Connection state / saved-network management  |
| `ifconfig`                       | Show IP / netmask / gateway / DNS            |
| `ping HOST`                      | ICMP ping                                    |
| `dns HOST`                       | Resolve hostname                             |
| `ntp [server]`                   | Sync RTC from NTP                            |
| `fetch URL`                      | HTTP or verified HTTPS GET (display body)     |
| `wget URL FILE`                  | HTTP or verified HTTPS GET to file            |
| `weather [LOCATION]`             | Fetch weather forecast                       |
| `irc HOST PORT NICK CHAN`        | Minimal IRC client                           |
| `telnet HOST PORT`               | Telnet client                                |
| `mpkg`                           | Package manager (see below)                  |
| `netstat`                        | Show open sockets                            |

### mpkg — package manager

`mpkg` fetches a JSON repository index over certificate-verified HTTPS,
checks every artifact against its SHA-256 digest, and installs it on the SD
card. It is compiled only into the Pico 2 W image; other builds answer with a
one-line explanation and link no download or crypto code.

| Command | Description |
| --- | --- |
| `mpkg repo [URL]` | Show or save the index URL in `/MPKG.CFG` |
| `mpkg update` | Download and cache the index at `/apps/repo.json` |
| `mpkg list [TEXT]` | List packages; `*` installed, `+` upgrade available |
| `mpkg info NAME` | Version, size, kind, digest, and install status |
| `mpkg install NAME [--force]` | Download, verify, and install |
| `mpkg upgrade [NAME\|--all]` | Upgrade one package or everything installed |
| `mpkg installed` | Packages recorded on this card |
| `mpkg remove NAME` | Delete the file and its state entry |
| `mpkg verify` | Re-hash installed files |

Destinations are fixed by package kind: `mapp` → `/apps`, `theme` →
`/themes`, `script` → `/bin`. Installation state lives in
`/apps/.mpkg-state.json`. A TLS connection needs a PEM CA certificate at
`/CA.CRT` and a set clock (`ntp` or `date`).

#### Repository index format

```json
{
  "schema": 1,
  "name": "Example repository",
  "packages": [
    {
      "name": "hello",
      "version": "1.0.0",
      "kind": "mapp",
      "file": "hello.mapp",
      "url": "https://example.org/pkgs/hello-1.0.0.mapp",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "size": 4096,
      "description": "Hello world module"
    }
  ]
}
```

- `schema` must be `1`.
- `kind` is `mapp`, `theme`, or `script`.
- `file` is a bare name; path separators and `..` are rejected.
- `sha256` is the lowercase or uppercase hex digest of the artifact.
- `size` is optional but recommended: it aborts oversized downloads early.
- Entries that fail validation are skipped and counted; the rest still load.

The index is cached at `/apps/repo.json` and re-read on every command, so
`mpkg update` is only needed when the repository changes.

## Personal Productivity

| Command                | Description                                          |
|------------------------|------------------------------------------------------|
| `notes` / `memo`       | Note pad (persistent)                                |
| `journal` / `diary`    | Dated journal                                        |
| `todo` / `tasks`       | Todo list                                            |
| `planner` / `agenda` / `plan` | Daily / weekly planner                        |
| `planner import FILE`       | Merge validated `YYYY-MM-DD|TEXT` entries      |
| `planner export [FILE]`     | Export planner entries in the import format    |
| `planner remind`            | Show today's entries when the clock is set      |
| `habits` / `habit`     | Habit tracker with streaks                           |
| `bookmarks` / `favs`   | Path bookmarks (saves CWD)                           |
| `bookmark` / `favorites` | Bookmark aliases                                     |
| `tasks-export`         | Export tasks to CSV file                             |
| `journal-export`       | Export journal entries to Markdown file              |
| `habits-export`        | Export habits to CSV file                            |

## Apps & Games

| Command                    | Description                                       |
|----------------------------|---------------------------------------------------|
| `paint`                    | Pixel-art editor                                  |
| `sprite`                   | 16x16 sprite editor                               |
| `snake`                    | Classic snake (persistent high score)             |
| `tetris`                   | Tetris                                            |
| `life`                     | Conway's Game of Life                             |
| `mandelbrot` / `fractal`   | Mandelbrot zoom                                   |
| `piano`                    | On-screen piano (PWM audio)                       |
| `dice` / `coin` / `guess`  | Quick games                                       |
| `2048`                     | 2048 sliding-tile game                            |
| `minesweeper` / `mines`    | Minesweeper                                       |
| `games` / `game`          | Game launcher                                        |
| `stopwatch` / `timer`     | Elapsed-time utility                                 |
| `pomodoro`                | Focus timer workflow                                 |
| `samples` / `demos`        | Sample programs                                   |
| `units VALUE FROM TO`      | Convert length, weight, or temperature units      |
| `pass [N]` / `password [N]`| Generate a random password of N characters        |
| `home` / `launcher` / `dashboard` | Visual launchers                            |
| `sysmon` / `monitor` / `status` | System monitor                               |
| `term` / `terminal` / `tty` / `serial` | Full-screen VT100/ANSI serial terminal |
| `rawterm`                  | Minimal byte pass-through (no emulation)          |
| `theme`                    | Choose color theme (`noir`, `paper`, `green`, ...) |
| `hw` / `workbench`         | Hardware Workbench overview and pin map           |

## File Transfer

Both protocols stream straight to and from the SD card, so file size is
limited by the card rather than RAM. Transfers run over USB-CDC (`-p usb`) or
a hardware UART (`-p uart1` on GP4/GP5 by default, `-p uart0` for the console
pins).

| Command         | Description                                              |
|-----------------|----------------------------------------------------------|
| `xmodem send FILE [-p usb\|uart0\|uart1] [-b BAUD]` | Send over XMODEM |
| `xmodem recv FILE [-p ...] [-b BAUD]` | Receive to FILE                       |
| `ymodem send FILE [-p ...]` | Send with a name/size header                   |
| `ymodem recv [FILE] [-p ...]` | Receive; FILE is optional with YMODEM         |

CRC-16 mode is requested first and falls back to the 8-bit checksum when the
far end sends a NAK. With YMODEM the announced file name and size are used, so
the received file is not padded out to a block boundary.

## Hardware Workbench

| Command | Description |
| --- | --- |
| `hw` / `workbench` | Command overview |
| `hw pins` | GPIO ownership, free pins, and peripheral placement |
| `gpio PIN high\|low` | Drive a GPIO (claims it as an output) |
| `gpio read PIN` | Sample a pin |
| `gpio mode PIN in\|out` | Claim and configure a pin |
| `gpio pull PIN up\|down\|none` | Set the internal pull |
| `gpio free PIN` | Release a pin and stop any PWM on it |
| `gpio list` | List free pins |
| `pwm PIN FREQ_HZ DUTY` | Start a PWM output (duty 0-100) |
| `pwm PIN DUTY` | Change the duty of a running output |
| `pwm PIN off` | Stop the output |
| `i2cdetect` / `i2c` | Scan a bus (`[--bus N] [first[-last]]`) |

Pins that belong to the display, microSD, keyboard, audio, radio, or the
serial console are refused; add `--force` to override deliberately. The
`i2cdetect` grid follows the classic `i2cdetect -y` layout with `--` for
"no device" and the address for "present". I2C0 uses GP8/GP9, I2C1 uses
GP2/GP3, and the keyboard bus is restored to 10 kHz after a scan.

### Script bindings

BASIC adds the hardware statements plus a graphics mode and polyphonic sound:

```basic
GPIO 5, 1              : REM drive GP5 high
GPIO READ 5            : REM prints "GPIO 5 = 1"
PWM 5, 1000, 50        : REM 1 kHz at 50% (duty -1 stops)
I2CDETECT              : REM print the bus map
```

```basic
GR 160                 : REM enter 160x160 graphics (80/160/320; TEXT exits)
PSET X, Y [, C]        : REM one pixel            (C defaults to the pen)
LINE X1, Y1, X2, Y2 [, C]
RECT / FRECT X, Y, W, H [, C]   : REM outline / filled rectangle
CIRCLE / FCIRCLE X, Y, R [, C]  : REM outline / filled disc
COLOR INDEX, R, G, B   : REM palette entry (xterm default at GR)
PEN C                  : REM default colour for the statements above
CLS [C]                : REM clear the surface (text screen without GR)
SHOW                   : REM force a present now (normally one per line)
SOUND FREQ_HZ, MS [, VOICE]     : REM square voice 0-3
NOISE MS [, VOICE]              : REM percussion voice
NOSOUND                : REM stop all voices
```

`GR` allocates the indexed surface, presents centred on the panel, and the
interpreter presents the dirty region once per program line. `TEXT`, the end
of a program, `RUN`, or `BYE` all return to text mode and stop any voices.
See `/apps/BOUNCE.BAS` in the starter pack for a complete example.

Forth adds four words:

```forth
1 5 gpio!       \ ( value pin -- )
5 gpio@ .       \ ( pin -- value )
50 1000 5 pwm!  \ ( duty freq pin -- )
250 ms          \ ( delay in milliseconds -- )
i2cdetect
```

## Settings

| Command            | Description                                              |
|--------------------|----------------------------------------------------------|
| `settings` / `set` | Global settings app (theme, brightness, etc.)            |
