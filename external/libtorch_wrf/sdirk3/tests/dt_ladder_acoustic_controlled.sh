#!/usr/bin/env bash
# 9F.D23 (review §9): large-dt ladder with the ACOUSTIC TIMESTEP CONTROLLED.
#
# The earlier ladder varied dt with num_sound_steps fixed, so acoustic dts moved WITH
# dt and the two effects were confounded. Two axes here:
#   A  nss fixed at 4     -> dts = dt/4 varies   (the confounded baseline, kept as control)
#   B  dts fixed at 37.5  -> nss = dt/37.5       (isolates the large-step effect)
# Axis B is only reachable upward: the split guard requires nss EVEN and >= 4, so
# holding dts constant by lowering nss at small dt is illegal; raising nss at large dt
# is not. Hence dts=37.5 rather than the baseline 150.
#
# Growth is compared at matched PHYSICAL time, not matched step index -- a fixed step
# window is a different physical window per dt, which is what made an earlier ladder
# appear to change sign.
set -u
# Repo-relative, so this runs from any checkout. OUT is overridable.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO/test/em_b_wave" || { echo "no test/em_b_wave under $REPO"; exit 2; }
export OMP_NUM_THREADS=1 MKL_NUM_THREADS=1
OUT="${DT_LADDER_OUT:-$PWD/dt_ladder_out}"
mkdir -p "$OUT"
cp namelist.input namelist.input.ladderbak

run () {  # $1=dt  $2=nss  $3=tag
  sed -i.tmp "s/^ time_step  *=.*/ time_step                           = $1,/" namelist.input
  sed -i.tmp "s/^ run_days  *=.*/ run_days                            = 0,/"  namelist.input
  sed -i.tmp "s/^ run_hours  *=.*/ run_hours                           = 6,/" namelist.input
  rm -f namelist.input.tmp
  WRF_SDIRK3_SPLIT_EXPLICIT=1 \
  WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND="$2" \
  WRF_SDIRK3_UTERMS_TRACE=1 \
    timeout 1500 ./wrf.exe >/dev/null 2>&1
  cp rsl.error.0000 "$OUT/$3.log" 2>/dev/null
  local nsteps eff
  nsteps=$(grep -c "Timing for main" "$OUT/$3.log" 2>/dev/null || echo 0)
  eff=$(grep -o "time_step_sound=[0-9]*" "$OUT/$3.log" 2>/dev/null | tail -1)
  echo "$3  dt=$1 nss_requested=$2 ($eff) steps=$nsteps  dts=$(python3 -c "print($1/$2)")"
}

echo "=== axis A: nss fixed at 4 (dts varies WITH dt -- confounded control) ==="
run 600 4  A_dt600_nss4
run 300 4  A_dt300_nss4
run 150 4  A_dt150_nss4
echo "=== axis B: dts fixed at 37.5 (nss scales with dt -- isolates large-step) ==="
run 600 16 B_dt600_nss16
run 300 8  B_dt300_nss8
echo "   (B_dt150 == A_dt150_nss4: dt=150,nss=4 already gives dts=37.5)"

cp namelist.input.ladderbak namelist.input
rm -f namelist.input.tmp
echo "namelist restored"

# ---------------------------------------------------------------------------
# MEASURED 2026-07-27 (9F.D23), split-explicit path, em_b_wave:
#
#   sigma_eff = d(log|adv_z|)/dt over the matched PHYSICAL window 1h-5h
#     axis A (nss=4):   dt=600 6.99e-05 | dt=300 1.65e-04 | dt=150 1.47e-04
#     axis B (dts=37.5): dt=600 7.40e-05 | dt=300 1.02e-04 | dt=150 1.47e-04
#
#   The acoustic timestep IS a real confound: at the SAME dt=300, changing only
#   nss 4->8 moves sigma 1.65e-04 -> 1.02e-04 (-38%). An uncontrolled ladder
#   cannot be read.
#
#   All three start at the same |adv_z| at 1h (~2.09e4) and fan out, so the
#   ordering is physical, not a fitting artifact:
#     1h/2h/3h/4h/5h  dt=600  2.09e4 2.54e4 2.97e4 3.91e4 9.57e4
#                     dt=300  2.09e4 2.70e4 3.94e4 5.97e4 1.17e5
#                     dt=150  2.10e4 3.36e4 6.76e4 1.15e5 1.79e5
#   SMALLER dt grows FASTER -- the opposite of a large-step over-extrapolation.
#
#   Run to failure (24h target, nss=4):
#     dt=600 -> fails step  38 = 6.33 h
#     dt=150 -> fails step 165 = 6.88 h
#   A 4x smaller timestep buys 9% more physical time while sigma_eff DOUBLES.
#   Failure time is ~dt-independent; sigma_eff does not predict it.
# ---------------------------------------------------------------------------
