# Weakly nonlinear wave gate: retry in progress

Local timestamp: 2026-10-06T19:37:06+09:00.

Worktree: `/private/tmp/pr274-ci-20261004`, branch `agent/weakly-nonlinear-wave-20261006`, HEAD `04b298b9c9fd6cf71fb6996261922d47253e9a72`, tree `aec0d443b7d1d7d648060338daf8fcd3c540610a`.

## Previously accepted PR278 evidence: closed

The accepted PR278 fixture evidence remains closed: source-transcribed (Q_2) coefficient comparison for source-selected modes packed into native fields; the declared signed trajectories and native VJP check; the exact 128-test CI inventory with 127 passing, one MPS skip, and zero failures. Those results remain scoped to their dry fixture. They do not establish a general full-tile Hessian or time/space convergence order.

## New weakly nonlinear review items

- [x] The candidate runner now uses a physical quarter-period roll, compares full native fields, and includes a fixed-x negative control.
- [x] The source reference includes Q0 EOS and transport channels with phase-mixed U mean response, alongside Q2. The weak trajectory carries q0, q1, q2, and mass/mean state terms.
- [x] The source reference provides a polarized (DQ_2) sensitivity; the runner compares it with native VJP results over the selected amplitude and timestep cases.
- [x] The fixed-control candidate extends the source/native trajectory checks to 300, 600, and 900 seconds with EOS positivity and mass-weighted theta budgets.
- [ ] Obtain clean exact-source results for the complete weakly nonlinear gate. Final numerical metrics are pending.

The amplitude envelope is about `0.06245` while the sign-smoothing scale is `0.001`, a ratio near `62`. The finite-amplitude upwind cases therefore do not establish a strict infinitesimal regime. Residual-degree results should remain empirical for the tested amplitudes, times, and norms. The theta check is a mass-weighted budget and does not claim exact RK energy conservation.

## First gate attempt and retry

The first affected CTest run passed `Native_Stable_Wave_Refinement` in `56.62 s`. `Native_Quadratic_Wave_Forcing` failed in the new theta-budget helper when it indexed scalar `eta_delta` as an array. This is a runner input-shape error; it produced no valid numerical result for the new gate. The runner owner is changing the eta broadcast, with equations and tolerances held fixed. The retry uses a fresh output directory and runs only the quadratic CTest.

The source-before-run receipt at `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261006/weakly_nonlinear_wave/final_gate/source_before_run.json` binds the retry executable and sources. Its runner snapshot hash is `280eb2fc1ed60b5d011e61c093c5262b793c458708a00d9cb564b1a860236847`. The earlier post-Q0 graph snapshot is preserved, but its runner copy is from before this fix; see `weakly_nonlinear_graph_prefx_receipt.json`. Refresh Graphify for the corrected runner after the gate passes.

No builds, tests, or model runs were performed while preparing this status note. No final PR279 metrics or new CI result are asserted here.
