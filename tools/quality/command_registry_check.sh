#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

python3 tools/commands/build_registry.py >/tmp/mellivora-registry.out

MISSING_COUNT="$(grep -E '^Missing in docs:' reports/command-coverage-latest.md | awk '{print $4}')"
if [[ -z "$MISSING_COUNT" ]]; then
  echo "Could not determine missing command count from coverage report."
  exit 1
fi

if [[ "$MISSING_COUNT" -ne 0 ]]; then
  echo "Command documentation coverage check failed."
  cat reports/command-coverage-latest.md
  exit 1
fi

echo "Command registry coverage checks passed."
