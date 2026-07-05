#!/bin/zsh
# INC 1 validation: libtorch small_step_prep c2a = (cp/cv)*(pb+p)/alt matches dyn_em.
#   scheme=6 + split_explicit=1 -> [SPLIT-EXPLICIT PREP] c2a (libtorch, at U_n = IC)
#   scheme=3 (stock RK3)        -> [PARITY c2a]        (dyn_em small_step_prep, at IC)
# Compare mean + max (element-count-robust). muts(mu_full) reported for reference.
cd /Users/yhlee/SDIRK3/test/em_b_wave
MDIR=inc1_validate_20260705
mkdir -p $MDIR
cp -f namelist.input $MDIR/nl.bak
sed -i '' 's/^\( *time_step *= *\)[0-9]*,/\1600,/' namelist.input

echo "===== libtorch (scheme=6, split_explicit=1) ====="
sed -i '' 's/^\( *time_integration_scheme *= *\)[0-9]*,/\16,/' namelist.input
rm -f rsl.error.* rsl.out.* 2>/dev/null
env WRF_SDIRK3_HEVI_SPLIT=1 WRF_SDIRK3_SPLIT_EXPLICIT=1 \
    /opt/homebrew/bin/timeout 300 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > $MDIR/wrf.libtorch.log 2>&1
cp -f rsl.error.0000 $MDIR/rsl.error.libtorch 2>/dev/null
grep -aE '\[SPLIT-EXPLICIT PREP\]' $MDIR/rsl.error.libtorch

echo "===== dyn_em (scheme=3, stock RK3) ====="
sed -i '' 's/^\( *time_integration_scheme *= *\)[0-9]*,/\13,/' namelist.input
rm -f rsl.error.* rsl.out.* 2>/dev/null
/opt/homebrew/bin/timeout 200 /opt/homebrew/bin/mpirun -np 1 ./wrf.exe > $MDIR/wrf.dynem.log 2>&1
cp -f rsl.error.0000 $MDIR/rsl.error.dynem 2>/dev/null
grep -aE '\[PARITY c2a\]' $MDIR/rsl.error.dynem | head -1

cp -f $MDIR/nl.bak namelist.input; echo "namelist restored"
echo "=== PASS if libtorch c2a mean/max ~= dyn_em c2a mean/max ==="
