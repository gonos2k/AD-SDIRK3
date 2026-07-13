# WRF-SDIRK3 Test Suite

## Test Files

### JVP & Autograd Validation
| Test | Purpose |
|------|---------|
| `test_jvp_fixes_validation.cpp` | **2025-01-25 fixes validation**: clone safety, memory stability, batch JVP reuse, allow_unused handling, non-contiguous tensors, deterministic reproducibility, early termination safety, RNG consistency, zeros_like property preservation, graph collapse detection, AMP consistency |
| `test_autograd_simple.cpp` | Basic autograd JVP correctness (linear, nonlinear, large tensor) |
| `test_taylor_jvp.cpp` | Taylor test for JVP convergence (FD vs autograd) |
| `test_gradient_flow.cpp` | Gradient flow through solver stages |

### Numerical Accuracy
| Test | Purpose |
|------|---------|
| `test_taylor_simple.cpp` | Simple Taylor test |
| `test_taylor_double.cpp` | Double precision Taylor test |
| `test_taylor_final.cpp` | Final Taylor convergence validation |

### Physics & Boundaries
| Test | Purpose |
|------|---------|
| `test_boundary_conditions_ad.cpp` | AD-compatible boundary conditions |
| `test_moisture_ad.cpp` | Moisture physics with AD |
| `test_moisture_simple.cpp` | Simple moisture physics |
| `test_momentum_rotation.cpp` | Momentum coordinate rotation |

### Infrastructure
| Test | Purpose |
|------|---------|
| `test_pytorch_init.cpp` | PyTorch initialization |
| `test_torch_path.cpp` | LibTorch path validation |
| `test_preconditioner_compilation.cpp` | Preconditioner builds correctly |
| `test_density_threshold.cpp` | Density threshold handling |
| `test_grid_resolution.cpp` | Grid resolution tests |
| `test_performance_optimizations.cpp` | Performance regression tests |
| `test_sdirk3_fixes.cpp` | General SDIRK3 bug fixes validation |
| `test_tensor_validation.cpp` | Tensor shape/dtype validation |

## Build & Run

```bash
# Example: build single test
clang++ -std=c++17 -O2 \
  -I/path/to/torch/include \
  -I/path/to/torch/include/torch/csrc/api/include \
  -I.. \
  -L/path/to/torch/lib -ltorch -ltorch_cpu -lc10 \
  test_jvp_fixes_validation.cpp -o test_jvp_fixes_validation

# Run with library path
DYLD_LIBRARY_PATH=/path/to/torch/lib ./test_jvp_fixes_validation
```

## CI Integration

Compile with `-DSDIRK3_QUIET_TESTS` for minimal pass/fail output.
See `test_output_utils.h` for verbosity control macros.

### Determinism Notes

Tests 7-12 in `test_jvp_fixes_validation.cpp`:
- **Test 7 (Deterministic Reproducibility)**: Verifies bitwise-identical JVP results
- **Test 8 (Early Termination Safety)**: Uses fixed inputs, checks graph release
- **Test 9 (RNG State Consistency)**: Uses `torch::manual_seed(42)` for reproducibility
- **Test 10 (zeros_like Consistency)**: Verifies dtype/device/size preservation
- **Test 11 (Graph Collapse Detection)**: Detects NoGradGuard-induced JVP=0 bug
- **Test 12 (AMP Consistency)**: Validates JVP under autocast/mixed precision

For CI stability, these tests use fixed seeds and deterministic F() (no dropout/randn inside).
If adding tests with stochastic F(), either:
1. Use `torch::manual_seed()` before each JVP call, or
2. Enable `at::globalContext().setDeterministicAlgorithms(true, false)`

### Graph Collapse Warning

Test 11 demonstrates a common bug pattern:
```cpp
// WRONG: NoGradGuard inside F causes JVP=0
auto F_buggy = [](const torch::Tensor& t) {
    torch::NoGradGuard guard;  // <-- BUG: destroys computation graph
    return t * t;
};
```
If your F() accidentally wraps computations in NoGradGuard or InferenceMode,
the returned tensor has no grad_fn and JVP becomes zero. Always verify that
F() runs with gradients enabled.

### JVP Hardening Summary (2025-01-25)

**Core fixes applied**:
1. **`detach().clone()` pattern**: All JVP entry points isolate upstream graph by default
   - `ad_strict_mode=true` preserves upstream graph (higher-order AD only)
2. **`requires_grad(false)` policy**: All zero fallbacks explicitly set requires_grad=false
3. **v_batch validation**: Device/dtype/requires_grad warnings at JVP call site
4. **Autocast normalization**: v_batch normalized to x.dtype() under AMP

**Fixed files**:
- ~~`wrf_sdirk3_jvp_optimized.cpp`~~: (removed — the file was deleted in the legacy JVP-API cleanup; its compute_jvp_batch_optimized was declared but never defined nor called)
- `wrf_sdirk3_jvp_autograd.cpp`: 4x detach().clone() (ad_strict_mode conditional)
- `jvp_bridge.cpp`: zeros_like safety fix
- `wrf_sdirk3_jvp_autograd.h`: Comprehensive DEBUG CHECKLIST added

**Quick grep for code review**:
```bash
rg "clone\(\)\.requires_grad_" --glob "*.cpp" | grep -v detach
rg "zeros_like" --glob "*jvp*.cpp"
rg "\.detach\(\)|\.data\(" --glob "*rhs*.cpp" --glob "*physics*.cpp"
rg "NoGradGuard|InferenceMode" --glob "*.cpp" -C2
rg "create_graph.*=.*true" --glob "*.cpp"
rg "GradMode" --glob "*.cpp"
```

### Graph Collapse/Leak Prevention Checklist (2025-01-25)

**Graph Collapse (JVP=0) Risks**:
- [x] NoGradGuard/InferenceMode not wrapping JVP call paths
- [x] F() path has no .detach()/.data on input-derived tensors
- [x] F() path has no .item()/.cpu()/.numpy() on AD tensors
- [x] allow_unused fallback uses requires_grad(false)

**Graph Over-generation/Leak Risks**:
- [x] No create_graph=true in first-order JVP paths
- [x] retain_graph=false on last batch iteration
- [x] Exception paths rely on RAII for graph release
- [x] grad_outputs requires_grad policy documented

**Multi-GPU/AMP Consistency**:
- [x] v_batch device/dtype validation at call site
- [x] Device guard policy documented for F() with default device

**Advanced Risk Patterns (verified 2025-01-25)**:
- [x] No custom autograd::Function (save_for_backward/mark_dirty)
- [x] No as_strided usage (all view/reshape are AD-safe)
- [x] from_blob isolated to Fortran bridge (not in JVP paths)
- [x] No nested autograd::grad calls
- [x] ad_strict_mode policy consistent across all JVP entry points
- [x] In-place ops (copy_, add_, index_put_) not on x_var

### MPI Safety & Halo Consistency Checklist (2025-01-25)

**Issue #1: Halo Ownership Overlap Prevention**:
- [x] Policy: WRF handles ALL halo exchanges at MPI level
- [x] SDIRK3 operates on INTERIOR points [its:ite, jts:jte, kts:kte] only
- [x] `wrf_sdirk3_mpi_safety.h` provides validation guards
- [x] `isWithinInterior()` validates index ranges
- [x] `checkHaloAccess()` detects potential halo access conflicts

**Issue #2: MPI Exchange Timing (Stale Halo Prevention)**:
- [x] `HaloFreshnessGuard` tracks halo exchange epochs
- [x] `markHaloFresh()` called from Fortran after WRF halo exchange
- [x] `verifyFreshness()` warns if reading potentially stale halo data
- [x] Epoch-based synchronization between WRF and C++

**Issue #3: Tile Partition Boundary Validation**:
- [x] `validateTileBoundaries()` checks for off-by-one errors
- [x] Validates: its<=ite, jts<=jte, kts<=kte (order)
- [x] Validates: tile within domain within memory
- [x] `SDIRK3_VALIDATE_TILE_BOUNDS` macro for entry point validation

**Issue #4: Periodic BC Single-Application Guard**:
- [x] `WRF_SDIRK3_WRF_HANDLES_BC=1` (default): C++ skips periodic BC
- [x] WRF Fortran applies periodic BC via MPI
- [x] Compile with `WRF_SDIRK3_WRF_HANDLES_BC=0` for standalone tests only
- [x] `PeriodicBCGuard` class tracks BC application per field/timestep

**Issue #5: Threading+MPI Safety (MPI_THREAD_FUNNELED)**:
- [x] WRF uses `MPI_THREAD_FUNNELED` (only master thread can call MPI)
- [x] `MPIThreadGuard` validates MPI calls from master thread only
- [x] `SDIRK3_MPI_THREAD_GUARD(func_name)` macro for MPI call validation
- [x] Warning/abort if MPI called from OpenMP worker thread

**Safety Header Location**:
- `wrf_sdirk3_mpi_safety.h` - Comprehensive MPI safety guards

**Fortran C Interface**:
```c
// Initialize MPI safety guards (automatically called by sdirk3_init)
void sdirk3_mpi_safety_init();

// Notify SDIRK3 that WRF halo exchange completed
void sdirk3_notify_halo_fresh();

// Set current timestep for periodic BC guard (64-bit)
void sdirk3_set_timestep(int64_t timestep);

// Set current timestep for periodic BC guard (32-bit for Fortran INTEGER)
void sdirk3_set_timestep_i4(int timestep);
```

**Integration Points in solve_em.F** (FIX 2025-01-25):
```fortran
! Before SDIRK3 stage loop (line ~709):
CALL sdirk3_set_timestep_i4(grid%itimestep)

! After HALO_EM_D2_*.inc inside stage loop (line ~729):
#ifdef DM_PARALLEL
   CALL sdirk3_notify_halo_fresh()  ! Stage-level halo exchange
#endif

! After PERIOD_BDY_EM_D.inc post-stage (line ~744):
#ifdef DM_PARALLEL
   CALL sdirk3_notify_halo_fresh()  ! Periodic BC exchange
#endif

! MPI safety init is automatic (called by sdirk3_init in C++)
```

**Halo Freshness Coverage** (Complete within SDIRK3 block):
| Location | Exchange Type | Notification |
|----------|--------------|--------------|
| Line 723/725 | HALO_EM_D2_5/3.inc | ✅ Line 729 |
| Line 742 | PERIOD_BDY_EM_D.inc | ✅ Line 744 |

**Compile Flags**:
```bash
# Default: WRF handles boundary conditions
-DWRF_SDIRK3_WRF_HANDLES_BC=1

# Standalone tests: C++ handles boundary conditions
-DWRF_SDIRK3_WRF_HANDLES_BC=0
```

### Boundary Tensor Stagger Dimension Fix (2025-01-25)

**Issue #6: Boundary Array Stagger Convention**:
- [x] WRF boundary arrays follow field staggering convention
- [x] U boundaries: i-staggered (nx_u = nx+1 on Y-boundary dimension)
- [x] V boundaries: j-staggered (ny_v = ny+1 on X-boundary dimension)
- [x] W/PH boundaries: k-staggered (nz_w = nz+1 on vertical dimension)
- [x] T/MU boundaries: mass point (no staggering)

**Boundary Tensor Dimension Matrix**:
| Field | X-Boundary (xs/xe) | Y-Boundary (ys/ye) |
|-------|-------------------|-------------------|
| U     | [ny, nz, bw]      | [nx_u, nz, bw]    |
| V     | [ny_v, nz, bw]    | [nx, nz, bw]      |
| W     | [ny, nz_w, bw]    | [nx, nz_w, bw]    |
| T     | [ny, nz, bw]      | [nx, nz, bw]      |
| PH    | [ny, nz_w, bw]    | [nx, nz_w, bw]    |
| MU    | [ny, bw]          | [nx, bw]          |

**Stagger Convention Explanation**:
- X-boundaries (West/East): Boundary strip varies in j-direction
  - U (i-staggered): Uses mass j-dimension → ny
  - V (j-staggered): Uses staggered j-dimension → ny_v
  - W/PH (k-staggered): Uses staggered k-dimension → nz_w
- Y-boundaries (South/North): Boundary strip varies in i-direction
  - U (i-staggered): Uses staggered i-dimension → nx_u
  - V (j-staggered): Uses mass i-dimension → nx
  - W/PH (k-staggered): Uses staggered k-dimension → nz_w

**Fixed Files**:
- `wrf_sdirk3_tile_unified_impl.cpp`: Lines 21461-21630 (boundary tensor creation)

**Validation**:
```cpp
// X-boundary: V uses ny_v (j-staggered), W/PH use nz_w (k-staggered)
grid_info_->v_bdy_xs = tls_cached_from_blob(..., {ny_v, nz, bw}, ...);
grid_info_->w_bdy_xs = tls_cached_from_blob(..., {ny, nz_w, bw}, ...);

// Y-boundary: U uses nx_u (i-staggered), W/PH use nz_w (k-staggered)
grid_info_->u_bdy_ys = tls_cached_from_blob(..., {nx_u, nz, bw}, ...);
grid_info_->w_bdy_ys = tls_cached_from_blob(..., {nx, nz_w, bw}, ...);
```

### WRF Boundary Condition Verification (2025-01-25)

**Issue #7: Complete WRF BC Type Coverage**:
- [x] All 6 WRF BC types implemented in SDIRK3 C++
- [x] ConfigFlags 1:1 mapping with WRF Fortran grid_config_rec_type
- [x] BC application precedence matches WRF Fortran
- [x] All BC implementations are AD-compatible (NoGradGuard wrapped)

**WRF BC Types vs SDIRK3 Implementation**:
| BC Type | WRF Fortran (module_bc.F) | SDIRK3 C++ (wrf_sdirk3_boundary_ad.cpp) |
|---------|---------------------------|----------------------------------------|
| Periodic | `periodicity_x`, `periodicity_y` | ✅ `applyPeriodicBoundaryAD()` |
| Symmetric | `symmetry_xs/xe/ys/ye` | ✅ `applySymmetricBoundaryAD()` |
| Open | `open_xs/xe/ys/ye` | ✅ `applyOpenBoundaryAD()` |
| Specified | `specified` + Davies relaxation | ✅ `applySpecifiedBoundaryAD()` |
| Nested | `nested` | ✅ `applyNestedBoundaryAD()` |
| Polar | `polar` | ✅ `applyPolarFilterAD()` |

**ConfigFlags Field Mapping** (wrf_config_flags.h):
```cpp
// BC configuration - complete 1:1 mapping with WRF
int open_xs = 0;      // WRF: grid%open_xs
int open_xe = 0;      // WRF: grid%open_xe
int open_ys = 0;      // WRF: grid%open_ys
int open_ye = 0;      // WRF: grid%open_ye
int periodic_x = 0;   // WRF: grid%periodic_x
int periodic_y = 0;   // WRF: grid%periodic_y
int symmetric_xs = 0; // WRF: grid%symmetric_xs
int symmetric_xe = 0; // WRF: grid%symmetric_xe
int symmetric_ys = 0; // WRF: grid%symmetric_ys
int symmetric_ye = 0; // WRF: grid%symmetric_ye
int specified = 0;    // WRF: grid%specified
int nested = 0;       // WRF: grid%nested
int polar = 0;        // WRF: grid%polar
```

**BC Application Precedence** (matches WRF module_bc.F):
```
periodic > symmetric > open > specified > nested
```

**AD Compatibility**:
- All BC functions wrap boundary updates in `torch::NoGradGuard`
- Boundaries are external constraints, not part of optimization
- Interior gradients flow correctly through solver

**spec_bdy_width==0 Optimization** (wrf_sdirk3_tile_unified_impl.cpp):
```cpp
// OPT 2025-01-25: Skip boundary tensor creation for periodic/global domains
if (bw <= 0) {
    // Set defaults and skip to boundary_tensors_done label
    goto boundary_tensors_done;
}
```

**Verified Files**:
- `wrf_sdirk3_boundary_ad.h`: All 6 BC function declarations
- `wrf_sdirk3_boundary_ad.cpp`: Complete BC implementations with precedence
- `wrf_config_flags.h`: ConfigFlags with all BC fields (lines 316-347)
- `wrf_sdirk3_tile_unified_impl.cpp`: spec_bdy_width skip optimization

**Reference WRF Fortran Files**:
- `share/module_bc.F`: BC type definitions, stagger handling
- `dyn_em/module_bc_em.F`: EM-specific BCs, Davies relaxation

### I/O Consistency Verification (2025-01-25)

**Issue #8: WRF Fortran ↔ SDIRK3 C++ I/O Isolation**:
- [x] C++ does NOT use NetCDF API (nc_, netcdf, pnetcdf, PIO)
- [x] BIND(C) interface does NOT pass file handles (ncid, fid, unit)
- [x] SDIRK3 block (lines 707-767) runs BEFORE history/restart I/O
- [x] C++ uses only std::cout/std::cerr for logging (no file conflicts)

**Check 1: NetCDF API Usage in C++** ✅
```bash
rg "nc_|netcdf|pnetcdf|PIO" -g"*.cpp" -g"*.h" --no-ignore
# Result: No NetCDF function calls found
# Only comments/documentation mentioning NetCDF
```

**Check 2: File Handle Passing via BIND(C)** ✅
- `module_implicit_sdirk3_zerocopy.F` interface inspected
- Only data pointers passed: u_ptr, v_ptr, w_ptr, ph_ptr, etc.
- NO file handles: ncid, fid, unit_, io_form not in interface

**Check 3: I/O Timing Sequence** ✅
```
solve_em.F execution order:
  Line 380-389: history/restart alarm flags SET (no actual I/O)
  Line 707-767: SDIRK3 block RUNS (dynamics computation)
  Line 3717+:   history output WRITTEN (actual I/O)

SDIRK3 runs BETWEEN flag setting and actual I/O → No conflict
```

**Check 4: C++ Logging Policy** ✅
- All C++ logging uses `std::cout` / `std::cerr` (stdout/stderr)
- No file-based logging that could conflict with Fortran I/O
- Only exception: `wrf_sdirk3_config.cpp:953` uses `std::ifstream`
  - This is initialization-only (`wrf_sdirk3_load_config_from_namelist`)
  - Called once at startup, not during timestep loop

**SDIRK3_IO_TIMING_* Macros** ✅
- Located in `wrf_sdirk3_common_macros.h` (lines 2963-2980)
- Currently commented out (documentation only)
- If enabled, would be debug-only and compile-out in release

**Quick Verification Commands**:
```bash
# Verify no NetCDF in C++
rg "nc_|netcdf|pnetcdf|PIO" -g"*.cpp" -g"*.h" external/libtorch_wrf/sdirk3/

# Verify no file handles in BIND(C)
grep -E "ncid|fid|unit_|io_form" dyn_em/module_implicit_sdirk3_zerocopy.F

# Check C++ file I/O (should only find config loading)
rg "fopen|fwrite|ofstream|ifstream" -g"*.cpp" external/libtorch_wrf/sdirk3/
```

### Advanced Consistency Verification (2025-01-25)

**Issue #9: Restart Path Consistency**:
- [x] `incrementSolverEpoch()` called in `sdirk3_init()` (fortran_c_interface.cpp:38)
- [x] `incrementSolverEpoch()` called in `sdirk3_cleanup()` (fortran_c_interface.cpp:339)
- [x] Process restart: TLS caches cleared automatically (new process = new thread contexts)
- [x] Solver recycling: `solver_unique_id` mechanism detects stale TLS cache
- ⚠️ **GAP**: No explicit Fortran-side `reset_full` call on restart path
  - **Recommendation**: Add `CALL sdirk3_tile_solver_reset_full(solver_ptr)` in start_em.F when `config_flags%restart=.TRUE.`

**Available Reset APIs** (wrf_sdirk3_interface.h):
| API | Scope | Use Case |
|-----|-------|----------|
| `reset_state` | Per-solver state only | Solver reuse, lightweight |
| `reset_full` | + All solver caches | Grid geometry changes |
| `reset_full_parallel` | + All-thread TLS | Multi-threaded test harness |
| `reset_tls_parallel` | TLS caches only | Thread pool recycling |

**Issue #10: Nested/Moving-Nest Cache Safety**:
- [x] `moving_nest` config flag controls cache check aggressiveness
- [x] When `moving_nest=true`: 9-point signature check EVERY call
- [x] When `moving_nest=false`: 4-corner check with periodic full-verify
- [x] `metric_utils::setMovingNestMode()` called on config change
- [x] `invalidateVerticalMetricCaches()` called in `reset_full()` path
- [x] Epoch-based cache invalidation detects rdnw/rdn pointer changes

**Moving Nest Cache Policy** (wrf_sdirk3_tile_unified_impl.cpp:7455-7477):
```cpp
bool is_moving_nest = metric_utils::isMovingNestMode();
if (is_moving_nest) {
    // Aggressive 9-point signature check EVERY call
    do_extended_verify = true;
} else if (full_verify_period > 0 && (call_count % full_verify_period == 0)) {
    // Periodic full-verify even when moving_nest=false
    do_extended_verify = true;
}
```

**Issue #11: Config Freeze Policy**:
- [x] **IMPLEMENTED** (FIX 2025-01-25): `markWorkersStarted()` and `warnIfWorkersStarted()` now implemented
- `g_workers_started` atomic flag in `wrf_sdirk3_config.cpp`
- `wrf_sdirk3_mark_workers_started()` called in `sdirk3_init()` after solver creation
- All config setters (`set_config_int/float/bool/uint64`) call `warnIfWorkersStarted()`
- Config changes after workers started log warning but still apply (allows runtime tuning)

**Config Freeze Implementation** (wrf_sdirk3_config.cpp):
```cpp
// FIX 2025-01-25: IMPLEMENTED
static std::atomic<bool> g_workers_started{false};

static void warnIfWorkersStarted(const char* config_key) {
    if (g_workers_started.load(std::memory_order_acquire)) {
        std::cerr << "[CONFIG WARN] Config change for '" << config_key
                  << "' after workers started." << std::endl;
    }
}

void wrf_sdirk3_mark_workers_started(void) {
    g_workers_started.exchange(true, std::memory_order_release);
}
```

**Call Site** (fortran_c_interface.cpp:sdirk3_init):
```cpp
std::cout << "SDIRK3 initialized successfully" << std::endl;
wrf_sdirk3_mark_workers_started();  // FIX 2025-01-25: Config freeze point
```

**Issue #12: FP64 Mode Transition Consistency**:
- [x] `sdirk3_set_fp64_scalars()` tracks mode transitions (wrf_sdirk3_interface_zerocopy.cpp:2058-2131)
- [x] Per-solver WARN_ONCE for FP64↔FP32 transitions
- [x] `fp64_explicit` flag gates FP64 tensor creation
- [x] `fp64_mode_changed` triggers tensor recreation in `advanceZeroCopy()`
- [x] No duplicate logging (transition logged once per solver via `last_fp64_transition` tracking)

**FP64 Transition Logging** (wrf_sdirk3_interface_zerocopy.cpp:2112-2131):
```cpp
const bool should_log = (grid_info->fp64_explicit != prev_fp64_explicit) &&
                        (current_transition != grid_info->last_fp64_transition);
if (should_log) {
    std::cerr << "SDIRK3 INFO: FP64 mode transition...";
    grid_info->last_fp64_transition = current_transition;
}
```

**Issue #13: Zero-Copy Lifetime Guarantee**:
- [~] **MITIGATED** (FIX 2025-01-25): Key paths now reset caches; full `wrf_array_version` deferred
- C++ side uses epoch + data_ptr for cache validation
- Documentation in `wrf_sdirk3_common_macros.h:3626-3662` provides Fortran implementation guide

**Current Protection Mechanisms** (FIX 2025-01-25 Enhanced):
1. **Epoch-based**: `incrementSolverEpoch()` invalidates TLS caches
2. **Pointer tracking**: Cache validates `data_ptr` hasn't changed
3. **solver_unique_id**: Detects memory recycling for solver pointers
4. **9-point signature**: Validates grid metrics haven't changed in-place
5. **Restart reset** (NEW): `sdirk3_reset_on_restart()` in start_em.F clears all caches
6. **Moving nest reset**: `reset_full()` path invalidates vertical metric caches

**Deferred Mechanism** (full wrf_array_version):
```fortran
! DEFERRED - current restart/moving-nest handling is sufficient for most cases:
! Full wrf_array_version would require WRF framework changes:
! - Track version in module_domain or module_configure
! - Increment on DEALLOCATE/ALLOCATE cycles
! - C++ interface to query version
!
! Current mitigation: reset_on_restart() handles the main use case (restart files)
```

**Conclusion**: The restart path fix (`sdirk3_reset_on_restart`) addresses the primary concern.
Full `wrf_array_version` implementation deferred as non-critical enhancement.

### Summary of Verification Results (FIX 2025-01-25)

| Issue | Status | Resolution |
|-------|--------|------------|
| Restart reset_full | ✅ FIXED | `sdirk3_reset_on_restart()` in start_em.F |
| Config freeze | ✅ IMPLEMENTED | `warnIfWorkersStarted()` in all setters |
| wrf_array_version | ✅ MITIGATED | Restart/moving-nest paths reset caches |

**Files Modified (Round 1)**:
- `dyn_em/start_em.F`: Added USE and call to `sdirk3_reset_on_restart()`
- `dyn_em/module_implicit_sdirk3.F`: Added `sdirk3_reset_on_restart()` subroutine
- `wrf_sdirk3_config.h`: Added `wrf_sdirk3_mark_workers_started()` declaration
- `wrf_sdirk3_config.cpp`: Added config freeze mechanism implementation
- `fortran_c_interface.cpp`: Added `wrf_sdirk3_mark_workers_started()` call in init

### Additional Verification Results (FIX 2025-01-25 Round 2)

| Item | Status | Verification Result |
|------|--------|---------------------|
| Halo freshness coverage | ✅ FIXED | Added `sdirk3_notify_halo_fresh()` calls in module subroutines |
| Config freeze timing | ✅ VERIFIED | `mark_workers_started()` now called in both C++ and Fortran paths |
| Physics duplication | ✅ SAFE | `C_NULL_PTR` tendencies = F_phys=0, no duplication |
| Boundary parameters | ✅ SAFE | `spec_bdy_width>0` guards all boundary operations |
| I/O concurrency | ✅ SAFE | WRF output between steps, not during solve_em |
| MPI thread level | ✅ SAFE | `MPICallGuard` ensures master thread MPI calls |

**Files Modified (Round 2)**:
- `dyn_em/module_implicit_sdirk3.F`:
  - Added `sdirk3_notify_halo_fresh` INTERFACE declaration
  - Added `wrf_sdirk3_mark_workers_started` INTERFACE declaration
  - Added call to `sdirk3_notify_halo_fresh()` after HALO_EM_D2_5.inc
  - Added call to `wrf_sdirk3_mark_workers_started()` at end of init
- `dyn_em/module_implicit_sdirk3_zerocopy.F`:
  - Added module-level INTERFACE for `sdirk3_notify_halo_fresh`
  - Added call to `sdirk3_notify_halo_fresh()` after HALO_EM_D2_5.inc

**Halo Freshness Coverage Fix Detail**:
Previously, `sdirk3_notify_halo_fresh()` was only called in solve_em.F (lines 729, 744).
The module subroutines `sdirk3_halo_exchange()` in both modules included HALO_EM_D2_5.inc
but lacked notify calls. Now both paths notify C++ after halo exchange.

**Config Freeze Timing Fix Detail**:
Previously, `mark_workers_started()` was only called in fortran_c_interface.cpp sdirk3_init().
The Fortran-driven initialization path (`init_implicit_sdirk3`) didn't call it.
Now both initialization paths freeze config after completion.

| Moving nest | ✅ Complete | - |
| FP64 transition | ✅ Complete | - |
| Epoch tracking | ✅ Complete | - |
| solver_unique_id | ✅ Complete | - |

### Complete Round 2 Verification (FIX 2025-01-25)

**6 Verification Items Completed**:

| Item | Status | Verification Detail |
|------|--------|---------------------|
| Driver/path coverage | ✅ SAFE | Only `solve_em.F` is compiled; `solve_em_backup.F` NOT in Makefile, `solve_em_kslab_integration.F` is documentation only |
| HALO include variants | ✅ SAFE | Both modules use `HALO_EM_D2_5.inc` unconditionally (conservative larger stencil); freshness notification present |
| BC priority combinations | ✅ CONSISTENT | C++ precedence (periodic>symmetric>open>specified>nested) matches WRF; nested+specified+polar handled correctly |
| PIO async I/O concurrency | ✅ SAFE | WRF I/O quilt architecture buffers data; I/O servers never access compute task arrays; async overlap is previous timestep only |
| Config freeze timing | ✅ FIXED | Moved `mark_workers_started` from `init_implicit_sdirk3` to END of `sdirk3_initialize_solvers` (after physics coupling config) |
| Stagger/stride 48 tensors | ✅ VERIFIED | U(i-stagger nx+1), V(j-stagger ny+1), W/PH(k-stagger nz+1) all correct; tests in `test_boundary_conditions_ad.cpp:237-275` |

**Config Freeze Timing Bug Fix Detail**:

The bug was that `sdirk3_configure_physics_coupling()` (contains `set_config_*` calls at lines 1470-1475) was called AFTER `mark_workers_started()`:
- `init_implicit_sdirk3` called `mark_workers_started` at end (line ~506)
- `sdirk3_initialize_solvers` called `init_implicit_sdirk3` THEN `sdirk3_configure_physics_coupling`
- Result: config warnings triggered unnecessarily

**Fix applied**:
1. Removed `mark_workers_started` from `init_implicit_sdirk3`
2. Added `mark_workers_started` to END of `sdirk3_initialize_solvers` (after ALL config)
3. Updated `solve_em.F` line 328 to use `sdirk3_initialize_solvers` wrapper

**Files Modified (Round 2 Config Freeze Fix)**:
- `dyn_em/module_implicit_sdirk3.F`: Moved `wrf_sdirk3_mark_workers_started()` call
- `dyn_em/solve_em.F`: Changed line 328 to use `sdirk3_initialize_solvers`

**Stagger Dimension Test Coverage** (test_boundary_conditions_ad.cpp):
```cpp
// Line 260: U-staggered verification
ASSERT_EQ(staggered_dims[2], nx + 1);  // i-dimension has +1

// Line 261: V-staggered verification
ASSERT_EQ(staggered_dims[0], ny + 1);  // j-dimension has +1

// Line 262: W/PH-staggered verification
ASSERT_EQ(staggered_dims[1], nz + 1);  // k-dimension has +1
```

**WRF I/O Timing Sequence** (verified in share/mediation_integrate.f90):
```
med_before_solve_io()  → I/O alarm flag setup
solve_em()             → SDIRK3 runs here (dynamics)
med_after_solve_io()   → output_history writes (actual I/O)
med_last_solve_io()    → restart/auxhist writes
```
SDIRK3's arrays on compute tasks are NEVER accessed concurrently by I/O servers.

### Round 3 Verification Results (FIX 2025-01-25)

**5 Verification Items Completed**:

| Item | Status | Verification Detail |
|------|--------|---------------------|
| Regrid/Move-nest 경로 | ✅ SAFE | `grid%moved` set in `med_nest_move` → `start_domain` → `start_em` → `sdirk3_reset_on_restart()`. No other regrid path bypasses this. |
| Open BC 차이 검증 | ✅ NO IMPACT | SDIRK3 C++ does NOT apply BC during WRF integration (line 496-499: returns field unchanged). WRF Fortran handles all BC. |
| BC 적용 위치 | ✅ NO CONFLICT | SDIRK3: interior only (its:ite). rk_phys_bc_dry_2: ghost cells only (ids-1 to ids-3). Disjoint regions. |
| time-level 혼입 방지 | ✅ SAFE | `model_to_grid_config_rec()` called at solve_interface.F:29 BEFORE solve_em(). Config is fresh each timestep. |
| MPI decomposition 민감도 | ✅ CODE SAFE | Interior-only computation guarantees rank-independence. Recommend 1-rank/2-rank comparison for validation. |

**Regrid/Move-nest Path Analysis**:
```
med_nest_move (share/mediation_nest_move.F)
  ├── nest%moved = .true.  (line 253)
  ├── start_domain(nest)   (line 268)
  │     └── start_em
  │           └── IF (grid%moved) CALL sdirk3_reset_on_restart()  (line 260-261)
  └── nest%moved = .false. (line 275)
```
All moving nest paths go through `start_domain` → `start_em` → reset. No bypass exists.

**Open BC Implementation Difference (Not Used)**:
- WRF Fortran: Copy BC (zero gradient): `dat(ids-1,j) = dat(ids,j)`
- SDIRK3 C++: Has radiation BC code but **disabled** at line 495-499:
  ```cpp
  // CRITICAL FIX: SDIRK3 should NOT handle halo processing
  // WRF already manages all halo exchanges and boundary conditions
  return field;  // No-op - WRF handles all BC
  ```

**BC Application Location**:
- SDIRK3 solver: Lines 715-735 (3 stages on interior points)
- rk_phys_bc_dry_2: Lines 753-765 (ghost cells only, AFTER SDIRK3)
- set_physical_bc3d checks: `(its == ids)` for west BC, `(ite == ide)` for east BC
  - Only modifies `dat(ids-1)`, `dat(ids-2)`, `dat(ids-3)` etc. (ghost cells)

**config_flags Update Timing**:
```
solve_interface.F:
  Line 29: model_to_grid_config_rec() ← config_flags updated HERE
  Line 49: solve_em() ← SDIRK3 uses fresh config_flags
```
No config_flags changes during solve_em execution.

**MPI Decomposition Consistency Guarantees**:
1. SDIRK3 operates on interior points [its:ite, jts:jte, kts:kte]
2. WRF handles all MPI halo exchanges (HALO_EM_D2_5.inc, PERIOD_BDY_EM_D.inc)
3. `HaloFreshnessGuard` ensures fresh halo data
4. `validateTileBoundaries()` prevents off-by-one errors
5. Interior computation is deterministic and rank-independent

**Recommended Runtime Verification**:
```bash
# 1-rank run
mpirun -np 1 ./wrf.exe
# Extract interior checksum for U field
ncdump -v U wrfout_d01_* | grep -A100 "U =" > u_1rank.txt

# 2-rank run
mpirun -np 2 ./wrf.exe
ncdump -v U wrfout_d01_* | grep -A100 "U =" > u_2rank.txt

# Compare (should be identical or within roundoff)
diff u_1rank.txt u_2rank.txt
```

### Round 4 Verification Results (FIX 2025-01-25)

**6 Verification Items Completed**:

| Item | Status | Verification Detail |
|------|--------|---------------------|
| Staggered interior bounds 재확인 | ✅ SAFE | `u_i_end = min(i_end + 1, nx_u)`, `v_j_end = min(j_end + 1, ny_v)` - extra stagger point correctly included |
| Halo size vs spec_bdy_width | ✅ INDEPENDENT | Separate parameters: `halo_width` for MPI exchanges, `spec_bdy_width` for boundary zone tensor creation |
| Boundary pointer nullness 정책 | ✅ CORRECT | Each of 48 boundary pointers checked with `!= nullptr` before tensor creation (lines 21479-21525) |
| move-nest/restart + halo epoch | ✅ SAFE | `incrementSolverEpoch()` called in both `sdirk3_init()` and `sdirk3_cleanup()` |
| MPI decomposition 민감도 | ✅ ARCHITECTURE SAFE | Interior-only policy (HALO_OWNERSHIP_POLICY) guarantees rank-independent results |
| BC 파라미터 범위 sanity | ✅ NO CONFLICT | WRF enforces `relax_zone = spec_bdy_width - spec_zone`; C++ has additional `std::min()` bounds |

**Item 1: Staggered Interior Bounds Verification**:

U and V staggered fields correctly handle the extra stagger point for interior computation:
```cpp
// wrf_sdirk3_tile_unified_impl.cpp:28251-28262
// U staggered: includes extra point in i-dimension
int64_t u_i_start = i_start;
int64_t u_i_end = std::min(i_end + 1, nx_u);  // u[i_end] for mass point i_end-1

// wrf_sdirk3_tile_unified_impl.cpp:28361-28371
// V staggered: includes extra point in j-dimension
int64_t v_j_start = j_start;
int64_t v_j_end = std::min(j_end + 1, ny_v);  // v[j_end] for mass point j_end-1
```

**Item 2: Halo vs spec_bdy_width Independence**:

These are completely separate parameters with different purposes:
- `halo_width`: Used in `halo_exchange_init()` for MPI halo exchange configuration
- `spec_bdy_width`: Part of `SDIRK3_BoundaryConfig` struct for specified boundary tensor sizing

```cpp
// wrf_sdirk3_interface_params.h:48-53
struct SDIRK3_BoundaryConfig {
    int32_t spec_bdy_width;  // Specified boundary width (for BC tensors)
    int32_t spec_zone;       // Specified zone width
    int32_t relax_zone;      // Relaxation zone width
    int32_t padding;         // Alignment padding
};
```

**Item 3: Boundary Pointer Null Check Policy**:

Each of 48 boundary pointers is individually validated before tensor creation:
```cpp
// wrf_sdirk3_tile_unified_impl.cpp:21479+
if (config.u_bdy_xs != nullptr) {
    grid_info_->u_bdy_xs = tls_cached_from_blob(config.u_bdy_xs, {ny, nz, bw}, ...);
}
if (config.u_bdy_xe != nullptr) {
    grid_info_->u_bdy_xe = tls_cached_from_blob(config.u_bdy_xe, {ny, nz, bw}, ...);
}
// ... continues for all 48 boundary arrays
```

**Item 4: Move-nest/Restart Epoch Handling**:

Solver epoch is incremented on both initialization and cleanup paths:
```cpp
// fortran_c_interface.cpp
void sdirk3_init(...) {
    wrf::sdirk3::incrementSolverEpoch();  // Line 42 - invalidates stale TLS
    // ... initialization code ...
    wrf_sdirk3_mark_workers_started();    // Line 80 - config freeze
}

void sdirk3_cleanup() {
    wrf::sdirk3::incrementSolverEpoch();  // Line 347 - cleanup invalidation
    // ... cleanup code ...
}
```

Nest move/restart flow: `start_domain` → `start_em` → `sdirk3_init()` → epoch increment

**Item 5: MPI Decomposition Architecture Safety**:

The `HALO_OWNERSHIP_POLICY` documented in `wrf_sdirk3_mpi_safety.h` guarantees rank-independence:
```cpp
// wrf_sdirk3_mpi_safety.h:41-63
/**
 * POLICY: WRF handles ALL halo exchanges at MPI level.
 *         SDIRK3 operates on INTERIOR points ONLY.
 */
inline bool isWithinInterior(
    int i, int j, int k,
    int its, int ite, int jts, int jte, int kts, int kte)
{
    return (i >= its && i <= ite &&
            j >= jts && j <= jte &&
            k >= kts && k <= kte);
}
```

Interior-only computation means different MPI decompositions produce identical results.

**Item 6: BC Parameter Constraint Validation**:

WRF enforces the constraint at Fortran level, C++ has additional bounds protection:
```fortran
! module_check_a_mundo.f90:248
model_config_rec%spec_zone = 1
model_config_rec%relax_zone = model_config_rec%spec_bdy_width - model_config_rec%spec_zone
```

C++ boundary application has additional safety:
```cpp
// Relaxation width bounded to available domain size
int bounded_relax = std::min(relax_zone, nx_u);
```

**Round 4 Verification Summary**:

All 6 items verified SAFE with no code changes required. The existing implementation correctly handles:
- Staggered grid interior bounds
- Independent halo/boundary parameters
- Defensive null pointer checks
- Epoch-based cache invalidation
- MPI decomposition independence
- BC parameter constraint enforcement
