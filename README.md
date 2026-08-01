# AD-SDIRK3

A **differentiable SDIRK3** (3rd-order singly-diagonally-implicit Runge–Kutta) implicit time
integrator built into **WRF v4.7.0**, using **PyTorch / libtorch** with a zero-copy Fortran↔C++
interface and autodiff (JVP and VJP are implemented and contract-tested on the supported paths;
HVP via double-backward is a design goal). The goal is a differentiable dynamical core for
**4D-Var adjoint** modeling.

- **IMEX split** (mode 3 = ARK324L2SA): horizontal/slow terms explicit, vertical acoustic implicit.
- **Matrix-free Newton–Krylov** implicit solve: **FGMRES** (flexible, right-preconditioned — the
  earlier fixed-preconditioner GMRES was replaced during the full-repo review) with Eisenstat–Walker
  adaptive forcing, a vertical preconditioner, and a trust-region fallback. `A·v = v − dt·γ·J·v` is
  a JVP; the 4D-Var gradient is a VJP.
- **Zero-copy interface:** Fortran `(i,k,j)` column-major maps to C++ `(j,k,i)` row-major with no
  data copy — layout `{nj,nk,ni}`, strides `{ni*nk, ni, 1}`.
- Cross-platform CPU / CUDA / MPS.

## Status

The model **builds and runs** (`main/wrf.exe`, `main/ideal.exe`,
`external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`). The stock-RK3 baseline is validated.
The differentiable implicit solve converges at small timesteps; making it converge and remain
stable at the **operational timestep dt=600** on `em_b_wave` is the active investigation, and
it is **unresolved**.

Verification is an **exact 37-test CTest inventory** pinned by
`.github/ci/expected_ctest_names.txt`, plus a numerical fingerprint that hashes the
deterministic solver-diagnostic and RHS-digest streams so behaviour-preserving changes can be
proven byte-identical.

### What is measured

- **Base-state EOS and hydrostatic pressure.** WRF's Exner form
  `alpha = (R_d/p0)*theta*(p/p0)^(-cv/cp)` is a single authority, contract-tested forward *and*
  in its tangent. The pressure integrator's eta orientation is pinned against WRF's own algebra
  by a contract that calls the production helper.
- **Vertical metric.** One fail-close policy across every source; no eps substitution, no
  cross-stagger `rdn`→`rdnw` fallback, no length padding. Verified on a *stretched* eta grid,
  where the staggers differ by 5e-01 — on a uniform grid they agree to 0, which is where this
  class of defect hides.
- **Well-balancedness.** The assembled production RHS returns **exactly zero** in every channel
  and every `RhsMode` at zero perturbation, paired with a non-zero control so the measurement
  cannot be confused with a dead probe.
- **The RHS Jacobian shows nothing anomalous at the first RHS base point.** Its implicit part is
  state-invariant to six digits (predictably: the coefficient is `mu`, which moves 0.01% between
  rest and jet); its explicit part is proportional to base-state amplitude with a measured
  power-law exponent of **1.00031**, which is what a bilinear advective operator must do. The
  leading singular direction is the acoustic `w` mode.
- **AD.** Forward-mode duals, reverse-mode VJPs and the `<Jv,w> == <v,J^T w>` identity hold on
  the EOS and the pressure integrator; the reverse pass runs through the whole production RHS.
  Production `J_FD` and `J_AD^T` agree to **1.198e-06**.

### What is NOT measured, and matters

- **The full timestep map `DG` is unmeasured.** All Jacobian analysis above is of the RHS `F`.
  Stability is governed by `DG`, which additionally contains the ARK stage composition, the
  implicit resolvents and the acoustic substep maps. `DF` behaving normally does **not** bound
  `DG`.
- **Implicit-stage differentiation is an algebra contract, not production.** The implicit
  function theorem forms (`dK/dU = A^-1 J`, adjoint `J^T A^-T`) are pinned against closed-form
  Jacobians but are **not wired** into the stage solve.
- **The 4D-Var adjoint replay does not converge.** Localised: the RHS is differentiable and the
  transpose operator is correct (rel 1.198e-06), but the transpose solve runs with an *identity*
  preconditioner and stalls at rel_error ~0.997 at every dt from 600 down to 20.

  The vertical preconditioner is not Euclidean self-adjoint in one probe (rel 2.18e-02) and is
  exactly linear (`M(2v) - 2M(v)` = 0). It is **not** established that `M^-T` is required: for
  an *equation-level* `A^T` solve any adequate preconditioner will do, since convergence is
  judged on the true residual `|b - A^T lambda| / |b|`. The order to try is (i) frozen existing
  `M^-1` as a general right preconditioner for `A^T`, (ii) flexible per-iteration
  preconditioning, and only then (iii) a genuine `M^-T`. An exact `M^-T` is needed for an
  *algorithmic* transpose pairing, which is not what this path requires.

  The self-adjointness probe also calls a stateful `apply()` twice in sequence, so it cannot yet
  separate true asymmetry from state change between calls; a repeatability and call-order
  contract is needed before the 2.18e-02 is read as a property of the operator.
- **No multi-step well-balancedness, geostrophic/thermal-wind balance, mass/energy/PV budgets,
  or formal temporal-order verification.**
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
  (`MPI_Halo_Contract_np{1,2,4}` + `MPI_Runtime_Contract_np{1,2,4}` in the 37-test CTest suite).
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
