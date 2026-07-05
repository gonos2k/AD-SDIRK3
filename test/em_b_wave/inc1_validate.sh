#!/bin/zsh
# INC 1 validation: libtorch small_step_prep c2a = (cp/cv)*(pb+p)/alt must match dyn_em.
#
# dyn_em REFERENCE (measured 2026-07-06 at the em_b_wave IC via a TEMP parity dump in
# dyn_em/module_small_step_em.F:233 `[PARITY c2a]`, since REVERTED to keep the tree clean):
#     c2a mean = 44073.1 , alt mean = 2.4525 , p_full mean = 41459.7   (n=204800)
# To regenerate this reference, re-add that dump (see git history / the walls-measurement doc),
# run scheme=3, and read the `[PARITY c2a]` line.
#
# This script re-runs ONLY the libtorch side (scheme=6 + split_explicit=1), extracts its c2a mean
# from `[SPLIT-EXPLICIT PREP]`, ASSERTS it is within tolerance of the dyn_em reference, prints
# PASS/FAIL, and EXITS NON-ZERO on failure (so it cannot silently mask a regression).
set -e
cd /Users/yhlee/SDIRK3/test/em_b_wave
DYNEM_C2A_MEAN=44073.1
TOL_PCT=5.0
MDIR=inc1_validate_20260705
mkdir -p $MDIR
cp -f namelist.input $MDIR/nl.bak
restore() { cp -f "$MDIR/nl.bak" namelist.input; }
trap restore EXIT
sed -i '' 's/^\( *time_integration_scheme *= *\)[0-9]*,/\16,/' namelist.input
sed -i '' 's/^\( *time_step *= *\)[0-9]*,/\1600,/' namelist.input
# Clear BOTH the live rsl and the saved copy so a stale prior run can never be graded.
rm -f rsl.error.* rsl.out.* "$MDIR/rsl.error.libtorch" 2>/dev/null
# The dt=600 mode-3 run aborts at Stage 3 BY DESIGN (split_explicit falls through to ARK324);
# the Inc-1 prep dump fires before that. `|| true` tolerates that expected non-zero exit ONLY —
# the freshness checks below reject a run that never started or produced no output.
env WRF_SDIRK3_HEVI_SPLIT=1 WRF_SDIRK3_SPLIT_EXPLICIT=1 \
    /opt/homebrew/bin/timeout 300 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > $MDIR/wrf.libtorch.log 2>&1 || true
if [ ! -f rsl.error.0000 ]; then
  echo "FAIL: this run produced no rsl.error.0000 (wrf.exe did not start / crashed at init)"
  exit 1
fi
cp -f rsl.error.0000 "$MDIR/rsl.error.libtorch"

LINE=$(grep -aE '\[SPLIT-EXPLICIT PREP\]' "$MDIR/rsl.error.libtorch" | head -1)
echo "$LINE"
C2A=$(echo "$LINE" | sed -nE 's/.*c2a L2=[^ ]+ mean=([0-9.eE+-]+).*/\1/p')
if [ -z "$C2A" ]; then
  echo "FAIL: no [SPLIT-EXPLICIT PREP] c2a line found (build not applied, or run crashed before the prep dump)"
  exit 1
fi
VERDICT=$(awk -v a="$C2A" -v b="$DYNEM_C2A_MEAN" -v t="$TOL_PCT" \
  'BEGIN{d=100*(a-b)/b; if(d<0)d=-d; printf "%s %.2f", (d<=t?"PASS":"FAIL"), (a-b)/b*100}')
STATUS=${VERDICT%% *}
DIFF=${VERDICT##* }
echo "c2a: libtorch=$C2A  dyn_em_ref=$DYNEM_C2A_MEAN  diff=${DIFF}%  tol=${TOL_PCT}%  => $STATUS"
[ "$STATUS" = "PASS" ] || { echo "INC 1 VALIDATION FAILED"; exit 1; }
echo "INC 1 VALIDATION PASSED"
