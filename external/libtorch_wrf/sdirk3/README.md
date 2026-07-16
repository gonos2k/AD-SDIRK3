# WRF-SDIRK3 differentiable core (libtorch)

The differentiable SDIRK3 implicit time integrator for WRF v4.7.0: a matrix-free
Newton–Krylov solve built on PyTorch / libtorch C++ with a zero-copy Fortran↔C++
interface. **Research code** — the supported single-rank path builds and runs
production WRF (`em_b_wave`); convergence at the operational timestep is an
active investigation (see the repository root `README.md` and `doc/`).

## Overview

- **Newton–Krylov solver** with Eisenstat–Walker forcing and a trust-region
  fallback (`wrf_sdirk3_newton_solver.cpp`).
- **FGMRES** (flexible, right-preconditioned) as the linear solver. The earlier
  fixed-preconditioner GMRES was replaced during the full-repo review; the
  legacy `wrf_sdirk3_gmres_fixed.h` / `wrf_sdirk3_gmres_ad_safe.h` headers
  remain in-tree for history but the production Krylov loop is the FGMRES
  implementation inside the Newton solver.
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
- Cross-platform CPU / CUDA / MPS.

## Key files

| File | Role |
|---|---|
| `wrf_sdirk3_newton_solver.cpp` | Newton–Krylov + FGMRES + solver diagnostics |
| `wrf_sdirk3_tile_unified_impl.cpp` | Unified RHS, tile parallelization, ARK324 stage loop, HEVI split |
| `wrf_sdirk3_unified_preconditioner.cpp` | Vertical preconditioner (M) |
| `wrf_sdirk3_config.h` | Config knobs, `effective_imex_split_mode()` |
| `wrf_sdirk3_jvp_autograd.{cpp,h}`, `wrf_sdirk3_jvp_fwad_or_fd.h` | JVP (forward-mode dual + counted FD fallback) |
| `wrf_sdirk3_interface_zerocopy.cpp` | Struct-based zero-copy C ABI for the Fortran bridge |
| `wrf_sdirk3_halo_exchange.cpp`, `wrf_sdirk3_ad_halo_exchange.cpp` | MPI halo primitive (forward + adjoint) with lifecycle/freshness contracts |
| `wrf_sdirk3_mpi_safety.h`, `wrf_sdirk3_mpi_safety_impl.cpp` | MPI fail-close contracts: baseline thread, single-flight scope, freshness guard |
| `jvp_bridge.F90` | Fortran↔C++ AD bridge |

The production archive is `libwrf_sdirk3_libtorch.a` (exact 21-TU manifest in
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
ctest --test-dir build/sdirk3 --output-on-failure
```

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
| Stage-2 Krylov restarts | `sdirk3_stage2_max_krylov_restarts` | `WRF_SDIRK3_STAGE2_MAX_KRYLOV_RESTARTS` |
| Stage-2 Krylov tolerance | `sdirk3_stage2_krylov_tol` | `WRF_SDIRK3_STAGE2_KRYLOV_TOL` |

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

For long windows, use `retain_graph_for_adjoint = .false.` with trajectory/checkpoint
replay (`save_trajectory`, `checkpoint_interval`). Retaining the full graph is intended
for short debug windows only.

When observation-aware replay is enabled, enforce endpoint semantics:
- `x0` (window-start state) must be present for replay-enabled windows.
- `xN` is terminal-state input for `lambda_T` assembly and must not add an extra replay step.

## Testing

The CMake tree registers an **exact 15-test CTest inventory** (pinned by
`.github/ci/expected_ctest_names.txt`; any drift fails hosted CI):

- 9 core contracts (geometry matrix, MSF stats, VJP semantics, FGMRES
  contract, WRMS gate metric, acoustic-substep AD, core manifest/archive/link
  parity),
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
