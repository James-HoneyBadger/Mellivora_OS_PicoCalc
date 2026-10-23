#!/usr/bin/env bash
# Check formatting for the newly pure/testable modules and their tests.
# Existing firmware files are intentionally left alone for now.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if ! command -v clang-format >/dev/null 2>&1; then
    if [[ "${CI:-}" == "true" ]]; then
        echo "format_check: clang-format is required in CI" >&2
        exit 1
    fi
    echo "format_check: clang-format not installed; skipping (install it to enable)"
    exit 0
fi

FILES=(
    picocalc/src/core/*.c
    picocalc/src/core/*.h
    picocalc/src/console.c
    picocalc/src/console.h
    picocalc/src/audio.c
    picocalc/src/audio.h
    picocalc/tests/*.c
    picocalc/tests/*.h
)

clang-format --dry-run --Werror --style=file "${FILES[@]}"
echo "format_check: all checked files are formatted"
