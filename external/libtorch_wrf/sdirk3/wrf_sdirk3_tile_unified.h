// wrf_sdirk3_tile_unified.h
// Unified SDIRK3 solver for WRF tiles - NO MODE SEPARATION
//
// ABI VERSION: 94 (WRF_SDIRK3_GRIDINFO_ABI_VERSION in wrf_sdirk3_types.h)
// FIX Round113: Updated from 93 to 94 for solver_unique_id addition.
// This file uses WRFGridInfo which includes cached_reference_device/cached_device_index/
// warned_dz_rdz_device_mismatch/dz_rdz_warn_throttle_counter/solver_unique_id.
// If ABI mismatch occurs, rebuild all dependent modules.

#ifndef WRF_SDIRK3_TILE_UNIFIED_H
#define WRF_SDIRK3_TILE_UNIFIED_H

#include "wrf_sdirk3_torch_wrapper.h"
#include "wrf_sdirk3_tile_base.h"
#include "wrf_sdirk3_unified_rhs.h"
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_tile_boundary_optimizer.h"
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-12-29: For invalidateStaticMetricCaches()
#include "wrf_sdirk3_pressure_gradient_vectorized.h"  // FIX 2025-01-11 Round58: For pg_detail::invalidateAllCaches()
#include "wrf_sdirk3_tensor_cache.h"  // OPT Pass33+: For getThreadLocalTensorViewCache() in invalidateThreadLocalCachesOnly()
#include "wrf_sdirk3_config.h"  // FIX Round122: For g_sdirk3_config.log_solver_pointer in tls_debug
#include "wrf_sdirk3_ww_cp.h"   // PR 9C.2: WdampRuntimeContract + boundary policies
#include "wrf_sdirk3_stage_history_diag.h"  // PR 9F: StageDefectTensorSnapshot
#include "wrf_sdirk3_ad_halo_exchange.h"  // AD halo exchange for DA mode
#include "wrf_sdirk3_ad_halo_utils.h"     // Full-halo utilities
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <iostream>  // FIX Round93: For TLS debug tracking
#include <cstdint>   // FIX Round94: For uint64_t in TLS throttling

namespace wrf {
namespace sdirk3 {

// IMEX split mode for computeUnifiedRHS
// Full: all terms (backward compatible, existing behavior)
// ImplicitOnly: acoustic/gravity terms only (PGF, geopotential, mass continuity, buoyancy, W-damping)
// ExplicitOnly: nonlinear/advective terms only (advection, Coriolis, curvature, diffusion)
enum class RhsMode {
    Full,           // All terms — backward compatible
    ImplicitOnly,   // Fast-mode: PGF + geopotential + mass continuity + buoyancy + W-damping
    ExplicitOnly    // Slow-mode: advection + Coriolis + curvature + H-diff + V-diff
};

// Step outcome code exposed to the C ABI query function.
// Values are stable across language boundary (Fortran/C/C++).
enum class StepOutcomeCode : int {
    OK_ADVANCED = 0,
    OK_SKIPPED = 1,        // e.g., rk_step != 1 in unifiedStep
    SOFT_NO_PROGRESS = 10, // finite solve path but update ratio is effectively zero
    HARD_STAGE_ABORT = 20, // stage-abort path committed U_{n+1}=U_n (or equivalent abort)
    FATAL_INPUT = 100,
    FATAL_INTERNAL = 101
};

// FIX Round93: TLS solver tracking for debug mode warnings
// When multiple solvers run on the same thread, TLS caches are shared.
// This tracking helps detect potential issues in multi-solver scenarios.
//
// FIX Round120: CROSS-DSO (SHARED LIBRARY) LIMITATION
// These variables are declared as `inline thread_local`. In C++17, each DSO
// (Dynamic Shared Object / shared library) that includes this header gets its
// OWN copy of these TLS variables. This means:
//   - If libsdirk3.so and libwrf_physics.so both include this header,
//     each library has independent tls_release_check_counter, etc.
//   - resetTLSTracking() only resets the calling DSO's copy
//   - This is acceptable for WRF's typical single-DSO build
//   - For multi-DSO builds requiring cross-DSO TLS consistency, consider:
//     (a) Moving TLS variables to a single .cpp file with extern declarations
//     (b) Using a shared singleton with explicit DSO registration
//     (c) Accepting per-DSO isolation as intended behavior
// Current design assumes single-DSO usage (WRF monolithic build).
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
namespace tls_debug {
    // Last solver that touched TLS caches on this thread (debug only)
    inline thread_local void* last_tls_solver_ptr = nullptr;

    // FIX Round94: Epoch-based throttling for TLS solver tracking
    // Only log warnings every N solver changes to avoid log spam on high-frequency paths
    inline thread_local uint64_t tls_solver_change_count = 0;
    inline thread_local uint64_t tls_last_logged_count = 0;
    // FIX Round143/Round146: TLS_LOG_THROTTLE_INTERVAL is compile-time constant for performance.
    // Current value (1000) balances diagnostic visibility with log volume.
    // FUTURE: If runtime tuning needed, add g_sdirk3_config.tls_log_interval (uint64_t).
    //   Implementation: Replace constexpr with config read in log_tls_change().
    //   Trade-off: Config read adds ~1 memory access per throttle check.
    constexpr uint64_t TLS_LOG_THROTTLE_INTERVAL = 1000;  // Log every 1000th change

    // FIX Round119: Release-mode TLS check sampling counter (moved from function-local)
    // When WRF_SDIRK3_RELEASE_TLS_CHECK is enabled, this counter implements sampling
    // to reduce hot-path overhead. Moved to namespace scope so resetTLSTracking() can
    // reset it for deterministic diagnostics. The counter is always defined for ODR
    // consistency, but only used when WRF_SDIRK3_RELEASE_TLS_CHECK is enabled.
    inline thread_local uint32_t tls_release_check_counter = 0;

    // Check and warn if different solver is touching TLS caches
    // FIX Round94: Added epoch-based throttling to reduce overhead on hot paths
    // FIX Round122: Respect log_solver_pointer policy for pointer masking
    inline void checkTLSSolverChange(void* current_solver, int debug_level) {
        if (debug_level >= 2 && last_tls_solver_ptr != nullptr &&
            last_tls_solver_ptr != current_solver) {
            // FIX Round94: Throttle logging - only log every N solver changes
            ++tls_solver_change_count;
            if (tls_solver_change_count - tls_last_logged_count >= TLS_LOG_THROTTLE_INTERVAL) {
                std::cerr << "SDIRK3 DEBUG: TLS cache accessed by different solver on same thread\n";
                // FIX Round122: Apply log_solver_pointer policy for consistency
                if (g_sdirk3_config.log_solver_pointer) {
                    std::cerr << "  Previous: " << last_tls_solver_ptr << "\n"
                              << "  Current:  " << current_solver << "\n";
                } else {
                    std::cerr << "  Previous: <masked>\n"
                              << "  Current:  <masked>\n";
                }
                std::cerr << "  TLS caches are shared - this is expected but may cause cross-contamination.\n"
                          << "  Use single solver per thread or accept shared TLS state.\n"
                          << "  (Suppressed " << (tls_solver_change_count - tls_last_logged_count - 1)
                          << " similar warnings, total changes: " << tls_solver_change_count << ")"
                          << std::endl;
                tls_last_logged_count = tls_solver_change_count;
            }
        }
        last_tls_solver_ptr = current_solver;
    }

    // FIX Round142: TLS CACHE REVALIDATION COUNTERS (for profiling)
    // Track cache hit/miss/revalidation rates to diagnose performance issues.
    // Access pattern: fast_hit + reval_hit + slow_miss = total_lookups
    //   - fast_hit: Perfect cache hit (generation matches)
    //   - reval_hit: Generation mismatch but solver still valid (revalidation)
    //   - slow_miss: Full re-lookup (solver changed or first access)
    // FIX Round145: PROFILING REQUIREMENT - Counter increments are GATED by debug_level >= 2.
    //   When debug_level < 2, counters stay at 0 (no hot-path overhead).
    //   Metrics logged every TLS_LOG_THROTTLE_INTERVAL lookups when enabled.
    // FIX Round144: MULTI-DSO NOTE - These counters are per-thread AND per-DSO.
    //   In multi-DSO environments, each DSO has its own counter set.
    //   For unified statistics, use single shared library build.
    // FIX Round145: RESET NOTE - Counters are thread_local, so resetTLSTracking() only
    //   affects the calling thread. To reset all worker threads, call from within
    //   parallel region (e.g., #pragma omp parallel { resetTLSTracking(); }).
    inline thread_local uint64_t tls_cache_fast_hit = 0;
    inline thread_local uint64_t tls_cache_reval_hit = 0;
    inline thread_local uint64_t tls_cache_slow_miss = 0;
    inline thread_local uint64_t tls_cache_last_logged_total = 0;

    // FIX Round94: Reset TLS tracking state (call when restarting simulation)
    // FIX Round119: Also resets tls_release_check_counter for deterministic diagnostics
    // FIX Round142: Also resets revalidation counters
    inline void resetTLSTracking() {
        last_tls_solver_ptr = nullptr;
        tls_solver_change_count = 0;
        tls_last_logged_count = 0;
        tls_release_check_counter = 0;  // FIX Round119: Reset sampling counter
        // FIX Round142: Reset revalidation counters
        tls_cache_fast_hit = 0;
        tls_cache_reval_hit = 0;
        tls_cache_slow_miss = 0;
        tls_cache_last_logged_total = 0;
    }
}
#endif

// Zero-Copy configuration structure
struct ZeroCopyConfig {
    // State variable pointers
    float* u_ptr = nullptr;
    float* v_ptr = nullptr;
    float* w_ptr = nullptr;
    float* ph_ptr = nullptr;
    float* al_ptr = nullptr;
    float* mu_ptr = nullptr;
    float* moist_ptr = nullptr;  // 4D moisture array (i,k,j,species) from WRF
    int n_moist = 0;              // Number of moisture species
    float* qv_ptr = nullptr;   // ADDED: Water vapor mixing ratio (legacy)

    // PARITY FIX 2025-12-13: Chemistry, tracer, and scalar array pointers
    // WRF vertical_diffusion_s loops over all chem/tracer/scalar species
    float* chem_ptr = nullptr;    // 4D chemistry array (i,k,j,species) from WRF-Chem
    int n_chem = 0;               // Number of chemistry species
    float* tracer_ptr = nullptr;  // 4D tracer array (i,k,j,species) from WRF
    int n_tracer = 0;             // Number of tracer species
    float* scalar_ptr = nullptr;  // 4D scalar array (i,k,j,species) for additional scalars
    int n_scalar = 0;             // Number of scalar species

    float* p_ptr = nullptr;    // perturbation pressure
    float* t_ptr = nullptr;    // perturbation theta

    // Tendency pointers
    float* ru_tend_ptr = nullptr;
    float* rv_tend_ptr = nullptr;
    float* rw_tend_ptr = nullptr;
    float* ph_tend_ptr = nullptr;
    float* al_tend_ptr = nullptr;
    float* mu_tend_ptr = nullptr;
    float* p_tend_ptr = nullptr;
    float* t_tend_ptr = nullptr;

    // PARITY FIX 2025-12-13: Moist/chem/tracer/scalar tendency OUTPUT pointers
    // Vertical diffusion tendencies must be written back to WRF arrays
    // These point to WRF's moist_tendf, chem_tendf, tracer_tendf arrays
    float* moist_tend_ptr = nullptr;  // 4D moist tendency array (i,k,j,species)
    float* chem_tend_ptr = nullptr;   // 4D chem tendency array (i,k,j,species)
    float* tracer_tend_ptr = nullptr; // 4D tracer tendency array (i,k,j,species)
    float* scalar_tend_ptr = nullptr; // 4D scalar tendency array (i,k,j,species)

    // PARITY FIX 2025-12-13: 4D tendency array stride information
    // WRF Fortran layout (i,k,j,species) requires proper stride calculation
    // for non-contiguous or halo-padded arrays. Caller MUST set these correctly.
    // Stride is the linear offset increment for each dimension.
    // For contiguous arrays: stride_i=1, stride_k=dim_i, stride_j=dim_i*dim_k, stride_species=dim_i*dim_k*dim_j
    // If strides are all 0, write-back is SKIPPED to avoid memory corruption.
    struct Tend4DStrides {
        int dim_i = 0;        // Total i-dimension (ime-ims+1) of the array
        int dim_k = 0;        // Total k-dimension (kme-kms+1) of the array
        int dim_j = 0;        // Total j-dimension (jme-jms+1) of the array
        int i_offset = 0;     // Offset in i from array start to tile start (its-ims)
        int k_offset = 0;     // Offset in k from array start to tile start (kts-kms)
        int j_offset = 0;     // Offset in j from array start to tile start (jts-jms)

        // Check if strides are valid (non-zero dimensions)
        bool is_valid() const {
            return dim_i > 0 && dim_k > 0 && dim_j > 0;
        }

        // Calculate linear index for Fortran column-major (i,k,j,species)
        // where i varies fastest, species slowest
        int linear_index(int i, int k, int j, int species) const {
            int wrf_i = i + i_offset;
            int wrf_k = k + k_offset;
            int wrf_j = j + j_offset;
            return wrf_i + dim_i * (wrf_k + dim_k * (wrf_j + dim_j * species));
        }
    };

    Tend4DStrides moist_tend_strides;   // Strides for moist_tend_ptr
    Tend4DStrides chem_tend_strides;    // Strides for chem_tend_ptr
    Tend4DStrides tracer_tend_strides;  // Strides for tracer_tend_ptr
    Tend4DStrides scalar_tend_strides;  // Strides for scalar_tend_ptr

    // Grid metrics
    float rdx = 0.0f;
    float rdy = 0.0f;
    // FIX 2025-01-11 Round59: Optional FP64 rdx/rdy for precision preservation
    // When set (non-zero), these are used instead of float rdx/rdy for FP64 simulation paths
    // This avoids precision loss from float→double promotion when computing dx_fp64/dy_fp64
    // Callers should set these directly from double-precision source values (e.g., grid%dx)
    double rdx_fp64 = 0.0;
    double rdy_fp64 = 0.0;
    float* rdnw_ptr = nullptr;
    float* rdn_ptr = nullptr;
    
    // Divergence damping and diffusion coefficients
    float kdamp = 0.0f;      // Divergence damping coefficient
    float khdif = 0.0f;      // Horizontal diffusion coefficient
    float kvdif = 0.0f;      // Vertical diffusion coefficient
    
    // Map scale factors
    float* msftx_ptr = nullptr;
    float* msfty_ptr = nullptr;
    float* msfux_ptr = nullptr;
    float* msfuy_ptr = nullptr;
    float* msfvx_ptr = nullptr;
    float* msfvy_ptr = nullptr;
    
    // Vertical coordinate coefficients
    float* c1f_ptr = nullptr;
    float* c2f_ptr = nullptr;
    float* c1h_ptr = nullptr;
    float* c2h_ptr = nullptr;
    
    // Vertical interpolation weights (fnm=fzm, fnp=fzp in WRF)
    float* fnm_ptr = nullptr;  // Weight for level k in vertical interpolation
    float* fnp_ptr = nullptr;  // Weight for level k-1 in vertical interpolation
    
    // Coriolis parameters (spatially varying)
    float* f_ptr = nullptr;    // 2*Omega*sin(lat) at mass points
    float* e_ptr = nullptr;    // 2*Omega*cos(lat) at mass points
    float* sina_ptr = nullptr; // sin(alpha) for coordinate rotation
    float* cosa_ptr = nullptr; // cos(alpha) for coordinate rotation
    
    // Moisture correction factors
    float* cqu_ptr = nullptr;  // Moisture correction for U-momentum pressure gradient
    float* cqv_ptr = nullptr;  // Moisture correction for V-momentum pressure gradient
    float* cqw_ptr = nullptr;  // Moisture correction for W-momentum pressure gradient
    
    // Bounds
    int its, ite, jts, jte, kts, kte;
    int ids, ide, jds, jde, kds, kde;
    int ims, ime, jms, jme, kms, kme;
    
    // Dimensions
    int nx, ny, nz;
    int nx_u, ny_v, nz_w;
    
    // Boundary conditions
    bool* periodic_x_ptr = nullptr;
    bool* periodic_y_ptr = nullptr;

    // ============================================================================
    // Boundary Arrays for Specified/Nested BC (Issue #4)
    // ============================================================================

    // Boundary value arrays (24 arrays: 6 variables × 4 sides)
    // X-boundaries: Fortran (j,k,spec_bdy_width) → PyTorch [j, k, spec]
    float* u_bdy_xs = nullptr;   float* u_bdy_xe = nullptr;
    float* v_bdy_xs = nullptr;   float* v_bdy_xe = nullptr;
    float* w_bdy_xs = nullptr;   float* w_bdy_xe = nullptr;
    float* t_bdy_xs = nullptr;   float* t_bdy_xe = nullptr;
    float* ph_bdy_xs = nullptr;  float* ph_bdy_xe = nullptr;
    float* mu_bdy_xs = nullptr;  float* mu_bdy_xe = nullptr;

    // Y-boundaries: Fortran (i,k,spec_bdy_width) → PyTorch [i, k, spec]
    float* u_bdy_ys = nullptr;   float* u_bdy_ye = nullptr;
    float* v_bdy_ys = nullptr;   float* v_bdy_ye = nullptr;
    float* w_bdy_ys = nullptr;   float* w_bdy_ye = nullptr;
    float* t_bdy_ys = nullptr;   float* t_bdy_ye = nullptr;
    float* ph_bdy_ys = nullptr;  float* ph_bdy_ye = nullptr;
    float* mu_bdy_ys = nullptr;  float* mu_bdy_ye = nullptr;

    // Boundary tendency arrays (24 arrays: 6 variables × 4 sides)
    // X-boundaries: Fortran (j,k,spec_bdy_width) → PyTorch [j, k, spec]
    float* u_btend_xs = nullptr; float* u_btend_xe = nullptr;
    float* v_btend_xs = nullptr; float* v_btend_xe = nullptr;
    float* w_btend_xs = nullptr; float* w_btend_xe = nullptr;
    float* t_btend_xs = nullptr; float* t_btend_xe = nullptr;
    float* ph_btend_xs = nullptr; float* ph_btend_xe = nullptr;
    float* mu_btend_xs = nullptr; float* mu_btend_xe = nullptr;

    // Y-boundaries: Fortran (i,k,spec_bdy_width) → PyTorch [i, k, spec]
    float* u_btend_ys = nullptr; float* u_btend_ye = nullptr;
    float* v_btend_ys = nullptr; float* v_btend_ye = nullptr;
    float* w_btend_ys = nullptr; float* w_btend_ye = nullptr;
    float* t_btend_ys = nullptr; float* t_btend_ye = nullptr;
    float* ph_btend_ys = nullptr; float* ph_btend_ye = nullptr;
    float* mu_btend_ys = nullptr; float* mu_btend_ye = nullptr;

    // Relaxation coefficient arrays (4 arrays: 2 variables × 2 directions)
    float* fcx_ptr = nullptr;  // X-direction relaxation term (1D, size: ims:ime)
    float* gcx_ptr = nullptr;  // X-direction 2nd relaxation term (1D, size: ims:ime)
    float* fcy_ptr = nullptr;  // Y-direction relaxation term (1D, size: jms:jme)
    float* gcy_ptr = nullptr;  // Y-direction 2nd relaxation term (1D, size: jms:jme)

    // Boundary zone parameters
    int spec_bdy_width = 5;    // Total boundary array width (typically 5)
    int spec_zone = 1;         // Outer specified zone width (typically 1, no relaxation)
    int relax_zone = 4;        // Inner edge of relaxation zone (typically 4)
    float dtbc = 0.0f;         // Temporal interpolation increment (from grid%dtbc)

    // ============================================================================
    // END Boundary Arrays
    // ============================================================================
};

} // namespace sdirk3
} // namespace wrf

// Forward declarations
namespace wrf {
namespace sdirk3 {
    class UnifiedRHS;

    // FIX 2025-12-28: Expose msf 3D cache invalidation for grid changes (moving nests, regridding)
    // Call this when map factors are updated to force recomputation of cached 3D expansions.
    void invalidateMsf3DCache();
}
}

class TileSDIRK3UnifiedSolver : public wrf::sdirk3::TileSDIRK3Solver {
private:
    // UnifiedRHS for physics computation (FINAL_DESIGN.md compliant)
    std::unique_ptr<wrf::sdirk3::UnifiedRHS> unified_rhs_;
    
public:
    TileSDIRK3UnifiedSolver(int nx, int ny, int nz, 
                           float dx, float dy,
                           const std::vector<float>& rdx,
                           const std::vector<float>& rdy,
                           const std::vector<float>& rdnw,
                           int tile_id);
    
    ~TileSDIRK3UnifiedSolver();

    // OPT Pass33+: Explicit non-copyable/non-movable due to std::atomic and std::mutex members
    TileSDIRK3UnifiedSolver(const TileSDIRK3UnifiedSolver&) = delete;
    TileSDIRK3UnifiedSolver& operator=(const TileSDIRK3UnifiedSolver&) = delete;
    TileSDIRK3UnifiedSolver(TileSDIRK3UnifiedSolver&&) = delete;
    TileSDIRK3UnifiedSolver& operator=(TileSDIRK3UnifiedSolver&&) = delete;

    // Implement pure virtual method from base class
    int solve(double* state_new, const double* state_old, 
              const double* forcing, int stage_num) override;
    
    // Unified SDIRK-3 step (all 3 stages internally)
    // Now uses potential temperature instead of inverse density
    void unifiedStep(float* u, float* v, float* w,
                    float* ph, float* theta, float* mu,  // theta replaces al
                    float* ru_tend, float* rv_tend, float* rw_tend,
                    float* ph_tend, float* theta_tend, float* mu_tend,  // theta_tend replaces al_tend
                    float rdx, float rdy, float* rdnw, float* rdn,
                    float* msftx, float* msfty,
                    float* msfux, float* msfuy,
                    float* msfvx, float* msfvy,
                    float* c1f, float* c2f, float* c1h, float* c2h,  // Vertical coordinate coefficients
                    float* fnm, float* fnp,  // Vertical interpolation weights (also used as fzm, fzp)
                    int rk_step, float dt,
                    int nx, int ny, int nz,
                    int nx_u, int ny_v, int nz_w);
    
    // Zero-Copy version - operates directly on WRF grid memory
    void advanceZeroCopy(const wrf::sdirk3::ZeroCopyConfig& config, 
                        int rk_step, float dt);
    
    // Set base state for perturbation calculations
    // Note: th_base is actually alb (inverse density) from WRF
    void setBaseState(const float* p_base, const float* alb_base,
                     const float* ph_base, const float* mu_base);

    // Compute 3D vertical metrics from geopotential for horizontal diffusion
    // PARITY FIX 2025-12-09: Required for proper g/(dn*rdz) scaling
    // Should be called after setBaseState when ph_base is available
    // ph_pert: perturbation geopotential [ny, nz_w, nx] - can be nullptr if base-only
    void computeVerticalMetrics(const torch::Tensor& ph_pert = torch::Tensor());

    // PARITY FIX 2025-12-09: Compute or set terrain slope fields for horizontal diffusion
    // zx = ∂z/∂x, zy = ∂z/∂y in terrain-following coordinates
    // Can be computed from geopotential or set directly from WRF arrays
    void computeTerrainSlopes(const torch::Tensor& ph_pert = torch::Tensor());
    void setTerrainSlopes(const torch::Tensor& zx, const torch::Tensor& zy);

    // Set boundary condition flags from WRF configuration
    void setBoundaryConditions(bool periodic_x, bool periodic_y,
                             bool symmetric_xs, bool symmetric_xe,
                             bool symmetric_ys, bool symmetric_ye,
                             bool open_xs, bool open_xe,
                             bool open_ys, bool open_ye,
                             bool specified = false, bool nested = false);
    
    // Set WRF indices for proper halo exchange and boundary handling
    void setWRFIndices(int its, int ite, int jts, int jte, int kts, int kte,
                       int ids, int ide, int jds, int jde, int kds, int kde,
                       int ims, int ime, int jms, int jme, int kms, int kme);
    
    // Set staggered grid dimensions
    void setStaggeredDimensions(int nx_u, int ny_v, int nz_w);

    // SINGLE geometry contract (external review round 3j): validate the
    // caller's per-call dimensions against the solver's initialized geometry.
    // int64 arithmetic (no signed-overflow UB), all six required positive and
    // sane, stagger shape base <= staggered <= base+1, and STRICT equality
    // with the init geometry — no "uninitialized => allow" escape (the
    // constructor and setStaggeredDimensions guarantee valid init values or
    // throw). Called from the v2 ABI entry BEFORE setWRFIndices/any state
    // change, and again from advanceZeroCopy (defense in depth). Throws
    // std::runtime_error on violation.
    void validateCallGeometry(long long nx, long long ny, long long nz,
                              long long nx_u, long long ny_v, long long nz_w) const;

    /**
     * FIX 2025-01-11 Round79/Round81: SINGLE ENTRY POINT for all cache invalidation
     *
     * Invalidate ALL internal caches when map factors or grid data change.
     * Call this when map factors are updated (moving nests, regridding).
     *
     * FIX Round81: This is now the SINGLE ENTRY POINT for reset_full.
     * It calls grid_info_->invalidateVerticalMetricCaches() to include all
     * epoch-based caches, avoiding duplicate epoch increments.
     *
     * ═══════════════════════════════════════════════════════════════════════════
     * CACHE INVENTORY (FIX Round79/Round81 - authoritative list for maintenance)
     * ═══════════════════════════════════════════════════════════════════════════
     *
     * SOLVER CACHES (invalidated directly):
     *
     * 1. DIVERGENCE CACHE (invalidateDivergenceCache)
     *    - Location: TileSDIRK3UnifiedSolver member
     *    - Purpose: Cached divergence computations for advection
     *
     * 2. MSF 3D EXPANSION CACHE (wrf::sdirk3::invalidateMsf3DCache)
     *    - Location: wrf_sdirk3_unified_rhs.cpp thread_local
     *    - Purpose: Expanded 3D map scale factors from 2D inputs
     *
     * 3. UNIFIED RHS ACOUSTIC METRIC CACHE (unified_rhs_->invalidate_acoustic_metric_cache)
     *    - Location: UnifiedRHS member
     *    - Contents: rdzw/rdz device tensors for acoustic computations
     *
     * 4. PRESSURE GRADIENT CACHES (wrf::sdirk3::pg_detail::invalidateAllCaches)
     *    - Location: wrf_sdirk3_pressure_gradient.cpp thread_local
     *    - Contents: rdx/rdy scalars, c1h/c2h/c1f/c2f/rdnw/rdn arrays
     *
     * GRID METRIC CACHES (via grid_info_->invalidateVerticalMetricCaches):
     *
     * 5. STATIC METRIC CACHES (metric_utils::invalidateStaticMetricCaches)
     *    - Location: SpatialDerivativesAutograd thread_local
     *    - Contents: rdz, dnw, dn vertical metrics (via global epoch)
     *
     * 6. Z1D PROFILE CACHE (invalidateZ1DCache)
     *    - Location: wrf_sdirk3_rayleigh_damping_ad.cpp
     *    - Purpose: Rayleigh damping z1d profiles
     *
     * 7. DZ MIN CACHE (invalidateDzMinCache)
     *    - Location: wrf_sdirk3_boundary_ad.cpp
     *    - Purpose: CFL check min(dz) values
     *
     * 8. LAT CPU CACHE (invalidateLatCpuCache)
     *    - Location: wrf_sdirk3_boundary_ad.cpp
     *    - Purpose: Boundary condition latitude values
     *
     * 9. SCALAR MEAN CACHE (invalidateScalarMeanCache)
     *    - Location: wrf_sdirk3_unified_preconditioner.cpp
     *    - Contents: mub/c1f/c2f/msfty/mu_base .mean() values (via scalar epoch)
     *
     * ═══════════════════════════════════════════════════════════════════════════
     * MAINTENANCE NOTE (FIX Round79/Round81):
     * When adding new caches to the solver, you MUST:
     *   1. Add cache invalidation call to this function OR to invalidateVerticalMetricCaches()
     *   2. Update this inventory documentation
     *   3. Update wrf_sdirk3_interface.h sdirk3_tile_solver_reset_full() docs
     *   4. Update module_implicit_sdirk3.F (sole bridge) Fortran binding docs
     * ═══════════════════════════════════════════════════════════════════════════
     *
     * DESIGN DECISION (FIX Round82): Full invalidation vs "light invalidate" path
     * ─────────────────────────────────────────────────────────────────────────────
     * This function always invalidates ALL 9 caches. A "light invalidate" path
     * (only solver caches, skip grid metric caches) was considered but rejected:
     *
     *   1. SIMPLICITY: One code path easier to maintain/debug than two
     *   2. SAFETY: No risk of caller using wrong path and leaving stale caches
     *   3. PERFORMANCE: Invalidation is O(1) - just flag/epoch increments
     *   4. FREQUENCY: Called only on grid changes (rare), not per-timestep
     *
     * If profiling ever shows invalidation overhead is significant, consider
     * adding a separate invalidateSolverCachesOnly() that skips the epoch-based
     * caches, but document clearly when each should be used.
     *
     * THREAD POLICY (FIX Round83/Round84/Round88):
     * ─────────────────────────────────────────────────────────────────────────────
     * Caches #2 (MSF 3D) and #4 (pressure gradient) are thread_local.
     * This function only invalidates caches in the CALLING THREAD.
     * In multi-threaded execution (e.g., OpenMP parallel regions):
     *   - Each thread must call invalidateCaches() independently, OR
     *   - Use epoch-based invalidation (caches #5-9) which uses global atomics
     * For WRF's typical usage (single-threaded solver per tile), this is safe.
     *
     * FIX Round88: TLS CACHE SHARING BETWEEN SOLVERS
     * Thread-local caches are shared by ALL solvers running on the same thread.
     * If you run multiple solvers on one thread:
     *   - invalidateCaches() on solver A will clear TLS caches for solver B too
     *   - This is by design (epoch-based, not solver-keyed) for simplicity
     *   - WRF's one-solver-per-tile model is unaffected
     *   - For multi-solver-per-thread scenarios, accept shared invalidation or
     *     ensure solvers have compatible cache lifetimes
     *
     * FIX Round84: OpenMP parallel invalidation pattern:
     *   #pragma omp parallel
     *   {
     *       solver->invalidateCaches();  // Each thread invalidates its own
     *   }
     * Or call from within existing OpenMP parallel region where solver is used.
     * WARNING: reset_full() from Fortran is single-threaded - thread_local caches
     * in worker threads will NOT be invalidated. Use single-threaded execution
     * for test harnesses requiring deterministic cache state.
     *
     * FREQUENT INVALIDATION (FIX Round83):
     * ─────────────────────────────────────────────────────────────────────────────
     * In test harnesses with rapid repeated calls, consider:
     *   - Batching grid changes before single invalidation
     *   - Using reset_state() for solver reuse (cheaper than reset_full)
     *   - FIX Round84: Run tests with OMP_NUM_THREADS=1 for deterministic behavior
     * No logging/warning in invalidateCaches() itself to avoid spam.
     * ═══════════════════════════════════════════════════════════════════════════
     */
    void invalidateCaches() {
        // FIX Round81: Call grid_info_->invalidateVerticalMetricCaches() FIRST
        // FIX Round82: Null check allows safe call during partial initialization
        // This is the single entry point for all epoch-based caches:
        //   - Z1D profile cache
        //   - Dz min cache
        //   - Lat CPU cache
        //   - Static metric caches (rdz, dnw, dn) via global epoch
        //   - Scalar mean cache (mub/c1f/c2f/msfty/mu_base) via scalar epoch
        if (grid_info_) {
            grid_info_->invalidateVerticalMetricCaches();
        }

        // 1. Divergence cache (PARITY FIX 2025-12-16)
        invalidateDivergenceCache();

        // 2. MSF 3D expansion cache (FIX 2025-12-28)
        wrf::sdirk3::invalidateMsf3DCache();

        // 3. UnifiedRHS acoustic metric cache - rdzw/rdz device tensors (FIX 2025-01-10 Round5)
        // FIX Round83: Null check for partial initialization (unified_rhs_ may not exist yet)
        if (unified_rhs_) {
            unified_rhs_->invalidate_acoustic_metric_cache();
        }

        // 4. Pressure gradient caches - scalars and arrays (FIX 2025-01-11 Round58)
        wrf::sdirk3::pg_detail::invalidateAllCaches();

        // 5. Coefficient device tensor cache (c1f/c2f/rdnw) (PERF FIX 2026-01-31)
        invalidateCoeffDeviceCache();
    }

    /**
     * FIX Round85/Round87: Invalidate ONLY thread_local caches (for parallel execution)
     *
     * Call this from each OpenMP worker thread to invalidate thread-specific caches
     * without redundant global epoch increments. This avoids the issue where
     * invalidateCaches() called N times increments global epochs N times.
     *
     * Thread-local caches invalidated:
     *   - #2 MSF 3D expansion cache
     *   - #4 Pressure gradient caches
     *
     * NULL SAFETY (FIX Round87): This function calls namespace-scope functions only,
     * not member functions. Safe to call even if solver is partially initialized.
     *
     * PERFORMANCE (FIX Round87): Both functions use epoch-based invalidation
     * (atomic increment). Cost is O(1) per thread regardless of cache size.
     * Actual cache clearing is lazy (happens at next usage, not at invalidation).
     * No "fast path" needed - epoch increment is already minimal cost.
     *
     * Usage pattern (from reset_full_parallel):
     *   // Master thread: global caches
     *   solver->invalidateGlobalCachesOnly();
     *   #pragma omp parallel
     *   {
     *       solver->invalidateThreadLocalCachesOnly();
     *   }
     */
    void invalidateThreadLocalCachesOnly() {
        // FIX Round93/Round97: Track TLS access for multi-solver debugging
        // FIX Round97: Guard call site with debug_level check to eliminate
        // function call overhead in production (debug_level < 2)
#ifndef WRF_SDIRK3_NO_TLS_TRACKING
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            // OPT Pass34: Use fully qualified namespace
            wrf::sdirk3::tls_debug::checkTLSSolverChange(this, wrf::sdirk3::g_sdirk3_config.debug_level);
        }
#endif

        // 2. MSF 3D expansion cache (thread_local)
        wrf::sdirk3::invalidateMsf3DCache();

        // 4. Pressure gradient caches (thread_local)
        wrf::sdirk3::pg_detail::invalidateAllCaches();

        // OPT Pass33+: TLS TensorViewCache (thread_local from_blob cache)
        // Must be cleared on grid reinit/reset to avoid stale boundary tensor views
        wrf::sdirk3::getThreadLocalTensorViewCache().clear();
    }

    /**
     * FIX Round85/Round87: Invalidate ONLY global/epoch-based caches (for master thread)
     *
     * Call this from the master thread only to invalidate caches that use
     * global atomics or are not thread_local. Avoids redundant epoch increments
     * when combined with invalidateThreadLocalCachesOnly() in parallel region.
     *
     * Global caches invalidated:
     *   - #1 Divergence cache (member variable)
     *   - #3 Acoustic metric cache (member variable)
     *   - #5-9 Grid metric caches (via global epochs)
     *
     * NULL SAFETY (FIX Round87): All member accesses are guarded with null checks.
     * Safe to call even if solver is partially initialized (grid_info_ or
     * unified_rhs_ may be null).
     */
    void invalidateGlobalCachesOnly() {
        // Grid metric caches (epoch-based, thread-safe)
        if (grid_info_) {
            grid_info_->invalidateVerticalMetricCaches();
        }

        // 1. Divergence cache (member variable, not thread_local)
        invalidateDivergenceCache();

        // 3. Acoustic metric cache (member variable, not thread_local)
        if (unified_rhs_) {
            unified_rhs_->invalidate_acoustic_metric_cache();
        }
    }

    // Set MPI process information for parallel halo exchange
    void setMPIProcessInfo(int nprocx, int nprocy, int mypx, int mypy);
    
    // Set non-hydrostatic mode flag
    void setNonHydrostatic(bool non_hydrostatic) { non_hydrostatic_ = non_hydrostatic; }
    
    // Set vertical interpolation coefficients for non-hydrostatic dpn
    void setVerticalInterpolationCoefficients(const float* fnm, const float* fnp,
                                            float cf1, float cf2, float cf3);
    
    // Get grid info for extended features
    std::shared_ptr<wrf::sdirk3::WRFGridInfo> getGridInfo() const { return grid_info_; }
    
    // Base state query methods
    bool isBaseStateInitialized() const { return base_state_initialized_; }
    torch::Tensor getBaseStatePressure() const { return p_base_; }

    // 4DVAR checkpoint query helpers
    size_t getSavedTrajectoryCount() const {
        return newton_solver_ ? newton_solver_->get_saved_trajectory_count() : 0;
    }
    int getSavedGlobalTimestep() const {
        return newton_solver_ ? newton_solver_->get_saved_global_timestep() : 0;
    }
    std::vector<torch::Tensor> getSavedTrajectory() const {
        return newton_solver_ ? newton_solver_->get_saved_trajectory() : std::vector<torch::Tensor>{};
    }
    void clearSavedTrajectory() {
        if (newton_solver_) {
            newton_solver_->clear_saved_trajectory();
        }
    }
    void setSavedTrajectory(const std::vector<torch::Tensor>& checkpoints,
                            int global_timestep = -1) {
        if (newton_solver_) {
            newton_solver_->set_saved_trajectory(checkpoints, global_timestep);
        }
    }
    int64_t getStateVectorSize() const;
    torch::Tensor runAdjointReplay(const torch::Tensor& lambda_terminal,
                                   float dt,
                                   float gamma,
                                   int gmres_restart = 15,
                                   int gmres_max_iterations = 80,
                                   float gmres_tolerance = 1e-5f);

    // PARITY FIX 2025-12-13: Getters for accumulated moist scalar tendencies (vertical diffusion)
    // Returns tendencies accumulated from vertical diffusion (moist_tendf in WRF)
    torch::Tensor getQvTendency() const { return qv_tend_; }
    torch::Tensor getQcTendency() const { return qc_tend_; }
    torch::Tensor getQrTendency() const { return qr_tend_; }
    torch::Tensor getQiTendency() const { return qi_tend_; }
    torch::Tensor getQsTendency() const { return qs_tend_; }
    torch::Tensor getQgTendency() const { return qg_tend_; }

    // Get all moist tendencies as a vector (species 0=qv, 1=qc, 2=qr, 3=qi, 4=qs, 5=qg)
    std::vector<torch::Tensor> getMoistTendencies() const {
        return {qv_tend_, qc_tend_, qr_tend_, qi_tend_, qs_tend_, qg_tend_};
    }

    // Get the number of active moist species
    int getNumMoistSpecies() const { return n_moist_; }

    // PARITY FIX 2025-12-13: Getters for chemistry, tracer, and scalar tendencies
    // Returns tendencies accumulated from vertical diffusion (like chem_tendf, tracer_tendf in WRF)

    // Get all chemistry tendencies as a vector
    std::vector<torch::Tensor> getChemTendencies() const { return chem_tend_; }

    // Get tendency for a specific chemistry species (0-indexed)
    torch::Tensor getChemTendency(int species) const {
        if (species >= 0 && species < static_cast<int>(chem_tend_.size())) {
            return chem_tend_[species];
        }
        return torch::Tensor();  // Return empty tensor if out of range
    }

    // Get the number of active chemistry species
    int getNumChemSpecies() const { return n_chem_; }

    // Get all tracer tendencies as a vector
    std::vector<torch::Tensor> getTracerTendencies() const { return tracer_tend_; }

    // Get tendency for a specific tracer species (0-indexed)
    torch::Tensor getTracerTendency(int species) const {
        if (species >= 0 && species < static_cast<int>(tracer_tend_.size())) {
            return tracer_tend_[species];
        }
        return torch::Tensor();  // Return empty tensor if out of range
    }

    // Get the number of active tracer species
    int getNumTracerSpecies() const { return n_tracer_; }

    // Get all scalar tendencies as a vector
    std::vector<torch::Tensor> getScalarTendencies() const { return scalar_tend_; }

    // Get tendency for a specific scalar species (0-indexed)
    torch::Tensor getScalarTendency(int species) const {
        if (species >= 0 && species < static_cast<int>(scalar_tend_.size())) {
            return scalar_tend_[species];
        }
        return torch::Tensor();  // Return empty tensor if out of range
    }

    // Get the number of active scalar species
    int getNumScalarSpecies() const { return n_scalar_; }

    // Step outcome snapshot for ABI-side non-freeze contract.
    int getLastStepOutcomeCode() const { return last_step_outcome_code_; }
    bool getLastStepFinalUpdateAborted() const { return last_step_final_update_aborted_; }
    float getLastStepProgressRatio() const { return last_step_progress_ratio_; }
    bool isLastStepProgressRatioValid() const { return last_step_progress_ratio_valid_; }
    void setLastStepOutcome(int code,
                            bool final_update_aborted,
                            float progress_ratio,
                            bool progress_ratio_valid) {
        last_step_outcome_code_ = code;
        last_step_final_update_aborted_ = final_update_aborted;
        last_step_progress_ratio_ = progress_ratio;
        last_step_progress_ratio_valid_ = progress_ratio_valid;
    }

private:
    // Grid information (extended for advanced features)
    std::shared_ptr<wrf::sdirk3::WRFGridInfo> grid_info_;
    // Unified preconditioner (unified_rhs_ already declared above)
    std::unique_ptr<wrf::sdirk3::UnifiedPreconditioner> unified_precond_;
    
    // Newton-Krylov solver
    std::unique_ptr<wrf::sdirk3::WRFNewtonKrylovSolver> newton_solver_;
    
    // Grid parameters
    std::vector<float> rdx_;
    std::vector<float> rdy_;
    std::vector<float> rdnw_;
    // PARITY FIX 2025-12-25: Global update sequence for proper freshness comparison.
    // Independent epoch counters (vec/grid) can't determine true recency.
    // Each source records its last update sequence for proper "which is newer" comparison.
    mutable uint64_t rdnw_update_seq_ = 0;  // Global sequence counter (incremented on any update)
    mutable uint64_t rdnw_seq_vec_ = 0;     // Sequence when rdnw_ vector was last updated
    mutable uint64_t rdnw_seq_grid_ = 0;    // Sequence when grid_info_->rdnw was last updated
    // Legacy epoch counters kept for compatibility (may be removed later)
    mutable uint64_t rdnw_epoch_vec_ = 0;   // Version counter for rdnw_ vector updates
    mutable uint64_t rdnw_epoch_grid_ = 0;  // Version counter for grid_info_->rdnw updates
    // PARITY FIX 2025-12-19: Track when grid_info_->rdnw was last synced to rdnw_epoch_vec_.
    // Only update grid_info_->rdnw (which creates new tensor via clone) when vector epoch changes.
    mutable uint64_t rdnw_epoch_cached_grid_ = 0;
    // PARITY FIX 2025-12-19: Signature for fast rdnw value change detection (O(1) vs O(n) comparison)
    float rdnw_signature_ = 0.0f;
    // PARITY FIX 2025-12-20: Track grid_info_->rdnw pointer and signature for epoch bumping.
    // GridRdnw source may have same pointer but in-place value changes that need detection.
    mutable const void* grid_rdnw_ptr_ = nullptr;  // Last-seen grid_info_->rdnw data pointer
    mutable float grid_rdnw_signature_ = 0.0f;  // Signature for fast O(1) grid_info_->rdnw change detection
    // PARITY FIX 2025-12-20: Snapshot of grid_info_->rdnw for memcmp comparison.
    // Used when signature matches to detect changes at non-sample points.
    // PARITY FIX 2025-12-20: Changed to uint8_t for dtype-safe raw byte comparison.
    mutable std::vector<uint8_t> grid_rdnw_snapshot_;
    // PARITY FIX 2025-12-19: Device-cached rdnw tensor to avoid repeated .to() calls
    mutable torch::Tensor rdnw_dev_;  // Device-aligned rdnw tensor (GPU or CPU)
    mutable uint64_t rdnw_epoch_cached_dev_ = 0;  // Last rdnw_epoch_ when device copy was made
    // PARITY FIX 2025-12-19: Track which source was used to build rdnw_dev_ and its pointer.
    // This enables refresh when source changes (e.g., grid_info_->dnw pointer swap).
    // Uses int encoding: 0=Fallback, 1=GridRdnw, 2=VecRdnw, 3=GridDnw, 4=GridRdn
    mutable int rdnw_dev_src_type_ = 0;  // Which source built rdnw_dev_ (RdnwSource encoded as int)
    mutable const void* rdnw_dev_src_ptr_ = nullptr;  // Source data pointer for cache invalidation
    // PARITY FIX 2025-12-20: Track source-specific epoch for in-place change detection.
    // When using GridDnw/GridRdn, pointer may stay same while values change in-place.
    mutable uint64_t rdnw_dev_src_epoch_ = 0;  // dnw_epoch_ or rdn_epoch_ when cache was built
    // PARITY FIX 2025-12-21: Track cached nz for device cache size validation.
    mutable int64_t rdnw_dev_nz_ = 0;
    // PARITY FIX 2025-12-20: Track ph_base pointer for fallback source invalidation.
    // Fallback uses ph_base to compute ztop = max(1000, max(ph_base)/g).
    // PARITY FIX 2025-12-21: Separate CPU/GPU fallback tracking to prevent cross-device cache pollution.
    // PERF FIX 2025-12-23: Removed rdnw_fallback_ph_base_ptr_* - now use ph_base_max_ptr_* only.
    // PERF FIX 2025-12-22: Fallback epoch counters based on clamped ztop signature.
    // Used by cast cache to detect stale cache when allocator reuses same pointer.
    // Epoch is bumped when signature changes, providing a unique value for cache key.
    mutable uint64_t rdnw_fallback_epoch_gpu_ = 0;
    mutable uint64_t rdnw_fallback_epoch_cpu_ = 0;
    mutable uint64_t rdn_fallback_epoch_gpu_ = 0;
    mutable uint64_t rdn_fallback_epoch_cpu_ = 0;

    // PERF FIX 2025-12-22: Shared helper for ph_base signature computation and epoch management.
    // Consolidates duplicate logic from getRdnwTensor and getRdnTensor fallback paths.
    // PERF FIX 2025-12-23: Now computes g_val|ztop composite signature (g_bits<<32 | ztop_bits).
    // Bumps rdnw_fallback_epoch_* when composite sig changes; callers use epoch for invalidation.
    // This prevents double epoch bumps and duplicate sampling when both functions are called.
    struct PhBaseSignatureResult {
        uint64_t signature;  // NOTE: g_bits<<32 | ztop_bits (g_bits=0 when ztop clamped to z_top_min)
        bool changed;
        float max_val;  // For ztop calculation in fallback
    };
    PhBaseSignatureResult updatePhBaseSignature(const torch::Tensor& ph_base, bool want_cpu) const;

    // PARITY FIX 2025-12-19: Helper to get device-aligned rdnw tensor with unified fallback chain.
    // This consolidates repeated rdnw tensor creation logic across multiple code paths.
    // Returns tensor with padding when source has fewer elements than nz.
    torch::Tensor getRdnwTensor(const torch::Device& device, torch::ScalarType dtype, int64_t nz) const;

    // PARITY FIX 2025-12-20: Cached rdnw reference value (average magnitude) to avoid GPU sync per call.
    // Used in V-diffusion 1D fallback scaling. Recomputed when rdnw source changes.
    // Cache key includes: source type, source pointer, source-specific epoch, nz, and rdnw_dev_ pointer.
    // PARITY FIX 2025-12-20: Added nz and rdnw_dev_.data_ptr() to cache key to detect:
    //   - nz changes from padding/slicing that alter the mean without source change
    //   - Fallback ph_base changes that rebuild rdnw_dev_ with same source type/epoch
    mutable float rdnw_ref_v_cached_ = 0.0f;
    mutable int rdnw_ref_v_src_type_ = 0;          // Which source built cache (RdnwSource as int)
    mutable const void* rdnw_ref_v_src_ptr_ = nullptr;  // Source data pointer
    mutable uint64_t rdnw_ref_v_src_epoch_ = 0;    // Source-specific epoch when cache was built
    mutable int64_t rdnw_ref_v_nz_ = 0;            // nz when cache was built (detect padding changes)
    mutable const void* rdnw_ref_v_dev_ptr_ = nullptr;  // rdnw_dev_.data_ptr() when cache was built
    // PARITY FIX 2025-12-21: Track fallback ph_base signature for CPU cache separation
    mutable uint64_t rdnw_ref_v_fallback_ph_sig_ = 0;
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    mutable int rdnw_ref_v_dev_index_ = -1;

    // PARITY FIX 2025-12-20: Cache for U-diffusion rdnw_ref (mean of interior |rdnw| values).
    // Used in compute_horizontal_diffusion_u 1D fallback scaling.
    // PARITY FIX 2025-12-21: Added dev_ptr to track fallback rebuild (like rdnw_ref_v_dev_ptr_).
    mutable float rdnw_ref_u_cached_ = 0.0f;
    mutable int rdnw_ref_u_src_type_ = 0;
    mutable const void* rdnw_ref_u_src_ptr_ = nullptr;
    mutable uint64_t rdnw_ref_u_src_epoch_ = 0;
    mutable int64_t rdnw_ref_u_nz_ = 0;
    mutable const void* rdnw_ref_u_dev_ptr_ = nullptr;  // rdnw_dev_.data_ptr() when cache was built
    // PARITY FIX 2025-12-21: Track fallback ph_base signature for CPU cache separation
    mutable uint64_t rdnw_ref_u_fallback_ph_sig_ = 0;
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    mutable int rdnw_ref_u_dev_index_ = -1;

    // PARITY FIX 2025-12-20: Cache for W-diffusion rdn_ref_w (mean of interior |rdn| values).
    // Used in compute_horizontal_diffusion_w 1D fallback scaling.
    // PARITY FIX 2025-12-21: Added dev_ptr to track fallback rebuild (like rdnw_ref_v_dev_ptr_).
    mutable float rdn_ref_w_cached_ = 0.0f;
    mutable int rdn_ref_w_src_type_ = 0;
    mutable const void* rdn_ref_w_src_ptr_ = nullptr;
    mutable uint64_t rdn_ref_w_src_epoch_ = 0;
    mutable int64_t rdn_ref_w_nz_ = 0;
    mutable const void* rdn_ref_w_dev_ptr_ = nullptr;  // rdn_dev_.data_ptr() when cache was built
    // PARITY FIX 2025-12-21: Track fallback ph_base signature for CPU cache separation
    mutable uint64_t rdn_ref_w_fallback_ph_sig_ = 0;
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    mutable int rdn_ref_w_dev_index_ = -1;

    // PARITY FIX 2025-12-20: Helper to refresh grid_info_ epochs for in-place change detection.
    // Called at top of getRdnwTensor() and ensureDivergenceCache() to ensure consistent
    // epoch tracking regardless of which code path accesses the cached tensors.
    void refreshGridMetricEpochs() const;

    // PARITY FIX 2025-12-25: Helper to ensure c1f_/c2f_ CPU cache is valid.
    // Handles pointer change detection, signature sampling (with period logic for CUDA),
    // and full memcmp verification. Called by both X and Y direction blocks.
    // Returns true if cache is valid after the call.
    bool ensureC1fC2fCpuCache() const;

    // PARITY FIX 2025-12-20: Device-cached rdn tensor similar to rdnw_dev_.
    // Used in hydrostatic pressure integration and other paths that need rdn.
    mutable torch::Tensor rdn_dev_;  // Device-aligned rdn tensor (GPU or CPU)
    mutable uint64_t rdn_epoch_cached_dev_ = 0;  // Last rdn_epoch_ when device copy was made
    // PARITY FIX 2025-12-20: Source type tracking for rdn cache to prevent cross-source contamination.
    // Same epoch from different sources (Vec vs Grid vs GridDn) should not share cache.
    enum class RdnSource { Vec, Grid, GridDn, Fallback };
    mutable RdnSource rdn_dev_src_type_{RdnSource::Fallback};
    mutable const void* rdn_dev_src_ptr_{nullptr};
    mutable uint64_t rdn_dev_src_epoch_{0};
    // PARITY FIX 2025-12-21: Track cached nz for device cache size validation.
    mutable int64_t rdn_dev_nz_{0};
    // Helper to get device-aligned rdn tensor with caching (like getRdnwTensor for rdn).
    torch::Tensor getRdnTensor(const torch::Device& device, torch::ScalarType dtype, int64_t nz) const;

    std::vector<float> rdzw_;  // Inverse spacing at W-levels for mass-level equations (1D approx)

    // Scalar grid spacing from config (for when vectors are empty)
    float config_rdx_ = 0.0f;
    float config_rdy_ = 0.0f;
    std::vector<float> rdn_;   // rdzu in WRF - inverse spacing between W-levels
    // PARITY FIX 2025-12-20: Split rdn epochs into vec/grid to prevent cross-source invalidation.
    // Similar to rdnw_epoch_vec_/rdnw_epoch_grid_ pattern for proper cache isolation.
    mutable uint64_t rdn_epoch_vec_ = 0;   // Version counter for rdn_ vector updates
    mutable uint64_t rdn_epoch_grid_ = 0;  // Version counter for grid_info_->rdn updates
    // PARITY FIX 2025-12-25: Global update sequence for proper freshness comparison.
    // Independent epoch counters can diverge; global sequence ensures correct priority.
    mutable uint64_t rdn_update_seq_ = 0;  // Global sequence counter (increments on any rdn source update)
    mutable uint64_t rdn_seq_vec_ = 0;     // Sequence when rdn_ vector was last updated
    mutable uint64_t rdn_seq_grid_ = 0;    // Sequence when grid_info_->rdn was last updated
    float rdn_signature_ = 0.0f;           // Signature for fast O(1) change detection on rdn_ vector

    // =========================================================================
    // GRID METRIC WARNING FLAGS (2025-12-25/26)
    // =========================================================================
    // Per-instance, thread-safe warning flags to prevent duplicate log messages.
    // Uses std::atomic<bool> with memory_order_relaxed for minimal overhead.
    //
    // Two source categories:
    //   1. VECTOR SOURCE: rdnw_/rdn_ member vectors (from raw pointer input)
    //   2. GRID SOURCE: grid_info_->rdnw/dnw/rdn/dn tensors
    //
    // NaN/Inf correction policy: Replace with eps=1e-10f via safe_abs_or_eps().
    // See RDNW_FINAL_VERIFICATION.md section 5.2 for details.
    // =========================================================================

    // --- VECTOR SOURCE: negative value warnings ---
    mutable std::atomic<bool> warned_rdnw_negative_{false};       // rdnw_[] negative (sample-based)
    mutable std::atomic<bool> warned_rdn_negative_{false};        // rdn_[] negative (sample-based)

    // --- VECTOR SOURCE: NaN/Inf warnings (sample-based O(1)) ---
    mutable std::atomic<bool> warned_rdnw_nan_inf_{false};        // rdnw_[] NaN/Inf (sample-based)
    mutable std::atomic<bool> warned_rdn_nan_inf_{false};         // rdn_[] NaN/Inf (sample-based)

    // --- VECTOR SOURCE: NaN/Inf warnings (full O(n) periodic check) ---
    // Separate from sample-based to catch NaN/Inf at non-sample positions.
    mutable std::atomic<bool> warned_rdnw_nan_inf_full_{false};   // rdnw_[] NaN/Inf (full check)
    mutable std::atomic<bool> warned_rdn_nan_inf_full_{false};    // rdn_[] NaN/Inf (full check)

    // --- GRID SOURCE: negative value warnings ---
    mutable std::atomic<bool> warned_grid_rdnw_negative_{false};  // grid_info_->rdnw negative
    mutable std::atomic<bool> warned_grid_dnw_negative_{false};   // grid_info_->dnw negative
    mutable std::atomic<bool> warned_grid_rdn_negative_{false};   // grid_info_->rdn negative
    mutable std::atomic<bool> warned_grid_dn_negative_{false};    // grid_info_->dn negative

    // --- GRID SOURCE: NaN/Inf warnings ---
    mutable std::atomic<bool> warned_grid_rdnw_nan_inf_{false};   // grid_info_->rdnw NaN/Inf
    mutable std::atomic<bool> warned_grid_dnw_nan_inf_{false};    // grid_info_->dnw NaN/Inf
    mutable std::atomic<bool> warned_grid_rdn_nan_inf_{false};    // grid_info_->rdn NaN/Inf
    mutable std::atomic<bool> warned_grid_dn_nan_inf_{false};     // grid_info_->dn NaN/Inf

    // =========================================================================
    // GRID METRIC PERIODIC VERIFICATION (2025-12-25/26)
    // =========================================================================
    // Periodic O(n) full comparison when grid_metric_full_verify_period > 0.
    // Counter-based: triggers every N calls (not time-based).
    // =========================================================================
    mutable std::atomic<int> grid_metric_call_counter_{0};               // Call counter (increments on valid data)
    mutable std::atomic<int> last_grid_metric_full_verify_period_{-1};   // Last config value (-1 = uninitialized)
    mutable std::atomic<bool> last_has_grid_metric_data_{false};         // Previous call had data (for false→true reset)

    // =========================================================================
    // DIAGNOSTIC SAMPLING COUNTERS (OPT Pass33+)
    // =========================================================================
    // Separate counters for standard and heavy diagnostics to allow independent
    // sampling phases. This prevents phase coupling when periods are different.
    // Example: standard=5, heavy=10 → heavy samples at calls 1,11,21... not 1,10,20...
    // =========================================================================
    mutable std::atomic<uint64_t> diag_call_counter_{0};                 // Standard diagnostic sampling counter
    mutable std::atomic<uint64_t> diag_heavy_call_counter_{0};           // Heavy diagnostic sampling counter

    // PERF FIX 2025-12-25: Cached index tensor for 5-point sample-based negative detection.
    // Avoids creating new tensor on every debug check. Cache key: (n, device_type, device_index).
    // 2-SLOT CACHE FIX 2025-12-25: Expanded to 2 slots to handle alternating n values (e.g., nz and nz-1).
    // THREAD-SAFETY FIX 2025-12-25: Mutex protects cache updates in multi-threaded scenarios.
    // CACHE KEY FIX 2025-12-25: Use device type + resolved index to handle CUDA device.index()==-1 case.
    static constexpr int kSampleIdxCacheSlots = 2;
    struct SampleIdxCacheEntry {
        int64_t n = 0;                    // Cached tensor size
        int16_t device_type = -1;         // Cached device type (future-proof for DeviceType expansion)
        int device_index = -2;            // Cached device index (resolved via current_device())
        torch::Tensor tensor;             // Cached [0, n/4, n/2, 3*n/4, n-1] indices
        uint64_t last_used = 0;           // LRU counter for eviction
    };
    mutable std::mutex sample_idx_mutex_;                        // Protects cache access/update
    mutable SampleIdxCacheEntry sample_idx_cache_[kSampleIdxCacheSlots];  // 2-slot LRU cache
    mutable uint64_t sample_idx_lru_counter_ = 0;                // Global LRU counter
    // Helper to get cached index tensor for 5-point sampling. Returns tensor on specified device.
    torch::Tensor getSampleIndexTensor(int64_t n, const torch::Device& device) const;

    // PARITY FIX 2025-12-19: Track when grid_info_->rdn was last synced to rdn_epoch_
    mutable uint64_t rdn_epoch_cached_grid_ = 0;
    // PARITY FIX 2025-12-20: Track grid_info_->rdn pointer and signature for epoch bumping.
    // When grid_info_->rdn is the rdnw source, must detect its in-place changes.
    mutable const void* grid_rdn_ptr_ = nullptr;  // Last-seen grid_info_->rdn data pointer
    mutable float grid_rdn_signature_ = 0.0f;  // Signature for fast O(1) grid_info_->rdn change detection
    // PARITY FIX 2025-12-20: Snapshot for memcmp when signature matches.
    // Changed to uint8_t for dtype-safe raw byte comparison.
    mutable std::vector<uint8_t> grid_rdn_snapshot_;
    std::vector<float> dnw_;
    // PARITY FIX 2025-12-20: Separate vector/grid epochs and signatures for dnw_ to prevent
    // false epoch bumps when one changes but not the other.
    mutable uint64_t dnw_epoch_vec_ = 0;   // Version counter for dnw_ vector updates
    mutable uint64_t dnw_epoch_grid_ = 0;  // Version counter for grid_info_->dnw updates
    // PARITY FIX 2025-12-25: Global sequence counter for proper dnw freshness comparison.
    // Allows comparing dnw freshness against rdnw/rdn to select the truly freshest source.
    mutable uint64_t dnw_update_seq_ = 0;  // Global sequence counter (incremented on any dnw update)
    mutable uint64_t dnw_seq_vec_ = 0;     // Sequence when dnw_ vector was last updated
    mutable uint64_t dnw_seq_grid_ = 0;    // Sequence when grid_info_->dnw was last updated
    mutable float dnw_signature_vec_ = 0.0f;   // Signature for dnw_ vector O(1) change detection
    mutable float grid_dnw_signature_ = 0.0f;  // Signature for grid_info_->dnw O(1) change detection
    // PARITY FIX 2025-12-19: Track grid_info_->dnw pointer for epoch bumping.
    // When grid_info_->dnw is the rdnw source, must detect its in-place changes.
    mutable const void* grid_dnw_ptr_ = nullptr;  // Last-seen grid_info_->dnw data pointer
    // PARITY FIX 2025-12-20: Snapshot for memcmp when signature matches.
    // Changed to uint8_t for dtype-safe raw byte comparison.
    mutable std::vector<uint8_t> grid_dnw_snapshot_;

    // PARITY FIX 2025-12-20: Track grid_info_->dn for in-place change detection in getRdnTensor.
    mutable const void* grid_dn_ptr_ = nullptr;   // Last-seen grid_info_->dn data pointer
    mutable float grid_dn_signature_ = 0.0f;      // Signature for O(1) change detection
    mutable uint64_t dn_epoch_ = 0;               // Version counter for dn updates
    // PARITY FIX 2025-12-25: Global sequence counter for proper dn freshness comparison.
    // Allows comparing dn freshness against rdn to select the truly freshest source.
    mutable uint64_t dn_update_seq_ = 0;  // Global sequence counter (incremented on any dn update)
    mutable uint64_t dn_seq_grid_ = 0;    // Sequence when grid_info_->dn was last updated
    // PARITY FIX 2025-12-20: Snapshot for memcmp when signature matches.
    // Changed to uint8_t for dtype-safe raw byte comparison.
    mutable std::vector<uint8_t> grid_dn_snapshot_;

    // PARITY FIX 2025-12-20: Counter for periodic CUDA sampling to detect in-place GPU updates.
    // CUDA tensors normally only sample on pointer change, but in-place GPU updates keep same pointer.
    // Every N calls, force a signature sample for CUDA tensors to catch such updates.
    mutable uint64_t cuda_sample_counter_ = 0;
    static constexpr uint64_t CUDA_SAMPLE_PERIOD = 10;  // Sample every 10 calls

    // PERF FIX 2025-12-22: Cached index tensors for sampling to avoid repeated allocation.
    // Uses 3-slot cache for nz, nz_w, and nesting scenarios with different sizes.
    static constexpr int SIG_INDEX_CACHE_SLOTS = 3;
    mutable torch::Tensor sig_indices_3pt_[SIG_INDEX_CACHE_SLOTS];  // 3-point {0, n/2, n-1} indices
    mutable int64_t sig_indices_3pt_n_[SIG_INDEX_CACHE_SLOTS] = {0, 0, 0};  // Size n for each slot
    mutable torch::Tensor sig_indices_2pt_[SIG_INDEX_CACHE_SLOTS];  // 2-point {0, n-1} indices for first/last
    mutable int64_t sig_indices_2pt_n_[SIG_INDEX_CACHE_SLOTS] = {0, 0, 0};  // Size n for each slot

    // PERF FIX 2025-12-22: Counter for periodic CPU ph_base max recomputation.
    // CPU always has periodic_sample=true, so without this counter, max() runs O(n) every call.
    mutable uint64_t cpu_phbase_max_counter_ = 0;
    static constexpr uint64_t CPU_PHBASE_MAX_PERIOD = 10;  // Recompute max every 10 calls on CPU

    // PARITY FIX 2025-12-24: Separate counter for CUDA ph_base sampling.
    // Avoids dependency on refreshGridMetricEpochs() being called before updatePhBaseSignature().
    // If updatePhBaseSignature() is called directly from another path, sampling won't stagnate.
    mutable uint64_t cuda_phbase_sample_counter_ = 0;

    // PERF FIX 2025-12-22: Counter for periodic CPU memcmp in refreshGridMetricEpochs.
    // Controls accuracy vs performance tradeoff for change detection.
    // g_sdirk3_config.debug_level >= 2: Full memcmp every call (most accurate, highest cost)
    // g_sdirk3_config.debug_level < 2: Periodic memcmp based on CPU_MEMCMP_PERIOD_RELAXED
    mutable uint64_t cpu_memcmp_counter_ = 0;
    static constexpr uint64_t CPU_MEMCMP_PERIOD_STRICT = 1;   // Full memcmp every call
    static constexpr uint64_t CPU_MEMCMP_PERIOD_RELAXED = 10; // Memcmp every 10 calls
    // NOTE: Uses wrf::sdirk3::g_sdirk3_config.debug_level directly for consistency

    // PARITY FIX 2025-12-21: Separate CPU cache for getRdnwTensor/getRdnTensor to avoid
    // GPU->CPU cache pollution when calling with kCPU device (e.g., for scalar extraction).
    // rdnw_dev_ remains GPU-aligned, rdnw_cpu_ is CPU-only cache.
    mutable torch::Tensor rdnw_cpu_;  // CPU-only rdnw cache
    mutable int rdnw_cpu_src_type_ = 0;
    mutable const void* rdnw_cpu_src_ptr_ = nullptr;
    mutable uint64_t rdnw_cpu_src_epoch_ = 0;
    mutable int64_t rdnw_cpu_nz_ = 0;
    mutable torch::Tensor rdn_cpu_;   // CPU-only rdn cache
    mutable RdnSource rdn_cpu_src_type_{RdnSource::Fallback};  // Use RdnSource enum for consistency
    mutable const void* rdn_cpu_src_ptr_ = nullptr;
    mutable uint64_t rdn_cpu_src_epoch_ = 0;
    mutable int64_t rdn_cpu_nz_ = 0;

    // PERF FIX 2025-12-22: Dtype-specific cast caches to avoid repeated allocation in AMP.
    // Base cache is always float32. When a different dtype is requested, we cache the cast result
    // to avoid creating a new tensor on every call.
    // PERF FIX 2025-12-22: Also track epoch and nz to detect in-place updates and size changes.
    mutable torch::Tensor rdnw_dev_casted_;  // Casted GPU rdnw (non-float32 dtype)
    mutable torch::ScalarType rdnw_dev_casted_dtype_{torch::kFloat32};
    mutable const void* rdnw_dev_casted_base_ptr_ = nullptr;  // data_ptr of base cache when cast was made
    mutable uint64_t rdnw_dev_casted_src_epoch_ = 0;  // epoch of base cache when cast was made
    mutable int64_t rdnw_dev_casted_nz_ = 0;  // nz of base cache when cast was made
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    // Different GPUs can have same pointer value, causing wrong cast cache reuse.
    mutable int rdnw_dev_casted_device_index_ = -1;  // -1 = uninitialized/CPU
    mutable torch::Tensor rdnw_cpu_casted_;  // Casted CPU rdnw (non-float32 dtype)
    mutable torch::ScalarType rdnw_cpu_casted_dtype_{torch::kFloat32};
    mutable const void* rdnw_cpu_casted_base_ptr_ = nullptr;
    mutable uint64_t rdnw_cpu_casted_src_epoch_ = 0;
    mutable int64_t rdnw_cpu_casted_nz_ = 0;
    mutable torch::Tensor rdn_dev_casted_;   // Casted GPU rdn (non-float32 dtype)
    mutable torch::ScalarType rdn_dev_casted_dtype_{torch::kFloat32};
    mutable const void* rdn_dev_casted_base_ptr_ = nullptr;
    mutable uint64_t rdn_dev_casted_src_epoch_ = 0;
    mutable int64_t rdn_dev_casted_nz_ = 0;
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    mutable int rdn_dev_casted_device_index_ = -1;
    mutable torch::Tensor rdn_cpu_casted_;   // Casted CPU rdn (non-float32 dtype)
    mutable torch::ScalarType rdn_cpu_casted_dtype_{torch::kFloat32};
    mutable const void* rdn_cpu_casted_base_ptr_ = nullptr;
    mutable uint64_t rdn_cpu_casted_src_epoch_ = 0;
    mutable int64_t rdn_cpu_casted_nz_ = 0;

    // PARITY FIX 2025-12-21: Cache for ph_base max value to avoid CUDA sync in fallback path.
    // Used in getRdnwTensor fallback to compute ztop. Cached based on ph_base ptr/signature.
    // PERF FIX 2025-12-23: Separate CPU/GPU caches to avoid thrashing when alternating devices.
    // PERF FIX 2025-12-23: Track numel to detect size changes (ptr may stay same on resize).
    // PERF FIX 2025-12-23: Track device index for multi-GPU safety.
    mutable float ph_base_max_cached_cpu_ = 0.0f;
    mutable const void* ph_base_max_ptr_cpu_ = nullptr;
    mutable int64_t ph_base_numel_cpu_ = 0;
    mutable float ph_base_max_cached_gpu_ = 0.0f;
    mutable const void* ph_base_max_ptr_gpu_ = nullptr;
    mutable int64_t ph_base_numel_gpu_ = 0;
    mutable int16_t ph_base_device_idx_gpu_ = -1;  // -1 = uninitialized, int16 for large GPU clusters
    // PERF FIX 2025-12-22: Separate ztop-specific signature (max_val + numel only) for fallback cache.
    // Full signature includes first/last/max, but ztop only depends on max. Separating reduces
    // unnecessary cache invalidation when first/last change but max doesn't.
    mutable uint64_t ph_base_ztop_sig_cpu_ = 0;  // max_val+numel hash for CPU fallback
    mutable uint64_t ph_base_ztop_sig_gpu_ = 0;  // max_val+numel hash for GPU fallback
    // PARITY FIX 2025-12-23: Track last g value to detect g changes immediately.
    // Without this, g changes are only detected at CUDA_SAMPLE_PERIOD or CPU_PHBASE_MAX_PERIOD.
    mutable float last_g_val_cpu_ = 0.0f;
    mutable float last_g_val_gpu_ = 0.0f;
    // PARITY FIX 2025-12-24: Track last z_top_min/z_top_default to detect config changes immediately.
    // Without this, config changes on CUDA path wait for periodic sample.
    mutable float last_z_top_min_cpu_ = 0.0f;
    mutable float last_z_top_min_gpu_ = 0.0f;
    mutable float last_z_top_default_cpu_ = 0.0f;
    mutable float last_z_top_default_gpu_ = 0.0f;

    // PARITY FIX 2025-12-21: Cache for CFL rdnw abs max to avoid per-step CUDA sync.
    // Used in CFL calculation (check_cfl). Invalidated when rdnw source changes.
    // For Fallback source (type 0), also track ph_base signature to detect in-place updates.
    mutable float rdnw_abs_max_cached_ = 0.0f;
    mutable int rdnw_abs_max_src_type_ = 0;
    mutable const void* rdnw_abs_max_src_ptr_ = nullptr;
    mutable uint64_t rdnw_abs_max_src_epoch_ = 0;
    mutable int64_t rdnw_abs_max_nz_ = 0;
    mutable uint64_t rdnw_abs_max_fallback_ph_sig_ = 0;  // ph_base signature when cache was built
    // PARITY FIX 2025-12-25: Track device index to detect multi-GPU cache mismatch.
    mutable int rdnw_abs_max_dev_index_ = -1;

    // 3D vertical metrics for terrain-following coordinates (computed from geopotential)
    // PARITY FIX 2025-12-09: Required for proper horizontal diffusion scaling
    // rdz_3d_ = 2/(z_w[k+1]-z_w[k-1]) at w-levels, used in W diffusion
    // rdzw_3d_ = 1/(z_w[k+1]-z_w[k]) at mass levels, used in U/V diffusion
    torch::Tensor rdz_3d_;    // [ny, nz_w, nx] inverse physical layer thickness at w-levels
    torch::Tensor rdzw_3d_;   // [ny, nz, nx] inverse physical layer thickness at mass levels

    // PARITY FIX 2025-12-09: Terrain slope fields for horizontal diffusion corrections
    // These are ∂z/∂x and ∂z/∂y in terrain-following coordinates
    // Required by Fortran horizontal_diffusion_u/v/w_2 for terrain slope corrections
    // zx = ∂z/∂x at mass points (3D field varying with height due to coordinate transform)
    // zy = ∂z/∂y at mass points (3D field varying with height due to coordinate transform)
    torch::Tensor zx_;        // [ny, nz_w, nx] terrain slope in x-direction
    torch::Tensor zy_;        // [ny, nz_w, nx] terrain slope in y-direction

    // Map scale factors (stored for use in computations)
    // PARITY FIX 2025-12-19: Keep CPU originals (zero-copy from WRF) separate from GPU copies.
    // This allows WRF in-place updates to CPU arrays to be detected via epoch.
    torch::Tensor msftx_cpu_;  // CPU zero-copy from WRF, tracks in-place updates
    torch::Tensor msfty_cpu_;
    torch::Tensor msfux_cpu_;  // PARITY FIX 2025-12-19: Added for compute_defor13/23 consistency
    torch::Tensor msfuy_cpu_;
    torch::Tensor msfvx_cpu_;
    torch::Tensor msfvy_cpu_;  // PARITY FIX 2025-12-19: Added for compute_defor13/23 consistency
    uint64_t msf_epoch_ = 0;        // Incremented when CPU map factors are refreshed
    uint64_t msf_epoch_cached_ = 0; // Last epoch when GPU copies were made
    // PARITY FIX 2025-12-19: Signature-based change detection for map factors
    // Avoids unconditional epoch increment; only increment when values actually change
    float msf_signature_ = 0.0f;    // Sample-based signature (corner values sum)
    torch::Tensor msftx_;  // Map factor at mass points in x-direction (device-aligned)
    torch::Tensor msfty_;  // Map factor at mass points in y-direction (device-aligned)
    torch::Tensor msfux_;  // Map factor at u-points in x-direction (device-aligned)
    torch::Tensor msfuy_;  // Map factor at u-points in y-direction (device-aligned)
    torch::Tensor msfvx_;  // Map factor at v-points in x-direction (device-aligned)
    torch::Tensor msfvy_;  // Map factor at v-points in y-direction (device-aligned)
    
    // Physical constants - match WRF values exactly
    static constexpr float g_ = 9.81f;
    static constexpr float cp_ = 1004.5f;    // WRF: 7.*r_d/2. = 1004.5
    static constexpr float cv_ = 717.5f;     // WRF: cp - r_d = 717.5
    static constexpr float rd_ = 287.0f;     // WRF: r_d = 287.0
    static constexpr float p0_ = 1.0e5f;     // WRF: p1000mb = 100000.
    
    // Reference state (will be set from WRF)
    torch::Tensor p_base_;     // Base state pressure (3D)
    torch::Tensor th_base_;    // Base state potential temperature (3D)
    torch::Tensor rho_base_;   // Base state density (3D)
    torch::Tensor mu_base_;    // Base state column mass (2D)
    torch::Tensor ph_base_;    // Base state geopotential (3D, w-staggered)

    // v19.1: Cached CPU copies for physics scaling (avoid per-stage D2H transfers).
    // Invalidated in setBaseState and when dtype or grid dims change.
    torch::Tensor ph_base_scaling_cpu_;   // |ph_base_|.clamp_min(100).flatten(), CPU
    torch::Tensor th_base_scaling_cpu_;   // |th_base_|.clamp_min(100).flatten(), CPU
    bool scaling_cpu_cache_valid_ = false;
    torch::Dtype scaling_cpu_cache_dtype_ = torch::kFloat32;

    torch::Tensor u_base_;     // Base state u-wind profile (1D, nz levels)
    torch::Tensor v_base_;     // Base state v-wind profile (1D, nz levels)

    // Reference state for advection (to avoid Newton iteration feedback)
    torch::Tensor U_ref_stage_;  // State at beginning of SDIRK stage for advection terms
    // SPLIT-EXPLICIT D2 (2026-07-11): WRF's vertical-advection flux OMEGA (mu*eta_dot from the
    // calc_ww_cp recurrence) for the evaluation state, in PACKED memory dims {ny,nz_w,nx}. Set
    // by the split driver around each coupled K_exp call; when defined (and the coupled-export
    // flag is on) computeUnifiedRHS uses it as rom instead of the legacy mu*w form. Undefined
    // on the baseline path => byte-identical baseline.
    torch::Tensor split_omega_ww_;
    torch::Tensor k1_prev_;     // Latest stage-1 k1 cache (timestep n)
    torch::Tensor k1_prev2_;    // One-step older stage-1 k1 cache (timestep n-1)
    int prev_effective_split_mode_ = -1;  // Track mode changes to invalidate k1_prev_

    // v20.14r27e: Per-stage convergence tracking for inter-stage gating
    bool last_stage_converged_ = true;
    float last_stage_residual_ = 0.0f;
    float last_stage_rel_R_full_ = 0.0f;  // from RESIDUAL_REEVAL: floored ratio (K_floor for gate stability)
    float last_stage_rel_R_full_raw_ = 0.0f; // v20.14r41: raw ratio (no K_floor) for post-damp trigger
    float last_stage_R_full_norm_ = 0.0f; // v20.14r27v: absolute ||R_full|| for post-damp comparison
    float last_stage_wrms_norm_ = 0.0f;   // P1: WRMS residual norm using stage state as reference
    float last_stage_wrms_growth_ = 0.0f; // P1: WRMS(R_k) / WRMS(R_0), production stage-gate metric
    float last_stage_initial_R0_ = 0.0f;  // v20.14r39: Newton's initial unscaled L2 ||R_0|| for diagnostics
    int progress_low_streak_ = 0;         // v20.14r64: consecutive low-progress step counter

    // PR 9E (diagnosis-only, stage_operand_diag): RAW L2 diagnostics carried
    // back from the most recent implicit stage solve (-1 when the flag is off).
    // Never affect the solve; consumed only by the stage-operand history summary.
    float last_stage_fast_rhs_norm_ = -1.0f; // ||F_fast(U_eval_final)|| of last stage solve
    float last_stage_defect_l2_raw_ = -1.0f; // ||K_final - F_fast(U_eval_final)|| of last stage solve
    // PR 9F (diagnosis-only): the coherent {K, F, R} defect triple + returned stage
    // value of the most recent implicit stage solve (point==Unobserved when the flag
    // is off or the stage was explicit). Consumed by the record-stage summary, which
    // fails closed (DEFECT_UNOBSERVED) unless K equals the returned stage value.
    wrf::sdirk3::StageDefectTensorSnapshot last_stage_defect_tensor_;
    long long stage_operand_diag_step_ = 0;  // monotonic diag step label (advanced only when flag on)

    // Last step outcome for ABI-side fail-closed checks.
    int last_step_outcome_code_ = static_cast<int>(wrf::sdirk3::StepOutcomeCode::OK_ADVANCED);
    bool last_step_final_update_aborted_ = false;
    float last_step_progress_ratio_ = 0.0f;
    bool last_step_progress_ratio_valid_ = false;

    // v20.14r39: UV fraction warmup ramp state
    bool uv_vfrac_warmed_up_ = false;   // Once true, stays at target for rest of run
    float uv_vfrac_target_ = -1.0f;    // v20.14r42: Set in constructor from config (before any warmup)
    int uv_vfrac_warmup_ts_ = 0;       // Timesteps completed at warmup value
    float effective_uv_vfrac_ = -1.0f;  // v20.14r42: Local effective value (avoids global config mutation)
    
    // Dynamic diffusion coefficients (from physics)
    torch::Tensor Kh_mom_;     // Horizontal eddy viscosity for momentum (3D)
    torch::Tensor Kv_mom_;     // Vertical eddy viscosity for momentum (3D, w-staggered)
    torch::Tensor Kh_scalar_;  // Horizontal diffusivity for scalars (3D)
    torch::Tensor Kv_scalar_;  // Vertical diffusivity for scalars (3D, w-staggered)
    
    // Vertical coordinate coefficients from WRF
    torch::Tensor c1f_;        // Terrain-following coefficient at w-levels
    torch::Tensor c2f_;        // Terrain-following coefficient at w-levels
    // PERF FIX 2025-12-24: CPU cache for c1f_/c2f_ to avoid repeated GPU→CPU copies.
    // Updated when c1f_/c2f_ are set or moved to different device.
    // PARITY FIX 2025-12-25: Track pointer and 3-point signature to detect in-place updates
    // when WRF updates from_blob buffer without changing pointer.
    mutable torch::Tensor c1f_cpu_;
    mutable torch::Tensor c2f_cpu_;
    mutable bool c1f_c2f_cpu_valid_ = false;
    mutable const void* c1f_cached_ptr_ = nullptr;
    mutable const void* c2f_cached_ptr_ = nullptr;
    // PARITY FIX 2025-12-25: Track device index to detect tensor migration between GPUs.
    // Pointer alone can't detect device change (different GPU may have same address).
    mutable int c1f_cached_device_index_ = -1;  // -1 = CPU, 0+ = CUDA device index
    mutable int c2f_cached_device_index_ = -1;
    // PERF FIX 2025-12-25: Signature arrays support 3 or 5 point modes
    // 3-point: [first, mid, last]; 5-point: [first, 1/4, mid, 3/4, last]
    mutable float c1f_sig_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    mutable float c2f_sig_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // Cached index tensors for signature sampling - avoids repeated allocation
    mutable torch::Tensor c1f_sig_indices_;
    mutable torch::Tensor c2f_sig_indices_;
    mutable int64_t c1f_sig_indices_n_ = 0;  // cached c1f_.numel()
    mutable int64_t c2f_sig_indices_n_ = 0;  // cached c2f_.numel()
    mutable int c1f_c2f_sig_points_ = 0;     // cached signature point count
    mutable int c1f_c2f_verify_counter_ = 0; // counter for full_verify_interval
    mutable int c1f_c2f_sig_call_counter_ = 0; // counter for signature_period (skip GPU→CPU transfer)
    // PARITY FIX 2025-12-25: Track last config values to detect runtime changes
    mutable int last_c1f_c2f_config_sig_points_ = 0;
    mutable int last_c1f_c2f_config_period_ = 0;
    mutable int last_c1f_c2f_config_verify_interval_ = -1;  // -1 means not initialized
    // PARITY FIX 2025-12-25: Per-instance warning flag for c1f/c2f device mismatch
    mutable bool warned_c1f_c2f_device_mismatch_ = false;
    // PARITY FIX 2025-12-25: Per-instance warning flag for c1f/c2f fallback zeros
    mutable bool warned_c1f_c2f_fallback_zeros_ = false;
    torch::Tensor c1h_;        // Terrain-following coefficient at mass levels
    torch::Tensor c2h_;        // Terrain-following coefficient at mass levels
    
    // Coriolis parameters (spatially varying)
    torch::Tensor f_;          // 2*Omega*sin(lat) at mass points (2D)
    torch::Tensor e_;          // 2*Omega*cos(lat) at mass points (2D)
    torch::Tensor sina_;       // sin(alpha) for coordinate rotation (2D)
    torch::Tensor cosa_;       // cos(alpha) for coordinate rotation (2D)
    
    // Moisture correction factors
    torch::Tensor cqu_;        // Moisture correction for U-momentum pressure gradient (3D)
    torch::Tensor cqv_;        // Moisture correction for V-momentum pressure gradient (3D)
    torch::Tensor cqw_;        // Moisture correction for W-momentum pressure gradient (3D)
    
    // Pressure and temperature fields (from WRF)
    // NOTE (9F.D5, reviewer P0 #1): p_pert_ is set to grid%p in advanceZeroCopy but computeUnifiedRHS
    // REBINDS it to compute_pressure_hydrostatic (:15352), clobbering the WRF zero-copy view and
    // writing the hydrostatic field back to grid%p (:30458). Fixing this (immutable WRF view + a
    // separate p_hydrostatic_ member for the v/w PGF consumers at :16014/:16552/:19794/:19886/:20315,
    // + pressure as an explicit stage auxiliary state) is scoped to a dedicated PR — it changes the
    // SDIRK3 baseline write-back and needs full baseline re-validation.
    torch::Tensor p_pert_;     // Perturbation pressure from WRF (3D)
    torch::Tensor t_pert_;     // Perturbation potential temperature from WRF (3D)
    torch::Tensor al_;         // Inverse density (specific volume) from WRF (3D)
    
    // Non-hydrostatic pressure components
    torch::Tensor php_;        // Hydrostatic pressure component at mass points (3D)
    torch::Tensor dpn_u_;      // Vertical pressure gradient at u-points (3D, nz+1 levels)
    torch::Tensor dpn_v_;      // Vertical pressure gradient at v-points (3D, nz+1 levels)
    bool non_hydrostatic_ = false;  // Flag for non-hydrostatic mode
    
    // Vertical interpolation coefficients for non-hydrostatic dpn
    torch::Tensor fnm_;        // Vertical interpolation weight (current level) - GPU aligned
    torch::Tensor fnp_;        // Vertical interpolation weight (level below) - GPU aligned
    // PARITY FIX 2025-12-19: CPU originals for epoch-based refresh (same pattern as map factors)
    torch::Tensor fnm_cpu_;    // CPU original from WRF for change detection
    torch::Tensor fnp_cpu_;    // CPU original from WRF for change detection
    uint64_t fnm_epoch_ = 0;   // Incremented when fnm/fnp CPU values are updated
    uint64_t fnm_epoch_gpu_cached_ = 0;  // Last epoch when GPU copy was made
    // PARITY FIX 2025-12-19: Signature-based in-place change detection for fnm/fnp
    float fnm_signature_ = 0.0f;   // Sample-based signature (3 points: first, mid, last)
    const void* fnm_ptr_ = nullptr;  // Pointer to WRF's fnm array for change detection
    const void* fnp_ptr_ = nullptr;  // Pointer to WRF's fnp array for change detection
    float cf1_ = 0.5f;         // Bottom boundary coefficient 1
    float cf2_ = 0.0f;         // Bottom boundary coefficient 2
    float cf3_ = 0.0f;         // Bottom boundary coefficient 3
    float cfn_ = 1.0f;         // Top boundary coefficient 1 (cft1 in WRF)
    float cfn1_ = 0.0f;        // Top boundary coefficient 2 (cft2 in WRF)

    // SDIRK stage timestep (stored from advanceZeroCopy for use in RHS computation)
    float dt_stage_ = 0.0f;    // Current SDIRK stage timestep

    // External driving fields for boundary nudging
    torch::Tensor u_ext_;      // External u-field (staggered)
    torch::Tensor v_ext_;      // External v-field (staggered)
    torch::Tensor w_ext_;      // External w-field (staggered)
    torch::Tensor t_ext_;      // External t field (renamed from theta)
    torch::Tensor ph_ext_;     // External geopotential (staggered)
    torch::Tensor mu_ext_;     // External column mass (2D)
    
    // Zero-copy optimization: Pre-allocated tendency tensors
    torch::Tensor ru_tend_;     // Pre-allocated coupled U-momentum tendency (WRF naming)
    torch::Tensor rv_tend_;     // Pre-allocated coupled V-momentum tendency (WRF naming)
    torch::Tensor rw_tend_;     // Pre-allocated coupled W-momentum tendency (WRF naming)
    torch::Tensor t_tend_;      // Pre-allocated theta tendency (WRF naming: t_tend)
    torch::Tensor qv_tend_;     // Pre-allocated QV (water vapor) tendency (WRF naming: moist_tendf for P_QV)
    torch::Tensor qc_tend_;     // Pre-allocated QC (cloud water) tendency
    torch::Tensor qr_tend_;     // Pre-allocated QR (rain water) tendency
    torch::Tensor qi_tend_;     // Pre-allocated QI (cloud ice) tendency
    torch::Tensor qs_tend_;     // Pre-allocated QS (snow) tendency
    torch::Tensor qg_tend_;     // Pre-allocated QG (graupel) tendency
    torch::Tensor mu_tend_;     // Pre-allocated mu tendency
    torch::Tensor ph_tend_;     // Pre-allocated geopotential tendency
    int n_moist_ = 0;           // Number of active moist species from config

    // PARITY FIX 2025-12-13: Chemistry, tracer, and scalar tendency storage
    // WRF vertical_diffusion_s loops over all chem/tracer/scalar species
    std::vector<torch::Tensor> chem_tend_;    // Pre-allocated chem tendencies [n_chem]
    std::vector<torch::Tensor> tracer_tend_;  // Pre-allocated tracer tendencies [n_tracer]
    std::vector<torch::Tensor> scalar_tend_;  // Pre-allocated scalar tendencies [n_scalar]
    int n_chem_ = 0;            // Number of active chemistry species
    int n_tracer_ = 0;          // Number of active tracer species
    int n_scalar_ = 0;          // Number of active scalar species

    // Zero-copy optimization: Pre-allocated working memory
    torch::Tensor ru_adv_z_work_;    // Working memory for ru vertical advection (WRF naming)
    torch::Tensor rv_adv_z_work_;    // Working memory for rv vertical advection (WRF naming)
    torch::Tensor rw_adv_z_work_;    // Working memory for rw vertical advection (WRF naming)
    torch::Tensor t_adv_z_work_;     // Working memory for t vertical advection (WRF naming)
    torch::Tensor div_x_work_;       // Working memory for x-divergence
    torch::Tensor div_y_work_;       // Working memory for y-divergence
    torch::Tensor div_z_work_;       // Working memory for z-divergence
    
    // Zero-copy optimization: Pre-allocated packState tensors
    torch::Tensor pack_u_work_;      // Working memory for packState U
    torch::Tensor pack_v_work_;      // Working memory for packState V
    torch::Tensor pack_w_work_;      // Working memory for packState W
    torch::Tensor pack_ph_work_;     // Working memory for packState ph
    torch::Tensor pack_t_work_;      // Working memory for packState t
    torch::Tensor pack_mu_work_;     // Working memory for packState mu
    
    // Zero-copy optimization: Pre-allocated flux tensors
    torch::Tensor flux_w_u_work_;    // Working memory for U-momentum flux_w
    torch::Tensor flux_w_v_work_;    // Working memory for V-momentum flux_w
    
    // Zero-copy optimization: Pre-allocated physics tensors
    torch::Tensor c1f_3d_work_;      // Working memory for c1f expansion
    torch::Tensor c2f_3d_work_;      // Working memory for c2f expansion
    torch::Tensor dpx_work_;         // Working memory for pressure gradient X
    torch::Tensor dpn_diff_work_;    // Working memory for non-hydrostatic pressure
    torch::Tensor cqu_at_u_work_;    // Working memory for moisture correction U
    torch::Tensor cqv_at_v_work_;    // Working memory for moisture correction V
    torch::Tensor cqw_at_w_work_;    // Working memory for moisture correction W
    torch::Tensor alt_w_work_;       // Working memory for inverse density at W-points
    torch::Tensor t_w_work_;         // Working memory for potential temperature at W-points (WRF naming)
    
    // Zero-copy optimization: Pre-allocated Coriolis tensors
    torch::Tensor rv_at_u_work_;     // Working memory for V at U-points
    torch::Tensor f_at_u_work_;      // Working memory for Coriolis f at U
    torch::Tensor e_at_u_work_;      // Working memory for Coriolis e at U
    torch::Tensor cosa_at_u_work_;   // Working memory for cos(alpha) at U
    torch::Tensor rw_at_u_work_;     // Working memory for W at U-points
    
    // V-momentum Coriolis
    torch::Tensor ru_at_v_work_;     // Working memory for U at V-points
    torch::Tensor f_at_v_work_;      // Working memory for Coriolis f at V
    torch::Tensor e_at_v_work_;      // Working memory for Coriolis e at V
    torch::Tensor sina_at_v_work_;   // Working memory for sin(alpha) at V
    torch::Tensor rw_at_v_work_;     // Working memory for W at V-points
    torch::Tensor rw_at_v_mass_work_;// Working memory for W at V-mass-points
    
    // W-momentum Coriolis
    torch::Tensor ru_at_w_work_;     // Working memory for U at W-points
    torch::Tensor rv_at_w_work_;     // Working memory for V at W-points
    torch::Tensor u_at_w_work_;      // Working memory for u velocity at W-points
    torch::Tensor v_at_w_work_;      // Working memory for v velocity at W-points
    
    // Physics computation
    torch::Tensor omega_work_;       // Working memory for omega (vertical pressure velocity)

    // PERF FIX 2025-12-13: Cached buffers for vertical mixing to reduce allocations
    // These are lazily initialized with resize_().zero_() pattern
    torch::Tensor vmix_u_work_;      // Working memory for U vertical mixing result
    torch::Tensor vmix_v_work_;      // Working memory for V vertical mixing result
    torch::Tensor vmix_w_work_;      // Working memory for W vertical mixing result
    torch::Tensor vmix_s_work_;      // Working memory for scalar vertical mixing result
    torch::Tensor Kv_u_work_;        // Cached Kv interpolated to u-points
    torch::Tensor Kv_v_work_;        // Cached Kv interpolated to v-points
    torch::Tensor rho_u_work_;       // Cached rho interpolated to u-points
    torch::Tensor rho_v_work_;       // Cached rho interpolated to v-points
    torch::Tensor xkxavg_u_work_;    // Cached Kv*rho at vorticity points for U
    torch::Tensor xkxavg_v_work_;    // Cached Kv*rho at vorticity points for V
    torch::Tensor H3_work_;          // Cached H3 for W vertical mixing

    // PERF FIX 2025-12-13: Cached constant tensor views for vertical mixing
    // These are computed once and reused across calls
    torch::Tensor fnm_view_cache_;   // fnm reshaped to {1, nz, 1} for broadcasting
    torch::Tensor fnp_view_cache_;   // fnp reshaped to {1, nz, 1} for broadcasting
    torch::Tensor rdnw_view_cache_;  // rdnw reshaped to {1, nz, 1} for broadcasting
    torch::Tensor rdn_view_cache_;   // rdn reshaped to {1, nz, 1} for broadcasting
    int fnm_cache_size_ = 0;         // Size when cache was created (for invalidation)
    torch::ScalarType fnm_cache_dtype_ = torch::kFloat32;  // Dtype when cache was created (for mixed precision invalidation)
    int rdnw_cache_size_ = 0;        // Size when cache was created (for invalidation)
    // PARITY FIX 2025-12-19: Track rdnw source identity to detect tensor replacement
    const void* rdnw_cache_ptr_ = nullptr;
    torch::ScalarType rdnw_cache_dtype_ = torch::kFloat32;
    // PARITY FIX 2025-12-19: Track rdnw/rdn epochs for fnm/fnp recalculation on moving nests/regridding
    // fnm depends on rdnw, cfn/cfn1 depend on rdn. Recalculate when either changes.
    // PERF FIX 2025-12-22: Renamed to fnm_epoch_cached_rdnw/rdn to clarify what they track.
    // These now track max(vec_epoch, grid_epoch) to detect both vector AND grid_info_ changes.
    uint64_t fnm_epoch_cached_rdnw_ = 0;  // Last effective rdnw epoch when fnm/fnp were computed
    uint64_t fnm_epoch_cached_rdn_ = 0;   // Last effective rdn epoch when cfn/cfn1 were computed
    // PARITY FIX 2025-12-19: Track if fnm/fnp were provided by WRF (skip recalc in that case)
    bool fnm_fnp_from_wrf_ = false;  // True if WRF provided fnm/fnp pointers directly

    // =========================================================================
    // PERF FIX 2026-01-31: Cached c1f/c2f and rdnw device tensors for pressure gradient
    // Avoids repeated from_blob().to(device).reshape() calls per RHS evaluation.
    // Cache invalidated via invalidateCoeffDeviceCache() called from invalidateCaches().
    // Safety: .to(device) creates owned deep copy — cached tensors do NOT depend on
    // original Fortran pointer lifetime.
    // =========================================================================
    // JVP FIX 2026-02-01: Boundary mask for Omega (rom) — constant grid tensor
    // Interior=1, boundary (k=0, k=nz_w-1)=0. Used instead of index_put_ for AD compatibility.
    mutable torch::Tensor omega_bc_mask_;

    mutable torch::Tensor c1f_dev_u_cached_;   // [1, nk_pg, 1] on compute device
    mutable torch::Tensor c2f_dev_u_cached_;   // [1, nk_pg, 1]
    mutable torch::Tensor c1f_dev_v_cached_;   // [1, nk_pg_y, 1]
    mutable torch::Tensor c2f_dev_v_cached_;   // [1, nk_pg_y, 1]
    mutable torch::Tensor rdnw_dev_u_cached_;  // [1, dpn_nz, 1]
    mutable torch::Tensor rdnw_dev_v_cached_;  // [1, dpn_nz, 1]
    mutable torch::Device cached_coeff_device_{torch::kCPU};
    mutable int64_t cached_coeff_nk_pg_{0};
    mutable int64_t cached_coeff_nk_pg_y_{0};
    mutable int64_t cached_coeff_dpn_nz_{0};
    // Track source pointer identity for c1f/c2f to detect content changes
    mutable const void* cached_coeff_c1f_ptr_{nullptr};
    mutable const void* cached_coeff_rdnw_ptr_{nullptr};

    // =========================================================================
    // PERF FIX 2026-01-31: Cached c1h/c2h device tensors
    // c1h_/c2h_ are 1D hybrid coordinate coefficients called .to(device, dtype)
    // up to 6x per RHS. On GPU each call is a H2D copy. Cache key: device + dtype + source ptr.
    // =========================================================================
    mutable torch::Tensor c1h_dev_cached_;      // c1h_ on compute device
    mutable torch::Tensor c2h_dev_cached_;      // c2h_ on compute device
    mutable torch::Device c1h_cached_device_{torch::kCPU};
    mutable torch::ScalarType c1h_cached_dtype_{torch::kFloat32};
    mutable const void* c1h_cached_src_ptr_{nullptr};
    mutable const void* c2h_cached_src_ptr_{nullptr};

    void invalidateCoeffDeviceCache() const {
        c1f_dev_u_cached_ = torch::Tensor();
        c2f_dev_u_cached_ = torch::Tensor();
        c1f_dev_v_cached_ = torch::Tensor();
        c2f_dev_v_cached_ = torch::Tensor();
        rdnw_dev_u_cached_ = torch::Tensor();
        rdnw_dev_v_cached_ = torch::Tensor();
        cached_coeff_device_ = torch::kCPU;
        cached_coeff_nk_pg_ = 0;
        cached_coeff_nk_pg_y_ = 0;
        cached_coeff_dpn_nz_ = 0;
        cached_coeff_c1f_ptr_ = nullptr;
        cached_coeff_rdnw_ptr_ = nullptr;
        // c1f/c2f 1D device cache
        c1f_dev_cached_ = torch::Tensor();
        c2f_dev_cached_ = torch::Tensor();
        c1f_cached_device_ = torch::kCPU;
        c1f_cached_dtype_ = torch::kFloat32;
        c1f_cached_src_ptr_ = nullptr;
        c2f_cached_src_ptr_ = nullptr;
        // c1h/c2h cache
        c1h_dev_cached_ = torch::Tensor();
        c2h_dev_cached_ = torch::Tensor();
        c1h_cached_device_ = torch::kCPU;
        c1h_cached_dtype_ = torch::kFloat32;
        c1h_cached_src_ptr_ = nullptr;
        c2h_cached_src_ptr_ = nullptr;
    }

    // =========================================================================
    // PERF FIX 2026-01-31: Cached c1f/c2f 1D device tensors (general-purpose)
    // c1f_/c2f_ are called .to(device, dtype) in ph_tend, W-momentum, and Kh diffusion.
    // Separate from the c1f_dev_u/v_cached_ which are from_blob-based [1,nk,1] shaped.
    // =========================================================================
    mutable torch::Tensor c1f_dev_cached_;      // c1f_ on compute device (1D)
    mutable torch::Tensor c2f_dev_cached_;      // c2f_ on compute device (1D)
    mutable torch::Device c1f_cached_device_{torch::kCPU};
    mutable torch::ScalarType c1f_cached_dtype_{torch::kFloat32};
    mutable const void* c1f_cached_src_ptr_{nullptr};
    mutable const void* c2f_cached_src_ptr_{nullptr};

    const torch::Tensor& ensureC1fDevCached(const torch::Device& dev, torch::ScalarType dtype) const {
        if (!c1f_.defined()) return c1f_;
        const void* c1f_ptr = c1f_.data_ptr();
        const void* c2f_ptr = c2f_.defined() ? c2f_.data_ptr() : nullptr;
        if (!c1f_dev_cached_.defined()
            || c1f_cached_device_ != dev
            || c1f_cached_dtype_ != dtype
            || c1f_cached_src_ptr_ != c1f_ptr
            || c2f_cached_src_ptr_ != c2f_ptr) {
            torch::NoGradGuard no_grad;
            c1f_dev_cached_ = c1f_.to(dev, dtype, /*non_blocking=*/true);
            c2f_dev_cached_ = c2f_.defined()
                ? c2f_.to(dev, dtype, /*non_blocking=*/true)
                : torch::Tensor();
            c1f_cached_device_ = dev;
            c1f_cached_dtype_ = dtype;
            c1f_cached_src_ptr_ = c1f_ptr;
            c2f_cached_src_ptr_ = c2f_ptr;
        }
        return c1f_dev_cached_;
    }
    const torch::Tensor& ensureC2fDevCached(const torch::Device& dev, torch::ScalarType dtype) const {
        ensureC1fDevCached(dev, dtype);
        return c2f_dev_cached_;
    }

    // PERF FIX 2026-01-31: Lazy-init helper for c1h/c2h device cache.
    // Returns cached c1h on target device+dtype. Rebuilds if source changed.
    // c1h_ and c2h_ are constant hybrid coefficients — leaf tensors, no grad.
    const torch::Tensor& ensureC1hDevCached(const torch::Device& dev, torch::ScalarType dtype) const {
        // Guard: if c1h_/c2h_ not yet set, return the (empty) source directly
        if (!c1h_.defined()) return c1h_;
        const void* c1h_ptr = c1h_.data_ptr();
        const void* c2h_ptr = c2h_.defined() ? c2h_.data_ptr() : nullptr;
        if (!c1h_dev_cached_.defined()
            || c1h_cached_device_ != dev
            || c1h_cached_dtype_ != dtype
            || c1h_cached_src_ptr_ != c1h_ptr
            || c2h_cached_src_ptr_ != c2h_ptr) {
            torch::NoGradGuard no_grad;
            c1h_dev_cached_ = c1h_.to(dev, dtype, /*non_blocking=*/true);
            c2h_dev_cached_ = c2h_.defined()
                ? c2h_.to(dev, dtype, /*non_blocking=*/true)
                : torch::Tensor();
            c1h_cached_device_ = dev;
            c1h_cached_dtype_ = dtype;
            c1h_cached_src_ptr_ = c1h_ptr;
            c2h_cached_src_ptr_ = c2h_ptr;
        }
        return c1h_dev_cached_;
    }
    const torch::Tensor& ensureC2hDevCached(const torch::Device& dev, torch::ScalarType dtype) const {
        // ensureC1hDevCached rebuilds both c1h and c2h atomically
        ensureC1hDevCached(dev, dtype);
        return c2h_dev_cached_;
    }

    // =========================================================================
    // PERF FIX 2025-12-15: Cached map factors and rdnw for compute_3d_divergence
    // Avoids repeated to(device, dtype) calls and H2D copies per invocation.
    // Cache is invalidated when device, dtype, dimensions, or source data pointers change.
    // PARITY FIX 2025-12-16: Include data pointers in key to detect map factor updates
    // (e.g., moving nests, regridding, or refreshed map factors with same shape).
    // =========================================================================

    // PARITY FIX 2025-12-19: Track which rdnw source was used for cache key.
    // Only include relevant pointer in key to avoid false cache misses.
    enum class RdnwSource {
        GridRdnw,   // grid_info_->rdnw tensor (priority 1)
        VecRdnw,    // rdnw_ member vector (priority 2)
        GridDnw,    // grid_info_->dnw tensor fallback (priority 3)
        GridRdn,    // grid_info_->rdn tensor fallback (priority 4)
        Fallback    // Physical fallback computed internally (priority 5)
    };

    struct DivergenceCacheKey {
        torch::Device device{torch::kCPU};
        torch::ScalarType dtype{torch::kFloat32};
        int64_t ny_mass{0};
        int64_t nx_mass{0};
        int64_t nz{0};
        // PARITY FIX 2025-12-16: Track source tensor data pointers to detect content changes
        // If caller passes new tensor with same shape but different data, cache must invalidate.
        const void* msftx_data_ptr{nullptr};
        const void* msfty_data_ptr{nullptr};
        const void* msfuy_data_ptr{nullptr};
        const void* msfvx_data_ptr{nullptr};
        // PARITY FIX 2025-12-19: Track msfuy/msfvx shape to detect re-wrapping at same pointer.
        // WRF may reuse same memory address with different staggered grid dimensions.
        int64_t msfuy_ny{0}, msfuy_nx{0};
        int64_t msfvx_ny{0}, msfvx_nx{0};
        // PARITY FIX 2025-12-19: Track strides to detect different memory layouts at same ptr/shape.
        // Same pointer + same shape can have different strides (e.g., transposed view).
        int64_t msftx_stride0{0}, msftx_stride1{0};
        int64_t msfty_stride0{0}, msfty_stride1{0};
        int64_t msfuy_stride0{0}, msfuy_stride1{0};
        int64_t msfvx_stride0{0}, msfvx_stride1{0};
        // PARITY FIX 2025-12-19: Track which rdnw source is used and only relevant pointer.
        // Avoids false cache misses when unused rdn/dnw pointers change.
        RdnwSource rdnw_src{RdnwSource::Fallback};
        const void* rdnw_src_ptr{nullptr};  // Pointer for the chosen rdnw source
        // PARITY FIX 2025-12-19: Track rdnw version to detect coefficient updates.
        // rdnw_epoch_ increments when rdnw_ vector is updated, invalidating rdnw_broadcast.
        uint64_t rdnw_epoch{0};
        // PARITY FIX 2025-12-19: Track source-specific epochs for fallback sources.
        // When using GridRdn/GridDnw as rdnw source, their in-place changes must invalidate cache.
        uint64_t rdn_epoch{0};   // Used when rdnw_src == GridRdn
        uint64_t dnw_epoch{0};   // Used when rdnw_src == GridDnw
        // PARITY FIX 2025-12-20: Track ph_base for Fallback source.
        // Fallback uses ph_base to compute ztop = max(1000, max(ph_base)/g) for rdnw approximation.
        // PERF FIX 2025-12-23: ph_base_ptr is for debugging only, NOT used in comparison.
        // Invalidation is based on ph_base_signature (clamped ztop) and rdnw_epoch.
        const void* ph_base_ptr{nullptr};   // Debug only - NOT compared
        uint64_t ph_base_signature{0};      // Clamped ztop signature for invalidation
        // PARITY FIX 2025-12-19: Track map factor version for in-place value changes.
        // Even with same pointer/shape, WRF may modify values between timesteps.
        uint64_t msf_epoch{0};

        bool operator==(const DivergenceCacheKey& other) const {
            return device == other.device && dtype == other.dtype &&
                   ny_mass == other.ny_mass && nx_mass == other.nx_mass && nz == other.nz &&
                   msftx_data_ptr == other.msftx_data_ptr &&
                   msfty_data_ptr == other.msfty_data_ptr &&
                   msfuy_data_ptr == other.msfuy_data_ptr &&
                   msfvx_data_ptr == other.msfvx_data_ptr &&
                   msfuy_ny == other.msfuy_ny && msfuy_nx == other.msfuy_nx &&
                   msfvx_ny == other.msfvx_ny && msfvx_nx == other.msfvx_nx &&
                   msftx_stride0 == other.msftx_stride0 && msftx_stride1 == other.msftx_stride1 &&
                   msfty_stride0 == other.msfty_stride0 && msfty_stride1 == other.msfty_stride1 &&
                   msfuy_stride0 == other.msfuy_stride0 && msfuy_stride1 == other.msfuy_stride1 &&
                   msfvx_stride0 == other.msfvx_stride0 && msfvx_stride1 == other.msfvx_stride1 &&
                   rdnw_src == other.rdnw_src && rdnw_src_ptr == other.rdnw_src_ptr &&
                   rdnw_epoch == other.rdnw_epoch &&
                   rdn_epoch == other.rdn_epoch && dnw_epoch == other.dnw_epoch &&
                   // PERF FIX 2025-12-23: Removed ph_base_ptr from comparison (debug only).
                   // Invalidation uses ph_base_signature (clamped ztop) only.
                   ph_base_signature == other.ph_base_signature &&
                   msf_epoch == other.msf_epoch;
        }
        bool operator!=(const DivergenceCacheKey& other) const { return !(*this == other); }
    };

    struct DivergenceCache {
        DivergenceCacheKey key;
        torch::Tensor msftx_aligned;   // [ny_mass, nx_mass] on target device/dtype
        torch::Tensor msfty_aligned;   // [ny_mass, nx_mass] on target device/dtype
        torch::Tensor msfuy_aligned;   // [ny_u, nx_u] on target device/dtype (optional)
        torch::Tensor msfvx_aligned;   // [ny_v, nx_v] on target device/dtype (optional)
        // PERF FIX 2025-12-19: Pre-sized rdnw_broadcast [nz_common] replaces rdnw_ref.
        // Built once per cache key with exact size, eliminating per-call slice/pad/cat.
        // WRF-COMPLIANT REFACTOR 2025-12-25: rdnw > 0 (WRF standard, positive)
        torch::Tensor rdnw_broadcast;      // [nz_common] pre-sized, on target device/dtype (positive)
        // LEGACY: rdnw_broadcast_abs retained for backward compatibility but now equals rdnw_broadcast
        torch::Tensor rdnw_broadcast_abs;  // [nz_common] = rdnw_broadcast (already positive)
        bool valid{false};
    };
    mutable DivergenceCache div_cache_;  // Mutable for lazy initialization in const methods

    // Helper to compute/retrieve cached divergence tensors
    void ensureDivergenceCache(const torch::Device& device, torch::ScalarType dtype,
                               int64_t ny_mass, int64_t nx_mass, int64_t nz,
                               const torch::Tensor& msftx, const torch::Tensor& msfty,
                               const torch::Tensor& msfuy, const torch::Tensor& msfvx) const;

    // Explicit cache invalidation for map factor updates (moving nests, regridding)
    void invalidateDivergenceCache() const { div_cache_.valid = false; }

    // =========================================================================
    // PERF FIX 2025-12-15: Interior bounds helper for boundary reduction
    // Reusable across divergence, diffusion, and other routines.
    // =========================================================================
    struct InteriorBounds {
        int64_t i_start{0};   // West boundary (inclusive)
        int64_t i_end{0};     // East boundary (exclusive)
        int64_t j_start{0};   // South boundary (inclusive)
        int64_t j_end{0};     // North boundary (exclusive)
        int64_t ni() const { return i_end - i_start; }
        int64_t nj() const { return j_end - j_start; }
        bool empty() const { return ni() <= 0 || nj() <= 0; }
    };

    // Compute interior bounds based on boundary conditions and grid size
    InteriorBounds computeInteriorBounds(int64_t nx_mass, int64_t ny_mass) const;

    // Base state initialization flag
    bool base_state_initialized_ = false;
    
    // Halo exchange initialization flag
    bool halo_exchange_initialized_ = false;

    // MPI neighbor ranks for physical boundary detection (AD halo, Finding #45)
    // -1 = no neighbor (edge of Cartesian grid), >=0 = MPI neighbor rank
    // Populated from halo_exchange_get_neighbors() after halo_exchange_init()
    int neighbor_north_ = -1, neighbor_south_ = -1;
    int neighbor_east_ = -1,  neighbor_west_ = -1;
    // Valid flag — false until init + cache succeeds (Finding #57).
    // Co-invalidated with halo_exchange_initialized_ (Finding #58).
    bool neighbor_cache_valid_ = false;

    // v10: Epoch-based comm change detection
    uint64_t halo_exchange_epoch_ = 0;

    // Epoch-based comm change detection. Checks global epoch against cached.
    // If mismatch, calls invalidateHaloCache(). Implemented in .cpp.
    bool checkAndInvalidateOnEpochChange();

    // Post-init sync: updates proc grid, neighbors, epoch from global state.
    // Implemented in .cpp.
    void syncHaloStateAfterInit();

    // Single helper to prevent missed co-invalidation sites (Finding #64)
    void invalidateHaloCache() {
        halo_exchange_initialized_ = false;
        neighbor_cache_valid_ = false;
        // Reset full-halo dimensions so ghost masks get recomputed
        // on next AD halo activation (guard checks nj_mem_==0)
        nj_mem_ = 0;
        ni_mem_ = 0;
        j_off_ = 0;
        i_off_ = 0;
        // Release ghost masks (will be recomputed when nj_mem_==0 triggers init)
        ghost_mask_u_ = torch::Tensor();
        ghost_mask_v_ = torch::Tensor();
        ghost_mask_w_ = torch::Tensor();
        ghost_mask_ph_ = torch::Tensor();
        ghost_mask_t_ = torch::Tensor();
        ghost_mask_mu_ = torch::Tensor();
    }

    // Full-halo dimensions (computed from memory indices, for AD halo)
    int64_t nj_mem_ = 0, ni_mem_ = 0;
    int64_t j_off_ = 0, i_off_ = 0;

    // Per-field ghost masks for combineGhostAndInterior (AD halo)
    // Precomputed bool tensors: true=ghost, false=interior
    torch::Tensor ghost_mask_u_, ghost_mask_v_, ghost_mask_w_;
    torch::Tensor ghost_mask_ph_, ghost_mask_t_, ghost_mask_mu_;

    // MPI process grid information for parallel halo exchange
    // PARITY FIX 2025-12-16: Store actual process grid from WRF (module_dm.F)
    // These default to serial (1,1,0,0) but should be set via setMPIProcessInfo()
    // for correct halo sizing and neighbor indexing in multi-process runs.
    int nprocx_ = 1;   // Number of processes in X direction (default: serial)
    int nprocy_ = 1;   // Number of processes in Y direction (default: serial)
    int mypx_ = 0;     // This process's position in X (0-indexed, default: 0)
    int mypy_ = 0;     // This process's position in Y (0-indexed, default: 0)
    bool mpi_info_set_ = false;  // Flag indicating setMPIProcessInfo was called

    // WRF tile and domain indices
    // PR 9C.2: enabled W-damping runtime contract, resolved once per step
    // BEFORE any Newton callback is constructed (topology + boundary
    // authority; see resolve_wdamp_runtime_contract).
    wrf::sdirk3::WdampRuntimeContract wdamp_contract_{};
    int its_ = 1, ite_ = 1, jts_ = 1, jte_ = 1, kts_ = 1, kte_ = 1;  // Tile indices
    int ids_ = 1, ide_ = 1, jds_ = 1, jde_ = 1, kds_ = 1, kde_ = 1;  // Domain indices
    int ims_ = 1, ime_ = 1, jms_ = 1, jme_ = 1, kms_ = 1, kme_ = 1;  // Memory indices
    
    // SDIRK-3 L-stable parameter (root of 6γ³ - 18γ² + 9γ - 1 = 0)
    // v20.14 r50-fix: float→double for full 16-digit precision.
    // float32 gives ~7.2 digits → O(1e-8) order condition error per timestep.
    // With double, truncation is O(1e-16), negligible vs solver tolerance.
    static constexpr double gamma_ = 0.43586652150845899942;

    // SDIRK-3 Butcher tableau: Hairer-Wanner L-stable, 3rd order, stiffly accurate
    //
    //   γ       | γ        0        0
    //   (1+γ)/2 | a21      γ        0
    //   1       | b1       b2       γ
    //   --------|---------------------------
    //           | b1       b2       γ       (stiffly accurate: bi = a3i)
    //
    // FIX 2026-01-29: Previous coefficients (a21=1-2γ, b3=0) were only 1st order.
    // The correct H-W scheme satisfies 3rd order conditions and is L-stable.
    static constexpr double a21_ = 0.28206673924577050029;   // (1-γ)/2
    static constexpr double a22_ = gamma_;                    // = γ
    // b2 = (1 - 4γ + 2γ²)/(1-γ)
    static constexpr double a31_ = 1.20849664917310119469;    // = b1 (stiffly accurate)
    static constexpr double a32_ = -0.64436317068156019411;   // = b2 (stiffly accurate)
    static constexpr double a33_ = gamma_;                    // = γ

    // Weights: stiffly accurate (bi = a_{3,i}) ensures L-stability with R(∞) = 0
    static constexpr double b1_ = 1.20849664917310119469;
    static constexpr double b2_ = -0.64436317068156019411;
    static constexpr double b3_ = 0.43586652150845899942;     // = γ (NOT 0!)
    
    // Helper functions
    torch::Tensor packState(const float* u, const float* v, const float* w,
                           const float* ph, const float* theta, const float* mu,
                           int nx_u, int ny_v, int nz_w);

    // =========================================================================
    // GRAPH SIMPLIFICATION 2025-12-14: Utility helpers for fnm/fnp alignment,
    // 1D→3D broadcast, and padding to reduce code duplication and graph nodes
    // =========================================================================

    /**
     * @brief Align fnm_/fnp_ to target device/dtype and slice to runtime extent
     * @param k_start Start k-index (inclusive); default 0 for full range
     * @param k_end End k-index (exclusive); returned tensors have size (k_end - k_start)
     * @param dev Target device
     * @param dtype Target scalar type
     * @return Pair of (fnm_aligned, fnp_aligned) sliced to [k_start, k_end)
     *
     * Use k_start=0 when caller accesses fnm_aligned[k] directly in a k=1..nz loop.
     * Use k_start=1 when caller needs interior-only weights and will use fnm_aligned[k-1].
     *
     * This avoids per-iteration .item<float>() calls that break autograd and
     * cause host↔device copies on GPU.
     */
    std::pair<torch::Tensor, torch::Tensor> alignAndSliceFnmFnp(
        int64_t k_start, int64_t k_end, const torch::Device& dev, torch::ScalarType dtype) const;

    /**
     * @brief Expand 1D coefficient to 3D for broadcasting
     * @param coeff_1d 1D tensor of size >= nz
     * @param ny Target j-dimension
     * @param nz Target k-dimension (coefficient is sliced to this size)
     * @param nx Target i-dimension
     * @return Contiguous 3D tensor of shape [ny, nz, nx]
     *
     * Replaces k-loop + torch::stack patterns with single slice + broadcast.
     */
    torch::Tensor expand1dTo3d(const torch::Tensor& coeff_1d,
                               int64_t ny, int64_t nz, int64_t nx) const;

    /**
     * @brief Align fnm_/fnp_ and expand to 3D for vectorized hatavg
     * @param k_start Start k-index (inclusive)
     * @param k_end End k-index (exclusive)
     * @param ny Target j-dimension for broadcasting
     * @param nx Target i-dimension for broadcasting
     * @param dev Target device
     * @param dtype Target scalar type
     * @return Pair of 3D tensors (fnm_3d, fnp_3d) with shape [ny, k_end-k_start, nx]
     *
     * Combines alignAndSliceFnmFnp + view/expand to eliminate repeated view/expand in callers.
     * Enables fully vectorized hatavg computation without k-loops.
     */
    std::pair<torch::Tensor, torch::Tensor> alignAndSliceFnmFnp3d(
        int64_t k_start, int64_t k_end, int64_t ny, int64_t nx,
        const torch::Device& dev, torch::ScalarType dtype) const;

    void unpackState(const torch::Tensor& state,
                    float* u, float* v, float* w,
                    float* ph, float* theta, float* mu,
                    int nx_u, int ny_v, int nz_w);
    
    // IEVA-inspired vertical stability analysis (Shchepetkin 2015)
    // Computes column-wise vertical Courant numbers for adaptive explicit/implicit split
    // PARITY FIX 2025-12-10: Full Fortran parity with module_ieva_em.F WW_SPLIT
    // Inputs:
    //   ww: contravariant vertical velocity (omega, mass flux form) [ny, nz_w, nx]
    //   u, v: horizontal velocities for horizontal divergence [staggered]
    //   mut: column dry air mass perturbation (mu_d - mu_d_base) [ny, nx]
    //   c1f, c2f: vertical coordinate coefficients [nz_w]
    //   rdnw: reciprocal eta spacing [nz]
    //   rdx, rdy: reciprocal grid spacing
    // Returns: vertical_cfl [ny, nx] - max absolute Courant number per column
    torch::Tensor computeVerticalStabilityMetric(const torch::Tensor& ww,
                                                const torch::Tensor& u,
                                                const torch::Tensor& v,
                                                const torch::Tensor& mut,
                                                const torch::Tensor& c1f,
                                                const torch::Tensor& c2f,
                                                const torch::Tensor& rdnw,
                                                float rdx, float rdy,
                                                float dt) const;
    
    // Adaptive Newton solver parameters based on vertical stability
    // Tensor version - indexes into vertical_cfl at (j,i), requires GPU sync
    std::tuple<float, int> getAdaptiveNewtonParameters(const torch::Tensor& vertical_cfl,
                                                      int j, int i) const;
    // PERF FIX 2025-12-28: Direct float version - use when caller has pre-copied tensor to CPU
    // Example: auto vcfl_cpu = vertical_cfl.to(kCPU).contiguous();
    //          float cfl_val = vcfl_cpu.data_ptr<float>()[j * nx + i];
    //          auto [tol, iter] = getAdaptiveNewtonParameters(cfl_val);
    std::tuple<float, int> getAdaptiveNewtonParameters(float cfl_value) const;
    
    // State variable extraction and combination for unified RHS
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor>
    extractStateVariables(const torch::Tensor& state_vec);
    
    torch::Tensor combineStateVariables(const torch::Tensor& u, const torch::Tensor& v,
                                       const torch::Tensor& w, const torch::Tensor& ph,
                                       const torch::Tensor& theta, const torch::Tensor& mu);
    
    torch::Tensor solveImplicitStage(const torch::Tensor& U_stage,
                                     const torch::Tensor& F_phys,
                                     float dt, float a_ii, int stage,
                                     const torch::Tensor& K_prev = torch::Tensor(),
                                     const torch::Tensor& U_full_exchanged = torch::Tensor());

    torch::Tensor computeUnifiedRHS(const torch::Tensor& U, wrf::sdirk3::RhsMode mode = wrf::sdirk3::RhsMode::Full);

    // Full-halo RHS computation for AD halo path (Step 7c)
    // Requires neighbor_cache_valid_==true. Uses view-based field extraction
    // and slice-only policy for stencil operations.
    torch::Tensor computeUnifiedRHSFullHalo(const torch::Tensor& U_full,
                                             wrf::sdirk3::RhsMode mode = wrf::sdirk3::RhsMode::Full);

    // Merge ghost from exchanged state + current Newton interior (Step 6b)
    torch::Tensor combineGhostAndInterior(const torch::Tensor& U_full_exchanged,
                                           const torch::Tensor& current_interior);

    // Build ADHaloConfig from current solver state
    wrf::sdirk3::ADHaloConfig buildADHaloConfig() const;

    // New modular RHS computation using UnifiedRHS class (FINAL_DESIGN.md compliant)
    torch::Tensor computeUnifiedRHSModular(const torch::Tensor& U);
    void initializeUnifiedRHS();
    void updateUnifiedRHSBaseState();
    
    // Helper function to ensure expand uses actual tensor dimensions
    inline torch::Tensor expand_with_actual_dims(const torch::Tensor& tensor, 
                                                  const torch::Tensor& reference, 
                                                  int target_dim) {
        // Get actual dimension from reference tensor
        int actual_dim = reference.size(target_dim);
        
        // Create expand sizes with -1 for other dimensions
        std::vector<int64_t> expand_sizes;
        for (int i = 0; i < tensor.dim(); ++i) {
            if (i == 0) {  // Assuming we're expanding first dimension
                expand_sizes.push_back(actual_dim);
            } else {
                expand_sizes.push_back(-1);  // Keep other dimensions unchanged
            }
        }
        
        return tensor.expand(expand_sizes);
    }
    
    // Damping functions
    void applyRayleighDamping(torch::Tensor& rhs, const torch::Tensor& U);
    void applyDivergenceDamping(torch::Tensor& rhs, 
                               const torch::Tensor& u,
                               const torch::Tensor& v,
                               const torch::Tensor& w);
    
    // Damping control flags
    bool apply_rayleigh_damp_ = true;  // Upper boundary damping
    bool apply_div_damp_ = false;      // Divergence damping (optional)

    // Recompute local boundary flags from raw WRF config + MPI process position.
    // This prevents physical Y-boundary conditions from being applied on
    // interior MPI tile boundaries.
    void refreshProcessAwareBoundaryFlags_();
    
    // Boundary condition flags (to be set from WRF configuration)
    bool config_flags_periodic_x_ = false;
    bool config_flags_periodic_y_ = false;
    bool config_flags_symmetric_xs_ = false;
    bool config_flags_symmetric_xe_ = false;
    bool config_flags_symmetric_ys_ = false;
    bool config_flags_symmetric_ye_ = false;
    bool config_flags_open_xs_ = false;
    bool config_flags_open_xe_ = false;
    bool config_flags_open_ys_ = false;
    bool config_flags_open_ye_ = false;
    bool config_flags_specified_ = false;  // Specified lateral boundary conditions
    bool config_flags_nested_ = false;     // Nested domain boundary conditions
    bool config_flags_polar_ = false;      // Polar filtering flag (v20.14r21)
    bool mix_full_fields_ = true;          // Mix full fields (true) or perturbation from base state (false)

    // STOP-GATE (external review round 3):
    // split_forward_ran_: set when the split-explicit branch executes a forward step.
    // runAdjointReplay() checks it and refuses — the implicit-transpose replay has no
    // knowledge of the split forward map (composite VJP lands in Inc 7), so replaying
    // implicit checkpoints after a split forward would return a stale/wrong adjoint.
    bool split_forward_ran_ = false;
    // Raw U-staggered map-factor verification (external review rounds 3/3b/3c):
    // the periodic-x preprocessing repairs raw zero msfux/msfuy entries to a fallback
    // value, so the split guard cannot trust the (repaired) member tensors. These flags
    // capture the RAW state from the Fortran memory (true (ims:ime,jms:jme) layout:
    // tile offset its-ims/jts-jms, row stride mem_nx) BEFORE any repair or copy:
    //   msf_raw_u_checked_ : the raw scan actually ran (saw the raw arrays)
    //   msf_raw_u_unit_    : EACH array INDEPENDENTLY (msfux AND msfuy — never a joint
    //                        accumulator, which would let a wired array mask a
    //                        never-wired all-zero sibling) satisfied: at least one
    //                        nonzero entry, all entries finite, and every nonzero
    //                        entry ~1.0 (genuine unit-map with benign staggered/halo
    //                        zeros, e.g. em_b_wave). Predicate + regression matrix:
    //                        wrf_sdirk3_msf_raw_stats.h / test_msf_raw_stats.cpp.
    bool msf_raw_u_checked_ = false;
    bool msf_raw_u_unit_ = false;
    // Round 3f: whole-domain +1 stagger shape (nx_u==nx+1 && ny_v==ny+1),
    // evaluated PER CALL in the msf preflight from the ACTUAL ABI-passed
    // extents (safe_config) — not from init-time members, which can go stale
    // relative to what the caller passes. Consumed by the split-explicit
    // supported-config guard. Fail-closed default.
    bool split_stagger_ok_ = false;

    // Raw (global-domain) BC flags from Fortran namelist/config.
    // These are projected to process-local flags by refreshProcessAwareBoundaryFlags_().
    bool raw_flags_periodic_x_ = false;
    bool raw_flags_periodic_y_ = false;
    bool raw_flags_symmetric_xs_ = false;
    bool raw_flags_symmetric_xe_ = false;
    bool raw_flags_symmetric_ys_ = false;
    bool raw_flags_symmetric_ye_ = false;
    bool raw_flags_open_xs_ = false;
    bool raw_flags_open_xe_ = false;
    bool raw_flags_open_ys_ = false;
    bool raw_flags_open_ye_ = false;
    bool raw_flags_specified_ = false;
    bool raw_flags_nested_ = false;

    // Default halo size matching WRF memory domain structure
    // WRF uses 5 halo points: ims = ids - 5, ime = ide + 5
    int halo_size_ = 5;
    
    // Halo exchange helper function
    void performHaloExchange(torch::Tensor& U_stage);
    
    // Diagnostic helper functions
    void checkTensorHealth(const torch::Tensor& tensor, const std::string& name);
    float computeEnergy(const torch::Tensor& U);
    float computeMass(const torch::Tensor& U);
    float getCurrentMemoryUsage();
    
    // Spatial derivative helper functions for staggered grid
    torch::Tensor d_dx_mass(const torch::Tensor& f, float rdx);  // x-derivative at mass points
    torch::Tensor d_dx_u(const torch::Tensor& f, float rdx, int nx_u_target = -1);  // x-derivative at u-points
    torch::Tensor d_dy_v(const torch::Tensor& f, float rdy, int ny_v_target = -1);  // y-derivative at v-points
    torch::Tensor d_dz_w(const torch::Tensor& f, const torch::Tensor& rdnw);  // z-derivative at w-points
    
    // High-order flux functions for advection
    torch::Tensor flux5_upwind(const torch::Tensor& q_im3, const torch::Tensor& q_im2,
                              const torch::Tensor& q_im1, const torch::Tensor& q_i,
                              const torch::Tensor& q_ip1, const torch::Tensor& q_ip2,
                              const torch::Tensor& vel);
    torch::Tensor flux3_upwind(const torch::Tensor& q_im1, const torch::Tensor& q_i,
                              const torch::Tensor& q_ip1, const torch::Tensor& q_ip2,
                              const torch::Tensor& vel);
    torch::Tensor flux2_centered(const torch::Tensor& q_im1, const torch::Tensor& q_i,
                                const torch::Tensor& vel);
    
    // Advection-specific functions with upwind-biasing
    torch::Tensor advect_scalar_x(const torch::Tensor& f, const torch::Tensor& u, float rdx);
    torch::Tensor advect_scalar_y(const torch::Tensor& f, const torch::Tensor& v, float rdy);
    
    // Advection functions for already-staggered variables
    torch::Tensor advect_u_point_scalar_x(const torch::Tensor& f, const torch::Tensor& u, float rdx);
    torch::Tensor advect_v_point_scalar_y(const torch::Tensor& f, const torch::Tensor& v, float rdy);
    torch::Tensor advect_u_point_scalar_y(const torch::Tensor& f, const torch::Tensor& v, float rdy);
    torch::Tensor advect_v_point_scalar_x(const torch::Tensor& f, const torch::Tensor& u, float rdx);
    
    // Averaging/interpolation helpers
    torch::Tensor avg_x_to_u(const torch::Tensor& f, int nx_u_target = -1);  // Average from mass to u-points
    torch::Tensor avg_x_to_u_2d(const torch::Tensor& f, int nx_u_target = -1);  // 2D version for mu
    torch::Tensor avg_x_to_mass(const torch::Tensor& f);  // Average from u to mass-points
    torch::Tensor avg_y_to_v(const torch::Tensor& f, int ny_v_target = -1);  // Average from mass to v-points
    torch::Tensor avg_y_to_v_2d(const torch::Tensor& f, int ny_v_target = -1);  // 2D version for mu
    torch::Tensor avg_y_to_v_w_stagger(const torch::Tensor& f, int ny_v_target = -1);  // For w-staggered inputs
    torch::Tensor avg_y_to_u(const torch::Tensor& f);  // Average from mass to u-points in y
    torch::Tensor avg_z_to_w(const torch::Tensor& f);  // Average from mass to w-points
    torch::Tensor avg_mass_to_w(const torch::Tensor& f);  // Average from mass to w-points
    torch::Tensor avg_u_to_mass(const torch::Tensor& f);  // Average from u to mass-points
    torch::Tensor avg_v_to_mass(const torch::Tensor& f);  // Average from v to mass-points
    torch::Tensor avg_w_to_mass(const torch::Tensor& f);  // Average from w to mass-points
    
    // Diffusion and mixing helpers
    torch::Tensor compute_horizontal_diffusion(const torch::Tensor& f, float Kh, float rdx, float rdy);
    torch::Tensor compute_horizontal_diffusion_3d(const torch::Tensor& f, const torch::Tensor& Kh, float rdx, float rdy);
    // PARITY FIX 2025-12-07: Added msfvx and mut parameters for Fortran parity
    torch::Tensor compute_horizontal_diffusion_scalar_wrf(const torch::Tensor& var, const torch::Tensor& Kh,
                                                          float rdx, float rdy, const torch::Tensor& msftx,
                                                          const torch::Tensor& msfty,
                                                          const torch::Tensor& msfvx = torch::Tensor(),
                                                          const torch::Tensor& mut = torch::Tensor());
    // PARITY FIX 2025-12-07: Added muu/muv parameters for MUT weighting
    // PARITY FIX 2025-12-10: Added optional ph_full parameter for on-the-fly rdzw computation
    // ph_full = ph_pert + ph_base (total geopotential at w-levels, [ny, nz_w, nx])
    // When rdzw_3d_ is not pre-set, rdzw is computed from ph_full to ensure 3D metrics
    torch::Tensor compute_horizontal_diffusion_u_wrf(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
                                                     const torch::Tensor& Kh, float rdx, float rdy,
                                                     const torch::Tensor& msfux, const torch::Tensor& msfuy,
                                                     const torch::Tensor& msftx, const torch::Tensor& msfty,
                                                     const torch::Tensor& muu = torch::Tensor(),
                                                     const torch::Tensor& ph_full = torch::Tensor());
    torch::Tensor compute_horizontal_diffusion_v_wrf(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
                                                     const torch::Tensor& Kh, float rdx, float rdy,
                                                     const torch::Tensor& msfvx, const torch::Tensor& msfvy,
                                                     const torch::Tensor& msftx, const torch::Tensor& msfty,
                                                     const torch::Tensor& muv = torch::Tensor(),
                                                     const torch::Tensor& ph_full = torch::Tensor());
    // PARITY FIX 2025-12-08: Add MUT parameter for proper flux-form diffusion
    // PARITY FIX 2025-12-10: Add ph_full parameter for 3D rdz computation from total geopotential
    // PARITY FIX 2025-12-13: Add u, v for defor13/defor23-based tau31/tau32 computation
    // PARITY FIX 2025-12-13: Add rho for xkxavg = rhoavg * Km_avg density weighting
    torch::Tensor compute_horizontal_diffusion_w_wrf(const torch::Tensor& u, const torch::Tensor& v,
                                                     const torch::Tensor& w, const torch::Tensor& Kh,
                                                     const torch::Tensor& rho,  // density for xkxavg
                                                     float rdx, float rdy, const torch::Tensor& msftx,
                                                     const torch::Tensor& msfty,
                                                     const torch::Tensor& mu_full = torch::Tensor(),
                                                     const torch::Tensor& ph_full = torch::Tensor());
    torch::Tensor compute_biharmonic_diffusion(const torch::Tensor& f, float Kh4, float rdx, float rdy);
    torch::Tensor compute_vertical_mixing(const torch::Tensor& f, const torch::Tensor& Kv, const torch::Tensor& rdnw);
    torch::Tensor compute_vertical_mixing_u(const torch::Tensor& u, const torch::Tensor& w, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw, const torch::Tensor& rho);
    torch::Tensor compute_vertical_mixing_v(const torch::Tensor& v, const torch::Tensor& w, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw, const torch::Tensor& rho);
    torch::Tensor compute_vertical_mixing_w(const torch::Tensor& w, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw, const torch::Tensor& rho);
    // PERF FIX 2025-12-13: Overload accepting pre-computed defor33 to avoid duplicate computation
    // When horizontal diffusion already computed defor33 for tau33 stress, pass it here to save kernels
    torch::Tensor compute_vertical_mixing_w(const torch::Tensor& w, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw,
                                           const torch::Tensor& rho, const torch::Tensor& defor33_precomputed);
    torch::Tensor compute_vertical_mixing_scalar(const torch::Tensor& scalar, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw,
                                                const torch::Tensor& rho, const torch::Tensor& theta_full, const torch::Tensor& mu_full);
    // Overload with mix_full_fields and qv_base for QV perturbation mode (WRF vertical_diffusion_s parity)
    torch::Tensor compute_vertical_mixing_scalar(const torch::Tensor& scalar, const torch::Tensor& Kv_mass, const torch::Tensor& rdnw,
                                                const torch::Tensor& rho, const torch::Tensor& theta_full, const torch::Tensor& mu_full,
                                                bool is_qv, bool mix_full_fields, const torch::Tensor& qv_base);
    torch::Tensor compute_numerical_filter(const torch::Tensor& f, float alpha);
    
    // Deformation tensor components (for stress tensor calculation)
    torch::Tensor compute_defor11(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w, 
                                  float rdx, float rdy, const torch::Tensor& rdnw);
    torch::Tensor compute_defor12(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w, 
                                  float rdx, float rdy, const torch::Tensor& rdnw);
    torch::Tensor compute_defor22(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w, 
                                  float rdx, float rdy, const torch::Tensor& rdnw);
    torch::Tensor compute_defor13(const torch::Tensor& u, const torch::Tensor& w, 
                                  const torch::Tensor& rdx, const torch::Tensor& rdnw);
    torch::Tensor compute_defor23(const torch::Tensor& v, const torch::Tensor& w, 
                                  const torch::Tensor& rdy, const torch::Tensor& rdnw);
    torch::Tensor compute_defor33(const torch::Tensor& w, const torch::Tensor& rdnw);

    // 3D divergence for compressibility correction (vectorized GPU version)
    // Computes: ∇·V = mx*my*(∂(u/my)/∂x + ∂(v/mx)/∂y) + ∂w/∂z
    // PARITY FIX 2025-12-14: Use staggered map factors (msfuy, msfvx) for u/v components,
    //                        mass map factors (msftx, msfty) for combined weighting only
    // PARITY FIX 2025-12-16: Added keep_float32 parameter for AMP/fp16 stability
    // When keep_float32=true, returns divergence in float32 for downstream calculations
    // that benefit from higher precision (e.g., compressibility term multiplication).
    // When keep_float32=false (default), casts result back to input dtype for compatibility.
    torch::Tensor compute_3d_divergence(const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
                                        const torch::Tensor& msftx, const torch::Tensor& msfty,
                                        const torch::Tensor& msfuy, const torch::Tensor& msfvx,
                                        float rdx, float rdy, bool keep_float32 = false);

    // Stress tensor based vertical diffusion (physics-accurate)
    torch::Tensor compute_vertical_diffusion_u_stress(const torch::Tensor& u, const torch::Tensor& defor13,
                                                     const torch::Tensor& Kv_mom, const torch::Tensor& rho,
                                                     const torch::Tensor& rdnw);
    torch::Tensor compute_vertical_diffusion_v_stress(const torch::Tensor& v, const torch::Tensor& defor23,
                                                     const torch::Tensor& Kv_mom, const torch::Tensor& rho,
                                                     const torch::Tensor& rdnw);
    torch::Tensor compute_vertical_diffusion_w_stress(const torch::Tensor& w, const torch::Tensor& defor33,
                                                     const torch::Tensor& Kv_mom, const torch::Tensor& rho,
                                                     const torch::Tensor& rdnw);
    
    // Set dynamic diffusion coefficients from physics
    void setDiffusionCoefficients(const float* Kh_mom, const float* Kv_mom,
                                 const float* Kh_scalar, const float* Kv_scalar);
    
    // Set vertical coordinate coefficients from WRF
    void setVerticalCoordinateCoefficients(const float* c1f, const float* c2f,
                                          const float* c1h, const float* c2h);
    
    // Set Coriolis parameters from WRF
    void setCoriolisParameters(const float* f, const float* e,
                              const float* sina, const float* cosa);
    
    // Boundary condition handling
    void applyLateralBoundaryConditions(torch::Tensor& u_tend, torch::Tensor& v_tend, 
                                       torch::Tensor& w_tend, torch::Tensor& theta_tend,
                                       const torch::Tensor& u, const torch::Tensor& v,
                                       const torch::Tensor& w, const torch::Tensor& theta);
    void applyTopBottomBoundaryConditions(torch::Tensor& w_tend, torch::Tensor& theta_tend,
                                         const torch::Tensor& w, const torch::Tensor& theta);
    
    // Autograd-compatible versions that return new tensors instead of modifying by reference
    std::tuple<torch::Tensor, torch::Tensor> applyTopBottomBoundaryConditionsAutograd(
                                         const torch::Tensor& w_tend, const torch::Tensor& theta_tend,
                                         const torch::Tensor& w, const torch::Tensor& theta);
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor> 
        applyLateralBoundaryConditionsAutograd(
                                       const torch::Tensor& u_tend, const torch::Tensor& v_tend, 
                                       const torch::Tensor& w_tend, const torch::Tensor& theta_tend,
                                       const torch::Tensor& u, const torch::Tensor& v,
                                       const torch::Tensor& w, const torch::Tensor& theta);
    
    // Set external driving fields for boundary nudging
    void setExternalFields(const float* u_ext, const float* v_ext,
                          const float* w_ext, const float* theta_ext,
                          const float* ph_ext, const float* mu_ext);
    
    // CRITICAL: Move staggered dimensions BEFORE unique_ptr members to avoid corruption
    // These must be protected from memory overwrites
    // Staggered grid dimensions (stored for use in pack/unpack)
    int nx_u_ = 0;  // U-staggered dimension in X
    int ny_v_ = 0;  // V-staggered dimension in Y  
    int nz_w_ = 0;  // W-staggered dimension in Z
    
    // Add padding to protect critical members from memory corruption
    // This helps with potential struct alignment issues between Fortran/C++
    alignas(64) char padding_[64] = {0};  // 64-byte aligned padding
    
    // Tile boundary optimization (moved after critical members)
    std::unique_ptr<wrf::sdirk3::TileBoundaryOptimizer> boundary_optimizer_;
    std::unique_ptr<wrf::sdirk3::BoundaryFluxComputer> flux_computer_;
    
    // Non-hydrostatic helper functions
    torch::Tensor compute_php(const torch::Tensor& ph, const torch::Tensor& phb);
    torch::Tensor compute_dpn_u(const torch::Tensor& p_pert, int nx_u,
                               const torch::Tensor& fnm, const torch::Tensor& fnp,
                               float cf1, float cf2, float cf3);
    torch::Tensor compute_dpn_v(const torch::Tensor& p_pert, int ny_v,
                               const torch::Tensor& fnm, const torch::Tensor& fnp,
                               float cf1, float cf2, float cf3);
    void compute_nonhydrostatic_terms(const torch::Tensor& ph, const torch::Tensor& phb,
                                      const torch::Tensor& p_pert);
};

#endif // WRF_SDIRK3_TILE_UNIFIED_H
