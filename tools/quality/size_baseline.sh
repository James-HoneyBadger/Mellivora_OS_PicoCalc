#!/usr/bin/env bash
# Capture .text/.data/.bss for each built target into reports/size-current-*.txt.
# Safe to run without a build present (it skips missing ELFs and reports them).
#
# The per-target files are a snapshot of the current build, not the regression
# reference: size_gate.sh compares against reports/size-baseline-*.txt, which
# only `make size-baseline` (pass --update-baseline) writes. Keeping the two
# apart means a routine `make quality-check` cannot silently accept a
# regression by rewriting the baseline it is measured against.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/reports}"
MODE="${2:-}"
UPDATE_BASELINE=0
if [[ "$MODE" == "--update-baseline" ]]; then
    UPDATE_BASELINE=1
elif [[ -n "$MODE" ]]; then
    echo "size_baseline: unknown option '$MODE' (expected --update-baseline)" >&2
    exit 1
fi
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
    if ((UPDATE_BASELINE)); then
        echo "Mode: committed baselines refreshed (make size-baseline)"
    else
        echo "Mode: current build only; committed baselines unchanged"
    fi
    echo
    echo "| Target | text | data | bss | dec | hex |"
    echo "| --- | ---: | ---: | ---: | ---: | --- |"
} > "$SUMMARY"

for target in pico pico2 pico2w; do
    elf="${ELFS[$target]}"
    out="$OUT_DIR/size-current-$target.txt"
    if [[ ! -f "$elf" ]]; then
        echo "size_baseline: skipping $target (no ELF at $elf)" >&2
        echo "| $target | _missing_ | | | | |" >> "$SUMMARY"
        continue
    fi
    "$SIZE_BIN" "$elf" > "$out"
    if ((UPDATE_BASELINE)); then
        cp "$out" "$OUT_DIR/size-baseline-$target.txt"
    fi
    # Parse Berkeley-format size output (header + one data row).
    row="$(awk 'NR==2 {print $1, $2, $3, $4, $5}' "$out")"
    read -r text data bss dec hex <<<"$row"
    echo "| $target | $text | $data | $bss | $dec | $hex |" >> "$SUMMARY"
done

echo "Wrote $SUMMARY"
if ((UPDATE_BASELINE)); then
    echo "Refreshed reports/size-baseline-*.txt"
fi
