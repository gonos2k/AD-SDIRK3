# Stable compressible wave and inverse review checklist

Local timestamp: 2026-10-04T15:39:23+09:00.
Base: main5201af93a8f41252956dda050ce43df690234e61, byte-identical tree to accepted PR274 HEADb51a26f. Working branch agent/stable-wave-coupling-20261004.

## Closed previous scope

PR274:8/16step MAP windows, withheld3/6s predictions, same-data h=.25/.125 comparison, stable310–316K background/initialEulerianbuoyancy diagnostic, WRMSroundoff and retained-root graph corrections are accepted; do not reopen them. Actual PR-head CI37169950570 succeeded all4jobs,119registered118passed1MPSskip0failed; total CTest5880.79s; #75 completed3785.51s. Its90→150min job allowance changed no numerical criteria. PR274 is merged as5201af9; pending-CI wording has been corrected in its body.

## Ordered new checklist

- [x] Define the actual compressible four-mass-layer C-grid wave problem, including nonzero periodic-X wavenumber, symmetricY, native sigma/EOS equilibrium and exact vertical boundary/caller conventions.
- [x] Assemble an independent small linear operator from authoritative Fortran source equations for EOS, pressure gradient/buoyancy, continuity/Omega, theta and PH. No production RHS Jacobian may stand in as the independent reference.
- [x] Compile/source-check representative Fortran rows and verify phase staggering/signs/units/coefficients before selecting a reference eigenmode.
- [x] Select a coupled oscillatory mode from the independent spectrum; verify measurable pressure/buoyancy/W contributions and use its full frequency, rather than imposeomega=N or a Boussinesq dispersion.
- [x] Compare native small-amplitude/A-half trajectories to independent wave phase, amplitude/decay and physical energy components; record the source pressure/buoyancy coupling, and explicitly separate the unclosed free-top boundary work from any conservation claim. No fitted matrix norm is called physical energy.
- [x] Run fixed-state coarse/fine forecasts once at the coarse-optimal control; split direct forecast delta into integration and reoptimization effects and verify algebraic/vector/norm closure. No extra optimization is needed.
- [x] Recover the verified wave's modal quadratures/initial amplitude-phase with the existing multi-output FP64 adjoint and fixed physical R; use independent-reference observations, physical states, actual cost/gradient decrease, and unassimilated later phase/amplitude checks. Measure memory before choosing a long retained window.
- [x] Refresh source-matched Graphify; run affected checks, preserve exact source/binary/case/compiler evidence and final Green/Red review; prepare publication of the bounded result as a new PR. Publication/CI state is recorded in the PR body, not inferred from this pre-publication snapshot.

## Source and boundary contracts

The actual stable background remains MUB80kPa/MUbar4kPa/ptop20kPa, theta310/312/314/316K, four equal sigma intervals. EOS/PH balance is native-discrete; don't substitute a continuum profile. Native Fourier mass points and Ufaces have distinct phases; all8x6mass cells are owned, while105Wobservation sites are a subset.

Bridge kte=kde-1 only counts mass layers. Original solve_em passes k_start/k_end fromkps/kpe to the W-staggered routines; advance_w uses local k_end=kte-1 then a special top row. The current top_lid=false path allows Wtop and PHtop evolution. Do not infer an inert top row from bridge sizing or impose a rigid lid accidentally. projectStateBoundaries only projects lateral normal velocity/packedaliases. Bottom is flat/no-flow.

Reference sources: module_big_step_utilities_em.F calc_p_rho_phi/calc_php/pgf/pg_buoy_w/calc_ww_cp/rhs_ph; module_em.F advect_scalar/rk_addtend_dry; module_small_step_em.F calc_p_rho/advance_mu_t/advance_w. Dynamics: [WRF technical note](https://www2.mmm.ucar.edu/wrf/site/documentation/technote.html). The note defines the compressible mass-coordinate flux variables; equations/conditions must be checked against the actual source revision. Nonisothermal linear-wave context: [consistent fast-mode analysis](https://journals.ametsoc.org/view/journals/mwre/135/1/mwr3275.1.xml). These are primary contextual sources, not replacements for this discrete operator.

Eulerian p/entropy differ from values on moving eta surfaces. Available-potential, acoustic and kinetic energies require physical/staggered weights; free moving top can carry boundary work/surface energy. Do not claim exact total energy from Qtheta or fitH fromproductionA/eigenvectors and relabel it physical energy. A mixed acoustic-gravity branch may give a bounded first propagation case; label its branch and duration, without claiming all20minute internal-gravity modes are qualified.

For refinement, P_h(v_h)-P_f(v_f) = [P_h(v_h)-P_f(v_h)] + [P_f(v_h)-P_f(v_f)]. Norms can cancel; retain the dot/cross term.

## Validation status and reuse

Completion snapshot 2026-10-04T18:57:55+09:00: the owned-U pressure-gradient range/wrap correction, independent wave/inverse and fixed-control decomposition are validated. Same-executable WRF FP64 ON completes48/48 stages; OFF still rejects first-stepStage2. Exact receipts, source/executable hashes and field/runtime comparisons are in docs/evidence/stable_wave_coupling/. Preserve PR274's same-setup WRF48/48stage evidence and RK3 field differences in docs/evidence/fp64_window_forecast/wrf_validation.json (step-log6.49975s vsRK3 .26174s is not wall/equal-accuracy cost). Existing timeorder/short-adjoint/stable-equilibrium gates remain valid in their declared scope. MPI/moist/diagnosedK/terrain/real observations remain subsequent expansions.

Cached19-file Graphify code corpus matches main5201 source. Fortran/preprocessor/template extraction gaps are navigation limitations; authoritative formula/caller inspection is the numerical evidence. The source test is in the cached corpus even though its complex anonymous-namespace relationships may be incompletely extracted.

## Bounded closure

All eight implementation/validation items are closed in the declared dry, flat, fixed-coefficient single-tile scope. Local passing coverage includes every one of the123 registered names across partitions; two initial test failures and their controlled diagnoses remain archived. Green and Red final reviews found no remaining code blocker. See (202610041727)_stable_wave_coupling_and_forecast_validation.md for results and the retained limitations. Exact total-energy/boundary-work closure, MPI/moist/terrain/diagnosedK and operational qualification are subsequent work, not completed claims. The PR body will identify the published HEAD and live GitHub CI state.
