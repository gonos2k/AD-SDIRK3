# PR279 weakly nonlinear wave closeout

Local timestamp: 2026-10-06T22:32:38+09:00.

Attempt-6 and attempt-7 sources are bound to the same worktree base `04b298b9c9fd6cf71fb6996261922d47253e9a72` and tree `aec0d443b7d1d7d648060338daf8fcd3c540610a`. Before/after receipts match for both attempts. The compact source/test/graph summary and per-attempt receipts are in [the evidence folder](evidence/20261006/weakly_nonlinear_wave/).

## Local test results

Attempt 6 passed the full affected pair: `Native_Stable_Wave_Refinement` in `51.92 s` and `Native_Quadratic_Wave_Forcing` in `359.06 s`, total `410.98 s`.

Attempt 7 made a runner scope-string wording correction and passed `Native_Quadratic_Wave_Forcing` alone in `359.31 s`. It did not rerun `Native_Stable_Wave_Refinement`; the evidence therefore records the full pair result from attempt 6 and the single quadratic retry from attempt 7 separately. Exact-head full GitHub CI remains pending.

Earlier failures remain archived separately. Attempt 4 passed the stable-wave test, then failed the long m=0 W coefficient check with `2.69027e-7 m/s` against a `2.02224e-7 m/s` budget. Attempt 5 passed the stable-wave test and completed the quadratic numerical work, then failed while serializing a NumPy boolean into JSON. Neither attempt counts as a full gate pass.

## Scientific scope and limits

Attempt 7 reports signed instantaneous coefficient scaled errors `5.7906e-7` on 8x6x4 and `3.1377e-7` on 16x12x8. Physical quarter-period full-field covariance and its negative control pass. The weakly nonlinear forward check carries Q0, Q1, Q2, phase-mixed mean U, and mass/mean state on the coarse 8x6x4 fixture to 300, 600, and 900 seconds, at `dt=2.5` and `5 s` for amplitudes `0.5` and `1.0`.

The signed Q0 W coefficients use m/s. For `dt=2.5 s`, their relative errors against the source signal are `4.75%`, `13.44%`, and `5.75%` at 300, 600, and 900 seconds. The full-state residual is resolved above time/FP64 floors at 300 and 600 seconds, but not at 900 seconds; no 900-second residual-degree estimate is reported. The 300/600-second degree estimates (`1.9787` and `1.9890`) are empirical for these controls, amplitudes, times, and norms. The report claims no temporal order or universal cubic law.

The independent source-polarized (DQ_2) prediction for the selected six-second m=2-W objective is `-1.4848316633e-8`; the native amplitude/timestep Richardson sensitivity is `-1.4848241738e-8`. Their `7.4895e-14` absolute difference is within the `5.4904e-13` accepted budget. The native VJP versus centered-FD relative error is `9.9541e-5`, within `2e-3`. This is a selected objective/control result, not a global Hessian bound.

The mass-weighted theta calibration matches domain-area × column-mass / gravity exactly for a uniform +1 K perturbation on both grids. The theta check is a mass-weighted budget, not exact RK energy conservation. The source omega envelope peaks near `0.062455 Pa/s` against sign-smoothing delta `0.001 Pa/s`; the finite amplitudes are outside a strict infinitesimal regime. Residual-degree interpretations remain empirical.

The separate 360-step, single coarse-grid memory sample measured `3,144,138,752` bytes peak RSS in `59.96 s`. It is one prototype measurement and does not establish a general memory bound. Its log and CSV are preserved in the external archive.

## WRF and Graphify evidence

A fresh guard-fix incremental-link `em_b_wave` run at `dt=15 s`, `T=240 s`, one MPI rank and one thread passed 48/48 stages with finite output variables and positive dry column mass and W-face thickness. It reused the cached Fortran object tree. The archived same-input RK3 endpoint RMS values are U `0.00210824 m/s`, V `0.000952211 m/s`, W `0.000598811 m/s`, PH `1.30420 m²/s²`, T `0.0414245 K`, and MU `0.0995476 Pa`. The recorded runtime comparison is descriptive, not a controlled performance result.

The final scoped raw AST graph is `1,073` nodes and `3,350` edges, SHA-256 `2dec45b4e846980e981f5a86793083bfec2a2ec04731cc77d9d9b61f9e20edd2`. Its 39 source files match attempt-7; CMake and CI YAML are hash-bound and manually inspected, and WRF Fortran links remain manual. Graph edges aid navigation and are not numerical proof. The earlier PR278 baseline graph is preserved.

All per-cell native reports and CSV/TXT payloads remain outside Git under `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261006/weakly_nonlinear_wave/`; the external manifest records `224,052,013` bytes of attempt-6/7 raw payloads. No generic full-tile Hessian, global time/space-order, universal cubic, or equal-accuracy forecast claim is made. PR278’s accepted fixture-scoped coefficient, trajectory, and VJP evidence remains closed.
