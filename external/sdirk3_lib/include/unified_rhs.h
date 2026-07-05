#ifndef UNIFIED_RHS_H
#define UNIFIED_RHS_H

#include <cstdint>  // FIX 2025-01-11 Round70: For int8_t in per-solver logging state
#include <torch/torch.h>
#include "unified_state_vector.h"
#include <memory>

// FIX Round97-99: ABI version - single source of truth in libtorch_wrf/sdirk3/wrf_sdirk3_abi_version.h
// REQUIRED: Add to include path: -I${PROJECT_ROOT}/external/libtorch_wrf/sdirk3
#ifndef WRF_SDIRK3_ABI_VERSION_H
// ═══════════════════════════════════════════════════════════════════════════
// WARNING: Primary header not included! This fallback may be STALE.
// DO NOT EDIT this version - update wrf_sdirk3_abi_version.h instead.
// Fix: Add -I${PROJECT_ROOT}/external/libtorch_wrf/sdirk3 to include path
// ═══════════════════════════════════════════════════════════════════════════

// FIX Round99: Strict mode - #error in CI/debug builds when primary header missing
#ifdef WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER
#error "wrf_sdirk3_abi_version.h not found! Add -I libtorch_wrf/sdirk3 to include path. (Strict mode enabled)"
#endif

// FIX Round99: Cross-platform warning (MSVC uses #pragma message, GCC/Clang use #warning)
// FIX Round99: Once-only warning per build using WRF_SDIRK3_ABI_FALLBACK_WARNED
#ifndef WRF_SDIRK3_ABI_FALLBACK_WARNED
#define WRF_SDIRK3_ABI_FALLBACK_WARNED 1
#ifdef _MSC_VER
#pragma message("WARNING: wrf_sdirk3_abi_version.h not found - using fallback. Add libtorch_wrf/sdirk3 to include path!")
#else
#warning "wrf_sdirk3_abi_version.h not found - using fallback. Add libtorch_wrf/sdirk3 to include path!"
#endif
#endif

// FIX Round99/Round100: Define guard to prevent redefinition warnings if primary header included later
// NOTE: Once fallback is used, any subsequent #include of wrf_sdirk3_abi_version.h will be SKIPPED
//       due to this guard. This is intentional - first definition wins. Ensure consistent include order.
#define WRF_SDIRK3_ABI_VERSION_H
// FIX Round100: WRF_SDIRK3_ABI_VERSION_FALLBACK is COMPILE-TIME ONLY for diagnostics.
// WARNING: Do NOT use this macro for runtime decisions across multiple TUs - different TUs may have
//          different values depending on header include order. Use only within single TU or build scripts.
#define WRF_SDIRK3_ABI_VERSION_FALLBACK 1  // Mark that fallback was used (compile-time only!)
#define WRF_SDIRK3_GRIDINFO_ABI_VERSION 94  // DO NOT EDIT - sync with wrf_sdirk3_abi_version.h
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) \
    static_assert(WRF_SDIRK3_GRIDINFO_ABI_VERSION == expected, \
        "ABI mismatch! Add -I libtorch_wrf/sdirk3 to include path and rebuild.")
#endif

// FIX Round94/Round98/Round106: Verify ABI version consistency at compile time
WRF_SDIRK3_VERIFY_ABI_VERSION(94);  // Must match WRF_SDIRK3_GRIDINFO_ABI_VERSION

namespace wrf {
namespace sdirk3 {

// Forward declarations
class WRFGridInfo;
class PhysicsConfig;

// WRF grid information
class WRFGridInfo {
public:
    // Grid dimensions
    int nx, ny, nz;
    int its, ite, jts, jte, kts, kte;
    int ims, ime, jms, jme, kms, kme;
    
    // Grid spacing
    float dx, dy;
    // FIX 2025-01-11 Round59: Add double storage for FP64 precision paths
    // Synchronized with wrf_sdirk3_types.h WRFGridInfo struct
    double dx_fp64 = 0.0;  // Grid spacing in meters (double precision)
    double dy_fp64 = 0.0;  // Grid spacing in meters (double precision)
    // FIX 2025-01-11 Round63/Round64: Track explicit FP64 activation
    // Synchronized with wrf_sdirk3_types.h WRFGridInfo struct
    bool fp64_explicit = false;
    // FIX 2025-01-11 Round70/Round73: Per-solver logging state for multi-solver environments
    // Synchronized with wrf_sdirk3_types.h WRFSDIRKGridInfo struct
    int8_t last_fp64_transition_logged = -1;  // -1=none, 0=to_fp32, 1=to_fp64
    bool null_path_fp64_logged = false;
    bool warned_no_reference_device = false;  // Round73: Per-solver warning flag
    bool warned_dz_rdz_device_mismatch = false;  // FIX Round94/95: WARN_ONCE for dz/rdz device mismatch
    int16_t cached_reference_device = -1;     // Round73/91: Cached device type (-1=not cached, int16_t for consistency)
    int16_t cached_device_index = -1;         // Round89/90: Cached device index for multi-GPU (int16_t for >127 GPUs)
    torch::Tensor dz;      // Vertical spacing (varies with k)
    
    // Grid coefficients
    torch::Tensor rdx, rdy, rdz;     // Reciprocals
    torch::Tensor rdzw;              // Reciprocal spacing for w points
    torch::Tensor c1h, c2h;          // Hydrostatic coefficients
    torch::Tensor dnw, dn;           // Vertical grid coefficients
    torch::Tensor fnm, fnp;          // Vertical interpolation

    // FIX 2025-01-07 Batch41 Round15: Explicit kdim for 2D rdzw on cubic-like grids
    // See wrf_sdirk3_types.h for full documentation. Values:
    //   0 = [nz+1, nx] layout (WRF standard), 1 = [ny, nz+1] layout, -1 = auto-detect
    int rdzw_kdim = -1;

    // FIX 2025-01-08 Batch41 Round31: Explicit kdim for 2D rdz on cubic-like grids
    // Same semantics as rdzw_kdim but for mass-level rdz (nz elements):
    //   0 = [nz, nx] layout (WRF standard), 1 = [ny, nz] layout, -1 = auto-detect
    int rdz_kdim = -1;

    // Reference state
    torch::Tensor p_base, th_base;   // Base state pressure and theta
    torch::Tensor rho_base;          // Base state density
    torch::Tensor mub;               // Base state column mass
    
    // Physical constants
    float g = 9.81;                  // Gravity
    float cp = 1004.0;               // Specific heat
    float cv = 717.0;                // Cv
    float rd = 287.0;                // Gas constant
    float p0 = 1.0e5;                // Reference pressure
    float t0 = 300.0;                // Reference temperature
    float cs = 340.0;                // Sound speed
    
    // Coriolis parameters
    torch::Tensor f;                 // Coriolis parameter
    torch::Tensor e;                 // Map scale factors
    
    // Damping coefficients
    float khdif = 0.0;               // Horizontal diffusion
    float kvdif = 0.0;               // Vertical diffusion
    float kdamp = 0.0;               // Divergence damping
    float damp_mu = 0.0;             // External mode filter
    
    // Constructor
    WRFGridInfo() = default;

    // Initialize from WRF grid
    // =========================================================================
    // FIX 2025-01-07 Batch41 Round16: IMPORTANT NOTES FOR EXTERNAL CONSUMERS
    // FIX 2025-01-08 Batch41 Round32: Added rdz_kdim documentation
    //
    // This function initializes core grid fields but does NOT populate:
    //   - rdzw (reciprocal w-level spacing tensor)
    //   - rdzw_kdim (2D rdzw k-dimension config for w-levels, nz+1)
    //   - rdz_kdim (2D rdz k-dimension config for mass-levels, nz)
    //
    // For moisture advection and other physics that require rdzw:
    //   1. After calling initialize_from_fortran(), set rdzw manually:
    //        grid_info.rdzw = torch::from_blob(rdzw_ptr, {nz+1}, torch::kFloat32);
    //   2. For 2D rdzw on cubic-like grids (ny == nx == nz+1), set rdzw_kdim:
    //        grid_info.rdzw_kdim = 0;  // [nz+1, nx] layout (WRF standard)
    //        // or
    //        grid_info.rdzw_kdim = 1;  // [ny, nz+1] layout
    //
    // For buoyancy diffusion that uses rdz:
    //   1. rdz is typically set via initialize_from_fortran() from dz_
    //   2. For 2D rdz on cubic-like grids (ny == nx == nz), set rdz_kdim:
    //        grid_info.rdz_kdim = 0;   // [nz, nx] layout (WRF standard)
    //        // or
    //        grid_info.rdz_kdim = 1;   // [ny, nz] layout
    //
    // See wrf_sdirk3_types.h for complete rdzw_kdim/rdz_kdim documentation.
    // =========================================================================
    void initialize_from_fortran(
        int nx_, int ny_, int nz_,
        const float* dx_, const float* dy_, const float* dz_,
        const float* c1h_, const float* c2h_,
        const float* dnw_, const float* dn_,
        const float* p_base_, const float* th_base_,
        const float* mub_, const float* f_, const float* e_
    );
};

// Physics configuration
class PhysicsConfig {
public:
    bool enabled = true;
    bool pbl_physics = false;
    bool cu_physics = false;
    bool mp_physics = false;
    bool ra_physics = false;
    bool sf_physics = false;
    
    // Physics scheme indices
    int pbl_scheme = 0;
    int cu_scheme = 0;
    int mp_scheme = 0;
    int ra_scheme = 0;
    int sf_scheme = 0;
};

// Main RHS computation class
class UnifiedRHS : public torch::nn::Module {
private:
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<PhysicsConfig> physics_config_;
    
    // Cached tensors for efficiency
    torch::Tensor work_3d_;
    torch::Tensor work_2d_;
    
public:
    // Constructor
    UnifiedRHS(std::shared_ptr<WRFGridInfo> grid_info,
               std::shared_ptr<PhysicsConfig> physics_config);
    
    // Main RHS computation combining slow and fast terms
    torch::Tensor forward(const torch::Tensor& U);
    
    // Compute only fast terms for preconditioner
    torch::Tensor compute_fast_terms(const torch::Tensor& U);
    
    // Individual term computations
    void add_advection_terms(torch::Tensor& F, 
                            const torch::Tensor& u,
                            const torch::Tensor& v,
                            const torch::Tensor& w);
    
    void add_coriolis_terms(torch::Tensor& F,
                           const torch::Tensor& u,
                           const torch::Tensor& v);
    
    void add_pressure_gradient_slow(torch::Tensor& F,
                                   const torch::Tensor& theta,
                                   const torch::Tensor& phi);
    
    void add_acoustic_terms(torch::Tensor& F,
                           const torch::Tensor& rho,
                           const torch::Tensor& u,
                           const torch::Tensor& v,
                           const torch::Tensor& w,
                           const torch::Tensor& mu,
                           const torch::Tensor& phi);
    
    void add_buoyancy_terms(torch::Tensor& F,
                           const torch::Tensor& theta,
                           const torch::Tensor& rho);
    
    void add_pressure_gradient_fast(torch::Tensor& F,
                                   const torch::Tensor& phi);
    
    void add_divergence_damping(torch::Tensor& F,
                               const torch::Tensor& u,
                               const torch::Tensor& v,
                               const torch::Tensor& w);
    
    void add_external_mode_filter(torch::Tensor& F,
                                 const torch::Tensor& mu);
    
    void add_physics_tendencies(torch::Tensor& F,
                               const torch::Tensor& U);
    
private:
    // Helper functions for derivatives
    torch::Tensor ddx(const torch::Tensor& field);
    torch::Tensor ddy(const torch::Tensor& field);
    torch::Tensor ddz(const torch::Tensor& field);
    
    torch::Tensor divergence(const torch::Tensor& u,
                            const torch::Tensor& v,
                            const torch::Tensor& w);
    
    torch::Tensor gradient_x(const torch::Tensor& field);
    torch::Tensor gradient_y(const torch::Tensor& field);
    torch::Tensor gradient_z(const torch::Tensor& field);
    
    // Interpolation helpers
    torch::Tensor interp_u_to_mass(const torch::Tensor& u);
    torch::Tensor interp_v_to_mass(const torch::Tensor& v);
    torch::Tensor interp_w_to_mass(const torch::Tensor& w);
    
    torch::Tensor interp_mass_to_u(const torch::Tensor& field);
    torch::Tensor interp_mass_to_v(const torch::Tensor& field);
    torch::Tensor interp_mass_to_w(const torch::Tensor& field);
};

// Factory function to create RHS module
std::shared_ptr<UnifiedRHS> create_unified_rhs(
    const WRFGridInfo& grid_info,
    const PhysicsConfig& physics_config
);

// Tile-based RHS computation for efficiency
class TileRHS {
public:
    // Compute RHS for a single tile
    static void compute_tile_rhs(
        float* __restrict__ F,           // Output tendencies
        const float* __restrict__ U,     // State vector
        const TileInfo& tile,
        const WRFGridInfo& grid_info,
        const PhysicsConfig& physics_config
    );
    
private:
    // Tile-based term computations
    static void add_advection_tile(
        float* __restrict__ F,
        const float* __restrict__ u,
        const float* __restrict__ v,
        const float* __restrict__ w,
        const TileInfo& tile,
        const WRFGridInfo& grid_info
    );
    
    static void add_acoustic_tile(
        float* __restrict__ F,
        const float* __restrict__ rho,
        const float* __restrict__ u,
        const float* __restrict__ v,
        const float* __restrict__ w,
        const float* __restrict__ mu,
        const float* __restrict__ phi,
        const TileInfo& tile,
        const WRFGridInfo& grid_info
    );
    
    // Vectorized operations
    static inline void compute_divergence_vec(
        float* __restrict__ div,
        const float* __restrict__ u,
        const float* __restrict__ v,
        const float* __restrict__ w,
        int i, int k, int j,
        float rdx, float rdy, float rdz
    );
    
    static inline void compute_gradient_vec(
        float* __restrict__ grad_x,
        float* __restrict__ grad_y,
        float* __restrict__ grad_z,
        const float* __restrict__ field,
        int i, int k, int j,
        float rdx, float rdy, float rdz
    );
};

} // namespace sdirk3
} // namespace wrf

#endif // UNIFIED_RHS_H