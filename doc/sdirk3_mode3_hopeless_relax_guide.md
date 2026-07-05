# SDIRK3 Mode3 Hopeless-Relax Guide

## Scope

This note summarizes mode3 (`imex_split_mode=3`) behavior around:

- Stage3 hopeless GMRES budget cap
- `sdirk3_hopeless_relax`
- stage gate retry mode (`sdirk3_stage_fail_action`)

Case: `test/em_b_wave`, `OMP_NUM_THREADS=1`, `mpirun -np 1`, `dt=600`, 1-day run.

## Key Finding

`sdirk3_hopeless_relax=.true.` bypasses Stage3 hopeless budget capping logic.
When bypassed, Stage3 keeps higher restart budget and spends more JVP/GMRES cost.

Code path:

- cap active path: `external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp:3956`
- bypass log path: `external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp:3972`

## Measured Results

Reference files:

- `run/step2_step3_compare_np1_omp1_1day_2026-02-22.txt`
- `run/step2_step3_step3cap2_compare_np1_omp1_1day_2026-02-22.txt`
- `run/step2_step3_step3cap2_relaxoff_compare_np1_omp1_1day_2026-02-22.txt`
- `run/step2_step3_cap2_cap3_relaxoff_compare_np1_omp1_1day_2026-02-22.txt`
- `run/step3_stagefail2_compare_np1_omp1_1day_2026-02-22.txt`

Summary (mean timestep seconds):

- Step2 baseline: `0.832039`
- Step3 old (`cap=3`, `relax=on`): `0.963733`
- Step3 new (`cap=2`, `relax=on`): `0.985594` (cap bypassed, no gain)
- Step3 (`cap=2`, `relax=off`): `0.733657` (best among tested)
- Step3 (`cap=3`, `relax=off`): `0.802699`

Interpretation:

- Turning `relax` off is the main enabler.
- With `relax=off`, `cap=2` is faster than `cap=3`.

## About Stage-Fail Retry (`stage_fail_action=2`)

Test result showed:

- Stage3 failed and retried as expected.
- Stage4 then entered catastrophic path with `NaN`, so overall run behavior worsened.

Recommendation for now:

- Keep `sdirk3_stage_fail_action = 0` for operational mode3.
- Use `stage_fail_action=2` only for controlled diagnostics.

## Operational Recommendation

For current code base:

1. Use `sdirk3_hopeless_relax = .false.` in mode3 performance runs.
2. Keep Stage3 hopeless cap at `2` (current code setting).
3. Keep `sdirk3_stage_fail_action = 0` unless debugging retry behavior.

## Remaining Risk

Even with the faster profile, Stage3 non-convergence events may still occur.
Current gain is mostly from reducing wasted GMRES/JVP budget, not from fully resolving Newton convergence pathology.
