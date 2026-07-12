/**
 * @file wrf_sdirk3_interface_zerocopy.cpp
 * @brief Zero-Copy C interface for WRF-SDIRK3 solver
 * 
 * This file provides zero-copy C-compatible functions that can be called from Fortran
 * without the overhead of data copying and temporary memory allocation
 */

#include "wrf_sdirk3_interface.h"
#include "wrf_sdirk3_interface_params.h"
#include "wrf_sdirk3_tile_unified.h"
#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_unified_rhs_extended.h"
#include "wrf_sdirk3_profiler.h"
#include "wrf_sdirk3_halo_exchange.h"
// FIX 2025-01-11 Round72: Removed <atomic> - no longer needed after Round70 switch to per-solver state
// FIX Round108: Re-added <atomic> for TLS cache generation counter
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <limits>

// FIX Round85: OpenMP support for parallel cache invalidation
#ifdef _OPENMP
#include <omp.h>
#endif

// Use the TileSDIRK3UnifiedSolver class from global namespace
using TileSDIRK3UnifiedSolver = ::TileSDIRK3UnifiedSolver;

// ============================================================================
// FIX Round111: MULTI-INTERFACE DESIGN DOCUMENTATION
// ============================================================================
// This project has MULTIPLE solver interfaces with SEPARATE registries:
//
// 1. ZEROCOPY INTERFACE (this file) - PRIMARY for WRF integration
//    - Solver type: TileSDIRK3UnifiedSolver
//    - Registry: g_tile_solvers (void* key) + g_tile_solvers_mutex
//    - TLS cache: Yes, with g_solver_generation for invalidation
//    - solver_unique_id: Used for debugging/tracing
//    - Functions: sdirk3_tile_solver_create_zerocopy, _destroy_zerocopy, etc.
//
// 2. LEGACY TILE INTERFACE (wrf_sdirk3_tile_interface.cpp)
//    - Solver type: TileSDIRK3Solver
//    - Registry: wrf::sdirk3::g_tile_solvers + g_solver_mutex
//    - TLS cache: No
//    - Functions: sdirk3_tile_solver_create, _destroy
//
// 3. UNIFIED INTERFACE (wrf_sdirk3_unified_solver.cpp)
//    - Solver type: UnifiedSDIRK3Solver
//    - Registry: g_tile_solvers (int key) + g_solver_mutex
//    - TLS cache: No
//    - Functions: sdirk3_unified_solver_create, _destroy
//
// IMPORTANT: The TLS cache, g_solver_generation, and solver_unique_id are
// ONLY relevant to the zerocopy interface. Other interfaces don't need them
// because they don't have TLS caching.
// ============================================================================
//
// FIX Round148: LOGGING NOTE - std::cerr INTERLEAVING IN MULTI-THREADED EXECUTION
// All debug output uses std::cerr directly. In multi-threaded (OpenMP) execution,
// output from different threads may INTERLEAVE, making logs hard to parse.
// WORKAROUND: Set debug_level from single thread, or post-process logs by thread ID.
// FUTURE: If precise analysis needed, consider:
//   - Thread-local stringstream buffer with periodic flush
//   - Structured logging (JSON) with thread_id field
//   - External logging library (spdlog, etc.) with thread-safe buffering
//
// FIX Round150: STREAM UNIFICATION RECOMMENDATION
// This codebase uses std::cerr exclusively for debug output.
// DO NOT mix std::cout and std::cerr in the same execution path because:
//   1. stdout and stderr may have different buffering policies (line vs full)
//   2. Interleaved output order is platform-dependent and unpredictable
//   3. Redirected outputs (2>&1) may still show unexpected ordering
// RECOMMENDATION: Use std::cerr for ALL diagnostic/debug output in this module.
// If stdout is needed for user-facing output, ensure clear separation of concerns.
// ============================================================================

// Global solver registry - maps solver pointer to actual solver object
// This is defined outside namespace so it can be extern'd from other files
std::mutex g_tile_solvers_mutex;
std::unordered_map<void*, std::unique_ptr<TileSDIRK3UnifiedSolver>> g_tile_solvers;

// FIX Round109: Global generation counter + per-solver unique ID (hybrid approach).
//
// Problem (Round108): Global generation - any destroy invalidates ALL TLS caches.
// If solver A is destroyed, solver B's TLS cache is unnecessarily invalidated.
//
// Solution (Round109): Hybrid approach with revalidation fast path:
// 1. g_solver_generation: Increments on any solver destroy (for safety)
// 2. solver_unique_id: Each solver has unique ID (in WRFGridInfo)
// 3. TLS fast path: If generation mismatches but solver_ptr matches, REVALIDATE
//    by checking if the SAME solver is still in registry (pointer identity check)
// 4. If same solver: update cached generation without full re-lookup
// 5. If different solver: full slow path re-cache
//
// This avoids UAF while minimizing cross-solver cache invalidation.
//
// FIX Round110: PERFORMANCE TRADE-OFF DOCUMENTATION
// Current: Global generation is still incremented on ANY destroy, requiring all threads
//   to revalidate their TLS caches (though revalidation is cheap if solver unchanged).
// Impact: In high solver-churn scenarios (frequent create/destroy), mutex contention
//   may increase as multiple threads hit the revalidation path simultaneously.
// Mitigation: The revalidation path is O(1) hash lookup + pointer compare, much faster
//   than full re-lookup with cache miss. For most WRF workloads (few long-lived solvers),
//   this is not a bottleneck.
// Future: If perf-critical, could explore per-solver generation stored in solver registry
//   (not WRFGridInfo) with TLS storing solver+generation pair. However, this adds
//   complexity and the hybrid approach is sufficient for current use cases.
static std::atomic<uint64_t> g_solver_generation{0};

// FIX Round109/Round112: Counter for assigning unique IDs to solvers at creation.
// Stored in WRFGridInfo::solver_unique_id for debugging/tracing and TLS cache validation.
//
// WRAPAROUND SAFETY (FIX Round112):
// - Type is uint64_t: can create 18 quintillion solvers before wraparound
// - At 1 million solver creates/second, wraparound takes ~584,942 years
// - Counter starts at 1 (0 = invalid/uninitialized)
// - Counter is process-global and monotonic (never reset during process lifetime)
// - If process restarts, counter resets to 1, but all TLS caches are also cleared
//   (new process = new thread contexts), so no stale pointer risk
// - ASSUMPTION: No code path resets this counter during process lifetime
static std::atomic<uint64_t> g_next_solver_unique_id{1};  // Start at 1 (0 = invalid)

namespace {
void set_solver_step_outcome_if_present(void* solver_ptr,
                                        int outcome_code,
                                        int final_update_aborted,
                                        float progress_ratio,
                                        int progress_ratio_valid) {
    if (!solver_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return;
    }
    it->second->setLastStepOutcome(
        outcome_code,
        final_update_aborted != 0,
        progress_ratio,
        progress_ratio_valid != 0);
}
}  // namespace

// v10: C entry point for WRF Fortran to pass Cartesian communicator
// Located in this file (not wrf_sdirk3_interface.cpp) because Makefile.physics
// and Makefile.simple only compile wrf_sdirk3_interface_zerocopy.cpp.
extern "C" void sdirk3_set_mpi_comm(int fortran_comm,
                                     int periodic_x, int periodic_y) {
#if defined(DMPARALLEL) || defined(DM_PARALLEL)
    wrf::sdirk3::set_wrf_communicator(
        static_cast<MPI_Fint>(fortran_comm),
        periodic_x != 0, periodic_y != 0);
#else
    (void)fortran_comm; (void)periodic_x; (void)periodic_y;
#endif
}

extern "C" {

/**
 * NEW: Struct-based Zero-Copy SDIRK3 interface (reduces parameter count from 127 to ~70)
 *
 * This version uses packed structs to work around gfortran BIND(C) limitations
 * with large parameter counts. The old version with 127 individual parameters
 * suffers from ABI corruption after ~79 parameters.
 */
extern "C" void sdirk3_tile_unified_step_zerocopy_v2(
    void* solver_ptr,
    // State variable pointers (8 pointers + 1 int = 9)
    float* u_ptr, float* v_ptr, float* w_ptr,
    float* ph_ptr, float* al_ptr, float* mu_ptr,
    float* p_ptr, float* t_ptr,
    float* moist_ptr, int n_moist,
    // Tendency pointers (8 pointers)
    float* ru_tend_ptr, float* rv_tend_ptr, float* rw_tend_ptr,
    float* ph_tend_ptr, float* al_tend_ptr, float* mu_tend_ptr,
    float* p_tend_ptr, float* t_tend_ptr,
    // Grid metric arrays (18 pointers)
    float* rdnw_ptr, float* rdn_ptr,
    float* msftx_ptr, float* msfty_ptr,
    float* msfux_ptr, float* msfuy_ptr,
    float* msfvx_ptr, float* msfvy_ptr,
    float* c1f_ptr, float* c2f_ptr,
    float* c1h_ptr, float* c2h_ptr,
    float* fnm_ptr, float* fnp_ptr,
    float* f_ptr, float* e_ptr,
    float* sina_ptr, float* cosa_ptr,
    // Moisture correction factors (3 pointers)
    float* cqu_ptr, float* cqv_ptr, float* cqw_ptr,
    // Boundary arrays (48 pointers - X/Y value/tendency for 12 variables)
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
    // Relaxation coefficients (4 pointers)
    float* fcx, float* gcx, float* fcy, float* gcy,
    // STRUCT POINTERS - replaces 36 individual parameters with 4 pointers
    const SDIRK3_IndexBounds* bounds_ptr,      // 18 ints (tile/domain/memory bounds)
    const SDIRK3_Dimensions* dims_ptr,         // 7 ints (nx,ny,nz,nx_u,ny_v,nz_w,rk_step)
    const SDIRK3_ScalarParams* scalars_ptr,    // 7 floats (rdx,rdy,kdamp,khdif,kvdif,dtbc,dt)
    const SDIRK3_BoundaryConfig* bdy_cfg_ptr)  // 4 ints (spec_bdy_width,spec_zone,relax_zone,padding)
{
    try {
    // FIX Round150: Gate verbose debug output with compile-time flag or debug_level >= 3.
    // These outputs run EVERY STEP and cause severe log spam + performance overhead.
    // Enable via: -DWRF_SDIRK3_VERBOSE_V2_DEBUG or set debug_level >= 3 at runtime.
#if defined(WRF_SDIRK3_VERBOSE_V2_DEBUG)
    std::cerr << "\n=== ENTERED sdirk3_tile_unified_step_zerocopy_v2 ===" << std::endl;
    std::cerr << "  bounds_ptr=" << bounds_ptr << " dims_ptr=" << dims_ptr
              << " scalars_ptr=" << scalars_ptr << " bdy_cfg_ptr=" << bdy_cfg_ptr << std::endl;
#endif

    // Defensive null checks on struct pointers
    if (!bounds_ptr || !dims_ptr || !scalars_ptr || !bdy_cfg_ptr) {
        std::cerr << "=== FATAL: NULL struct pointer in v2 ===" << std::endl;
        set_solver_step_outcome_if_present(
            solver_ptr,
            SDIRK3_STEP_OUTCOME_FATAL_INPUT,
            1,
            0.0f,
            0);
        return;
    }

    // Extract struct contents
    const auto& bounds = *bounds_ptr;
    const auto& dims = *dims_ptr;
    const auto& scalars = *scalars_ptr;
    const auto& bdy_cfg = *bdy_cfg_ptr;

    // FIX Round150: Gate ABI validation output (runs every step)
#if defined(WRF_SDIRK3_VERBOSE_V2_DEBUG)
    std::cerr << "\n=== sdirk3_tile_unified_step_zerocopy_v2 (STRUCT-BASED) ===" << std::endl;
    std::cerr << "  solver_ptr = " << solver_ptr << std::endl;
    std::cerr << "  Bounds: its=" << bounds.its << " ite=" << bounds.ite
              << " jts=" << bounds.jts << " jte=" << bounds.jte << std::endl;
    std::cerr << "  Dims: nx=" << dims.nx << " ny=" << dims.ny << " nz=" << dims.nz << std::endl;
    std::cerr << "  Scalars: rdx=" << scalars.rdx << " rdy=" << scalars.rdy << " dt=" << scalars.dt << std::endl;
#endif

    // Validate bounds - if these fail, we still have ABI issues
    if (bounds.ims >= bounds.ime || bounds.jms >= bounds.jme || bounds.kms >= bounds.kme ||
        bounds.its < bounds.ims || bounds.ite > bounds.ime ||
        bounds.jts < bounds.jms || bounds.jte > bounds.jme ||
        bounds.kts < bounds.kms || bounds.kte > bounds.kme) {
        std::cerr << "=== FATAL: Invalid bounds in struct ===" << std::endl;
        std::cerr << "  Memory bounds: ims=" << bounds.ims << " ime=" << bounds.ime
                  << " (should have ims < ime)" << std::endl;
        std::cerr << "                 jms=" << bounds.jms << " jme=" << bounds.jme
                  << " (should have jms < jme)" << std::endl;
        std::cerr << "                 kms=" << bounds.kms << " kme=" << bounds.kme
                  << " (should have kms < kme)" << std::endl;
        std::cerr << "  Tile bounds:   its=" << bounds.its << " ite=" << bounds.ite << std::endl;
        std::cerr << "                 jts=" << bounds.jts << " jte=" << bounds.jte << std::endl;
        std::cerr << "                 kts=" << bounds.kts << " kte=" << bounds.kte << std::endl;
        set_solver_step_outcome_if_present(
            solver_ptr,
            SDIRK3_STEP_OUTCOME_FATAL_INPUT,
            1,
            0.0f,
            0);
        return;
    }

    // Retrieve the solver from the registry
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        std::cerr << "SDIRK3 v2 ERROR: Invalid solver pointer" << std::endl;
        return;
    }

    auto& solver = *(it->second);

    // Create a zero-copy configuration
    wrf::sdirk3::ZeroCopyConfig config;
    config.u_ptr = u_ptr;
    config.v_ptr = v_ptr;
    config.w_ptr = w_ptr;
    config.ph_ptr = ph_ptr;
    config.al_ptr = al_ptr;
    config.mu_ptr = mu_ptr;
    config.moist_ptr = moist_ptr;
    config.n_moist = n_moist;
    config.qv_ptr = nullptr;
    config.p_ptr = p_ptr;
    config.t_ptr = t_ptr;

    config.ru_tend_ptr = ru_tend_ptr;
    config.rv_tend_ptr = rv_tend_ptr;
    config.rw_tend_ptr = rw_tend_ptr;
    config.ph_tend_ptr = ph_tend_ptr;
    config.al_tend_ptr = al_tend_ptr;
    config.mu_tend_ptr = mu_tend_ptr;
    config.p_tend_ptr = p_tend_ptr;
    config.t_tend_ptr = t_tend_ptr;

    // FIX Round150: Gate O(n) validation scans - SEVERE PERFORMANCE IMPACT per-step
    // Enable via: -DWRF_SDIRK3_VERBOSE_V2_DEBUG (compile-time only for safety)
#if defined(WRF_SDIRK3_VERBOSE_V2_DEBUG)
    {
        int nx = dims.nx;
        int ny = dims.ny;
        int nz = dims.nz;
        // FIX Round192: Use int64_t to prevent overflow on large grids
        int64_t total = static_cast<int64_t>(nx) * ny * nz;

        std::cerr << "\n=== FORTRAN INPUT DATA VALIDATION ===" << std::endl;
        std::cerr << "  Grid dimensions: nx=" << nx << " ny=" << ny << " nz=" << nz << std::endl;
        std::cerr << "  Total points: " << total << std::endl;

        // Check u velocity
        if (u_ptr) {
            float u_min = 1e30f, u_max = -1e30f;
            for (int idx = 0; idx < total && idx < 100000; ++idx) {
                u_min = std::min(u_min, u_ptr[idx]);
                u_max = std::max(u_max, u_ptr[idx]);
            }
            std::cerr << "  u range: [" << u_min << ", " << u_max << "]" << std::endl;
        }

        // Check v velocity
        if (v_ptr) {
            float v_min = 1e30f, v_max = -1e30f;
            for (int idx = 0; idx < total && idx < 100000; ++idx) {
                v_min = std::min(v_min, v_ptr[idx]);
                v_max = std::max(v_max, v_ptr[idx]);
            }
            std::cerr << "  v range: [" << v_min << ", " << v_max << "]" << std::endl;
        }

        // Check mu (dry air mass)
        if (mu_ptr) {
            float mu_min = 1e30f, mu_max = -1e30f, mu_sum = 0.0f;
            int mu_total = nx * ny;  // mu is 2D
            for (int idx = 0; idx < mu_total && idx < 10000; ++idx) {
                mu_min = std::min(mu_min, mu_ptr[idx]);
                mu_max = std::max(mu_max, mu_ptr[idx]);
                mu_sum += mu_ptr[idx];
            }
            float mu_mean = mu_sum / std::min(mu_total, 10000);
            std::cerr << "  mu range: [" << mu_min << ", " << mu_max << "] mean=" << mu_mean << " Pa" << std::endl;
        }

        // Check t (potential temperature perturbation)
        if (t_ptr) {
            float t_min = 1e30f, t_max = -1e30f;
            for (int idx = 0; idx < total && idx < 100000; ++idx) {
                t_min = std::min(t_min, t_ptr[idx]);
                t_max = std::max(t_max, t_ptr[idx]);
            }
            std::cerr << "  t range: [" << t_min << ", " << t_max << "] K" << std::endl;
        }

        // Check tendencies
        if (ru_tend_ptr) {
            float ru_min = 1e30f, ru_max = -1e30f;
            for (int idx = 0; idx < total && idx < 100000; ++idx) {
                ru_min = std::min(ru_min, ru_tend_ptr[idx]);
                ru_max = std::max(ru_max, ru_tend_ptr[idx]);
            }
            std::cerr << "  ru_tend range: [" << ru_min << ", " << ru_max << "]" << std::endl;
        }

        std::cerr << "=== END FORTRAN INPUT VALIDATION ===" << std::endl;
    }
#endif  // WRF_SDIRK3_VERBOSE_V2_DEBUG

    config.rdx = scalars.rdx;
    config.rdy = scalars.rdy;
    // FIX 2025-01-11 Round61: Also set FP64 rdx/rdy from float scalars as fallback
    // For true FP64 precision, caller should use sdirk3_set_fp64_scalars() BEFORE this call
    // which sets grid_info->dx_fp64/dy_fp64 directly (takes precedence in advanceZeroCopy)
    config.rdx_fp64 = static_cast<double>(scalars.rdx);
    config.rdy_fp64 = static_cast<double>(scalars.rdy);
    config.kdamp = scalars.kdamp;
    config.khdif = scalars.khdif;
    config.kvdif = scalars.kvdif;
    config.rdnw_ptr = rdnw_ptr;
    config.rdn_ptr = rdn_ptr;

    config.msftx_ptr = msftx_ptr;
    config.msfty_ptr = msfty_ptr;
    config.msfux_ptr = msfux_ptr;
    config.msfuy_ptr = msfuy_ptr;
    config.msfvx_ptr = msfvx_ptr;
    config.msfvy_ptr = msfvy_ptr;

    config.c1f_ptr = c1f_ptr;
    config.c2f_ptr = c2f_ptr;
    config.c1h_ptr = c1h_ptr;
    config.c2h_ptr = c2h_ptr;

    config.fnm_ptr = fnm_ptr;
    config.fnp_ptr = fnp_ptr;

    config.f_ptr = f_ptr;
    config.e_ptr = e_ptr;
    config.sina_ptr = sina_ptr;
    config.cosa_ptr = cosa_ptr;

    config.cqu_ptr = cqu_ptr;
    config.cqv_ptr = cqv_ptr;
    config.cqw_ptr = cqw_ptr;

    config.u_bdy_xs = u_bdy_xs;   config.u_bdy_xe = u_bdy_xe;
    config.v_bdy_xs = v_bdy_xs;   config.v_bdy_xe = v_bdy_xe;
    config.w_bdy_xs = w_bdy_xs;   config.w_bdy_xe = w_bdy_xe;
    config.t_bdy_xs = t_bdy_xs;   config.t_bdy_xe = t_bdy_xe;
    config.ph_bdy_xs = ph_bdy_xs; config.ph_bdy_xe = ph_bdy_xe;
    config.mu_bdy_xs = mu_bdy_xs; config.mu_bdy_xe = mu_bdy_xe;

    config.u_bdy_ys = u_bdy_ys;   config.u_bdy_ye = u_bdy_ye;
    config.v_bdy_ys = v_bdy_ys;   config.v_bdy_ye = v_bdy_ye;
    config.w_bdy_ys = w_bdy_ys;   config.w_bdy_ye = w_bdy_ye;
    config.t_bdy_ys = t_bdy_ys;   config.t_bdy_ye = t_bdy_ye;
    config.ph_bdy_ys = ph_bdy_ys; config.ph_bdy_ye = ph_bdy_ye;
    config.mu_bdy_ys = mu_bdy_ys; config.mu_bdy_ye = mu_bdy_ye;

    config.u_btend_xs = u_btend_xs;   config.u_btend_xe = u_btend_xe;
    config.v_btend_xs = v_btend_xs;   config.v_btend_xe = v_btend_xe;
    config.w_btend_xs = w_btend_xs;   config.w_btend_xe = w_btend_xe;
    config.t_btend_xs = t_btend_xs;   config.t_btend_xe = t_btend_xe;
    config.ph_btend_xs = ph_btend_xs; config.ph_btend_xe = ph_btend_xe;
    config.mu_btend_xs = mu_btend_xs; config.mu_btend_xe = mu_btend_xe;

    config.u_btend_ys = u_btend_ys;   config.u_btend_ye = u_btend_ye;
    config.v_btend_ys = v_btend_ys;   config.v_btend_ye = v_btend_ye;
    config.w_btend_ys = w_btend_ys;   config.w_btend_ye = w_btend_ye;
    config.t_btend_ys = t_btend_ys;   config.t_btend_ye = t_btend_ye;
    config.ph_btend_ys = ph_btend_ys; config.ph_btend_ye = ph_btend_ye;
    config.mu_btend_ys = mu_btend_ys; config.mu_btend_ye = mu_btend_ye;

    config.fcx_ptr = fcx;  config.gcx_ptr = gcx;
    config.fcy_ptr = fcy;  config.gcy_ptr = gcy;

    config.spec_bdy_width = bdy_cfg.spec_bdy_width;
    config.spec_zone = bdy_cfg.spec_zone;
    config.relax_zone = bdy_cfg.relax_zone;
    config.dtbc = scalars.dtbc;

    config.its = bounds.its; config.ite = bounds.ite;
    config.jts = bounds.jts; config.jte = bounds.jte;
    config.kts = bounds.kts; config.kte = bounds.kte;

    config.ids = bounds.ids; config.ide = bounds.ide;
    config.jds = bounds.jds; config.jde = bounds.jde;
    config.kds = bounds.kds; config.kde = bounds.kde;

    config.ims = bounds.ims; config.ime = bounds.ime;
    config.jms = bounds.jms; config.jme = bounds.jme;
    config.kms = bounds.kms; config.kme = bounds.kme;

    config.nx = dims.nx; config.ny = dims.ny; config.nz = dims.nz;
    config.nx_u = dims.nx_u; config.ny_v = dims.ny_v; config.nz_w = dims.nz_w;

    // TEST-ONLY fault injection (review round 3g negative regression): with
    // WRF_SDIRK3_TEST_STAGGER_MISMATCH_AT=N (default: unset = off, zero
    // behavior change), the N-th and later v2 calls pass nx_u=nx — simulating
    // a caller whose per-call extents disagree with the solver's initialized
    // geometry. The per-call geometry validation in advanceZeroCopy must
    // REJECT it (throw), proving mismatches are re-checked on EVERY call and
    // never approved via cached members.
    {
        static std::atomic<int> v2_call_counter{0};
        const int call_no = ++v2_call_counter;
        const char* inj_env = std::getenv("WRF_SDIRK3_TEST_STAGGER_MISMATCH_AT");
        if (inj_env && *inj_env) {
            const int inj_at = std::atoi(inj_env);
            if (inj_at > 0 && call_no >= inj_at) {
                std::cerr << "[SDIRK3 TEST] injecting stagger mismatch nx_u=nx at v2 call "
                          << call_no << " (WRF_SDIRK3_TEST_STAGGER_MISMATCH_AT="
                          << inj_at << ")" << std::endl;
                config.nx_u = config.nx;
            }
        }
    }

    // Set WRF indices in the solver
    solver.setWRFIndices(bounds.its, bounds.ite, bounds.jts, bounds.jte, bounds.kts, bounds.kte,
                        bounds.ids, bounds.ide, bounds.jds, bounds.jde, bounds.kds, bounds.kde,
                        bounds.ims, bounds.ime, bounds.jms, bounds.jme, bounds.kms, bounds.kme);

    // Call advanceZeroCopy
    // FIX Round151: Gate per-step V2 debug output with compile-time flag
#if defined(WRF_SDIRK3_VERBOSE_V2_DEBUG)
    std::cerr << "=== V2: Calling solver.advanceZeroCopy ===" << std::endl;
#endif
    solver.advanceZeroCopy(config, dims.rk_step, scalars.dt);
#if defined(WRF_SDIRK3_VERBOSE_V2_DEBUG)
    std::cerr << "=== V2: advanceZeroCopy completed ===" << std::endl;
#endif

    // Print profiling summary if enabled (debug_level >= 1)
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        wrf::sdirk3::ProfileAccumulator::instance().print_summary();
    }
    } catch (const std::exception& e) {
        set_solver_step_outcome_if_present(
            solver_ptr,
            SDIRK3_STEP_OUTCOME_FATAL_INTERNAL,
            1,
            0.0f,
            0);
        std::cerr << "SDIRK3 v2 ERROR: Exception in sdirk3_tile_unified_step_zerocopy_v2: "
                  << e.what() << std::endl;
    } catch (...) {
        set_solver_step_outcome_if_present(
            solver_ptr,
            SDIRK3_STEP_OUTCOME_FATAL_INTERNAL,
            1,
            0.0f,
            0);
        std::cerr << "SDIRK3 v2 ERROR: Unknown exception in sdirk3_tile_unified_step_zerocopy_v2"
                  << std::endl;
    }
}

extern "C" int sdirk3_tile_solver_get_last_step_outcome_zerocopy(
    void* solver_ptr,
    int* outcome_code,
    int* final_update_aborted,
    float* progress_ratio,
    int* progress_ratio_valid) {
    if (outcome_code) {
        *outcome_code = SDIRK3_STEP_OUTCOME_FATAL_INPUT;
    }
    if (final_update_aborted) {
        *final_update_aborted = 1;
    }
    if (progress_ratio) {
        *progress_ratio = 0.0f;
    }
    if (progress_ratio_valid) {
        *progress_ratio_valid = 0;
    }
    if (!solver_ptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return 0;
    }

    TileSDIRK3UnifiedSolver* solver = it->second.get();
    if (outcome_code) {
        *outcome_code = solver->getLastStepOutcomeCode();
    }
    if (final_update_aborted) {
        *final_update_aborted = solver->getLastStepFinalUpdateAborted() ? 1 : 0;
    }
    if (progress_ratio) {
        *progress_ratio = solver->getLastStepProgressRatio();
    }
    if (progress_ratio_valid) {
        *progress_ratio_valid = solver->isLastStepProgressRatioValid() ? 1 : 0;
    }
    return 1;
}

/**
 * OLD: Zero-Copy version of the SDIRK3 tile unified step (127 parameters - DEPRECATED)
 *
 * This function receives pointers directly to WRF grid arrays and operates
 * on them in-place without any data copying.
 *
 * DEPRECATED: This version suffers from gfortran BIND(C) ABI corruption with 127 parameters.
 * Use sdirk3_tile_unified_step_zerocopy_v2 instead.
 *
 * Key differences from the original version:
 * 1. No temporary arrays are allocated
 * 2. Data is accessed directly from WRF grid memory
 * 3. Results are written directly back to WRF grid memory
 * 4. Fortran column-major to C++ row-major conversion is handled on-the-fly
 */

/**
 * Create a tile solver and register it
 * Now receives actual rdnw values from WRF
 * Updated to receive staggered dimensions for proper grid configuration
 */
void* sdirk3_tile_solver_create_zerocopy(
    int nx, int ny, int nz,
    float dx, float dy,
    float* rdnw_ptr,
    int tile_id,
    int nx_u, int ny_v, int nz_w)  // Add staggered dimensions
{
    // v20.14: Ensure env var overrides are loaded (once per process).
    // wrf_sdirk3_load_config_from_namelist() is never called from Fortran,
    // so load_from_env() was never reached. Call it here on first solver creation.
    // P0 FIX (2026-07-12, external review): solver creation runs under an OpenMP tile loop —
    // an unsynchronized `static bool` here let multiple threads race load_from_env() against
    // concurrent reads of the global config. std::call_once serializes the one-time load.
    {
        static std::once_flag env_once;
        std::call_once(env_once, [] { wrf::sdirk3::g_sdirk3_config.load_from_env(); });
    }

    // FIX Round150: Gate solver creation debug output with debug_level >= 2
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "=== C++ ENTRY: sdirk3_tile_solver_create_zerocopy ===" << std::endl;
        std::cerr << "    Before any LibTorch calls" << std::endl;
        std::cerr << "    nx=" << nx << ", ny=" << ny << ", nz=" << nz << ", tile_id=" << tile_id << std::endl;
        std::cerr << "    dx=" << dx << ", dy=" << dy << std::endl;
        std::cerr << "    nx_u=" << nx_u << ", ny_v=" << ny_v << ", nz_w=" << nz_w << std::endl;
        std::cerr << "    About to enter try block..." << std::endl;
    }
    
    try {
        // Create vectors for grid metrics
        std::vector<float> rdx(1, 1.0f/dx);
        std::vector<float> rdy(1, 1.0f/dy);
        
        // Use actual rdnw values from WRF
        std::vector<float> rdnw(nz);
        if (rdnw_ptr != nullptr) {
            // Copy the rdnw values from WRF
            // FIX Round194: size_t cast to prevent overflow
            std::memcpy(rdnw.data(), rdnw_ptr, static_cast<size_t>(nz) * sizeof(float));
        } else {
            // Fallback: Create dummy values if not provided (for backward compatibility)
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "WARNING: rdnw_ptr is NULL, creating dummy values" << std::endl;

            }
            float dz_base = 250.0f;  // Base vertical spacing in meters
            for (int k = 0; k < nz; ++k) {
                float stretch_factor = 1.0f + 0.01f * k;  // Gradual stretching
                float dz = dz_base * stretch_factor;
                rdnw[k] = 1.0f / dz;  // rdnw is reciprocal of vertical spacing
            }
        }
        
        // FIX Round151: Gate solver creation debug output with debug_level >= 2
        // Previously unconditionally logged "Always log solver creation" - now gated.
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "\n=== sdirk3_tile_solver_create_zerocopy DEBUG ===" << std::endl;
            std::cerr << "  Creating solver with dimensions:" << std::endl;
            std::cerr << "  nx=" << nx << ", ny=" << ny << ", nz=" << nz << std::endl;
            std::cerr << "  Staggered dimensions: nx_u=" << nx_u << ", ny_v=" << ny_v << ", nz_w=" << nz_w << std::endl;
        }

        // FIX Round151: Converted std::cout → std::cerr for stream unification
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
            std::cerr << "sdirk3_tile_solver_create_zerocopy: Creating solver with:" << std::endl;
            std::cerr << "  nx=" << nx << ", ny=" << ny << ", nz=" << nz << std::endl;
            std::cerr << "  dx=" << dx << ", dy=" << dy << std::endl;
            std::cerr << "  rdnw_ptr=" << (void*)rdnw_ptr << " (from WRF)" << std::endl;
            std::cerr << "  First 5 rdnw values: ";
            for (int i = 0; i < std::min(5, nz); ++i) {
                std::cerr << rdnw[i] << " ";
            }
            std::cerr << std::endl;
            std::cerr << "  Last 5 rdnw values: ";
            for (int i = std::max(0, nz-5); i < nz; ++i) {
                std::cerr << rdnw[i] << " ";
            }
            std::cerr << std::endl;
        }
        
        // FIX Round150: Gate solver creation debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "=== CRITICAL: About to create TileSDIRK3UnifiedSolver ===" << std::endl;
            std::cerr << "    This is where LibTorch initialization likely occurs" << std::endl;
        }

        auto solver = std::make_unique<TileSDIRK3UnifiedSolver>(
            nx, ny, nz, dx, dy, rdx, rdy, rdnw, tile_id);

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "=== SUCCESS: TileSDIRK3UnifiedSolver created ===" << std::endl;
            std::cerr << "=== BEFORE setStaggeredDimensions ===" << std::endl;
            std::cerr << "  About to call with nx_u=" << nx_u << ", ny_v=" << ny_v << ", nz_w=" << nz_w << std::endl;
        }

        // Set the staggered dimensions after construction
        solver->setStaggeredDimensions(nx_u, ny_v, nz_w);

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "=== AFTER setStaggeredDimensions ===" << std::endl;
            std::cerr << "  Successfully set staggered dimensions" << std::endl;
        }

        // FIX Round109/Round112/Round118: Assign unique ID to solver for debugging/tracing and TLS cache.
        //
        // CRITICAL ORDER (FIX Round118): solver_unique_id MUST be assigned BEFORE
        // the solver is inserted into g_tile_solvers. This prevents TLS cache from
        // seeing a solver with id=0 (default) which would cause false positive matches.
        // The sequence is:
        //   1. Create solver (id defaults to 0)
        //   2. Assign unique_id (this block)
        //   3. Insert into registry (below)
        //
        // GRID_INFO STABILITY (FIX Round112):
        // The grid_info_ shared_ptr in TileSDIRK3UnifiedSolver is created once during
        // construction (via std::make_shared) and never reassigned. It remains stable
        // for the solver's entire lifetime. Therefore:
        // - solver_unique_id is set once here and never needs preservation on "reallocation"
        // - There is no grid_info reallocation/swap path in the zerocopy interface
        // - TLS cache can safely rely on solver_unique_id being constant per solver
        auto grid_info = solver->getGridInfo();
        if (grid_info) {
            grid_info->solver_unique_id = g_next_solver_unique_id.fetch_add(1, std::memory_order_relaxed);
        }

        void* ptr = static_cast<void*>(solver.get());

        // FIX Round151: Gate solver pointer debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "=== SOLVER POINTER DEBUG ===" << std::endl;
            std::cerr << "  Solver pointer: " << ptr << std::endl;
            std::cerr << "  Solver address: " << &solver << std::endl;
            if (grid_info) {
                std::cerr << "  Solver unique_id: " << grid_info->solver_unique_id << std::endl;
            }
        }

        // FIX Round118: Insert into registry AFTER unique_id assignment (see comment above)
        {
            std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
            g_tile_solvers[ptr] = std::move(solver);
        }

        // FIX Round151: Gate solver registration debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "  Solver successfully registered in global map" << std::endl;
            std::cerr << "  Returning pointer: " << ptr << std::endl;
        }
        
        return ptr;
    } catch (const std::exception& e) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "SDIRK3 Zero-Copy ERROR: Failed to create solver: " 
                  << e.what() << std::endl;

        }
        return nullptr;
    }
}

/**
 * Destroy a tile solver and unregister it
 *
 * FIX Round108: Increments g_solver_generation to invalidate all TLS caches.
 * This prevents stale pointer issues if solver addresses are reused.
 */
void sdirk3_tile_solver_destroy_zerocopy(void* solver_ptr)
{
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    g_tile_solvers.erase(solver_ptr);
    // FIX Round108: Increment generation to invalidate TLS caches
    // Any thread with a cached pointer to this solver will see the generation
    // mismatch on next access and re-acquire the mutex to verify.
    ++g_solver_generation;
}

int sdirk3_tile_solver_get_saved_trajectory_count_zerocopy(void* solver_ptr)
{
    if (!solver_ptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return 0;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        return 0;
    }

    return static_cast<int>(unified_solver->getSavedTrajectoryCount());
}

int sdirk3_tile_solver_get_saved_global_timestep_zerocopy(void* solver_ptr)
{
    if (!solver_ptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return 0;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        return 0;
    }

    return unified_solver->getSavedGlobalTimestep();
}

void sdirk3_tile_solver_clear_saved_trajectory_zerocopy(void* solver_ptr)
{
    if (!solver_ptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        return;
    }

    unified_solver->clearSavedTrajectory();
}

int sdirk3_tile_solver_get_state_vector_size_zerocopy(void* solver_ptr)
{
    if (!solver_ptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return 0;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        return 0;
    }

    const int64_t state_size = unified_solver->getStateVectorSize();
    if (state_size <= 0 || state_size > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return 0;
    }

    return static_cast<int>(state_size);
}

int sdirk3_tile_solver_run_adjoint_replay_zerocopy(
    void* solver_ptr,
    const float* lambda_terminal,
    int lambda_size,
    float* lambda_initial,
    float dt,
    float gamma,
    int gmres_restart,
    int gmres_max_iter,
    float gmres_tol)
{
    if (!solver_ptr || !lambda_terminal || !lambda_initial || lambda_size <= 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end() || !it->second) {
        return -1;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        return -1;
    }

    const int64_t expected_size = unified_solver->getStateVectorSize();
    if (expected_size <= 0 || expected_size != static_cast<int64_t>(lambda_size)) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3 4DVAR replay ERROR: lambda size mismatch (got "
                      << lambda_size << ", expected " << expected_size << ")" << std::endl;
        }
        return -1;
    }

    try {
        auto opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto lambda_terminal_tensor = torch::from_blob(
            const_cast<float*>(lambda_terminal),
            {expected_size},
            opts).clone();

        auto lambda_initial_tensor = unified_solver->runAdjointReplay(
            lambda_terminal_tensor,
            dt,
            gamma,
            gmres_restart,
            gmres_max_iter,
            gmres_tol);

        auto lambda_cpu = lambda_initial_tensor.detach().to(torch::kCPU, torch::kFloat32).contiguous();
        std::memcpy(
            lambda_initial,
            lambda_cpu.data_ptr<float>(),
            static_cast<size_t>(expected_size) * sizeof(float));

        const size_t replayed = unified_solver->getSavedTrajectoryCount();
        if (replayed == 0) {
            // Review round 3: 0 checkpoints replayed must be an ERROR at this ABI
            // boundary too (runAdjointReplay already throws on empty checkpoints;
            // this keeps the C contract explicit even across binary skew).
            std::cerr << "SDIRK3 4DVAR replay ERROR: 0 checkpoints replayed" << std::endl;
            return -1;
        }
        if (replayed > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return std::numeric_limits<int>::max();
        }
        return static_cast<int>(replayed);
    } catch (const std::exception& e) {
        std::cerr << "SDIRK3 4DVAR replay ERROR: " << e.what() << std::endl;
        return -1;
    }
}

/**
 * FIX 2025-01-11 Round75/Round76/Round77/Round78: Lightweight reset (per-solver state only)
 *
 * Resets per-solver logging state and device cache. This is the RECOMMENDED function
 * for production use as it has minimal overhead.
 *
 * FIX Round140: THREAD SCOPE
 *   - Affects: Per-solver state only (shared across threads)
 *   - Does NOT affect: TLS caches (use reset_full_parallel for TLS reset)
 *
 * What it resets:
 *   - FP64 transition logging state
 *   - No-reference-device warning flag
 *   - Device cache
 *
 * What it does NOT reset (use sdirk3_tile_solver_reset_full for these):
 *   - Divergence cache
 *   - MSF 3D expansion cache
 *   - Static metric caches
 *   - Pressure gradient caches
 *
 * FIX Round93: FP64 PRECISION MODE RETENTION
 *   - fp64_explicit, dx_fp64, dy_fp64 are NOT cleared by this function
 *   - If reusing solver with DIFFERENT precision mode, explicitly call:
 *       sdirk3_set_fp64_scalars(solver_ptr, NULL)  // Clear FP64 mode (revert to FP32)
 *     OR
 *       sdirk3_set_fp64_scalars(solver_ptr, &new_fp64_params)  // Set new FP64 values
 *   - This is intentional: precision mode is a configuration, not per-step state
 *
 * CALLING CONDITIONS (Round78):
 *   - MUST be called when solver is NOT actively computing
 *   - Do NOT call during GPU kernel execution or while tensors are being accessed
 *   - Safe to call between timesteps or at simulation boundaries
 *
 * Fortran binding: sdirk3_tile_solver_reset_state (module_implicit_sdirk3_zerocopy.F)
 */
void sdirk3_tile_solver_reset_state(void* solver_ptr)
{
    if (!solver_ptr) return;  // FIX Round78: Null pointer safety

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it != g_tile_solvers.end() && it->second) {
        // FIX 2025-01-11 Round78: Lightweight reset - per-solver state only
        auto grid_info = it->second->getGridInfo();
        if (grid_info) {
            grid_info->resetPerSolverState();
        }

        // FIX Round96: Reset TLS solver tracking for this thread
        // This provides more frequent reset opportunities than reset_full() alone,
        // preventing TLS throttling counter from growing too large in long runs.
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
        wrf::sdirk3::tls_debug::resetTLSTracking();
#endif

        // FIX Round119: tls_release_check_counter (sampling counter for release-mode TLS validation)
        // is now reset by resetTLSTracking() above. It was moved from function-local static to
        // wrf::sdirk3::tls_debug namespace for deterministic diagnostic reset capability.
        // See wrf_sdirk3_tile_unified.h tls_debug::tls_release_check_counter.

        // FIX Round119: TLS SOLVER LOOKUP CACHE PRESERVATION
        // The TLS solver lookup cache (tls_cached_solver_ptr/generation/unique_id in getGridInfo())
        // is intentionally NOT invalidated here. Rationale:
        //   1. The solver remains valid in g_tile_solvers registry after reset
        //   2. TLS lookup cache correctly reflects "solver is valid" - no stale data
        //   3. Forcing revalidation would just re-lock mutex to find the same solver
        //   4. Per-solver state (warnings, device cache) is reset via resetPerSolverState()
        // If you need to force TLS revalidation (e.g., for testing), destroy and recreate the solver.

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3: Lightweight reset (per-solver state) for solver " << solver_ptr << std::endl;
        }
    }
}

/**
 * FIX 2025-01-11 Round78: Full reset including all internal caches
 *
 * Performs comprehensive reset of per-solver state AND all internal caches.
 * Use this for test harnesses or when complete cache invalidation is required.
 *
 * FIX Round140: THREAD SCOPE
 *   - Affects: Per-solver state + solver caches (shared) + calling thread TLS
 *   - Does NOT affect: Other threads' TLS (use reset_full_parallel for all-thread TLS)
 *
 * What it resets:
 *   1. Per-solver state (warning flags, device cache, logging state)
 *   2. Solver internal caches via invalidateCaches():
 *      - Divergence cache
 *      - MSF 3D expansion cache
 *      - Static metric caches (rdz, dnw, dn)
 *      - UnifiedRHS acoustic metric caches
 *      - Pressure gradient caches (scalar and aligned tensor)
 *
 * CALLING CONDITIONS:
 *   - MUST be called when solver is NOT actively computing
 *   - Do NOT call during GPU kernel execution - may cause sync/contention
 *   - Safe to call between timesteps, at simulation boundaries, or between test cases
 *   - This function is HEAVY - use sdirk3_tile_solver_reset_state() for production
 *
 * Use cases:
 *   1. Test harnesses that reuse solver instances across test cases
 *   2. External integrations with solver pooling/recycling
 *   3. Debug scenarios requiring complete cache purge
 *
 * Fortran binding: sdirk3_tile_solver_reset_full (module_implicit_sdirk3_zerocopy.F)
 */
void sdirk3_tile_solver_reset_full(void* solver_ptr)
{
    if (!solver_ptr) return;  // FIX Round78: Null pointer safety

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it != g_tile_solvers.end() && it->second) {
        // FIX 2025-01-11 Round78: Reset per-solver state (warnings, logging, device cache)
        auto grid_info = it->second->getGridInfo();
        if (grid_info) {
            grid_info->resetPerSolverState();
        }

        // FIX Round95: Reset TLS solver tracking for this thread
        // This ensures fresh debug tracking when solver is reused
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
        wrf::sdirk3::tls_debug::resetTLSTracking();
#endif

        // FIX 2025-01-11 Round81: SINGLE ENTRY POINT for all cache invalidation
        // invalidateCaches() now internally calls grid_info_->invalidateVerticalMetricCaches()
        // which covers ALL caches:
        //   - Solver caches: divergence, MSF 3D, acoustic metrics, pressure gradient
        //   - Grid metric caches: z1d, dz_min, lat_cpu, static metrics, scalar mean
        // See wrf_sdirk3_tile_unified.h for complete inventory.
        it->second->invalidateCaches();

        // FIX Round119: TLS SOLVER LOOKUP CACHE PRESERVATION
        // The TLS solver lookup cache (tls_cached_solver_ptr/generation/unique_id in getGridInfo())
        // is intentionally NOT invalidated by reset_full. This is DIFFERENT from solver caches above.
        // Rationale:
        //   1. TLS lookup cache answers "is solver in registry?" - solver is still registered
        //   2. invalidateCaches() clears SOLVER INTERNAL caches (divergence, metrics, etc.)
        //   3. g_solver_generation only increments on solver DESTRUCTION, not reset
        //   4. Forcing TLS revalidation would just re-lock mutex to confirm same solver
        // The solver caches ARE fully invalidated; TLS lookup cache validity is unaffected.
        // If you need to force TLS revalidation, destroy and recreate the solver.
        //
        // FIX Round120: RECYCLED SOLVER BEHAVIOR
        // When a solver is recycled for a different grid configuration:
        //   - TLS lookup cache remains valid (points to same solver, same unique_id)
        //   - Solver INTERNAL caches (grid data) are cleared by invalidateCaches() above
        //   - This is correct: TLS cache identifies the solver, doesn't cache grid data
        //   - If solver_unique_id or generation were changed, TLS would auto-refresh on
        //     next access due to mismatch detection in getGridInfo() revalidation path
        // For explicit TLS cache clearing (debug/testing), you can either:
        //   (a) Destroy and recreate the solver (g_solver_generation increments)
        //   (b) Accept that TLS correctly identifies the recycled solver

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3: Full reset (per-solver state + all caches) for solver " << solver_ptr << std::endl;
        }
    }
}

/**
 * FIX Round85: Full reset with proper OpenMP parallel thread_local cache handling
 *
 * This function correctly handles the separation of global vs thread_local caches:
 *   1. Master thread: per-solver state + global/epoch-based caches
 *   2. All threads (parallel): thread_local caches only
 *
 * This avoids redundant global epoch increments that would occur if invalidateCaches()
 * were called from every thread in a parallel region.
 *
 * Use this instead of reset_full() when:
 *   - Running with OpenMP enabled (OMP_NUM_THREADS > 1)
 *   - Need deterministic cache state across all worker threads
 *   - Test harnesses requiring complete cache isolation
 *
 * FIX Round103: CRITICAL LIMITATION when called from INSIDE a parallel region:
 * ═══════════════════════════════════════════════════════════════════════════════
 * If omp_in_parallel() is true, this function can only reset TLS for the MASTER
 * thread. Other worker threads' TLS caches remain STALE.
 *
 * REQUIRED FOLLOW-UP for complete TLS reset in parallel region:
 *   Option 1 (RECOMMENDED): Call reset_full_parallel() BEFORE !$OMP PARALLEL
 *   Option 2: Have ALL threads call reset_tls_parallel() individually:
 *             !$OMP PARALLEL
 *             CALL sdirk3_tile_solver_reset_tls_parallel(solver_ptr)
 *             !$OMP BARRIER
 *             ... use solver ...
 *             !$OMP END PARALLEL
 *   Option 3: Accept that only master thread has clean TLS state
 *
 * Failure to follow up may cause stale TLS cache issues in worker threads.
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Fortran binding: sdirk3_tile_solver_reset_full_parallel (module_implicit_sdirk3_zerocopy.F)
 */
void sdirk3_tile_solver_reset_full_parallel(void* solver_ptr)
{
    if (!solver_ptr) return;

    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it != g_tile_solvers.end() && it->second) {
        // FIX Round85: Per-solver state (master thread only)
        auto grid_info = it->second->getGridInfo();
        if (grid_info) {
            grid_info->resetPerSolverState();
        }

        // FIX Round85/Round88/Round89: Combined global + TLS cache invalidation
#ifdef _OPENMP
        // FIX Round88/Round89: Check if already in parallel region to prevent nested team creation
        if (omp_in_parallel()) {
            // FIX Round89: Enforce master-only execution when called from parallel region
            // If each thread calls reset_full_parallel(), only master should actually do work
            // This prevents redundant invalidation when caller mistakenly invokes per-thread
            if (omp_get_thread_num() != 0) {
                // FIX Round90: Warn when non-master thread calls this function
                // This helps enforce master-only policy and detect incorrect usage
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                    // Only log at debug_level >= 2 to avoid spam in production
                    #pragma omp critical
                    {
                        std::cerr << "SDIRK3 WARNING: Non-master thread " << omp_get_thread_num()
                                  << " called reset_full_parallel() - skipped (master-only policy)\n"
                                  << "  Hint: Call reset_full_parallel() from master thread only, "
                                  << "or use #pragma omp single before the call." << std::endl;
                    }
                }
                return;
            }
            // FIX Round101: Master thread in parallel region: do serial invalidation (avoid nested teams)
            // ═══════════════════════════════════════════════════════════════════════════
            // CRITICAL LIMITATION: This only invalidates TLS cache for master thread!
            // Other threads' TLS caches REMAIN STALE. Callers must ensure one of:
            //   1. Call reset_full_parallel() BEFORE entering the parallel region
            //   2. Have ALL threads call sdirk3_tile_solver_reset_tls_parallel() separately
            //   3. Accept that only master thread has clean TLS state
            // For solver pooling / multi-thread tests, use (1) or (2) for consistency.
            // ═══════════════════════════════════════════════════════════════════════════
            it->second->invalidateGlobalCachesOnly();
            it->second->invalidateThreadLocalCachesOnly();
            // FIX Round95: Reset TLS tracking (master thread only in this path)
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
            wrf::sdirk3::tls_debug::resetTLSTracking();
#endif
            // FIX Round102: WARN_ONCE pattern to avoid log spam on repeated calls
            static bool warned_omp_in_parallel_limitation = false;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && !warned_omp_in_parallel_limitation) {
                warned_omp_in_parallel_limitation = true;
                std::cerr << "SDIRK3 WARNING: Master-only reset (in parallel region, TLS only for master) for solver "
                          << solver_ptr << " (WARN_ONCE)\n"
                          << "  Other threads' TLS caches NOT reset! Call reset_full_parallel() BEFORE parallel region.\n"
                          << "  Or use sdirk3_tile_solver_reset_tls_parallel() from each thread."
                          << std::endl;
            }
        } else {
            // FIX Round88: Single parallel region with master+barrier for efficiency
            // This avoids team creation overhead compared to separate parallel regions
            #pragma omp parallel
            {
                #pragma omp master
                {
                    // Global caches: master thread only (avoids redundant epoch increments)
                    it->second->invalidateGlobalCachesOnly();
                }
                #pragma omp barrier
                // TLS caches: all threads after global is complete
                it->second->invalidateThreadLocalCachesOnly();
                // FIX Round95: Reset TLS tracking for each thread
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
                wrf::sdirk3::tls_debug::resetTLSTracking();
#endif
            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                int num_threads = omp_get_max_threads();
                std::cerr << "SDIRK3: Parallel full reset (global + " << num_threads
                          << " thread TLS caches) for solver " << solver_ptr << std::endl;
            }
        }
#else
        // Non-OpenMP build: just call both invalidations sequentially
        it->second->invalidateGlobalCachesOnly();
        it->second->invalidateThreadLocalCachesOnly();
        // FIX Round95: Reset TLS tracking (single thread in non-OpenMP)
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
        wrf::sdirk3::tls_debug::resetTLSTracking();
#endif
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3: Full reset (non-OpenMP, single thread) for solver " << solver_ptr << std::endl;
        }
#endif
    }
}

/**
 * FIX Round101/Round102/Round107: Lightweight TLS-only parallel reset API
 *
 * Resets ONLY thread-local (TLS) caches for the CALLING thread.
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
 * FIX Round102: OpenMP vs Non-OpenMP behavior:
 *   - OpenMP build: Each calling thread resets its own TLS cache. Call from all threads.
 *   - Non-OpenMP build: Serial fallback - resets single-thread TLS state.
 *     Effect is minimal in non-OpenMP; use reset_full() for complete reset instead.
 *
 * FIX Round107: Performance optimization - Thread-local solver cache
 *   - First call: Acquires mutex, looks up solver, caches in thread-local storage
 *   - Subsequent calls (same solver_ptr): Cache hit, NO mutex acquisition
 *   - Cache is per-thread and auto-invalidated when solver_ptr changes
 *   - WARNING: Cache does NOT auto-invalidate on solver destruction. Caller must
 *     ensure solver lifetime spans all potential cached accesses.
 *
 * Thread safety: First call per thread acquires mutex; subsequent calls are lock-free.
 * However, the solver state (including per-solver warning flags like
 * warned_dz_rdz_device_mismatch) assumes single-thread access per solver.
 * If multiple threads access the same solver, ensure external synchronization.
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * ⛔ MANDATORY CONTRACT (FIX Round105/Round107): NO CONCURRENT DESTROY
 * ═══════════════════════════════════════════════════════════════════════════════
 * The solver MUST remain alive while ANY thread is in this function or may call it.
 * This is NOT a recommendation - it is a MANDATORY CONTRACT. Violations cause:
 *   - Use-after-free (undefined behavior)
 *   - Memory corruption
 *   - Unpredictable crashes
 *
 * The debug check (#ifndef NDEBUG) detects violations AFTER they occur, but CANNOT
 * prevent them. In release builds, violations silently corrupt memory.
 *
 * MANDATORY usage patterns (choose one):
 *   1. Call from parallel region where solver lifetime is guaranteed by outer scope
 *   2. Use !$OMP BARRIER after all TLS reset calls, before any solver destroy
 *   3. Use reference counting or shared_ptr at application level if ownership unclear
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * @param solver_ptr Solver handle (used for validation, not modified)
 */
void sdirk3_tile_solver_reset_tls_parallel(void* solver_ptr)
{
    if (!solver_ptr) return;

    // FIX Round107/Round108/Round109/Round111: Thread-local cache with hybrid revalidation.
    // Each thread caches its last verified (solver_ptr -> solver -> generation -> unique_id).
    //
    // FIX Round109: Hybrid approach to minimize cross-solver cache invalidation:
    // 1. Fast path: solver_ptr + generation both match → cache hit (no mutex)
    // 2. Revalidation path: solver_ptr matches but generation doesn't → check if SAME
    //    solver still exists (by unique_id). If yes, update generation only.
    // 3. Slow path: full mutex lookup for cache miss or different solver at same address.
    //
    // FIX Round111: Added tls_cached_unique_id for more robust validation.
    // During revalidation, we compare unique_id instead of raw pointer identity.
    // This is more reliable because unique_id is guaranteed unique per solver lifetime,
    // while pointer addresses can be reused. NOTE: This doesn't reduce mutex hits
    // (we still need mutex to read current solver's unique_id), but improves validation
    // robustness and provides better debugging info when mismatches occur.
    //
    // FIX Round112: WHY MUTEX CANNOT BE AVOIDED ON REVALIDATION PATH:
    // The map lookup on revalidation is REQUIRED for safety, not just convenience.
    // Consider this scenario WITHOUT map verification:
    //   1. Thread A caches solver_ptr=0x1000, solver=SolverX, unique_id=42
    //   2. SolverX is destroyed (g_solver_generation increments)
    //   3. New SolverY is created at SAME address 0x1000 with unique_id=43
    //   4. Thread A's fast path fails (generation mismatch)
    //   5. Thread A's revalidation: solver_ptr still matches, but solver is DIFFERENT
    //   6. WITHOUT map lookup: Thread A would use stale cached pointer → USE-AFTER-FREE
    //   7. WITH map lookup: Thread A verifies solver exists, compares unique_id → safe
    //
    // Alternative approaches considered and rejected:
    //   - Atomic "alive" flag per solver: Race condition between flag check and use
    //   - Lock-free concurrent map: Major complexity, marginal benefit for WRF workload
    //   - RCU (Read-Copy-Update): Overkill for typical <16 solvers per process
    //   - Caching raw WRFGridInfo*: Same problem - need to verify it's still valid
    //
    // PERFORMANCE IMPACT: The revalidation path only triggers when:
    //   (a) Another solver was destroyed (rare in steady-state WRF)
    //   (b) Thread's first access after destroy event
    // In typical WRF usage, the fast path (no mutex) handles >99% of calls.
    // The mutex is uncontended in most cases (short critical section, different solvers).
    //
    // FIX Round113: MAINTENANCE NOTE - This rationale assumes g_tile_solvers is the sole
    // registry for zerocopy solvers. If future code adds additional registries (e.g., for
    // GPU-specific solvers or specialized solver types), this documentation and the TLS
    // cache validation logic MUST be updated to reflect the new source(s) of truth.
    // See MULTI-INTERFACE DESIGN DOCUMENTATION at top of file for registry inventory.
    static thread_local void* tls_cached_solver_ptr = nullptr;
    static thread_local wrf::sdirk3::TileSDIRK3Solver* tls_cached_solver = nullptr;
    static thread_local uint64_t tls_cached_generation = 0;
    static thread_local uint64_t tls_cached_unique_id = 0;  // FIX Round111
    // FIX Round119: Sampling counter moved to wrf::sdirk3::tls_debug namespace
    // (wrf_sdirk3_tile_unified.h) so resetTLSTracking() can reset it for deterministic diagnostics.
    // Access via wrf::sdirk3::tls_debug::tls_release_check_counter

    wrf::sdirk3::TileSDIRK3Solver* solver = nullptr;

    // FIX Round149: Snapshot debug_level at function start to avoid repeated hot-path reads.
    // This single read replaces multiple per-branch reads (fast_hit, reval_hit, slow_miss, stats).
    const int debug_level = wrf::sdirk3::g_sdirk3_config.debug_level;

    // FIX Round108/Round109: Read current generation (atomic, no mutex needed)
    const uint64_t current_generation = g_solver_generation.load(std::memory_order_acquire);

    // Fast path: check thread-local cache first (no mutex)
    // Cache hit requires: same solver_ptr, valid cached solver, AND same generation
    if (solver_ptr == tls_cached_solver_ptr &&
        tls_cached_solver != nullptr &&
        tls_cached_generation == current_generation) {
        // Perfect cache hit - no mutex needed
        solver = tls_cached_solver;
        // FIX Round142/Round144/Round145: Track cache hit for profiling (gated by debug_level)
        // Counter increment only when debug_level >= 2 to avoid hot path overhead.
        // FIX Round145/Round146/Round147: CONFIG STABILITY - debug_level is read per-access (no snapshot).
        //   ★ STRONG RECOMMENDATION ★: Set debug_level BEFORE starting compute, keep UNCHANGED.
        //   Runtime changes are STRONGLY DISCOURAGED in hot paths - they cause inconsistent
        //   profiling data and unnecessary memory reads. If you must change mid-run:
        //   use snapshot pattern (local_debug = g_config.debug_level at function start).
        //   Current design accepts partial metrics for simplicity; snapshot adds complexity.
        // FIX Round148: COMPILE-TIME GATE OPTION - For ZERO overhead in production builds:
        //   #ifdef WRF_SDIRK3_ENABLE_TLS_PROFILING
        //   if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) { ++counter; }
        //   #endif
        //   Build without -DWRF_SDIRK3_ENABLE_TLS_PROFILING to eliminate all counter code.
        //   Trade-off: Requires rebuild to enable profiling; runtime toggle not available.
        // FIX Round149: Use local debug_level snapshot (see function start)
        if (debug_level >= 2) {
            ++wrf::sdirk3::tls_debug::tls_cache_fast_hit;
        }

        // FIX Round113/Round114/Round115: Validate TLS cached unique_id matches actual solver.
        // This catches cache corruption or illegal solver_unique_id mutation.
        //
        // FIX Round114: Gated by macros to control performance impact:
        //   - WRF_SDIRK3_DEBUG_TLS_CACHE: Enable in debug builds (opt-in)
        //   - WRF_SDIRK3_RELEASE_TLS_CHECK: Enable in release builds (opt-in)
        //
        // FIX Round115: Release check uses global warning (not per-thread) with abort
        // threshold to prevent silent corruption across many threads.
        //
        // Default behavior:
        //   - Debug builds: assert (unless WRF_SDIRK3_DEBUG_TLS_CACHE not defined)
        //   - Release builds: no check (define WRF_SDIRK3_RELEASE_TLS_CHECK to enable)
#ifdef WRF_SDIRK3_DEBUG_TLS_CACHE
        {
            // OPT Pass34: Cast to derived class for getGridInfo() access
            auto* unified_solver_dbg = static_cast<TileSDIRK3UnifiedSolver*>(solver);
            auto debug_grid_info = unified_solver_dbg->getGridInfo();
            if (debug_grid_info && tls_cached_unique_id != 0) {
                assert(tls_cached_unique_id == debug_grid_info->solver_unique_id &&
                       "TLS cached unique_id doesn't match solver's actual unique_id! "
                       "This indicates cache corruption or solver_unique_id mutation.");
            }
        }
#elif defined(NDEBUG) && defined(WRF_SDIRK3_RELEASE_TLS_CHECK)
        // FIX Round115/Round116: Release-mode check with sampling and global abort threshold.
        // Opt-in via -DWRF_SDIRK3_RELEASE_TLS_CHECK to enable validation in release builds.
        //
        // FIX Round116: ODR SAFETY NOTE - This code is in wrf_sdirk3_interface_zerocopy.cpp
        // (a .cpp file, not a header). The static atomics below have a single instance
        // across all TUs because function-local statics in a non-inline function in a
        // single TU are guaranteed unique. Do NOT move this to a header without using
        // C++17 inline variables or extern linkage.
        //
        // FIX Round116: SAMPLING OPTIMIZATION - To reduce getGridInfo() overhead, we only
        // perform the full check every WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL calls (default 1000).
        // This provides statistical coverage while minimizing hot-path impact.
        {
            // FIX Round119: Use namespace-scoped counter (tls_debug::tls_release_check_counter)
            // so resetTLSTracking() can reset it for deterministic diagnostics.
            // Default interval from wrf_sdirk3_abi_version.h (SINGLE SOURCE OF TRUTH)
            if (++wrf::sdirk3::tls_debug::tls_release_check_counter < WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL) {
                // Skip check this iteration (sampling early-out)
            } else {
                wrf::sdirk3::tls_debug::tls_release_check_counter = 0;  // Reset counter

                // OPT Pass34: Cast to derived class for getGridInfo() access
                auto* unified_solver_rel = static_cast<TileSDIRK3UnifiedSolver*>(solver);
                auto release_grid_info = unified_solver_rel->getGridInfo();
                if (release_grid_info && tls_cached_unique_id != 0 &&
                    tls_cached_unique_id != release_grid_info->solver_unique_id) {
                    // Global atomic counter and flag for cross-thread coordination
                    // ODR-safe: single instance because this is in a .cpp file
                    // Default threshold from wrf_sdirk3_abi_version.h (SINGLE SOURCE OF TRUTH)
                    static std::atomic<bool> g_warned_unique_id_mismatch{false};
                    static std::atomic<uint32_t> g_unique_id_mismatch_count{0};
                    const uint32_t count = g_unique_id_mismatch_count.fetch_add(1, std::memory_order_relaxed) + 1;

                    // Global WARN_ONCE (first thread to hit logs the warning)
                    bool expected = false;
                    if (g_warned_unique_id_mismatch.compare_exchange_strong(expected, true)) {
                        std::cerr << "SDIRK3 CRITICAL: TLS cached unique_id ("
                                  << tls_cached_unique_id << ") != solver unique_id ("
                                  << release_grid_info->solver_unique_id << "). "
                                  << "Cache corruption or illegal mutation detected! "
                                  << "Will abort after " << WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD
                                  << " occurrences. (WARN_ONCE global)" << std::endl;
                    }

                    // Abort after threshold to prevent silent corruption
                    if (count >= WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD) {
                        std::cerr << "SDIRK3 FATAL: TLS unique_id mismatch count ("
                                  << count << ") exceeded abort threshold ("
                                  << WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD
                                  << "). Aborting to prevent silent corruption." << std::endl;
                        std::abort();
                    }
                }
            }
        }
#elif !defined(NDEBUG)
        // Debug build without WRF_SDIRK3_DEBUG_TLS_CACHE: standard assert
        {
            // OPT Pass34: Cast to derived class for getGridInfo() access
            auto* unified_solver = static_cast<TileSDIRK3UnifiedSolver*>(solver);
            auto debug_grid_info = unified_solver->getGridInfo();
            if (debug_grid_info && tls_cached_unique_id != 0) {
                assert(tls_cached_unique_id == debug_grid_info->solver_unique_id &&
                       "TLS cached unique_id doesn't match solver's actual unique_id! "
                       "This indicates cache corruption or solver_unique_id mutation.");
            }
        }
#endif
        // FIX Round115: Release builds without WRF_SDIRK3_RELEASE_TLS_CHECK skip the check
        // entirely for maximum hot-path performance. Define the macro to opt-in.
    } else if (solver_ptr == tls_cached_solver_ptr &&
               tls_cached_solver != nullptr &&
               tls_cached_generation != current_generation) {
        // FIX Round109/Round111: Revalidation fast path - generation mismatch but same solver_ptr.
        // Some OTHER solver was destroyed. Check if our cached solver is still valid.
        // This avoids full re-lookup when solver B destroys don't affect solver A.
        //
        // FIX Round111: Use unique_id comparison instead of raw pointer identity.
        // This is more robust because unique_id is guaranteed unique per solver lifetime,
        // while pointer comparison could theoretically have edge cases with address reuse.
        //
        // FIX Round118/Round123: OPTIMIZATION ANALYSIS - Mutex on revalidation path
        // The mutex is required here because we need to verify solver existence in the registry.
        // Potential optimizations considered and deferred:
        //   1. Lock-free concurrent map: Would avoid mutex but adds significant complexity
        //   2. RCU-style updates: Overkill for typical <16 solvers per process
        //   3. Generation delta check: If (current_generation - tls_cached_generation) > N,
        //      assume solver gone and skip mutex. REJECTED: Not safe, could miss valid solver.
        //   4. Per-solver "alive" atomic flag: Race condition between flag check and use.
        // Current design is optimal for WRF's typical usage pattern (stable solvers, rare churn).
        // If solver churn becomes a bottleneck, profile first before adding complexity.
        //
        // FIX Round123: HIGH-FREQUENCY CHURN MITIGATION GUIDANCE
        // If profiling shows this mutex is a bottleneck (>10% time in lock contention):
        //
        //   Option A - Reader-Writer Lock:
        //     Replace std::mutex with std::shared_mutex. Solver lookups (read-heavy) use
        //     shared_lock, while create/destroy (rare) uses unique_lock. ~50% improvement
        //     for read-heavy workloads with minimal code change.
        //
        //   Option B - Per-Solver Atomic State:
        //     Each solver maintains an atomic<uint64_t> combining unique_id and alive flag.
        //     Readers check atomic without mutex; writers update atomic then optionally
        //     signal mutex for registry cleanup. Requires careful memory ordering.
        //
        //   Option C - Lock-Free Registry:
        //     Use a lock-free hash map (e.g., folly::ConcurrentHashMap). Maximum scalability
        //     but adds external dependency and significant complexity.
        //
        // For WRF's typical use (stable solver set, rare churn), current mutex is adequate.
        // Implement alternatives only after profiling confirms contention is the bottleneck.
        std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
        auto it = g_tile_solvers.find(solver_ptr);
        if (it != g_tile_solvers.end() && it->second) {
            auto current_grid_info = it->second->getGridInfo();
            uint64_t current_unique_id = current_grid_info ? current_grid_info->solver_unique_id : 0;

            if (current_unique_id != 0 && current_unique_id == tls_cached_unique_id) {
                // FIX Round111: Same solver verified by unique_id - just update generation
                tls_cached_generation = current_generation;
                solver = tls_cached_solver;
                // FIX Round142/Round144/Round149: Track revalidation hit (use local snapshot)
                if (debug_level >= 2) {
                    ++wrf::sdirk3::tls_debug::tls_cache_reval_hit;
                }
            } else {
                // Different solver at same address (unique_id mismatch) - full re-cache
                solver = it->second.get();
                tls_cached_solver_ptr = solver_ptr;
                tls_cached_solver = solver;
                tls_cached_generation = current_generation;
                tls_cached_unique_id = current_unique_id;  // FIX Round111
            }
        } else {
            // Solver removed - invalidate cache
            tls_cached_solver_ptr = nullptr;
            tls_cached_solver = nullptr;
            tls_cached_generation = 0;
            tls_cached_unique_id = 0;  // FIX Round111
            wrf::sdirk3::tls_debug::tls_release_check_counter = 0;  // FIX Round119: Use namespaced counter
        }
    } else {
        // Full slow path: solver_ptr changed or cache was empty
        // FIX Round142/Round144/Round149: Track slow miss (use local snapshot)
        if (debug_level >= 2) {
            ++wrf::sdirk3::tls_debug::tls_cache_slow_miss;
        }
        // FIX Round104: Grab solver pointer then release mutex before TLS work
        // to avoid serializing all threads through the lock during invalidation.
        // FIX Round105: CRITICAL - This creates a window where solver could be destroyed
        // by another thread. Caller MUST guarantee solver lifetime during this call.
        // See "NO CONCURRENT DESTROY" in docstring above.
        {
            std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
            auto it = g_tile_solvers.find(solver_ptr);
            if (it != g_tile_solvers.end() && it->second) {
                solver = it->second.get();
                // FIX Round111: Also cache unique_id for robust revalidation
                auto grid_info = it->second->getGridInfo();
                uint64_t unique_id = grid_info ? grid_info->solver_unique_id : 0;
                // Update thread-local cache with current generation and unique_id
                tls_cached_solver_ptr = solver_ptr;
                tls_cached_solver = solver;
                tls_cached_generation = current_generation;
                tls_cached_unique_id = unique_id;  // FIX Round111
            } else {
                // Solver not found - invalidate cache
                tls_cached_solver_ptr = nullptr;
                tls_cached_solver = nullptr;
                tls_cached_generation = 0;
                tls_cached_unique_id = 0;  // FIX Round111
                wrf::sdirk3::tls_debug::tls_release_check_counter = 0;  // FIX Round119: Use namespaced counter
            }
        }
    }
    // Lock released here (if taken) - TLS invalidation proceeds without mutex
    // SAFETY: Caller guarantees solver won't be destroyed during this window

    // FIX Round142: LOG REVALIDATION STATISTICS (for profiling TLS cache effectiveness)
    // Output stats every TLS_LOG_THROTTLE_INTERVAL lookups to avoid log spam.
    // Metrics help diagnose if sdirk3_clear_tls_solver_cache() is called too frequently.
// FIX Round149: Use local debug_level snapshot for stats logging
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
    if (debug_level >= 2) {
        using namespace wrf::sdirk3::tls_debug;
        uint64_t total = tls_cache_fast_hit + tls_cache_reval_hit + tls_cache_slow_miss;
        if (total > 0 && total - tls_cache_last_logged_total >= TLS_LOG_THROTTLE_INTERVAL) {
            double fast_pct = 100.0 * tls_cache_fast_hit / total;
            double reval_pct = 100.0 * tls_cache_reval_hit / total;
            double slow_pct = 100.0 * tls_cache_slow_miss / total;
            // FIX Round149: Added solver_unique_id for multi-solver log differentiation
            // FIX Round153: Add _OPENMP guard for omp_get_thread_num()
#ifdef _OPENMP
            std::cerr << "SDIRK3 TLS CACHE STATS (thread=" << omp_get_thread_num()
                      << " solver_id=" << tls_cached_unique_id << "): "
                      << "total=" << total << " fast=" << fast_pct << "% "
                      << "reval=" << reval_pct << "% miss=" << slow_pct << "%" << std::endl;
#else
            std::cerr << "SDIRK3 TLS CACHE STATS (non-OpenMP"
                      << " solver_id=" << tls_cached_unique_id << "): "
                      << "total=" << total << " fast=" << fast_pct << "% "
                      << "reval=" << reval_pct << "% miss=" << slow_pct << "%" << std::endl;
#endif
            tls_cache_last_logged_total = total;
        }
    }
#endif

    if (solver) {
        // FIX Round101: TLS-only invalidation - each calling thread resets its own TLS
        // OPT Pass34: Cast to derived class for invalidateThreadLocalCachesOnly() access
        auto* unified_solver = static_cast<TileSDIRK3UnifiedSolver*>(solver);
        unified_solver->invalidateThreadLocalCachesOnly();
        // FIX Round122: Reset TLS tracking state (sampling counter, debug tracking)
        // When WRF_SDIRK3_NO_TLS_TRACKING is defined, tls_debug namespace doesn't exist,
        // so we guard the call. Compilers optimize away empty blocks in release builds.
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
        wrf::sdirk3::tls_debug::resetTLSTracking();
#endif

        // FIX Round106: Debug mode check for "NO CONCURRENT DESTROY" violation
        // Re-verify solver still exists after TLS work to catch concurrent destruction.
        // This check is intentionally after the work is done - if solver was destroyed,
        // we've already hit UB, but this helps detect the bug during testing.
#ifndef NDEBUG
        {
            std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
            auto it = g_tile_solvers.find(solver_ptr);
            if (it == g_tile_solvers.end() || !it->second ||
                it->second.get() != solver) {
                // FIX Round107/Round108/Round111: Clear TLS cache before aborting
                tls_cached_solver_ptr = nullptr;
                tls_cached_solver = nullptr;
                tls_cached_generation = 0;
                tls_cached_unique_id = 0;  // FIX Round111
                wrf::sdirk3::tls_debug::tls_release_check_counter = 0;  // FIX Round119: Use namespaced counter
                std::cerr << "SDIRK3 FATAL: NO CONCURRENT DESTROY violation detected!\n"
                          << "  Solver was destroyed while reset_tls_parallel() was in progress.\n"
                          << "  This is undefined behavior. Fix caller to ensure solver lifetime.\n"
                          << std::flush;
                std::abort();  // Fail fast in debug mode
            }
        }
#endif

        // FIX Round104: Use omp master instead of omp critical for logging efficiency
        // FIX Round104: Apply log_solver_pointer policy like other logs
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
#ifdef _OPENMP
            #pragma omp master
            {
                std::cerr << "SDIRK3: TLS-only reset for " << omp_get_max_threads() << " threads on solver ";
                if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                    std::cerr << solver_ptr;
                } else {
                    std::cerr << "<hidden>";
                }
                std::cerr << std::endl;
            }
#else
            std::cerr << "SDIRK3: TLS-only reset (non-OpenMP) for solver ";
            if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                std::cerr << solver_ptr;
            } else {
                std::cerr << "<hidden>";
            }
            std::cerr << std::endl;
#endif
        }
    }
}

/**
 * FIX Round124: Lightweight TLS debug tracking reset (current thread only)
 *
 * C API wrapper for tls_debug::resetTLSTracking() from wrf_sdirk3_tile_unified.h.
 * This is a minimal reset that only affects debug tracking counters, NOT the
 * actual TLS solver lookup cache.
 *
 * Use this for test harnesses that need to reset debug counters between tests
 * without the overhead of a full solver reset.
 *
 * Thread scope: Current calling thread only.
 */
void sdirk3_reset_tls_debug_tracking(void)
{
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
    wrf::sdirk3::tls_debug::resetTLSTracking();
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "SDIRK3: TLS debug tracking reset (current thread only)" << std::endl;
    }
#endif
    // When WRF_SDIRK3_NO_TLS_TRACKING is defined, this is a no-op
}

/**
 * FIX Round127/Round128: Explicit TLS solver lookup cache clear (current thread only)
 *
 * Clears the thread-local solver lookup cache, forcing re-lookup on next solver access.
 * This is useful for test harnesses that need deterministic cache behavior between tests,
 * or for solver recycling scenarios where explicit cache invalidation is desired.
 *
 * Thread scope: Current thread only. Call from each thread in multi-threaded scenarios.
 *
 * Note: For normal WRF operation, this is NOT required. The solver_unique_id mechanism
 * provides automatic protection against stale TLS cache after solver destroy/recreate.
 *
 * ZEROCOPY INTERFACE ONLY: This function only affects the zerocopy interface TLS cache.
 * Legacy tile and unified interfaces do not use TLS caching.
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * FIX Round128: OPTIMIZATION GUIDANCE FOR FREQUENT CACHE CLEARS
 * ═══════════════════════════════════════════════════════════════════════════════
 * Current implementation uses GLOBAL generation increment, which invalidates TLS
 * cache for ALL threads, even if only one thread/solver needs clearing.
 *
 * If profiling shows this is a bottleneck (e.g., high-frequency solver recycling):
 *
 *   Alternative A - Per-Solver Generation:
 *     Store generation counter in WRFGridInfo (solver_generation field).
 *     TLS cache stores (solver_ptr, cached_generation) pairs.
 *     Clear function only increments target solver's generation.
 *     Pros: Minimal cross-thread impact
 *     Cons: Requires per-solver field addition, more complex validation
 *
 *   Alternative B - Per-Thread Solver Map:
 *     Each thread maintains map<solver_ptr, cached_data>.
 *     Clear function marks entry as stale (no global atomic).
 *     Pros: Zero cross-thread impact, no global atomic
 *     Cons: Higher memory footprint, more complex data structure
 *
 *   Alternative C - Epoch-Based Invalidation:
 *     Use global epoch + per-solver last-valid-epoch.
 *     TLS caches epoch; check against solver's last-valid on access.
 *     Pros: O(1) invalidation, amortized O(1) check
 *     Cons: Requires additional per-solver storage
 *
 * Current design is optimal for WRF's typical usage (stable solvers, rare clears).
 * Implement alternatives only after profiling confirms this is a bottleneck.
 * ═══════════════════════════════════════════════════════════════════════════════
 */
void sdirk3_clear_tls_solver_cache(void)
{
    // FIX Round127/Round129: Increment global generation to force revalidation.
    // This is simpler than exposing static thread_local variables directly.
    //
    // ═══════════════════════════════════════════════════════════════════════════
    // FIX Round129: DELAYED INVALIDATION BEHAVIOR
    // ═══════════════════════════════════════════════════════════════════════════
    // This function uses GLOBAL generation increment for DELAYED invalidation:
    //   1. Global g_solver_generation is atomically incremented
    //   2. Each thread's TLS cache stores its last-seen generation
    //   3. On NEXT ACCESS, thread detects generation mismatch → re-lookup
    //
    // IMPORTANT: Invalidation is NOT immediate! Other threads continue using
    // their cached data until their next solver access triggers revalidation.
    //
    // For IMMEDIATE invalidation in all threads, use:
    //   sdirk3_tile_solver_reset_tls_parallel(solver_ptr)
    // which spawns an OpenMP parallel region to reset each thread's TLS.
    //
    // Use case guidance:
    //   - Delayed OK (most cases): Test setup, solver recycling between runs
    //   - Need immediate: Mid-simulation solver swap, debugging race conditions
    //
    // FIX Round130: PROFILING CHECKLIST FOR OPTIMIZATION DECISIONS
    // If sdirk3_clear_tls_solver_cache() is called frequently (>100/sec), consider
    // per-solver epoch design. Key metrics to profile:
    //
    // FIX Round131: MEASUREMENT METHOD AVAILABILITY
    //   [PROVIDED] = Available via debug_level≥2 logging in this codebase
    //   [EXTERNAL] = Requires external tools (perf, tracepoints, custom instrumentation)
    //
    //   1. Cache miss rate: TLS lookups that require re-lookup due to generation mismatch
    //      - Metric: (generation_mismatches / total_lookups) × 100
    //      - Target: <5% for healthy caching, >20% = consider per-solver epoch
    //      - Method: [EXTERNAL] Add custom counters or use tracepoints
    //   2. Mutex contention: If g_solver_generation is heavily contended
    //      - Metric: perf stat -e cache-misses, lock:lock_acquired events
    //      - Target: <1ms per million accesses, >10ms = bottleneck
    //      - Method: [EXTERNAL] Linux perf, Intel VTune, or similar
    //   3. Revalidation overhead: Time spent in map lookup after invalidation
    //      - Metric: tls_revalidation_time / total_solve_time
    //      - Target: <1% overhead, >5% = consider caching strategy change
    //      - Method: [EXTERNAL] Add timing instrumentation around lookups
    //   4. Call frequency: How often sdirk3_clear_tls_solver_cache() is invoked
    //      - Metric: calls/second via debug_level≥2 logging
    //      - Target: <10/sec is fine, >100/sec = redesign caching strategy
    //      - Method: [PROVIDED] Set debug_level≥2, count log messages
    //
    // FIX Round132: PER-SOLVER EPOCH DESIGN (MULTI-SOLVER OPTIMIZATION)
    // Current design: Global g_solver_generation invalidates ALL solvers on any call.
    // Problem: In multi-solver scenarios (e.g., nested domains), clearing one solver's
    // cache unnecessarily invalidates all others, causing extra re-lookups.
    //
    // Alternative design (NOT YET IMPLEMENTED):
    //   - Store per-solver epoch in solver struct (uint64_t solver_epoch)
    //   - TLS stores map<solver_ptr, last_seen_epoch>
    //   - sdirk3_clear_tls_solver_cache_for(solver_ptr) increments only that solver's epoch
    //   - Benefits: O(1) invalidation for single solver, no cross-solver interference
    //   - Trade-off: Slightly more complex TLS management, per-thread map overhead
    //
    // When to implement: If profiling shows >20% cache miss rate in multi-solver runs
    // AND sdirk3_clear_tls_solver_cache() is called >100/sec per solver.
    //
    // FIX Round138: DECISION CRITERIA (one-liner)
    //   Implement per-solver epoch when: multi-solver + miss_rate>20% + clear_freq>100Hz
    //
    // FIX Round134: PROPOSED PER-SOLVER API (FOR FUTURE IMPLEMENTATION)
    // ⚠️ NOT YET IMPLEMENTED - This is a DESIGN PROPOSAL for future optimization.
    // void sdirk3_clear_tls_solver_cache_for(void* solver_ptr);
    //   - Invalidates only the specified solver's TLS cache entries
    //   - Implementation: solver->epoch++; (no global generation change)
    //   - Thread safety: Atomic increment on per-solver epoch field
    //   - Benefit: Zero impact on other solvers in multi-solver runs
    //   - Usage: CALL sdirk3_clear_tls_solver_cache_for(solver_ptr)
    // ⚠️ CURRENT WORKAROUND: Use sdirk3_tile_solver_reset_tls_parallel(solver_ptr)
    // for targeted per-solver TLS reset (though with higher overhead).
    //
    // FIX Round133: MULTI-SOLVER ENVIRONMENT RECOMMENDATIONS (UNTIL PER-SOLVER EPOCH IS IMPLEMENTED)
    // Since global generation affects ALL solvers, in multi-solver environments:
    //   1. REDUCE CALL FREQUENCY: Batch cache clears instead of per-operation calls
    //      - Before: clear after each solver operation → N solvers × M ops = N×M clears
    //      - After: clear once per time step → 1 clear per step
    //   2. USE reset_tls_parallel FOR TARGETED RESET: If you need to reset specific solver's
    //      TLS state without affecting others, use sdirk3_tile_solver_reset_tls_parallel(solver_ptr)
    //      which resets only that solver's TLS entries across threads (immediate, not delayed).
    //   3. AVOID IN HOT LOOPS: If called >10Hz, consider whether cache clear is truly needed.
    //      Normal WRF runs rarely need explicit cache clears during simulation.
    //
    // FIX Round136: ORDER-INDEPENDENT CALLS
    // The TLS APIs (clear_tls_solver_cache, reset_tls_debug_tracking, reset_tls_parallel)
    // are ORDER-INDEPENDENT. You can call them in any sequence without affecting correctness.
    // Exceptions:
    //   1. sdirk3_tile_solver_destroy_zerocopy() MUST be called AFTER all TLS-using
    //      operations on that solver are complete (to avoid use-after-free).
    //   2. FIX Round137: When REUSING same solver_ptr after destroy+create cycle,
    //      call reset_full() or reset_full_parallel() to clear stale TLS mappings.
    //
    // FIX Round133: CALL FREQUENCY PERFORMANCE IMPACT
    // FIX Round136: ★ RECOMMENDED: <1 Hz (per second) ★ - 아래 표에서 성능 영향 확인
    // ┌────────────────────┬───────────────────────────────────────────────────┐
    // │ Call Frequency     │ Expected Impact                                   │
    // ├────────────────────┼───────────────────────────────────────────────────┤
    // │ ★ <1 Hz (권장)     │ Negligible - normal test/debug usage              │
    // │ 1-10 Hz            │ Low - acceptable for per-timestep clears          │
    // │ 10-100 Hz          │ Moderate - may increase cache miss rate by 5-10%  │
    // │ >100 Hz            │ High - consider per-solver epoch or batching      │
    // └────────────────────┴───────────────────────────────────────────────────┘
    // FIX Round137: MEASUREMENT PREMISE - Impact estimates based on TLS cache hit rate
    // degradation in typical WRF workloads. Actual impact varies by solver count, thread
    // count, and access patterns. Profile your specific workload for accurate assessment.
    // Monitor with debug_level≥2: count "TLS solver cache invalidated" messages.
    //
    // FIX Round139: REVALIDATION BEHAVIOR
    // Generation mismatch triggers IMMEDIATE revalidation on next TLS access.
    // This is independent of any sampling period settings - sampling only affects
    // debug tracking, not the generation-based cache invalidation mechanism.
    // FIX Round140: FAST PATH vs MUTEX PATH
    //   - Fast path (TLS cache hit): sampling period applies to debug tracking only
    //   - Mutex path (generation mismatch): always revalidates, no sampling involved
    // FIX Round141: DEBUG SAMPLING ACTIVATION
    //   - Debug sampling enabled by: WRF_SDIRK3_RELEASE_TLS_CHECK macro (release builds)
    //   - Always enabled in debug builds (NDEBUG not defined)
    //   - Interval controlled by: WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL (default 1000)
    // ═══════════════════════════════════════════════════════════════════════════

    uint64_t old_gen = g_solver_generation.load(std::memory_order_relaxed);
    g_solver_generation.fetch_add(1, std::memory_order_release);

    // FIX Round142/Round143/Round144: PARALLEL REGION WARNING (with _OPENMP guard)
    // This function affects CALLING THREAD ONLY (delayed invalidation via generation).
    // If called from within an OpenMP parallel region, only that thread's TLS will
    // be invalidated on its next access. Other threads in the same parallel region
    // continue using their cached data until their next access.
    // For all-thread invalidation, call from OUTSIDE parallel region, or use
    // sdirk3_tile_solver_reset_tls_parallel() instead.
    // FIX Round143: Uses WARN_ONCE per thread (thread_local bool) to prevent spam.
    // FIX Round144: NOTE - This warning is OpenMP-specific (omp_in_parallel()).
    //   For non-OpenMP parallelism (std::thread, TBB, etc.), no warning is issued.
    //   Same per-thread limitation applies - caller is responsible for coordination.
#ifdef _OPENMP
    if (omp_in_parallel()) {
        static thread_local bool warned_parallel = false;
        if (!warned_parallel && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3 WARNING: sdirk3_clear_tls_solver_cache() called from within "
                      << "parallel region (thread " << omp_get_thread_num() << "). "
                      << "Only this thread is affected. For all-thread invalidation, "
                      << "call from outside parallel region." << std::endl;
            warned_parallel = true;
        }
    }
#endif

    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "SDIRK3: TLS solver cache invalidated via generation bump ("
                  << old_gen << " -> " << g_solver_generation.load()
                  << ") - delayed until next access per thread" << std::endl;
    }

    // FIX Round129: Also reset debug tracking state (included in "complete" reset)
    // This ensures sdirk3_clear_tls_solver_cache() resets BOTH lookup cache and debug tracking.
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
    wrf::sdirk3::tls_debug::resetTLSTracking();
#endif
}

/**
 * Set base state for a tile solver
 * Called once during initialization to set reference state variables
 * Extended version with advanced dynamics parameters
 */
void sdirk3_tile_set_base_state(void* solver_ptr,
                                float* pb, float* alb, float* phb, float* mub,
                                int nx, int ny, int nz,
                                // Extended dynamics parameters
                                float* sin_alpha_x, float* sin_alpha_y,
                                float* cos_alpha_x, float* cos_alpha_y,
                                int div_damp_opt, float div_damp_coef,
                                int diff_6th_opt, float diff_6th_factor,
                                int diff_6th_slopeopt, float diff_6th_thresh,
                                int smagorinsky_opt, float c_s, float c_k)
{
    // Use stderr and flush immediately
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "\n=== C++ sdirk3_tile_set_base_state ENTRY ===" << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  solver_ptr = " << solver_ptr << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  pb = " << (void*)pb << ", alb = " << (void*)alb << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  phb = " << (void*)phb << ", mub = " << (void*)mub << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  dimensions: nx=" << nx << ", ny=" << ny << ", nz=" << nz << std::endl;

    }
    {
        std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "  Number of solvers in map: " << g_tile_solvers.size() << std::endl;
            // FIX Round156: Removed flush() - std::endl already flushes
        }
    }
    
    // Check if pointers are valid
    if (!solver_ptr) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: NULL solver_ptr passed to sdirk3_tile_set_base_state" << std::endl;

        }
        return;
    }
    if (!pb || !alb || !phb || !mub) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: NULL data pointers passed to sdirk3_tile_set_base_state" << std::endl;

        }
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "  pb=" << (void*)pb << " alb=" << (void*)alb 
                  << " phb=" << (void*)phb << " mub=" << (void*)mub << std::endl;

        }
        return;
    }
    
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {


        std::cerr << "  About to search for solver in map..." << std::endl;
        // FIX Round157: Removed flush() - std::endl already flushes

    }
    
    // Print all solvers in map for debugging
    {
        std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "  Solvers in map before search:" << std::endl;

            for (const auto& pair : g_tile_solvers) {

                std::cerr << "    Key: " << pair.first << std::endl;

            }
            // FIX Round156: Removed flush() - std::endl already flushes
        }
    }
    
    // Try to find the solver
    TileSDIRK3UnifiedSolver* solver = nullptr;
    try {
        std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
        auto it = g_tile_solvers.find(solver_ptr);
        if (it != g_tile_solvers.end()) {
            solver = it->second.get();
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "  Found solver in map!" << std::endl;

            }
        } else {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "ERROR: Solver not found in map" << std::endl;

            }
            return;
        }
    } catch (const std::exception& e) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: Exception during map search: " << e.what() << std::endl;

        }
        return;
    } catch (...) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: Unknown exception during map search" << std::endl;

        }
        return;
    }
    
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {


        std::cerr << "  Found solver in map, calling setBaseState..." << std::endl;
        // FIX Round157: Removed flush() - std::endl already flushes

    }
    
    // The solver's setBaseState expects pointers to the tile data
    // WRF arrays are in Fortran order: pb(ims:ime,kms:kme,jms:jme)
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  About to call solver->setBaseState..." << std::endl;
        // FIX Round157: Removed flush() - std::endl already flushes
    }
    
    try {
        solver->setBaseState(pb, alb, phb, mub);
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "  setBaseState completed successfully" << std::endl;

        }
        
        // Set extended dynamics parameters if available
        // Get the solver's grid info and try to cast to extended version
        auto grid_info = solver->getGridInfo();
        auto grid_ext = std::static_pointer_cast<wrf::sdirk3::WRFGridInfoExtended>(grid_info);
        
        if (grid_ext) {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "  Setting extended dynamics parameters..." << std::endl;

            }
            
            // Set terrain slope arrays if provided
            if (sin_alpha_x && sin_alpha_y && cos_alpha_x && cos_alpha_y) {
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                    std::cerr << "  Initializing terrain slope arrays..." << std::endl;

                }
                grid_ext->initializeTerrainSlope(sin_alpha_x, sin_alpha_y, 
                                                cos_alpha_x, cos_alpha_y, nx, ny);
            }
            
            // Set divergence damping parameters
            grid_ext->div_damp_opt = div_damp_opt;
            grid_ext->div_damp_coef = div_damp_coef;
            
            // Set 6th order diffusion parameters
            grid_ext->diff_6th_opt = diff_6th_opt;
            grid_ext->diff_6th_factor = diff_6th_factor;
            grid_ext->diff_6th_slopeopt = diff_6th_slopeopt;
            grid_ext->diff_6th_thresh = diff_6th_thresh;
            
            // Set Smagorinsky turbulence parameters
            grid_ext->smagorinsky_opt = smagorinsky_opt;
            grid_ext->c_s = c_s;
            // c_k parameter not used in current implementation
            
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            
                std::cerr << "  Extended dynamics configured:" << std::endl;

            
            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Divergence damping: opt=" << div_damp_opt 
                      << ", coef=" << div_damp_coef << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    6th order diffusion: opt=" << diff_6th_opt 
                      << ", factor=" << diff_6th_factor << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Slope damping: opt=" << diff_6th_slopeopt
                      << ", thresh=" << diff_6th_thresh << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Smagorinsky: opt=" << smagorinsky_opt
                      << ", c_s=" << c_s << ", c_k=" << c_k << std::endl;

            }
            
            if (grid_ext->hasTerrainSlope()) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>()
                // FIX 2025-12-28: Changed from pointer dereference to value member access
                // FIX Round192: NoGradGuard for consistency
                torch::NoGradGuard no_grad;
                auto sin_x_max = grid_ext->sin_alpha_x.abs().max().to(torch::kCPU).item<float>();
                auto sin_y_max = grid_ext->sin_alpha_y.abs().max().to(torch::kCPU).item<float>();
                float max_slope_x = std::asin(sin_x_max) * 180.0f / M_PI;
                float max_slope_y = std::asin(sin_y_max) * 180.0f / M_PI;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                    std::cerr << "    Terrain slope: max_x=" << max_slope_x 
                          << " deg, max_y=" << max_slope_y << " deg" << std::endl;

                }
            }
        } else {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "  WARNING: Extended grid info not available, advanced features disabled" << std::endl;

            }
        }
        
    } catch (const std::exception& e) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: Exception in setBaseState: " << e.what() << std::endl;

        }
    } catch (...) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: Unknown exception in setBaseState" << std::endl;

        }
    }
}

} // extern "C"

// Additional extern "C" block for debug function
extern "C" {

/**
 * Set boundary condition flags for a tile solver (v2 — with specified/nested).
 * Full 12-flag version; v1 delegates here with specified=0, nested=0.
 */
void sdirk3_tile_set_boundary_conditions_v2(void* solver_ptr,
                                            int periodic_x, int periodic_y,
                                            int symmetric_xs, int symmetric_xe,
                                            int symmetric_ys, int symmetric_ye,
                                            int open_xs, int open_xe,
                                            int open_ys, int open_ye,
                                            int specified, int nested)
{
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Invalid solver pointer in sdirk3_tile_set_boundary_conditions_v2" << std::endl;
        }
        return;
    }

    auto* solver = it->second.get();
    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(solver);
    if (!unified_solver) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Failed to cast to TileSDIRK3UnifiedSolver in set_boundary_conditions_v2" << std::endl;
        }
        return;
    }

    // Call the solver's setBoundaryConditions method (full 12-param version)
    unified_solver->setBoundaryConditions(
        periodic_x != 0, periodic_y != 0,
        symmetric_xs != 0, symmetric_xe != 0,
        symmetric_ys != 0, symmetric_ye != 0,
        open_xs != 0, open_xe != 0,
        open_ys != 0, open_ye != 0,
        specified != 0, nested != 0
    );

    if (wrf::sdirk3::g_sdirk3_config.debug_level > 0) {
        std::cerr << "SDIRK3: Boundary conditions set for solver " << solver_ptr << std::endl;
        std::cerr << "  periodic_x=" << periodic_x << ", periodic_y=" << periodic_y << std::endl;
        std::cerr << "  symmetric_xs=" << symmetric_xs << ", symmetric_xe=" << symmetric_xe << std::endl;
        std::cerr << "  symmetric_ys=" << symmetric_ys << ", symmetric_ye=" << symmetric_ye << std::endl;
        std::cerr << "  open_xs=" << open_xs << ", open_xe=" << open_xe
                  << ", open_ys=" << open_ys << ", open_ye=" << open_ye << std::endl;
        std::cerr << "  specified=" << specified << ", nested=" << nested << std::endl;
    }
}

/**
 * Set boundary condition flags for a tile solver (v1 — backward compatible).
 * Delegates to v2 with specified=0, nested=0.
 */
void sdirk3_tile_set_boundary_conditions(void* solver_ptr,
                                         int periodic_x, int periodic_y,
                                         int symmetric_xs, int symmetric_xe,
                                         int symmetric_ys, int symmetric_ye,
                                         int open_xs, int open_xe,
                                         int open_ys, int open_ye)
{
    sdirk3_tile_set_boundary_conditions_v2(
        solver_ptr,
        periodic_x, periodic_y,
        symmetric_xs, symmetric_xe,
        symmetric_ys, symmetric_ye,
        open_xs, open_xe,
        open_ys, open_ye,
        0, 0  // specified=false, nested=false (backward compat)
    );
}

/**
 * Set solver-local MPI process grid information.
 * Fortran should call this before per-step entry so setWRFIndices()
 * initializes halo exchange with correct nprocx/nprocy/mypx/mypy.
 */
void sdirk3_tile_set_mpi_process_info(void* solver_ptr,
                                      int nprocx, int nprocy,
                                      int mypx, int mypy)
{
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Invalid solver pointer in sdirk3_tile_set_mpi_process_info" << std::endl;
        }
        return;
    }

    auto* solver = it->second.get();
    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(solver);
    if (!unified_solver) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Failed to cast solver in sdirk3_tile_set_mpi_process_info" << std::endl;
        }
        return;
    }

    unified_solver->setMPIProcessInfo(nprocx, nprocy, mypx, mypy);
}

/**
 * Debug function to test basic Fortran-C++ interface connectivity
 */
void sdirk3_debug_step(void* solver_ptr, int tile_id, int itimestep) {
    // First goal: verify this message appears
    // FIX Round151: Converted std::cout → std::cerr for stream unification
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "C++ sdirk3_debug_step successfully called for tile "
              << tile_id << " at timestep " << itimestep << std::endl;
    }

    // Verify solver pointer validity
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "DEBUG_STEP: Invalid solver pointer!" << std::endl;
        }
        return;
    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "DEBUG_STEP: Solver pointer is valid." << std::endl;
    }
}

/**
 * Set map projection for idealized case detection
 * Called during initialization to identify idealized cases (map_proj = 0)
 */
void sdirk3_tile_set_map_projection(void* solver_ptr, int map_proj)
{
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Invalid solver pointer in sdirk3_tile_set_map_projection" << std::endl;
        }
        return;
    }
    
    auto* solver = it->second.get();
    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(solver);
    if (!unified_solver) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "ERROR: Solver is not a TileSDIRK3UnifiedSolver in sdirk3_tile_set_map_projection" << std::endl;
        }
        return;
    }
    
    // Store the map projection value for use in advanceZeroCopy
    // We'll use the grid_info_ to store this value
    if (unified_solver->getGridInfo()) {
        unified_solver->getGridInfo()->map_proj = map_proj;
    }
    
    // FIX Round151: Converted std::cout → std::cerr for stream unification
    if (wrf::sdirk3::g_sdirk3_config.debug_level > 0) {
        std::cerr << "SDIRK3: Map projection set for solver " << solver_ptr << std::endl;
        std::cerr << "  map_proj=" << map_proj;
        if (map_proj == 0) {
            std::cerr << " (idealized case)";
        }
        std::cerr << std::endl;
    }
}

/**
 * FIX 2025-01-11 Round60/Round63/Round64: Set FP64 scalar parameters for precision-critical simulations
 *
 * This function allows Fortran callers to set double-precision grid spacing values
 * which are used when compute paths require FP64 precision. Call this BEFORE
 * sdirk3_tile_unified_step_zerocopy_v2() if FP64 precision is needed.
 *
 * @param solver_ptr   Solver handle returned by sdirk3_tile_solver_create_zerocopy()
 * @param fp64_ptr     Pointer to SDIRK3_ScalarParams_FP64 struct, or NULL to clear
 *
 * INVALID INPUT HANDLING (FIX Round64):
 *   If fp64_ptr is non-NULL but contains invalid values (zero, negative, NaN, Inf):
 *   - Invalid values are IGNORED - the PREVIOUS FP64 state is RETAINED
 *   - A warning is logged (at debug_level >= 1) explaining the retention
 *   - To CLEAR FP64 state and revert to FP32 mode, pass fp64_ptr = NULL
 *   This design avoids accidentally disabling FP64 mode due to transient bad values.
 *
 * THREAD SAFETY (FIX Round63):
 *   This function modifies solver-internal grid_info state. It is protected by
 *   g_tile_solvers_mutex for solver lookup, but the grid_info modification is NOT
 *   synchronized with advanceZeroCopy(). Therefore:
 *   - MUST be called BEFORE any call to sdirk3_tile_unified_step_zerocopy_v2()
 *   - MUST NOT be called concurrently with solver stepping on the same solver
 *   - Safe to call between time steps when solver is idle
 *   - Typical pattern: call once during initialization or when grid changes
 *
 * Fortran usage:
 *   TYPE(SDIRK3_ScalarParams_FP64) :: fp64_scalars
 *   fp64_scalars%rdx_fp64 = 1.0D0 / DBLE(dx)
 *   fp64_scalars%rdy_fp64 = 1.0D0 / DBLE(dy)
 *   fp64_scalars%dx_fp64 = DBLE(dx)
 *   fp64_scalars%dy_fp64 = DBLE(dy)
 *   CALL sdirk3_set_fp64_scalars(solver_ptr, C_LOC(fp64_scalars))
 */
// FIX 2025-01-11 Round70: Use SDIRK3_ScalarParams_FP64_C* to match header declaration
// The _C suffix indicates the C API type (from wrf_sdirk3_interface.h).
// SDIRK3_ScalarParams_FP64 (from params.h) and SDIRK3_ScalarParams_FP64_C are ABI-equivalent
// (identical layout: 4 × double, 32 bytes, 8-byte aligned) but using different types
// in declaration vs implementation violates ODR and can cause linker issues.
void sdirk3_set_fp64_scalars(void* solver_ptr, const SDIRK3_ScalarParams_FP64_C* fp64_ptr)
{
    std::lock_guard<std::mutex> lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        std::cerr << "SDIRK3 sdirk3_set_fp64_scalars ERROR: Invalid solver pointer" << std::endl;
        return;
    }

    auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(it->second.get());
    if (!unified_solver) {
        std::cerr << "SDIRK3 sdirk3_set_fp64_scalars ERROR: Solver is not TileSDIRK3UnifiedSolver" << std::endl;
        return;
    }

    auto grid_info = unified_solver->getGridInfo();
    if (!grid_info) {
        std::cerr << "SDIRK3 sdirk3_set_fp64_scalars ERROR: No grid_info in solver" << std::endl;
        return;
    }

    // FIX 2025-01-11 Round61: Track previous values to avoid unnecessary invalidation
    double prev_dx_fp64 = grid_info->dx_fp64;
    double prev_dy_fp64 = grid_info->dy_fp64;
    // FIX 2025-01-11 Round66: Track FP64 mode to warn on transitions
    const bool prev_fp64_explicit = grid_info->fp64_explicit;
    bool values_changed = false;

    if (fp64_ptr) {
        // FIX 2025-01-11 Round60: Set FP64 values from caller with validation
        // Prefer direct values if provided, compute if zero
        // Guard against NaN/Inf values which would corrupt grid_info
        bool has_valid_dx_fp64 = false;
        bool has_valid_dy_fp64 = false;

        if (fp64_ptr->dx_fp64 > 0.0 && std::isfinite(fp64_ptr->dx_fp64)) {
            grid_info->dx_fp64 = fp64_ptr->dx_fp64;
            has_valid_dx_fp64 = true;
        } else if (fp64_ptr->rdx_fp64 > 0.0 && std::isfinite(fp64_ptr->rdx_fp64)) {
            grid_info->dx_fp64 = 1.0 / fp64_ptr->rdx_fp64;
            has_valid_dx_fp64 = true;
        }
        // FIX 2025-01-11 Round63/Round64: Warn if caller passed invalid values (both dx and rdx are bad)
        // Round64: Clarify that old value is RETAINED, not cleared. Pass NULL to clear.
        else if ((fp64_ptr->dx_fp64 != 0.0 || fp64_ptr->rdx_fp64 != 0.0) &&
                 wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3 WARNING: sdirk3_set_fp64_scalars received invalid dx values"
                      << " (dx_fp64=" << fp64_ptr->dx_fp64
                      << ", rdx_fp64=" << fp64_ptr->rdx_fp64 << ")"
                      << " - RETAINING previous dx_fp64=" << grid_info->dx_fp64
                      << ". Pass NULL to clear FP64 state." << std::endl;
        }

        if (fp64_ptr->dy_fp64 > 0.0 && std::isfinite(fp64_ptr->dy_fp64)) {
            grid_info->dy_fp64 = fp64_ptr->dy_fp64;
            has_valid_dy_fp64 = true;
        } else if (fp64_ptr->rdy_fp64 > 0.0 && std::isfinite(fp64_ptr->rdy_fp64)) {
            grid_info->dy_fp64 = 1.0 / fp64_ptr->rdy_fp64;
            has_valid_dy_fp64 = true;
        }
        // FIX 2025-01-11 Round63/Round64: Warn if caller passed invalid values
        // Round64: Clarify that old value is RETAINED, not cleared. Pass NULL to clear.
        else if ((fp64_ptr->dy_fp64 != 0.0 || fp64_ptr->rdy_fp64 != 0.0) &&
                 wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "SDIRK3 WARNING: sdirk3_set_fp64_scalars received invalid dy values"
                      << " (dy_fp64=" << fp64_ptr->dy_fp64
                      << ", rdy_fp64=" << fp64_ptr->rdy_fp64 << ")"
                      << " - RETAINING previous dy_fp64=" << grid_info->dy_fp64
                      << ". Pass NULL to clear FP64 state." << std::endl;
        }

        // FIX 2025-01-11 Round63: Set fp64_explicit flag if ANY valid FP64 value was provided
        // This flag gates FP64 tensor creation in advanceZeroCopy to prevent FP32 runs from
        // inadvertently using float-derived FP64 values which can promote downstream ops
        if (has_valid_dx_fp64 || has_valid_dy_fp64) {
            grid_info->fp64_explicit = true;
        }

        // FIX 2025-01-11 Round66-72: Warn on FP64 mode transitions (per-solver WARN_ONCE)
        // Helps diagnose issues where fp64_explicit may be incorrectly set/unset.
        // Uses per-solver state (grid_info->last_fp64_transition_logged) for multi-solver support.
        // Guarded by debug_level >= 1 to avoid log spam.
        const int8_t current_transition = grid_info->fp64_explicit ? 1 : 0;
        const bool should_log = (grid_info->fp64_explicit != prev_fp64_explicit) &&
                                (current_transition != grid_info->last_fp64_transition_logged) &&
                                (wrf::sdirk3::g_sdirk3_config.debug_level >= 1);
        if (should_log) {
            grid_info->last_fp64_transition_logged = current_transition;
            // FIX 2025-01-11 Round71: Optionally hide solver pointer for security-sensitive environments
            std::cerr << "SDIRK3 INFO: FP64 mode transition in sdirk3_set_fp64_scalars (solver=";
            if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                std::cerr << solver_ptr;
            } else {
                std::cerr << "<hidden>";
            }
            std::cerr << "): "
                      << (prev_fp64_explicit ? "FP64" : "FP32") << " -> "
                      << (grid_info->fp64_explicit ? "FP64" : "FP32") << std::endl;
            if (grid_info->fp64_explicit) {
                std::cerr << "  FP64 tensors will be used for rdx/rdy in subsequent advanceZeroCopy calls" << std::endl;
            } else {
                std::cerr << "  Falling back to FP32 tensors (no valid FP64 values provided)" << std::endl;
            }
        }

        // FIX 2025-01-11 Round61/Round67: Only invalidate if values actually changed
        // Round67: This also handles rdx_fp64/rdy_fp64-only input patterns because
        // when rdx_fp64 is provided, we compute dx_fp64 = 1.0/rdx_fp64, so any change
        // in rdx_fp64 will be reflected in dx_fp64 and trigger invalidation
        values_changed = (grid_info->dx_fp64 != prev_dx_fp64) || (grid_info->dy_fp64 != prev_dy_fp64);
        if (values_changed) {
            wrf::sdirk3::pg_detail::invalidateScalarCache();
        }

        // FIX 2025-01-11 Round62: Check consistency between dx_fp64 and rdx tensor
        // Warn if they differ significantly (>1% relative error)
        constexpr double consistency_threshold = 0.01;  // 1% relative error
        if (grid_info->dx_fp64 > 0.0 && grid_info->rdx.defined() && grid_info->rdx.numel() == 1) {
            torch::NoGradGuard no_grad;
            double tensor_rdx = grid_info->rdx.to(torch::kCPU).item<double>();
            if (tensor_rdx > 0.0 && std::isfinite(tensor_rdx)) {
                double computed_rdx = 1.0 / grid_info->dx_fp64;
                double rel_err = std::abs(tensor_rdx - computed_rdx) / std::max(tensor_rdx, computed_rdx);
                if (rel_err > consistency_threshold) {
                    std::cerr << "SDIRK3 WARNING: dx_fp64/rdx tensor inconsistency detected!" << std::endl;
                    std::cerr << "  dx_fp64=" << grid_info->dx_fp64 << " -> rdx=" << computed_rdx << std::endl;
                    std::cerr << "  rdx tensor=" << tensor_rdx << " (rel_err=" << (rel_err * 100.0) << "%)" << std::endl;
                    std::cerr << "  Using dx_fp64 value (authoritative from sdirk3_set_fp64_scalars)" << std::endl;
                }
            }
        }
        if (grid_info->dy_fp64 > 0.0 && grid_info->rdy.defined() && grid_info->rdy.numel() == 1) {
            torch::NoGradGuard no_grad;
            double tensor_rdy = grid_info->rdy.to(torch::kCPU).item<double>();
            if (tensor_rdy > 0.0 && std::isfinite(tensor_rdy)) {
                double computed_rdy = 1.0 / grid_info->dy_fp64;
                double rel_err = std::abs(tensor_rdy - computed_rdy) / std::max(tensor_rdy, computed_rdy);
                if (rel_err > consistency_threshold) {
                    std::cerr << "SDIRK3 WARNING: dy_fp64/rdy tensor inconsistency detected!" << std::endl;
                    std::cerr << "  dy_fp64=" << grid_info->dy_fp64 << " -> rdy=" << computed_rdy << std::endl;
                    std::cerr << "  rdy tensor=" << tensor_rdy << " (rel_err=" << (rel_err * 100.0) << "%)" << std::endl;
                    std::cerr << "  Using dy_fp64 value (authoritative from sdirk3_set_fp64_scalars)" << std::endl;
                }
            }
        }

        // FIX Round151: Converted std::cout → std::cerr for stream unification
        if (wrf::sdirk3::g_sdirk3_config.debug_level > 0) {
            std::cerr << "SDIRK3: FP64 scalars set for solver " << solver_ptr << std::endl;
            std::cerr << "  dx_fp64=" << grid_info->dx_fp64
                      << " dy_fp64=" << grid_info->dy_fp64
                      << " (changed=" << values_changed << ")" << std::endl;
        }
    } else {
        // NULL pointer - clear FP64 values (will fall back to float precision)
        values_changed = (grid_info->dx_fp64 != 0.0) || (grid_info->dy_fp64 != 0.0) ||
                         grid_info->fp64_explicit;
        grid_info->dx_fp64 = 0.0;
        grid_info->dy_fp64 = 0.0;
        // FIX 2025-01-11 Round63: Clear explicit FP64 flag on NULL pointer call
        grid_info->fp64_explicit = false;

        // FIX 2025-01-11 Round66-72: Warn on FP64->FP32 mode transition (NULL path, per-solver WARN_ONCE)
        // Uses per-solver state (grid_info->null_path_fp64_logged) for multi-solver support.
        // Guarded by debug_level >= 1 to avoid log spam.
        const bool should_log_null = prev_fp64_explicit &&
                                     !grid_info->null_path_fp64_logged &&
                                     (wrf::sdirk3::g_sdirk3_config.debug_level >= 1);
        if (should_log_null) {
            grid_info->null_path_fp64_logged = true;
            // FIX 2025-01-11 Round71: Optionally hide solver pointer for security-sensitive environments
            std::cerr << "SDIRK3 INFO: FP64 mode cleared via NULL pointer call (solver=";
            if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                std::cerr << solver_ptr;
            } else {
                std::cerr << "<hidden>";
            }
            std::cerr << "): FP64 -> FP32" << std::endl;
            std::cerr << "  Subsequent advanceZeroCopy calls will use FP32 rdx/rdy tensors" << std::endl;
            std::cerr << "  (stale FP64 tensors will be recreated as FP32 on next step)" << std::endl;
        }
        // Reset flag when transitioning back to FP64 (so next NULL call can log again)
        if (!prev_fp64_explicit) {
            grid_info->null_path_fp64_logged = false;
        }

        // FIX 2025-01-11 Round61: Only invalidate if values actually changed
        if (values_changed) {
            wrf::sdirk3::pg_detail::invalidateScalarCache();
        }

        // FIX Round151: Converted std::cout → std::cerr for stream unification
        if (wrf::sdirk3::g_sdirk3_config.debug_level > 0) {
            // FIX 2025-01-11 Round71: Optionally hide solver pointer
            std::cerr << "SDIRK3: FP64 scalars cleared for solver ";
            if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                std::cerr << solver_ptr;
            } else {
                std::cerr << "<hidden>";
            }
            std::cerr << " (changed=" << values_changed << ")" << std::endl;
        }
    }
}

} // extern "C"
