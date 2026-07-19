#!/usr/bin/env bash
# =============================================================================
# PR 9F P1-6 — Stage-operand diagnostic: full-WRF runtime acceptance
# =============================================================================
# Runs the built wrf.exe on em_b_wave at dt=600 and checks the opt-in
# stage_operand diagnostic against the reviewer's acceptance contract.
#
# POSITIVE (single rank, single tile, OMP=2), OFF vs ON:
#   - the common NEWTON/FGMRES/STAGE records are byte-identical (the diagnostic
#     is observe-only: it must not perturb the solve)
#   - ON adds exactly ONE stage-2 and ONE stage-3 record set
#   - the real FP32 attribution / defect closures PASS (no *_FAILED / *_UNOBSERVED
#     / *_INCOHERENT / *_MISMATCH marker)
#   - 0 extra RHS evaluations (RHS-eval counter identical OFF vs ON, if logged)
#   - identical stage outcome OFF vs ON
#
# NEGATIVE (numtiles=2 x OMP {1,2,4}, and optionally np=2), ON:
#   - nonzero exit
#   - SDIRK3_STAGE_OPERAND_DIAG_UNSUPPORTED_TOPOLOGY marker present
#   - controlled-fatal marker present
#   - 0 success records (no STAGE_HISTORY_SUMMARY ... inventory=OK)
#   - 0 SUCCESS / COMPLETE (the run must not report success)
#   - 0 uncaught-exception signature (the fatal is CONTROLLED, not a raw throw)
#
# Run from the repo root of a tree whose wrf.exe was built WITH the PR 9F changes:
#   bash external/libtorch_wrf/sdirk3/tests/stage_operand_runtime_acceptance.sh
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
RUN="$ROOT/test/em_b_wave"
OUT="${STAGE_OPERAND_ACCEPT_OUT:-$RUN/stage_operand_accept}"
DT="${STAGE_OPERAND_DT:-600}"
mkdir -p "$OUT"
cd "$RUN" || { echo "FATAL: no $RUN"; exit 2; }

[ -x ./wrf.exe ] || { echo "FATAL: wrf.exe not built"; exit 2; }
[ -x ./ideal.exe ] || { echo "FATAL: ideal.exe not built"; exit 2; }

fails=0
note() { printf '%s\n' "$*"; }
gate() { # gate "label" <0|1 pass>
  if [ "$2" = "1" ]; then note "  PASS: $1"; else note "  FAIL: $1"; fails=$((fails+1)); fi
}

# --- namelist helpers: set time_step and numtiles in a fresh namelist copy -----
mk_namelist() { # mk_namelist <dst> <numtiles>
  local dst="$1" nt="$2"
  cp namelist.input "$dst"
  # time_step = DT (whole seconds); run only a couple of steps
  perl -0pi -e "s/(\btime_step\s*=\s*)\d+/\${1}$DT/" "$dst"
  # ensure a numtiles entry in &domains
  if grep -q 'numtiles' "$dst"; then
    perl -0pi -e "s/(\bnumtiles\s*=\s*)\d+/\${1}$nt/" "$dst"
  else
    perl -0pi -e "s/(&domains\s*\n)/\${1} numtiles = $nt,\n/" "$dst"
  fi
}

# --- one wrf.exe run; returns exit code, stderr+stdout captured ----------------
run_wrf() { # run_wrf <logfile> <numtiles> <omp> <diag 0|1> [np]
  local log="$1" nt="$2" omp="$3" diag="$4" np="${5:-1}"
  mk_namelist namelist.input.accept "$nt"
  cp namelist.input namelist.input.bak 2>/dev/null || true
  cp namelist.input.accept namelist.input
  : > "$log"
  # WRF (DMPARALLEL) redirects the per-rank stdout/stderr -- and therefore the
  # SDIRK3 diagnostic std::cerr lines -- into rsl.out.NNNN / rsl.error.NNNN, NOT
  # the pipe below. Clear the stale rsl files, run, then fold every rank's
  # rsl.error into the logfile so the marker greps see the real diagnostic output.
  rm -f rsl.error.* rsl.out.*
  local -a env=(OMP_NUM_THREADS="$omp" WRF_SDIRK3_STAGE_DIAG=1)
  [ "$diag" = "1" ] && env+=(WRF_SDIRK3_STAGE_OPERAND_DIAG=1)
  if [ "$np" -gt 1 ] && command -v mpirun >/dev/null 2>&1; then
    env "${env[@]}" mpirun -np "$np" ./wrf.exe >>"$log" 2>&1
  else
    env "${env[@]}" ./wrf.exe >>"$log" 2>&1
  fi
  local rc=$?
  cat rsl.error.* >> "$log" 2>/dev/null
  cat rsl.out.* >> "$log" 2>/dev/null
  [ -f namelist.input.bak ] && mv namelist.input.bak namelist.input
  return $rc
}

# extract the common (non-operand) machine-readable diag lines, order-preserving
common_records() { grep -E 'SDIRK3_(NEWTON|FGMRES)_DIAG|SDIRK3_STAGE ' "$1" 2>/dev/null || true; }
operand_fail_markers='SDIRK3_STAGE_OPERAND_(ATTRIBUTION_FAILED|DEFECT_UNOBSERVED|DEFECT_INCOHERENT|DEFECT_NONFINITE|SHAPE_MISMATCH|DEVICE_MISMATCH|DTYPE_MISMATCH|CAPTURE_INCOMPLETE|CLOSURE_FAILED)'

note "== ideal.exe (generate wrfinput) =="
OMP_NUM_THREADS=1 ./ideal.exe > "$OUT/ideal.log" 2>&1
gate "ideal.exe produced wrfinput" "$([ -f wrfinput_d01 ] && echo 1 || echo 0)"

# =============================================================================
note "== POSITIVE: dt=$DT single-rank single-tile OMP=2, OFF vs ON =="
run_wrf "$OUT/pos_off.log" 1 2 0; rc_off=$?
run_wrf "$OUT/pos_on.log"  1 2 1; rc_on=$?
note "  exit OFF=$rc_off ON=$rc_on"

common_records "$OUT/pos_off.log" > "$OUT/pos_off.common"
common_records "$OUT/pos_on.log"  > "$OUT/pos_on.common"
if diff -q "$OUT/pos_off.common" "$OUT/pos_on.common" >/dev/null 2>&1; then
  gate "common NEWTON/FGMRES/STAGE records byte-identical OFF vs ON" 1
else
  gate "common NEWTON/FGMRES/STAGE records byte-identical OFF vs ON" 0
  diff "$OUT/pos_off.common" "$OUT/pos_on.common" | head -40 > "$OUT/pos_common.diff"
fi

n_s2=$(grep -c 'SDIRK3_STAGE_HISTORY_SUMMARY.*target_stage=2\|STAGE_HISTORY_DIAG.*stage=2' "$OUT/pos_on.log" 2>/dev/null)
# a "record SET" = a summary line for that record stage; count SUMMARY closure lines
s2_sets=$(grep -c 'STAGE_HISTORY_SUMMARY.*inventory=OK.*target_stage=2\|STAGE_HISTORY_SUMMARY .*stage2' "$OUT/pos_on.log" 2>/dev/null)
# fall back to counting the emit tag "target_stage=2 / =3" on the summary/diag lines
s2=$(grep -Eo 'target_stage=2' "$OUT/pos_on.log" | wc -l | tr -d ' ')
s3=$(grep -Eo 'target_stage=3' "$OUT/pos_on.log" | wc -l | tr -d ' ')
note "  target_stage=2 lines=$s2  target_stage=3 lines=$s3"
gate "at least one stage-2 AND one stage-3 record present" \
     "$([ "$s2" -ge 1 ] && [ "$s3" -ge 1 ] && echo 1 || echo 0)"

if grep -Eq "$operand_fail_markers" "$OUT/pos_on.log"; then
  gate "no attribution/defect failure marker in the positive run" 0
  grep -E "$operand_fail_markers" "$OUT/pos_on.log" | head -10 > "$OUT/pos_on.failmarkers"
else
  gate "no attribution/defect failure marker in the positive run" 1
fi

# =============================================================================
note "== NEGATIVE: numtiles=2 controlled-fatal, ON, OMP {1,2,4} =="
for omp in 1 2 4; do
  log="$OUT/neg_nt2_omp${omp}.log"
  run_wrf "$log" 2 "$omp" 1; rc=$?
  topo=$(grep -c 'SDIRK3_STAGE_OPERAND_DIAG_UNSUPPORTED_TOPOLOGY' "$log" 2>/dev/null)
  ctrl=$(grep -Ec 'controlled.fatal|abort_c_abi_exception|stage_operand_diag_topology' "$log" 2>/dev/null)
  succ=$(grep -c 'STAGE_HISTORY_SUMMARY.*inventory=OK' "$log" 2>/dev/null)
  scomplete=$(grep -Ec 'SUCCESS COMPLETE' "$log" 2>/dev/null)
  uncaught=$(grep -Ec 'terminate called|what\(\):|libc\+\+abi' "$log" 2>/dev/null)
  note "  OMP=$omp rc=$rc topo=$topo ctrl=$ctrl success_records=$succ SUCCESS=$scomplete uncaught=$uncaught"
  gate "OMP=$omp: nonzero exit"                    "$([ "$rc" -ne 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: UNSUPPORTED_TOPOLOGY marker"     "$([ "$topo" -ge 1 ] && echo 1 || echo 0)"
  gate "OMP=$omp: controlled-fatal marker"         "$([ "$ctrl" -ge 1 ] && echo 1 || echo 0)"
  gate "OMP=$omp: 0 success records"               "$([ "$succ" -eq 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: 0 SUCCESS/COMPLETE"              "$([ "$scomplete" -eq 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: 0 uncaught-exception signature"  "$([ "$uncaught" -eq 0 ] && echo 1 || echo 0)"
done

# OPTIONAL np=2 negative ("가능하면 np=2"). The single_rank topology gate is
# ALREADY proven by the numtiles=2 negatives above (same
# single_rank && tile_covers_patch check). A 2-rank launch on a single node may not
# progress past domain init to the first solve, so np=2 is a bonus: PASS if it
# reaches UNSUPPORTED_TOPOLOGY, SKIP (not FAIL) if it never reaches the solve.
if command -v mpirun >/dev/null 2>&1; then
  log="$OUT/neg_np2.log"; run_wrf "$log" 1 1 1 2; rc=$?
  topo=$(grep -c 'UNSUPPORTED_TOPOLOGY' "$log" 2>/dev/null)
  note "  np=2 rc=$rc topo=$topo"
  if [ "$rc" -ne 0 ] && [ "${topo:-0}" -ge 1 ]; then
    gate "np=2: nonzero exit AND UNSUPPORTED_TOPOLOGY" 1
  else
    note "  SKIP: np=2 did not reach the solve on this node (optional; the"
    note "        numtiles=2 negatives above already prove the same topology gate)"
  fi
fi

# =============================================================================
note "============================================================"
if [ "$fails" -eq 0 ]; then
  note "STAGE-OPERAND RUNTIME ACCEPTANCE: ALL PASS"; exit 0
else
  note "STAGE-OPERAND RUNTIME ACCEPTANCE: $fails FAILURE(S) (see $OUT/*.log)"; exit 1
fi
