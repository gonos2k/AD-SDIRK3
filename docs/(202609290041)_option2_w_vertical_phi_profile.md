# Option-2 W Vertical PH′ Profile Contract

Local timestamp: 2026-09-29 00:41:55 JST

## Context

This work is in a fresh worktree based on PR #261 HEAD `9516238afc3d85b64f0d29ab5f3767bb2207443e`. It adds one physical, single-tile PH′ case to the supported native option-2 scalar-K W actual-RHS fixture. The test keeps μ′=0, qv=0, t′=0.01, constant namelist K, map factor 1.25, periodic X, and symmetric Y. It retains the existing nonflat PHB terrain.

The manufactured W-level profile is `PH′(j,k,i) = g * 50 m * sin(2π k/NZ) * cos(2π i/NX)`, with zero-based k=0…NZ. It varies vertically and horizontally, vanishes at the bottom and top W levels, and keeps the vertical PH thickness perturbation within ±50 m of the existing 1000 m base layer.

Fortran nonhydrostatic `calc_p_rho_phi` computes `al′ = -(alb*c1h*mu′ + rdnw_signed*ΔPH′)/(c1h*mut+c2h)`; `calc_alt` forms `alt=alb+al′`, and dry `rho=1/alt`. With μ′=0, c1h=1, c2h=0, and signed `rdnw=-4`, this gives `al′=4*ΔPH′/mut`. C++ `calc_p_rho_wrf` flips positive `rdnw_abs=4` to the same signed value. The metric routines use total `PH′+PHB`; the density equation uses PH′ differences. No supplied K is used, so the native option-2 gate remains satisfied.

## Changes

Extended the existing W actual-RHS fixture and extracted Fortran oracle with this PH′ profile. The test emits and compares current-state rho, alt, dz, pointwise W mass, direct horizontal and vertical W helper tendencies before final conversion, and the public W ON−OFF rate. It requires positive alt/dz and a measurable rho/dz range. The earlier μ-only profile remains as a separate case.

Graphify was refreshed after edits from this worktree based on `9516238afc3d85b64f0d29ab5f3767bb2207443e`; its `built_at_commit` metadata records that base HEAD while the extraction includes the edited working tree. The generated graph remains a local artifact and is excluded from the change.

## Validation

Standalone build used AppleClang 21.0.0, LibTorch at `/opt/homebrew/lib/libtorch.dylib`, and GNU Fortran 15.2.0. Test executable SHA-256: `41d3e5b4642837370d6135bf4ba40781d623fece0da5321590d218a9bb358517`.

- `Option2_W_Actual_Rhs_Contract`: passed, including existing physical/packed scalar-K controls, μ-only density, and the new PH′ case.
- `Option2_Momentum_Geometry_Source_Parity`: passed.
- PH′ case: rho span `0.0534669161`, minimum alt `0.933593273`, dz span `100.000366 m`, and minimum dz `949.999756 m`.
- Direct current-state rho/alt/dz errors: `0` / `5.96046448e-8` / `3.05175781e-4`.
- Direct raw W H/V helper errors: `1.49011612e-8` / `3.7252903e-9`, against budgets `4.02e-7` / `8.11e-7`.
- Public final W rate error: `1.48270046e-7`, against budget `1.44e-6`.
- Recovered H/V differences from full ON−OFF subtraction were `0.0111190` / `0.0109203`; these are diagnostic only because common RHS background cancellation obscures raw component accuracy.
- `git diff --check`: passed.

Edited-file SHA-256 values:

- `tests/test_scalar_diffusion_contract.cpp`: `47bccffd6755cba21b5c84528f94e2a72d793915519b3105985289314464530c`
- `tests/test_option2_momentum_geometry.py`: `0300c171ce5cc1680f4350e024830b5135b40e9d4431a5dfdd63af096836c028`
- `tests/test_option2_w_actual_rhs.py`: `8953aa27f501a5e98fe2a0e7f70eeca5c15dd0d4f174ddc6b24cf440bcfda036`

Red reviewed the final source/fixture comparison and found no blocker. No WRF `test/em_b_wave` run or same-setup RK3 field/runtime comparison was performed.

## Next Actions

Run exact-head CI/PR review. Packed PH′ coverage is outside this fixture's scope.
