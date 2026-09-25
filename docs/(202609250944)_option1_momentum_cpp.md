# Option-1 U/V/W coordinate-surface diffusion and remaining checklist

Timestamp: 2026-09-25 09:44:59 JST (+0900)

## Context and change

This candidate starts from draft PR #226 head `aabba232087f749c26222ded7a8164730222d1f5`, which corrected the inherited Fortran V-Y layer-mass omission. Its exact-head CI run `36010802008` passed. The C++ Step 9 still sent `diff_opt=1` U/V/W through the option-2 deformation-stress helpers, after preweighting K by hybrid layer mass. On a flat U-X or V-Y Fourier mode that route has a different sign, stencil and normalization from Fortran `horizontal_diffusion`; it could not be made equivalent by a local sign or tolerance adjustment.

Step 9 now dispatches `diffusion_option=1` U/V/W to one dedicated coordinate-surface helper. It retains physical mass-point diffusivity K, forms `L_h=c1h*(MU+MUB)+c2h` for U/V and `L_f=c1f*(MU+MUB)+c2f` for interior W levels, and interpolates K and L separately to each component's Fortran flux locations. The helper returns the raw Fortran `ru/rv/rw_tendf` contribution. Before adding it to the C++ column-mass-coupled accumulator, Step 9 multiplies by the staggered `velocity_mass/L`. This combines Fortran's `rk_addtend_dry` division by the component map with the conversion from `alpha=L/map` to the C++ column-mass basis; the map factors cancel. The option-2 stress branch and the option-1 scalar changes from #223–#225 remain separate. The helper supports one complete periodic-X/symmetric-Y tile in physical or WRF packed layout, using the same packed alias/reflection map Q as the stage projection. Other boundary flags, multi-tile/MPI ownership and incompatible shapes fail explicitly; no end-value or mass-column replication is used as a substitute for a neighbor halo.

The focused Graphify code corpus matched the target worktree revision before editing and was refreshed afterward to 693 nodes, 1,474 edges and 24 communities. Copied source hashes match the final local C++ and test files. The graph served as a call-path map; numerical correctness is assessed from the authoritative Fortran source, independent manufactured modes and execution results.

## Fortran–C++ and RHS validation

`tools/test_option1_momentum_diffusion.py` extracts and compiles the actual corrected Fortran `horizontal_diffusion` body with bounds checks. A flat 8×6×4 fixture checks independent U-X periodic sine, V-Y no-flux sine/odd-reflection, and W-X interior-W sine modes against discrete Laplacian eigenvalues at two positive column masses and four hybrid levels. The direct production-library CLI then compares every owned raw tendency in both precisions. A second packed fixture uses 7×5 unique mass cells in an 8×6 packed array with nonconstant K, MUT, distinct nonunit mass/U/V maps and nonzero cross-direction gradients. It compares all Fortran-owned cells and checks the C++ output aliases against Q.

| Case | Owned cells | FP32 maximum difference | FP64 maximum difference |
|---|---:|---:|---:|
| Physical U, two mass values | 192 per value | `4.88281e-4` | `6.82121e-13` |
| Physical V, two mass values | 224 per value | `5.18799e-4` | `9.66338e-13` |
| Physical W interior, two mass values | 144 per value | `1.22070e-4` | `2.27374e-13` |
| Packed U / V / W | 160 / 168 / 105 | `1.95313e-3` / `1.03760e-3` / `1.34277e-3` | at most `9.09495e-13` |

All differences are below the fixed precision-scaled budgets; the smallest listed FP32 packed budget is `0.14298`, while the largest FP64 packed difference is below its budget by more than two orders of magnitude. The no-C++ analytic Fortran modes pass default REAL/REAL64 at `-O0`/`-O2`, with resolved signal and negative discrete work. The final source-extracted/cross-language transcript SHA-256 is `aa7e43a956f8fd297736ab3495a9e1b0097b1dc2bf6f808b5ee44109e955a033`.

The production `Full` and `ExplicitOnly` RHS contract checks option-1 U-X/V-Y `RHS(K)-RHS(0)` against independent `K*lambda*velocity` in primitive output units. Errors are at most `6.89e-11`, with negative modal work; the existing option-2 `2K*lambda` checks still pass. A further integrated hybrid fixture uses nonunit, varying maps and MU with `c1<1,c2>0`; its U/V/W `Full` and `ExplicitOnly` ON−OFF increments match raw-helper output divided by the staggered L within `2.98e-8` (budget `3e-4`). Algebraic counterfeit outputs for the former direct `raw/M` path differ by `0.040–0.047`, while an incorrect `raw*map/L` path differs by `0.0078–0.0081`; all exceed ten budgets. This verifies the normalized C++ primitive RHS basis, not the complete historical Fortran `rk_tendf → rk_addtend_dry → advance_uv/w` time step. The counterfeit checks are fixture discrimination, not temporary production mutations. Apple Clang 22.1.4/Homebrew libtorch rebuilt the full affected core, and GNU Fortran 15.2.0 compiled the extracted routines using the detected macOS SDK sysroot. The final source-extracted parity transcript is `.validation/option1-mom-cpp/momentum_parity_basis.log`; the integrated scalar-contract transcript SHA-256 is `e85dcf8c2892edc7cc07e5817116e8e495e15f49e091dcc4873287180f8d5dde`. The targeted `Scalar_Diffusion_Contract`, `Horizontal_Momentum_Contract`, both `Fixed_Trajectory_*` and all five `Full_Tile_*` CTests passed **9/9** after the basis change. `actionlint` and `git diff --check` passed.

## Same-setup WRF and RK3 evidence

The rebuilt C++ archive SHA-256 is `8b1c43e2971c1c86c9a84f25ec2e54f8087d28382cad30b9cc1ec16414dab0e2`; it was incrementally relinked with archived WRF objects, including the corrected Fortran #226 object, to executable SHA-256 `a622ed6dc1c02b43876e0649ba7a31ea727bae6e882e3687bd79573abb963498`. This is **not** a clean full WRF build. The reused `em_b_wave` input SHA-256 is `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`. All runs use one MPI rank and one OpenMP thread on the reduced 17×17×16 case.

At `dt=60 s` for one step, `diff_opt=1,khdif=0` remains byte-identical to the prior output (SHA-256 `87b4af49b25e97daee45b5f1f927b861d99bcc29446f843b170157ef2cdf7a43`). The untouched `diff_opt=2,khdif=1000` branch also remains byte-identical (SHA-256 `33ef0a3ca62f68b9b851ed9d757668ca9257ee8731c24dbe81621aebdb429b75`). The corrected option-1 branch completes one step with `khdif=1`, `100` and `1000`; the final `khdif=1000` output SHA-256 is `b01e0ceb4be280f6d23a64fd4307a89bad155f7364610f7b567e3e8db8e20b22`. All 174 floating output variables in the final K0, option-2, option-1 PC2 and same-source RK3 files are finite. The active option-1 run has three linear solves, each converging in its first restart. Before the C++ dispatch fix, the same `khdif=1` setup did not complete in a 55 s bound and had true GMRES error above target; `khdif=1000` repeatedly stalled near `0.07`. This A/B result strongly ties the previous one-step block to the old option-1 operator path, without isolating which of its sign, mass or stencil errors dominated.

The same executable and initial field under corrected Fortran `diff_opt=1,khdif=1000` were used for a split-explicit RK3 one-step reference. At the final frame, PC2–RK3 RMS differences in each output variable's units are U `0.002088888`, V `0.0000220962`, W `0.0000874799`, PH `0.0325998`, T `0.0000312647`, MU `0.0414919`. This is a same-source implementation comparison, not error against truth. The new one-step log times were PC2 `0.49619 s` and RK3 `0.08742 s`; no repeated, same-accuracy cost or temporal-order result is inferred. The final local input/executable/output receipt is `.validation/option1-mom-cpp/wrf/basis_receipt.json` (SHA-256 `2c8db0b040cef57c260ddf3d99f09a239e4d6ced0d62976283bbfcbaa5a38cd1`). The new sigma-coordinate outputs are byte-identical to the preceding run, as expected when L=M and maps are unity; the integrated hybrid RHS test exercises the newly corrected basis.

## Checklist status and next action

| ID | Current status | Remaining closure evidence |
|---|---|---|
| I0 | Open | Integrate the reviewed PR stack into the source used for qualification, then bind exact source/executable/inputs. |
| H1/S1a, O1, S1b-θslow, F1-flat | Earlier candidate results | Integrate; they do not establish whole-model budgets. |
| F1-hybrid-1 | Candidate verified in #223–#225 | Scalar hybrid mass, exact face maps and nonuniform theta base on the supported complete tile. |
| F1-U/V/W-opt1 | **Candidate verified here** | Physical/packed one-tile U/V/W raw operator parity, hybrid/nonunit-map integrated RHS basis, and one-step sigma WRF activation are established; nonperiodic/open, partial-tile/MPI and longer physical runs remain. |
| F1-opt2 | Open | Match `horizontal_diffusion_s` rho, current PH geometry, terrain corrections, signed vertical metrics, maps and halo/coef ownership. |
| S1b-θall, S1b-mom | Open | Accepted fast/source and staggered momentum face budgets. |
| G1, K1 | Open | Partial-tile/MPI equivalence and diagnosed-coefficient derivative policy. |
| T1 | **One-step option-1 solve block resolved for this case** | Active time refinement, stronger nonlinear/stability cases, solver error budget and repeated same-accuracy RK3/PC0/PC2 cost. |
| A1 | Open | Active option-1 diffusion VJP/FD and full observation-path adjoint. Checked-in Fortran Tapenade V-Y TL/AD remains stale after #226. |

The immediate follow-up is an active-diffusion directional-derivative/VJP check with velocity, MU and declared coefficient perturbations, then the option-2 scalar terrain operator and partial-tile halo ownership. No general WRF convergence, forecast-skill or DA readiness claim follows from this one-step case.
