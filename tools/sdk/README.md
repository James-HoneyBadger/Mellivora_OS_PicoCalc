# Mellivora application SDK

Build the included sample in seconds with an ARM GNU toolchain and Python 3:

```sh
make -C tools/sdk
```

`make` builds both samples (`hello` and `plasma`); `make APP=plasma` builds
just one. Either `.mapp` can be copied to `/apps/` on the FAT32 microSD card
and appears in the launcher. Build your own standalone C app by placing
`name.c` here and running `make APP=name`.

Include `mapp.h`, define `mapp_main(const mapp_api_t *, int32_t, char **)`, and
use only the API table for firmware services. The target is freestanding and
has no libc; compile for Cortex-M0+ Thumb. The packer applies the ELF
`R_ARM_ABS32` relocations and rejects unsupported allocated sections, so code,
read-only data, initialized data, and BSS can be moved together into the
firmware's 16 KiB SRAM arena. Do not retain pointers after returning.

The ABI is append-only. Accept a loader at or above your build's minimum and
test for the calls you use:

```c
if (!api || api->version < MAPP_API_VERSION_MIN) return -1;
if (api->size < sizeof(mapp_api_t)) return -1;
if (!api->capabilities || !(api->capabilities() & MAPP_CAP_GFX)) return -1;
```

**v1** exposes display control/output, blocking and nonblocking keyboard input,
line input, FAT-backed file read/write/create/delete operations, millisecond
timers, and network availability/DNS/HTTP GET where the board supports Wi-Fi.

**v2** adds capabilities, `yield()`, an indexed-colour graphics surface API
(`gfx_create`/`destroy`, `pixel`/`hline`/`vline`/`rect`/`fill_rect`/`char`,
`blit` with a transparent index, `palette`, `invalidate`, `present`), and
non-blocking `audio_tone`/`audio_stop`. See `plasma.c` for the whole loop.

## Graphics surfaces

Surfaces are 8-bit indexed, one palette per app (256 entries, xterm-style
default). Presenting only sends the dirty rectangle, because a full 320x320
frame is ~307 KiB of SPI traffic:

| Surface | Bytes | Fits |
| --- | ---: | --- |
| 320 x 320 | 102,400 | RP2350 only |
| 160 x 160 | 25,600 | both boards |
| 80 x 80 | 6,400 | both boards |

`gfx_present(surface, x, y)` copies the changed region to the panel and resets
the dirty box; `gfx_invalidate(surface)` forces a full resend, which is what a
palette-only animation needs. Surfaces are freed automatically when
`mapp_main` returns.

To package a bytecode instruction stream, use
`python3 tools/sdk/mapp_pack.py --bytecode --name "Title" input.bin output.mapp`.

`.mapp` is an executable format, not a sandbox: native code has the same
privileges as firmware and can corrupt the device or card. Only run apps from
sources you trust. Apps must return from `mapp_main`; there is no process
isolation or forced-stop mechanism.

## Bytecode

The loader also accepts kind `2` images (no relocations or BSS). Bytecode is a
stream starting at `entry_offset`; operands are little-endian:

| Opcode | Encoding | Operation |
|---|---|---|
| `00` | — | Halt |
| `01` | `length:u8`, bytes | Print bytes |
| `02` | `character:u8` | Print one character |
| `03` | — | Read key and push its value |
| `04` | — | Push milliseconds since boot |
| `05` | — | Pop and sleep that many milliseconds |
| `06` | `value:i32` | Push signed integer |
| `07` / `08` / `09` | — | Pop two values and push add/subtract/multiply |
| `0a` | — | Pop and print signed integer |
| `0b` | `offset:i16` | Relative jump from the next instruction |
| `0c` | `offset:i16` | Pop; jump if zero |
| `0d` | — | Clear display |
| `0e` / `0f` | — | Duplicate top / drop top |

The bytecode stack has 32 signed 32-bit slots. A malformed instruction or
stack underflow stops the module and reports it through the launcher.

## Script apps

Files in `/apps/` with `.bas`, `.c`, or `.fth` extensions are listed alongside
`.mapp` programs. `.bas` uses Mellivora BASIC, `.c` uses the built-in Tiny C
subset (not a general C compiler), and `.fth` uses the built-in Forth subset.
An optional first-line/early comment sets a menu title:

```text
REM @app-name: My BASIC app
// @app-name: My Tiny C app
\ @app-name: My Forth app
```
