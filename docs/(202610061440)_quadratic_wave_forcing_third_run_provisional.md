# Quadratic wave forcing third run (provisional)

Local timestamp: 2026-10-06T14:40:00+09:00.

Status: provisional as of the third native CTest run. The root agent is still making small metadata, assertion, and help-text edits and plans one final CTest run. Refresh source hashes and receipts after that freeze before treating this report as final.

## Context and scope

This work checks whether source-transcribed quadratic interactions of two selected WRF-native m=1 modes reproduce the native SDIRK3 m=2 response. It uses the merged-main base `0ae8ae2d6cc700b1c70428cf1fccecdbd2c2ac59` in `/private/tmp/pr274-ci-20261004`, on branch `agent/quadratic-wave-forcing-20261006`. The scientific fixtures cover the full quadratic m=2 terms in the dry EOS/pressure-gradient, nonhydrostatic pressure, advection/mass flux, geopotential, and active curvature equations. The same common eta profiles are used on the 8x6x4 and 16x12x8 fixtures, with piecewise-linear C0 vertical remapping. That isolates this equation comparison; it does not establish second-order spatial convergence.

The independent Python reference is in `tools/wave_quadratic_reference.py` and `tools/wave_quadratic_transport.py`. The native producer is the C++ test added in `external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp`; the production solver sources remain at the PR277-identical revision. Native responses are compared with a source-transcribed reference, while the native AD path supplies forward/VJP responses only. No Hessian from the producer is used as independent truth.

## Third-run evidence

At the time of this report, `Native_Quadratic_Wave_Forcing` passed CTest in 184.10 seconds, with zero failed tests. The captured log is `/private/tmp/pr278-quadratic-third-ctest.log`. The native report and machine-readable inputs receipt are in `/private/tmp/stable-wave-build-20261004/quadratic_wave_forcing/wave_quadratic_forcing_report.json` and `wave_quadratic_inputs_receipt.json`; raw probe and trajectory payloads are beside them. The report records native executable SHA-256 `7b4e5ee04281730337902c86bb3fb7942df27651d5d58ac767328057cf313ce1`, test-source SHA-256 `54f4fd0b8e5a7e39e0646dc100ad2207d52557d3490d76b90bbcc10ec1f3541e`, and repository base above. These identities are provisional pending the final root run.

Across both native grids, signed instantaneous coefficient checks passed: reported scaled error is `5.7906e-7` on 8x6x4 and `3.1377e-7` on 16x12x8; half-shift parity error is zero. Component checks compare U/W, theta, mu, and pressure against an explicit pressure-roundoff floor. The small W response is roundoff-sensitive, so the absolute/scaled budgets and EOS pressure floor are retained in the machine report rather than interpreting a large relative error against near-zero entries.

The full-amplitude and half-amplitude signed trajectories were integrated to 30 seconds at 1.0- and 0.5-second steps on both grids. At 30 seconds, the reported temporal scatter norms are `7.31e-10` (coarse) and `1.04e-8` (fine); amplitude scatter is `7.21e-11` and `8.75e-11`. These values compare the declared common-eta fixture and Richardson temporal pairs, not grid-convergence order. The six-step, six-second m=2-W objective pullback has relative directional error `9.95e-5` against centered finite differences, below its `2e-3` budget; epsilon scatter is `2.30e-4`.

The machine report is the authoritative record for all signed channels, cross and self interactions, alias handling, per-component floors, exact control state, and every emitted trajectory. Its `scope` field describes the current pass as `source-transcribed fixture-scope complete Q2; AD only supplies native forward/VJP responses`.

## Integration and validation boundaries

No production solver source or ABI change is present in the current diff; this is test/reference/documentation work. No new WRF `test/em_b_wave` forecast or same-setup RK3 comparison was run. Existing qualified PR277 evidence is reused only for the unchanged production solver: 15-second, 240-second em_b_wave with 48 positive finite stages and the archived same-input RK3 field/runtime record. That comparison is not controlled performance evidence. Historical option-1 NaN VJP, periodic damping seam/general coefficient parity, moist physics, and MPI behavior remain outside this fixture's scope.

The required finalization remains open: wait for the frozen final CTest artifact; regenerate the source/executable/input receipts from that exact revision; retain only compact receipt/report artifacts under `docs/evidence/20261006/quadratic_wave_forcing/` rather than committing large native payload dumps; refresh the scoped external Graphify cache and record extraction gaps; then obtain the independent Green and Red review. Do not claim full CI completion from the registered test count alone. The Linux run currently has 128 expected CTests, with 127 passing and the one expected MPI skip after the full suite.

## Next actions

1. After the root's final run, compare final report and receipts to these provisional numbers and replace them with exact frozen identities/results.
2. Archive a compact machine summary, source/input/executable hashes, and CTest log receipt under `docs/evidence/20261006/quadratic_wave_forcing/`; leave large payload CSVs in the external scratch artifact directory.
3. Update the external graph only after verifying its base revision and source manifest against the frozen worktree. Preserve the previous graph before any clean AST extraction; document the known CMake AST and Fortran extraction gaps and manually cite the affected callers.
4. Record Green/Red review and final CI state before closing the PR work.
