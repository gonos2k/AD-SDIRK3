# S1b weighted whole-step and accepted-stage budget review

Local timestamp: 2026-09-26 09:35 JST (+0900)

Reviewed PR #239 source revision `1c5c828470b858146ad6b8f8e539a1ee79dade46` in an isolated detached worktree. No solver or test source was changed. The candidate already contains the requested nonunit-area, nonuniform-hybrid whole-step budget and a bounded accepted-stage budget fixture; duplicating the weighted-budget block causes compile-time redeclarations and adds no coverage.

## Source and graph evidence

Graphify was rebuilt for this exact worktree (`external/libtorch_wrf/sdirk3/graphify-out`). Its extracted graph confirms the `unifiedStep()` to `computeUnifiedRHS()` path and connects the test trace capture API to the RHS implementation. The graph is navigation evidence; the equations were checked against source.

`dyn_em/module_initialize_ideal.F:876-884` defines hybrid-opt=2 `c3f` from `eta` and `etac`. At `:951-956`, WRF defines the mass-level coefficients as `c1h = Δc3f/Δznw` and `c2h = (1-c1h)(p1000mb-p_top)`. Thus the layer dry-mass factor is `-dnw*(c1h*(MUB+MU)+c2h)`, with `-dnw` the positive layer width. The test checks the vertical sum against the column dry mass at lines 349-356 and independently anchors the initial hybrid theta sum at lines 438-450.

## Existing coverage at this revision

In `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp`:

- Lines 215-300 exercise an actual tile step with spatially varying mass/U/V map factors. The physical cell area is `dx*dy/(msftx*msfty)`; both dry-mass and mass-weighted theta budgets use it. The unweighted-area negative control is required to exceed the physical tolerances by 10x. The run printed `mass_drift=0.00384521`, `mass_budget=14.9406`, `theta_drift=401.609`, `theta_budget=44650.1`; naive-area drifts were `37131.1` and `1.15701e+07`.
- Lines 302-485 use nonuniform eta layers and WRF hybrid coefficients on the same nonunit map, execute the actual tile step, and calculate physical dry mass and mass-weighted theta. A compensated theta transfer is a same-state negative control: physical weighted error was `0`, while the old uniform `/nz` diagnostic error was `1.39815e+09`. The step printed hybrid mass drift `0.00708008` against `14.9405` and theta drift `9710.45` against `944758`.
- Lines 490-795 capture accepted ARK states and RHS terms. The fixture requires zero external physics source, ordinary ARK mode, no vertical/Rayleigh diffusion, periodic X, and impermeable Y. It checks stage-1 producer face terms against the accepted slow tendency, verifies all later accepted slow-stage theta face divergences against their captured tendencies, and verifies all-stage physical dry-mass/theta rates plus diagnosed mass-flux boundary closure. It then checks bitwise ARK replay to `raw_final`, equality of `projected_final` with the production output, and the endpoint mass budget.

The direct test output reported zero mass-flux boundary maximum at every stage, stage-1 slow face error `9.9243e-05`, later slow-stage errors `1.04453e-04`, `1.02021e-04`, and `1.56742e-04`, and a mass discrete residual `0.0163716`. Each satisfied its in-test bound. This is bounded evidence for the constructed fixture, not a claim for other physics, maps, boundaries, decompositions, or production WRF runs.

## Precise remaining limitation

The test reports `theta_endpoint_minus_stage` but checks only that it is finite. Here it was `-9703.2`. That difference is expected to need separate treatment: the endpoint diagnostic is the nonlinear product `Q = M(theta+300)`, while the stage budget sums the chain-rule rate `M*theta_dot + c1h*(theta+300)*mu_dot`. An ARK weighted sum of those stage rates is not an exact discrete product rule for endpoint `Q`. A valid tighter endpoint assertion would need to derive and bound the ARK product cross-term from the captured `MU` and theta increments; comparing these two numbers directly to the near-zero conservation tolerance would assert a false discrete invariant. The present whole-step weighted theta tolerance is broad (`944758` versus observed drift `9710.45`), so broader quantitative acceptance remains open.

The result is a read-only finding: the checklist's S1b request for physical area, hybrid layer mass, an independent wrong-weight counterexample, accepted-stage flux/source evidence, and exact endpoint replay is already represented at this source revision for one closed fixture. Remaining S1b work should be framed as a derived endpoint product-rule/cross-term bound or broader physically consistent cases; no additional small same-state stage-face check is missing in this test.

## Validation and provenance

- Targeted standalone test: `ctest --test-dir build/sdirk3 -R '^Full_Tile_Step_Adjoint$' --output-on-failure` passed (1/1, 19.33 s).
- Direct `build/sdirk3/test_full_tile_step` passed and printed the budget and negative-control values above.
- A temporary duplicate insertion was rejected by compilation due to duplicate declarations and then fully reverted. The test source hash below matches PR HEAD; no implementation or test diff remains.
- No WRF `em_b_wave` model run, split-explicit RK3 field/runtime comparison, MPI decomposition run, or full WRF rebuild was performed.

SHA-256:

- `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp`: `7b83d0b2821232324720ebbf70d41079ab7e04eb54498b9aae1a6ca7c209a286`
- `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp`: `ddec376d3c398502571d8d43ea2d97c764e2e9eaea2d19b81c706c8b0389dba5`
- `dyn_em/module_initialize_ideal.F`: `7499596c6ab104eac5510236345bde3ff69e5a76b5579597bcf78549917a16c0`
