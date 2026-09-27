# S1b ARK product identity findings

Local timestamp: 2026-09-26 09:49 JST (+0900)

Follow-up to `(202609260935)_S1b_weighted_budget_review.md` on isolated branch `agent/s1b_budget_audit`, based at `1c5c828` plus the documentation-only audit commit. The test experiment was reverted; no test or solver source change remains.

An independent pointwise expansion was evaluated from `trace.input`, `trace.stage_state`, accepted fast/slow rates, and `trace.raw_final`, using the existing physical cell/layer weights. It reproduced the direct endpoint-product-minus-stage-chain-rule diagnostic:

- Measured and expanded `D`: `-9703.17`.
- Independent algebra residual: `5.09135e-09`.
- FP64 arithmetic/reduction bound: `11.8567`.
- Pointwise measured D versus the existing globally reduced diagnostic: `0.0279883`.
- Raw endpoint bilinear contribution: `-24.5203`; omitting it changed the residual by only `24.5203` (`2.07×` the bound).
- Accepted-stage bilinear contribution: `-24.5624`; omitting it changed the residual by only `24.5624` (`2.07×` the bound).
- The two bilinear terms nearly cancel. Their combined contribution is about `0.0421`; the remaining `-9703.21` is in the linear `(δR−δA)` terms when accepted FP32 rates are independently integrated in FP64 and compared with the FP32 raw ARK endpoint.

Neither proposed bilinear omission mutant clears the requested `100×` precision-bound discrimination threshold. The candidate assertion was therefore reverted; the bound was not enlarged. The direct and expanded identities reconcile, but the proposed negative control is not discriminating for this fixture because the measured D is dominated by the linear endpoint-versus-rate accumulation difference. No product-cross-term assertion is claimed. A useful future check needs an independently derived bound for the production FP32 ARK accumulation or a fixture with a larger uncancelled bilinear signal.

The targeted test run with the experimental assertion failed only at that new assertion. The pre-experiment baseline `Full_Tile_Step_Adjoint` had passed 1/1; no test was rerun after reverting the source, as requested. No WRF `em_b_wave` run, RK3 comparison, full build, or MPI run was performed.

Source hashes at the unchanged PR #239 code revision:

- `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp`: `7b83d0b2821232324720ebbf70d41079ab7e04eb54498b9aae1a6ca7c209a286`
- `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp`: `ddec376d3c398502571d8d43ea2d97c764e2e9eaea2d19b81c706c8b0389dba5`
- `dyn_em/module_initialize_ideal.F`: `7499596c6ab104eac5510236345bde3ff69e5a76b5579597bcf78549917a16c0`
