# Command Reference

This is the canonical reference for all built-in commands and apps.
For the user-friendly tour, see [USER_GUIDE.md](USER_GUIDE.md).

## Conventions

- `arg` — required argument
- `[arg]` — optional argument
- Multiple commands can be chained on one line with `;`
- Output can be redirected with `> file` (truncate) or `>> file` (append)
- Aliases are expanded before dispatch (`alias ll="ls -l"`)
- `Ctrl-C` cancels long-running commands
- `Ctrl-R` searches command history

## System

| Command                    | Description                                            |
|----------------------------|--------------------------------------------------------|
| `version`                  | Print firmware version, build date, project URL        |
| `uname [-a]`               | OS name; with `-a` adds hardware/build details         |
| `sysinfo`                  | Detailed system info (memory, clock, peripherals)      |
| `dmesg`                    | Kernel-style boot log                                  |
| `reboot`                   | Soft reset via watchdog                                |
| `shutdown` / `halt` / `poweroff` | Flush state, blank display, halt CPU             |
| `sleep`                    | Display-off low-power mode (any key wakes)             |
| `sleep MS`                 | Pause for MS milliseconds (script use)                 |
| `clear`                    | Clear screen                                           |
| `id`                       | Always prints `mellivora`                              |
| `true` / `false`           | Exit status helpers (for scripts)                      |
| `lock`                     | Lock the device with a PIN                             |

## File System

| Command                      | Description                                          |
|------------------------------|------------------------------------------------------|
| `mount` / `umount`           | Mount/unmount the SD card                            |
| `ls [-l] [path]`             | List directory                                       |
| `cd [path]`                  | Change working directory (persistent)                |
| `pwd`                        | Print working directory                              |
| `mkdir <path>`               | Create directory                                     |
| `rm [-rRf] <path>`           | Remove file (or recursively for directories)         |
| `cp [-r] <src> <dst>`        | Copy file or directory                               |
| `mv <src> <dst>` / `rename`  | Rename / move                                        |
| `cat <file>`                 | Display file contents                                |
| `more <file>` / `less`       | Paged display                                        |
| `head [-n N] <file>`         | First N (or 10) lines                                |
| `tail [-n N] <file>`         | Last N (or 10) lines                                 |
| `wc [-lwc] <file>`           | Line / word / byte counts                            |
| `cut -dCHR -fN <file>`       | Extract a column                                     |
| `sort <file>`                | Sort lines                                           |
| `uniq <file>`                | Collapse adjacent duplicate lines                    |
| `grep [-i] PATTERN <file>`   | Regex search                                         |
| `find <path> -name PATTERN`  | Find files                                           |
| `du <path>`                  | Disk usage                                           |
| `df`                         | Free space                                           |
| `stat <file>`                | File metadata                                        |
| `hexdump` / `xxd` / `od`     | Binary dump                                          |
| `hexedit` / `hex`            | Interactive hex editor                               |
| `strings <file>`             | Printable ASCII runs                                 |
| `diff <a> <b>`               | Line-based diff                                      |
| `tee <file>`                 | Tee stdin to file (and stdout)                       |
| `write <file>`               | Read stdin until `.` line, write to file             |
| `edit <file>` / `bedit`      | Line editor / block editor                           |
| `browse` / `files`           | Visual file browser                                  |

## Shell

| Command                 | Description                                              |
|-------------------------|----------------------------------------------------------|
| `history`               | Show command history                                     |
| `alias [name=value]`    | List or define aliases                                   |
| `unalias <name>`        | Remove alias                                             |
| `env`                   | Show environment / variables                             |
| `set X=Y`               | Set a shell variable                                     |
| `script <file>`         | Run a script of shell commands                           |
| `watch CMD`             | Re-run CMD every 2s until any key                        |
| `yes [STRING]`          | Repeatedly print STRING                                  |
| `seq N` / `seq A B`     | Numeric sequence                                         |
| `hello`                 | Greeting (sample command)                                |
| `help`                  | Show categorized help                                    |

## Time & Calendar

| Command           | Description                                              |
|-------------------|----------------------------------------------------------|
| `date`            | Show current date/time                                   |
| `date YYYY-MM-DD HH:MM:SS` | Set the software RTC manually                   |
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
| `fetch URL`                      | HTTP GET (display body)                      |
| `wget URL`                       | HTTP GET to file                             |
| `weather [LOCATION]`             | Fetch weather forecast                       |
| `irc HOST PORT NICK CHAN`        | Minimal IRC client                           |
| `telnet HOST PORT`               | Telnet client                                |
| `netstat`                        | Show open sockets                            |

## Personal Productivity

| Command                | Description                                          |
|------------------------|------------------------------------------------------|
| `notes` / `memo`       | Note pad (persistent)                                |
| `journal` / `diary`    | Dated journal                                        |
| `todo` / `tasks`       | Todo list                                            |
| `planner` / `agenda` / `plan` | Daily / weekly planner                        |
| `habits` / `habit`     | Habit tracker with streaks                           |
| `bookmarks` / `favs`   | Path bookmarks (saves CWD)                           |

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
| `samples` / `demos`        | Sample programs                                   |
| `home` / `launcher` / `dashboard` | Visual launchers                            |
| `sysmon` / `monitor` / `status` | System monitor                               |
| `terminal` / `term` / `tty` / `serial` | UART pass-through                      |
| `theme`                    | Choose color theme                                |

## File Transfer

| Command         | Description                                              |
|-----------------|----------------------------------------------------------|
| `xmodem send <file>` | Send file via XMODEM                                |
| `xmodem recv <file>` | Receive file via XMODEM                             |

## Settings

| Command            | Description                                              |
|--------------------|----------------------------------------------------------|
| `settings` / `set` | Global settings app (theme, brightness, etc.)            |
