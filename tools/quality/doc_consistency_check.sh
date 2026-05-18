#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

fail=0

if grep -R --line-number "ALIAS.CFG" docs >/dev/null; then
  echo "Inconsistent alias filename found in docs (expected ALIASES.CFG)."
  fail=1
fi

if grep -R --line-number "HABITS.TXT" docs >/dev/null; then
  echo "Inconsistent habits filename found in docs (expected HABITS.CFG)."
  fail=1
fi

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "Documentation consistency checks passed."
