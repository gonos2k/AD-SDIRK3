#!/usr/bin/env bash
# check_ratchets.sh (PR 6): closes the ratchet-bypass the PR 5 reviewer
# flagged. Three conditions:
#   1. actual violation count == head baseline (fixing violations REQUIRES
#      lowering the baseline; leaving it stale also fails)
#   2. head baseline <= base-branch baseline (never raised; pass the base SHA
#      as $1 on PR events, omitted on push)
#   3. the baseline file is exactly one integer line
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
echo "OK: ratchet respected"
