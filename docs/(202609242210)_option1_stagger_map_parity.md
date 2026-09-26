# Option-1 scalar stagger-map parity and remaining checklist

Timestamp: 2026-09-24 22:10:40 JST (+0900)

## Context and change

This candidate is stacked on PR #223 head `03d5875c9f8e6ae0f607cbef502e18c6ebe093c6`. That PR established option-1 hybrid layer-mass and physical scalar-coefficient ownership for a unit-map X-mode. In the same Fortran consumer, `horizontal_diffusion_3dmp`, X scalar faces use the **provided U-face** ratio `msfux/msfuy`, Y faces use the **provided V-face** ratio `msfvy*msfvx_inv`, and mass-cell divergence uses `msftx*msfty`. The C++ scalar helper had reconstructed the first two ratios from mass-point maps, which is not the same operator on a varying map.

Step 9 now passes its existing staggered U/V map tensors to the scalar helper only for `diffusion_option=1`; it requires all four relevant staggered arrays and valid `c1h/c2h` coefficients in that production path. Sigma callers must supply `c1h=1,c2h=0` rather than silently falling back to 2-D mass. The helper uses the exact U-face ratio, and V-face `msfvy/msfvx`, at the matching face index. The direct helper keeps its existing approximate-map mode when those optional tensors are absent, which preserves the option-2 path until its distinct terrain-metric Fortran operator is reconciled. The old option-2 periodic seam multiplication order was retained. No Fortran source, Fortran/C++ ABI, solver tolerance or option-2 formula changed.

The Graphify focused corpus was verified against this worktree's starting commit by file hashes before editing, then refreshed after editing. Its final copied code hashes equal the authoritative source; it has 372 nodes, 885 edges and 18 communities. Graph relationships guided call-path inspection; source formulas and compiled comparisons provide the numerical evidence.

## Direct comparison and mutation controls

`tools/test_hybrid_scalar_layer_mass.py` still extracts the actual Fortran `horizontal_diffusion_3dmp` body and compiles it with bounds checks. A new 8×6×4 case prescribes distinct, positive, nonunit mass/U/V maps, varying `Kh` and hybrid layer mass, and scalar gradients in both X and Y. Y outer halo values reproduce the no-flux wall used by the C++ fixture; X is a complete periodic tile with the first/last U-face map values aliased. All 192 owned physical cells, including the X seam, are compared with the production-library helper. Neither a continuous-Laplacian approximation nor a reconstructed mass-point face map is used as the reference.

| Precision / Fortran optimization | Fortran–C++ maximum error | Fixed budget | Old X-map-only error | Old Y-map-only error |
|---|---:|---:|---:|---:|
| FP32 / `-O0` and `-O2` | `4.88281e-4` | `2.77793e-2` | `135.387` | `105.618` |
| FP64 / `-O0` and `-O2` | `6.82121e-13` | `5.17430e-11` | `135.387` | `105.618` |

The expected signal is approximately `3641.09`; reverting both face-map directions yields error `194.997`. The earlier hybrid-mass, sigma-rank, and flat periodic scalar comparisons also pass unchanged. In an actual option-1 RHS fixture, nonunit U/V face maps are supplied to the solver, and its `Full` and `ExplicitOnly` ON−OFF theta increments both match an independently assembled layer-mass/face-map helper reference after the fixture's primitive-theta conversion. Their signal is `6.36023e-4`, reference error `0`, and budget `9.53674e-7`. A compiled negative control that omitted the new stagger-map arguments at the **actual RHS caller** failed both modes with reference error `6.89397e-5`; the direct Fortran helper test alone is therefore not the only gate.

The final Apple Clang 22.1.4/Homebrew libtorch core built cleanly. GNU Fortran 15.2.0 ran the extracted routine with `-fcheck=bounds` in default REAL and REAL64 at both optimization levels. The existing `horizontal_diffusion_s` flat periodic C++–Fortran comparison still passes in FP32/FP64. The final `Scalar_Diffusion_Contract` and all five `Full_Tile_*` CTests passed **6/6** in 119.86 s; transcript SHA-256 `edde87393b693f6bb1f4b985198f26a71d7f7f8e690e3f22c19df5e91bb9cb4f`. The final Fortran/C++ hybrid-map transcript SHA-256 is `46e28144eb171ab07c96d25482d3c60562afe6b8f184fd91363e2b7971aa2fc8`. `actionlint` and `git diff --check` passed. Independent Green and Red reviews found no blocker for the stated scope; both keep packed exact-map topology open.

## Same-setup WRF and RK3 context

The final C++ archive SHA-256 is `4e31e3c4d0f6171c511806743de8eef00d6ba5c169aaaec43f5f9a80837e6ee5`, incrementally relinked against archived WRF objects to executable SHA-256 `1186f8b003679f532a4987b8de35e23a2095e80d3816901f9be7b58f5a6b7131`. This is **not** a clean full WRF rebuild. The reused `em_b_wave` initial field SHA-256 is `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; MPI is one rank, OpenMP one thread. Diffusion OFF (60 s × 4) and `diff_opt=2,khdif=1000,kvdif=0` (60 s × 1) both completed and remained byte-identical to #223: output SHA-256 `feddafb618397f279ece672110c13d67312b8836fd360b24728edaf8391b8d23` and `33ef0a3ca62f68b9b851ed9d757668ca9257ee8731c24dbe81621aebdb429b75`, respectively. This directly checks regression neutrality for those setups.

A bounded one-step `diff_opt=1,khdif=1000,kvdif=0` run still did not reach a forecast step: the stage-2 GMRES true residual repeatedly stagnated near `0.0703`. The prior #223 and pre-#223 executables also stalled at the same stage and residual scale. The separate Red investigation identified a pressure-dominated coupled linear solve plateau but did not establish a source-level root cause. This map correction is therefore not whole-WRF option-1 convergence evidence. No new RK3 run or repeated same-accuracy runtime comparison was performed. The archived same-setup option-2 RK3 RMS comparison remains U/V/W/PH/T/MU `0.002088916`, `0.0000220964`, `0.0000872781`, `0.0325392`, `0.0000309467`, `0.0414933`; it does not validate option 1.

## Checklist status and next action

| ID | Status after this candidate | Missing closure evidence |
|---|---|---|
| I0 | Open | Integrate the reviewed PR stack into the branch actually used for qualification. |
| H1/S1a, O1, S1b-θslow, F1-flat | Candidate verified in earlier PRs | Their narrow results require I0; they do not close full model budgets. |
| F1-hybrid-1 | Candidate verified locally | Source-extracted option-1 scalar `L` and exact X/Y stagger maps now match on a whole periodic-X tile with Y no-flux halo. Exact-head CI and integration remain. |
| F1-terrain | Open | Nonuniform scalar base, option-2 density/terrain metrics, physical halo/BC, then U/V/W and full RHS at the same state. |
| S1b-θall, S1b-mom | Open | Accepted fast/source and staggered momentum face budgets. |
| G1, K1 | Open | Partial tile/MPI boundary equivalence and diagnosed-coefficient derivative policy. |
| T1 | Open | Diagnose stage-2 option-1 PH-dominated Krylov plateau; then active time refinement and same-accuracy RK3/PC0/PC2 cost. |
| A1 | Open | Whole-WRF observation-path adjoint and objective Taylor/dot checks. |

The immediate diagnostic for the stage-2 plateau is a fixed-state blockwise AD-JVP versus centered finite-difference JVP, followed by TDMA pre/post residuals in PH/MU/U/V/T. That separates Jacobian/context errors from preconditioner or conditioning limits without changing numerical acceptance thresholds. The next Fortran operator comparison can treat a nonuniform scalar base, followed separately by option-2 `horizontal_diffusion_s`; this report does not claim either completed.
