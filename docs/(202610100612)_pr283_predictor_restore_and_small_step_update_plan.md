# PR282 후속: 예측값 복원과 작은 시간간격 갱신 계획

Local timestamp: 2026-10-10T06:12:11.013075+09:00.
Base main 4615c2e06551e743b27ab3ccd7925a82e6f23c30. Original dirty worktree remains untouched.

Cold state-only 16-step replay failed before replay VJP: step9 max endpoint difference4.547473508864641e-13. Failure source125b231ce7761f1ce75f08ec8ebb5c4cec2c61ce5dd63ae3d24512d9177420b9 and artifacts are preserved in /private/tmp/pr283-replay-smoke-run-20261010. This is a missing replay condition, not a new governing-equation defect.

Existing Newton CarriedState now clones the two persistent stage2/stage3 predictors on capture and restore, preserves undefined state, and includes their dtype/shape/bytes in the diagnostic digest. It remains a listed-state snapshot, not a complete solver/preconditioner replay framework. The supported fixture has precond0, GMRES/INN/Stage3 warm starts off, fixed timestep/no retune; EW adaptive linear tolerance remains enabled. No model equation, physical data, sign-smoothing parameter or tolerance was changed.

- [x] Fresh matching-header/core/caller build. Predictor ownership/digest contract22/22 passed.
- [x] Short final shared-helper replay, 16steps at0.3125 on Mac: all16 endpoints bitwise equal; full and six packed-block gradient differences0; physical-state checks17 and snapshot checks2. Source8e852669862f8ece581e1fccfef546aa993fef4e10e9f0dea10deb0a83624df7, executable78bc1290ceb25ee22c685fdf5f13dc7a959cb8f36c3a1c3ed87f74bda546f4a1. This is an internal replay proof; Mac base/PHB differs from the archived Linux context and does not qualify the fixed-context Linux inverse.
- [x] Fresh WRF em_b_wave regression with the new archive:48stages,174finitevariables,positivefullmass/thickness, three outputs byte-identical toPR282. Same-input archivedRK3 comparison is in (202610100556)_pr283_wrf_regression_fresh_archive.md; no newRK3run/equal-accuracy speed claim.
- [x] Independent Green/Red source review; actionlint/whitespace and offline fixture/metric checks pass. Exact Graphify mirror bindings checked; runtime/context extraction gaps retained.
- [ ] Hosted Linux SAME-executable16-step replay gate and exact original fixture context before any960-step inverse call.
- [ ] Same0.3125/N14/K12/EW1 gradient: primal one-stepFP64handoffs,961detachedstates/121predictorsnapshots,120reverse8-stepwindows with cotangents only at480/960 and strict every-endpoint replay parity.
- [ ] One/two Armijo accepted updates in Zsaved+Bcan delta, fixed105observations/R; at most4backtracks perupdate, stepnormcap0.05. Oldh5FDmatrix is a fixed SPD search metric only, never a new gradient/prior. Accept only strict true native cost decrease.
- [ ] Whole gradient errorEg, certified optimum and initial-state/unused-forecast recovery remain open until evidence supports them. No h5 small-gradient certificate is required before attempting these bounded updates.

The manual small-step-inverse workflow has120-minute cap only for this selected workload (all other diagnostic choices retain35minutes). It runs offline preflight, fresh short gate, then the same executable driver; fixture mismatches stop before960. No long run has been dispatched at this timestamp. No generic Hessian/checkpoint framework or deployment work was added.
