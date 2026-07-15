#!/usr/bin/env bash
# run_decomposition_matrix.sh (PR 7C): SDIRK3 decomposition fail-close matrix.
#
# Drives a real WRF em_b_wave build across the decompositions in
# decomposition_cases.txt and pins the SDIRK3 support boundary:
#   - the sole supported decomposition (1 rank, 1 tile) reaches SUCCESS
#     COMPLETE and reproduces the six split-explicit stage norms bit-for-bit;
#   - every unsupported decomposition is REFUSED with its exact marker, a
#     nonzero exit, no SUCCESS COMPLETE, and — critically — no stage-norm
#     output at all, proving the refusal happens BEFORE any solve/state
#     mutation (the guards sit in advance_implicit's prologue).
#
# Two modes:
#   (default)     run the matrix on a built tree (needs mpirun + ideal/wrf.exe).
#                 Invoked by the self-hosted wrf-integration workflow (PR 7D).
#   --self-test   exercise the assertion logic against synthetic fixtures with
#                 NO WRF/mpirun, proving the harness both ACCEPTS valid evidence
#                 and REJECTS missing/wrong evidence. Runs in hosted CI.
#
# A marker-only pass is not enough and a nonzero-exit-only pass is not enough:
# a reject case must carry the marker AND lack SUCCESS COMPLETE AND lack stage
# norms, so a crash for an unrelated reason cannot masquerade as the contract.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$REPO_ROOT/.github/ci/decomposition_cases.txt"
GOLDEN="$REPO_ROOT/test/em_b_wave/ci_expected_stage_norms.txt"
CASE_DIR="test/em_b_wave"
CASE_TIMEOUT="${SDIRK3_DECOMP_CASE_TIMEOUT:-600}"

STAGE_NORM_GREP='SPLIT-EXPLICIT RK. stage'
fail=0

# ---------------------------------------------------------------------------
# Evidence predicates — pure functions over a captured log directory. Shared
# by the live runner path and --self-test so both exercise identical logic.
# ---------------------------------------------------------------------------

# Number of rsl.error.* files carrying the marker (0 = none).
marker_rank_count() {
  local marker="$1" logdir="$2" n=0 f
  for f in "$logdir"/rsl.error.*; do
    [ -e "$f" ] || continue
    if grep -q -- "$marker" "$f" 2>/dev/null; then n=$((n + 1)); fi
  done
  printf '%s' "$n"
}

has_success_complete() {
  grep -q "SUCCESS COMPLETE" "$1/rsl.error.0000" 2>/dev/null
}

has_stage_norms() {
  grep -qE "$STAGE_NORM_GREP" "$1/rsl.error.0000" 2>/dev/null
}

stage_norms_match_golden() {
  local logdir="$1"
  local actual="$logdir/stage_norms_actual.txt"
  grep -E "$STAGE_NORM_GREP" "$logdir/rsl.error.0000" 2>/dev/null | head -6 > "$actual" || true
  diff "$GOLDEN" "$actual" >/dev/null 2>&1
}

# assert_case <case_id> <expect> <marker> <exit_code> <logdir>
# Returns 0 iff every required piece of evidence holds. Prints PASS/FAIL.
assert_case() {
  local id="$1" expect="$2" marker="$3" ec="$4" logdir="$5"

  if [ "$ec" -eq 124 ]; then
    echo "FAIL [$id]: timed out (exit 124) — deadlock/hang, not a clean outcome"
    return 1
  fi

  case "$expect" in
    success)
      if [ "$ec" -ne 0 ]; then
        echo "FAIL [$id]: expected success, exit=$ec"; return 1; fi
      if ! has_success_complete "$logdir"; then
        echo "FAIL [$id]: no SUCCESS COMPLETE"; return 1; fi
      if ! has_stage_norms "$logdir"; then
        echo "FAIL [$id]: no split-explicit stage norms emitted"; return 1; fi
      if ! stage_norms_match_golden "$logdir"; then
        echo "FAIL [$id]: stage norms differ from the tracked golden baseline"; return 1; fi
      echo "PASS [$id]: SUCCESS COMPLETE + six stage norms bit-identical"
      return 0
      ;;
    reject)
      if [ "$ec" -eq 0 ]; then
        echo "FAIL [$id]: expected rejection, exit=0"; return 1; fi
      local n; n="$(marker_rank_count "$marker" "$logdir")"
      if [ "$n" -lt 1 ]; then
        echo "FAIL [$id]: rejection marker '$marker' on no rank"; return 1; fi
      if has_success_complete "$logdir"; then
        echo "FAIL [$id]: SUCCESS COMPLETE present on a rejected decomposition"; return 1; fi
      if has_stage_norms "$logdir"; then
        echo "FAIL [$id]: stage norms present — refusal did NOT precede the solve"; return 1; fi
      echo "PASS [$id]: exit=$ec, marker on $n rank(s), no SUCCESS COMPLETE, no pre-refusal solve"
      return 0
      ;;
    *)
      echo "FAIL [$id]: unknown expect '$expect'"; return 1 ;;
  esac
}

# ---------------------------------------------------------------------------
# Live runner path
# ---------------------------------------------------------------------------

# Regenerate a 2-step namelist for one case from the tracked template.
write_namelist() {
  local nx="$1" ny="$2" mode="$3"
  python3 - "$CASE_DIR/namelist.input" "$nx" "$ny" "$mode" <<'PY'
import re, sys
path, nx, ny, mode = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
s = open(path).read()
s = re.sub(r'(run_days\s*=\s*)\d+',    r'\g<1>0',    s, count=1)
s = re.sub(r'(run_hours\s*=\s*)\d+',   r'\g<1>0',    s, count=1)
s = re.sub(r'(run_minutes\s*=\s*)\d+', r'\g<1>0',    s, count=1)
s = re.sub(r'(run_seconds\s*=\s*)\d+', r'\g<1>1200', s, count=1)
s = re.sub(r'(nproc_x\s*=\s*)\d+', r'\g<1>' + nx, s, count=1)
s = re.sub(r'(nproc_y\s*=\s*)\d+', r'\g<1>' + ny, s, count=1)
# strip any prior injected knobs so cases do not leak into each other
s = re.sub(r'^\s*numtiles\s*=.*\n', '', s, flags=re.M)
s = re.sub(r'^\s*sdirk3_enable_ad_halo_exchange\s*=.*\n', '', s, flags=re.M)
if mode == 'split_multitile':
    s = s.replace('&domains',  '&domains\n numtiles = 2,', 1)
    s = s.replace('&dynamics', '&dynamics\n sdirk3_enable_ad_halo_exchange = .true.,', 1)
open(path, 'w').write(s)
PY
}

run_case() {
  local id="$1" ranks="$2" nx="$3" ny="$4" mode="$5" expect="$6" marker="$7"
  local logdir="$CASE_DIR/logs_$id"
  rm -rf "$logdir"; mkdir -p "$logdir"

  write_namelist "$nx" "$ny" "$mode"

  # ideal.exe builds the IC at this decomposition; its failure is an
  # environment fault, reported distinctly from the SDIRK3 contract.
  ( cd "$CASE_DIR" && rm -f rsl.error.* rsl.out.* )
  if ! ( cd "$CASE_DIR" && timeout "$CASE_TIMEOUT" mpirun -np "$ranks" ./ideal.exe >/dev/null 2>&1 ); then
    echo "FAIL [$id]: ideal.exe did not complete at np=$ranks (environment/decomposition)"
    ( cd "$CASE_DIR" && cp -f rsl.error.* rsl.out.* "../../$logdir/" 2>/dev/null || true )
    return 1
  fi

  ( cd "$CASE_DIR" && rm -f rsl.error.* rsl.out.* )
  local ec=0
  ( cd "$CASE_DIR" && env WRF_SDIRK3_SPLIT_EXPLICIT=1 \
      timeout "$CASE_TIMEOUT" mpirun -np "$ranks" ./wrf.exe >/dev/null 2>&1 ) || ec=$?
  # Preserve every rank's logs for the CI artifact before asserting.
  ( cd "$CASE_DIR" && cp -f rsl.error.* rsl.out.* "logs_$id/" 2>/dev/null || true )

  assert_case "$id" "$expect" "$marker" "$ec" "$logdir"
}

run_matrix() {
  command -v mpirun >/dev/null 2>&1 || { echo "FAIL: mpirun not on PATH"; exit 1; }
  [ -x "$CASE_DIR/ideal.exe" ] && [ -x "$CASE_DIR/wrf.exe" ] || {
    echo "FAIL: $CASE_DIR/{ideal,wrf}.exe not built"; exit 1; }
  cp -f "$CASE_DIR/namelist.input" "$CASE_DIR/namelist.input.7c_template"
  local restore="$CASE_DIR/namelist.input.7c_template"
  # shellcheck disable=SC2064
  trap "cp -f '$restore' '$CASE_DIR/namelist.input'; rm -f '$restore'" EXIT

  local id ranks nx ny mode expect marker
  while read -r id ranks nx ny mode expect marker; do
    case "$id" in ''|\#*) continue ;; esac
    cp -f "$restore" "$CASE_DIR/namelist.input"
    run_case "$id" "$ranks" "$nx" "$ny" "$mode" "$expect" "$marker" || fail=1
  done < "$MANIFEST"

  if [ "$fail" -ne 0 ]; then
    echo "DECOMPOSITION MATRIX: FAILED"; exit 1; fi
  echo "DECOMPOSITION MATRIX: all cases hold"
}

# ---------------------------------------------------------------------------
# Self-test: assertion logic against synthetic fixtures (no WRF/mpirun).
# Verifies assert_case ACCEPTS valid evidence and REJECTS every way a case
# could silently pass on the wrong evidence.
# ---------------------------------------------------------------------------
self_test() {
  local tmp st=0
  tmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" RETURN

  local good_norms; good_norms="$(cat "$GOLDEN")"

  mk() { mkdir -p "$tmp/$1"; }
  err() { printf '%s\n' "$2" > "$tmp/$1/rsl.error.0000"; }

  expect_verdict() { # <want:0|1> <case_id> <expect> <marker> <ec> <logdir>
    local want="$1"; shift
    if assert_case "$@" >/dev/null 2>&1; then local got=0; else local got=1; fi
    if [ "$got" -ne "$want" ]; then
      echo "SELFTEST FAIL: $2 want=$want got=$got"; st=1; fi
  }

  # success: valid evidence PASSES
  mk s_ok; printf 'SUCCESS COMPLETE\n%s\n' "$good_norms" > "$tmp/s_ok/rsl.error.0000"
  expect_verdict 0 s_ok success - 0 "$tmp/s_ok"
  # success negatives: each missing piece FAILS
  mk s_ec;  printf 'SUCCESS COMPLETE\n%s\n' "$good_norms" > "$tmp/s_ec/rsl.error.0000"
  expect_verdict 1 s_ec success - 1 "$tmp/s_ec"          # nonzero exit
  mk s_nsc; printf '%s\n' "$good_norms" > "$tmp/s_nsc/rsl.error.0000"
  expect_verdict 1 s_nsc success - 0 "$tmp/s_nsc"        # no SUCCESS COMPLETE
  mk s_drift; printf 'SUCCESS COMPLETE\n[SPLIT-EXPLICIT RK] stage=1 dts=200 n_sub=1 |U_stage-U_n|=999.999\n' > "$tmp/s_drift/rsl.error.0000"
  expect_verdict 1 s_drift success - 0 "$tmp/s_drift"    # norms differ from golden

  # reject: valid evidence PASSES
  mk r_ok; err r_ok "SDIRK3_MPI_STAGE_HALO_UNSUPPORTED: decomposition 2 x 1"
  expect_verdict 0 r_ok reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 1 "$tmp/r_ok"
  # reject negatives
  mk r_ec; err r_ec "SDIRK3_MPI_STAGE_HALO_UNSUPPORTED"
  expect_verdict 1 r_ec reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 0 "$tmp/r_ec"   # exit 0
  mk r_nomark; err r_nomark "some unrelated crash"
  expect_verdict 1 r_nomark reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 1 "$tmp/r_nomark"  # wrong reason
  mk r_sc; printf 'SDIRK3_MPI_STAGE_HALO_UNSUPPORTED\nSUCCESS COMPLETE\n' > "$tmp/r_sc/rsl.error.0000"
  expect_verdict 1 r_sc reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 1 "$tmp/r_sc"   # success leaked
  mk r_norms; printf 'SDIRK3_MPI_STAGE_HALO_UNSUPPORTED\n[SPLIT-EXPLICIT RK] stage=1 dts=200 n_sub=1 |U_stage-U_n|=1.0\n' > "$tmp/r_norms/rsl.error.0000"
  expect_verdict 1 r_norms reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 1 "$tmp/r_norms"  # solved before refusing
  mk r_to; err r_to "SDIRK3_MPI_STAGE_HALO_UNSUPPORTED"
  expect_verdict 1 r_to reject SDIRK3_MPI_STAGE_HALO_UNSUPPORTED 124 "$tmp/r_to"  # timeout/deadlock

  # manifest sanity: every non-comment row has 7 fields and a valid expect
  local rows=0 id ranks nx ny mode expect marker
  while read -r id ranks nx ny mode expect marker; do
    case "$id" in ''|\#*) continue ;; esac
    rows=$((rows + 1))
    case "$expect" in success|reject) ;; *) echo "SELFTEST FAIL: bad expect '$expect' in $id"; st=1 ;; esac
    [ "$((nx * ny))" -eq "$ranks" ] || { echo "SELFTEST FAIL: $id nproc_x*nproc_y != ranks"; st=1; }
    [ "$expect" = success ] && [ "$marker" != "-" ] && { echo "SELFTEST FAIL: $id success with marker"; st=1; }
    [ "$expect" = reject ]  && [ "$marker" = "-" ] && { echo "SELFTEST FAIL: $id reject without marker"; st=1; }
  done < "$MANIFEST"
  [ "$rows" -ge 4 ] || { echo "SELFTEST FAIL: manifest has $rows cases, expected >= 4"; st=1; }

  if [ "$st" -ne 0 ]; then echo "SELF-TEST: FAILED"; return 1; fi
  echo "SELF-TEST: assertion logic + manifest sane ($rows cases)"
  return 0
}

case "${1:-}" in
  --self-test) self_test ;;
  '')          run_matrix ;;
  *)           echo "usage: $0 [--self-test]"; exit 2 ;;
esac
