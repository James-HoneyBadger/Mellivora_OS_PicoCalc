#!/usr/bin/env bash
# Run clang-tidy over the hardware-independent core modules and shared helpers.
# The firmware itself is also built with -Werror for every Mellivora source.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if ! command -v clang-tidy >/dev/null 2>&1; then
    if [[ "${CI:-}" == "true" ]]; then
        echo "static_analysis: clang-tidy is required in CI" >&2
        exit 1
    fi
    echo "static_analysis: clang-tidy not installed; skipping (install it to enable)"
    exit 0
fi

FILES=(
    picocalc/src/core/*.c
    picocalc/src/apps/app_shared.c
)

clang-tidy --quiet --config-file="$ROOT_DIR/.clang-tidy" "${FILES[@]}" \
    -- -std=c11 -Ipicocalc/src -Ipicocalc/src/core

# Firmware modules need the Pico SDK include paths; reuse the compile
# database produced by a normal build when it exists.
DB="$ROOT_DIR/picocalc/build/compile_commands.json"
if [[ -f "$DB" ]]; then
    FIRMWARE_FILES=(
        picocalc/src/main.c
        picocalc/src/fat.c
        picocalc/src/pipe.c
        picocalc/src/ui/ui.c
        picocalc/src/apps/text.c
        picocalc/src/apps/hex.c
        picocalc/src/apps/fs.c
        picocalc/src/apps/app_*.c
    )
    clang-tidy -p "$ROOT_DIR/picocalc/build" --quiet \
        --config-file="$ROOT_DIR/.clang-tidy" "${FIRMWARE_FILES[@]}"
else
    echo "static_analysis: no firmware compile database; run 'make picocalc' for the wider pass"
fi
