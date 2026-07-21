#!/usr/bin/env bash
# PR 9F.A (structural separation) — A1: BASELINE NUMERICAL FINGERPRINT.
#
# The stage-operand acceptance proves OFF==ON *within one binary*. It does NOT prove that a
# refactored binary reproduces the PRE-refactor binary: a change that perturbs both the OFF
# and ON records identically would pass OFF==ON yet not be byte-identical to the baseline.
#
# This harness closes that gap for the behavior-preserving PR A commits. It runs the model
# ON (deterministic single-rank / single-thread) and hashes the DETERMINISTIC numerical
# record streams — the Newton/FGMRES/STAGE norm records and the RHS operand-digest stream —
# into one sha256. Each PR A commit must reproduce the committed golden; a policy change
# (PR B/C) is the ONLY time the golden is allowed to move (with --update and a rationale).
#
# Usage:
#   numerical_fingerprint.sh            # run, compare to golden, exit !=0 on drift
#   numerical_fingerprint.sh --update   # capture a NEW golden (policy change only)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
RUN="$ROOT/test/em_b_wave"
GOLDEN="$HERE/numerical_fingerprint.golden"
MODE="${1:-check}"

cd "$RUN" || { echo "FATAL: no run dir $RUN"; exit 2; }
[ -x ./wrf.exe ]   || { echo "FATAL: wrf.exe not built";   exit 2; }
[ -x ./ideal.exe ] || { echo "FATAL: ideal.exe not built"; exit 2; }

sha256_of_stdin() { { sha256sum || shasum -a 256; } 2>/dev/null | cut -d' ' -f1; }

# mktemp'd logs (Gemini #65): unique names so concurrent runs don't clobber each other.
IDEAL_LOG="$(mktemp)"; WRF_LOG="$(mktemp)"
trap 'rm -f "$IDEAL_LOG" "$WRF_LOG"' EXIT

# Fresh IC, then a deterministic single-rank/single-thread ON run. WRF writes its std::cerr
# diagnostics into rsl.error.NNNN / rsl.out.NNNN, NOT the pipe, so clear stale rsl files
# first and read them back (exactly as the acceptance does).
rm -f rsl.error.* rsl.out.*
OMP_NUM_THREADS=1 ./ideal.exe > "$IDEAL_LOG" 2>&1
[ -f wrfinput_d01 ] || { echo "FATAL: ideal.exe produced no wrfinput_d01"; exit 2; }
rm -f rsl.error.* rsl.out.*
OMP_NUM_THREADS=1 WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_RHS_COUNT=1 \
    WRF_SDIRK3_STAGE_OPERAND_DIAG=1 ./wrf.exe > "$WRF_LOG" 2>&1
cat rsl.error.* rsl.out.* >> "$WRF_LOG" 2>/dev/null

# The deterministic numerical streams (same predicates the acceptance compares OFF vs ON).
FP="$(
  { grep -E 'SDIRK3_(NEWTON|FGMRES|STAGE)_DIAG\b' "$WRF_LOG"
    grep -E '^SDIRK3_RHS_DIGEST '                 "$WRF_LOG"; } \
  | sort | sha256_of_stdin )"
RHS_TOTAL="$(grep -Eo 'SDIRK3_RHS_RUN_TOTAL[^ ]* total=[0-9]+' "$WRF_LOG" | grep -Eo 'total=[0-9]+' | tail -1)"

echo "fingerprint sha256: $FP"
echo "rhs run total:      ${RHS_TOTAL:-<none>}"

if [ "$MODE" = "--update" ]; then
  printf '%s\n' "$FP" > "$GOLDEN"
  echo "GOLDEN UPDATED -> $GOLDEN  (policy change only)"
  exit 0
fi

[ -f "$GOLDEN" ] || { echo "FATAL: no golden at $GOLDEN (run --update first)"; exit 2; }
WANT="$(cat "$GOLDEN")"
if [ "$FP" = "$WANT" ]; then
  echo "NUMERICAL FINGERPRINT: MATCH (byte-identical to baseline)"
  exit 0
fi
echo "NUMERICAL FINGERPRINT: DRIFT"
echo "  golden: $WANT"
echo "  got:    $FP"
exit 1
