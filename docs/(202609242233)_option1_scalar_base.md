# Option-1 theta base-state subtraction and remaining checklist

Timestamp: 2026-09-24 22:33:36 JST (+0900)

## Context and change

This candidate is stacked on PR #224 head `379c46d56f1bea513d6cf33bbc66e6e36569a0c3`. The Fortran `diff_opt=1` theta caller passes `field=t` and `base_3d=t_init` to `horizontal_diffusion_3dmp`. Its X/Y face differences are those of `t-t_init`, where both WRF arrays are potential-temperature perturbations relative to 300 K. C++ receives the original `t` state slot but previously passed it to scalar diffusion without subtracting `t_init`. That changes the spatial operator whenever the base perturbation varies horizontally; a vertically varying but horizontally uniform base hides the omission.

`setBaseState` now retains the original cloned `t_init` in `t_init_pert_` alongside the already stored `th_base_=t_init+300`. Reconstructing `t_init` by subtracting 300 from `th_base_` would lose FP32 input bits. Option-1 scalar diffusion uses `t-t_init_pert_`, while option 2 keeps its existing raw `t` input because its Fortran terrain-metric scalar path has a different definition. The retained base tensor is aligned with state device/dtype for the RHS and zero-copy path and included in the fixed-trajectory input fingerprint. The scalar coefficient, map and layer-mass ownership from #223/#224 remains unchanged.

The focused Graphify corpus was verified by source hashes against this worktree's parent commit before editing, expanded with the Fortran initialization/bridge sources, and refreshed after code changes. It has 592 nodes, 1,316 edges and 21 communities. The graph guided call-path inspection; the numerical proof uses the actual source and compiled tests.

## Direct validation

The source-extracted `horizontal_diffusion_3dmp` test now has a fourth case: a horizontally varying `base_3d=t_init` is added to the prescribed scalar field, so the desired anomaly remains the earlier nonunit-map X/Y case. It compares all 192 physical cells to the production C++ helper using that anomaly. An independent negative control sends the unsubtracted full field to the same helper.

| Precision / Fortran optimization | Fortran–C++ maximum error | Fixed budget | Omitted-base error |
|---|---:|---:|---:|
| FP32 / `-O0` and `-O2` | `4.88281e-4` | `2.77793e-2` | `18357.0` |
| FP64 / `-O0` and `-O2` | `6.82121e-13` | `5.17430e-11` | `18357.0` |

The actual option-1 `Full` and `ExplicitOnly` RHS tests install a nonuniform `t_init`, set the state `t=t_init+q`, and compare the ON−OFF theta increment against a separately assembled `q=t-t_init` scalar helper reference after the fixture's primitive-theta conversion. Both modes pass with nonzero signal `6.36023e-4`, reference error `0`, and absolute budget `9.53674e-7`. A compiled mutation omitting the subtraction at the production caller fails both modes with reference error `4.01424e-3`. Thus the test distinguishes the caller fix as well as the direct helper contract.

The fixed-trajectory fingerprint has a separate regression: changing one `t_init` entry by `1e-6` leaves `t_init+300` identical in FP32 but is still rejected as a changed fixed input. Both existing lifecycle configurations passed this check. This protects the original input used by diffusion rather than relying on its rounded full-theta derivative. The current full fixed-trajectory profile keeps diffusion off, so this is input-integrity evidence, not a whole-active-diffusion adjoint result.

Apple Clang 22.1.4/Homebrew libtorch built the C++ core; GNU Fortran 15.2.0 compiled the extracted routine with bounds checking for default REAL/REAL64 and `-O0`/`-O2`. The previous hybrid layer-mass, exact stagger-map, sigma-rank and flat Fortran/C++ controls still pass. The final `Scalar_Diffusion_Contract`, both `Fixed_Trajectory_*` and five `Full_Tile_*` CTests passed **8/8** in 177.57 s; transcript SHA-256 `df993f4e2e3e8c1cfda8ed31a82ffb6dc08fcf0f4a5622d85c97e5921a2024d3`. The final source-extracted Fortran/C++ transcript SHA-256 is `52bb12e4498f3fc2a6f7d9dda951e7023816384f74b000b3e26b81e9f4077f1a`. Independent Green and Red read-only reviews found no blocker for this narrow option-1 base contract; they did not run the production library themselves.

## Same-setup WRF and RK3 context

The locally rebuilt C++ archive SHA-256 is `9199cb4f8a8ab381e5835d245ea92b3e21ef6d43d4ff7bdc8ba0e8814903a57b`, incrementally relinked with archived WRF objects into executable SHA-256 `d65546c9c60141e05749efc1fd74f779e5a0cff548be9d3423aba3be98662923`. This is **not** a clean full WRF build. The reused `em_b_wave` initial field SHA-256 is `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; runs use one MPI rank and one OpenMP thread. Diffusion OFF (60 s × 4) and `diff_opt=2,khdif=1000,kvdif=0` (60 s × 1) completed and remained byte-identical to the archived #224 outputs: SHA-256 `feddafb618397f279ece672110c13d67312b8836fd360b24728edaf8391b8d23` and `33ef0a3ca62f68b9b851ed9d757668ca9257ee8731c24dbe81621aebdb429b75`. These are negative regressions for the changed option-1 path.

The final executable also failed to finish a bounded 55 s one-step `diff_opt=1,khdif=1000,kvdif=0` run: stage-2 GMRES repeatedly stagnated near true residual `0.0703`. Previous #223/#224 binaries stalled at the same stage and residual scale under this input. Even a completed short forecast would not establish full L34/temporal/adjoint correctness. No new RK3 run or repeated same-accuracy runtime comparison was performed. The archived same-setup option-2 RK3 RMS differences U/V/W/PH/T/MU `0.002088916`, `0.0000220964`, `0.0000872781`, `0.0325392`, `0.0000309467`, `0.0414933` remain context only and cannot validate option 1.

## Checklist status and next action

| ID | Status after this candidate | Missing closure evidence |
|---|---|---|
| I0 | Open | Merge the reviewed branch stack into the WRF qualification source and identify its exact executable/input. |
| H1/S1a, O1, S1b-θslow, F1-flat | Earlier candidate results | Integrate; they do not establish whole-model budgets. |
| F1-hybrid-1 | Candidate verified locally | Hybrid mass, exact maps and nonuniform theta base now match the extracted option-1 scalar Fortran routine on the tested complete tile; exact-head CI and integration remain. |
| F1-terrain | Open | Option-2 `horizontal_diffusion_s` with rho/PH/metrics, then U/V/W and boundary/halo/general map cases. |
| S1b-θall, S1b-mom | Open | Accepted fast/source and staggered momentum budgets. |
| G1, K1 | Open | Partial-tile/MPI boundary equivalence and diagnosed-coefficient derivative policy. |
| T1 | Open | Diagnose option-1 PH-dominated stage-2 Krylov plateau; then active temporal refinement and same-accuracy RK3/PC0/PC2. |
| A1 | Open | Whole-WRF active-diffusion/observation-path adjoint and objective Taylor/dot checks. |

The next operator target is option-2 scalar terrain/density/vertical-metric parity, using the distinct active Fortran routine, not the option-1 anomaly rule. In parallel, the option-1 WRF plateau needs fixed-state blockwise AD-JVP versus centered finite difference and TDMA pre/post residuals before a solver change is justified.
