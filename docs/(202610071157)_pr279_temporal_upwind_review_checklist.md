# PR279 temporal-slice and finite-upwind review checklist

Local timestamp: 2026-10-07T11:57:03+09:00.

Correction recorded 2026-10-07T13:26:12+09:00: the h=1.25, T=300 check is three new native forwards (baseline plus amplitudes 1.0 and 0.5), not a pre-existing data set. It checks the same controls and initial states against reused dt=5/dt=2.5 and 900-second data. Use the newer `(202610071412)_pr280_temporal_upwind_status.md` for final focused outcomes and source-hash caveats.

Baseline: `/private/tmp/pr274-ci-20261004`, HEAD `0b2d8974b37a0f700f4694d10f2cb5f1905bdca0`, tree `eac5b99eb5e412da99f324a8f682632926281ec2`. The final scoped graph hash `2cefaac391266f472b99e06c303ec963663ad0d99c1e7a7dfb35d3c51148b744` binds 39 AST source files and three manually hashed configuration files; see [the final graph receipt](evidence/20261007/pr280_temporal_upwind_graph_refresh_receipt.json). No production solver implementation, WRF executable, or model input changed in this review diff.

## Accepted PR279 evidence: closed

- [x] Fixture-scoped Q0/Q2 coefficient and weakly nonlinear checks, including mean/column-mass state and physical-domain budgets.
- [x] Selected six-second polarized DQ2 sensitivity versus the native VJP and finite differences.
- [x] Attempt 6 passed the full two-test affected pair; attempt 7 passed the quadratic test after a scope-string-only correction. The separate reports and receipts remain linked from the closeout record.
- [x] 900-second full residual remains unresolved above temporal/FP64 scatter. Degree observations remain empirical; there is no universal cubic or global time/space-order claim.

## New review follow-up

- [x] **P2 packed temporal slice:** Native packed storage order is U/V/W/PH/THETA/MU. Modal/source harmonics are U/W/PH/THETA/MU; V is separate/raw and zero in this fixture. The focused report compares matched checkpoints with that explicit packed layout.
- [x] **h=1.25 check:** Three new native forward outputs at h=1.25, T=300 (zero baseline plus amplitudes 1.0 and 0.5) use the same initial-state hashes as the reused h=5/h=2.5 pair. Reported Q0-W source-relative errors are 22.4% (h=5), 4.7548% (h=2.5), and 0.6693% (h=1.25). This supports a bounded fixture comparison, not a general order claim.
- [x] **Isolate finite vertical upwind:** The native antisymmetric response flips U/V/W while holding PH/THETA/MU fixed; V is zero in this fixture. At T=300, all five U/W/PH/THETA/MU corrections improve residuals at amplitudes 0.5 and 1.0; direct odd U/W source/native max error is `2.38e-10`. At amplitude 1, corrected U/W/PH/MU residuals remain below successive time scatter; corrected THETA residual `2.166e-9` exceeds THETA scatter `1.385e-11`.
- [x] **Selected m3 finite-upwind-dominance diagnostic:** The final offline replay accepts the `(0.08,0.04)` epsilon pair: native FD scatter `7.7864e-4`, native AD/FD mismatch `0.0002468`, and selected source/full-map directional gap `0.0008477`. The [replay receipt](evidence/20261007/pr280_m3_offline_replay_receipt.json) binds current source and eight reused native payloads; the [five-width summary](evidence/20261007/pr280_m3_epsilon_sweep_summary.json) preserves the noisy/failing widths.
- [ ] **Complete cubic closure:** Remains an unclaimed scope limitation, not a blocker. The selected-direction diagnostic does not bound omitted m3 contributions or establish a general Hessian.

The non-adjoint focused report `/private/tmp/pr280-temporal-upwind-focused-final/wave_quadratic_forcing_report.json` records runner SHA-256 `9758c72be78df90c007b6ed6ed3c605ab80a41a77cb187fbe706a730b1b0a0f9` (including the parser fix). The latest source-bound runner SHA-256 is `a52896ad6704d5ea1d959d0b541e3bac4dde6563d150d6cb504bc78b70af0f34`, after only the m3 epsilon tuple and shared-receipt wiring changed; the numerical T300 functions were unchanged. The final scoped graph is hash-bound in [the Graphify receipt](evidence/20261007/pr280_temporal_upwind_graph_refresh_receipt.json).

Keep units explicit: U/W state and correction values are m/s; instantaneous RHS tendencies are m/s²; canonical Omega is Pa/s. The ratios 6.105/4.858/5.609/6.785/8.457 are successive time-scatter ratios for U/W/PH/THETA/MU (V is null), not signal/scatter ratios. Report only the signal/time range supported by the paired source/native artifacts. The Omega/smoothing ratio is a finite-amplitude regime diagnostic, not an infinitesimal or global cubic guarantee.

## Source navigation

Graphify confirms `run_long_weak_prediction() -> project_harmonic_state()` and the call path from `computeUnifiedRHS()` to `wrf_vert_adv3()` / `wrf_vert_adv3_w()`. Direct source inspection locates the long runner and component slices in [test_wave_quadratic_forcing.py](/private/tmp/pr274-ci-20261004/tools/test_wave_quadratic_forcing.py:1134), the source U/W vertical terms in [wave_quadratic_transport.py](/private/tmp/pr274-ci-20261004/tools/wave_quadratic_transport.py:88) and [the W kernel](/private/tmp/pr274-ci-20261004/tools/wave_quadratic_transport.py:109), and the native finite vertical kernels in [wrf_sdirk3_tile_unified_impl.cpp](/private/tmp/pr274-ci-20261004/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:627) and [its W counterpart](/private/tmp/pr274-ci-20261004/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:665). Their native U/W call sites are at [U](/private/tmp/pr274-ci-20261004/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:16717) and [W](/private/tmp/pr274-ci-20261004/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:17317). The CTest registration is [Native_Quadratic_Wave_Forcing](/private/tmp/pr274-ci-20261004/external/libtorch_wrf/sdirk3/CMakeLists.txt:621).

The final scoped graph refresh is complete for the frozen source. No further Graphify cycle, test run, or WRF run is part of this documentation closeout. Reuse the separately qualified PR279 WRF/RK3 evidence because production source and model inputs did not change.
