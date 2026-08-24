#!/usr/bin/env bash
# check_ratchets.sh (PR 6): closes the ratchet-bypass the PR 5 reviewer
# flagged. Three conditions, applied to EACH ratchet:
#   1. actual violation count == head baseline (fixing violations REQUIRES
#      lowering the baseline; leaving it stale also fails)
#   2. head baseline <= base-branch baseline (never raised; pass the base SHA
#      as $1 on PR events, omitted on push)
#   3. the baseline file is exactly one integer line
#
# R13.20 (adversarial loop, iteration 4): a SECOND ratchet, on unguarded
# `.item()`. CLAUDE.md states that rule unconditionally, and the project has
# recorded it as a repeated regression -- most recently a NoGradGuard declared
# inside a loop, whose scope had closed before a later block in the same
# function, in a probe a review had just checked for exactly this class. Review
# cannot hold it: NoGradGuard is RAII and the audit found 129 sites. So it is
# counted. Extending this script rather than the workflow keeps the YAML
# untouched -- this repo has rotted a CI-side counter four times.
set -euo pipefail
cd "$(dirname "$0")/../.."
BASE_SHA="${1:-}"
D=external/libtorch_wrf/sdirk3
BASE_FILE="$D/tests/lint_from_blob_baseline.txt"

if ! grep -qE '^[0-9]+$' "$BASE_FILE" || [ "$(wc -l < "$BASE_FILE" | tr -d ' ')" -ne 1 ]; then
  echo "FAIL: baseline file must be exactly one integer line"; exit 1
fi
head_baseline="$(cat "$BASE_FILE")"

actual="$(grep -n 'from_blob(' "$D"/*.cpp "$D"/*.h 2>/dev/null \
  | grep -v 'make_cpu_from_blob_opts' | grep -v 'LINT_EXCEPTION' \
  | grep -v '// from_blob' | grep -v 'FIX.*from_blob' \
  | grep -v 'inline.*from_blob' | grep -v '@brief.*from_blob' \
  | grep -vE ':[[:space:]]*(\*|//)' | grep -c . || true)"

echo "from_blob ratchet: actual=$actual head_baseline=$head_baseline"
if [ "$actual" -ne "$head_baseline" ]; then
  echo "FAIL: actual ($actual) != head baseline ($head_baseline)."
  echo "      Fixed violations MUST lower the baseline; new violations are forbidden."
  exit 1
fi
if [ -n "$BASE_SHA" ]; then
  base_baseline="$(git show "$BASE_SHA:$BASE_FILE" 2>/dev/null || echo "")"
  if [ -n "$base_baseline" ]; then
    echo "base-branch baseline: $base_baseline"
    if [ "$head_baseline" -gt "$base_baseline" ]; then
      echo "FAIL: baseline increased vs base branch ($base_baseline -> $head_baseline)"; exit 1
    fi
  else
    echo "note: baseline absent on base branch (new file) — head checks already applied"
  fi
fi

# ---- ratchet 2: `.item()` outside NoGradGuard and not on a detached operand ----
ITEM_BASE_FILE="$D/tests/lint_item_guard_baseline.txt"
if ! grep -qE '^[0-9]+$' "$ITEM_BASE_FILE" || [ "$(wc -l < "$ITEM_BASE_FILE" | tr -d ' ')" -ne 1 ]; then
  echo "FAIL: item-guard baseline file must be exactly one integer line"; exit 1
fi
item_head_baseline="$(cat "$ITEM_BASE_FILE")"
item_actual="$(python3 .github/ci/lint_item_guard.py)"

echo "item-guard ratchet: actual=$item_actual head_baseline=$item_head_baseline"
if [ "$item_actual" -ne "$item_head_baseline" ]; then
  echo "FAIL: actual ($item_actual) != head baseline ($item_head_baseline)."
  echo "      Every .item() must sit inside a NoGradGuard (or use guarded_item<T>)."
  echo "      Fixed sites MUST lower the baseline; new ones are forbidden."
  echo "      Run: python3 .github/ci/lint_item_guard.py --list"
  exit 1
fi
if [ -n "$BASE_SHA" ]; then
  item_base_baseline="$(git show "$BASE_SHA:$ITEM_BASE_FILE" 2>/dev/null || echo "")"
  if [ -n "$item_base_baseline" ]; then
    echo "base-branch item-guard baseline: $item_base_baseline"
    if [ "$item_head_baseline" -gt "$item_base_baseline" ]; then
      echo "FAIL: item-guard baseline increased vs base branch ($item_base_baseline -> $item_head_baseline)"; exit 1
    fi
  else
    echo "note: item-guard baseline absent on base branch (new file) — head checks already applied"
  fi
fi

echo "OK: ratchets respected"
