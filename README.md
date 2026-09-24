# AD-SDIRK3

A **differentiable SDIRK3** (3rd-order singly-diagonally-implicit Runge–Kutta) implicit time
integrator built into **WRF v4.7.0**, using **PyTorch / libtorch** with a zero-copy Fortran↔C++
interface and autodiff (JVP and VJP are implemented and contract-tested on the supported paths;
HVP via double-backward is a design goal). The goal is a differentiable dynamical core for
**4D-Var adjoint** modeling.

- **IMEX split** (mode 3 = ARK324L2SA): slow advection/Coriolis/diffusion terms explicit,
  acoustic/gravity terms implicit. The optional HEVI split also moves horizontal acoustic terms explicit.
- **Matrix-free Newton–Krylov** implicit solve: **FGMRES** (flexible, right-preconditioned — the
  earlier fixed-preconditioner GMRES was replaced during the full-repo review) with Eisenstat–Walker
  adaptive forcing, a vertical preconditioner, and a trust-region fallback. `A·v = v − dt·γ·J·v` is
  a JVP; the 4D-Var gradient is a VJP.
- **Zero-copy interface (prognostic arrays):** Fortran `(i,k,j)` column-major maps to C++
  `(j,k,i)` row-major with no data copy — layout `{nj,nk,ni}`, strides `{ni*nk, ni, 1}`.
  **Base-state initialisation is not zero-copy**: it materialises owned contiguous per-tile
  snapshots via `.contiguous()`. The `zerocopy` in those symbol names is historical.
- CPU WRF execution is validated in the documented cases. MPS map-cache
  transfers have separate contract tests; the full MPS RHS remains unsupported
  because of mixed CPU/MPS tensors. CUDA execution is unvalidated in this review.

## Status

The model **builds and runs** (`main/wrf.exe`, `main/ideal.exe`,
`external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`). Frozen revision `cc66520`
completed six `dt=600` steps on `em_b_wave` through 3600 seconds with finite output and
all 18 implicit stages below scaled RMS `1e-4`. Its seven output frames were compared
with the restored stock-RK3 archive using identical primary initial fields; see
[the frozen-run receipt](docs/(202609060337)_ad_sdirk3_dt600_rk3_comparison.md).
This validates that run and configuration. Long-duration stability, whole-WRF third-order
accuracy, forecast quality and the full trajectory adjoint remain **unverified**.

Verification is an **exact 102-test CTest inventory** pinned by
`.github/ci/expected_ctest_names.txt`, plus a numerical fingerprint that hashes the
deterministic solver-diagnostic and RHS-digest streams so behaviour-preserving changes can be
proven byte-identical.

Earlier RHS singular-value and solver-probe amplitudes below predate the horizontal-PGF
and dry-theta corrections. They are historical measurements, not current operator, forecast
or stability certifications.

### What is measured

- **C++ scalar diffusion sign.** `Scalar_Diffusion_Contract` calls the linked
  production helper in float32/float64 and checks constant preservation,
  interior Fourier eigenvalues, zero total tendency, and nonpositive `q·Lq`
  with unit maps and fixed diffusivity/mass. The helper uses zero outer fluxes;
  this test does not establish periodic-edge or terrain parity. Option dispatch,
  terrain metrics, and general coefficient/mass ownership remain open.

- **Option-2 diffusion coefficient locations.** The RHS constructs default
  horizontal viscosity on the mass grid, keeps it in physical diffusivity units,
  and uses `kvdif` for the W stress coefficient. Scalar fallback is `3*khdif`;
  consumers retain responsibility for their own interpolation and coupling.
  Staggered RHS coverage includes the V-boundary slice axis, the U stress
  average extent, and the W metric profile length; no terminal metric padding
  is introduced. The existing scalar contract also exercises the full RHS
  with staggered inputs, equivalent default/supplied horizontal coefficients,
  and an independently supplied scalar coefficient when `khdif=0`.
  This does not establish full U/V/W stress parity, dynamic W-coefficient
  support, or the separate option-1 coordinate-surface operator.

- **Option-2 U/V stress units.** Normal stresses include physical density
  `(1+qv)/alt`; shear stresses average density and viscosity separately.
  The diffusion helpers restore the signed eta orientation locally and return
  a coupled tendency without an additional dry-column-mass factor. The caller
  converts this to the existing velocity accumulator using the hybrid face
  mass and map factor. Explicit-only evaluations obtain density from the same
  current state. `Scalar_Diffusion_Contract` also checks flat U-X/V-Y normal
  stresses against an independent signed-eta oracle in FP32/FP64, varying
  density and column mass separately, with negative work required. A dry,
  unit-map, mass-coordinate-mode-0 RHS check compares `RHS(K)-RHS(0)` with
  `2*K*Laplacian` and compares full versus explicit-only evaluation.
  The U terrain term now uses the same outer vertical scale as its horizontal
  stress divergence, so layer depth cancels as in the Fortran formula. A
  nonzero-slope, vertically varying stress case checks two layer-depth
  profiles in FP32/FP64. A direct stretched-eta case checks U's 1D metric
  fallback against the layer depth implied by its divergence scale. Option-2
  W stress also receives the same current-state
  mass-point density as U/V; a nonzero-W-diffusion RHS check verifies its
  `(1+qv)` response and invariance to potential-temperature changes at fixed
  geometry and column mass. Full terrain, V's 1D metric fallback, boundaries,
  variable coefficients, the complete W stress operator, and option-1 parity
  still require separate validation.

- **Fortran scalar diffusion on terrain.**
  `python3 tools/test_horizontal_diffusion_scalar.py` extracts the current
  `compute_diff_metrics`, `set_physical_bc3d`, and `horizontal_diffusion_s`
  routines and checks manufactured cancellation and a nonzero Fourier mode
  against its discrete eigenvalue, in default REAL and REAL64 arithmetic.
  A mixed height/Fourier case activates both flux-divergence terrain corrections
  in the interior vertical layers; its oracle includes the discrete product
  correction. The original cases still check all physical layers. Oracle
  normalization uses the prescribed layer depth, not a computed metric.
  The fixed, precision-scaled engineering budget is not a rigorous general
  roundoff bound; actual errors and budgets are printed separately.
  This is a serial source-level check with prepared periodic input halos,
  not MPI communication, C++ diffusion parity, or whole-WRF qualification.

- **Dry potential temperature and continuity.** Ordinary ARK in WRFParity mode uses one
  hybrid/map-aware face-flux diagnosis for column mass, Omega, and theta transport.
  Periodic and symmetric stencils cover every physical face; packed aliases are excluded
  from the mass diagnosis and restored afterwards. Vertical theta flux uses WRF's eta
  sign and includes the top cell. The final product rule uses level mass `c1h*M+c2h`.
  The extra `-theta*div(u,v,w)` source and its unused private divergence cache were removed.
  `Potential_Temperature_Contract` checks constant theta, nonzero mass/Omega controls,
  and an independent column-divergence oracle with varying mass, hybrid coefficients,
  nonunit maps, orders 2/3/5, HEVI on/off, and physical/packed single-rank layouts.
  Split export retains its separate driver-supplied tendency convention.

- **Vertical momentum and geopotential transport.** Ordinary WRFParity uses the shared
  WRF order-3 vertical operators with the signed eta orientation and all U/V mass levels
  plus the W lid flux. Omega and hybrid coupled masses use the same boundary averaging;
  packed aliases are excluded before averaging. The raw WRF transport is converted to
  the legacy momentum accumulator so final division yields the correct velocity term.
  `Vertical_Momentum_Contract` compares independent scalar U/V/W flux oracles with
  nonunit anisotropic maps, layer-dependent hybrid coefficients and physical/packed layouts.
  The PH eta gradient uses `-|rdnw|` with the WRF outer `-Omega`; the production-linked
  FNM/FNP test checks its sign, boundaries and HEVI decomposition. These component
  contracts do not certify the whole momentum RHS or whole-WRF time order.

- **Horizontal pressure gradients.** The full and acoustic RHS share the first three WRF
  PGF terms with raw neighbor differences and sums. Grid inverse spacing and the common
  half are applied once. `Full_Tile_Horizontal_PGF` isolates Phi/p/pb and their derivatives,
  then tests actual tile X/Y accelerations from separate geopotential and thermal gradients
  at two grid spacings. This catches the former V double-spacing factor and U half-pressure
  term; unused V pressure interpolation and base-geopotential differences were removed.
  `PGF_Coordinate_Contract` checks U/V/W force-to-velocity conversion using the
  corresponding hybrid mass and map factor, including the separate W lid force.
  Independent stencils exercise nonunit maps, hybrid coefficients and HEVI decomposition.
- **Packed boundaries and observational diagnostics.** State, forcing and RHS share the
  normal-velocity wall constraint. A single whole-domain packed tile with periodic X,
  symmetric Y and AD halo exchange off also uses the same copy/reflection map at RHS input
  and output. It closes periodic aliases and the odd V north ghost within the ARK stage
  equations. This map is oblique: its pullback accumulates the transpose of each copy and
  reflection. Full-halo paths retain their separate exchange and wall contract.
  Debug levels 0/2 produce identical finite results and both reject an undefined EOS;
  logging no longer replaces a non-finite RHS with zero.
- **Base-state EOS and hydrostatic pressure.** WRF's Exner form
  `alpha = (R_d/p0)*theta*(p/p0)^(-cv/cp)` is a single authority, contract-tested forward *and*
  in its tangent. The pressure integrator's eta orientation is pinned against WRF's own algebra
  by a contract that calls the production helper.
- **Vertical metric.** One fail-close policy across every source; no eps substitution, no
  cross-stagger `rdn`→`rdnw` fallback, and no source-metric padding. One unused consumer
  slot remains (`vert_deriv_scale` is `nz_w` long while only `0..nz-1` is read); it is
  canary-tested with NaN on the measured path, and the `dz`-fallback twin is not yet
  covered. Verified on a *stretched* eta grid,
  where the staggers differ by 5e-01 — on a uniform grid they agree to 0, which is where this
  class of defect hides.
- **Instantaneous perturbation equilibrium.** The assembled production RHS returns **exactly
  zero** in every channel and every `RhsMode` at zero perturbation, paired with a non-zero
  control so the measurement cannot be confused with a dead probe. This is *not*
  well-balancedness: `F(U) = 1000U` also satisfies `F(0) = 0`. The full-tile test now measures
  1 / 10 / 100 steps: uniform dry mass is unchanged; W/Phi drift is also measured.
  These measurements do not establish exact equilibrium preservation.
- **The RHS Jacobian shows nothing anomalous at the first RHS base point.** Its implicit part is
  state-invariant to six digits (predictably: the coefficient is `mu`, which moves 0.01% between
  rest and jet); its explicit part is proportional to base-state amplitude with a measured
  power-law exponent of **1.00031**, which is what a bilinear advective operator must do. The
  leading singular direction is the acoustic `w` mode.
- **AD.** Forward-mode duals, reverse-mode VJPs and the `<Jv,w> == <v,J^T w>` identity hold on
  the EOS and the pressure integrator; the reverse pass runs through the whole production RHS.
  Production `J_FD` and `J_AD^T` agree to **1.198e-06**.
- **Production ARK composition and converged-stage pullbacks.**
  `ARK324_Production_Composition` uses the shared production stage/final sums and actual
  Newton–GMRES solves on noncommuting and time-dependent manufactured systems, including
  timestep refinement and a tighter-Newton control. Its one/three-step pullbacks agree with
  dense implicit roots and pass dot/Taylor tests. This is not a whole-WRF temporal-order claim.
  `Full_Tile_Temporal_Order` additionally refines an actual dry tile's vertical-wave trajectory,
  checks W/Phi against a finer reference, repeats with tighter Newton tolerance, and requires
  constant theta to stay constant. With the corrected dry physics, the `h=1/0.5/0.125`
  family at final time 64 observes W order 2.789 and Phi order 3.029. Tightening Newton
  tolerance from 1e-7 to 1e-9 leaves the fine state unchanged. The earlier h=2/1 W order
  of 2.479 was pre-asymptotic in this test; this remains a tile test, not a whole-WRF claim.
- **Last completed tile-step pullback.** With `retain_graph_for_adjoint=true`, mode 3,
  `use_autograd=true`, and `imex_slow_in_tangent=true`, `pullbackLastStep(cotangent)` evaluates
  a first-order VJP of the last completed single-rank, single-tile step. Converged RHS graphs
  are saved at their stage points; transpose solves must pass a true-residual check. A new
  forward step invalidates the saved graph. `Full_Tile_Step_Adjoint` calls `unifiedStep` and
  checks a non-identity derivative, finite-difference dots, objective Taylor remainders,
  invalid/zero cotangents, and identical forward results with retention off. The validated
  scope is a dry CPU tile with fixed timestep, forcing and boundary branches; second
  derivatives and a complete WRF/4D-Var trajectory are not implemented by this API.


- **Fixed native tile trajectories.** `beginFixedTrajectory(N, schedule)` retains the
  accepted stage graphs for N steps; an omitted schedule enforces constant timestep.
  `pullbackFixedTrajectory(cotangent)` composes their reverse VJPs, and
  `closeFixedTrajectory()` releases the tape. The checked profile is dry serial CPU
  ARK mode 3 with NH/curvature enabled, current-W buoyancy, fixed grid/base inputs,
  and zero projected physics forcing. Reset, input mutation, changed state handoff,
  unsupported branches, and incomplete tapes are rejected. The derivative and lifecycle
  tests cover two/three steps, scheduled timesteps, Taylor/FD checks, and input changes.
  This API does not supply the full Fortran/MPI or observation-window adjoint.

### What is NOT measured, and matters

- **Operational full-timestep stability remains unverified.** The tile-step derivative test
  above does not bound `DG` along the WRF forecast trajectory or include split-explicit
  acoustic substeps. RHS `DF` measurements alone also cannot establish that bound.
- **The legacy 4D-Var adjoint replay is not a completed full-ARK trajectory adjoint.** The
  opt-in last-step API above attaches the implicit-function pullback to converged production
  Newton roots; it does not complete the separate checkpoint replay. The RHS is differentiable and the transpose
  operator is correct (rel 1.198e-06).

  The remediation ordering that used to stand here — try a frozen `M^-1`, then flexible
  preconditioning, then a genuine `M^-T` — is **spent**. A transpose preconditioner exists and the
  `A^T` solve uses it (`apply_inverse_transpose`, wired at
  `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:39448`, which throws rather than
  silently returning `r`). It was measured **not** to be the blocker, so the convergence failure is
  still unlocalised rather than waiting on that build.

  The stalled-residual numbers previously quoted here were taken before that wiring and are not
  re-measured; treat them as history, not current state.

  The self-adjointness caveat — that the probe called a stateful `apply()` twice and so could not
  separate true asymmetry from state change — is now addressable: the operator contract requires a
  repeatability check and a state digest sampled around every call, and refuses an operator that
  supplies neither.

- **Built since, so this list stays honest in both directions:**
  - a **live `A P_j^-1` probe** reads the actual FGMRES triplet (`v_j`, `P_j^-1 v_j`,
    `A P_j^-1 v_j`) at zero extra operator calls, in both scaled-Krylov and physical-WRMS
    coordinates, with per-direction **gain / shape-defect / cosine** — opt-in via
    `WRF_SDIRK3_APINV_DEFECT=1`. The identity defect is a *target*, not a conditioning verdict:
    `B = cI` has an arbitrarily large one and is a one-step GMRES solve, which the contract pins
    as a case, so shape-defect is what actually costs Krylov directions.
  - the **adjoint replay owns its preconditioner** — a replay-local instance built from the same
    constructor inputs, and a guard *verifies* (fingerprint + generation equality) that the
    production instance is untouched across the replay rather than restoring it. **Cleanup
    incomplete**: a receipt-equality contract pinning strict no-write isolation is not yet in
    place.
### Historical solver experiments (2026-08-17; earlier source)

The results below describe their recorded configurations. They are not current-source
convergence or stability claims; later validation is recorded in timestamped `docs/` reports.

- **STAGE 2 CONVERGES at a real Krylov budget** (2026-08-17). At `stage2_gmres_restart=600`
  (510 Arnoldi) stage 2 converges — with the production preconditioner (gate 0.095) *and* without
  it (gate 0.058). It had never converged in this configuration before; the shipped budget is 7
  vectors. So the stage-2 stall the recent coefficient work was chasing yields to the
  large-budget experiment configuration — stated that way rather than as "budget starvation",
  because setting `stage2_gmres_restart` also changes the early-exit policy (it gates a
  mid-budget probe and suppresses periodic true-residual checks), so the experiment moves two
  things at once and the attribution to budget alone is not clean.
  This does **not** solve dt=600: stage 3 still fails (gate 0.727 without `M`, 0.999 with) and
  zero steps complete. **Stage 3: the earlier "not budget-starved" claim is RETRACTED.** That table's
  first row was labelled "global default" but stage 3 *inherits* the stage-2 budget when its own
  knob is unset, so the arms were not budget-comparable — measured, `unset` runs 510 Arnoldi
  (gate 0.727) while an explicit 600 runs 600 Arnoldi (gate 3.386). The mechanism is ordering:
  the EW budget scaling is applied *before* the stage-3 override, so an inherited value is scaled
  (600 → 510) and an explicit one is not. Stage 3 still fails at every setting tried, but *why*
  is unmeasured.
- **Measured, and it reframes the preconditioner work:** the operator GMRES iterates is
  **indefinite in the field-of-values sense** under WRFParity — the min eigenvalue of the
  symmetric part of the Arnoldi Hessenberg is **−2570** against a max of 4545, 13 of 51 negative.
  **RETRACTED — the Sylvester argument was wrong.** Sylvester's law governs *congruence*
  `CᵀHC`; right-preconditioning gives `AP⁻¹`, whose symmetric part `½(AP⁻¹+P⁻ᵀAᵀ)` is **not** a
  congruence of `H(A)`. An explicit 2×2 counterexample settles it: `A = [[0,−1],[2,2]]` has
  `H(A)` eigenvalues −0.118 / 2.118 (indefinite), yet the SPD `P⁻¹ = [[2,−1],[−1,2]]` gives
  `H(AP⁻¹) = diag(1,2)` — positive definite. So an indefinite symmetric part does **not** rule
  out an SPD preconditioner.
  What the measurement still supports: *in these Krylov coordinates, for this preconditioner
  realisation, the projected symmetric part had negative directions*. The `M`-with vs `M`-without
  numbers (−2570 vs −112) come from **different full-model runs** — different `A` and `b` — so
  they are not a causal statement that `M` amplifies anything. Measured at stage 2, dt=600,
  restart 60.
- **Not built yet, stated so the gap is not read as done:**
  - the **correct raw block diagonals** `A_qq = I - h·J_qq^direct`. The shipped `phi` and `mu`
    diagonals are dimensionally invalid (the source says so at both sites), but the replacement
    values are **not** established — a dimensional argument cannot supply them, and the
    Schur-reduced round trips are already computed elsewhere, so moving them into the raw
    diagonal would double-count. THREE scalar experiments have now been measured, and all
    are **harmful** when measured by the solver's OWN convergence quantity — the internal
    `error_tensor` GMRES tests against its tolerance (stage 2, dt=600, restart 60): shipped
    **0.6483**, `div_leg` 0.6703, `div_leg+phi_unity` 0.6773, `phi_unity` 0.7979, `mu_phi_zero`
    0.9194. **The shipped operator is the best of every configuration tried**, and each derived
    "correction" degrades it. Two earlier readings of these experiments — off the unscaled
    `rel_error`, then off a residual scaled with the wrong vector — ranked them the other way
    round; both are retracted. All on the open experiment PR rather than on `main`. See `AcousticGravity_Shadow_Contract`.
  - the **full WRF/4D-Var trajectory adjoint**, beyond the retained tile-step API above
  - the **acoustic–gravity coefficient re-derivation** (`D_mu`, `D_phi`, `c_s^2+N^2`,
    direct/Schur double-count, theta–W)
- **No certified multi-step well-balancedness, geostrophic/thermal-wind balance, exact
  mass/energy/PV budgets, or whole-WRF temporal-order verification.**
- Support boundary: **dry, single-rank, single-tile, idealised map factors.** MPI halo primitives
  are contract-tested; the integrated multi-rank SDIRK solve is not supported.

### On the earlier "Wall-1 / Wall-2" framing

Prior *pre-FGMRES* campaigns described two candidate walls. Treat both as **historical**:

- **Wall-1** (implicit indefiniteness at large dt) is a prior measurement that has **not** been
  re-established at the current head.
- **Wall-2** (explicit u-momentum cascade) had its apparent corroboration **withdrawn**
  (2026-08-01): the supporting figure came from a response matrix that divided a *coupled*
  tendency by an *uncoupled* scale, inflating it by ~1e5. Correctly normalised, the entry
  inverts. See `doc/` and the project memory for the audit trail.

The current honest position is that EOS, pressure orientation, the vertical metric and the
first-state RHS Jacobian have each been examined and found sound, which makes the remaining
candidates the **stage composition, the implicit stage solve, explicit–implicit
non-commutativity, and stage intermediate states** — none of which is measured yet.

## Build & run

Build **only from the repo root** — never `make` in an individual directory.

```bash
printf '37\n1\n' | ./configure                 # 37 = this system, 1 = nesting (read from STDIN)
nohup ./compile -j 4 em_b_wave > compile.log 2>&1 &   # then: tail -f compile.log
```

`./clean -a` is required **only after a Registry change** (it regenerates `frame/` / `inc/`); skip it
otherwise. Run the test case:

```bash
cd test/em_b_wave && ./ideal.exe && ./wrf.exe
```

## Configuration

The IMEX split is selected by `sdirk3_imex_split_mode` (`0` full-implicit → `2` post-SDIRK3 →
`3` ARK324, the operational mode). HEVI (horizontally-explicit / vertically-implicit) is opt-in via
`sdirk3_hevi_split` (default off → baseline byte-identical). New solver knobs are **opt-in**
(default = no behavior change) and fully wired through Registry + Fortran `set_config` + C++.

## Key files (`external/libtorch_wrf/sdirk3/`)

| File | Role |
|---|---|
| `wrf_sdirk3_newton_solver.cpp` | Newton–Krylov + FGMRES + solver diagnostics |
| `wrf_sdirk3_tile_unified_impl.cpp` | Unified RHS, tile parallelization, ARK324 stage loop, HEVI split |
| `wrf_sdirk3_unified_preconditioner.cpp` | Vertical preconditioner (M) |
| `wrf_sdirk3_imex_ark324_coeffs.h` | ARK324L2SA Butcher tableau |
| `wrf_sdirk3_jvp_autograd.{cpp,h}`, `wrf_sdirk3_jvp_fwad_or_fd.h` | JVP (forward-mode dual + FD fallback) |
| `wrf_sdirk3_config.h` | Config knobs, `effective_imex_split_mode()` |
| `jvp_bridge.F90` | Fortran↔C++ AD bridge |

## MPI / decomposition support boundary

The differentiable SDIRK3 core supports exactly one decomposition; everything else fails closed
with a stable marker **before** any communicator/halo state mutation or solve:

- **Single MPI rank + supported single-tile path** — production WRF positive evidence
  (`SUCCESS COMPLETE`, six split-explicit stage norms bit-identical to the tracked golden).
- **2/4-rank SDIRK** — refused pre-solve with `SDIRK3_MPI_STAGE_HALO_UNSUPPORTED`.
- **AD halo + multi-tile** — refused pre-solve with `SDIRK3_MPI_MULTI_TILE_UNSUPPORTED`.
- **MPI halo primitive** — verified independently of the solver at np=1/2/4: forward, adjoint,
  packed AD+BC transpose, and the runtime fail-close contracts
  (`MPI_Halo_Contract_np{1,2,4}` + `MPI_Runtime_Contract_np{1,2,4}` in the pinned CTest suite).
- **Decomposition evidence** — the SDIRK3 decomposition fail-close matrix
  (`.github/ci/run_decomposition_matrix.sh`, 4 cases) was produced by direct local-machine
  execution; it is *not* a full-WRF decomposition validation and does not include a stock-RK3
  baseline.
- **Stock RK3 1/2/4-rank decomposition baseline** — deferred: it requires a separate
  non-`USE_SDIRK3` build (a `USE_SDIRK3` binary always routes through the SDIRK3 path).

## Documentation

- `doc/SDIRK3_EM_B_WAVE_BASELINE_2026-02-16.md` — baseline validation
- `doc/sdirk3_hevi_preconditioner_findings_2026-06-21.md` — HEVI + preconditioner findings
- `doc/sdirk3_mode3_stage3_rootcause_2026-06-20.md` — Stage-3 root-cause analysis
- `doc/` files are dated point-in-time evidence records; where they describe the solver of their
  day (pre-FGMRES GMRES), that is historical, not the current contract.
- `external/sdirk3_lib/docs_archive_2025_08_16/` — archived early design documents (historical).
  The `external/sdirk3_lib/docs/` design-spec tree referenced by older notes is a local working
  archive and is **not tracked in this repository**.

## Constraints (for contributors)

- The `(j,k,i)` memory layout is correct — never change it.
- Every `.item()` must be inside a `NoGradGuard` scope (it breaks the autograd graph and forces a
  GPU→CPU sync). Prefer AD-safe tensor ops; keep `.item()` off the hot path.
- Protect the computational graph: no stray `.detach()` / `.data` / CPU sync in graph regions; use
  `index_put_` for tensor assignment; no global dtype changes (scoped autocast only).
- Defensive Fortran interface: null checks and dimension validation at every boundary.
