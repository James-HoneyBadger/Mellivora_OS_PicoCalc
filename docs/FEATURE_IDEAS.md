# Feature Ideas

This page tracks implemented feature proposals and remaining follow-up ideas.
It is a discussion aid, not a release commitment or an ordered roadmap. Ideas
should be evaluated against available RAM, flash, input ergonomics, SD-card
reliability, and the maintenance cost of supporting three board variants.

## Implemented

| Feature | How to use | Limitations and safeguards |
| --- | --- | --- |
| User-data backup/restore | `backup create`, `backup list`, `backup restore` | Directory snapshots are version-marked; restore only adds missing files. Backups include plaintext Wi-Fi credentials. |
| Storage diagnostics | `sdcheck`, `sdinfo` | Checks are read-only; `sdcheck` validates card initialization and sector 0, not every file or physical connection. |
| Planner data transfer | `planner export [FILE]`, `planner import FILE` | Stable `YYYY-MM-DD|TEXT` format. Imports validate, merge, skip duplicates, and never replace the planner file with unvalidated input. |
| Planner reminders | Automatic startup notice and `planner remind` | Requires a valid clock; date-only entries are shown once at startup, not scheduled by time of day. |
| Searchable command help | `help -k KEYWORD` | Searches command names and groups; detailed command pages remain available through `man`. |
| Serial hardware smoke tests | `python3 tools/hardware/smoke_test.py PORT` | Opt-in and non-destructive; checks shell output/dispatch over USB serial, not LCD or keyboard hardware. |
| Verified HTTPS | `fetch https://...`, `wget https://... FILE` | Pico 2 W only. Requires a valid software clock and a user-managed PEM CA certificate at `/CA.CRT`; TLS certificate and hostname checks are mandatory. |

## Follow-up opportunities

- Add import support for todo, habits, and bookmarks alongside their existing
  export mechanisms.
- Extend the serial smoke test with carefully controlled hardware-specific
  checks, without destructive SD operations.
- Consider archive-to-host transfer workflows; the current backup command
  snapshots to a directory on the SD card and does not transmit files.
- Measure HTTPS RAM/flash use on hardware and document CA certificate
  provisioning and renewal for users.

## Evaluation checklist

Before accepting a feature proposal, document:

- the user problem and expected workflow
- RP2040 and RP2350 memory and flash costs
- behavior when the SD card is absent or full
- power-loss and malformed-input behavior
- host-test coverage and required hardware validation
- compatibility impact on existing files and commands

For implemented features, update the relevant user guide, command reference,
technical documentation, and release notes.
