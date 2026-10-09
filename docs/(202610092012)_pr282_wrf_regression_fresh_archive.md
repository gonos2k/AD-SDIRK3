# PR282 fresh-archive WRF regression

Local timestamp: 2026-10-09 20:12:01 JST (+0900)

## Context

The PR282 change removes the unused `t_w` interpolation scratch tensor from `TileSDIRK3UnifiedSolver`. The original `/Users/yhlee/SDIRK3` checkout was left untouched. This run used the isolated worktree `/private/tmp/pr282-accuracy-20261009` at HEAD `0cf3b82383926f2d7ccee92dee71052ed5e22f5f`, with the two intended uncommitted source edits in `wrf_sdirk3_tile_unified.h` and `wrf_sdirk3_tile_unified_impl.cpp`.

## Fresh build and WRF run

The production C++ archive was freshly built against the matching changed header in `/private/tmp/pr282-production-memory-build-20261009`. The archive SHA-256 is `a12e8c68fd326a90e7d1ec23f6d5dabb78083054c214d6ef96258c976c7e6b49`. The build completed 27/27 targets; the corresponding standalone executable SHA-256 is `ac6b48aa88be11561084143fb69612700bbdc8bb0727860344b0fb0f06074862`.

WRF was relinked in `/private/tmp/pr282-final-wrf-validation-20261009` using the fresh archive and the previously qualified Fortran objects and libraries. This change does not alter the Fortran/C ABI. The WRF executable SHA-256 is `9f8aaf361ec658354c3f40d514a4143c1493ad0eb8a53ae47e8179590e8abd1b`. The exact one-rank run was `mpirun -np 1 ./wrf.exe` with `OMP_NUM_THREADS=1`; it exited 0 in 7.98 seconds and logged `SUCCESS COMPLETE WRF`.

The run reused the exact same setup as PR281: `namelist.input` SHA-256 `6258b997543f95b6cdde538aa37e14084f17862210e94c02dd23fb7fd91a8e06`, `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, and `diagnostics.txt` SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. This is the 240-second `em_b_wave` setup at 15-second timestep, yielding 16 main steps and 48 converged implicit stages (16 each at stages 2, 3, and 4).

## Results

All 174 floating-point output variables were finite in all three output files. The minimum perturbation mass, full dry-column mass, and W-face thickness stayed positive. The three output files are byte-identical to the prior PR281 SDIRK3 outputs, showing that the dead scratch removal did not change this forecast on the same build/input setup.

The archived split-explicit RK3 reference is `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20260929/SDIRK3-pr239-cleanbuild/.validation/pr239-cleanbuild/t1_240s_20260926/rk3_dt15`. Frame times match. At 240 seconds, SDIRK3 versus RK3 field RMS differences were MU 0.09955 Pa, P 0.06419 Pa, PH 1.30420, T 0.04142, U 0.002108, V 0.0009522, and W 0.0005988 in their WRF output units. These are same-setup descriptive field comparisons, not an equal-accuracy claim.

The sum of the 16 WRF `Timing for main` entries was 6.46191 seconds (max 0.48036 seconds). The archived RK3 sum is 0.26754 seconds. This single run is recorded for setup parity; no speedup claim is made.

## Evidence and next actions

`/private/tmp/pr282-final-wrf-validation-20261009/wrf_validation_summary.json` records per-frame hashes, field differences, finiteness, physical checks, stage counts, inputs, executable, and archive identity. `link_receipt.json`, `run_status.json`, `link.log`, `run.log`, `rsl.error.0000`, and `rsl.out.0000` preserve the commands and raw outputs. The PR282 standalone smoke also completed 30×10 Mac iterations with output byte-identical to its saved baseline; its maximum RSS was about 134.9 MiB versus about 577 MiB before the change.

No further WRF run is needed for this isolated removal. Green/Red final review should include this fresh-archive WRF evidence and the matching-header build/smoke receipts.
