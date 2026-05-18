#!/usr/bin/env bash
# Capture .text/.data/.bss for each built target into reports/size-baseline-*.txt.
# Safe to run without a build present (it skips missing ELFs and reports them).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/reports}"
mkdir -p "$OUT_DIR"

# Pick a size(1) we can actually run.
SIZE_BIN="${SIZE_BIN:-arm-none-eabi-size}"
if ! command -v "$SIZE_BIN" >/dev/null 2>&1; then
    SIZE_BIN="size"
fi
if ! command -v "$SIZE_BIN" >/dev/null 2>&1; then
    echo "size_baseline: no size(1) binary available" >&2
    exit 1
fi

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

declare -A ELFS=(
    [pico]="$ROOT_DIR/picocalc/build/mellivora_picocalc.elf"
    [pico2]="$ROOT_DIR/picocalc/build-pico2/mellivora_picocalc.elf"
    [pico2w]="$ROOT_DIR/picocalc/build-pico2w/mellivora_picocalc.elf"
)

SUMMARY="$OUT_DIR/size-baseline-latest.md"
{
    echo "# Firmware Size Baseline"
    echo
    echo "Generated: $TIMESTAMP"
    echo "Tool: $SIZE_BIN"
    echo
    echo "| Target | text | data | bss | dec | hex |"
    echo "| --- | ---: | ---: | ---: | ---: | --- |"
} > "$SUMMARY"

for target in pico pico2 pico2w; do
    elf="${ELFS[$target]}"
    out="$OUT_DIR/size-baseline-$target.txt"
    if [[ ! -f "$elf" ]]; then
        echo "size_baseline: skipping $target (no ELF at $elf)" >&2
        echo "| $target | _missing_ | | | | |" >> "$SUMMARY"
        continue
    fi
    "$SIZE_BIN" "$elf" > "$out"
    # Parse Berkeley-format size output (header + one data row).
    row="$(awk 'NR==2 {print $1, $2, $3, $4, $5}' "$out")"
    read -r text data bss dec hex <<<"$row"
    echo "| $target | $text | $data | $bss | $dec | $hex |" >> "$SUMMARY"
done

echo "Wrote $SUMMARY"
