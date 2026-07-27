#!/usr/bin/env bash
# 9F.D24 (review §10.3): normalized mode collapse.
#
# If the early growth is a LINEAR mode seeded by the bubble, then
#     dU_eps(t) / eps ,   dU_eps = U_eps(t) - U_0(t)
# must collapse onto one field across eps. The eps=0 control run is what makes
# dU meaningful at all -- without it the base jet's own evolution is mixed into
# every difference -- and it only became reachable with WRF_BWAVE_ALLOW_ZERO
# (9F.D21). This is a much stronger test of "same linearized mode" than a
# failure-step shift, which only reports a scalar.
# 9F.D27 (review section 9): fail-close, matching dt_ladder. These are the SAME
# defects I fixed there one commit earlier and left here -- set -u only, no trap, and
# no clearing of rsl.* between runs, so a run that died early could be analysed using
# the PREVIOUS run's log. Fixing one harness and not its sibling is how a known defect
# class survives.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO/test/em_b_wave" || { echo "no test/em_b_wave under $REPO"; exit 2; }
export OMP_NUM_THREADS=1 MKL_NUM_THREADS=1
OUT="${MODE_COLLAPSE_OUT:-$PWD/mc_out}"
mkdir -p "$OUT"
NL_BACKUP="$(mktemp)"
cp namelist.input "$NL_BACKUP"
cleanup() { cp "$NL_BACKUP" namelist.input; rm -f "$NL_BACKUP" namelist.input.tmp; }
trap cleanup EXIT INT TERM
# 2 h window = 12 steps at dt=600: inside the linear regime, well short of the
# 6.33 h failure, so no run is contaminated by its own terminal blow-up.
sed -i.tmp 's/^ run_days  *=.*/ run_days                            = 0,/; s/^ run_hours  *=.*/ run_hours                           = 2,/' namelist.input
rm -f namelist.input.tmp

for eps in 1.0 0.5 0.25 0.125 0.0625 0.0; do
  rm -f wrfinput_d01 wrfout_d01_* rsl.error.* rsl.out.*
  if [ "$eps" = "0.0" ]; then
    WRF_BWAVE_PERT_SCALE=0.0 WRF_BWAVE_ALLOW_ZERO=1 timeout 300 ./ideal.exe >/dev/null 2>&1
  else
    WRF_BWAVE_PERT_SCALE="$eps" timeout 300 ./ideal.exe >/dev/null 2>&1
  fi
  [ -f wrfinput_d01 ] || { echo "FAIL eps=$eps: ideal.exe produced no IC"; exit 1; }
  rc=0
  WRF_SDIRK3_SPLIT_EXPLICIT=1 timeout 900 ./wrf.exe >/dev/null 2>&1 || rc=$?
  [ -s rsl.error.0000 ] || { echo "FAIL eps=$eps: no rsl.error.0000 (rc=$rc)"; exit 1; }
  grep -q "Timing for main" rsl.error.0000 || { echo "FAIL eps=$eps: no step records (rc=$rc)"; exit 1; }
  f=$(ls wrfout_d01_* 2>/dev/null | head -1)
  [ -n "$f" ] || { echo "FAIL eps=$eps: no wrfout (rc=$rc)"; exit 1; }
  cp "$f" "$OUT/wrfout_eps$eps.nc"
  echo "eps=$eps steps=$(grep -c 'Timing for main' rsl.error.0000) rc=$rc out=yes"
done

rm -f wrfinput_d01 wrfout_d01_*
timeout 300 ./ideal.exe >/dev/null 2>&1
echo "baseline IC restored: $([ -f wrfinput_d01 ] && echo yes || echo NO)"

# ---------------------------------------------------------------------------
# MEASURED 2026-07-27 (9F.D24), split-explicit, dt=600, 2 h window (12 steps):
#
#   AMPLITUDE SCALING  d log||dU|| / d log eps = 0.99768   (1.000 = exactly linear)
#                      max|residual| = 2.0e-03 over eps = 1 .. 1/16
#   COLLAPSE           corr(dU_eps/eps, dU_1/eps) >= 0.9999 for eps >= 0.25
#
#   The residual decorrelation at small eps is an ADDITIVE, eps-INDEPENDENT NOISE
#   FLOOR, not nonlinearity. Referenced to the highest-SNR member (eps=1):
#       eps     1-corr     ratio to previous
#       0.5     9.87e-05        --
#       0.25    3.90e-04      3.96
#       0.125   1.68e-03      4.30
#       0.0625  6.73e-03      4.01
#   d log(1-corr)/d log eps = -2.038 over a 16x range. Signal ~ eps against a fixed
#   noise gives exactly -2; genuine nonlinearity would give a POSITIVE slope, since
#   it must VANISH as eps->0. It does the opposite.
#
#   NOTE: reference the CLEANEST (largest-eps) member. Using the smallest eps as
#   reference flattens the slope to -0.045 and hides the effect entirely, because
#   every comparison is then limited by that member's own noise.
#
#   PRACTICAL CALIBRATION: eps >= 0.25 is the usable range for amplitude-scaling
#   experiments on this case; below it the perturbation approaches the floor.
#
#   The eps=0 control is what makes dU meaningful and only became reachable with
#   WRF_BWAVE_ALLOW_ZERO (9F.D21).
# ---------------------------------------------------------------------------
