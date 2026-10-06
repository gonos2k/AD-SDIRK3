# Quadratic wave forcing: final local evidence

Local timestamp: 2026-10-06T14:56:17+09:00.

Status: the frozen native source/reference gate and both affected local CTests pass. Exact-head full GitHub CI and final Green/Red closure remain pending. Worktree `/private/tmp/pr274-ci-20261004` is based on `0ae8ae2d6cc700b1c70428cf1fccecdbd2c2ac59`.

## Result

The added source-transcribed reference checks the complete fixture-scope quadratic coefficient (Q_2) from two selected native m=1 modes, including EOS/PGF, the native nonhydrostatic pressure term, transport and mass flux, geopotential terms, and active curvature. The native AD path supplies forward and VJP responses; it does not produce the independent reference Hessian. The comparison keeps signed complex m=2 channels, self/cross interactions, mean and m=1 leakage, owned cells, V aliases, and the EOS pressure roundoff floor distinct.

On the 8x6x4 and 16x12x8 native columns, the complete signed instantaneous coefficient comparison reports scaled errors `5.7906e-7` and `3.1377e-7`; half-domain-shift parity error is zero. U relative coefficient errors are `7.65e-7` and `3.30e-7`. W relative errors are `7.89e-6` and `5.66e-6` on small, roundoff-sensitive signals; both absolute errors pass the per-component budgets with the EOS pressure floor explicitly included. The expected-zero V m=2 response is checked at every probed level and saved trajectory checkpoint.

The source-transcribed forcing was propagated through the fixture's (L(m=2)) operator for 30 seconds at `dt=1.0` and `dt=0.5`, with signed full and half amplitudes on both grids. At 30 seconds, temporal scatter is `7.31e-10` coarse and `1.04e-8` fine; source-integrator tolerance tightening differs from native scatter by `3.73e-7` and `4.72e-8`, both below the `1e-6` gate. The two grids share eta profiles with piecewise-linear C0 remapping, so these results do not establish second-order spatial convergence.

For the six-step, six-second m=2-W objective, the native current-state pullback differs from centered finite differences by `9.95e-5` relative against the `2e-3` budget; epsilon scatter is `2.30e-4`. This validates native VJP/finite-difference consistency for the declared objective and trajectory.

## Exact local validation and identity

`Native_Stable_Wave_Refinement` passed in `55.79 s`, and `Native_Quadratic_Wave_Forcing` passed in `161.45 s`; the two-test CTest run took `217.25 s` with zero failures. The compiler was Apple LLVM 21.0.0 and native libtorch was 2.10.0. Exact source, input, executable, CTest, and raw-artifact hashes are in [the final receipts](evidence/20261006/quadratic_wave_forcing/).

The production solver source is unchanged at SHA-256 `b997115c8991c26d06f455ea0c01eb01dfb0258e4e511a7912589782bf9e8144`. The native test SHA-256 is `aa5246f4fbcd92169bd79b31e7d76246df9a9be29ee55eab0b23f19e34c65e2d`; the executable SHA-256 is `59ec738c82ded5c8b1faedb427f2397c57a6fb954ae9423e61a8e5a97dc918fb`. The full per-cell report and approximately 84 MB of native artifacts remain in the external archive; the repository contains compact receipts and their hashes, not the raw dumps.

No new `test/em_b_wave` or same-setup RK3 model run was made. Because the production solver source is unchanged, the qualified PR277 `dt=15 s`, `T=240 s` em_b_wave run and same-input archived RK3 endpoint comparison are reused: 48/48 positive finite stages; endpoint RMS U `0.00210824 m/s`, V `0.000952211 m/s`, W `0.000598811 m/s`, PH `1.3042 m²/s²`, T `0.0414245 K`, and MU `0.0995476 Pa`. Logged runtimes `7.73706 s` and `0.26754 s` came from different build recipes and are descriptive, not a controlled performance comparison.

## Graph and remaining closeout

The external scoped Graphify cache was refreshed after preserving the prior graph. The new code graph has 1,016 nodes and 3,155 edges, with all 39 extracted source files plus CMake and CI configuration hashes bound to the frozen worktree. The source-transcribed reference-to-transport call path was checked in Graphify and against source. CMake relationships are hash-bound and manually reviewed because Graphify does not extract that syntax; WRF Fortran call sites are cited manually because they were not extracted. Graph relationships remain navigation aids, not numerical proof. See `graph_baseline_receipt.json` and `graph_refresh_receipt.json` in the evidence folder.

Exact-head full GitHub CI remains pending. The expected inventory is 128 CTests, with 127 runnable tests and one MPS-only skip on Linux; no full-CI result is claimed here. Final Green/Red closeout is also pending. Historical option-1 NaN VJP, damping-seam/general coefficient parity, moist physics, and MPI behavior remain outside this fixture's scope.
