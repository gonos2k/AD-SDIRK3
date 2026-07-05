#!/bin/zsh
# INC 0 validation: the sdirk3_split_explicit knob is fully wired + default-off byte-identical.
#   OFF (default): [CONFIG EFFECTIVE] split_explicit=off, NO [SPLIT-EXPLICIT] marker, baseline behavior.
#   ON  (env=1)  : [CONFIG ENV]+[CONFIG EFFECTIVE] split_explicit true/ON, [SPLIT-EXPLICIT] marker fires,
#                  behavior UNCHANGED (falls through to ARK324 — same Stage-3 abort at dt=600).
cd /Users/yhlee/SDIRK3/test/em_b_wave
MDIR=inc0_validate_20260705
mkdir -p $MDIR
cp -f namelist.input $MDIR/nl.bak
# ensure scheme=6 (SDIRK3) + mode-3 + dt=600 (quick: aborts at Stage 3)
sed -i '' 's/^\( *time_integration_scheme *= *\)[0-9]*,/\16,/' namelist.input
sed -i '' 's/^\( *time_step *= *\)[0-9]*,/\1600,/' namelist.input
run () {
  local TAG="$1"; shift
  rm -f rsl.error.* rsl.out.* 2>/dev/null
  env WRF_SDIRK3_HEVI_SPLIT=1 "$@" \
      /opt/homebrew/bin/timeout 300 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > "$MDIR/wrf.$TAG.log" 2>&1
  cp -f rsl.error.0000 "$MDIR/rsl.error.$TAG" 2>/dev/null
  echo "===== $TAG ====="
  grep -aE '\[CONFIG ENV\] split_explicit|\[CONFIG EFFECTIVE\] split_explicit|\[SPLIT-EXPLICIT\]' "$MDIR/rsl.error.$TAG"
  echo "  Stage-3 outcome: $(grep -aoE 'STAGE GATE\].*Stage 3 failed|Timing for main' "$MDIR/rsl.error.$TAG" | head -1)"
}
run off
run on WRF_SDIRK3_SPLIT_EXPLICIT=1
cp -f $MDIR/nl.bak namelist.input; echo "namelist restored"
echo "=== EXPECT: off => split_explicit=off, no marker; on => true/ON + [SPLIT-EXPLICIT] marker; SAME Stage-3 outcome ==="
