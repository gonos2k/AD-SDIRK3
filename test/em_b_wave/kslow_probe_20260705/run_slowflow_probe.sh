#!/bin/zsh
# PHASE-0 DE-RISK for the Wall-2 sub-cycling fix. Question: does Wicker-Skamarock
# RK3 sub-stepping of the explicit slow tendency at h=dt/M bound the measured
# u-advection cascade? For each dt, the probe runs slow_flow(U_n, dt, M) for:
#   M=1              (positive control, h=dt) -> expect L1/L2 to GROW (over-extrapolation)
#   M=ceil(dt/4)     (h~4s)                   -> expect L0/L1/L2 to STAY O(1010)
# GO iff the M=ceil(dt/4) ladder stays ~1010 while M=1 grows.
cd /Users/yhlee/SDIRK3/test/em_b_wave
MDIR=kslow_probe_20260705
cp -f namelist.input "$MDIR/namelist.input.pre_slowflow_bak"
restore() { cp -f "$MDIR/namelist.input.pre_slowflow_bak" namelist.input; echo "namelist restored"; }
trap restore EXIT

run () {
  local TAG="$1"; local DT="$2"
  sed -i '' "s/^\( *time_step *= *\)[0-9]*,/\1${DT},/" namelist.input
  echo "===== dt=$DT (time_step: $(grep -E '^ *time_step ' namelist.input | head -1 | tr -s ' ')) ====="
  rm -f rsl.out.* rsl.error.* 2>/dev/null
  env WRF_SDIRK3_HEVI_SPLIT=1 WRF_SDIRK3_SLOWFLOW_PROBE=1 WRF_SDIRK3_DEBUG_LEVEL=1 \
      /opt/homebrew/bin/timeout 600 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > "$MDIR/wrf.slowflow_$TAG.log" 2>&1
  cp -f rsl.error.0000 "$MDIR/rsl.error.slowflow_$TAG" 2>/dev/null
  grep -aE '\[SLOWFLOW\]' "$MDIR/rsl.error.slowflow_$TAG"
}
run dt60  60
run dt600 600
echo "=== DONE ==="
