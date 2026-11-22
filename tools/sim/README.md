# Mellivora host simulator

Run a `tools/sdk` app on the desktop against the *same* ABI, the same
`core/gfx.c` surface engine, and the same `core/mixer.c` the firmware
links. No SDL, no window, no hardware:

```sh
make -C tools/sim APP=plasma          # build, run 4 s, screenshot
make -C tools/sim APP=hello           # text app: output on stdout
make APP=plasma run=0                 # just build
make APP=plasma MAPP_AUTOQUIT_MS=800  # shorter virtual-key timeout
```

The app source is unchanged: `runtime.c` implements `mapp_api_t` v3
headlessly. Graphics surfaces render through the real indexed gfx core;
when `mapp_main` returns, the last presented frame is written as
`shots/NAME.png`. Files are sandboxed under `build/simroot/`, and the
network reports offline (no `MAPP_CAP_NETWORK`).

The simulator is what CI uses to screenshot every example app for the
gallery, and it doubles as the fastest edit-and-see loop:

```sh
make -C tools/sim APP=mygame run=0   # build
./tools/sim/build/mygame             # run + screenshot
```

Limitations: `gfx_char` uses a compact 5x7 font rather than the panel's
8x16 bitmap, audio is mixed but discarded, and keyboard input is a single
virtual key after `MAPP_AUTOQUIT_MS`. Timing is real, not simulated.
