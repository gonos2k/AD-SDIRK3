# Matched prepared-operator pressure cost screen

Two source blocks each had one native attempt. The initial source11b71a6b
failed at input before any native JVP or M: selected Stage2 was incorrectly
excluded by the Stage4-oriented V-primary capture policy. Its original
INCONCLUSIVE result is in `initial_capture_failure/`; zero timings are unmeasured
placeholders, not zero-cost samples. Source d0bfa40b corrects only this metadata
capture, with unchanged equations, thresholds, input and cost policy, and a
separate registration. Neither source completed a Newton stage or timestep.

The corrected attempt completed P0/P1/P1/P0 against one common PreparedJvp A,
b,x0,S. All original-native residuals, 15 native oracle calls, one tape RHS,
27 tape checks and actual Arnoldi mixtures passed; bounded probe agreement
is not a proof for all Tile states or directions. The current profile has
reuse enabled before construction; it does not measure the native-FWAD
kernel's performance or combine historical speed percentages.

**The cost decision is NO_GO.** Both mirror pairs and both CPU/elapsed costs,
with and without measured guard/sketch subtraction, miss the registered2x
screen. `charged_total` models common+tape (fully charged to every candidate),
freshM setup, solve and native residual. It is not an end-to-end ABBA runtime
partition. Separate verification/boundary-guard times are excluded; subtraction
is not a direct uninstrumented timing and can still have cache effects.

`analysis/arms.csv`, `analysis/summary.json` and `analysis/matched_cost.png`
contain the data and conditional arithmetic ceiling. In a scratch copy,
`python3 analyze_matched.py .` recomputes them from the public run JSONs;
Matplotlib/NumPy are needed for plotting and PNG bytes may vary by version.
Do not overwrite sealed evidence. No forecast/RK3 cost, W-noise remedy,6–12-step
or five-day acceptance is claimed. RK3/archive initial fields only were checked.

Native scripts retain their executed names and hardcoded original roots:
`/private/tmp/matched-pressure-cost-20261102` and the two absolute evidence
roots recorded in registration. Reproduction needs the pinned prior Fortran,
SDK/compiler/MPI/netCDF/LibTorch stack, objects, source revisions, raw input,
and archive fixtures in the receipts. Executables, objects and NetCDF are
local; this folder alone is not a native restart/replay bundle. The clock
helper is authoritative in `tools/sdirk3_run_measurement.py` at the native
source revision; tests there and in `tests/` have separate recorded runs.

The interval clock policy records all six endpoint readings and resolution,
keeping wide brackets unresolved. This corrects future measurement ambiguity,
not the earlier #337 verdict. The stderr recount preserves that earlier
original `converged_stages: []`. Stage-count merging lacks timestep IDs, so
its documented ambiguity is not a long-forecast completion certificate.

Ordinary CI covers default-off code paths. Matched-macro evidence is the
focused tests, syntax/production build and these separately registered runs.
Large basis/arrays and global ps/lsof polling were not added.
