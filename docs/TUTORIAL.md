# Mellivora PicoCalc Tutorial

This tutorial walks through a realistic first session on Mellivora PicoCalc. By the end, you will have mounted storage, created files, used the built-in apps, and tried the programming tools.

## 1. Boot the Device

After flashing the firmware, boot the device normally. You should see the shell prompt.

Start by checking the basics:

```text
help
uname
dashboard
```

This confirms the system is running and shows the available command families.

## 2. Mount the SD Card

Insert your SD card and run:

```text
mount
ls
pwd
```

If the mount succeeds, you are ready to use the filesystem-backed apps.

## 3. Create a Working Folder

Make a small workspace for your notes and experiments:

```text
mkdir /WORK
cd /WORK
pwd
```

Now create a first text file:

```text
touch HELLO.TXT
write HELLO.TXT Welcome to Mellivora PicoCalc
cat HELLO.TXT
```

## 4. Open the File Browser

Launch the interactive browser:

```text
browse /WORK
```

Use the keyboard controls shown in the browser to move around and inspect files.

## 5. Write a Real Note

Open the built-in notes workflow:

```text
notes
```

If you want to edit a specific file directly, use:

```text
edit /WORK/IDEAS.TXT
```

Add a few lines, save, and return to the shell.

## 6. Start Using the Personal Apps

### Todo

```text
todo
```

Use this to manage simple tasks.

### Planner

```text
planner
```

Add a dated event or test reminder.

### Journal

```text
journal
```

Create a quick entry to confirm the system can save personal logs.

### Habits

```text
habits
```

Create one habit and mark a completion.

### Bookmarks

```text
bookmarks
```

Save a favorite path so you can jump back later.

## 7. Explore the Utility Layer

Try a few practical commands:

```text
tree /WORK
du /WORK
df
calc 12*12
```

These show storage structure, disk usage, capacity, and simple calculations.

## 8. Use the Launcher

Open the launcher:

```text
home
```

From here you can jump quickly into notes, browse, calculator, games, and other built-in tools.

## 9. Try the Creative Tools

### Paint

```text
paint
```

Draw on the screen and exit when finished.

### Sprite editor

```text
sprite
```

Toggle pixels and save a small pattern.

## 10. Try the Games

Start with the game menu:

```text
games
```

You can also launch a game directly:

```text
snake
dice 2 6
coin 3
guess
```

## 11. Write a BASIC Program

Open BASIC:

```text
basic
```

Enter:

```text
10 PRINT "HELLO"
20 FOR A = 1 TO 5
30 PRINT A
40 NEXT A
RUN
```

This confirms the built-in BASIC environment is working.

## 12. Try Tiny C

Back at the shell, run:

```text
tcc
```

Then enter:

```text
int n = 4;
print(n);
n = n + 3;
vars
```

This shows the Tiny C environment handling short integer-oriented code.

## 13. Save a Tiny Script File

Create a script you can rerun later:

```text
edit /WORK/START.SH
```

Add commands such as:

```text
pwd
ls
calc 7*9
```

Then run it with:

```text
script /WORK/START.SH
```

## 14. Set a Preference

Try a persistent setting:

```text
settings
set backlight 180
```

This makes the device feel more personal and shows how configuration is preserved.

## 15. Next Steps

Once you are comfortable, the best next moves are:

- keep your notes and todo list on the SD card
- use planner and journal regularly
- build tiny BASIC or Tiny C experiments
- explore the file browser, hex editor, and samples library
- customize your startup workflow with settings
