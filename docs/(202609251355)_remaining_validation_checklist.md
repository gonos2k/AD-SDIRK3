# Remaining AD-SDIRK3 validation checklist after bounded diffusion work

Timestamp: 2026-09-25 13:55:21 JST (+0900)

## Source and evidence status

Remote `main` was `27abe61b12337c0192c7dfa40f6f1184e1f8e46b` at this check. PRs #222–#229 are marked merged into their **feature-branch bases**, not into `main`. PR #230 (bounded dry option-2 Step 9) and #231 (its active RHS increment VJP) remain open drafts. Exact-head manual CI completed successfully for #227–#231; local reports distinguish direct execution from developer/remote evidence. This report is a live checklist, not an assertion that the full project is ready for forecast or data-assimilation use.

| ID | State | Evidence now available | Needed to close the broader item |
|---|---|---|---|
| I0 integration | **Open** | Feature-branch stack and source/executable receipts exist; current `main` does not contain #226–#231 | Review the combined branch topology, run one exact integrated CI/WRF build, then merge only when authorized. |
| F1 option-1 scalar | **Scoped candidate passed** | #223–#225 cover hybrid layer mass, staggered maps and `t-t_init`; source-extracted/local RHS tests | Other boundaries, partial tiles and physics-supplied coefficients. |
| F1 option-1 momentum | **Scoped candidate passed** | #226–#227 forward Fortran V-Y/U/V/W source parity, packed RHS basis and one-step activation; #228 RHS increment VJP | Other options/tiles, full-step Fortran equivalence, WRFPLUS TL/AD regeneration. |
| F1 option-2 scalar | **Scoped candidate passed** | #229 source-matched helper; #230 dry `km_opt=1,damp_opt=0` Step 9 physical/packed ON−OFF, one-step WRF and QV rejection; #231 fixed-K RHS VJP; current variable K/rho/map helper case | Dynamic/physics-supplied `xkhh`, moist rho, stretched eta, noncanonical/boundary/MPI ownership, stage-dependent coefficient policy. |
| F1 option-2 U/V/W and L34 | **Open** | Prior flat normal-stress and shape/sign fixes; scalar metric path above | Same-state Fortran comparison of full U/V/W stress, terrain/boundary/variable-coefficient fluxes and mass normalization. |
| S1b whole-step budgets | **Open** | Existing closed-tile and hybrid dry mass/weighted-theta fixtures; partial accepted-stage trace | Stage flux/source and boundary exchange budgets on physically consistent nonuniform cases and the exact final update. |
| G1 decomposition | **Open** | One-tile packed alias tests and explicit rejection of unsupported topologies | Two tile layouts and MPI halo/owner equivalence for forward and transpose. |
| K1 coefficient derivatives | **Open** | Fixed-K scalar and momentum RHS directional VJPs | Declare which diagnosed K and rho dependencies are differentiated; verify variable-coefficient JVP/VJP against the actual stage policy. |
| T1 time accuracy/stability/cost | **Open** | Same-executable 60/30/15 s and exploratory 7.5 s `em_b_wave` runs complete, but output self-difference slopes are mixed; dt15 Newton tolerance `1e-6` stalls near scaled residual `1.325e-6` | Separate solver/output floor and physical forcing refresh; use smooth compatible reference, then repeated same-accuracy PC0/PC2/RK3 cost and longer stability checks. |
| A1 full adjoint | **Open** | Fixed-context active option-1 and option-2 RHS increment FD/VJP checks; earlier limited converged-stage pullback | Actual converged ARK step, boundary/packing/MPI transpose, observation objective Taylor test and regenerated current-hybrid WRFPLUS TL/AD. |
| Reproducible full WRF | **Open** | Affected Fortran object plus C++ archive incremental relink completed reduced dry `em_b_wave`; source/input/executable/output hashes retained | Fresh checkout, clean full WRF rebuild and repeat of supported nonzero diffusion run with the same configuration. |

## Immediate next work

First finish and review the variable K/rho/nonunit-map helper parity case in this branch. Then connect physical diagnosed coefficient ownership at a declared stage state rather than passing a similarly shaped proxy, and extend option-2 scalar parity to stretched eta. In parallel, qualify the forward U/V/W stress path before using active-diffusion time-refinement or full-step adjoint results as acceptance evidence. The current 60/30/15/7.5 s output fields do not yet resolve third order; tightening Newton from `1e-5` to `1e-6` at 15 s failed at a measured residual floor, so a new solver/output precision control is needed before interpreting those ratios.

No new WRF model run or RK3 comparison was performed by the **test-only variable-coefficient change** documented alongside this checklist. Its scientific claim remains private-helper source parity, not a new production coefficient route.
