# V nonuniform actual-RHS and interpolation-order gate

Local timestamp: 2026-09-28 00:04 JST

## Context

This is a test-only extension isolated from PR #249 at HEAD `14bf727b94da8db14b736e53e14103ca0d6cee5c`. It checks the V-face fixed-mass path with nonuniform positive dry mass and EOS density, then separately checks option-2 vertical stress with independently varying positive diffusivity. No production source or solver configuration changed.

## Fixture and checks

The public native actual-RHS fixture now has a positive y-varying `MU` perturbation and matching geopotential perturbation. The extracted Fortran oracle derives `rho` from the same EOS relation. The fixture uses non-equal `fnm/fnp`, packed and physical layouts, and `Kh=0` so the actual-RHS comparison isolates vertical stress. It checks owned V cells, the varying V-face mass and layer coefficient, EOS density, ON/OFF source closure, same-solver ordering, and a one-sided mass mutant. Expected mass arithmetic reproduces the C++ float32 trigonometric input, storage, interpolation, and layer-coefficient operations; the mass/L checks use a predeclared three-epsilon-float relative representation bound.

A separate direct helper/source call uses `K=2-6(rho-1)` at mass levels, positive over the full fixture and anticorrelated with the nonuniform EOS density. It compares the C++ V vertical-stress helper against extracted Fortran `vertical_diffusion_v_2` with the same non-equal `fnm/fnp`. A product-before-interpolation mutant, `I(rho*K)` in place of `I(rho)I(K)`, is required to differ by more than ten times the raw-source comparison budget. This mutant is a metamorphic call to the same C++ helper with `rho'=rho*K, K'=1`, so its separation is not an independent implementation; the correct variable-K output is independently checked against compiled Fortran. The variable-K path runs after the native actual-RHS output and does not change its scalar native K configuration.

## Validation

Built target `test_scalar_diffusion_contract`, then ran CTest `Option2_V_Actual_Rhs_Contract`; result: 1/1 passed. Exact log: `.validation/v-nonuniform-p2/targeted_ctest.log`, SHA-256 `5bcbea56f65cd1c522bb91f5bc722bccb04f60a0cbe217acfc668c3a9aacb21c`.

For physical / packed nonuniform fixtures, respectively:

- Actual raw vertical-stress source error: `2.2361335e-8` / `1.4896981e-8`; ON/OFF source closure error: `7.60872225e-11` / `4.08803177e-11`.
- EOS rho error: `1.19314087e-7` / `1.19693909e-7`; rho range width: `0.152696371` / `0.159581423`.
- Direct variable-K operator error: `4.47329483e-8` / `4.46909058e-8`, against budgets `3.92e-7` / `3.82e-7`.
- `I(rho*K)` mutant separation: `7.046908e-4` / `1.1657923e-3`, over 1,700× / 3,000× the respective budget.
- Wrong one-sided mass mutant separation: `4.9914174e-8` / `6.10660221e-8`, over ten times the ON/OFF floor.

The older probe with horizontal diffusion enabled had a raw-H discrepancy associated with D12/D22: at physical cell `(j,k,i)=(5,2,0)`, D12/D22 differences were `+9.64891114e-7` / `-9.62306672e-7`; packed `(4,2,0)` differences were `+1.24711186e-6` / `-1.24277415e-6`. Those horizontal terms are excluded from this vertical-only actual-RHS fixture (`Kh=0`); this test makes no new claim about the y-varying H branch. The retained historical component receipt is `.validation/v-nonuniform-p2/component-summary.txt`.

No full CTest suite or new WRF/RK3 run was performed for this test-only extension. The production implementation SHA remains `a34900fc4377edce9daa310a90afb638a5abf529ce005d7cd399de3d006550dd`, unchanged from PR #249. The preceding V fixed-mass validation ran 60-second PC2, RK3, and K0 cases; the candidate outputs were byte-identical to #246/#247 controls. Its receipt is `.validation/v-step10/wrf_candidate_final/receipt_202609272244.md` in the prior validation worktree (receipt SHA-256 `cb4ef23cf82e4284deed3637486e41f5039419a81c7ea8f0f7740064e5a878b8`). This inherited evidence does not exercise the new test fixture and adds no forecast or time-order evidence for this test-only extension.

## Provenance

The compiled test executable SHA-256 is `bd7f9d5b8a9b4b6d441f918cceb4b54e12789650f51ff25b8ec60c58607afb15`. Relevant source SHA-256 values:

- `test_scalar_diffusion_contract.cpp`: `7f6f371fc86d8c93932dac7c5e44f14306fc37c0cccc5f5fb0787751624e0170`
- `test_option2_momentum_geometry.py`: `e6bbbb982b798b54cc8385732aeef89bdc0c98214fb9b0ba3c9b2a40a9b12db0`
- `test_option2_v_actual_rhs.py`: `30290c591b2f052a552ea8de736fe241745dea89b629fa5ebfe19566b12421df`
- Extracted physical / packed Fortran source: `0c4fd682f20628dd9286257c3a8078a0eaede526045cf8491155271389a163ae` / `30572a86c92c678f321ecbd54cb8b7fbad141fb6d2d3044dcff7113096a9dd7a`.

Focused Graphify was refreshed on the mirror containing the changed test sources. The baseline mirror was rebuilt from HEAD `14bf727`; pre/post `graph.json` SHA-256 values are `1ed845716f9436dc2e13c492c0c10270ef5f1a3f140939247a0a5e89b16f9c97` / `e034d0ff5e4497dc59a9333606003111b1f93186ea20c64b311f1841da59fe61`. These graphs are navigation aids, not numerical evidence.
