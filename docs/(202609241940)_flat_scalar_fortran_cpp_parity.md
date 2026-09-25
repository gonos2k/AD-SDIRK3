# Flat periodic scalar diffusion: direct Fortran–C++ comparison

Timestamp: 2026-09-24 19:40:00 JST (+0900)

## Context and change

This test-only candidate starts from PR #220 head `735f585c1cf30674002b05255229c1b28c7a4b32`. That PR repairs the C++ periodic-X scalar-diffusion seam, but its own Fourier and stage-face tests do not run the original Fortran operator on the **same** state. The F1-flat checklist item therefore remained open.

The existing source-extracted `tools/test_horizontal_diffusion_scalar.py` now includes one flat, Y-invariant periodic-X case. It still compiles and runs the actual `compute_diff_metrics`, `set_physical_bc3d` and `horizontal_diffusion_s` routines. An optional compiled-C++ argument runs the production-library scalar helper with the same 8×6×4 field and compares all 192 physical-cell raw tendencies, including first and last X cells. Default invocation continues to run the Fortran-only regression. The core Linux workflow invokes the cross-language comparison after the C++ test executable is built; no new CTest inventory entry was added.

Both paths use `q=1+0.03 sin(2πi/8)`, `Kh=2`, `rho=1`, unit maps, flat 20 m layers, `dnw=-0.25`, and `rdx=0.1f` through the C++ float metric ABI. The corresponding dry column-mass coefficient is `MUT=rho·g·Δz/|dnw|=784.8 Pa`. For flat geometry the Fortran code forms `H1_F=-rho·Kh·D_xq` and multiplies its divergence by `g/(dnw·rdzw)`; with `rdzw=1/20`, this yields the **same raw coupled tendency** as the C++ helper's `+div(Kh·MUT·grad q)`. The outputs are compared directly, with no post-hoc scaling. REAL64 Fortran uses the promoted float32 `rdx` because the C++ helper's `rdx` argument is float even for a REAL64 tensor. Y is invariant, so the different Y-boundary choices contribute zero.

## Direct validation and negative control

Apple Clang 22.1.4/Homebrew libtorch built the affected C++ executable; GNU Fortran 15.2.0 compiled the extracted Fortran module and driver with bounds checks. On this macOS host GNU Fortran required the installed Xcode SDK path in `FC` for linking; the same source uses plain `gfortran` on Linux CI. The source-extracted Fortran test passed five cases in both default REAL and REAL64, including its independent flat-X Fourier expectation. `Scalar_Diffusion_Contract` passed locally.

Direct cellwise comparison:

| Precision | Cells | Maximum C++–Fortran difference | Seam-adjacent maximum | Fixed `64ε·max(1,signal)` budget |
|---|---:|---:|---:|---:|
| FP32/default REAL | 192 | `5.96046448e-8` | `1.49011612e-8` | `7.62939453e-6` |
| FP64/REAL64 | 192 | `5.55111512e-17` | `3.01841889e-17` | `1.42108547e-14` |

The nonzero reference signal is about `0.275835` in both precisions. A local negative-control build deliberately disabled the new periodic C++ seam while leaving the rest of the source and Fortran reference unchanged. It compiled, and the direct comparison rejected it: FP32 maximum and seam-adjacent difference `0.332962006`, far above the fixed budget. The correct source was restored and rebuilt before the final successful comparison. This isolates the repaired seam in a cross-language test rather than relying on the current C++ implementation as its own oracle.

Source SHA-256: `tools/test_horizontal_diffusion_scalar.py` `69064dc8b7f559b2dfe294e4da7d417b701eefe7aafb6781c70a3d5e88584a03`, `tests/test_scalar_diffusion_contract.cpp` `b307e69679442c0ea6ec875865405230ce1b91ce379ee6e56548da6d2f8a8213`, CI workflow `df0456e1c96be7d2bb86e6866dbd4c7017f8b32eff16fdf1dae6362fd3ee4a6e`. Final local parity log SHA-256 `38620490544dd824e42405411128d823f2d445eb012ffc2efdad4c28b78926ed`; negative-control log `84ba57774489a77df834732eb6a230b67d2e520dd5faac1e4e7b2abe1c988cca`. C++ executable SHA-256 `7a55758d07db962ced2d867b237820364afd97dc1ce3c45e391c42552f14fe28`. A source-hash-matched Graphify corpus was inspected before the edit and refreshed after it; the numerical verdict comes from the actual Fortran/C++ outputs.

## Limits and next action

No production RHS or Fortran implementation changed in this candidate. No new WRF `em_b_wave` run or RK3 run was performed; the earlier #220 short WRF/RK3 field comparison remains context and does not validate this new cross-language fixture. This closes only F1-flat for a dry, flat, unit-map, sigma-like constant-coefficient scalar kernel. Hybrid layer mass, terrain metrics, variable rho/K, other boundary policies, momentum diffusion and full RHS remain F1-terrain/G1 work.

Next, obtain exact-head Linux CI for this branch and submit the test-only stacked PR. Then compare the hybrid/terrain scalar operator at the same state with its proper layer mass and face interpolation, rather than extrapolating this flat agreement to L34 as a whole.
