# Weakly nonlinear wave candidate: attempt 4 pending

Local timestamp: 2026-10-06T21:25:04+09:00.

Worktree `agent/weakly-nonlinear-wave-20261006` is frozen at base HEAD `04b298b9c9fd6cf71fb6996261922d47253e9a72`, tree `aec0d443b7d1d7d648060338daf8fcd3c540610a`. The exact source, executable, and library hashes are in [the attempt-4 binding receipt](evidence/20261006/weakly_nonlinear_wave/attempt4_source_binding_pending.json).

## Current state

The complete `Native_Stable_Wave_Refinement` and `Native_Quadratic_Wave_Forcing` run is attempt 4 at `/private/tmp/pr279-final-affected-ctest-attempt4.log`. It is in progress at this note’s timestamp. No attempt-4 result or CI result is asserted. Three earlier failed attempts and their logs remain archived under `weakly_nonlinear_wave/final_gate/attempt{1,2,3}_...`; they are diagnostic history, not validation passes.

The candidate includes the quarter-period/full-field negative control, Q0 and Q2 source forcing with phase-mixed mean U response, independent polarized (DQ_2) sensitivity versus selected native VJP cases, and fixed-control 300/600/900-second forecasts with EOS, mass, and theta budgets. The uniform +1 K mass-weighted theta calibration reports `1.0275228918e13 kg K`, equal to the domain-area × column-mass / gravity formula. The theta criterion is a budget and does not claim exact RK energy conservation.

The source omega envelope is about `0.06245`; the sign smoothing scale is `0.001`, a ratio near `62`. Residual degree remains empirical at the tested finite amplitudes and time domain; this does not establish a universal cubic law.

## Independent evidence already available

A fresh guard-fix `em_b_wave` run is archived at `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261006/weakly_nonlinear_wave/wrf_runs/sdirk3_guardfix_dt15_240/`. It uses the final production source hash `199874f3666604a57a177937784bb4fa0a13a31836e8224259f57101c166ae18`, an incremental C++ relink with cached Fortran objects, `dt=15 s`, `T=240 s`, and one MPI rank/thread. Its receipt reports 48/48 stages, finite output fields, positive dry column mass and W-face thickness, and byte-identical native outputs across the three guard-fix run attempts. The same-input archived RK3 endpoint comparison is included in the receipt; its timing comparison is descriptive.

The separate solver-tolerance cross-check on the selected controls is archived at `weakly_nonlinear_wave/solver_tightening/`. It compares 900-second `dt=10` runs at Newton tolerances `1e-12` and `1e-13` against the `dt=20`, `1e-12` run. Tolerance-induced endpoint changes are far smaller than the `dt=10` versus `dt=20` differences. This supports interpreting those timestep differences as time-discretization sensitivity rather than solver-tolerance drift; it does not substitute for the in-progress attempt-4 fixture gate.

The Q0/Q2 Graphify snapshot at `5de66e96f82e32cf39f448628a2f5dc0814405165e5746680e52d380a4a57535` predates the final production-guard edit and is marked stale. The original PR278 graph remains preserved. Refresh the scoped graph once after attempt 4 passes and bind it to the frozen source hashes.

PR278’s accepted dry-fixture coefficient/trajectory/VJP scope remains closed. No generic full-tile Hessian, global time/space order, or universal finite-amplitude residual-degree claim is made here.
