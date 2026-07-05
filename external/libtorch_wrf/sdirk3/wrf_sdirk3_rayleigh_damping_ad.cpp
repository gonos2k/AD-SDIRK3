/**
 * @file wrf_sdirk3_rayleigh_damping_ad.cpp
 * @brief AD-compatible Rayleigh damping for WRF SDIRK3
 * 
 * WRF reference: module_damping.F (rk_rayleigh_damp)
 */

#include <torch/torch.h>
#include <cmath>
#include <vector>  // FIX 2025-12-26: For stride vector in Z1DProfileCache
#include <atomic>  // FIX 2025-12-26: For thread-safe epoch counter
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_rayleigh_damping_ad.h"

namespace wrf {
namespace sdirk3 {

// ============================================================================
// CACHE OPTIMIZATION: Extract 1D z profile once, reuse in multiple functions
// ============================================================================
// PERF FIX 2025-12-26: z_at_w reduction (mean or max) is O(ny*nz*nx) and was
// being computed twice per call to computeCompleteRayleighDampingAD. This
// helper extracts the 1D profile once for reuse by all damping functions.
//
// EPOCH CACHE FIX 2025-12-26: The z_at_w grid typically doesn't change between
// timesteps. We cache the reduced 1D profile and invalidate when the source
// tensor changes (detected via data pointer comparison).

namespace {

// ============================================================================
// Z_AT_W EPOCH CACHE
// ============================================================================
// Caches the 1D reduction result to avoid O(ny*nz*nx) recomputation each call.
// Invalidated when:
//   1. z_at_w data pointer changes (different tensor)
//   2. use_max_height mode changes
//   3. numel changes (grid resize)
//   4. device or dtype changes (multi-device safety) - FIX 2025-12-26
//
// AD SAFETY: Cache is bypassed when requires_grad() to preserve gradient flow.
struct Z1DProfileCache {
    const void* cached_data_ptr = nullptr;  // z_at_w.data_ptr()
    int64_t cached_numel = 0;               // z_at_w.numel()
    int64_t cached_size_1 = 0;              // FIX 2025-12-26: Track nz+1 dimension (size(1))
    int64_t cached_dim = 0;                 // FIX 2025-12-26: Track tensor dim
    std::vector<int64_t> cached_strides;    // FIX 2025-12-26: Track full stride vector
    bool cached_use_max = false;            // use_max_height mode
    torch::Device cached_device = torch::kCPU;  // FIX 2025-12-26: Track device
    torch::ScalarType cached_dtype = torch::kFloat32;  // FIX 2025-12-26: Track dtype
    torch::Tensor cached_result;            // The 1D profile [nz+1]

    // FIX 2025-12-26: Static epoch counter for external cache invalidation
    // Increment this when grid metrics are known to change (e.g., restart, regrid)
    // FIX 2025-12-26: Use std::atomic for thread safety (relaxed ordering suffices)
    static inline std::atomic<uint64_t> global_metric_epoch{0};
    uint64_t cached_epoch = 0;

    bool is_valid(const torch::Tensor& z_at_w, bool use_max_height) const {
        if (!z_at_w.defined() || z_at_w.numel() == 0) return false;
        if (!cached_result.defined()) return false;
        // FIX 2025-12-26: Compare dims first (different dims = different layout)
        if (z_at_w.dim() != cached_dim) return false;
        // FIX 2025-12-26: Compare strides to catch view/transpose differences
        // Two contiguous tensors with same data_ptr but different shapes have different strides
        auto current_strides = z_at_w.strides();
        if (current_strides.size() != cached_strides.size()) return false;
        for (size_t i = 0; i < current_strides.size(); ++i) {
            if (current_strides[i] != cached_strides[i]) return false;
        }
        // FIX 2025-12-26: Include device, dtype, size(1), and epoch in cache key
        return (z_at_w.data_ptr() == cached_data_ptr &&
                z_at_w.numel() == cached_numel &&
                (z_at_w.dim() < 2 || z_at_w.size(1) == cached_size_1) &&  // nz dimension check
                use_max_height == cached_use_max &&
                z_at_w.device() == cached_device &&
                z_at_w.scalar_type() == cached_dtype &&
                cached_epoch == global_metric_epoch.load(std::memory_order_relaxed));
    }

    void update(const torch::Tensor& z_at_w, bool use_max_height,
                const torch::Tensor& result) {
        cached_data_ptr = z_at_w.data_ptr();
        cached_numel = z_at_w.numel();
        cached_size_1 = z_at_w.dim() >= 2 ? z_at_w.size(1) : 0;  // FIX 2025-12-26: Track nz
        cached_dim = z_at_w.dim();  // FIX 2025-12-26: Track dim
        // FIX 2025-12-26: Store strides for stride-based invalidation
        auto strides = z_at_w.strides();
        cached_strides.assign(strides.begin(), strides.end());
        cached_use_max = use_max_height;
        cached_device = z_at_w.device();  // FIX 2025-12-26
        cached_dtype = z_at_w.scalar_type();  // FIX 2025-12-26
        cached_epoch = global_metric_epoch.load(std::memory_order_relaxed);  // FIX 2025-12-26: Capture epoch
        cached_result = result;
    }

    void invalidate() {
        cached_data_ptr = nullptr;
        cached_numel = 0;
        cached_size_1 = 0;
        cached_dim = 0;
        cached_strides.clear();
        cached_epoch = 0;
        cached_result = torch::Tensor();
    }

    // FIX 2025-12-26: Call this when grid metrics are modified in-place
    static void incrementEpoch() { global_metric_epoch.fetch_add(1, std::memory_order_relaxed); }
};

// Thread-local cache to avoid synchronization overhead
// Note: In typical WRF usage, this is single-threaded per MPI rank
static thread_local Z1DProfileCache z1d_cache;

/**
 * Extract 1D z profile from possibly 3D z_at_w tensor with epoch caching.
 * @param z_at_w  Heights at W-levels [nz+1] (1D) or [ny, nz+1, nx] (3D)
 * @param use_max If true, use max for mountainous terrain; else use mean
 * @return        1D tensor [nz+1] suitable for damping computations
 *
 * AD SAFETY 2025-12-26: Bypasses cache when requires_grad() to preserve graph.
 */
inline torch::Tensor extractZ1DProfile(
    const torch::Tensor& z_at_w,
    bool use_max_height) {

    if (!z_at_w.defined() || z_at_w.numel() == 0) {
        return z_at_w;  // Return as-is (will be handled by caller)
    }

    if (z_at_w.dim() == 1) {
        return z_at_w;  // Already 1D, no reduction needed
    }

    // 3D case: [ny, nz+1, nx] → reduce over j,i → [nz+1]
    TORCH_CHECK(z_at_w.dim() == 3,
        "z_at_w must be 1D [nz+1] or 3D [ny, nz+1, nx], got dim=", z_at_w.dim());

    // AD SAFETY 2025-12-26: Bypass cache if requires_grad to preserve graph
    // Caching a computed tensor would detach it from the autograd graph
    if (z_at_w.requires_grad() || wrf::sdirk3::g_sdirk3_config.use_autograd) {
        if (use_max_height) {
            auto max0 = std::get<0>(z_at_w.max(0));
            return std::get<0>(max0.max(1));
        } else {
            // FIX 2025-01-26: Explicit IntArrayRef to resolve ambiguous mean() overload
            return z_at_w.mean(at::IntArrayRef{0, 2});
        }
    }

    // EPOCH CACHE CHECK: Return cached result if valid
    if (z1d_cache.is_valid(z_at_w, use_max_height)) {
        return z1d_cache.cached_result;
    }

    // Compute reduction (O(ny*nz*nx) operation)
    torch::Tensor result;
    if (use_max_height) {
        // Use max for mountainous terrain to capture highest model top
        auto max0 = std::get<0>(z_at_w.max(0));  // max along j → [nz+1, nx]
        result = std::get<0>(max0.max(1));        // max along i → [nz+1]
    } else {
        // FIX 2025-01-26: Explicit IntArrayRef to resolve ambiguous mean() overload
        result = z_at_w.mean(at::IntArrayRef{0, 2});  // Horizontal mean for flat terrain
    }

    // Update cache for next call
    z1d_cache.update(z_at_w, use_max_height, result);

    return result;
}
}  // anonymous namespace

// Compute Rayleigh damping coefficient profile
torch::Tensor computeRayleighDampingCoefficient(
    const torch::Tensor& z_at_w,     // Heights at W-levels [nz+1] or [ny, nz+1, nx]
    float zdamp,                     // Damping depth (m)
    float dampcoef,                  // Damping coefficient
    const WRFGridInfo& grid,
    bool use_max_height) {           // Use max instead of mean for mountainous terrain

    const int nz = grid.nz;

    // SAFETY FIX 2025-12-26: Guard against zdamp <= 0 to prevent NaN from division
    // If zdamp is zero or negative, return zero damping coefficients
    if (zdamp <= 0.0f || dampcoef <= 0.0f) {
        return torch::zeros({nz}, z_at_w.options());
    }

    // PERF FIX 2025-12-26: Use extractZ1DProfile helper for unified 1D extraction
    // Caller may pass pre-computed 1D profile (avoids redundant reduction)
    auto z_at_w_1d = extractZ1DProfile(z_at_w, use_max_height);

    // FIX 2025-12-28: Validate z_at_w_1d length before indexing to prevent OOB
    TORCH_CHECK(z_at_w_1d.numel() >= nz + 1,
        "computeRayleighDampingCoefficient: z_at_w_1d must have at least nz+1=", nz + 1,
        " entries for W-staggered access, got ", z_at_w_1d.numel());

    // AUTOGRAD FIX: Use tensor operations instead of .item() loops
    // This preserves the computation graph for differentiation

    // FIX 2025-12-26: Always use tensor path to avoid GPU→CPU sync from .item()
    // The scalar path was a premature optimization that caused sync on GPU tensors
    // Model top height - always use tensor indexing to preserve device/grad
    auto ztop_tensor = z_at_w_1d[nz];  // 0D tensor, preserves device and optionally grad
    auto z_damp_base_tensor = ztop_tensor - zdamp;

    // Compute height at mass levels using tensor operations
    auto z_lower = z_at_w_1d.slice(0, 0, nz);    // [nz]
    auto z_upper = z_at_w_1d.slice(0, 1, nz+1);  // [nz]
    auto z_mass = 0.5f * (z_lower + z_upper); // Height at mass levels [nz]

    // Compute normalized height for damping layer
    // FIX 2025-12-26: Use z_damp_base_tensor (not z_damp_base) to preserve AD graph
    auto normalized_z = (z_mass - z_damp_base_tensor) / zdamp;

    // Compute sin^2 profile using tensor operations
    float pi = 3.14159265358979323846f;
    auto sin_term = torch::sin(0.5f * pi * normalized_z);
    auto gamma_profile = dampcoef * sin_term * sin_term;

    // Apply mask: only apply damping where z > z_damp_base (inside damping layer)
    // FIX 2025-12-26: Use z_damp_base_tensor for consistent AD graph
    auto mask = (z_mass > z_damp_base_tensor).to(z_at_w.options());
    torch::Tensor gamma_m = gamma_profile * mask;

    return gamma_m;
}

// Apply Rayleigh damping to momentum (U, V, W)
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
applyRayleighDampingMomentumAD(
    const torch::Tensor& u,          // U-velocity [ny, nz, nx+1]
    const torch::Tensor& v,          // V-velocity [ny+1, nz, nx]
    const torch::Tensor& w,          // W-velocity [ny, nz+1, nx]
    const torch::Tensor& u_base,     // Base state U
    const torch::Tensor& v_base,     // Base state V
    const torch::Tensor& gamma_m,    // Damping coefficient at mass levels [nz]
    const torch::Tensor& gamma_w,    // Damping coefficient at W-levels [nz+1]
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    // AUTOGRAD FIX: Use tensor broadcasting instead of .item() loops
    // This preserves the computation graph for differentiation

    // gamma_m has shape [nz], need to broadcast to [ny, nz, nx] or [ny, nz, nx+1]
    // Reshape gamma for broadcasting: [1, nz, 1]
    auto gamma_m_bc = gamma_m.view({1, -1, 1});  // [1, nz, 1]
    auto gamma_w_bc = gamma_w.view({1, -1, 1});  // [1, nz+1, 1]

    // U-damping: u has shape [ny, nz, nx+1]
    // Damp perturbation from base state using tensor operations
    torch::Tensor u_pert = u - u_base;
    torch::Tensor u_tend = -gamma_m_bc * u_pert;

    // V-damping: v has shape [ny+1, nz, nx]
    torch::Tensor v_pert = v - v_base;
    torch::Tensor v_tend = -gamma_m_bc * v_pert;

    // W-damping: w has shape [ny, nz+1, nx]
    // W base state is typically zero, so just damp w directly
    // Note: gamma_w already has zeros at boundaries from computeRayleighDampingProfile
    torch::Tensor w_tend = -gamma_w_bc * w;

    return std::make_tuple(u_tend, v_tend, w_tend);
}

// Apply Rayleigh damping to scalars (theta, qv, etc.)
torch::Tensor applyRayleighDampingScalarAD(
    const torch::Tensor& scalar,     // Scalar field [ny, nz, nx]
    const torch::Tensor& scalar_base,// Base state scalar
    const torch::Tensor& gamma_m,    // Damping coefficient [nz]
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    // AUTOGRAD FIX: Use tensor broadcasting instead of .item() loops
    // This preserves the computation graph for differentiation

    // Reshape gamma for broadcasting: [1, nz, 1]
    auto gamma_m_bc = gamma_m.view({1, -1, 1});  // [1, nz, 1]

    torch::Tensor scalar_tend;

    // Special handling for different variables (branching on string is OK - not tensor data)
    if (variable_name == "theta" || variable_name == "t") {
        // Damp perturbation from base state
        torch::Tensor scalar_pert = scalar - scalar_base;
        scalar_tend = -gamma_m_bc * scalar_pert;
    } else if (variable_name.find("q") == 0) {
        // Moisture variables - damp to zero (no base state)
        scalar_tend = -gamma_m_bc * scalar;
    } else {
        // Default: damp to base state if available, otherwise to zero
        if (scalar_base.defined()) {
            torch::Tensor scalar_pert = scalar - scalar_base;
            scalar_tend = -gamma_m_bc * scalar_pert;
        } else {
            scalar_tend = -gamma_m_bc * scalar;
        }
    }

    return scalar_tend;
}

// Apply upper-level absorbing layer (enhanced damping)
torch::Tensor applyAbsorbingLayerAD(
    const torch::Tensor& w,          // W-velocity [ny, nz+1, nx]
    const torch::Tensor& z_at_w,     // Heights at W-levels [nz+1] or [ny, nz+1, nx]
    float zdamp,                     // Damping depth
    float dampcoef,                  // Damping coefficient
    const WRFGridInfo& grid,
    const ConfigFlags& config,
    bool use_max_height) {           // Use max instead of mean for mountainous terrain

    const int nz = grid.nz;

    // SAFETY FIX 2025-12-26: Guard against zdamp <= 0 to prevent NaN from division
    // If zdamp is zero or negative, return zero tendency
    if (zdamp <= 0.0f || dampcoef <= 0.0f) {
        return torch::zeros_like(w);
    }

    // PERF FIX 2025-12-26: Use extractZ1DProfile helper for unified 1D extraction
    // Caller may pass pre-computed 1D profile (avoids redundant reduction)
    auto z_at_w_1d = extractZ1DProfile(z_at_w, use_max_height);

    // AUTOGRAD FIX: Use tensor operations instead of .item() loops
    // This preserves the computation graph for differentiation

    // AD FIX 2025-12-26: Conditional gradient preservation for z_at_w_1d
    // Model top height - use tensor path if z_at_w_1d requires gradients
    torch::Tensor z_absorb_base_tensor;

    // FIX 2025-12-26: Always use tensor path to avoid .item<float>() GPU sync
    // Both paths now use tensor operations - only difference is NoGradGuard for non-AD case
    if (z_at_w_1d.requires_grad() || wrf::sdirk3::g_sdirk3_config.use_autograd) {
        // TENSOR PATH: Preserve gradient graph when z_at_w_1d requires gradients
        torch::Tensor ztop_tensor = z_at_w_1d[nz];  // 0D tensor, preserves grad
        z_absorb_base_tensor = ztop_tensor - 0.5f * zdamp;  // Absorbing layer in upper half
    } else {
        // TENSOR PATH (no-grad): Use tensor ops without gradient tracking
        // FIX 2025-12-26: Removed .item<float>() GPU sync - unified with computeRayleighDampingCoefficient
        torch::NoGradGuard no_grad;
        torch::Tensor ztop_tensor = z_at_w_1d[nz];  // 0D tensor
        z_absorb_base_tensor = ztop_tensor - 0.5f * zdamp;
    }

    // Get interior W-levels (skip boundaries k=0 and k=nz)
    auto z_interior = z_at_w_1d.slice(0, 1, nz);  // [nz-1] levels from k=1 to k=nz-1

    // Compute normalized height for absorbing layer
    // AD FIX 2025-12-26: Use z_absorb_base_tensor to preserve gradients when z_at_w requires grad
    auto normalized_z = (z_interior - z_absorb_base_tensor) / (0.5f * zdamp);

    // Compute enhanced damping coefficient: 2 * dampcoef * normalized_z^2
    auto gamma_absorb = 2.0f * dampcoef * normalized_z * normalized_z;

    // Apply mask: only apply damping where z > z_absorb_base
    auto mask = (z_interior > z_absorb_base_tensor).to(z_at_w.options());
    gamma_absorb = gamma_absorb * mask;

    // Reshape for broadcasting: [1, nz-1, 1] to match w interior levels
    auto gamma_bc = gamma_absorb.view({1, -1, 1});

    // Apply to W interior levels (skip k=0 boundary)
    // w has shape [ny, nz+1, nx], interior is [ny, nz-1, nx] from k=1 to k=nz-1
    auto w_interior = w.slice(1, 1, nz);  // [ny, nz-1, nx]

    // Compute tendency for interior levels
    auto w_tend_interior = -gamma_bc * w_interior;

    // AD FIX 2025-12-26: Use torch::cat instead of in-place copy_()
    // Preserves gradient graph by avoiding leaf tensor in-place modification
    int ny_w = w.size(0);
    int nx_w = w.size(2);
    auto pad0 = torch::zeros({ny_w, 1, nx_w}, w.options());
    auto w_tend = torch::cat({pad0, w_tend_interior, pad0}, 1);

    return w_tend;
}

// Complete Rayleigh damping for all variables
RayleighDampingTendencies computeCompleteRayleighDampingAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& qc,
    const torch::Tensor& qr,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    RayleighDampingTendencies tendencies;
    
    // Check if damping is enabled
    if (config.damp_opt == 0 || config.dampcoef <= 0) {
        // No damping - return zero tendencies
        tendencies.u_tend = torch::zeros_like(u);
        tendencies.v_tend = torch::zeros_like(v);
        tendencies.w_tend = torch::zeros_like(w);
        tendencies.theta_tend = torch::zeros_like(theta);
        tendencies.qv_tend = torch::zeros_like(qv);
        tendencies.qc_tend = torch::zeros_like(qc);
        tendencies.qr_tend = torch::zeros_like(qr);
        return tendencies;
    }
    
    // Get heights at W-levels
    // FIX 2025-01-26: WRFGridInfo doesn't have z_at_w member directly.
    // Compute z_at_w from grid.dn (vertical spacing) or ph_base (base geopotential)
    torch::Tensor z_at_w;
    if (grid.ph_base.defined() && grid.ph_base.numel() > 0) {
        // ph_base is base state geopotential at w-staggered points (3D)
        // z_at_w = ph_base / g (geopotential height)
        constexpr float g = 9.81f;
        // Extract 1D profile by taking first column
        if (grid.ph_base.dim() == 3) {
            z_at_w = grid.ph_base.select(0, 0).select(1, 0) / g;  // [nz+1]
        } else if (grid.ph_base.dim() == 1) {
            z_at_w = grid.ph_base / g;
        } else {
            // Fallback for unexpected dimensions
            z_at_w = torch::arange(grid.nz + 1, theta.options()) * 100.0f;
        }
    } else if (grid.dn.defined() && grid.dn.numel() >= grid.nz) {
        // AD FIX 2025-12-26: Vectorize z_at_w computation with cumsum
        // Replaces k-loop that would break AD graph with raw indexing
        // FIX 2025-12-28: Handle 1D/2D/3D grid.dn
        torch::Tensor dn_1d;
        if (grid.dn.dim() == 3 && config.use_max_height_for_damping) {
            // FIX 2025-01-26: Fix .values → std::get<0>() for max result
            // Use max over horizontal dimensions to get tallest column profile
            auto max_over_i = std::get<0>(grid.dn.max(/*dim=*/2));  // [ny, nz]
            dn_1d = std::get<0>(max_over_i.max(/*dim=*/0));          // [nz]
        } else if (grid.dn.dim() == 3) {
            // Mean over horizontal dimensions for flat terrain
            dn_1d = grid.dn.mean(at::IntArrayRef{0, 2});
        } else if (grid.dn.dim() == 1) {
            dn_1d = grid.dn;
        } else {
            dn_1d = grid.dn.flatten().slice(0, 0, grid.nz);
        }
        // Ensure positive values and correct dtype
        auto dn_safe = torch::clamp(dn_1d.to(theta.options()), /*min=*/1e-6f);
        // z_at_w[0] = 0, z_at_w[k] = sum(dn[0:k])
        auto z_interior = torch::cumsum(dn_safe, 0);
        z_at_w = torch::cat({torch::zeros({1}, theta.options()), z_interior}, 0);
    } else {
        // Fallback: uniform spacing (default 100m per level)
        auto k_indices = torch::arange(grid.nz + 1, theta.options());
        z_at_w = k_indices * 100.0f;
    }

    // PERF FIX 2025-12-26: Pre-compute 1D z profile once, pass to all damping functions
    // Avoids redundant O(ny*nz*nx) reduction in computeRayleighDampingCoefficient
    // and applyAbsorbingLayerAD. Both functions will detect 1D input and skip reduction.
    auto z_at_w_1d = extractZ1DProfile(z_at_w, /*use_max_height=*/false);

    // DEVICE/DTYPE FIX 2025-12-26: Ensure z_at_w_1d matches theta device/dtype
    // If grid.z_at_w was defined but on CPU while theta is on GPU, this prevents
    // gamma_m (CPU) vs u/v/w (GPU) device mismatch in subsequent operations.
    if (z_at_w_1d.device() != theta.device() || z_at_w_1d.scalar_type() != theta.scalar_type()) {
        z_at_w_1d = z_at_w_1d.to(theta.options());
    }

    // Compute damping coefficient profiles (pass pre-computed 1D profile)
    auto gamma_m = computeRayleighDampingCoefficient(z_at_w_1d, config.zdamp,
                                                     config.dampcoef, grid);
    
    // W-level damping coefficients
    // AD FIX 2025-12-26: Vectorize gamma_w computation using cat (not in-place copy_)
    // gamma_w[k] = 0.5 * (gamma_m[k-1] + gamma_m[k]) for k in [1, nz-1]
    // Boundaries (k=0, k=nz) remain zero
    torch::Tensor gamma_w;
    if (grid.nz > 1) {
        auto gamma_w_interior = 0.5f * (gamma_m.slice(0, 0, grid.nz - 1) +
                                        gamma_m.slice(0, 1, grid.nz));
        auto zero = torch::zeros({1}, gamma_m.options());
        gamma_w = torch::cat({zero, gamma_w_interior, zero}, 0);
    } else {
        gamma_w = torch::zeros({grid.nz + 1}, theta.options());
    }
    
    // Get base state fields
    // FIX 2025-01-26: grid.theta_base doesn't exist, use grid.th_base (3D base state theta)
    torch::Tensor u_base = grid.u_base.defined() ? grid.u_base : torch::zeros_like(u);
    torch::Tensor v_base = grid.v_base.defined() ? grid.v_base : torch::zeros_like(v);
    torch::Tensor theta_base;
    if (grid.th_base.defined()) {
        // th_base is already 3D [ny, nz, nx] in WRFGridInfo
        theta_base = grid.th_base;
    } else {
        theta_base = torch::zeros_like(theta);
    }
    
    // Apply damping to momentum
    auto [u_damp, v_damp, w_damp] = applyRayleighDampingMomentumAD(
        u, v, w, u_base, v_base, gamma_m, gamma_w, grid, config);
    
    tendencies.u_tend = u_damp;
    tendencies.v_tend = v_damp;
    tendencies.w_tend = w_damp;
    
    // Apply damping to scalars
    tendencies.theta_tend = applyRayleighDampingScalarAD(
        theta, theta_base, gamma_m, "theta", grid, config);
    
    // Moisture variables (no base state)
    torch::Tensor qv_base = torch::zeros_like(qv);
    torch::Tensor qc_base = torch::zeros_like(qc);
    torch::Tensor qr_base = torch::zeros_like(qr);
    
    tendencies.qv_tend = applyRayleighDampingScalarAD(
        qv, qv_base, gamma_m, "qv", grid, config);
    tendencies.qc_tend = applyRayleighDampingScalarAD(
        qc, qc_base, gamma_m, "qc", grid, config);
    tendencies.qr_tend = applyRayleighDampingScalarAD(
        qr, qr_base, gamma_m, "qr", grid, config);
    
    // Add absorbing layer for W if enabled (pass pre-computed 1D z profile)
    if (config.w_damping > 0) {
        tendencies.w_tend = tendencies.w_tend +
                           applyAbsorbingLayerAD(w, z_at_w_1d, config.zdamp,
                                                config.dampcoef, grid, config);
    }
    
    return tendencies;
}

// Apply implicit Rayleigh damping (for strong damping)
torch::Tensor applyImplicitRayleighDampingAD(
    const torch::Tensor& field,      // Field to damp
    const torch::Tensor& field_base, // Base state
    const torch::Tensor& gamma,      // Damping coefficient
    float dt,                        // Time step
    const WRFGridInfo& grid) {
    
    // Implicit update: field_new = (field_old + dt*gamma*field_base) / (1 + dt*gamma)
    torch::Tensor damping_factor = 1.0f / (1.0f + dt * gamma.unsqueeze(0).unsqueeze(2));
    
    torch::Tensor field_new;
    if (field_base.defined()) {
        field_new = (field + dt * gamma.unsqueeze(0).unsqueeze(2) * field_base) * damping_factor;
    } else {
        // Damp to zero
        field_new = field * damping_factor;
    }
    
    return field_new;
}

// ============================================================================
// Cache Invalidation Implementation
// ============================================================================

void invalidateZ1DCache() {
    // FIX 2025-12-26: Increment epoch to invalidate cached z_at_w 1D profiles
    // Called when grid metrics are modified in-place
    Z1DProfileCache::incrementEpoch();
}

} // namespace sdirk3
} // namespace wrf