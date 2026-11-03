# Security Policy

## Supported Versions

The current `main` branch and latest published release are supported with
security fixes. Older releases may not receive backported fixes.

## Scope

Mellivora PicoCalc is embedded firmware for RP2040- and RP2350-based
PicoCalc hardware. The main security areas are:

- memory safety in the shell and filesystem code
- SD card data integrity and file operation validation
- build and flashing workflow safety
- input handling on serial or device-attached interfaces
- handling of data stored on removable SD cards

Wi-Fi credentials saved by Pico 2 W firmware are stored as readable text
in `/WIFI.CFG` on the SD card. This firmware does not encrypt that file.
Treat the card as sensitive if it contains saved network credentials or
personal data.

The `backup` command includes `/WIFI.CFG`; backups therefore contain the
same sensitive data and should be stored and transferred securely. HTTPS
requests require a user-provided CA certificate at `/CA.CRT`, a valid device
clock, and certificate/hostname verification. The firmware does not provide
a certificate-validation bypass.

## Reporting a Vulnerability

Please use
[GitHub private vulnerability reporting](https://github.com/James-HoneyBadger/Mellivora_OS_PicoCalc/security/advisories/new)
instead of opening a public issue. Include:

- a clear description of the issue
- steps to reproduce it
- expected impact
- any suggested mitigation if available
