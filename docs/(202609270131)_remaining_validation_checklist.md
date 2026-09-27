# Remaining validation after PR #244 plus scalar integration

Local timestamp: 2026-09-27 01:31:02 JST (+0900)

Integrated candidate: `c6c6655ad78a5ff8d728f0aac249aa4b84f54ab6`, based on exact #244 HEAD `17b505cda24fd232979518140d58757eebefb4d1`. The Homebrew Torch 2.10 build passed 104/104 CTest, Make archive parity passed, and the candidate-first one-step WRF relink/runs completed. This remains a bounded scalar candidate, not a forecast-acceptance result.

| ID | Current status | Evidence and next closure condition |
|---|---|---|
| I0 integration | **Integrated locally; Red/CI pending** | Scalar commits are stacked on #244. Full Homebrew CTest passed 104/104; targeted Scalar, Option2 geometry-source, WRF dynamics, and snapshot tests passed 4/4. Obtain final Red review and exact-head CI before merge. |
| F1 option-1 scalar/momentum | **Signed scalar smoke passed; full parity open** | Actual option-1 vertical-only RHS smoke uses khdif=0, kvdif=10, flat unit maps, zero velocity, monotone theta. It checks finite/nonzero signal and negative fixture work; it is not full option-1 Fortran parity. Existing option-1 momentum contracts remain bounded coverage. |
| F1 option-2 scalar | **Stage metric/sign closure passed for declared cases** | Same-state actual RHS ON−OFF matches a source-equation oracle in physical and packed extents for stress and simple branches. Old cached-rdz controls separate at roughly 6e-8–1e-7 against predeclared ~1e-10 budgets. Dynamic/supplied K, moisture, and other terrain/map/BC configurations remain open. |
| F1 option-2 U/V/W | **Composite parity for declared fixtures; full RHS open** | Existing source-extracted U/V/W composites and controls remain passing on prescribed fixtures. They do not establish WRF halo/physical-boundary/MPI coupling or complete RHS parity. |
| F1 top extrapolation `cfn/cfn1` | **Bounded production defect fixed in candidate** | Supplied WRF `fnm/fnp` with changed `rdnw` refreshes top coefficients on each zero-copy RHS call. External-to-internal weight-source switching remains unverified. |
| P1 one-step active wind blow-up | **Earlier bounded correction maintained; scalar PC2 deltas need review** | Candidate PC2 completes 60 s and is finite, but differs materially from clean #244: max absolute U 3.34e-6, V 1.84e-5, W 3.082e-3, PH 1.4693, T 6.433e-2, MU 1.812e-5. RK3 is bitwise identical. Investigate with Fortran full-RHS and longer runs before any forecast-quality claim. |
| S1b physical budgets | **Open** | Existing accepted-stage face/source checks remain; bilinear omission mutants were too weak relative to their experiment bound. Find a more resolved fixture or justify the endpoint product-defect budget. |
| Reproducible full WRF | **Candidate-first relink validated; clean integrated build open** | Integrated archive was linked into a new executable with unchanged clean #244 Fortran objects/libwrflib. The clean worktree was not modified. Run a clean full WRF build of exact integrated source. |
| G1 decomposition | **Open** | Need forward and transpose owner/halo equivalence across multiple tile and MPI layouts. |
| K1 coefficient derivatives | **Open** | Fixed-K derivatives exist; diagnosed K/rho policy and variable-coefficient stage JVP/VJP remain. |
| T1 time accuracy, stability, cost | **Open** | Existing refinement evidence is mixed/nonmonotone; the scalar change has only a one-step PC2/RK3 comparison. No third-order or equal-accuracy speed claim. |
| A1 complete active-step adjoint | **Open** | Need current-geometry ARK-step derivatives, physical-boundary/MPI transpose, objective Taylor evidence, and WRFPLUS TL/AD agreement with the current primal. |

Next closure: complete exact-head CI and Red review, then run the clean full WRF build on this integrated source. Resolve the PC2 one-step field differences with source-level RHS comparison and longer runs. S1b, G1, K1, T1, and A1 remain open.
