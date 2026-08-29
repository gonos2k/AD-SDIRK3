# Dead-header removal — three independent confirmations

**39 headers, 11,200 lines**, removed from `external/libtorch_wrf/sdirk3/`.
**Independent review: NOT RUN.**

## Why they are dead

Not one is `#include`d by any source in the tree — production, tests, or the WRF side — in any
include form. 38 of the 39 have not been touched since a single commit on **2026-07-05**: they sat
frozen through R10–R13.25, the entire period in which the solver was rebuilt. They are the residue
of an early bulk import; the work since has all been in `newton_solver.cpp` and
`tile_unified_impl.cpp`.

## Three confirmations, and two of them changed the list

**1 — static census.** Every header not included by any `.cpp`/`.h` in the directory or its
`tests/`. First pass: 42.

**2 — build-system and cross-tree references.** This is where the list first shrank.
`wrf_sdirk3_gmres_fixed.h` is listed in CMake's **install-header candidates**: a public API a
downstream consumer may include, invisible to any include scan of this repo. **Kept.** (The same
list names six headers that no longer exist at all — an existence check hides that it is stale.)
Also checked: the Make source manifest, the WRF-side sources (`dyn_em/`, `frame/`, `main/`), and
Fortran/CMake/Python/doc references across the repo — three doc mentions, no code.

**3 — the compiler decides.** Deleted for real and built. The first attempt **failed**:
`tests/test_hydrostatic_balance.cpp` includes `../wrf_sdirk3_hydrostatic_balance.h`, and my scan
matched only bare filenames. Re-running the census on include **basenames** rescued that header
and `wrf_sdirk3_response_probe.h` — two false positives that confirmations 1 and 2 both passed.
Grep supplies evidence; the build supplies the verdict.

## Final verification, with 39 headers deleted

| check | result |
|---|---|
| CMake build of the core | OK |
| **Make** build (the production archive path) | OK |
| ctest | **62/62** |
| Full WRF build (`./compile em_b_wave`) | `wrf.exe`, `ideal.exe`, archive — **0 errors** |
| dt=600 run | `SDIRK3_*` telemetry **byte-identical** |
| ratchets | green (from_blob, item-guard, rule-consumer 97/97) |

No deleted header is named anywhere in the build log.

## Removed


- `sdirk3_solver.h`
- `wrf_adaptive_control.h`
- `wrf_autograd_staggered.h`
- `wrf_flux_form_advection.h`
- `wrf_hybrid_interface.h`
- `wrf_module_jvp_mapping.h`
- `wrf_physics_constraints.h`
- `wrf_physics_jvp_cache.h`
- `wrf_rhs_validator.h`
- `wrf_sdirk3_acoustic_gravity_coupling.h`
- `wrf_sdirk3_ad_safe_impl.h`
- `wrf_sdirk3_advection_ad.h`
- `wrf_sdirk3_compressible_heating.h`
- `wrf_sdirk3_custom_autograd.h`
- `wrf_sdirk3_deformation_ad.h`
- `wrf_sdirk3_dimension_fix.h`
- `wrf_sdirk3_divergence_damping.h`
- `wrf_sdirk3_full_physics.h`
- `wrf_sdirk3_globals.h`
- `wrf_sdirk3_gmres_ad_safe.h`
- `wrf_sdirk3_graph_layers.h`
- `wrf_sdirk3_halo_exchange_optimized.h`
- `wrf_sdirk3_halo_exchange_vectorized.h`
- `wrf_sdirk3_imex_ark324_stepper.h`
- `wrf_sdirk3_jvp_operators.h`
- `wrf_sdirk3_memory_pool_adapter.h`
- `wrf_sdirk3_microphysics_vectorized_portable.h`
- `wrf_sdirk3_newton_krylov_solver.h`
- `wrf_sdirk3_performance_benchmark.h`
- `wrf_sdirk3_preconditioner.h`
- `wrf_sdirk3_preconditioner_utils.h`
- `wrf_sdirk3_pressure_gradient_ad.h`
- `wrf_sdirk3_stability_analysis.h`
- `wrf_sdirk3_terrain_slope.h`
- `wrf_sdirk3_tile_unified_interface.h`
- `wrf_sdirk3_unified_rhs_optimized.h`
- `wrf_sdirk3_wsm6_kslab.h`
- `wrf_sdirk3_zero_copy_view.h`
- `wrf_specialized_preconditioners.h`
