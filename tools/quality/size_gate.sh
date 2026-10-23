#!/usr/bin/env bash
# Enforce flash/RAM budgets and guard against growth beyond the committed
# size baselines. Run after building the firmware targets.
#
# Thresholds:
#   MAX_TEXT_GROWTH_PCT  allowed .text+data growth (default 5%)
#   MAX_BSS_GROWTH_PCT   allowed .bss growth (default 5%)
#   ABS_SLACK            absolute byte slack (default 2048)
#
# Usage: size_gate.sh [pico] [pico2] [pico2w]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

MAX_TEXT_GROWTH_PCT="${MAX_TEXT_GROWTH_PCT:-5}"
MAX_BSS_GROWTH_PCT="${MAX_BSS_GROWTH_PCT:-5}"
ABS_SLACK="${ABS_SLACK:-2048}"

SIZE_BIN="${SIZE_BIN:-arm-none-eabi-size}"
if ! command -v "$SIZE_BIN" >/dev/null 2>&1; then
    SIZE_BIN="size"
fi
if ! command -v "$SIZE_BIN" >/dev/null 2>&1; then
    echo "size_gate: no size(1) binary available" >&2
    exit 1
fi

declare -A ELFS=(
    [pico]="picocalc/build/mellivora_picocalc.elf"
    [pico2]="picocalc/build-pico2/mellivora_picocalc.elf"
    [pico2w]="picocalc/build-pico2w/mellivora_picocalc.elf"
)
# Usable budgets: flash size, and RAM minus a 32 KiB stack reserve.
declare -A FLASH_BUDGET=( [pico]=2097152 [pico2]=4194304 [pico2w]=4194304 )
declare -A RAM_BUDGET=( [pico]=237568 [pico2]=499712 [pico2w]=499712 )

if [[ $# -gt 0 ]]; then
    TARGETS=("$@")
else
    TARGETS=(pico pico2 pico2w)
fi

fail=0
printf '%-8s %10s %10s %10s %10s %-24s\n' "target" "flash" "flash max" "ram" "ram max" "status"
printf '%-8s %10s %10s %10s %10s %-24s\n' "------" "-----" "---------" "---" "-------" "------"

for target in "${TARGETS[@]}"; do
    elf="${ELFS[$target]:-}"
    if [[ -z "$elf" ]]; then
        echo "size_gate: unknown target '$target'" >&2
        fail=1
        continue
    fi
    if [[ ! -f "$elf" ]]; then
        echo "size_gate: missing $elf (build '$target' first)" >&2
        fail=1
        continue
    fi

    read -r text data bss _dec _hex < <("$SIZE_BIN" "$elf" | awk 'NR==2 {print $1, $2, $3, $4, $5}')
    flash=$((text + data))
    ram=$((bss + data))
    flash_max="${FLASH_BUDGET[$target]}"
    ram_max="${RAM_BUDGET[$target]}"
    status=""

    if ((flash > flash_max)); then
        status="FLASH BUDGET"
    fi
    if ((ram > ram_max)); then
        status="${status:+$status, }RAM BUDGET"
    fi

    baseline="reports/size-baseline-$target.txt"
    if [[ -f "$baseline" ]]; then
        read -r btext bdata bbss _ _ < <(awk 'NR==2 {print $1, $2, $3, $4, $5}' "$baseline")
        if ! awk -v cur="$text" -v base="$btext" -v pct="$MAX_TEXT_GROWTH_PCT" \
            -v slack="$ABS_SLACK" 'BEGIN { exit !(cur <= base + base * pct / 100 + slack) }'; then
            status="${status:+$status, }TEXT REGRESSION"
        fi
        if ! awk -v cur="$bss" -v base="$bbss" -v pct="$MAX_BSS_GROWTH_PCT" \
            -v slack="$ABS_SLACK" 'BEGIN { exit !(cur <= base + base * pct / 100 + slack) }'; then
            status="${status:+$status, }BSS REGRESSION"
        fi
    else
        status="${status:+$status, }NO BASELINE"
    fi

    if [[ -z "$status" ]]; then
        status="ok"
    fi

    if [[ "$status" != "ok" ]]; then
        fail=1
    fi

    printf '%-8s %10d %10d %10d %10d %-24s\n' \
        "$target" "$flash" "$flash_max" "$ram" "$ram_max" "$status"
done

if [[ $fail -ne 0 ]]; then
    echo
    echo "size_gate: FAILED (budgets: flash, RAM - 32 KiB stack; baselines: 5% + 2 KiB slack)"
    exit 1
fi

echo
echo "size_gate: all targets within budget and baseline tolerance"
