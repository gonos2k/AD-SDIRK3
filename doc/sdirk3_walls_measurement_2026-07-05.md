# SDIRK3 dt=600 walls — measurement campaign (2026-07-05)

> **HISTORICAL RECORD (dated snapshot).** Solver/API descriptions in this
> document (e.g. the pre-FGMRES GMRES/BiCGSTAB era) reflect the code as of
> the date of this record, not the current contract. For the current state
> (FGMRES solver, MPI support boundary, CTest inventory) see the repository
> root `README.md` and `external/libtorch_wrf/sdirk3/README.md`.


Measured results for the two obstacles to a working differentiable forward core on `em_b_wave`
(mode-3 / ARK324). All findings are from direct instrumentation (env-gated, default-off probes),
not inference — earlier causal stories in this investigation were repeatedly downgraded, so only
measured facts are stated as facts.

## Diagnostic probes (opt-in, default off; stderr only)

| env flag | file | measures |
|---|---|---|
| `WRF_SDIRK3_RITZ_DUMP` | `wrf_sdirk3_newton_solver.cpp` | in-GMRES Ritz spectrum of the iterated operator (Hessenberg eigenvalues) |
| `WRF_SDIRK3_LINPOINT_UN` | `wrf_sdirk3_newton_solver.cpp` | re-centers the Stage-3 Jacobian at the raw physical state `U_n` (Route-B test) |
| `WRF_SDIRK3_KSLOW_PROBE` | `wrf_sdirk3_tile_unified_impl.cpp` | per-field ‖k_slow‖ (u/v/w/ph/t/mu) + amplitude-scaling g(ε) |
| `WRF_SDIRK3_SLOWFLOW_PROBE` (`_DT_STABLE`) | `wrf_sdirk3_tile_unified_impl.cpp` | Wicker–Skamarock RK3 sub-cycling of the slow tendency (Phase-0 de-risk) |

(The RITZ/LINPOINT probes settled Wall-1 / Route B and are documented here; the KSLOW/SLOWFLOW probes
are committed in-source as opt-in diagnostics.)

## Wall-1 (dt ≥ ~80): intrinsic operator indefiniteness — preconditioner-dead

The fast operator `A = I − dt·γ·J_fast` at dt=600 is **intrinsically indefinite**: the in-GMRES Ritz
spectrum straddles the origin (min_re ≈ −1.2e5, ~19–36% of eigenvalues in the left half-plane),
unpreconditioned and damping-independent. Re-centering the Jacobian at the smooth balanced IC (`U_n`,
a ~150× change in linearization point) gives a **term-by-term-identical** indefinite spectrum → the
operator is ~linear, so no smooth background removes the indefiniteness. By Sylvester's law of inertia,
no SPD preconditioner reaches dt=600. **Route B** (re-linearized SDIRK3 autodiff TLM about a smooth
background) is therefore measured-**dead**; the large-step adjoint is deferred to a WRFPLUS-style TL/AD.

## Wall-2 (dt ≤ ~70): explicit u-advection cascade

Per-component + amplitude-scaling measurement:
- The blow-up channel is the **u-momentum (`ru`) block** — ≥99.99% of ‖k_slow‖ at every stage
  (‖k_slow(u)‖: 1010 → 69294 → 8.0e5 → 5.0e10 at dt=60).
- The term is **horizontal advection** (only active bilinear u-momentum term; PGF excluded by
  HEVI-independence, Coriolis linear, curvature off, diffusion excluded).
- **Bilinear/quadratic** runaway (g(ε) slope 1.2→1.8), not a linear positive-real mode.
- HEVI-independent (baseline instability). k_slow₁=1010 is the physical initial advective tendency of
  the sheared baroclinic jet; the ARK324 explicit tableau over-extrapolates each stage seed
  (stage-2 seed = dt·aᴱ·k_slow₁ = 52840, measured), compounding to blow-up.

## Wall-2 fix — Phase-0 de-risk (2026-07-05): naive sub-cycling REFUTED

A design-eval recommended sub-cycling the slow tendency with WS-RK3 at h = dt/M (dt_stable ≈ 4 s).
The Phase-0 probe `slow_flow(U_n, dt, M)` **refuted it**:
- M=1 (positive control) reproduces the cascade (→3.9e10 at dt=60; →NaN at dt=600).
- At **all tested h** (4, 0.5, 0.1 s) the slow flow **still blows up** (NaN before completing M
  sub-steps). Smaller h keeps the first sub-steps flat (~1010) but only *delays* the blow-up — it is
  **h-independent**: the cumulative unbalanced drift over the integration window (≈ ‖k_slow‖·t) reaches
  the ~50000 displacement that triggers the nonlinear amplification.

**Interpretation (strongly supported by the h-independence):** the slow channel has PGF in the
*separate* fast/implicit channel, so advancing advection in isolation breaks geostrophic/hydrostatic
balance — exactly what WRF's split-explicit avoids by keeping advection+PGF together in the RK3 slow
part and sub-stepping only the acoustic perturbation (MRI-GARK-style frozen forcing). A robust Wall-2
fix therefore needs the fast (PGF) response co-integrated with the slow advection, not a naive split.
