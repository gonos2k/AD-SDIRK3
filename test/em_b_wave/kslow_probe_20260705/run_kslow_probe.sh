#!/bin/zsh
# WALL-2 ROOT-CAUSE ISOLATION (dt=60, mode-3). Two measurements per stage:
#   [KSLOW COMP] per-field norms (u/v/w/ph/t/mu) => WHICH field carries the blow-up.
#   [KSLOW GEPS] |k_slow(U_n + eps*(U_conv-U_n))| for eps in {0.25,0.5,1,2} =>
#                log-log slope: ~1 linear positive-real mode; ~2 bilinear runaway.
# hevi_on : full 4-stage cascade (step 1 blows up AFTER the probe prints).
# hevi_off: baseline fail-closes at stage 3 => stages 1-2 only (HEVI-independence check).
cd /Users/yhlee/SDIRK3/test/em_b_wave
MDIR=kslow_probe_20260705
# --- set dt=60 (restore on exit) ---
cp -f namelist.input "$MDIR/namelist.input.dt600_bak"
sed -i '' 's/^\( *time_step *= *\)600,/\160,/' namelist.input
echo "time_step now: $(grep -E '^ *time_step ' namelist.input | head -1)"
restore() { cp -f "$MDIR/namelist.input.dt600_bak" namelist.input; echo "namelist restored to dt=600"; }
trap restore EXIT

run () {
  local TAG="$1"; shift
  rm -f rsl.out.* rsl.error.* 2>/dev/null
  env WRF_SDIRK3_KSLOW_PROBE=1 WRF_SDIRK3_DEBUG_LEVEL=1 "$@" \
      /opt/homebrew/bin/timeout 600 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > "$MDIR/wrf.$TAG.log" 2>&1
  cp -f rsl.error.0000 "$MDIR/rsl.error.$TAG" 2>/dev/null
  echo "===== $TAG ====="
  grep -aE '\[KSLOW COMP\]|\[KSLOW GEPS\]' "$MDIR/rsl.error.$TAG"
}
run hevi_on  WRF_SDIRK3_HEVI_SPLIT=1
run hevi_off WRF_SDIRK3_HEVI_SPLIT=0
echo "=== DONE ==="
