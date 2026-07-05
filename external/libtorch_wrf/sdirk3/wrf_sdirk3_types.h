/**
 * @file wrf_sdirk3_types.h
 * @brief Common type definitions for WRF SDIRK3 implementation
 */

#ifndef WRF_SDIRK3_TYPES_H
#define WRF_SDIRK3_TYPES_H

#include <cstdint>  // FIX 2025-01-11 Round70: For int8_t in per-solver logging state
#include <climits>  // FIX Round107: For INT16_MAX in safe_device_index()
#include <iostream> // FIX Round107: For std::cerr in safe_device_index()
#include <torch/torch.h>

// FIX Round96/Round97: Single source of truth for ABI version
// See wrf_sdirk3_abi_version.h for version history and update instructions
#include "wrf_sdirk3_abi_version.h"

// FIX Round97/Round104: Verify ABI version - catches drift when types.h is included alone
WRF_SDIRK3_VERIFY_ABI_VERSION(94);

namespace wrf {
namespace sdirk3 {

// Full definition of WRFGridInfo is needed for inheritance
// This is a minimal definition to avoid circular dependency
struct WRFGridInfo {
    // Grid dimensions (C++ 0-based: actual tile size without halo)
    int nx, ny, nz;
    
    // Staggered grid dimensions
    int nx_u, ny_v, nz_w;
    
    // WRF index bounds (Fortran 1-based)
    // Tile indices (computational region)
    int its, ite, jts, jte, kts, kte;
    // Memory indices (includes halo)
    int ims, ime, jms, jme, kms, kme;
    // Domain indices (full domain)
    int ids, ide, jds, jde, kds, kde;
    
    // Grid spacing
    float dx, dy;
    // =========================================================================
    // ABI VERSION NOTE (FIX 2025-01-11 Round70/Round88):
    // The following fields were added in Round57-88 and change the struct layout:
    //   - dx_fp64, dy_fp64 (Round57)
    //   - fp64_explicit (Round63)
    //   - last_fp64_transition_logged, null_path_fp64_logged (Round70)
    //
    // WARNING: External modules compiled with older headers may have ABI mismatch.
    // If you encounter crashes or data corruption when using WRFSDIRKGridInfo across
    // library boundaries, ensure ALL dependent libraries are rebuilt with this header.
    //
    // Rebuild trigger: If WRFSDIRKGridInfo is passed by pointer or reference across
    // .so/.dylib boundaries, all libraries must use the same header version.
    //
    // FIX Round88: ABI DRIFT DETECTION
    // Use WRF_SDIRK3_GRIDINFO_ABI_VERSION macro to verify header compatibility:
    //   #if WRF_SDIRK3_GRIDINFO_ABI_VERSION != EXPECTED_VERSION
    //   #error "WRFGridInfo ABI version mismatch - rebuild required"
    //   #endif
    // Current version: 88 (increment when layout changes)
    // =========================================================================

    // FIX 2025-01-11 Round57: Add double storage for FP64 precision paths
    // When running FP64 simulations, use dx_fp64/dy_fp64 for full precision
    // Callers should set these alongside dx/dy for FP64 compatibility
    double dx_fp64 = 0.0;  // Grid spacing in meters (double precision)
    double dy_fp64 = 0.0;  // Grid spacing in meters (double precision)
    // FIX 2025-01-11 Round63: Track explicit FP64 activation via sdirk3_set_fp64_scalars()
    // When true, FP64 rdx/rdy tensors are created; when false, use FP32 even if dx_fp64 is set
    // This prevents float-derived config.rdx_fp64 from promoting downstream ops in FP32 runs
    bool fp64_explicit = false;

    // FIX 2025-01-11 Round70/Round73: Per-solver logging state for multi-solver environments
    // Global atomics/statics would cause one solver's log to suppress another's or data races.
    // These per-solver fields enable independent logging per solver instance.
    // Values: -1=none logged, 0=to_fp32 logged, 1=to_fp64 logged
    int8_t last_fp64_transition_logged = -1;
    bool null_path_fp64_logged = false;
    // FIX 2025-01-11 Round73: Per-solver warning for no reference device (was static bool with data race)
    // FIX Round126: WARNING POLICY DIFFERENCE
    // - warned_no_reference_device: Simple WARN_ONCE, resets immediately when reference available.
    //   This is intentional - "no reference" is a configuration issue that should be reported
    //   immediately when it recurs. Throttling would hide recurring problems.
    // - warned_dz_rdz_device_mismatch: Uses throttle counter (dz_rdz_warn_throttle_counter)
    //   because device oscillation can cause frequent state changes during normal operation.
    bool warned_no_reference_device = false;
    // FIX Round94: Per-solver WARN_ONCE for dz/rdz device mismatch
    // FIX Round121: THREAD SAFETY INVARIANT - These fields assume SINGLE-THREAD-PER-SOLVER
    // access pattern (WRF's one-solver-per-tile model). They are plain non-atomic types
    // for performance. If future changes introduce multi-threaded solver access (e.g.,
    // OpenMP parallelism INSIDE a single solver's timestep), these become data races:
    //   - warned_dz_rdz_device_mismatch: bool flag (race on read-modify-write)
    //   - dz_rdz_warn_throttle_counter: uint64_t counter (race on increment)
    // Migration path if multi-threaded solver access is introduced:
    //   (a) Convert to std::atomic<bool> / std::atomic<uint64_t>
    //   (b) OR protect with per-solver mutex (higher overhead)
    //   (c) OR accept racy statistics (if correctness not required)
    // Current assumption: Each solver instance is accessed by ONE thread at a time.
    bool warned_dz_rdz_device_mismatch = false;
    // FIX Round104: Per-solver throttle counter for dz/rdz mismatch warning
    // Moved from thread_local to per-solver to avoid cross-solver interference in
    // multi-solver environments. Each solver instance independently tracks its
    // warning throttle state. Reset to 0 when warning flag is cleared.
    // See Round121 THREAD SAFETY INVARIANT above for multi-threading considerations.
    uint64_t dz_rdz_warn_throttle_counter = 0;

    // FIX Round109: Per-solver unique generation ID for TLS cache validation.
    // Assigned from a global atomic counter at solver creation. TLS caches store
    // this ID and compare on cache hit. This allows targeted invalidation:
    // destroying solver A doesn't invalidate cache entries for solver B.
    // Value 0 = not initialized (will trigger slow path lookup).
    //
    // FIX Round114: IMMUTABILITY CONTRACT - This field MUST NOT be modified after
    // initial assignment during solver creation. The TLS cache system assumes this
    // value is stable for the solver's entire lifetime. Modifying it would cause:
    //   - TLS cache mismatches (cached ID != actual ID)
    //   - False cache invalidations or stale pointer use
    //   - Debug assertion failures / release-mode warnings
    // resetPerSolverState() intentionally does NOT reset this field - see comment there.
    uint64_t solver_unique_id = 0;

    // FIX 2025-01-11 Round73/Round75: Cached reference device to avoid repeated .defined() checks
    // Updated when dz or rdz tensors are set. -1 means not cached yet (need to compute).
    //
    // TYPE MAPPING (FIX Round75 - documented for type safety):
    //   -1 = not cached (compute on next access)
    //   Values >=0 map to c10::DeviceType enum (torch/c10/core/DeviceType.h):
    //     0 = c10::kCPU
    //     1 = c10::kCUDA
    //     2 = c10::kMKLDNN (rarely used)
    //     3 = c10::kOpenGL (rarely used)
    //     4 = c10::kOpenCL (rarely used)
    //     5 = c10::kIDEEP (rarely used)
    //     6 = c10::kHIP (AMD GPU)
    //     7 = c10::kFPGA (rarely used)
    //     8 = c10::kMPS (Apple Silicon, added PyTorch 1.12)
    //     ... (future device types may be added)
    //
    // DESIGN RATIONALE:
    //   - int8_t chosen for compact storage (1 byte vs 8+ bytes for torch::Device)
    //   - Avoids torch:: dependency in POD struct for Fortran ABI compatibility
    //   - -1 sentinel distinguishes "not cached" from c10::kCPU (value 0)
    //   - Staleness detection in get_target_device() validates cache vs actual tensor device
    // FIX Round91: Use int16_t for consistency with cached_device_index (future-proofing)
    int16_t cached_reference_device = -1;  // -1=not cached, else c10::DeviceType value
    // FIX Round89: Add device index for multi-GPU support
    // In multi-GPU scenarios (CUDA:0 vs CUDA:1), device type alone is insufficient.
    // -1 = not cached, else device index (0, 1, 2, ... for cuda:0, cuda:1, cuda:2, ...)
    // For CPU/MPS (single device), this is always 0 when cached.
    // Validity check: both cached_reference_device AND cached_device_index must match.
    // FIX Round90: Use int16_t for device index to support >127 GPUs in large clusters
    int16_t cached_device_index = -1;  // -1=not cached, else device index
    torch::Tensor dz;  // Vertical spacing (varies with height)
    torch::Tensor rdx, rdy, rdz;  // Reciprocal grid spacing (tensor dtype determines precision)
    torch::Tensor rdzw;  // Reciprocal spacing for w points
    torch::Tensor rdnw, rdn;  // Reciprocal eta spacings

    // =========================================================================
    // FIX 2025-01-07 Batch41 Round13: Explicit kdim for 2D rdzw on cubic-like grids
    // =========================================================================
    // DOCUMENTATION FOR EXTERNAL TOOLING:
    //
    // When constructing WRFGridInfo with 2D rdzw tensors, set this field to specify
    // which dimension is the vertical (k) dimension:
    //
    //   rdzw_kdim = 0  → rdzw shape is [nz+1, nx] (k is first dimension) - WRF standard
    //   rdzw_kdim = 1  → rdzw shape is [ny, nz+1] (k is second dimension)
    //   rdzw_kdim = -1 → auto-detect based on shape matching (default)
    //
    // Auto-detection works for non-cubic grids where ny, nx, and nz+1 are distinct.
    // For cubic-like grids (ny == nx == nz+1), auto-detection will default to kdim=0
    // with a warning. Set explicitly to suppress the warning.
    //
    // Example usage:
    //   WRFGridInfo grid_info;
    //   grid_info.rdzw = torch::rand({nz+1, nx});  // [nz+1, nx] layout
    //   grid_info.rdzw_kdim = 0;                    // Explicit: k is dim 0
    //
    // For 1D rdzw [nz+1] or 3D rdzw [ny, nz+1, nx], this field is ignored.
    // 3D rdzw is currently not supported in moisture advection (will fail-fast).
    // =========================================================================
    int rdzw_kdim = -1;

    // =========================================================================
    // FIX 2025-01-08 Batch41 Round31: Explicit kdim for 2D rdz on cubic-like grids
    // =========================================================================
    // Same semantics as rdzw_kdim but for mass-level rdz (nz elements, not nz+1):
    //
    //   rdz_kdim = 0  → rdz shape is [nz, nx] (k is first dimension) - WRF standard
    //   rdz_kdim = 1  → rdz shape is [ny, nz] (k is second dimension)
    //   rdz_kdim = -1 → auto-detect based on shape matching (default)
    //
    // Used by buoyancy diffusion in wrf_sdirk3_unified_rhs_acoustic_gravity_impl.cpp
    // via metric_utils::extract1DVerticalMetric(rdz, nz, ny, nx, rdz_kdim).
    // =========================================================================
    int rdz_kdim = -1;

    // Vertical grid coefficients
    torch::Tensor c1h, c2h;  // Half level coefficients
    torch::Tensor c1f, c2f;  // Full level coefficients
    torch::Tensor dnw, dn;   // Eta level spacings
    torch::Tensor fnm, fnp;  // Vertical interpolation weights (mass to w)
    torch::Tensor fzm, fzp;  // Vertical interpolation weights (w to mass)
    torch::Tensor znw, znu;  // Eta levels (w and mass points)
    
    // Reference state
    torch::Tensor p_base;    // Base state pressure (3D)
    torch::Tensor th_base;   // Base state potential temperature (3D)
    torch::Tensor ph_base;   // Base state geopotential (3D, w-staggered)
    torch::Tensor mub;       // Base state column mass (2D)
    torch::Tensor mu_base;   // Base state dry air mass (2D)
    torch::Tensor alb;       // Base state inverse density (3D)
    
    // Map scale factors
    torch::Tensor msfux, msfuy;  // Map factors at u points
    torch::Tensor msfvx, msfvy;  // Map factors at v points
    torch::Tensor msftx, msfty;  // Map factors at mass points
    torch::Tensor msfvx_inv;     // Inverse map factor at v points
    
    // Physical constants (WRF-exact values)
    float g = 9.81f;         // Gravity [m/s²]
    float cs = 340.0f;       // Sound speed [m/s]
    float cp = 1004.5f;      // Specific heat at constant pressure [J/kg/K] - WRF exact
    float cv = 717.5f;       // Specific heat at constant volume [J/kg/K] - WRF exact
    float rd = 287.0f;       // Gas constant for dry air [J/kg/K]
    float rv = 461.6f;       // Gas constant for water vapor [J/kg/K]
    float p0 = 100000.0f;    // Reference pressure [Pa] (p1000mb in WRF)
    float t0 = 300.0f;       // Reference temperature [K]
    float reradius = 1.0f/6370000.0f;  // 1/Earth radius
    
    // Coriolis and rotation parameters
    torch::Tensor f;         // Coriolis parameter (2D)
    torch::Tensor e;         // 2*Omega*cos(latitude) (2D)
    torch::Tensor sina;      // sin(latitude) (2D)
    torch::Tensor cosa;      // cos(latitude) (2D)
    
    // Timestep (SDIRK stage timestep for physics computations)
    float dt = 0.0f;         // Current SDIRK stage timestep [s]

    // Damping coefficients
    float khdif = 500.0f;    // Horizontal diffusion - DEFAULT CHANGED FOR STABILITY (typical WRF: 100-1000 m²/s)
    float kvdif = 100.0f;    // Vertical diffusion - DEFAULT CHANGED FOR STABILITY (typical WRF: 10-100 m²/s)
    float kdamp = 0.2f;      // Divergence damping - DEFAULT CHANGED FOR STABILITY (typical WRF: 0.1-0.3)
    float damp_mu = 0.0f;    // External mode filter
    float damp_top = 5000.0f;// Upper boundary damping - DEFAULT CHANGED FOR STABILITY (typical WRF: 5000m)
    torch::Tensor dampcoef;  // Vertical damping profile
    
    // Terrain and metric terms
    torch::Tensor phb;       // Base state geopotential at surface
    torch::Tensor ht;        // Terrain height
    torch::Tensor xlat;      // Latitude at mass points (2D)
    torch::Tensor xlong;     // Longitude at mass points (2D)
    torch::Tensor xlat_u;    // Latitude at u-staggered points (2D)
    torch::Tensor xlat_v;    // Latitude at v-staggered points (2D)
    
    // Terrain slope derivatives (needed for deformation with terrain correction)
    torch::Tensor zx;        // ∂z/∂x terrain slope (3D, at u,w-staggered points)
    torch::Tensor zy;        // ∂z/∂y terrain slope (3D, at v,w-staggered points)
    
    // Non-hydrostatic pressure coefficients
    torch::Tensor cf1, cf2, cf3;  // Coefficients for k=1 dpn calculation
    torch::Tensor cfn, cfn1;      // Coefficients for top lid dpn
    
    // Acoustic mode gradient terms (for split-mode compatibility)
    torch::Tensor acoustic_grad_u;  // Acoustic mode u gradient (3D)
    torch::Tensor acoustic_grad_v;  // Acoustic mode v gradient (3D)
    torch::Tensor acoustic_grad_w;  // Acoustic mode w gradient (3D)
    torch::Tensor acoustic_grad_p;  // Acoustic mode pressure gradient (3D)
    
    // Boundary relaxation coefficients
    torch::Tensor fcx;       // Relaxation term for X boundary zone (1D)
    torch::Tensor gcx;       // 2nd relaxation term for X boundary zone (1D)
    torch::Tensor fcy;       // Relaxation term for Y boundary zone (1D)
    torch::Tensor gcy;       // 2nd relaxation term for Y boundary zone (1D)
    int spec_bdy_width = 5;  // Width of boundary arrays (typically 5)
    int spec_zone = 1;       // Width of outer specified zone (no relaxation)
    int relax_zone = 4;      // Inner edge of relaxation zone
    float dtbc = 0.0f;       // Time increment for boundary tendency interpolation
    
    // Boundary value arrays for specified/nested BC (if available)
    // These store the prescribed boundary values and tendencies
    // Dimensions: [j,k,spec_bdy_width] for x-boundaries, [i,k,spec_bdy_width] for y-boundaries
    torch::Tensor u_bdy_xs, u_bdy_xe, u_bdy_ys, u_bdy_ye;      // U boundary values
    torch::Tensor v_bdy_xs, v_bdy_xe, v_bdy_ys, v_bdy_ye;      // V boundary values
    torch::Tensor w_bdy_xs, w_bdy_xe, w_bdy_ys, w_bdy_ye;      // W boundary values
    torch::Tensor t_bdy_xs, t_bdy_xe, t_bdy_ys, t_bdy_ye;      // Theta boundary values
    torch::Tensor ph_bdy_xs, ph_bdy_xe, ph_bdy_ys, ph_bdy_ye;  // Geopotential boundary values
    torch::Tensor mu_bdy_xs, mu_bdy_xe, mu_bdy_ys, mu_bdy_ye;  // Column mass boundary values
    
    // Boundary tendency arrays (time derivatives of boundary values)
    torch::Tensor u_btend_xs, u_btend_xe, u_btend_ys, u_btend_ye;
    torch::Tensor v_btend_xs, v_btend_xe, v_btend_ys, v_btend_ye;
    torch::Tensor w_btend_xs, w_btend_xe, w_btend_ys, w_btend_ye;
    torch::Tensor t_btend_xs, t_btend_xe, t_btend_ys, t_btend_ye;
    torch::Tensor ph_btend_xs, ph_btend_xe, ph_btend_ys, ph_btend_ye;
    torch::Tensor mu_btend_xs, mu_btend_xe, mu_btend_ys, mu_btend_ye;
    
    // Horizontal diffusion coefficients
    torch::Tensor xkmh;      // Horizontal eddy viscosity for momentum (3D)
    torch::Tensor xkhh;      // Horizontal eddy diffusivity for heat (3D)
    
    // Base state for idealized cases
    torch::Tensor t_base;    // Base state temperature profile (1D)
    torch::Tensor u_base;    // Base state u-wind profile (1D)
    torch::Tensor v_base;    // Base state v-wind profile (1D)
    torch::Tensor qv_base;   // Base state water vapor profile (1D)
    
    // Moisture fields (P_QV=1, P_QC=2, P_QR=3, P_QI=4, P_QS=5, P_QG=6 in WRF)
    // WRF moist array indices are 1-based in Fortran, 0-based here
    torch::Tensor qv;        // Water vapor mixing ratio (3D) - critical for μ_d calculation
    torch::Tensor qc;        // Cloud water mixing ratio (3D)
    torch::Tensor qr;        // Rain water mixing ratio (3D)
    torch::Tensor qi;        // Cloud ice mixing ratio (3D)
    torch::Tensor qs;        // Snow mixing ratio (3D)
    torch::Tensor qg;        // Graupel mixing ratio (3D)

    // Full 4D moist array and species count (for general iteration)
    // Shape: [ny, nz, nx, n_moist] matching WRF's (i,k,j,species) after transpose
    torch::Tensor moist;     // 4D moisture array
    int n_moist = 0;         // Number of active moisture species

    // Chemistry species (WRF-Chem chem array)
    // Shape: [ny, nz, nx, n_chem] matching WRF's (i,k,j,species) after transpose
    torch::Tensor chem;      // 4D chemistry array
    int n_chem = 0;          // Number of active chemistry species

    // Tracer species (WRF tracer array)
    // Shape: [ny, nz, nx, n_tracer] matching WRF's (i,k,j,species) after transpose
    torch::Tensor tracer;    // 4D tracer array
    int n_tracer = 0;        // Number of active tracer species

    // Scalar species (additional scalars beyond moist/chem/tracer)
    // Shape: [ny, nz, nx, n_scalar] matching WRF's (i,k,j,species) after transpose
    torch::Tensor scalar;    // 4D scalar array
    int n_scalar = 0;        // Number of active scalar species

    // Moisture correction factors for dynamics (WRF-consistent)
    torch::Tensor cqu;       // Moisture correction at u-points (3D, u-staggered)
    torch::Tensor cqv;       // Moisture correction at v-points (3D, v-staggered)
    torch::Tensor cqw;       // Moisture correction at w-points (3D, w-staggered)
    
    // Boundary conditions
    bool periodic_x = false;
    bool periodic_y = false;
    bool symmetric_xs = false;
    bool symmetric_xe = false;
    bool symmetric_ys = false;
    bool symmetric_ye = false;

    // Base state generation counter (for preconditioner coefficient refresh)
    // Incremented whenever setBaseState() is called with valid mub/th_base
    uint64_t base_state_generation_ = 0;
    bool open_xs = false;
    bool open_xe = false;
    bool open_ys = false;
    bool open_ye = false;
    bool specified = false;
    bool nested = false;
    bool polar = false;
    
    // Halo information for tensors with extended bounds
    int halo_width = 0;           // Halo width (typically 5 for HALO_EM_E_5)
    int j_halo_start = 0;         // Start index in j for tensors with halos
    int j_halo_end = 0;           // End index in j for tensors with halos  
    int i_halo_start = 0;         // Start index in i for tensors with halos
    int i_halo_end_mass = 0;      // End index in i for mass-point tensors with halos
    int i_halo_end_u = 0;         // End index in i for u-staggered tensors with halos
    
    // Map projection (0 = no projection/idealized case)
    int map_proj = -1;            // -1 = not set, 0 = idealized, >0 = real projection
    
    // Constructor
    WRFGridInfo() = default;

    // ============================================================================
    // Cache Invalidation Helper
    // ============================================================================
    // FIX 2025-12-26: Call this after modifying vertical metrics in-place
    // (dz, dn, dnw, z_at_w). This invalidates cached reductions.
    // FIX 2025-01-11 Round74: Also invalidates cached_reference_device.
    //
    // NOTE: This requires including the cache headers:
    //   #include "wrf_sdirk3_rayleigh_damping_ad.h"  // invalidateZ1DCache()
    //   #include "wrf_sdirk3_boundary_ad.h"         // invalidateDzMinCache()
    //
    // Example usage:
    //   grid.dz = new_dz_tensor;
    //   grid.invalidateVerticalMetricCaches();
    // ============================================================================
    void invalidateVerticalMetricCaches();  // Defined in wrf_sdirk3_cache_utils.cpp

    // ============================================================================
    // Per-Solver State Reset Helper
    // ============================================================================
    // FIX 2025-01-11 Round74: Call this when reusing a solver instance for a new
    // simulation or after device migration. Resets warning flags and logging state
    // so that warnings can be re-triggered for the new use case.
    //
    // Without this reset, a reused solver instance would:
    //   - Suppress FP64 transition warnings (last_fp64_transition_logged)
    //   - Suppress null path warnings (null_path_fp64_logged)
    //   - Suppress no-reference-device warnings (warned_no_reference_device)
    //   - Use potentially stale device cache (cached_reference_device)
    //
    // Example usage:
    //   // When reinitializing a solver for a new run:
    //   solver->grid_info_->resetPerSolverState();
    //   solver->grid_info_->invalidateVerticalMetricCaches();
    //
    //   // When migrating solver to a different device:
    //   solver->grid_info_->resetPerSolverState();  // Clears stale device info
    // ============================================================================
    void resetPerSolverState();  // Defined in wrf_sdirk3_cache_utils.cpp
};

// ============================================================================
// FIX Round107/Round108: Safe device index conversion with overflow protection
// ============================================================================
// Converts device index to int16_t with overflow checking.
// Use this instead of direct static_cast<int16_t>(device().index()) to avoid
// silent overflow when device index > 32767 (extreme multi-GPU clusters).
//
// Behavior:
//   - index in [-32768, 32767]: returns index unchanged
//   - index > 32767: returns INT16_MAX, logs WARN_ONCE
//   - index < -32768: returns INT16_MIN, logs WARN_ONCE
//
// FIX Round108: Implementation moved to wrf_sdirk3_cache_utils.cpp to ensure
// WARN_ONCE fires only once across all TUs (header-only static vars cause
// duplicate warnings per TU).
//
// Example usage:
//   int16_t cached_idx = safe_device_index(tensor.device().index());
// ============================================================================
int16_t safe_device_index(int64_t index);  // Defined in wrf_sdirk3_cache_utils.cpp

// FIX Round108/Round110: Static assert to catch future c10::DeviceIndex expansions.
// INTENTIONAL DUAL-CHECK POLICY:
// - static_assert: Catches STRUCTURAL changes (type size) at compile time
// - Runtime clamp: Handles VALUE overflow even when type fits (e.g., uint16_t > 32767)
// Both checks are complementary, not redundant.
//
// If static_assert fires, c10::DeviceIndex has grown beyond int16_t (rare - would break
// much code). Consider upgrading cached_device_index field in WRFGridInfo to int32_t,
// update safe_device_index accordingly, and bump ABI version.
static_assert(sizeof(c10::DeviceIndex) <= sizeof(int16_t),
    "c10::DeviceIndex has grown beyond int16_t! Consider upgrading cached_device_index.");

// FIX Round109/Round110: Overload for c10::DeviceIndex with runtime clamp.
// Always delegates to int64_t overload for consistent runtime safety:
// - Handles values > INT16_MAX even if type fits in sizeof() check
// - Provides clamp + warning if overflow occurs
inline int16_t safe_device_index(c10::DeviceIndex index) {
    return safe_device_index(static_cast<int64_t>(index));
}

// FIX Round186: Centralized convenience overloads for torch::Device and torch::Tensor
// These eliminate duplicate implementations in tile_unified_impl.cpp, validation_enhanced.h, etc.
//
// Overload for torch::Device - extracts and validates device index
inline int16_t safe_device_index(const torch::Device& dev) {
    if (!dev.has_index()) {
        return -1;  // No index (e.g., CPU, or default CUDA device)
    }
    return safe_device_index(dev.index());
}

// Overload for torch::Tensor - extracts device index from tensor's device
inline int16_t safe_device_index(const torch::Tensor& t) {
    if (!t.defined()) {
        return -1;  // Undefined tensor
    }
    return safe_device_index(t.device());
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_TYPES_H