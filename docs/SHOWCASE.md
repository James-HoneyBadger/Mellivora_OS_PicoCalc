# Mellivora Showcase and Studio

Development plan, not a release claim. Keep the RP2040 baseline usable; offer
larger features on RP2350 only after measuring stack, heap and panel throughput.

## First source examples

- Orbit Art (ORBIT.BAS): geometry, palette, timing.
- Chime Composer (CHIMES.BAS): editable original arpeggio.
- Binary Lab (BITS.BAS): integer powers of two.
- Life Lab (tools/sdk/lifelab.c): native interactive cellular automata.

The BASIC examples are included automatically by the starter-pack builder.
Life Lab requires host SDK compilation and copying the resulting module to /apps.
These additions are not yet hardware-tested.

## Flagship applications

| Application | First useful milestone |
| --- | --- |
| Studio IDE | Open/edit/save/run BASIC, Forth and Tiny C; return to source on exit |
| Scientific calculator | Plot expressions with bounded sampling; table and programmer view |
| Music tracker | Edit and play patterns on existing four-voice mixer |
| Sprite workshop | Edit palette and frames; save/export assets |
| Signal workbench | Plot supported sensor readings with explicit sampling limits |
| Pocket organizer | Notes, tasks, planner, search and reliable backup |
| Arcade | Original games demonstrating input, timing, sprites and audio |

Studio must support offline editing and execution on the device itself.
Use the existing editor and interpreter APIs, add explicit run-result and
source-location contracts before implementing breakpoints or variable inspection.
Do not imply full native C compilation is available on-device.

## Acceptance gates

Host tests, all three firmware builds, size gates, simulator runs for native
samples, SD pack validation, physical keyboard/display/audio testing and
power-loss/storage recovery checks before promoting to main. Native modules
are privileged, not sandboxed. No telemetry, accounts or required cloud services.
