# Wave energy budget, spatial convergence and two-mode inverse

Local timestamp: 2026-10-05T01:27:16+09:00.
Base: main `d6a0e3179989878f4787537cea7e00472e95c1f0`, tree identical to accepted
PR275 HEAD `92c0e6782a1178bcada2cea654d2a58ef4a34654`.
Branch: `agent/wave-energy-spatial-20261005`; existing temporary checkout reused.

## Closed scope

PR275's owned-U pressure-gradient repair, independent 17-component source
reference, 2980-second modal propagation, 300-second data-only two-control
inverse and native-analysis forecast to 900 seconds are accepted. Fixed-control
integration/reoptimization decomposition and the declared time-order gates are
also closed. Exact-head CI37198822707 passed all four jobs: 123 registered,
122 passed, one MPS skip, zero failures. Do not reopen these results as absent.

## Ordered completion checklist

- [x] Reproduce the existing four-layer source spectrum and approximately
  2.9162% quarter-period bulk diagnostic change, distinguishing the exact
  quarter from the nearest native sampled time and analytic FP64 backgrounds
  from imported FP32 PHB geometry.
- [x] Derive physical kinetic/acoustic/available-potential quadratic weights
  and the moving constant-pressure top's work/storage from governing equations.
  Do not recover a conserving weight by fitting the source matrix/eigenvectors.
- [x] Explain the measured diagnostic change through independently derived
  boundary work and explicit interior stencil/metric/coordinate-conversion
  defects. Check both instantaneous matrix identities and integrated budgets;
  a residual renamed as a source is not independent closure evidence.
- [x] Hold Lx=40 km, Ly=30 km, ptop=20 kPa, dry mass=84 kPa and the continuous
  theta(eta)=317-8eta profile fixed. Sample the same hydrostatic problem on each
  grid; compare horizontal and vertical refinement separately. Native baseline
  has nx=8, ny=6, nz=4, dx=dy=5 km, not nx=4.
- [x] Track the same mode by phase-independent physical profile overlap and
  vertical structure rather than choosing a new largest sub-Nmax frequency on
  each grid. Compare to an independently solved continuum boundary-value
  problem with the same compressibility and boundary conditions. Document
  ambiguity or failed convergence rather than tune a frequency/threshold.
- [x] Check a second mode or a two-mode mixture's four real quadratures are
  identifiable from the existing two-time W observations before spending on
  native optimizer runs. Reuse the FP64 forward/multi-output adjoint and
  independent observations; forecast from the actual accepted Z300 to Z900.
- [x] Preserve source/compiler/executable/input evidence, source-matched
  Graphify navigation and final independent Green/Red review. Run only affected
  numerical checks initially, then the required regressions once the algorithm
  scope is stable. The bounded local checklist is satisfied; PR submission and
  complete exact-head CI are tracked separately in the PR.

## Closure update

Local timestamp: 2026-10-05T02:40:17+09:00. Detailed results are in
`docs/(202610050240)_pr276_wave_energy_spatial_closure.md`. The energy,
fixed-geometry spatial, and two-mode identifiability/inverse items above are
closed at their stated source-reference/native-test scope. The final local item is now closed with the 126-name inventory, passing affected
checks, 28 source-bound graph files and Green/Red review (closure timestamp 2026-10-05T02:50:57.852064+09:00);
full exact-head remote CI remains a PR check; the required `em_b_wave` comparison is reused
from PR275 and is not a new run for this energy/spatial follow-up.

## Physical conventions

Let P denote Eulerian pressure perturbation in Pa and b=g*theta_E/theta_bar.
For the resting dry hydrostatic atmosphere,

    u_t = -ik P/rho
    w_t = -P_z/rho - g P/(gamma p_bar) + b
    P_t = rho g w - gamma p_bar (ik u + w_z)
    b_t = -N^2 w.

The pressure-stratification term is -g P/(rho c^2), where c^2=gamma p_bar/rho;
omitting rho in that notation changes units. Multiplying by physical quadratic
weights cancels the pressure-stratification and buoyancy exchanges, leaving
the pressure-work flux. At a moving constant-pressure top,

    P_top = rho_top g zeta_top,
    zeta_top_t = w_top,
    E_bulk_t = -area * P_top * w_top,
    E_surface_t = +area * rho_top g zeta_top * w_top.

The real-Fourier horizontal average contributes a factor of one half to these
quadratic quantities. The top density must follow the actual boundary state;
the old diagnostic's last-mass-level density was explicitly a candidate, not
an established boundary closure. Likewise the old finite-difference theta_z
must be distinguished from the continuous profile's analytic gradient.

The generalized source reference remains an independent transcription of the
original WRF discretization, not the production RHS Jacobian. A separately
derived flux/adjoint-consistent Eulerian discretization is a diagnostic
comparison; equality with it is not assumed and it must not replace the WRF
equations silently. Continuum, source-discrete and native comparisons are
different evidence levels.

## Evidence status

No production source or WRF execution has been changed for this follow-up at
this timestamp. Existing WRF/RK3 same-input field/runtime comparisons remain
in `docs/evidence/stable_wave_coupling/wrf_receipt.json`; they are not an energy
or spatial-convergence oracle. The cached external Graphify corpus matches
the accepted source tree; extraction gaps in Fortran/macros/templates remain
navigation limitations. NumPy2.2.6/SciPy1.15.3 are available for cheap reference
calculations. General terrain, moist physics, diagnosed K, MPI and real-weather
qualification remain later scope.
