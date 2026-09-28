# Option-2 W Actual RHS with Variable Dry Mass

Local timestamp: 2026-09-29 00:21:01 JST

## Context

This work is in a fresh worktree based on PR #260 HEAD `a64d69026641bccc4efd18fc96a4947f8332c2d9`. The C++ native option-2 path accepts current-state dry density from its WRF alpha/EOS calculation while retaining scalar namelist K. The fixture varies horizontal perturbation dry mass, keeps `ph' = 0`, holds `t' = 0.01`, and uses the existing nonflat base geopotential, dry `qv=0` setup, and map factor 1.25. This isolates the density and W mass changes while preserving the current terrain geometry.

Fortran `calc_p_rho_phi` computes `al = -(alb*c1h*mu + rdnw*Δph)/(c1h*muts+c2h)` for the nonhydrostatic hypsometric option, and `calc_alt` forms `alt=al+alb`; dry density is `rho=1/alt`. C++ `calc_p_rho_wrf` uses the same alpha relation with positive `rdnw_abs` converted to WRF's signed negative `rdnw`. The fixture uses the same frozen `mu`, `ph`, base state, and constant K in the Fortran oracle and C++ actual RHS.

Only the μ term in the alpha equation is varied here (`ph' = 0`); the `rdnw*Δph'` contribution is not exercised. The theta perturbation remains at `0.01` to match the existing native gate fixture. The nonflat base geopotential still drives the terrain metric path.

## Changes

Extended the existing physical W actual-RHS fixture with a spatially varying μ profile. The extracted Fortran oracle derives rho from the source `al/alt` equation and reports pointwise `mut`; it does not prescribe rho independently. C++ emits pointwise rho, W mass, direct H/V helper tendencies before final conversion, and public ON−OFF total/H/V rates.

The test first compares C++/Fortran rho and direct horizontal/vertical W helper outputs on the same state. It then checks the public W rate against `(HW_RAW+VW_RAW)/mut`. The recovered H/V terms from subtracting full ON/OFF RHS values are reported as a diagnostic because the shared RHS background creates larger subtraction noise.

Graphify was refreshed before and after edits, most recently after the final source edit. Its `built_at_commit` metadata records base commit `a64d6902`; the extraction reflects the edited working tree at refresh time, including the uncommitted test changes. The generated graph remains a local artifact and is excluded from the changes.

## Validation

Standalone build used AppleClang 21.0.0, LibTorch at `/opt/homebrew/lib/libtorch.dylib`, and GNU Fortran 15.2.0. Test executable SHA-256: `17c0e2a3bca6a9bd5e3a6f65f171e7e957981ba4bbbf0e24e915c3b72756d816`.

- `Option2_W_Actual_Rhs_Contract`: passed, including the existing constant-mass physical/packed controls and the new physical variable-μ case.
- `Option2_Momentum_Geometry_Source_Parity`: passed.
- Direct current-state density comparison: maximum error `1.1920929e-7`, budget `4.22e-6`.
- Direct raw W H/V helper comparison: maximum errors `2.42143869e-8` / `7.4505806e-9`, budgets `3.36e-7` / `8.75e-7`.
- Public final W rate comparison: maximum error `8.4017945e-9`, budget `5.75e-8`.
- Pointwise Fortran/C++ W mass maximum difference `1.01863407e-10`; predeclared two-FP32-ULP budget `0.015625` at this mass range.
- Recovered H/V errors from full ON−OFF subtraction were `6.64161984e-4` / `6.16257661e-4`; these are diagnostic only and are not used as the raw parity criterion.
- `git diff --check`: passed.

Edited-file SHA-256 values:

- `tests/test_scalar_diffusion_contract.cpp`: `804d0645d5ce47bd2ba1393b188382bc008345f9db8bd43037c96a02e79ef851`
- `tests/test_option2_momentum_geometry.py`: `ab750a29ce26b37a4409e20f94fa165cf2fc0f232928744be2b98ed340441396`
- `tests/test_option2_w_actual_rhs.py`: `32797a261c8ed70ac258bf8f7984ef25ba8347511db6fcce20386ac301470839`

No WRF `test/em_b_wave` run or same-setup RK3 field/runtime comparison was performed.

## Next Actions

Red reviewed the final variable-μ source/oracle comparison and found no blocker. Keep the claim limited to this dry, physical, single-rank native option-2 fixture until packed variable-μ coverage is added.
