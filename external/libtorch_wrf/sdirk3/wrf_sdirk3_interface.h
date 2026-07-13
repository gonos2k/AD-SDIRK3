#ifndef WRF_SDIRK3_INTERFACE_H
#define WRF_SDIRK3_INTERFACE_H

/**
 * WRF SDIRK3 ABI Version Information
 *
 * The WRFGridInfo struct ABI version is tracked in wrf_sdirk3_types.h via
 * WRF_SDIRK3_GRIDINFO_ABI_VERSION macro. Current version: 94
 *
 * ABI changes:
 *   v88 - Initial ABI version macro
 *   v89 - Added cached_device_index (int8_t) for multi-GPU
 *   v90 - Changed cached_device_index to int16_t for >127 GPU clusters
 *   v91 - Changed cached_reference_device to int16_t for type consistency
 *   v92 - Added warned_dz_rdz_device_mismatch for WARN_ONCE pattern
 *   v93 - Added dz_rdz_warn_throttle_counter for per-solver warning throttle
 *   v94 - Added solver_unique_id for per-solver TLS cache invalidation (Round109)
 *
 * If linking against WRFGridInfo from external modules, verify:
 *   #if WRF_SDIRK3_GRIDINFO_ABI_VERSION != 94
 *   #error "WRFGridInfo ABI version mismatch - rebuild required"
 *   #endif
 *
 * FIX Round99/Round100/Round102/Round103: ABI Header Control Macros
 * ==================================================================
 * Primary ABI header: wrf_sdirk3_abi_version.h (in libtorch_wrf/sdirk3/)
 * Required include path: -I${PROJECT_ROOT}/external/libtorch_wrf/sdirk3
 *
 * Build control macros (CMake: add_definitions(-D<macro>)):
 *
 *   WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER
 *     - Make fallback path emit #error instead of #warning
 *     - Recommended for CI/release builds to catch missing include paths
 *     - Usage: -DWRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER
 *
 *   WRF_SDIRK3_DISABLE_ABI_VERIFY (FIX Round102)
 *     - Disable ALL ABI version checks (static_assert/_Static_assert)
 *     - For legacy builds that can't use ABI verification
 *     - WARNING: Only use if you understand ABI compatibility risks!
 *     - Usage: -DWRF_SDIRK3_DISABLE_ABI_VERIFY
 *
 *   WRF_SDIRK3_STRICT_ABI_CHECK (FIX Round102)
 *     - Enable #error in pre-C11 builds (opt-in strict mode)
 *     - By default, pre-C11 uses soft mode (no-op for ABI verify)
 *     - Usage: -DWRF_SDIRK3_STRICT_ABI_CHECK
 *
 *   WRF_SDIRK3_ABI_FALLBACK_WARNED (auto-set)
 *     - Set automatically when fallback warning is emitted
 *     - Limits warning to once per build (first TU only)
 *
 *   WRF_SDIRK3_ABI_VERSION_FALLBACK (auto-set)
 *     - Set to 1 when fallback definitions are used
 *     - COMPILE-TIME ONLY: Do NOT use for runtime decisions across TUs
 *       (different TUs may have different values due to include order)
 *
 * Fallback behavior note:
 *   If fallback path is taken, it defines WRF_SDIRK3_ABI_VERSION_H guard.
 *   Any subsequent #include of the primary header will be SKIPPED (first wins).
 *   Ensure consistent include paths across all TUs to avoid inconsistency.
 *
 * FIX Round114/Round115/Round116: TLS Cache Debug/Validation Macros
 * ==================================================================
 * FIX Round146: DOC AUTHORITY HIERARCHY
 *   - TLS counters/reset impl: wrf_sdirk3_tile_unified.h (tls_debug namespace)
 *   - TLS API declarations: this file (wrf_sdirk3_interface.h)
 *   - Fortran bindings: module_implicit_sdirk3.F (sole bridge; synced summary)
 *
 * These macros control TLS cache validation for solver_unique_id consistency.
 * Used in wrf_sdirk3_interface_zerocopy.cpp, affect sdirk3_tile_solver_reset_tls_parallel().
 *
 * SINGLE SOURCE OF TRUTH: Default values are defined in wrf_sdirk3_abi_version.h
 * Override via compile flags: -DWRF_SDIRK3_<MACRO>=<value>
 *
 *   WRF_SDIRK3_DEBUG_TLS_CACHE
 *     - Enable intensive TLS cache validation in debug builds
 *     - Calls getGridInfo() on every cache hit to verify unique_id consistency
 *     - May impact debug performance; use for targeted debugging
 *     - Usage: -DWRF_SDIRK3_DEBUG_TLS_CACHE
 *
 *   WRF_SDIRK3_RELEASE_TLS_CHECK
 *     - Enable TLS cache validation in release builds (opt-in)
 *     - By default, release builds skip validation for maximum performance
 *     - When enabled: samples every N calls, logs global warning, aborts after threshold
 *     - Usage: -DWRF_SDIRK3_RELEASE_TLS_CHECK
 *
 *   WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL (default: 1000, defined in wrf_sdirk3_abi_version.h)
 *     - FIX Round116: Sampling interval to reduce getGridInfo() overhead
 *     - Only check every N calls when WRF_SDIRK3_RELEASE_TLS_CHECK enabled
 *     - Set to 1 for every-call checking (maximum coverage, higher overhead)
 *     - Usage: -DWRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL=100
 *
 *   WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD (default: 100, defined in wrf_sdirk3_abi_version.h)
 *     - Number of mismatches before aborting in release mode
 *     - Requires WRF_SDIRK3_RELEASE_TLS_CHECK to be defined
 *     - Usage: -DWRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD=50
 *
 * Default behavior by build type:
 *   - Debug (NDEBUG not defined): assert on mismatch (standard behavior)
 *   - Release (NDEBUG defined): no check unless WRF_SDIRK3_RELEASE_TLS_CHECK defined
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WRF SDIRK-3 C Interface
 *
 * Naming convention:
 * - wrf_sdirk3_<action>_<object>
 * - All functions use wrf_sdirk3_ prefix
 * - Actions: init, solve, compute, pack, unpack
 * - Objects: solver, stage, arrays, stats
 *
 * FIX Round132/Round147: SYMBOL VISIBILITY NOTE
 * This codebase does NOT use visibility macros (WRF_SDIRK3_API, __declspec(dllexport),
 * __attribute__((visibility("default")))). All C APIs have default visibility.
 * If shared library builds require explicit export control in the future:
 *   1. Define WRF_SDIRK3_API macro in a common header
 *   2. Apply to all extern "C" function declarations
 *   3. Update CMakeLists.txt with -fvisibility=hidden + explicit exports
 *   4. FIX Round147: When adding NEW C APIs, verify they are in export list.
 *      Checklist: wrf_sdirk3_set_config_*, sdirk3_*_tls_*, sdirk3_tile_solver_*
 *   5. FIX Round148: If using version scripts (.map) or Windows .def files:
 *      - Linux/macOS: Add to libsdirk3.map { global: wrf_sdirk3_*; sdirk3_*; local: *; }
 *      - Windows: Add to sdirk3.def EXPORTS section for each new C API
 *      - Verify: nm -D libsdirk3.so | grep -E 'wrf_sdirk3_|sdirk3_' (should list all)
 *   6. FIX Round149: CI AUTOMATION for export verification:
 *      # Linux/macOS: Compare declared vs exported
 *      DECLARED=$(grep -hE 'void (wrf_sdirk3_|sdirk3_)\w+' wrf_sdirk3_*.h | grep -oP '(wrf_sdirk3_|sdirk3_)\w+' | sort -u)
 *      EXPORTED=$(nm -D libsdirk3.so 2>/dev/null | grep -oE '(wrf_sdirk3_|sdirk3_)\w+' | sort -u)
 *      diff <(echo "$DECLARED") <(echo "$EXPORTED")  # Empty = OK
 *      # Windows: dumpbin /EXPORTS sdirk3.dll | findstr "sdirk3_"
 *
 * OPT Pass33+: DEPRECATED SYMBOL EXCLUSION FROM EXPORTS
 *   When WRF_SDIRK3_ENABLE_DEPRECATED_API=0 (default):
 *   - Deprecated APIs are #if'd out -> not compiled -> not in symbol table
 *   - No action needed for .map/.def files
 *   When WRF_SDIRK3_ENABLE_DEPRECATED_API=1 (not recommended):
 *   - Deprecated symbols will be declared but have NO implementation
 *   - Explicitly exclude from export lists to prevent confusion:
 *     .map: { global: sdirk3_tile_*_zerocopy*; local: wrf_sdirk3_init_solver; ... }
 *   - List of deprecated APIs to exclude (if enabled):
 *     wrf_sdirk3_init_solver, wrf_sdirk3_finalize_solver, wrf_sdirk3_init_solver_with_*,
 *     wrf_sdirk3_solve_stage, wrf_sdirk3_pack_arrays, wrf_sdirk3_unpack_arrays,
 *     wrf_sdirk3_get_stats, wrf_sdirk3_create_tile_solver*, wrf_sdirk3_destroy_tile_solver,
 *     wrf_sdirk3_benchmark, wrf_sdirk3_solve_acoustic_tile, wrf_sdirk3_solve_unified_tile,
 *     wrf_sdirk3_init_full_physics, wrf_sdirk3_solve_full_physics,
 *     wrf_sdirk3_set_preconditioner, wrf_sdirk3_set_convergence_params,
 *     wrf_sdirk3_print_diagnostics, wrf_sdirk3_compute_max_stable_dt
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * FIX Round150 (OPT Pass33+): RUNTIME CONFIGURATION API
 * ═══════════════════════════════════════════════════════════════════════════
 * SSoT: wrf_sdirk3_config.cpp set_config_*() functions
 * Last synced: 2025-01-19
 *
 * Runtime config setters (declared in wrf_sdirk3_config.h):
 *   void wrf_sdirk3_set_config_bool(const char* key, bool value);
 *   void wrf_sdirk3_set_config_int(const char* key, int value);
 *   void wrf_sdirk3_set_config_float(const char* key, float value);
 *   void wrf_sdirk3_set_config_uint64(const char* key, uint64_t value);
 *
 * COMPACT KEY REFERENCE (see wrf_sdirk3_config.h for full descriptions):
 * ┌─────────────────────────────────────┬───────────┬─────────────────────────┐
 * │ Key (category)                      │ Type      │ Purpose                 │
 * ├─────────────────────────────────────┼───────────┼─────────────────────────┤
 * │ SOLVER CORE                         │           │                         │
 * │  use_autograd                       │ bool      │ Enable AD               │
 * │  use_fp64                           │ bool      │ Force double precision  │
 * │  max_newton_iter, max_krylov_iter   │ int       │ Iteration limits        │
 * │  newton_tol, krylov_tol             │ float     │ Convergence tolerances  │
 * ├─────────────────────────────────────┼───────────┼─────────────────────────┤
 * │ TRUST REGION & EISENSTAT-WALKER     │           │                         │
 * │  use_trust_region, use_dogleg       │ bool      │ Enable trust region     │
 * │  ew_eta_max, ew_gamma               │ float     │ EW forcing parameters   │
 * ├─────────────────────────────────────┼───────────┼─────────────────────────┤
 * │ PHYSICS & PRECONDITIONING           │           │                         │
 * │  unified_physics_mode               │ int       │ Physics configuration   │
 * │  use_vertical_precond               │ bool      │ Enable TDMA precond     │
 * │  precond_type                       │ int       │ Preconditioner type     │
 * ├─────────────────────────────────────┼───────────┼─────────────────────────┤
 * │ DEBUG & DIAGNOSTICS                 │           │                         │
 * │  verbose, print_stats               │ bool      │ Logging control         │
 * │  debug_level                        │ int       │ Debug verbosity (0-3)   │
 * │  check_conservation                 │ bool      │ Mass/energy checks      │
 * │  benchmark_runs                     │ uint64    │ Benchmark iteration cnt │
 * └─────────────────────────────────────┴───────────┴─────────────────────────┘
 *
 * Fortran usage (via BIND(C)):
 *   CALL wrf_sdirk3_set_config_bool(C_LOC("use_autograd"//C_NULL_CHAR), .TRUE.)
 *   CALL wrf_sdirk3_set_config_int(C_LOC("max_newton_iter"//C_NULL_CHAR), 15)
 *
 * NOTE: Config setters are NOT thread-safe. Call BEFORE solver creation or
 * when solver is idle. See wrf_sdirk3_config.h for complete key inventory.
 * ═══════════════════════════════════════════════════════════════════════════
 */

// Return codes
#define WRF_SDIRK3_SUCCESS  0
#define WRF_SDIRK3_ERROR   -1

// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass33+: LEGACY API DEPRECATION CONTROL
// ═══════════════════════════════════════════════════════════════════════════
// The following APIs are declared but NOT implemented. They are retained for
// documentation/reference purposes only.
//
// FAILURE MODE when WRF_SDIRK3_ENABLE_DEPRECATED_API=1:
//   - Declarations become visible to compiler
//   - NO implementations exist -> LINKER ERROR (undefined symbol)
//   - To suppress build error, also define WRF_SDIRK3_ALLOW_DEPRECATED_LINKER_ERRORS=1
//     (This acknowledges you understand linking will fail if these APIs are called)
//
// Current production APIs use the zerocopy v2 interface:
//   - sdirk3_tile_solver_create_zerocopy()
//   - sdirk3_tile_unified_step_zerocopy_v2()
//   - sdirk3_tile_set_base_state_zerocopy_v2()
//   - etc. (see ZEROCOPY API DECLARATIONS section below)
//
// FORTRAN COMPATIBILITY:
//   module_implicit_sdirk3.F (sole bridge) does NOT reference any deprecated APIs.
//   All Fortran BIND(C) declarations use zerocopy v2 interface only.
//
// EXPORT/VISIBILITY:
//   When using -fvisibility=hidden or version scripts (.map/.def):
//   - Deprecated APIs are conditionally compiled out by default
//   - Only zerocopy v2 APIs should appear in export lists
//   - Example .map: { global: sdirk3_tile_*_zerocopy*; wrf_sdirk3_check_*; local: *; }
// ═══════════════════════════════════════════════════════════════════════════
#ifndef WRF_SDIRK3_ENABLE_DEPRECATED_API
#define WRF_SDIRK3_ENABLE_DEPRECATED_API 0
#endif

#if WRF_SDIRK3_ENABLE_DEPRECATED_API
// Build-time safety check: require explicit acknowledgment of linker errors
#ifndef WRF_SDIRK3_ALLOW_DEPRECATED_LINKER_ERRORS
#error "WRF_SDIRK3_ENABLE_DEPRECATED_API=1 requires WRF_SDIRK3_ALLOW_DEPRECATED_LINKER_ERRORS=1. \
These APIs have NO implementation and WILL cause linker errors if called. \
Use zerocopy v2 APIs instead: sdirk3_tile_unified_step_zerocopy_v2(), etc."
#endif
// ═══════════════════════════════════════════════════════════════════════════
// BEGIN DEPRECATED/UNIMPLEMENTED API DECLARATIONS
// WARNING: These functions are declared but NOT implemented!
// ═══════════════════════════════════════════════════════════════════════════

// Utility function removed - legacy pack/unpack no longer supported

/**
 * [DEPRECATED - NOT IMPLEMENTED] Initialization/Finalization
 */
int wrf_sdirk3_init_solver(
    int nx, int ny, int nz,
    float dx, float dy, float dt,
    int unified_physics_mode   // Unified physics configuration
);

void wrf_sdirk3_finalize_solver(void);

/**
 * Initialization with map scale factors
 */
int wrf_sdirk3_init_solver_with_maps(
    int nx, int ny, int nz,
    float dx, float dy, float dt,
    float* msftx, float* msfty,
    float* msfux, float* msfuy,
    float* msfvx, float* msfvy,
    int unified_mode
);

/**
 * Initialization with map scale factors and vertical grid
 */
int wrf_sdirk3_init_solver_with_grid(
    int nx, int ny, int nz,
    float dx, float dy, float dt,
    float* dnw,                // WRF vertical grid spacing (eta levels)
    float* rdnw,               // Reciprocal of dnw
    float* msftx, float* msfty,
    float* msfux, float* msfuy,
    float* msfvx, float* msfvy,
    int unified_mode
);

/**
 * SDIRK-3 stage computation
 */
int wrf_sdirk3_solve_stage(
    float* k_stage,            // Output: stage derivative
    const float* u_prev,       // Previous state
    const float* k_prev,       // Previous stages (can be NULL for stage 1)
    int stage_num,             // Stage number (1, 2, or 3)
    float dt,                  // Time step
    int nx, int ny, int nz     // Domain dimensions
);

/**
 * WRF array packing/unpacking
 * Fortran column-major <-> C row-major conversion
 */
void wrf_sdirk3_pack_arrays(
    float* packed_data,        // Output: C-style packed array
    const float* u,            // WRF u velocity
    const float* v,            // WRF v velocity
    const float* w,            // WRF w velocity
    const float* ph,           // WRF perturbation geopotential
    const float* mu,           // WRF column mass
    int nx, int ny, int nz,
    int ims, int ime,          // Memory dimensions
    int jms, int jme,
    int kms, int kme
);

void wrf_sdirk3_unpack_arrays(
    float* u,                  // Output: WRF arrays
    float* v,
    float* w,
    float* ph,
    float* mu,
    const float* packed_data,  // Input: C-style packed array
    int nx, int ny, int nz,
    int ims, int ime,
    int jms, int jme,
    int kms, int kme
);

/**
 * [DEPRECATED - NOT IMPLEMENTED] Statistics information
 */
void wrf_sdirk3_get_stats(
    int* newton_iters,
    int* gmres_iters,
    float* final_residual
);

/**
 * [DEPRECATED - NOT IMPLEMENTED] Tile-based solver (WRF parallelization)
 * NOTE: Use sdirk3_tile_solver_create_zerocopy() instead
 */
void* wrf_sdirk3_create_tile_solver(
    int nx, int ny, int nz,
    float dx, float dy, float dt,
    int its, int ite,          // Tile indices
    int jts, int jte,
    int kts, int kte,
    int unified_physics_mode,  // Unified physics configuration
    int tile_id
);

void* wrf_sdirk3_create_tile_solver_with_maps(
    int nx, int ny, int nz,
    float dx, float dy, float dt,
    float* msftx, float* msfty,
    float* msfux, float* msfuy,
    float* msfvx, float* msfvy,
    int tile_id
);

void wrf_sdirk3_destroy_tile_solver(void* solver);

#endif // WRF_SDIRK3_ENABLE_DEPRECATED_API
// ═══════════════════════════════════════════════════════════════════════════
// END DEPRECATED/UNIMPLEMENTED API DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// FIX Round121: SOLVER RESET API SUMMARY
// ═══════════════════════════════════════════════════════════════════════════
// FIX Round137: TERMINOLOGY GLOSSARY
//   - TLS lookup cache: solver_ptr→GridInfo* mapping cached per thread
//   - TLS debug tracking: change counters, sample counters stored per thread
//   - Generation counter: global atomic counter triggering lookup cache invalidation
//
// FIX Round136: QUICK REFERENCE TABLE (1-line scope summary per API)
// ┌───────────────────────────────────────────┬─────────────────────────────────────┐
// │ API                                       │ Scope (what it resets)              │
// ├───────────────────────────────────────────┼─────────────────────────────────────┤
// │ 1. tls_debug::resetTLSTracking()          │ TLS debug counters only (header)    │
// │ 2. sdirk3_tile_solver_reset_state()       │ Per-solver warning/device cache     │
// │ 3. sdirk3_tile_solver_reset_full()        │ #2 + all solver caches              │
// │ 4. sdirk3_tile_solver_reset_full_parallel │ #3 + all-thread TLS                 │
// │ 5. sdirk3_tile_solver_reset_tls_parallel()│ TLS caches only (no solver caches)  │
// │ 6. sdirk3_reset_tls_debug_tracking()      │ TLS debug (C API, per-thread)       │
// │ 7. sdirk3_clear_tls_solver_cache()        │ TLS lookup+debug (delayed,per-thrd) │
// └───────────────────────────────────────────┴─────────────────────────────────────┘
// FIX Round138/Round149: THREAD SCOPE WARNING (synced with module_implicit_sdirk3.F)
//   APIs #1, #6, #7 affect CALLING THREAD ONLY (per-thread TLS).
//   For all-thread reset, use #4 (reset_full_parallel) or #5 (reset_tls_parallel).
// FIX Round138: MULTI-SOLVER NOTE for API #7
//   sdirk3_clear_tls_solver_cache() increments GLOBAL generation counter.
//   Frequent calls force ALL solvers to revalidate TLS cache (cross-solver overhead).
//
// FIX Round142: DOC ROLE SEPARATION (table = summary, body = details)
//   - TABLE (above): Quick reference - use for API selection
//   - BODY (below): Full documentation with usage patterns and warnings
//   - config.cpp: Policy table for uint64 config keys
//   - module_implicit_sdirk3.F: Fortran-specific usage notes
//
// Four reset APIs in order of increasing scope/overhead:
//
// 1. wrf::sdirk3::tls_debug::resetTLSTracking() [Header-only, lowest overhead]
//    - Resets: TLS debug tracking state + release sampling counter
//    - Location: wrf_sdirk3_tile_unified.h (tls_debug namespace)
//    - Use for: Resetting diagnostic counters without touching solver state
//    - Thread scope: Current thread only (each thread has its own TLS)
//
// 2. sdirk3_tile_solver_reset_state() [Per-solver state only]
//    - Resets: Warning flags, device cache, logging state
//    - Does NOT reset: Solver caches (divergence, MSF, metrics)
//    - Use for: Production solver reuse between simulations
//    - Overhead: Low (O(1) field assignments)
//    - Thread scope: Per-solver state only (shared), no TLS affected
//
// 3. sdirk3_tile_solver_reset_full() [Per-solver + all caches]
//    - Resets: Everything in reset_state() + all solver caches
//    - Cache types: Divergence, MSF 3D, metric, pressure gradient
//    - Use for: Testing/debugging with full state isolation
//    - Overhead: Moderate (epoch increments, cache flag resets)
//    - Thread scope: Per-solver + calling thread TLS only
//
// 4. sdirk3_tile_solver_reset_full_parallel() [Full + multi-thread TLS]
//    - Resets: Everything in reset_full() + TLS caches in ALL threads
//    - Use for: OpenMP multi-threaded tests requiring deterministic state
//    - Overhead: Highest (OpenMP parallel region, all-thread synchronization)
//    - Thread scope: Per-solver + ALL threads TLS
//
// Additional TLS-only APIs:
// 5. sdirk3_tile_solver_reset_tls_parallel() [TLS caches only]
//    - Resets: TLS caches for reset_full_parallel worker without global caches
//    - Use for: Fine-grained TLS reset within parallel regions
//    - Overhead: Low-moderate (per-thread TLS operations)
//    - Thread scope: Calling thread TLS only (call from each thread in OMP PARALLEL)
//
// 6. sdirk3_reset_tls_debug_tracking() [Debug tracking only, NO solver lookup cache]
//    - Resets: tls_solver_change_count, tls_last_logged_count, tls_release_check_counter
//    - Does NOT reset: TLS solver lookup cache (solver_ptr, unique_id, generation)
//    - Does NOT reset: Solver internal caches (divergence, MSF, metrics, etc.)
//    - Use for: Test harnesses needing clean debug counters without affecting solver cache
//    - Overhead: Lowest (no solver lookup, no mutex, current thread only)
//    - Thread scope: Calling thread TLS only
//    - WARNING: No-op when WRF_SDIRK3_NO_TLS_TRACKING is defined at compile time!
//
// 7. sdirk3_clear_tls_solver_cache() [TLS lookup cache + debug tracking, NO solver caches]
//    - Resets: TLS solver lookup cache (via generation increment) + debug tracking
//    - Does NOT reset: Solver internal caches (divergence, MSF, metrics, etc.)
//    - Use for: Test harnesses requiring explicit cache invalidation between tests
//    - Use for: Solver recycling when you want forced re-lookup on next access
//    - Overhead: Low (atomic increment, no mutex, current thread only)
//    - Thread scope: Current thread only - forces that thread's next access to re-lookup
//    - ZEROCOPY ONLY: Only affects zerocopy interface, not legacy/unified interfaces
//
// ─────────────────────────────────────────────────────────────────────────────
// FIX Round128/Round129: COMPLETE TLS STATE RESET GUIDE
// ─────────────────────────────────────────────────────────────────────────────
// The TLS state consists of two parts:
//   A) Lookup cache: solver_ptr, solver, generation, unique_id
//   B) Debug tracking: solver_change_count, last_logged_count, release_check_counter
//
// Function coverage (FIX Round129: explicit debug tracking inclusion):
//   ┌────────────────────────────────────┬────────────┬────────────────┐
//   │ Function                           │ Resets A   │ Resets B       │
//   │                                    │ (Lookup)   │ (Debug Track)  │
//   ├────────────────────────────────────┼────────────┼────────────────┤
//   │ sdirk3_reset_tls_debug_tracking()  │ NO         │ YES            │
//   │ sdirk3_clear_tls_solver_cache()    │ YES        │ YES (internal) │
//   └────────────────────────────────────┴────────────┴────────────────┘
//
// IMPORTANT (FIX Round129):
//   - sdirk3_clear_tls_solver_cache() INCLUDES debug tracking reset internally
//   - sdirk3_reset_tls_debug_tracking() does NOT affect lookup cache
//   - There is NO function that resets lookup cache WITHOUT debug tracking
//
// Choosing the right function:
//   - Need fresh lookup cache + debug counters: sdirk3_clear_tls_solver_cache()
//   - Need ONLY debug counter reset (keep lookup cache): sdirk3_reset_tls_debug_tracking()
//
// NOTE: sdirk3_clear_tls_solver_cache() uses DELAYED invalidation via global
// generation increment. Each thread's cache is invalidated on NEXT ACCESS,
// not immediately. For immediate per-thread invalidation, use
// sdirk3_tile_solver_reset_tls_parallel() instead.
// ─────────────────────────────────────────────────────────────────────────────
//
// Recommendation: Use the lowest-overhead API that meets your needs.
// For production WRF runs, reset is typically NOT needed (solvers are
// created once per simulation). Use reset APIs for testing/reuse scenarios.
//
// FIX Round133: RECOMMENDED CALL COMBINATIONS (use cases with examples)
// ─────────────────────────────────────────────────────────────────────────────
// Use Case 1: Test setup - clean slate before each test
//   CALL sdirk3_tile_solver_reset_full(solver_ptr)         ! Reset solver caches
//   CALL sdirk3_clear_tls_solver_cache()                   ! Reset TLS lookup
//   ! Now run test with deterministic state
//
// Use Case 2: Debug counter reset only (keep solver and TLS cache intact)
//   CALL sdirk3_reset_tls_debug_tracking()                 ! Reset debug counters only
//   ! Solver caches and TLS lookup cache remain unchanged
//
// Use Case 3: Multi-threaded test teardown (all threads, all state)
//   !$OMP PARALLEL
//   CALL sdirk3_tile_solver_reset_full_parallel(solver_ptr) ! All threads reset
//   !$OMP END PARALLEL
//   ! Complete reset across all threads
// ─────────────────────────────────────────────────────────────────────────────
//
// ─────────────────────────────────────────────────────────────────────────────
// FIX Round123: TLS CACHE BEHAVIOR FOR SOLVER RECYCLING
// ─────────────────────────────────────────────────────────────────────────────
// CRITICAL: TLS cache persists across solver destroy/create cycles within a thread.
//
// Thread-local storage (TLS) in this implementation uses the solver's memory
// address to detect when a different solver is being accessed. However, when a
// solver is destroyed and a new solver is created at the same address (memory
// recycling), TLS cache may be STALE:
//
//   Scenario:
//     1. Thread A creates Solver1 at address 0x1234, TLS caches GridInfo* for 0x1234
//     2. Thread A destroys Solver1
//     3. Thread A creates Solver2 at address 0x1234 (allocator reuses memory)
//     4. TLS cache hits for address 0x1234, returns STALE GridInfo* from Solver1
//
//   Mitigations (use ONE of the following):
//     a) solver_unique_id: Each solver gets a unique ID; TLS validation checks ID
//        not just address. This is the default protection and handles most cases.
//     b) reset_full_parallel(): Explicitly reset TLS in all threads before recycling
//     c) Different addresses: Use separate allocations if recycling is problematic
//
//   The solver_unique_id mechanism (added in Round109) provides automatic protection
//   against stale TLS cache. The unique ID is checked during cache validation, and
//   cache misses trigger re-lookup when IDs don't match. This is the recommended
//   approach for production use.
//
//   If using WRF_SDIRK3_NO_TLS_TRACKING to disable TLS debug tracking, the
//   solver_unique_id validation still operates independently in production code.
//
//   FIX Round134: DESTROY API DEPENDENCY WARNING
//   TLS cache safety relies on proper use of sdirk3_tile_solver_destroy_zerocopy().
//   If a solver is deallocated WITHOUT calling destroy (e.g., direct free/delete),
//   the solver_unique_id mechanism cannot detect stale cache, and TLS revalidation
//   may return invalid pointers. ALWAYS call destroy before deallocating solvers.
// ═══════════════════════════════════════════════════════════════════════════

/**
 * FIX 2025-01-11 Round75/Round76/Round77/Round78: Lightweight reset for per-solver state only
 *
 * PRODUCTION RECOMMENDED - Low overhead reset for solver reuse scenarios.
 *
 * Call this when reusing a solver instance for a new simulation or after device
 * migration. Resets ONLY per-solver state (via grid_info->resetPerSolverState()):
 *   - FP64 transition logging state
 *   - No-reference-device warning flag
 *   - Device cache
 *
 * Does NOT invalidate solver caches (divergence, MSF, metric, pressure gradient).
 * Use sdirk3_tile_solver_reset_full() if full cache invalidation is needed.
 *
 * CALLING CONDITIONS (FIX Round78):
 *   MUST be called BEFORE any call to stepping functions
 *   MUST NOT be called concurrently with solver stepping on the same solver
 *   Safe to call between time steps when solver is idle
 *
 * Without this reset, a reused solver instance would:
 *   - Suppress FP64 transition warnings
 *   - Suppress no-reference-device warnings
 *   - Use potentially stale device cache
 *
 * Note: This is typically NOT needed during normal WRF operation where solvers
 * are created once and used for the entire simulation. Only call if reusing
 * solver instances across distinct simulation runs.
 *
 * Fortran binding: none since the dormant bridge was removed (C ABI retained)
 */
void sdirk3_tile_solver_reset_state(void* solver_ptr);

/**
 * FIX 2025-01-11 Round78/Round79/Round80/Round81: Full reset including all internal caches
 *
 * DEBUG/TEST RECOMMENDED - Heavy operation for complete solver state invalidation.
 *
 * Performs FULL reset in two steps:
 *
 * 1. Per-solver state (via grid_info->resetPerSolverState()):
 *    - FP64 transition logging state
 *    - No-reference-device warning flag
 *    - Device cache
 *
 * 2. All caches via SINGLE ENTRY POINT (solver->invalidateCaches(), FIX Round81):
 *    See wrf_sdirk3_tile_unified.h for authoritative inventory of all 9 cache types:
 *    - Solver caches: divergence, MSF 3D, acoustic metrics, pressure gradient
 *    - Grid metric caches: z1d, dz_min, lat_cpu, static metrics, scalar mean
 *
 * NOT RESET by this function (FIX Round79 - documented limitation):
 *   - Internal tensor pools (temporary buffers retained for performance)
 *   - PyTorch caching allocator memory
 *   - Dimension-based self-invalidating caches (auto-refresh on dimension change)
 *   If memory footprint reduction is critical (e.g., testing with many solver instances),
 *   consider destroying and recreating the solver, or implementing a pool clear API.
 *
 * THREAD POLICY (FIX Round84 - synced from wrf_sdirk3_tile_unified.h):
 *   Thread-local caches (#2 MSF 3D, #4 pressure gradient) are only invalidated
 *   in the CALLING THREAD. In multi-threaded execution (OpenMP):
 *   - Worker threads' caches remain valid after reset_full from main thread
 *   - Each thread must call invalidateCaches() independently for full reset
 *   - For test harnesses: use OMP_NUM_THREADS=1 for deterministic behavior
 *   - Epoch-based caches (#5-9) use global atomics and ARE thread-safe
 *
 * CALLING CONDITIONS (FIX Round78):
 *   MUST be called BEFORE any call to stepping functions
 *   MUST NOT be called concurrently with solver stepping on the same solver
 *   MUST NOT be called during GPU computation (may cause synchronization issues)
 *   Safe to call between time steps when solver is idle
 *
 * Without this reset, a reused solver instance would:
 *   - Suppress FP64 transition warnings
 *   - Suppress no-reference-device warnings
 *   - Use potentially stale device cache
 *   - Use stale metric/divergence/pressure gradient caches
 *
 * Note: Prefer sdirk3_tile_solver_reset_state() for production use.
 * Use this full reset only when grid geometry changes or for testing/debugging.
 *
 * Fortran binding: sdirk3_tile_solver_reset_full (module_implicit_sdirk3.F)
 */
void sdirk3_tile_solver_reset_full(void* solver_ptr);

/**
 * FIX Round85/Round86/Round88: Full reset with OpenMP parallel thread_local cache handling
 *
 * RECOMMENDED for OpenMP builds - Properly separates global vs thread_local caches.
 *
 * This function handles the separation of cache invalidation:
 *   1. Master thread only: per-solver state + global/epoch-based caches
 *   2. All threads (via #pragma omp parallel): thread_local caches
 *
 * This avoids redundant global epoch increments that would occur if
 * invalidateCaches() were called from every thread in a parallel region.
 *
 * FIX Round88: NESTED PARALLEL REGION SAFETY
 *   If called from within an existing parallel region (omp_in_parallel() == true),
 *   this function falls back to serial invalidation to avoid nested team creation.
 *   The optimized path uses single parallel region with master+barrier pattern.
 *
 * USE THIS INSTEAD OF reset_full() WHEN:
 *   - Running with OpenMP enabled (OMP_NUM_THREADS > 1)
 *   - Need deterministic cache state across all worker threads
 *   - Test harnesses requiring complete cache isolation between tests
 *
 * WHEN reset_full() IS SUFFICIENT (FIX Round86):
 *   - Single-threaded execution (OMP_NUM_THREADS=1 or no OpenMP)
 *   - Normal WRF production runs (single solver per tile)
 *   - Any scenario where only main thread accesses the solver
 *   Existing code using reset_full() does NOT need to change unless
 *   running multi-threaded tests that require deterministic TLS cache state.
 *
 * NON-OPENMP BUILD BEHAVIOR (FIX Round86):
 *   When compiled without OpenMP (_OPENMP undefined), this function is a
 *   serial fallback that calls global + TLS invalidation sequentially.
 *   Functionally equivalent to reset_full() but with separated code paths.
 *
 * Fortran binding: none since the dormant bridge was removed (C ABI retained)
 */
void sdirk3_tile_solver_reset_full_parallel(void* solver_ptr);

/**
 * FIX Round101: Lightweight TLS-only parallel reset API
 *
 * Resets ONLY thread-local (TLS) caches for the calling thread.
 * Does NOT touch global caches or per-solver state.
 *
 * Use this when:
 *   - Called from within a parallel region (each thread calls this)
 *   - Solver pooling scenarios where only TLS state needs cleanup
 *   - Multi-threaded test harnesses requiring TLS isolation
 *
 * IMPORTANT: This function should be called by ALL threads in a parallel region
 * to ensure consistent TLS state. Use #pragma omp barrier before/after if needed.
 *
 * Fortran binding: none since the dormant bridge was removed (C ABI retained)
 */
void sdirk3_tile_solver_reset_tls_parallel(void* solver_ptr);

/**
 * FIX Round124: Lightweight TLS debug tracking reset (current thread only)
 *
 * Resets TLS debug tracking state WITHOUT requiring a solver reference.
 * This is the C API wrapper for tls_debug::resetTLSTracking() in wrf_sdirk3_tile_unified.h.
 *
 * What it resets:
 *   - tls_solver_change_count
 *   - tls_last_logged_count
 *   - tls_release_check_counter
 *   - last_tls_solver_ptr
 *
 * Use cases:
 *   - Test setup/teardown to ensure clean TLS state
 *   - After solver recycling when you want fresh debug tracking
 *   - Debugging TLS cache behavior across solver boundaries
 *
 * Thread scope: Current thread only. For multi-threaded reset, call from each thread.
 *
 * Note: This does NOT reset solver caches or the TLS lookup cache (solver pointer, unique_id).
 * For full TLS cache reset including lookup cache, use sdirk3_clear_tls_solver_cache().
 *
 * IMPORTANT: When WRF_SDIRK3_NO_TLS_TRACKING is defined at compile time, this function
 * is a NO-OP (does nothing). Callers should be aware that debug tracking is disabled
 * in production builds using this macro, and this call will have no effect.
 *
 * FIX Round132: MULTI-DSO ENVIRONMENT NOTE
 * TLS debug counters are per-DSO (Dynamic Shared Object). If your application links
 * multiple shared libraries that each include this code, calling this function only
 * resets counters in the CURRENT DSO's TLS. For complete reset across all DSOs:
 *   - Call sdirk3_reset_tls_debug_tracking() from each DSO that uses TLS tracking
 *   - Or use a single shared library build to ensure unified TLS state
 *
 * Fortran example:
 *   INTERFACE
 *      SUBROUTINE sdirk3_reset_tls_debug_tracking() BIND(C)
 *      END SUBROUTINE
 *   END INTERFACE
 *   CALL sdirk3_reset_tls_debug_tracking()
 */
void sdirk3_reset_tls_debug_tracking(void);

/**
 * FIX Round127/Round128: Explicit TLS solver lookup cache clear (current thread only)
 *
 * Clears the thread-local solver lookup cache, forcing re-lookup on next solver access.
 * This is different from sdirk3_reset_tls_debug_tracking() which only resets debug counters.
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * FIX Round128: ZEROCOPY INTERFACE ONLY
 * This function ONLY affects the zerocopy interface (sdirk3_tile_solver_create_zerocopy,
 * sdirk3_tile_unified_step_zerocopy_v2, etc.). It does NOT affect:
 *   - Legacy tile interface (wrf_sdirk3_tile_interface.cpp, TileSDIRK3Solver)
 *   - Unified interface (wrf_sdirk3_unified_solver.cpp, UnifiedSDIRK3Solver)
 * Those interfaces do NOT use TLS caching, so this function has no effect on them.
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * What it clears:
 *   - tls_cached_solver_ptr (set to nullptr)
 *   - tls_cached_solver (set to nullptr)
 *   - tls_cached_generation (set to 0)
 *   - tls_cached_unique_id (set to 0)
 *
 * What it does NOT clear (use sdirk3_reset_tls_debug_tracking() for these):
 *   - Debug tracking counters (tls_solver_change_count, tls_last_logged_count)
 *
 * COMPLETE TLS RESET: To fully reset both lookup cache AND debug tracking:
 *   sdirk3_clear_tls_solver_cache();      // Reset lookup cache (generation bump)
 *   sdirk3_reset_tls_debug_tracking();    // Reset debug counters
 *
 * Use cases:
 *   - Test harnesses requiring deterministic cache behavior between tests
 *   - Solver recycling scenarios where you want to force cache re-population
 *   - After explicit solver destroy to ensure no stale cache references
 *   - Debugging TLS cache validation issues
 *
 * Thread scope: Current thread only. Call from each thread in multi-threaded scenarios.
 *
 * Note: This is NOT required for normal WRF operation. The solver_unique_id mechanism
 * provides automatic protection against stale TLS cache after solver destroy/recreate.
 * Use this only when you need explicit cache control for testing or debugging.
 *
 * FIX Round134: MULTI-DSO ENVIRONMENT NOTE
 * The global g_solver_generation counter is per-DSO. If your application links multiple
 * shared libraries that each include this code, calling this function only increments
 * the generation counter in the CURRENT DSO. For complete cache invalidation across all DSOs:
 *   - Call sdirk3_clear_tls_solver_cache() from each DSO that uses TLS caching
 *   - Or use a single shared library build to ensure unified generation counter
 *
 * Fortran example:
 *   INTERFACE
 *      SUBROUTINE sdirk3_clear_tls_solver_cache() BIND(C)
 *      END SUBROUTINE
 *   END INTERFACE
 *   CALL sdirk3_clear_tls_solver_cache()
 */
void sdirk3_clear_tls_solver_cache(void);

// Legacy pack/unpack functions removed - use zero-copy interface instead

/**
 * Zero-copy interface - RECOMMENDED for production use
 * Avoids memory copy overhead by creating direct tensor views
 */
void* wrf_sdirk3_create_zerocopy_state(
    float* u, float* v, float* w,
    float* t, float* ph, float* mu, float* p,
    int ims, int ime, int jms, int jme, int kms, int kme,
    int its, int ite, int jts, int jte, int kts, int kte
);

int wrf_sdirk3_compute_rhs_zerocopy(
    void* state_ptr,
    float* rhs_u, float* rhs_v, float* rhs_w,
    float* rhs_t, float* rhs_ph, float* rhs_mu, float* rhs_p,
    float dt,
    int ims, int ime, int jms, int jme, int kms, int kme,
    int its, int ite, int jts, int jte, int kts, int kte
);

void wrf_sdirk3_destroy_zerocopy_state(void* state_ptr);

/**
 * Performance benchmark function (optional, not part of core solver)
 */
void wrf_sdirk3_benchmark(int nx, int ny, int nz, int n_iterations);

/**
 * Acoustic tile solver for WRF integration
 */
int wrf_sdirk3_solve_acoustic_tile(
    void* solver,
    float* ru_new, float* rv_new, float* rw_new,
    float* rho_new, float* pp_new,
    const float* ru_old, const float* rv_old, const float* rw_old,
    const float* rho_old, const float* pp_old,
    float dt_acoustic,
    int rk_step,
    int rk_substep);

/**
 * Unified tile solver - NO MODE SEPARATION
 * Solves all dynamics (acoustic + gravity + advection) together
 */
int wrf_sdirk3_solve_unified_tile(
    void* solver,
    double* state_new,          // Complete state vector (output)
    const double* state_old,    // Previous state
    const double* forcing,      // External forcing (if any)
    double dt,                 // Full timestep (no acoustic substep)
    int stage_num              // SDIRK3 stage (1, 2, or 3)
);

/**
 * Unified physics interface - all dynamics solved implicitly together
 */
int wrf_sdirk3_init_full_physics(
    int nx, int ny, int nz,
    double dx, double dy,
    double* dz_w,               // Vertical spacing at w points
    double* dz_u,               // Vertical spacing at u points
    double dt,
    int physics_mode,          // Bitmask of physics options
    double implicit_gravity_fac,
    double implicit_acoustic_fac
);

int wrf_sdirk3_solve_full_physics(
    double* state_new,          // Output: new state [nvars, nz, ny, nx]
    const double* state_old,    // Input: old state
    const double* forcing,      // External forcing terms
    int nvars,                 // Number of variables
    double dt,
    int nx, int ny, int nz
);

/**
 * Preconditioner configuration
 */
int wrf_sdirk3_set_preconditioner(
    int precond_type,          // 0=None, 1=Jacobi, 2=Block, 3=Multigrid
    float* precond_params,     // Preconditioner parameters
    int n_params
);

/**
 * Convergence control parameters
 */
int wrf_sdirk3_set_convergence_params(
    float newton_tol,
    float newton_rtol,
    int max_newton_iter,
    float krylov_tol,
    int max_krylov_iter,
    int gmres_restart
);

/**
 * Diagnostic information output
 */
void wrf_sdirk3_print_diagnostics(
    int timestep,
    double elapsed_time
);

/**
 * Stability analysis
 */
double wrf_sdirk3_compute_max_stable_dt(
    const double* state,
    int nx, int ny, int nz,
    double dx, double dy,
    double* dz,
    double safety_factor
);

/**
 * Energy conservation verification
 */
void wrf_sdirk3_check_conservation(
    const double* state,
    int nx, int ny, int nz,
    double* total_mass,
    double* total_energy,
    double* total_momentum_x,
    double* total_momentum_y,
    double* total_momentum_z
);

/**
 * Adaptive timestep configuration
 */
void sdirk3_set_adaptive_config(
    void* solver_ptr,
    int adaptive_enabled,
    double error_tol,
    double safety_factor,
    double dt_min_factor,
    double dt_max_factor,
    int error_norm_type,
    int use_embedding
);

/**
 * Get adaptive timestep recommendation
 */
double sdirk3_get_dt_recommendation(void* solver_ptr);

/**
 * Tile-based solver functions (matching Fortran interface)
 */
void* sdirk3_tile_solver_create(
    int nx, int ny, int nz,
    double dx, double dy, double dt,
    int its, int ite, int jts, int jte, int kts, int kte,
    int unified_physics_mode,  // Unified physics configuration
    int use_autograd, int use_cpu_opt,
    int tile_id
);

void sdirk3_tile_solver_destroy(void* solver_ptr);

/**
 * Unified SDIRK3 tile step - solves ALL dynamics together
 * No acoustic substeps, uses full timestep dt
 */
void sdirk3_tile_unified_step(
    void* solver_ptr,
    float* u_2, float* v_2, float* w_2,
    float* ph_2, float* theta, float* mu_2,
    float* ru_tend, float* rv_tend, float* rw_tend,
    float* ph_tend, float* theta_tend, float* mu_tend,
    float rdx, float rdy, float* rdnw, float* rdn,  // rdx, rdy are scalars, rdnw/rdn are arrays
    float* msftx, float* msfty,         // Map factors at mass points
    float* msfux, float* msfuy,         // Map factors at u-points
    float* msfvx, float* msfvy,         // Map factors at v-points
    float* c1f, float* c2f,             // Vertical coordinate coefficients at w-levels
    float* c1h, float* c2h,             // Vertical coordinate coefficients at mass levels
    int stage, float dt,  // Full timestep, not acoustic substep
    int nx, int ny, int nz,
    int nx_u, int ny_v, int nz_w  // Staggered dimensions
);

void sdirk3_tile_rk_tendency(
    void* solver_ptr,
    double* u_2, double* v_2, double* w_2,
    double* t_2, double* ph_2, double* mu_2,
    double* u_tend, double* v_tend, double* w_tend,
    double* t_tend, double* ph_tend, double* mu_tend,
    int stage, int rk_step,
    int nx, int ny, int nz
);

/**
 * Set boundary condition flags for a tile solver (v1 — backward compatible).
 * specified and nested default to 0 (false).
 */
void sdirk3_tile_set_boundary_conditions(void* solver_ptr,
                                        int periodic_x, int periodic_y,
                                        int symmetric_xs, int symmetric_xe,
                                        int symmetric_ys, int symmetric_ye,
                                        int open_xs, int open_xe,
                                        int open_ys, int open_ye);

/**
 * Set boundary condition flags for a tile solver (v2 — with specified/nested).
 * Preferred over v1 when Fortran caller can provide all 12 BC flags.
 */
void sdirk3_tile_set_boundary_conditions_v2(void* solver_ptr,
                                           int periodic_x, int periodic_y,
                                           int symmetric_xs, int symmetric_xe,
                                           int symmetric_ys, int symmetric_ye,
                                           int open_xs, int open_xe,
                                           int open_ys, int open_ye,
                                           int specified, int nested);

/**
 * Set map projection for idealized case detection
 */
void sdirk3_tile_set_map_projection(void* solver_ptr, int map_proj);

/**
 * Set solver-local MPI process grid information.
 * Must be called before time-step entry so setWRFIndices() can initialize
 * halo state with the correct process topology.
 */
void sdirk3_tile_set_mpi_process_info(void* solver_ptr,
                                      int nprocx, int nprocy,
                                      int mypx, int mypy);

/**
 * FIX 2025-01-11 Round64/Round65: FP64 scalar parameters for precision-critical paths
 *
 * This struct allows callers to provide double-precision grid spacing values
 * which are used when compute paths require FP64 precision (e.g., CPU double runs).
 *
 * NOTE: This struct is defined outside #pragma pack to ensure proper 8-byte alignment
 * for double values on all architectures.
 *
 * ABI EQUIVALENCE (FIX Round65):
 *   - C API:      SDIRK3_ScalarParams_FP64_C (this file, for extern "C" callers)
 *   - C++ impl:   SDIRK3_ScalarParams_FP64   (wrf_sdirk3_interface_params.h, alignas(8))
 *   - Fortran:    SDIRK3_ScalarParams_FP64   (no Fortran binding since the dormant bridge was removed; C ABI type retained)
 *   All three MUST have identical layout: 4 × double (32 bytes total, 8-byte aligned).
 *   Modification to any requires updating all three and re-verifying static_asserts.
 */
typedef struct {
    double rdx_fp64;    /* Reciprocal grid spacing (1/dx) in double precision */
    double rdy_fp64;    /* Reciprocal grid spacing (1/dy) in double precision */
    double dx_fp64;     /* Grid spacing (dx) - preferred if provided, set to 0 if not */
    double dy_fp64;     /* Grid spacing (dy) - preferred if provided, set to 0 if not */
} SDIRK3_ScalarParams_FP64_C;

/**
 * Set FP64 scalar parameters for precision-critical simulations
 *
 * Call this BEFORE sdirk3_tile_unified_step_zerocopy_v2() if FP64 precision is needed.
 * Pass NULL to clear FP64 settings (will fall back to float precision).
 *
 * THREAD SAFETY:
 *   MUST be called BEFORE any call to stepping functions
 *   MUST NOT be called concurrently with solver stepping on the same solver
 *   Safe to call between time steps when solver is idle
 *
 * @param solver_ptr   Solver handle from sdirk3_tile_solver_create_zerocopy()
 * @param fp64_ptr     Pointer to FP64 parameters struct, or NULL to clear
 */
void sdirk3_set_fp64_scalars(void* solver_ptr, const SDIRK3_ScalarParams_FP64_C* fp64_ptr);

// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass33+: ZEROCOPY API DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════
// These APIs provide the recommended zero-copy interface for WRF integration.
// Struct definitions: wrf_sdirk3_interface_params.h (C++), module_implicit_sdirk3.F (Fortran, sole bridge)
//
// Usage pattern:
//   1. sdirk3_tile_solver_create_zerocopy() - Create solver
//   2. sdirk3_tile_set_base_state_zerocopy[_v2]() - Set base state
//   3. sdirk3_tile_unified_step_zerocopy_v2() - Time step (in loop)
//   4. sdirk3_tile_solver_destroy_zerocopy() - Cleanup

// Forward declarations for struct parameters (defined in wrf_sdirk3_interface_params.h)
struct SDIRK3_IndexBounds;
struct SDIRK3_Dimensions;
struct SDIRK3_ScalarParams;
struct SDIRK3_BoundaryConfig;

/**
 * Create a zerocopy tile solver
 *
 * @param nx,ny,nz Domain dimensions
 * @param dx,dy Grid spacing
 * @param rdnw_ptr Pointer to reciprocal vertical spacing array (nz elements)
 * @param tile_id Unique tile identifier
 * @param nx_u,ny_v,nz_w Staggered dimensions
 * @return Solver handle (void*) or NULL on failure
 */
void* sdirk3_tile_solver_create_zerocopy(
    int nx, int ny, int nz,
    float dx, float dy,
    float* rdnw_ptr,
    int tile_id,
    int nx_u, int ny_v, int nz_w);

/**
 * Destroy a zerocopy tile solver
 *
 * IMPORTANT: Always call this before deallocating the solver to ensure
 * TLS cache invalidation and proper cleanup.
 *
 * @param solver_ptr Solver handle from sdirk3_tile_solver_create_zerocopy()
 */
void sdirk3_tile_solver_destroy_zerocopy(void* solver_ptr);

/**
 * Query the number of saved 4DVAR trajectory checkpoints in a zerocopy solver.
 *
 * Returns 0 when solver_ptr is NULL or invalid.
 */
int sdirk3_tile_solver_get_saved_trajectory_count_zerocopy(void* solver_ptr);

/**
 * Query the global timestep associated with the saved trajectory in a zerocopy solver.
 *
 * Returns 0 when solver_ptr is NULL or invalid.
 */
int sdirk3_tile_solver_get_saved_global_timestep_zerocopy(void* solver_ptr);

/**
 * Clear saved 4DVAR trajectory checkpoints in a zerocopy solver.
 *
 * Safe no-op when solver_ptr is NULL or invalid.
 */
void sdirk3_tile_solver_clear_saved_trajectory_zerocopy(void* solver_ptr);

/**
 * Query packed state-vector size used by SDIRK3 replay-adjoint APIs.
 *
 * Returns 0 when solver_ptr is NULL or invalid.
 */
int sdirk3_tile_solver_get_state_vector_size_zerocopy(void* solver_ptr);

/**
 * Run checkpoint-replay adjoint loop on saved trajectory.
 *
 * @param solver_ptr         Solver handle
 * @param lambda_terminal    Terminal adjoint vector (packed state layout)
 * @param lambda_size        Number of elements in lambda_terminal/lambda_initial
 * @param lambda_initial     Output buffer for initial adjoint vector
 * @param dt                 Timestep used by forward window
 * @param gamma              SDIRK diagonal coefficient
 * @param gmres_restart      GMRES restart size for transpose solves
 * @param gmres_max_iter     GMRES max iterations for transpose solves
 * @param gmres_tol          GMRES relative tolerance for transpose solves
 * @return Number of replayed checkpoints (> 0) on success, -1 on error.
 *         ERRORS (fail-closed, review round 3): no checkpoints recorded (an
 *         identity adjoint is never returned as success), and any run where the
 *         split-explicit forward path was active or configured (its composite
 *         VJP is not implemented until Inc 7 — the implicit-transpose replay
 *         would produce a stale/wrong adjoint).
 */
int sdirk3_tile_solver_run_adjoint_replay_zerocopy(
    void* solver_ptr,
    const float* lambda_terminal,
    int lambda_size,
    float* lambda_initial,
    float dt,
    float gamma,
    int gmres_restart,
    int gmres_max_iter,
    float gmres_tol);

/**
 * Set base state for zerocopy solver (original interface)
 *
 * @param solver_ptr Solver handle
 * @param pb_ptr,alb_ptr,phb_ptr,mub_ptr Base state arrays at memory start
 * @param its,ite,jts,jte,kts,kte Tile bounds
 * @param ims,ime,jms,jme,kms,kme Memory bounds
 * @param sin_alpha_x/y,cos_alpha_x/y Terrain slope angles
 * @param div_damp_opt,div_damp_coef Divergence damping
 * @param diff_6th_opt,diff_6th_factor,diff_6th_slopeopt,diff_6th_thresh 6th-order diffusion
 * @param smagorinsky_opt,c_s,c_k Smagorinsky diffusion
 */
void sdirk3_tile_set_base_state_zerocopy(
    void* solver_ptr,
    float* pb_ptr, float* alb_ptr, float* phb_ptr, float* mub_ptr,
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme,
    float* sin_alpha_x, float* sin_alpha_y,
    float* cos_alpha_x, float* cos_alpha_y,
    int div_damp_opt, float div_damp_coef,
    int diff_6th_opt, float diff_6th_factor,
    int diff_6th_slopeopt, float diff_6th_thresh,
    int smagorinsky_opt, float c_s, float c_k);

/**
 * Set base state for zerocopy solver (v2 interface with thermal base state)
 *
 * Extended version that includes theta_base for proper thermal consistency.
 */
void sdirk3_tile_set_base_state_zerocopy_v2(
    void* solver_ptr,
    float* pb_ptr, float* alb_ptr, float* phb_ptr, float* mub_ptr,
    float* theta_base_ptr,
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme,
    float* sin_alpha_x, float* sin_alpha_y,
    float* cos_alpha_x, float* cos_alpha_y,
    int div_damp_opt, float div_damp_coef,
    int diff_6th_opt, float diff_6th_factor,
    int diff_6th_slopeopt, float diff_6th_thresh,
    int smagorinsky_opt, float c_s, float c_k);

/**
 * Perform one SDIRK3 time step using zerocopy interface (v2 struct-based)
 *
 * This is the PRIMARY stepping function for WRF integration.
 * Uses packed structs to work around gfortran BIND(C) 127-parameter limit.
 *
 * THREAD SAFETY:
 *   - Each solver instance is NOT thread-safe
 *   - Use one solver per thread/tile in OpenMP parallel regions
 *   - TLS caches are per-thread and automatically managed
 *
 * @param solver_ptr Solver handle from sdirk3_tile_solver_create_zerocopy()
 * @param u_ptr...t_ptr State variable pointers (8 3D arrays)
 * @param moist_ptr,n_moist Moisture array and count
 * @param ru_tend_ptr...t_tend_ptr Tendency pointers (8 3D arrays)
 * @param rdnw_ptr...cosa_ptr Grid metric arrays (18 pointers)
 * @param cqu_ptr,cqv_ptr,cqw_ptr Moisture correction factors
 * @param u_bdy_xs...mu_btend_ye Boundary arrays (48 pointers)
 * @param fcx,gcx,fcy,gcy Relaxation coefficients
 * @param bounds_ptr Tile/domain/memory bounds struct
 * @param dims_ptr Dimensions struct (nx,ny,nz,nx_u,ny_v,nz_w,rk_step)
 * @param scalars_ptr Scalar parameters struct (rdx,rdy,kdamp,etc.)
 * @param bdy_cfg_ptr Boundary configuration struct
 */
#define SDIRK3_STEP_OUTCOME_OK_ADVANCED 0
#define SDIRK3_STEP_OUTCOME_OK_SKIPPED 1
#define SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS 10
#define SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT 20
#define SDIRK3_STEP_OUTCOME_FATAL_INPUT 100
#define SDIRK3_STEP_OUTCOME_FATAL_INTERNAL 101

void sdirk3_tile_unified_step_zerocopy_v2(
    void* solver_ptr,
    /* State variables (8) */
    float* u_ptr, float* v_ptr, float* w_ptr,
    float* ph_ptr, float* al_ptr, float* mu_ptr,
    float* p_ptr, float* t_ptr,
    float* moist_ptr, int n_moist,
    /* Tendencies (8) */
    float* ru_tend_ptr, float* rv_tend_ptr, float* rw_tend_ptr,
    float* ph_tend_ptr, float* al_tend_ptr, float* mu_tend_ptr,
    float* p_tend_ptr, float* t_tend_ptr,
    /* Grid metrics (18) */
    float* rdnw_ptr, float* rdn_ptr,
    float* msftx_ptr, float* msfty_ptr,
    float* msfux_ptr, float* msfuy_ptr,
    float* msfvx_ptr, float* msfvy_ptr,
    float* c1f_ptr, float* c2f_ptr,
    float* c1h_ptr, float* c2h_ptr,
    float* fnm_ptr, float* fnp_ptr,
    float* f_ptr, float* e_ptr,
    float* sina_ptr, float* cosa_ptr,
    /* Moisture corrections (3) */
    float* cqu_ptr, float* cqv_ptr, float* cqw_ptr,
    /* Boundary arrays (48) */
    float* u_bdy_xs, float* u_bdy_xe, float* v_bdy_xs, float* v_bdy_xe,
    float* w_bdy_xs, float* w_bdy_xe, float* t_bdy_xs, float* t_bdy_xe,
    float* ph_bdy_xs, float* ph_bdy_xe, float* mu_bdy_xs, float* mu_bdy_xe,
    float* u_bdy_ys, float* u_bdy_ye, float* v_bdy_ys, float* v_bdy_ye,
    float* w_bdy_ys, float* w_bdy_ye, float* t_bdy_ys, float* t_bdy_ye,
    float* ph_bdy_ys, float* ph_bdy_ye, float* mu_bdy_ys, float* mu_bdy_ye,
    float* u_btend_xs, float* u_btend_xe, float* v_btend_xs, float* v_btend_xe,
    float* w_btend_xs, float* w_btend_xe, float* t_btend_xs, float* t_btend_xe,
    float* ph_btend_xs, float* ph_btend_xe, float* mu_btend_xs, float* mu_btend_xe,
    float* u_btend_ys, float* u_btend_ye, float* v_btend_ys, float* v_btend_ye,
    float* w_btend_ys, float* w_btend_ye, float* t_btend_ys, float* t_btend_ye,
    float* ph_btend_ys, float* ph_btend_ye, float* mu_btend_ys, float* mu_btend_ye,
    /* Relaxation coefficients (4) */
    float* fcx, float* gcx, float* fcy, float* gcy,
    /* Struct parameters (4) */
    const struct SDIRK3_IndexBounds* bounds_ptr,
    const struct SDIRK3_Dimensions* dims_ptr,
    const struct SDIRK3_ScalarParams* scalars_ptr,
    const struct SDIRK3_BoundaryConfig* bdy_cfg_ptr);

/**
 * Query the last step outcome for a zerocopy tile solver.
 *
 * @param solver_ptr Solver handle from sdirk3_tile_solver_create_zerocopy()
 * @param outcome_code Output step outcome code (SDIRK3_STEP_OUTCOME_*)
 * @param final_update_aborted Output 1 when stage-abort path skipped final update
 * @param progress_ratio Output update/ref norm ratio when available
 * @param progress_ratio_valid Output 1 when progress_ratio is valid
 * @return 1 on success, 0 on invalid solver or query failure
 */
int sdirk3_tile_solver_get_last_step_outcome_zerocopy(
    void* solver_ptr,
    int* outcome_code,
    int* final_update_aborted,
    float* progress_ratio,
    int* progress_ratio_valid);

#ifdef __cplusplus
}
#endif

#endif // WRF_SDIRK3_INTERFACE_H
