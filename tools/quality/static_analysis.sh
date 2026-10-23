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
