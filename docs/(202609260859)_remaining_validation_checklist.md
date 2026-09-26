# AD-SDIRK3 remaining validation checklist after option-2 vertical repair

Local timestamp: 2026-09-26 08:59:21 JST (+0900)

The candidate is `agent/option2_vertical_uv_fix`, stacked above draft PR #238. This is a scope-aware progress ledger, not an operational forecast or data-assimilation completion claim. The exact local source/build/run evidence is in `(202609260859)_option2_vertical_step10_validation.md`; remote exact-head CI is pending until the PR is pushed.

| ID | State | Evidence and next closure condition |
|---|---|---|
| I0 integration | **Open** | Draft stack #233–#238 has earlier exact-head CI and bounded full-build/one-step evidence. Combine and clean-build only the final reviewed source before merging; no merge is authorized here. |
| F1 option-1 scalar/momentum | **Scoped candidate passed** | Existing Fortran U/V/W, scalar, packed and fixed-K RHS contracts still pass after this change. Other boundary/physics/tiling and regenerated WRFPLUS TL/AD remain open. |
| F1 option-2 scalar | **Scoped candidate passed** | Dry `km_opt=1`, fixed physical coefficient, stage metric and scalar manufactured cases pass. Variable or physics-supplied K, moist density, alternative coefficient policy and MPI remain unsupported or unverified. |
| F1 option-2 horizontal U/V/W | **Open** | Bounded U complete-chain and W sign source oracles exist. General terrain, variable map/coefficients, V complete-chain, physical boundary and multi-tile Fortran parity remain. |
| F1 option-2 vertical U/V/W | **Bounded defect closed** | Actual-shape and all-mass-level U/V stress parity: 4,352 points each, max error 0; W vertical source parity: 4,335 points, max error 0. Same-state negative controls reject old U/V boundary, weighted interpolation and W coefficient/sign/metric paths. The complete RHS producer chain needs further parity. |
| P1 active one-step wind blow-up | **Resolved in tested case; broad acceptance open** | Source-level U/V shear had fallen back from physical `rdz≈0.001 m⁻¹` to `2*rdnw≈15–96`; exact-source WRF `kvdif=10` PC2−RK3 U RMS is 0.00209013 m/s versus ~29,807 m/s before the Step-10 stage fix. Four one-step outputs finite and PC2 stages converge. Repeat across a predeclared error budget, additional states/steps and full build before declaring general acceptance. |
| S1b whole-step budgets | **Open** | Closed-tile and accepted-stage traces exist. Need actual stage flux/source, area and hybrid layer-mass budgets on nonuniform physically consistent cases and exact final update. |
| G1 decomposition | **Open** | One-tile packed alias and fail-closed ownership guards exist. Need at least two tile layouts and MPI halo/owner equivalence for forward and transpose. |
| K1 coefficient derivatives | **Open** | Fixed-K active RHS VJPs exist. Declare diagnosed-K/rho differentiation policy and compare variable-coefficient JVP/VJP in the actual stage context. |
| T1 time accuracy, stability, cost | **Open** | One-step PC2/RK3 nonzero-diffusion comparison is now finite and close, but is not a time-order measurement. Prior 60/30/15/7.5-second refinement had mixed slopes and a Newton residual floor. Need solver/output-floor separation, smooth compatible refinement, longer stability and repeated equal-accuracy PC0/PC2/RK3 cost. |
| A1 full adjoint | **Open** | Fixed-context active RHS incremental FD/VJP and limited converged-stage pullback exist. Need actual active ARK step with packing/physical boundary/MPI transpose, coefficient dependencies and observation-objective Taylor test; WRFPLUS TL/AD needs regeneration against the current hybrid primal. |
| Reproducible full WRF | **Open** | Exact current C++ archive and executable/input/output hashes are preserved. The Fortran/WRF object set was reused from the earlier full build. A fresh checkout clean full WRF rebuild of the final integrated revision remains necessary. |

Immediate next sequence: (1) exact-head CI and review of this bounded PR; (2) same-state full option-2 U/V/W/scalar Fortran RHS comparison with owned boundaries and explicit input/output units; (3) clean combined WRF build and physically consistent multi-step, budget and time-refinement evidence; (4) active complete-step adjoint and decomposition. A green test count or one finite forecast cannot substitute for these closure conditions.
