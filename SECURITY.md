# Security Policy

## Supported Versions

Only the current `main` branch is supported with security fixes.

## Scope

Mellivora PicoCalc is embedded firmware for RP2040-based hardware. The main security areas are:

- memory safety in the shell and filesystem code
- SD card data integrity and file operation validation
- build and flashing workflow safety
- input handling on serial or device-attached interfaces

## Reporting a Vulnerability

Please use GitHub private vulnerability reporting and include:

- a clear description of the issue
- steps to reproduce it
- expected impact
- any suggested mitigation if available
