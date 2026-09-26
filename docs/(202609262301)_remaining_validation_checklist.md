# AD-SDIRK3 remaining validation after V composite diffusion parity

Local timestamp: 2026-09-26 23:01:14 JST (+0900)

This updates `(202609261705)_remaining_validation_checklist.md` for the V composite diffusion candidate based on draft PR #241 HEAD `9513b3aad2eb494077206f2a60bb1774bccdac78`. The candidate has source/test/report commits on the isolated worktree; it is ready to stack onto W draft PR #242 after integration review. It does not establish full RHS, MPI/physical-boundary, forecast, or adjoint acceptance.

| ID | Current status | Evidence and next closure condition |
|---|---|---|
| I0 integration | **Open** | V changes validate on the #241 base. Integrate onto the #242 stack, resolve any conflicts there, and run exact-head CI before merge. |
| F1 option-1 scalar/momentum | **Scoped candidate passed** | Source-extracted and C++ dedicated option-1 momentum tests pass REAL32/REAL64 at O0/O2, including packed U/V/W. The production option-1 dispatch uses `compute_horizontal_diffusion_option1_momentum` before the option-2 stress helpers. Broader BC/MPI/TLAD scope remains open. |
| F1 option-2 scalar | **Scoped candidate passed** | Existing fixed constant-K stage metric tests remain passing. Dynamic/supplied K, moist states, and noncanonical tile options remain unsupported or unverified. |
| F1 option-2 U | **Composite operator parity for one fixture; full RHS open** | The source-extracted U chain matches the C++ helper at FP32/REAL64 O0/O2 for interior and top pulses. This is not `computeUnifiedRHS` parity or a WRF halo/MPI comparison. |
| F1 option-2 V | **Composite operator parity for declared fixtures; full RHS open** | Same-state Fortran/C++ V horizontal stress, vertical stress, raw composite, D12/D22 producers, and post-map/mass normalization pass at FP32/REAL64 O0/O2. The fixed terrain case exercises bottom/top levels and periodic X seams; packed periodic mode verifies seven unique X cells plus endpoint aliases; the Y-terrain control activates nonzero `zy`, `D22`, and `titau2avg` Y interpolation. K0, wrong-K, old west-seam, wrong-Y-average, flat-terrain, and `nz=1/2` controls discriminate the checked paths. The source driver supplies analytic halos and does not call WRF physical BC or MPI exchange. Other metrics/maps, coefficients, partial tiles, and full RHS remain open. |
| F1 option-2 vertical U/V/W | **Prior bounded defect closure maintained** | PR #239 source-extracted U/V/W stress oracles still pass on their prescribed states. Full coupled diffusion parity remains open. |
| F1 top extrapolation `cfn/cfn1` | **Bounded production defect fixed in candidate** | Supplied WRF `fnm/fnp` and updated `rdnw` refresh top coefficients per zero-copy RHS call. External-to-internal weight-source switching remains unverified. |
| P1 one-step active wind blow-up | **Resolved in tested dry case; broad acceptance open** | The candidate PC2/RK3 one-step fields match the prior PR #241 arrays on the same 60 s static `em_b_wave` input. Its V field is zero, so the run confirms regression neutrality but does not activate V diffusion. Longer runs and active-V forecast acceptance remain open. |
| S1b physical budgets | **Scoped existing coverage; quantitative endpoint closure open** | Accepted-stage face/source checks and replay exist. The bilinear omission mutants remain too weak relative to the experiment bound; no weak assertion was retained. |
| Reproducible full WRF | **Incremental relink validated; clean full build open** | Candidate Make archive linked ahead of and explicitly after unchanged PR #239 Fortran objects. One-step PC2/RK3 completed; a clean full WRF rebuild with this final source is still open. |
| G1 decomposition | **Open** | Need forward and transpose ownership/halo equivalence across multiple tile and MPI layouts. |
| K1 coefficient derivatives | **Open** | Fixed-K RHS derivatives exist; diagnosed K/rho policy and variable-coefficient stage JVP/VJP remain. |
| T1 time accuracy, stability, cost | **Open** | Existing PC2/RK3 refinement evidence is mixed/nonmonotone and does not establish third order or equal-accuracy speed. |
| A1 complete active-step adjoint | **Open** | Need current-geometry ARK-step derivative, physical-boundary/MPI transpose, and objective Taylor evidence; WRFPLUS TL/AD must match the current primal. |

Next closure: integrate this source/test delta onto PR #242 and compare the complete option-2 U/V/W/scalar diffusion RHS with actual WRF physical boundaries, map/mass coupling, and MPI ownership. Preserve the current fixture as a bounded contract; it is not evidence for full RHS or forecast acceptance. No full clean WRF build or MPI run was performed here.
