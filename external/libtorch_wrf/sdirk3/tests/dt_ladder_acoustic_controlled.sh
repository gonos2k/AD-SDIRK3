#!/usr/bin/env bash
# 9F.D25 (review section 9 + section 8): large-dt ladder with the acoustic timestep
# controlled AT ALL THREE RK STAGES, and a fail-close harness.
#
# WHY THIS WAS REWRITTEN. The 9F.D23 version had two defects, both of a class this
# investigation has repeatedly been burned by:
#
#   1. It controlled the WRONG THING. Fixing dt/num_sound_steps holds stages 2 and 3
#      constant, but acoustic_schedule() gives stage 1 ONE step of dt/3 regardless of
#      num_sound_steps (wrf_sdirk3_acoustic_substep.cpp:822). So across the ladder
#      stage-1 dts ran 200 / 100 / 50 s -- still a 4x dt ladder -- and at dt=600 that
#      single 200 s step is 5.3x COARSER than the 37.5 s steps it was compared
#      against. Stage 1 dominated the acoustic error budget and was the one axis NOT
#      controlled. WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS now fixes it.
#
#   2. It was not FAIL-CLOSE. With only `set -u`, a run that died before writing could
#      leave the PREVIOUS run's rsl.error.0000 in place, and `cp` would happily copy
#      it -- analysing run N-1 as run N. That is exactly the "the experiment was
#      switched on but the operand never changed" failure that has produced wrong
#      causal conclusions here before. Now: rsl.* is cleared before every run, the
#      namelist edit is VERIFIED to have taken, the log must exist and contain real
#      step records, and a manifest records what actually ran.
#
# Growth is compared at matched PHYSICAL time, not matched step index -- a fixed step
# window is a different physical window per dt.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO/test/em_b_wave" || { echo "no test/em_b_wave under $REPO"; exit 2; }
export OMP_NUM_THREADS=1 MKL_NUM_THREADS=1
OUT="${DT_LADDER_OUT:-$PWD/dt_ladder_out}"
mkdir -p "$OUT"

# Restore the namelist on ANY exit path, including timeout/interrupt. The previous
# version restored only on the normal path, so an interrupted run left the case
# configured for whatever dt was last set -- silently poisoning the next experiment.
NL_BACKUP="$(mktemp)"
cp namelist.input "$NL_BACKUP"
cleanup() { cp "$NL_BACKUP" namelist.input; rm -f "$NL_BACKUP" namelist.input.tmp; }
trap cleanup EXIT INT TERM

manifest="$OUT/manifest.txt"
{
  echo "commit         $(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "wrf.exe sha256 $(shasum -a 256 "$REPO/main/wrf.exe" | cut -d' ' -f1)"
  echo "wrfinput       $(shasum -a 256 wrfinput_d01 | cut -d' ' -f1)"
  echo "date           $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$manifest"

run () {  # $1=dt  $2=nss  $3=stage1_substeps  $4=tag
  local dt="$1" nss="$2" s1="$3" tag="$4"
  sed -i.tmp "s/^ time_step  *=.*/ time_step                           = ${dt},/" namelist.input
  sed -i.tmp "s/^ run_days  *=.*/ run_days                            = 0,/"      namelist.input
  sed -i.tmp "s/^ run_hours  *=.*/ run_hours                           = 6,/"     namelist.input
  rm -f namelist.input.tmp
  # A sed pattern that silently fails to match would run the PREVIOUS dt while the
  # log is filed under the new tag. Verify the edit actually took.
  grep -q "^ time_step  *= *${dt}," namelist.input || { echo "FAIL $tag: time_step edit did not apply"; return 1; }
  grep -q "^ run_hours  *= *6,"     namelist.input || { echo "FAIL $tag: run_hours edit did not apply"; return 1; }

  rm -f rsl.error.* rsl.out.*        # never analyse a previous run's log

  local rc=0
  WRF_SDIRK3_SPLIT_EXPLICIT=1 \
  WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND="$nss" \
  WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS="$s1" \
  WRF_SDIRK3_UTERMS_TRACE=1 \
    timeout 2000 ./wrf.exe >/dev/null 2>&1 || rc=$?

  [ -s rsl.error.0000 ]                    || { echo "FAIL $tag: no rsl.error.0000 (rc=$rc)"; return 1; }
  grep -q "Timing for main" rsl.error.0000 || { echo "FAIL $tag: log has no step records (rc=$rc)"; return 1; }
  cp rsl.error.0000 "$OUT/$tag.log"

  local nsteps s1dts s23dts
  nsteps=$(grep -c "Timing for main" rsl.error.0000)
  s1dts=$(python3 -c "print(round($dt/3/$s1,3))")
  s23dts=$(python3 -c "print(round($dt/$nss,3))")
  printf '%-24s dt=%-4s nss=%-3s s1sub=%-3s steps=%-5s stage1_dts=%-8s stage23_dts=%-8s rc=%s\n' \
    "$tag" "$dt" "$nss" "$s1" "$nsteps" "$s1dts" "$s23dts" "$rc" | tee -a "$manifest"
}

echo "=== axis C: ALL THREE stages held near dts ~ 37.5 s (stage 1 now controlled) ==="
# stage-1 dts = (dt/3)/s1 -> s1 = dt/(3*37.5): 600->5.33, 300->2.67, 150->1.33.
# Exact 37.5 is not reachable at every dt with integer substeps, so the nearest
# integer is used and the ACHIEVED stage-1 dts is printed rather than assumed.
run 600 16 6 C_dt600
run 300  8 3 C_dt300
run 150  4 2 C_dt150

echo "=== axis B (9F.D23, stage 1 UNCONTROLLED) kept as the explicit control ==="
run 600 16 1 B_dt600_s1uncontrolled
run 300  8 1 B_dt300_s1uncontrolled
run 150  4 1 B_dt150_s1uncontrolled

echo "manifest: $manifest"
