# Option-2 scalar metric diffusion: source parity before production wiring

Timestamp: 2026-09-25 11:09:10 JST (+0900)

## Context and change

This candidate starts from PR #227 head `ccd692d9b2ffc64ddb043288b6f759889bfa855d`. The production Step 9 still sends option-2 theta through the older option-1-like horizontal scalar helper with `mu_full_diff`. That helper has no PH geometry, terrain slopes, physical rho, signed dnw, or option-2 flux-divergence metric, and it cannot generally equal Fortran `horizontal_diffusion_s`. The earlier parked flat case showed that a manually supplied 3-D coefficient could match a special zero-terrain formula, but it did not test a production option-2 operator.

This change adds a **private, explicit-input** C++ helper for the Fortran scalar metric stencil. Its inputs include the same-stage q, physical Kh and rho, zx/zy and rdzw, signed dnw/dn, fnm/fnp, cf1–3, rdx/rdy/g, and mass/X-face/Y-face map factors. It preserves separate `avg(K)*avg(rho)` face coefficients, negative H1/H2 flux, top/bottom q interpolation, zero top/bottom vertical flux interpolation, both slope corrections to the horizontal gradient, both slope corrections to flux divergence, and the signed cellwise `g/(dnw*rdzw)` conversion. It accepts one complete periodic-X/symmetric-Y tile with exact staggered shapes and explicitly rejects unsupported boundaries/partial tiles. **Step 9 and the Fortran/C++ ABI are not changed in this candidate.**

## Validation

The existing scalar contract binary now exposes this private helper to `tools/test_horizontal_diffusion_scalar.py`, which extracts and compiles the actual Fortran `compute_diff_metrics`, `set_physical_bc3d`, and `horizontal_diffusion_s` bodies. Fortran halo values are prescribed by periodic X and symmetric Y; C++ receives independently constructed analytic geometry, not the Fortran output arrays. The comparison includes all 192 owned mass cells in each of four cases and both default REAL and REAL64. The existing `Scalar_Diffusion_Contract` CTest passes 1/1. Local build used the configured Apple Clang/libtorch stack; full WRF was not rebuilt.

| Case | FP32 maximum C++–Fortran difference | FP64 maximum difference | Signal or control |
|---|---:|---:|---|
| Flat Fourier | `2.98e-8` | `2.78e-17` | `0.275835` |
| Flat, hybrid density profile | `9.31e-10` | `4.34e-19` | `0.00312810` |
| X/Y terrain cancellation | `9.97e-8` | `1.63e-16` | near zero |
| Mixed X/Y terrain–Fourier | `1.94e-7` | `3.89e-16` | `0.193815` |

The precision budget, fixed before the final run, is `2048*epsilon*max(1,signal)`: `2.4414e-4` for FP32 and `4.5475e-13` for REAL64 in these cases. Removing **both** terrain slopes produces gaps `0.0040495` in the cancellation case and `0.018325` in the mixed case, greater than ten budgets. This control shows the fixture depends on terrain terms collectively; it does not separately ablate X versus Y or gradient versus divergence. In the mixed fixture both Y corrections are nonzero by construction. The final local log is `.validation/option2-scalar/fortran_cpp_parity.log`, SHA-256 `4beda23a82cdb066476118c2841fbe1c2d54ed2ebffbb9038eb756ede08ea453`. Green/Red review found no P1 formula blocker for the stated private-helper scope.

The focused Graphify corpus was verified against this worktree and refreshed after edits: 16 files, 705 nodes and 1,497 edges. Its extracted relationships aided source navigation; numerical claims come from the Fortran source and execution above. No `test/em_b_wave` model run or same-setup RK3 field/runtime comparison was performed for this helper-only change. The prior one-step sigma/unit-map comparison in PR #227 remains the available comparison and does not exercise this new helper.

## Remaining checklist

Option-2 production Step 9 still uses the legacy scalar helper. The active bridge does not yet supply authoritative `xkhh`, rho, current PH-derived zx/zy/rdzw, signed vertical metrics and bottom interpolation coefficients together with the required halo/ownership contract. Therefore this PR does **not** close L34 option-2 production parity, nonunit maps/spatially varying K/rho, MPI/partial-tile boundaries, full-step time accuracy, or the active observation-path adjoint. Next, connect the exact inputs at one declared RHS state and compare the production diffusion ON−OFF tendency with Fortran before enabling broader cases; do not substitute a fitted mass factor or stale base-state metrics.
