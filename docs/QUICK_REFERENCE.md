# Mellivora PicoCalc Quick Reference

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `Up` / `Down` | Browse command history |
| `Tab` | Auto-complete command name |
| `Ctrl-C` | Cancel current line / exit tools |
| `Ctrl-U` | Clear current line |
| `Ctrl-L` | Redraw screen |
| `!!` | Re-run last command |
| `!N` | Re-run history entry N |

## Shell Features

| Feature | Syntax | Example |
|---|---|---|
| Chain commands | `cmd1 ; cmd2` | `mount ; ls` |
| Redirect output | `cmd > FILE` | `ls > DIR.TXT` |
| Append output | `cmd >> FILE` | `echo hi >> LOG.TXT` |
| Pipe to pager | `cmd \| more` | `cat FILE \| more` |
| Quoted args | `"text with spaces"` | `echo "hello world"` |
| Aliases | `alias NAME VALUE` | `alias ll ls` |

## System Commands

| Command | Description |
|---|---|
| `help` | Command overview |
| `man TOPIC` | Detailed help |
| `history` | Command history |
| `alias` / `unalias` | Manage aliases |
| `clear` | Clear screen |
| `reboot` | Restart device |
| `uname` | Platform info |
| `uptime` | Time since boot |
| `battery` | Battery level |
| `backlight N` | Keyboard backlight (0-255) |
| `mount` | Mount SD card |
| `sdinfo` | SD card status |

## File Commands

| Command | Syntax |
|---|---|
| `ls` / `dir` | `ls [PATH]` |
| `cd` | `cd [PATH]` |
| `pwd` | Print working directory |
| `cat` | `cat FILE` |
| `touch` | `touch FILE` |
| `write` | `write FILE TEXT` |
| `mkdir` | `mkdir DIR` |
| `rm` | `rm PATH` |
| `cp` | `cp SRC DST` (dirs supported) |
| `mv` | `mv OLD NEW` |
| `rename` | `rename OLD NEW` |
| `stat` | `stat PATH` |
| `tree` | `tree [-L DEPTH] [PATH]` |
| `du` | `du [PATH]` |
| `df` | Disk usage summary |

## Text Processing

| Command | Syntax |
|---|---|
| `grep` | `grep [-n] [-e] PATTERN FILE` |
| `sort` | `sort [-r] [-n] FILE` |
| `find` | `find [PATH] [-name PAT] [-type f\|d]` |
| `head` | `head [-n NUM] FILE` |
| `tail` | `tail [-n NUM] FILE` |
| `cut` | `cut [-d CHAR] [-f NUM] FILE` |
| `wc` | `wc FILE` |
| `rev` | `rev FILE` |
| `seq` | `seq N` |
| `od` | `od FILE` |
| `hexdump` | `hexdump FILE` |

## Interactive Tools

| Command | Description |
|---|---|
| `browse` | File browser (j/k, Enter, d, n, m, u, q) |
| `edit FILE` | Line editor |
| `hexedit FILE` | Hex editor (find subcommand) |
| `pager FILE` | File pager (Space/Enter/q) |
| `notes` | Open default notes file |
| `paint` | Pixel drawing (arrows, space, c, s, l, q) |
| `sprite` | Sprite editor |
| `terminal` | Raw terminal mode (Ctrl-X to exit) |

## Productivity Apps

| Command | Key Subcommands |
|---|---|
| `todo` | `list`, `add TEXT`, `done N`, `undo N`, `del N`, `purge`, `edit` |
| `planner` | `list`, `add DATE TEXT`, `today`, `month YYYY-MM`, `next`, `del N`, `edit` |
| `journal` | `list`, `add [DATE] TEXT`, `today`, `month YYYY-MM`, `edit` |
| `habits` | `list`, `add NAME`, `done N\|NAME [DATE]`, `reset N\|NAME`, `edit` |
| `bookmarks` | `list`, `add NAME TARGET`, `open N\|NAME`, `del N\|NAME`, `edit` |
| `settings` | `show`, `set KEY VALUE`, `save`, `load`, `reset` |

## Games

| Command | Description |
|---|---|
| `snake` | Step-based snake (wasd/hjkl, persistent high score) |
| `dice [N] [S]` | Roll N dice with S sides |
| `coin [N]` | Flip N coins |
| `guess` | Number guessing game (1-100) |
| `games` | Game selection menu |

## Languages

| Command | Description |
|---|---|
| `basic [FILE.BAS]` | BASIC interpreter |
| `tcc [FILE.TC]` | Tiny C interpreter |
| `calc [EXPR]` | Calculator / expression REPL |
| `script FILE` | Run shell script file |
| `samples` | Bundled example programs |

## Launchers

| Command | Description |
|---|---|
| `home` | App launcher menu |
| `dashboard` | System status + shortcuts |
| `sysmon` | Detailed system monitor |
