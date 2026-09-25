# Option-1 hybrid scalar layer-mass parity and remaining checklist

Timestamp: 2026-09-24 21:52:19 JST (+0900)

## Context and change

This candidate starts from PR #222 head `ce128fea92156120f2e3a5d8211fbd5ff81d9a05`, whose exact-head manual CI run `35998809411` passed. The target is the still-open F1 hybrid/terrain operator item in the [earlier checklist](./(202609241912)_remaining_findings_theta_seam.md). The actual `diff_opt=1` Fortran theta consumer is `horizontal_diffusion_3dmp` in `dyn_em/module_big_step_utilities_em.F`, called by `dyn_em/module_em.F`. On each scalar face it multiplies the separately averaged physical diffusivity and hybrid layer mass `L_k=c1h(k)*(MU+MUB)+c2h(k)`. The old C++ path preweighted momentum's `Kh_mom` by `L`, used that value again for the default scalar coefficient, and multiplied a two-dimensional `MU+MUB` in the scalar helper. Those operations do not implement the Fortran face product.

Step 9 now preserves the physical momentum diffusivity before its legacy option-1 weighting, uses `3*Kh_mom_physical` for the default scalar coefficient, and passes the current-state three-dimensional `L` to the scalar helper for option 1. The helper accepts either rank-2 sigma mass or rank-3 hybrid layer mass and averages it on X/Y scalar faces, including the whole-tile periodic X seam. A supplied physical scalar coefficient remains unweighted. An independently supplied scalar coefficient can activate option-1 scalar diffusion even when `khdif=0`. Option 2 retains its previous two-dimensional mass input: its active Fortran reference is the different `horizontal_diffusion_s`, with terrain, density and vertical metrics, and this PR does not claim that parity.

The graph inspection used a six-source focused corpus at `/tmp/sdirk3-hybrid-mass-graph`, built from the base tree before editing and refreshed after code changes to 365 nodes/873 edges/17 communities. The relevant call path (`module_em` → `horizontal_diffusion_3dmp`, and Step 9 → C++ scalar helper) was confirmed in authoritative code. Graph edges were navigation evidence, not numerical proof; the refreshed copied source hashes match this worktree's changed files.

## Direct validation

The new `tools/test_hybrid_scalar_layer_mass.py` extracts and compiles the actual Fortran `horizontal_diffusion_3dmp` body with bounds checks. It compares all 192 physical cells with the production C++ helper on an 8×6×4 unit-map, Y-invariant, periodic-X fixture with nonuniform positive `Kh`, `MU+MUB`, `c1h` and `c2h`. Its scalar base is zero; nonuniform `base_3d`, nonunit stagger maps, Y-face flux and physical terrain are outside this oracle. The fixed budget is precision scaled and chosen before the observed errors.

| Precision / Fortran optimization | Maximum C++–Fortran error | Budget | Legacy 2-D mass error | Missing `c2h` error |
|---|---:|---:|---:|---:|
| FP32 / `-O0` | `9.53674e-6` | `5.07591e-3` | `52.6154` | `400.452` |
| FP32 / `-O2` | `6.10352e-5` | `5.07591e-3` | `52.6154` | `400.452` |
| FP64 / `-O0` and `-O2` | `0` | `9.45461e-12` | `52.6154` | `400.452` |

The reference signal is about `665.309`; both periodic seam cells are included. A sigma control (`c1h=1,c2h=0`) gives the same C++ result with rank-2 and rank-3 mass. The actual-RHS test uses strongly level-varying `c2h` and compares the option-1 diffusion ON−OFF theta RHS against an independently assembled `L` passed through the Fortran-checked helper, followed by the fixture's primitive-theta mass conversion. `Full` and `ExplicitOnly` each have signal `2.34322e-4`, reference error `0`, and budget `9.53674e-7`; default and explicitly supplied physical scalar coefficients agree exactly. With `khdif=0`, injected scalar diffusivity changes theta but not U/V/W in both options 1 and 2. A compiled mutation disabling the option-1 caller's `L` construction fails both RHS modes with reference error `8.91211e-5`, so the direct helper comparison is not the only gate.

Independent Green and Red source/evidence reviews found no remaining blocker for this narrow option-1 contract after the caller mutation test was added. If hybrid coefficients are absent or too short, the existing rank-2 `MU+MUB` fallback still applies; this is only the sigma-like compatibility path and is not evidence for an underspecified hybrid state.

The affected core built with Apple Clang 22.1.4/Homebrew libtorch. The source-extracted Fortran check used GNU Fortran 15.2.0 with the installed Xcode SDK path and `-fcheck=bounds`; the Linux CI invocation uses plain `gfortran`. The existing flat source-extracted `horizontal_diffusion_s` regression still passes, with flat periodic C++–Fortran maximum errors `5.96e-8` (FP32) and `5.55e-17` (FP64). The final `Scalar_Diffusion_Contract` and all five `Full_Tile_*` CTests passed **6/6** in 118.71 s; the transcript SHA-256 is `d9035c2f4d5b15965d65f94e9fa48af7436bc3d47520084da0704f3c1aeb3795`. The source-extracted hybrid parity transcript SHA-256 is `fa0285c701a2142ffe96737afeb0c2755c2358ec697880df7f103250dc98b201`.

## Same-setup WRF and RK3 context

The final C++ archive was incrementally relinked into the archived complete WRF object tree; this is **not** a clean full WRF build. The source-matched archive SHA-256 is `6c492f2f0d2fbe19a78e2a33e3e378beb8ef5e69e869e020d0b47491cdd3ec29`, executable SHA-256 `94852b4b0ebebbe95002e159c28cbe3eb558b61d814446efe6393ac28fd8a9a2`, and reused `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`. Single-rank, single-thread `em_b_wave` K0 60 s × 4 and `diff_opt=2,khdif=1000,kvdif=0` 60 s × 1 both complete. Their outputs are byte-identical to the archived #222 results: K0 `feddafb618397f279ece672110c13d67312b8836fd360b24728edaf8391b8d23`, option 2 `33ef0a3ca62f68b9b851ed9d757668ca9257ee8731c24dbe81621aebdb429b75`. This verifies no change in those two configurations, not option-1 forecast correctness.

With the same initial field and a one-step `diff_opt=1,khdif=1000,kvdif=0` namelist, the final executable did not finish within the bounded attempt: its first Newton/Krylov solve repeatedly stagnated near true residual `0.07`. The immediately previous #222 executable also stagnated near `0.07` with the same option-1 namelist under a separate bounded run. Neither reached an output frame; the evidence does not identify this scalar change as the cause, nor can it validate option-1 WRF forecast fields. The exact first failure/solver cause remains open. No new RK3 run or same-accuracy runtime comparison was made. The archived same-setup option-2 RK3 comparison remains U/V/W/PH/T/MU RMS `0.002088916`, `0.0000220964`, `0.0000872781`, `0.0325392`, `0.0000309467`, `0.0414933`; it is context and cannot validate option 1 or time order.

## Checklist after this candidate

| ID | Status | Required next evidence |
|---|---|---|
| I0 | Open | Merge the intended stacked branches into the source actually used for WRF qualification, then identify the source/executable/input tree. |
| D1/B1 | Closed on `main` for dry fixed-K/sigma scope | Do not extend the claim to general WRF adjoint. |
| H1/S1a, O1 | Candidate verified on earlier branches | Complete I0 integration. |
| S1b-θslow | Candidate verified in #222 | Tie to I0; fast faces and sources remain outside it. |
| S1b-θall, S1b-mom | Open | Complete accepted-stage fast/source and staggered momentum budgets. |
| F1-flat | Candidate verified in #221 | Integrate with I0. |
| F1-hybrid-1 | Candidate verified locally in this branch | Exact-head CI and integration; only option-1 unit-map X-face hybrid mass, fallback coefficient and actual RHS link are checked. |
| F1-terrain | Open | Option-1 Y/nonunit-map/base and option-2 terrain/rho/metric parity, then variable-coefficient U/V/W. |
| G1, K1 | Open | Physical/tile/MPI boundaries and the declared diagnosed-coefficient derivative policy. |
| T1 | Open | Resolve the option-1 active WRF first-step stagnation; then active time refinement and same-accuracy RK3/PC0/PC2 comparison. |
| A1 | Open | Whole-WRF active-diffusion/observation-path adjoint and Taylor/dot checks. |

The smallest next operator comparison is option 1 with nonunit stagger maps and an active Y flux, followed separately by option-2 `horizontal_diffusion_s` on the same diagnosed geometry. Neither should reuse this unit-map result as a terrain or whole-model acceptance criterion.
