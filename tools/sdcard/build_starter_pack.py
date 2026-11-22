#!/usr/bin/env python3
"""Build sd_starter_pack.zip — a ready-to-copy FAT32 card layout.

Layout produced inside the archive:

    README.TXT          quick-start notes generated at build time
    apps/               BASIC, Tiny C, Forth samples plus room for .mapp files
    themes/             NAME.thm palettes for the ``theme`` command
    bin/                shell scripts
    docs/               offline copies of the user documentation

The archive is deterministic (fixed timestamps and ordering) so release
artifacts are reproducible.
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
STARTER = pathlib.Path(__file__).resolve().parent / "starter"

# Documentation copied verbatim into docs/ on the card: (source, arcname).
DOC_FILES = [
    ("README.md", "docs/README.md"),
    ("docs/USER_GUIDE.md", "docs/USER_GUIDE.md"),
    ("docs/COMMANDS.md", "docs/COMMANDS.md"),
    ("docs/QUICK_REFERENCE.md", "docs/QUICK_REFERENCE.md"),
    ("docs/HARDWARE.md", "docs/HARDWARE.md"),
    ("docs/TUTORIAL.md", "docs/TUTORIAL.md"),
    ("docs/PROGRAMMING_GUIDE.md", "docs/PROGRAMMING_GUIDE.md"),
]

FIXED_DATE = (2024, 1, 1, 0, 0, 0)


def project_version() -> str:
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    except OSError:
        return "0.0.0"


def readme_text(version: str) -> str:
    return f"""Mellivora OS {version} - SD card starter pack
=================================================

Copy the contents of this archive to the root of a FAT16 or FAT32
microSD card, insert it into the PicoCalc, then run:

    mount
    ls /apps

What is here
------------

apps/    Sample programs. BASIC (.bas), Tiny C (.c), and Forth (.fth)
         files appear in the launcher automatically. Drop .mapp modules
         in the same folder to have them listed too.

themes/  Palette files. Apply one with:   theme ocean
         Built-in palettes are always available; these are extras.

bin/     Shell scripts. Run one with:     script /bin/hello.sh

docs/    Offline copies of the user and hardware documentation.

First things to try
-------------------

    help                 list every command
    home                 open the launcher
    basic /apps/HELLO.BAS
    tcc /apps/PRIMES.C
    forth /apps/SQUARES.FTH
    theme ocean
    sysinfo              live core, heap, SD, and battery view

Hardware workbench
------------------

    hw pins              GPIO ownership, free pins, peripheral placement
    term                 full-screen VT100 terminal on UART1 (GP4/GP5)
    i2cdetect            scan I2C0 (SDA GP8, SCL GP9)
    gpio 5 high          drive a spare pin
    pwm 5 1000 50        1 kHz square wave on GP5
    xmodem recv FILE     receive over USB (-p usb) or UART (-p uart1)

From BASIC:  GPIO 5, 1     PWM 5, 1000, 50     GPIO READ 5
              GR 160         PSET/LINE/CIRCLE   SOUND 440, 100
See /apps/PWMFADE.BAS and /apps/BOUNCE.BAS for complete examples.
From Forth:  1 5 gpio!     50 1000 5 pwm!      5 gpio@ .
See /apps/PWMFADE.BAS, /apps/BOUNCE.BAS, and /apps/I2CSCAN.FTH for complete examples.

Writing your own app
--------------------

BASIC and Tiny C files only need a title comment on the first line:

    REM @app-name: My BASIC app
    // @app-name: My Tiny C app
    \\ @app-name: My Forth app

Native .mapp modules are built with the host SDK in tools/sdk/ of the
source tree; see docs/PROGRAMMING_GUIDE.md.

Packages (Pico 2 W only)
------------------------

The Wi-Fi build includes mpkg:

    mpkg repo https://example.org/index.json
    mpkg update
    mpkg list
    mpkg install hello

Packages are SHA-256 verified before they are written to the card.

Safety
------

.mapp modules run with full firmware privileges; there is no sandbox.
Only install packages and apps from sources you trust.
"""


def add_entry(zf: zipfile.ZipFile, arcname: str, data: bytes) -> None:
    info = zipfile.ZipInfo(arcname, date_time=FIXED_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def collect_starter_files() -> list[tuple[str, pathlib.Path]]:
    if not STARTER.is_dir():
        raise SystemExit(f"missing starter directory: {STARTER}")
    files: list[tuple[str, pathlib.Path]] = []
    for path in sorted(STARTER.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(STARTER).as_posix()
        files.append((rel, path))
    return files


def build(out: pathlib.Path, version: str) -> int:
    files = collect_starter_files()
    docs: list[tuple[str, pathlib.Path]] = []
    for source, arcname in DOC_FILES:
        path = ROOT / source
        if path.is_file():
            docs.append((arcname, path))
        else:
            print(f"warning: {source} not found, skipped", file=sys.stderr)

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        add_entry(zf, "README.TXT", readme_text(version).encode("utf-8"))
        for arcname, path in files:
            add_entry(zf, arcname, path.read_bytes())
        for arcname, path in docs:
            add_entry(zf, arcname, path.read_bytes())

    size = out.stat().st_size
    print(f"Wrote {out} ({size} bytes, {len(files) + len(docs) + 1} files)")
    for arcname, _ in files:
        print(f"  {arcname}")
    for arcname, _ in docs:
        print(f"  {arcname}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=ROOT / "build" / "sd_starter_pack.zip",
        help="output archive (default: build/sd_starter_pack.zip)",
    )
    parser.add_argument(
        "--version",
        default=project_version(),
        help="version banner written into README.TXT",
    )
    args = parser.parse_args(argv)

    stamp = dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    print(f"Building SD starter pack for Mellivora {args.version} at {stamp}")
    return build(args.out, args.version)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
