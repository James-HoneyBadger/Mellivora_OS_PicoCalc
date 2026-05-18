#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_FILE="${1:-$ROOT_DIR/reports/baseline-latest.md}"

mkdir -p "$(dirname "$OUT_FILE")"
cd "$ROOT_DIR"

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

C_FILES_COUNT="$(find picocalc/src -type f \( -name "*.c" -o -name "*.h" \) | wc -l | tr -d ' ')"
C_LINES_TOTAL="$(find picocalc/src -type f -name "*.c" -print0 | xargs -0 wc -l | tail -n 1 | awk '{print $1}')"
MD_LINES_TOTAL="$(find docs -maxdepth 1 -type f -name "*.md" -print0 | xargs -0 wc -l | tail -n 1 | awk '{print $1}')"
COMMAND_REF_ENTRIES="$(grep -E '^\| `[^`]+`' docs/COMMANDS.md | wc -l | tr -d ' ')"

cat > "$OUT_FILE" <<EOF
# Baseline Report

Generated: $TIMESTAMP

## Static Metrics

- C source/header files in picocalc/src: $C_FILES_COUNT
- Total C lines (picocalc/src/*.c): $C_LINES_TOTAL
- Total markdown lines (docs/*.md): $MD_LINES_TOTAL
- Command reference entries (table rows): $COMMAND_REF_ENTRIES

## Dynamic Hardware Metrics (manual capture placeholder)

Record the following from hardware test sessions:

- boot time to prompt
- prompt echo latency
- SD mount success rate
- large directory listing latency
- LCD redraw throughput under scroll
- Pico 2W network command latency (if applicable)

## Notes

This report is intended as a regression baseline artifact.
EOF

echo "Wrote baseline report: $OUT_FILE"
