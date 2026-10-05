# WRF-SDIRK3 differentiable core (libtorch)

The differentiable SDIRK3 implicit time integrator for WRF v4.7.0: a matrix-free
Newton–Krylov solve built on PyTorch / libtorch C++ with a zero-copy Fortran↔C++
interface. **Research code** — the supported single-rank path builds and runs
production WRF (`em_b_wave`); convergence at the operational timestep is an
active investigation (see the repository root `README.md` and `doc/`).

## Overview

- **Newton–Krylov solver** with Eisenstat–Walker forcing and a trust-region
  fallback (`wrf_sdirk3_newton_solver.cpp`).
- **FGMRES** (flexible, right-preconditioned) for the production Newton loop.
  The unused fixed-GMRES header has been removed.
- **JVP** via forward-mode autodiff (dual numbers) with an explicit,
  counted finite-difference fallback (`wrf_sdirk3_jvp_autograd.{cpp,h}`,
  `wrf_sdirk3_jvp_fwad_or_fd.h`). The FGMRES matvec is
  `A·v = v − dt·γ·(J·v)`.
- **VJP / adjoint** via reverse-mode autodiff with fail-close semantics on the
  supported paths (identity paths stay unenforced; unsupported configurations
  refuse before touching gradients). HVP via double-backward is a design goal,
  not a shipped contract.
- **Zero-copy interface:** Fortran `(i,k,j)` column-major maps to C++ `(j,k,i)`
  row-major with no data copy — layout `{nj,nk,ni}`, strides `{ni*nk, ni, 1}`.
  This layout is verified — do not change it.
- CPU WRF execution is validated in the documented cases. MPS map-cache
  transfers have separate contract tests; the full MPS RHS remains unsupported
  because of mixed CPU/MPS tensors. CUDA execution is unvalidated in this review.

## Vertical principal preconditioner

The canonical type-2, mode-3 WRF mass-coordinate profile uses a dry vertical
principal approximation in the packed velocity/Phi/theta/mass coordinates.
It binds a complete Newton stage snapshot and derives its pressure/Phi/W
couplings from authoritative hybrid mass and vertical metrics. The Schur solve
and its transpose share the same coefficients. Nonprincipal terms, including
horizontal transport and NH/curvature contributions, remain in the true
Newton operator; the approximation does not claim to equal that operator.

`precond_type=2` is the existing default, so selecting this model changes the
preconditioned path. Type 0 disables preconditioning. Explicit legacy tuning
selects the legacy model; ignored type-2 options do not affect selection.
The historical C++/archived-run damping default (0.1) and the WRF Registry
default (0.7) both identify supported default profiles, without changing their
values on the legacy path. Invalid or moist inputs after canonical selection
are rejected. Coefficients and column solves use FP32 on CPU, with results
returned to the input device/dtype. Higher precision inputs therefore use a
mixed precision preconditioner, and convergence is judged on the true Krylov
residual. Full GPU execution and broad performance claims require separate
validation.

## Key files

| File | Role |
|---|---|
| `wrf_sdirk3_newton_solver.cpp` | Newton–Krylov + FGMRES + solver diagnostics |
| `wrf_sdirk3_tile_unified_impl.cpp` | Unified RHS, tile parallelization, ARK324 stage loop, HEVI split |
| `wrf_sdirk3_unified_preconditioner.cpp` | Vertical preconditioner (M) |
| `wrf_sdirk3_config.h` | Config knobs, `effective_imex_split_mode()` |
| `wrf_sdirk3_jvp_autograd.{cpp,h}`, `wrf_sdirk3_jvp_fwad_or_fd.h` | JVP (forward-mode dual + counted FD fallback) |
| `wrf_sdirk3_interface_zerocopy.cpp` | Struct-based C ABI for the Fortran bridge. NOTE: the *base-state* path materialises OWNED contiguous per-tile snapshots via `.contiguous()` — the `zerocopy` in the symbol name is historical and does not describe it |
| `wrf_sdirk3_halo_exchange.cpp`, `wrf_sdirk3_ad_halo_exchange.cpp` | MPI halo primitive (forward + adjoint) with lifecycle/freshness contracts |
| `wrf_sdirk3_mpi_safety.h`, `wrf_sdirk3_mpi_safety_impl.cpp` | MPI fail-close contracts: baseline thread, single-flight scope, freshness guard |
| `jvp_bridge.F90` | Fortran↔C++ AD bridge |

The production archive is `libwrf_sdirk3_libtorch.a` (authoritative source manifest in
`wrf_sdirk3_core_sources.txt`, enforced by `tests/check_core_archive.sh`). The
sole Fortran bridge is `dyn_em/module_implicit_sdirk3.F` — the dormant
`module_implicit_sdirk3_zerocopy.F` duplicate was removed and a build contract
keeps it from resurfacing.

## Build

This directory is built as part of the WRF top-level build — never `make` here
by hand for production:

```bash
# from the repository root
printf '37\n1\n' | ./configure     # machine option, nesting (read from STDIN)
./compile -j 4 em_b_wave
```

For the standalone test suite, configure the CMake tree against a libtorch
install:

```bash
cmake -S external/libtorch_wrf/sdirk3 -B build/sdirk3 -G Ninja \
      -DCMAKE_PREFIX_PATH=/path/to/torch
cmake --build build/sdirk3 --parallel
FC=/absolute/path/to/gfortran ctest --test-dir build/sdirk3 --output-on-failure
```

The C++ core is built with the C++ compiler selected by CMake. The full CTest
suite also runs source-extracted Fortran oracles, which compile WRF routine
bodies at test time and require GNU Fortran (`gfortran`). Set `FC` to the
absolute path of a GNU Fortran executable when running CTest; these oracles use
GNU compiler flags including `-cpp` and `-fdefault-real-8`.
The source-derived wave energy and spatial-convergence checks use NumPy 2.2.6
and SciPy 1.15.3, pinned with the CPU Torch dependency in
`.github/ci/requirements-core.txt`.

## Runtime Configuration: Namelist First (WRF)

SDIRK3 runtime knobs that were frequently set via environment variables are now
available in `&dynamics` namelist entries. The recommended workflow is:

1. Set values in `namelist.input`.
2. Keep environment variables unset for reproducible runs.
3. Use environment variables only for temporary overrides.

Current load order is:

1. `namelist.input` (`load_from_namelist`)
2. environment (`load_from_env`, override)

So if both are set, environment still wins.

New knobs must be **opt-in** (default = no behavior change) and fully wired
through Registry + Fortran `set_config` + C++ (env, string setter, dump,
`[CONFIG EFFECTIVE]`, `validate()`).

### Moved/standardized controls

| Purpose | Namelist key (`&dynamics`) | Legacy env var |
|---|---|---|
| IMEX split path | `sdirk3_imex_split_mode` | `WRF_SDIRK3_IMEX_SPLIT_MODE` |
| Include slow term in tangent | `sdirk3_imex_slow_in_tangent` | `WRF_SDIRK3_IMEX_SLOW_IN_TANGENT` |
| Include physics term in tangent | `sdirk3_imex_phys_in_tangent` | `WRF_SDIRK3_IMEX_PHYS_IN_TANGENT` |
| Stage-1 explicit bypass | `sdirk3_stage1_explicit` | `WRF_SDIRK3_STAGE1_EXPLICIT` |
| Stage-3 warmstart | `sdirk3_stage3_warmstart` | `WRF_SDIRK3_STAGE3_WARMSTART` |
| Retain autograd graph (4DVAR debug) | `sdirk3_retain_graph_for_adjoint` | `WRF_SDIRK3_RETAIN_GRAPH_FOR_ADJOINT`, `WRF_SDIRK3_RETAIN_GRAPH` |
| Observation-aware 4DVAR toggle | `sdirk3_obs_aware_4dvar` | `WRF_SDIRK3_OBS_AWARE_4DVAR` |
| Observation payload source mode | `sdirk3_obs_source_mode` | `WRF_SDIRK3_OBS_SOURCE_MODE` |
| 4DVAR window endpoint sync mode | `sdirk3_obs_window_sync_mode` | `WRF_SDIRK3_OBS_WINDOW_SYNC_MODE` |
| Stage-2 GMRES restart | `sdirk3_stage2_gmres_restart` | `WRF_SDIRK3_STAGE2_GMRES_RESTART` |
| Stage-2 rejected-trial snapshot | `sdirk3_stage2_rejection_snapshot_diag` | `WRF_SDIRK3_STAGE2_REJECTION_SNAPSHOT_DIAG` |
| Internal FP64 ARK state and RHS | `sdirk3_internal_fp64` | `WRF_SDIRK3_INTERNAL_FP64` |
| Carry FP64 prognostic state across forward steps | `sdirk3_internal_fp64_state_carry` | `WRF_SDIRK3_INTERNAL_FP64_STATE_CARRY` |
| Stage-2 Krylov restarts | `sdirk3_stage2_max_krylov_restarts` | `WRF_SDIRK3_STAGE2_MAX_KRYLOV_RESTARTS` |
| Stage-2 Krylov tolerance | `sdirk3_stage2_krylov_tol` | `WRF_SDIRK3_STAGE2_KRYLOV_TOL` |
| W-damping activation (WRF parity) | `w_damping` (standard WRF key) | `WRF_SDIRK3_WRF_W_DAMPING` |
| IEVA / implicit vertical adv (WRF parity) | `zadvect_implicit` (standard WRF key) | `WRF_SDIRK3_WRF_ZADVECT_IMPLICIT` |
| W-damping critical CFL (WRF parity) | `w_crit_cfl` (standard WRF key; wired as `wrf_w_crit_cfl` — a separate field from the legacy sdirk3 knob) | `WRF_SDIRK3_WRF_W_CRIT_CFL` |

`sdirk3_internal_fp64` defaults off. It promotes the packed FP32 WRF input before
ARK stage assembly, retains FP64 stage states and Newton unknowns through the
RHS, and converts the completed step back to FP32 at the Fortran boundary.
The current implementation accepts ARK mode 3 on one CPU tile covering the
domain. Validation so far covers the dry, fixed-coefficient `em_b_wave` case;
it does not establish MPI, moist, variable-coefficient, or operational accuracy.

The separate `sdirk3_internal_fp64_state_carry` switch also defaults off. It
requires `sdirk3_internal_fp64 = .true.` and effective ARK mode 3; split-explicit
mode is excluded. Retained adjoints are permitted only while a fixed trajectory
is open; they record independent local step graphs and compose their pullbacks
over accepted FP64 checkpoints. A caller may provide the initial FP64 packed
checkpoint to `beginFixedTrajectory` or the deferred `requestFixedTrajectory`;
otherwise the first packed FP32 state is
promoted before the first local input leaf is created.
WRF initialization rejects it
when `mp_physics` is nonzero. The forward path is limited to one CPU tile and
rank, fixed timestep and auxiliary inputs, dry moisture corrections, zero external
tendencies, and supported fixed boundaries. It reuses a completed FP64 state
only when the next FP32 publication, host timestep, and RHS-context fingerprint
match; changed inputs fail closed. The fixed-trajectory profile retains its
legacy no-diffusion case and also admits dry fixed-K option-2 carry. These local
graphs cover short trajectories; they do not provide bounded-memory checkpoint
replay for long runs.
The C/Fortran pullback ABI keeps FP32 buffers: its terminal cotangent is promoted
once before the FP64 reverse chain and the initial gradient is narrowed once on
return. `FP64_Carry_Adjoint` checks native NH/curvature trajectories of two and
four steps, internal checkpoint equality, W/PH/MU objective derivatives, and the
diffusion ON-minus-OFF derivative with a separate signal-relative budget.

`FP64_Initial_Inverse` runs the same executable with `--inverse`: a six-control
W/PH/MU twin experiment with 105 independent terminal W observations. It uses
the carried FP64 forward and adjoint in an actual Armijo optimization, checks
observational rank against two-width FD uncertainty, and compares the result
with an independent regularized linear MAP reference. This short native example
does not qualify a full WRF observation window or real-atmosphere assimilation.

`FP64_Two_Time_Inverse` (`--two-times`) uses the same six controls and observations
at accepted steps 2 and 4. Each independent W sample has fixed physical error
standard deviation `sqrt(105)*0.001 m/s`, preserving the previous terminal cost;
additional observations are summed without a new count/time mean. The vector
`pullbackFixedTrajectory` overload takes cotangents for outputs Z1...ZN and adds
each immediately before its local pullback. Undefined entries mean zero; the
terminal-only overload remains compatible. The example checks temporal-placement
falsifiers, fixed-R information gain and weak MU posterior variance, then actually
minimizes the two-time cost. Only explicit physical rejection or reported ordinary
non-convergence can backtrack; fatal/contract/adjoint errors propagate.

`FP64_Nonlinear_Inverse` (`--nonlinear`) keeps that six-control, two-time
fixed-R problem and increases the truth increment, bounded initially by 10 m/s
in W, 20% of dry column mass and 50% of layer geopotential thickness. These
are initial perturbation bounds, not bounds on the evolved trajectory. A dense
inverse-BFGS search uses the actual adjoint gradient and positive curvature;
the weak experiment retains its fixed background metric. The strong case must
exercise Armijo backtracking and converge within the unchanged 30-update limit.
It checks a nonlinear-point directional derivative, deterministic replay and a
tighter Newton solve, and separates its nonlinear MAP from the independent
background-linear MAP relative to FD, roundoff and solve sensitivity estimates.
These estimates are engineering comparisons, not rigorous error bounds. This
large, unbalanced dry twin tests optimizer coupling; it does not validate realistic
balanced meteorological initial conditions or a full WRF assimilation window.

`FP64_Balanced_Inverse` (`--balanced`) first checks a nonzero horizontally
uniform dry hydrostatic background: theta=310 K, dry column mass=84000 Pa,
zero winds and an EOS-consistent geopotential perturbation. For this uniform
sigma fixture, prescribed pressure perturbations are `eta_mid*4000 Pa`; native
EOS inversion supplies the layer geopotential increments. The example requires
native pressure/density consistency, small Full RHS, Full=Explicit+Implicit and
four-step stationarity before reusing the six-control, fixed-R two-time inverse.
The truth contains dynamical W/PH/MU perturbations; it is not an equilibrium.
This rest-state gate does not cover terrain, general hybrid coefficients,
geostrophic balance or realistic WRF initial-condition assimilation.

The parity W-damping STRENGTH is WRF's module constant `w_alpha = 0.3`
(`share/module_model_constants.F:88`), fixed as `kWrfWAlpha` in
`wrf_sdirk3_w_damping.h` — WRF exposes no namelist for it, so neither do we.
The legacy `sdirk3_w_damp_alpha` knob is a NON-PARITY tuning input: it no
longer feeds the parity RHS term. Since PR 9D it no longer feeds any physical
preconditioner diagonal either (see below).

**W-damping preconditioner policy (PR 9D)**: the WRF-parity W-damping term's
smooth-region tangent flows through `ww`/`mu` (`calc_ww_cp` → rw tendency);
the physical `w` enters only through the hard SIGN, whose derivative is 0 away
from `w == 0` (proven by `WDamp_Tangent_Contract`'s pure-`w` case, which
measures an exactly zero production tangent). The term therefore has **no
direct W-diagonal Jacobian** in the smooth region, so the preconditioner's
physical W direct diagonal is **always 0** — an enabled RHS W-damping term is
never mirrored as a scalar onto the W diagonal. The only scalar W diagonal is
`precond_extra_wdamp`, which **is an explicit, deliberately non-operator
regularization. It does not mirror the WRF W-damping Jacobian.** It is gated
solely on `precond_extra_wdamp` (independent of `implicit_wdamp`,
`precond_match_rhs`, or IMEX scope) and consumes `w_damp_alpha` only when set;
config dumps report `source=extra_regularization` when it is active. The real
smooth Jacobian is the `u/v/mu → rw` cross-block, which a 1-D W diagonal
cannot represent and a scalar `alpha` does not approximate — implementing that
cross-block preconditioner is separate, out-of-scope design work.

The enabled parity path requires the complete `calc_ww_cp` geometry
(`c1h/c2h`, `msftx`, `msfuy`, `msfvx`) AND a supported lateral-boundary
policy per axis (periodic wrap, or symmetric replicate; open/specified/
nested boundaries and multi-rank internal seams have no authoritative mass
halo and are unsupported). Any violation is **FATAL**: the run terminates
through the ABI fatal boundary with a stable marker at the front of the
error — `SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED` for geometry/boundary
support, `SDIRK3_WDAMP_INVALID_INPUT` / `SDIRK3_WDAMP_INVALID_MASS` for
contract-violating inputs. The term is never silently skipped and never
computed with substituted unit metrics: integrating on without the physics
the namelist requested would be fail-open.

**Fatal mechanism (PR 9C.2, measured)**: the mpif90-linked `wrf.exe` cannot
unwind C++ exceptions at all (a same-function try/catch still terminates —
see `wrf_sdirk3_contract_fail.h`), so in production every W-damping contract
violation routes to the installed controlled-abort handler:
`SDIRK3_C_ABI_EXCEPTION` + the specific WDAMP marker to stderr, flush,
coordinated `MPI_Abort` on multi-rank, abort. Never an uncaught-exception
`std::terminate`. The runtime contract (topology + boundary policy) is
settled ONCE per step at `unifiedStep` entry, before any Newton callback
exists: multi-rank patches, internal tiles (tile ≠ rank patch), and
open/specified/nested/polar boundaries all refuse there, with open flags
taking priority over any conflicting periodic/symmetric flag.

`WRF_SDIRK3_WDAMP_FAULT_INJECT` (env-only, test-only, default OFF; values
`mass` | `rdnw`) poisons the corresponding W-damping operand on the enabled
path so the integration negatives can prove the controlled fatal routing on
dynamic-state violations; absent env leaves every operand untouched.

### Example (`imex_split_mode=3`, `stage2_budget=8/1/0`)

```fortran
&dynamics
 sdirk3_imex_split_mode            = 3,
 sdirk3_stage2_gmres_restart       = 8,
 sdirk3_stage2_max_krylov_restarts = 1,
 sdirk3_stage2_krylov_tol          = 0.0,
 sdirk3_stage1_explicit            = .false.,
 sdirk3_stage3_warmstart           = .false.,
 sdirk3_retain_graph_for_adjoint   = .false.,
 sdirk3_obs_aware_4dvar            = .false.,
 sdirk3_obs_source_mode            = 0,
 sdirk3_obs_window_sync_mode       = 0,
/
```

### 4DVAR operation note

`save_trajectory` retains sampled stage-1 states. The legacy replay applies an
implicit-only transpose at those states; it is not the derivative of the full ARK
trajectory. With `retain_graph_for_adjoint = .true.`, the supported dry, single-tile
mode-3 path exposes the last completed tile-step pullback. For fixed native CPU
trajectories, call `beginFixedTrajectory(N, schedule)` before the first step,
`pullbackFixedTrajectory(cotangent)` after N accepted steps, then
`closeFixedTrajectory()`. The checked NH/curvature profile requires fixed inputs
and zero projected physics forcing; an omitted schedule enforces constant timestep.
Fortran callers can use `sdirk3_begin_fixed_trajectory`,
`sdirk3_pullback_fixed_trajectory`, and `sdirk3_close_fixed_trajectory`.
Begin copies the request; recording starts after the first owner call publishes
its buffers. Pullback uses the native packed Float32 layout and preserves the
caller's output on error. These wrappers do not differentiate caller-side maps.
The full Fortran/MPI and observation-window adjoint remain outside this API.

When observation-aware replay is enabled, enforce endpoint semantics:
- `x0` (window-start state) must be present for replay-enabled windows.
- `xN` is terminal-state input for `lambda_T` assembly and must not add an extra replay step.

## Testing

The CMake tree registers an **exact 126-test CTest inventory**, pinned by
`.github/ci/expected_ctest_names.txt`. The breakdown below groups the tests;
the pinned file defines the inventory.

- core contracts (geometry matrix, MSF stats and full-reset publication, VJP semantics, FGMRES
  contract, WRMS gate metric, acoustic-substep AD, the W-damping forward-mode
  tangent contract, the rw term-capture safety contract, the WRF W-damping reference contract, the calc_ww_cp state-to-omega contract, the W-damping operator/preconditioner policy contract, the stage-operand decomposition contract, core
  manifest/archive/link parity),
- `FP64_State_Handoff_Contract` — autonomous ARK324 tile comparison of FP32
  inter-step publication and continuous FP64 state, with first-step identity,
  global self-convergence, one-step/two-half-step checks, and a fixed-state
  Full/ExplicitOnly/ImplicitOnly RHS dt-invariance check. It excludes
  Fortran-side updates, physical boundary refresh and MPI.
- `FP64_Window_Forecast` — the existing six-control, fixed-R inverse at
  8/16 accepted steps, with unused 3/6 s predictions, full-owned-cell W/PH/MU/
  theta diagnostics, and dry-mass/weighted-theta endpoint budgets.
- `FP64_Fixed_Data_Refinement` — one observation dataset at 2/4 s and fixed R
  compared at h=.25/.125 s, including the inverse solution, 6 s forecast and
  tighter-solve controls. The withheld endpoint difference is split into a
  same-control integration component and a reoptimization component; their sum
  is not labelled pure time error. Two time increments do not establish a
  convergence order.
- `FP64_Stable_Column_Inverse` — native EOS-consistent theta=310/312/314/316 K
  sigma column (Kh=1000, Kv=0), positive discrete N², sixteen-step equilibrium,
  initial coordinate-correct buoyancy response and the same inverse/forecast.
  These are short dry single-tile algorithm twins, not operational forecast skill.
- `FP64_Stable_Wave_Forward` — a source-derived four-layer stable gravity mode
  with `phi_adv_z=2`, compared with a one-period native forecast for phase,
  amplitude, and kinetic/available-potential exchange. The test does not assert
  closed full-energy conservation.
- `FP64_Stable_Wave_Inverse` — a two-time modal amplitude/quadrature inverse,
  directional finite-difference check, tighter Newton check, and withheld
  forecast against the source-derived linear reference.
- `FP64_Stable_Wave_Two_Mode_Inverse` — a four-control inverse for two
  independently selected source-derived stable modes, with a source-matrix
  observation-rank check and a 600-second withheld forecast scored at every
  physical W observation point. This remains a dry single-tile synthetic twin.
- `Wave_Physical_Energy_Budget` — source-derived four- and eight-layer wave
  energy, top-boundary work, and pressure/buoyancy coupling contracts. This
  Python check does not run the native solver or establish WRF forecast quality.
- `Wave_Source_Spatial_Convergence` — source-discrete mode tracking under
  horizontal and vertical refinement, compared with a separate continuum
  boundary-value problem. It is a reference-model study, not a native-grid WRF
  convergence or forecast qualification.
- `Horizontal_PGF_Actual_RHS` — actual Full-RHS pressure-gradient checks on
  physical and packed periodic layouts, using a constant-density pressure wave
  set by the dry EOS. It checks every owned U face, including the west seam and
  last interior face.
- `Stable_Wave_Fortran_Source_Rows` — compiled source-extracted WRF Fortran
  pressure/EOS, vertical-force, geopotential, omega, and horizontal-PGF rows;
  this is a row oracle, not a full coupled-wave forecast.

Internal FP64 stage quality compares WRMS growth against the larger of the
initial defect and stage-equation construction precision. The Newton tolerance
and growth cap are unchanged. A converged roundoff-limited root is retained
without legacy post-solve damping; resolved post-solve damping remains unsupported
by the retained converged-stage adjoint and fails explicitly.

- `MPI_Halo_Contract_np{1,2,4}` — halo primitive forward/adjoint/packed AD+BC
  transpose matrices,
- `MPI_Runtime_Contract_np{1,2,4}` — runtime fail-close contracts (baseline
  thread, single-flight scope, checked communicator/prepare, freshness
  lifecycle, AD lifecycle-epoch binding, field semantics) with exact
  failure-reason markers and a per-section coverage ratchet.

The decomposition fail-close matrix (`.github/ci/run_decomposition_matrix.sh`,
4 cases) runs against a real WRF build; its current evidence was produced by
**direct local-machine execution** (it is not a full-WRF decomposition
validation and contains no stock-RK3 baseline — that baseline needs a separate
non-`USE_SDIRK3` build and is deferred).

### Stage convergence diagnostics (opt-in)

`WRF_SDIRK3_STAGE_DIAG=1` enables the machine-readable
`SDIRK3_NEWTON_DIAG` / `SDIRK3_FGMRES_DIAG` / `SDIRK3_STAGE_DIAG` records
(identical format for every implicit stage — internal ARK324 stages 2/3/4 in
IMEX mode 3). When unset, every site is a single cached-boolean branch and
production output and numerics are unchanged (verified: split-path stage norms
bit-identical with the flag on and off).

> **Cost warning:** `WRF_SDIRK3_STAGE_DIAG=1` is a diagnosis-only mode. On
> CUDA/MPS it performs synchronous device-to-host scalar reads (norms and
> finiteness checks) every Newton iteration and can substantially reduce
> performance. It must not be used for throughput or timing measurements.

### Stage-4 JVP / operator directional consistency check (opt-in)

`WRF_SDIRK3_STAGE4_JVP_CHECK=1` runs a shadow directional-consistency check of
the production linearization at the ACTUAL operands of the implicit solves
(stage 4 Newton iterations 0/1, plus stage 3 iteration 0 as a positive
control), emitting machine-readable `SDIRK3_STAGE4_JVP_DIAG` records. Two
layers are verified independently against central finite differences over a
relative epsilon ladder (1e-2 … 1e-5):

- `operator=J` — the production JVP of the RHS at `U_eval` (extracted from
  the production matvec) vs `[F(U+eps*v) - F(U-eps*v)] / (2*eps)`;
- `operator=A` — the composed operator FGMRES actually iterated (including
  the S/S⁻¹ block conjugation, packed layout, and the dt·gamma factor) vs a
  central FD of the assembled Newton residual
  `R(K') = K' - F(U_stage + dt*gamma*K')` in the same coordinate frame.

Directions: the actual Arnoldi basis `V_j` and preconditioned basis
`Z_j = M_j⁻¹V_j` captured from the failing solve (first and last), the
returned correction `dK`, and a fixed deterministic block-balanced probe.
`rel_err = ‖prod−fd‖ / max(‖prod‖, ‖fd‖)`.

Record semantics (PR 9B evidence strengthening):

- **Per-block full ladders**: global AND every block (`ru/rv/rw/ph/t/mu`)
  rows at EVERY epsilon — a block's own best epsilon can differ from the
  global one (the global norm is ru-dominated). `summary=1` rows report the
  per-block best epsilon and best rel_err.
- **`fd=central|plus|minus|richardson`**: one-sided FDs discriminate
  nonsmooth/branch points (`plus`≠`minus` limits mean fwAD must not be
  judged wrong outright); `richardson` extrapolates consecutive central
  pairs.
- **`source=replay|actual`**: `replay` rows re-apply the production
  operators post-solve at the same state/direction; `actual` rows compare
  the in-situ `A_Z`/`J_w` captured inside the live Arnoldi loop.
  `fd=replay_vs_actual` rows measure drift between the two (route-lock /
  cache effects).
- **`purity=1` rows**: repeated + order-swapped evaluations of identical
  inputs, run BEFORE any FD ladder. The gate is fail-close: if any pair
  differs beyond `10*FLT_EPSILON`, a `purity_gate=failed` record is emitted
  and the checker refuses to produce FD verdicts for that checkpoint
  (`skipped=1 reason=shadow_rhs_impure`).

Isolation contract: when unset, the only cost anywhere is a cached-boolean
branch and a null capture pointer (zero extra tensor ops, RHS calls, or
records). When set, the checker calls `compute_rhs` directly on detached
clones (the solver's `jacobian_cache_` bookkeeping is bypassed), never calls
the preconditioner, snapshots/restores the loop-local JVP telemetry counters,
and refuses (with a `skipped=1` record) when `omega_update_ref_per_newton` is
enabled, since the RHS closure then mutates tile state. The same CUDA/MPS
cost warning as above applies — diagnosis only.

## MPI / decomposition support boundary

- **Single MPI rank + supported single-tile path**: production WRF positive
  evidence (`SUCCESS COMPLETE`, six split-explicit stage norms bit-identical
  to the tracked golden `test/em_b_wave/ci_expected_stage_norms.txt`).
- **2/4-rank SDIRK**: refused pre-solve with
  `SDIRK3_MPI_STAGE_HALO_UNSUPPORTED` (guards in `advance_implicit`'s
  prologue, before any communicator/halo state mutation).
- **AD halo + multi-tile**: refused pre-solve with
  `SDIRK3_MPI_MULTI_TILE_UNSUPPORTED`.
- **MPI halo primitive**: verified independently of the solver at np=1/2/4
  (forward, adjoint, packed AD+BC, runtime contracts).
- Halo freshness is a **hard fail-close contract** (publication/consumption
  bound to the halo lifecycle, baseline-thread-only, exact
  `SDIRK3_MPI_HALO_STALE` failures). There is no warning-only freshness mode
  and no `MPI_COMM_WORLD` fallback for the halo communicator.

## Critical constraints

1. The `(j,k,i)` memory layout is CORRECT — do not change it.
2. Every `.item()` must be inside a `NoGradGuard` scope; keep it off the hot
   path. Protect the autograd graph: no stray `.detach()` / `.data` / CPU
   syncs in graph regions; use `index_put_` for tensor assignment; scoped
   autocast only (no global dtype changes).
3. Defensive Fortran interface: null checks and dimension validation at every
   boundary.

## Documentation

Dated evidence and design history live in the repository `doc/` directory and
`external/sdirk3_lib/docs_archive_2025_08_16/` (historical). The
`external/sdirk3_lib/docs/` design-spec tree referenced by older notes is a
local working archive that is **not tracked in this repository**.
