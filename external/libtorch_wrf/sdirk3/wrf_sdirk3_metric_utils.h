// ============================================================================
// WRF-SDIRK3 Metric Safety Utilities
// ============================================================================
// Common helpers for safe metric operations (NaN/Inf/negative handling).
// Used by: spatial_derivatives, vectorized_ops, divergence_damping, AD modules.
//
// WRF CONVENTION: All vertical metrics (dn, dnw, rdnw, rdn) are POSITIVE.
// See RDNW_FINAL_VERIFICATION.md section 5.2 for EPS policy.
//
// AD SAFETY NOTE (2025-12-26):
// - MetricCache and safe_mean() use .detach() + CPU copy, breaking gradient flow
// - These are ONLY safe for static grid metrics (dn, dnw, rdnw, rdn, z_at_w)
// - For state tensors (u, v, w, theta) that require gradients, use tensor-based
//   alternatives: safe_mean_tensor(), normalize_positive(), safe_inv_tensor()
// - Use requiresGradCheck() to validate inputs in debug builds
//
// ============================================================================
// extract1DVerticalMetric() CALLING CONVENTION (2025-12-26)
// ============================================================================
// For extracting 1D [nz] vertical profiles from 1D/2D/3D metric tensors:
//
// 3-ARG VERSION: extract1DVerticalMetric(metric, nz, ny, nx)
//   - Auto-detects kdim for 2D tensors based on shape matching
//   - THROWS on cubic grids (ny==nx==nz) where layout is ambiguous
//   - Safe for non-cubic grids where dimensions are distinct
//
// 4-ARG VERSION: extract1DVerticalMetric(metric, nz, ny, nx, kdim)
//   - Use when 2D tensor layout is known explicitly
//   - kdim=0: [nz, nx] layout (k is first dimension)
//   - kdim=1: [ny, nz] layout (k is second dimension)
//   - kdim=-1: auto-detect (same as 3-arg, but allows cubic grids)
//   - REQUIRED for cubic grids where 3-arg would fail
//
// W-LEVEL VERSION: extract1DVerticalMetricForW(metric, nz, ny, nx, kdim=-1)
//   - For w-level quantities with nz+1 elements (z_at_w, w staggered)
//   - Internally uses nz+1 for dimension matching
//   - Same kdim semantics as 4-arg version
//   - Use for z_at_w; use extract1DVerticalMetric for dnw/dn
//
// RECOMMENDED PATTERN:
//   // For mass-level metrics (dnw, dn, c1f, c2f):
//   auto dnw_1d = extract1DVerticalMetric(grid.dnw, nz, ny, nx, /*kdim=*/0);
//
//   // For w-level metrics (z_at_w):
//   auto z_1d = extract1DVerticalMetricForW(grid.z_at_w, nz, ny, nx, /*kdim=*/0);
// ============================================================================
//
// ============================================================================
// CACHING EXPANSION OPPORTUNITIES (2025-12-31 Batch36 Issue 4)
// ============================================================================
// The codebase has many repeated unsqueeze().expand() patterns that could
// benefit from caching similar to Msf3DCache in wrf_sdirk3_tile_unified_impl.cpp.
//
// Current caching (Msf3DCache):
//   - msfux, msfuy, msfvx, msfvy, msftx, msfty (6 tensors)
//
// Potential caching candidates (found 100+ patterns):
//   1. Mu tensors: muu_3d, muv_3d, muw_3d, mu_full_3d (~50 locations)
//   2. Grid coefficients: c1h_3d, c2h_3d, c1f_3d, c2f_3d (~20 locations)
//   3. Rotation/trig: e_3d, cosa_3d, sina_3d, f_3d (~15 locations)
//   4. Terrain: sin_alpha_x_3d, sin_alpha_y_3d, cos_alpha_x_3d (~10 locations)
//   5. Base state: p_base_3d, th_base_3d, mu_base_3d (~10 locations)
//
// Implementation notes:
//   - Each cache would follow the Msf3DCache pattern with epoch-based invalidation
//   - Mu caches would need separate instances for u/v/w/mass grids
//   - Coefficient caches (c1h, c2h) would be simpler as they're 1D→3D expansions
//   - Consider StaticMetricCache3D pattern for immutable metrics
//
// Priority for implementation:
//   HIGH:   Mu tensors (most frequent, called in inner loops)
//   MEDIUM: Grid coefficients (static, easy to cache)
//   LOW:    Rotation/trig, terrain (less frequent)
// ============================================================================

#pragma once

#include <torch/torch.h>
#include <ATen/autocast_mode.h>  // OPT Pass34: For at::autocast API
#include <cmath>
#include <stdexcept>
#include "wrf_config_flags.h"  // For is_ad_strict_mode()
#include "wrf_sdirk3_types.h"  // FIX Round178: For safe_device_index()
#include "wrf_sdirk3_config.h"  // OPT Pass34: For g_sdirk3_config

namespace wrf {
namespace sdirk3 {
namespace metric_utils {

// ============================================================================
// AD Safety Utilities
// ============================================================================

/**
 * Check if tensor requires grad and warn/assert if used with scalar extraction.
 *
 * Behavior depends on build type and AD strict mode:
 * - Debug builds: Always throw to catch misuse early
 * - Release builds (normal): Log warning but continue
 * - Release builds (ad_strict_mode=1): Throw to prevent silent graph loss
 *
 * Use set_ad_strict_mode(true) at initialization for production AD workloads.
 */
inline void requiresGradCheck(const torch::Tensor& tensor, const char* context) {
    if (tensor.defined() && tensor.requires_grad()) {
#ifndef NDEBUG
        // Debug build: always throw to catch misuse early
        throw std::runtime_error(
            std::string("AD GRAPH BREAK: ") + context +
            " called on requires_grad tensor. Use tensor-based alternative.");
#else
        // Release build: check AD strict mode
        if (is_ad_strict_mode()) {
            // Strict mode: throw even in Release
            TORCH_CHECK(false,
                "AD STRICT MODE: ", context,
                " called on requires_grad tensor. "
                "Use tensor-based alternative or disable ad_strict_mode.");
        } else {
            // Normal mode: warn but continue (gradient will be lost)
            TORCH_WARN_ONCE("AD warning: ", context,
                            " called on requires_grad tensor - gradients will be lost");
        }
#endif
    }
}

/**
 * Safely detach tensor for scalar extraction, with explicit intent.
 * Use this when you KNOW gradient is not needed (e.g., static grid metrics).
 */
inline torch::Tensor detachForScalar(const torch::Tensor& tensor) {
    return tensor.defined() ? tensor.detach() : tensor;
}

// ============================================================================
// Epsilon Constants by Precision
// ============================================================================
// FP32 default, FP16/BF16 need larger eps to avoid underflow
constexpr float kMetricEpsFP32 = 1e-10f;
constexpr float kMetricEpsFP16 = 1e-4f;
constexpr float kMetricEpsBF16 = 1e-3f;

// Default epsilon (FP32 for backward compatibility)
constexpr float kMetricEps = kMetricEpsFP32;

/**
 * Get appropriate epsilon based on tensor dtype.
 * FP16/BF16 paths need larger eps to avoid underflow/inf.
 */
inline float getEpsForDtype(torch::ScalarType dtype) {
    switch (dtype) {
        case torch::kFloat16:
            return kMetricEpsFP16;
        case torch::kBFloat16:
            return kMetricEpsBF16;
        default:
            return kMetricEpsFP32;
    }
}

/**
 * Get appropriate epsilon based on tensor options.
 * Checks dtype and returns corresponding eps.
 */
inline float getEpsForOptions(const torch::TensorOptions& options) {
    return getEpsForDtype(options.dtype().toScalarType());
}

/**
 * Get autocast-aware epsilon.
 * Checks if autocast is enabled and returns appropriate eps.
 * FIX 2025-12-26: Use device-aware autocast detection API and query actual dtype.
 */
inline float getAutocastAwareEps(const torch::Tensor& tensor) {
    // Check if autocast is enabled (device-aware) and get the autocast dtype
    bool autocast_enabled = false;
    torch::ScalarType autocast_dtype = torch::kFloat32;

#if defined(AT_PER_OPERATOR_HEADERS) || (TORCH_VERSION_MAJOR > 1) || (TORCH_VERSION_MAJOR == 1 && TORCH_VERSION_MINOR >= 12)
    // PyTorch 1.12+ has device-specific autocast API
    if (tensor.is_cuda()) {
        autocast_enabled = at::autocast::is_autocast_enabled(at::kCUDA);
        if (autocast_enabled) {
            autocast_dtype = at::autocast::get_autocast_dtype(at::kCUDA);
        }
    } else if (tensor.is_cpu()) {
        autocast_enabled = at::autocast::is_autocast_enabled(at::kCPU);
        if (autocast_enabled) {
            autocast_dtype = at::autocast::get_autocast_dtype(at::kCPU);
        }
    }
    // MPS doesn't have autocast API yet, fall back to dtype check
#else
    // Older PyTorch: use legacy global check
#ifdef USE_CUDA
    autocast_enabled = at::autocast::is_enabled();
    if (autocast_enabled) {
        // Legacy API defaults to FP16 for CUDA autocast
        autocast_dtype = torch::kFloat16;
    }
#endif
#endif

    // Determine effective dtype: autocast dtype if enabled, else tensor's actual dtype
    auto effective_dtype = autocast_enabled ? autocast_dtype : tensor.scalar_type();

    // Return eps based on effective dtype
    return getEpsForDtype(effective_dtype);
}

// ============================================================================
// Default TensorOptions for Fallback
// ============================================================================
inline torch::TensorOptions getDefaultMetricOptions() {
    return torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
}

// ============================================================================
// Metric Injection Validation (FIX 2025-12-27)
// ============================================================================
/**
 * Check if externally-injected metric is properly defined.
 * Issues TORCH_WARN_ONCE if undefined/empty, since this may indicate
 * missing WRF→C++ data injection.
 *
 * WRF Metric Injection Paths:
 * - c1f, c2f: from_blob() in wrf_sdirk3_tile_unified_impl.cpp (1D {nz_w})
 * - dz: C++ computed in computeVerticalMetrics() (1D from 3D mean)
 * - dnw: Derived from rdnw_ or externally injected (expected 1D)
 * - dn: Externally injected only (expected 1D)
 *
 * @param metric  The metric tensor to check
 * @param name    Human-readable name for warning message
 * @param context Function/location context for debugging
 * @return true if metric is defined and non-empty, false otherwise
 */
inline bool checkMetricInjected(const torch::Tensor& metric,
                                 const char* name,
                                 const char* context) {
    if (!metric.defined() || metric.numel() == 0) {
        TORCH_WARN_ONCE(
            "METRIC INJECTION: ", name, " is undefined/empty in ", context, ". "
            "Using fallback (uniform spacing). If this is production, ensure "
            "WRF→C++ metric injection is configured.");
        return false;
    }
    return true;
}

/**
 * Get metric or fallback with warning.
 * Returns the metric if defined, otherwise creates fallback and warns.
 *
 * @param metric   Input metric (may be undefined)
 * @param nz       Size for fallback tensor
 * @param options  TensorOptions for fallback
 * @param name     Metric name for warning
 * @param context  Function context for warning
 * @return         Metric tensor (original or fallback)
 */
inline torch::Tensor getMetricOrFallback(const torch::Tensor& metric,
                                          int64_t nz,
                                          const torch::TensorOptions& options,
                                          const char* name,
                                          const char* context) {
    if (checkMetricInjected(metric, name, context)) {
        return metric;
    }
    // FIX 2025-01-11 Round38: Fallback metrics should never require gradients
    // Using options.requires_grad(true) would cause MetricCache/requiresGradCheck warnings
    auto fallback_options = options.requires_grad(false);
    return torch::ones({nz}, fallback_options);
}

// ============================================================================
// Scalar Helpers (for use with .item<float>())
// ============================================================================

/**
 * Safe absolute value with NaN/Inf handling.
 * Returns |value| if finite, else eps.
 */
inline float safe_abs(float value, float eps = kMetricEps) {
    return std::isfinite(value) ? std::abs(value) : eps;
}

/**
 * Safe inverse with NaN/Inf/zero handling.
 * Returns 1/|value| if finite and |value| >= eps, else 1/eps.
 */
inline float safe_inv(float value, float eps = kMetricEps) {
    float abs_val = safe_abs(value, eps);
    return (abs_val >= eps) ? (1.0f / abs_val) : (1.0f / eps);
}

/**
 * Safe inverse for dn/dnw metrics with NaN/Inf/zero handling.
 * Enforces positive result per WRF convention.
 */
inline float safe_inv_dn(float dn_value, float eps = kMetricEps) {
    return safe_inv(dn_value, eps);
}

/**
 * Safe rdnw/rdn value with NaN/Inf/negative handling.
 * Enforces positive result per WRF convention.
 */
inline float safe_rdnw(float rdnw_value, float eps = kMetricEps) {
    float abs_val = safe_abs(rdnw_value, eps);
    return (abs_val >= eps) ? abs_val : eps;
}

/**
 * Get dn value with safety (for use as divisor or multiplier).
 * Returns |value| clamped to >= eps.
 */
inline float safe_dn(float dn_value, float eps = kMetricEps) {
    float abs_val = safe_abs(dn_value, eps);
    return (abs_val >= eps) ? abs_val : eps;
}

// ============================================================================
// Tensor Helpers (for use with torch::Tensor)
// ============================================================================

/**
 * Normalize tensor to positive values with NaN/Inf handling.
 * Pattern: torch::where(isfinite, abs, eps) -> clamp_min(eps)
 *
 * EPS SELECTION GUIDANCE (FIX 2025-12-26):
 * - Default (eps=0.0f): Uses getAutocastAwareEps() which detects autocast state
 * - PROBLEM: If you've already upcasted to FP32 in a compute path, autocast
 *   detection may still return FP16 eps, causing over-clamping
 * - SOLUTION: When operating in explicit compute_opts context (e.g., after
 *   needs_fp32_upcast logic), pass explicit eps from getEpsForOptions():
 *
 *   Example:
 *   ```cpp
 *   auto compute_opts = needs_fp32_upcast ? opts.dtype(torch::kFloat32) : opts;
 *   float eps = metric_utils::getEpsForOptions(compute_opts);  // dtype-based
 *   auto dnw_safe = metric_utils::normalize_positive(dnw_tensor, eps);
 *   ```
 *
 * This ensures the eps matches the actual compute precision, not the
 * autocast/input tensor precision.
 */
inline torch::Tensor normalize_positive(const torch::Tensor& tensor, float eps = 0.0f) {
    // Use autocast-aware eps if not explicitly provided
    float effective_eps = (eps > 0.0f) ? eps : getAutocastAwareEps(tensor);

    auto finite_mask = torch::isfinite(tensor);
    auto abs_tensor = torch::where(finite_mask, tensor.abs(),
                                   torch::full_like(tensor, effective_eps));
    return torch::clamp_min(abs_tensor, effective_eps);
}

/**
 * Safe inverse of tensor with NaN/Inf/zero handling.
 * Returns 1.0 / normalize_positive(tensor).
 *
 * EPS SELECTION: See normalize_positive() for guidance on explicit eps passing
 * in compute_opts contexts. Pass getEpsForOptions(compute_opts) when the
 * compute dtype differs from the input tensor dtype.
 */
inline torch::Tensor safe_inv_tensor(const torch::Tensor& tensor, float eps = 0.0f) {
    return 1.0f / normalize_positive(tensor, eps);
}

// ============================================================================
// Vertical Metric Extraction (Shared Utility)
// ============================================================================
/**
 * Extract 1D vertical metric from potentially 1D/2D/3D tensor.
 *
 * FIX 2025-12-26: Shared utility to replace per-file local helpers.
 * Handles dimension detection and proper k-axis identification for 2D case.
 *
 * WRF Vertical Dimension Convention:
 * - Mass levels: nz (theta, density, pressure defined here)
 * - W-levels: nz+1 (vertical velocity defined here)
 * - dnw/rdnw: nz elements (gaps between nz+1 w-levels)
 * - dn/rdn: nz elements (gaps at mass levels, used for vertical differencing)
 * - z_at_w: nz+1 elements (height at w-levels)
 *
 * Pass nz for dnw/rdnw/dn (NOT nz+1), since these arrays have nz elements.
 * Pass nz+1 only for w-level quantities like z_at_w.
 *
 * Dimension handling:
 * - 1D [nz]: Return slice(0, 0, nz)
 * - 2D [nz,nx] or [ny,nz]: Detect k-axis by matching nz, reduce via mean
 * - 3D [ny,nz,nx]: mean({0,2}) to get [nz]
 *
 * @param metric  Input metric tensor (dnw, dn, c1f, c2f, z_at_w, etc.)
 * @param nz      Expected vertical dimension size (nz for dnw/dn, nz+1 for z_at_w)
 * @return        1D tensor [nz] suitable for vertical operations
 */
inline torch::Tensor extract1DVerticalMetric(const torch::Tensor& metric, int64_t nz) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();  // Return undefined for caller to handle
    }

    if (metric.dim() == 1) {
        // FIX 2025-01-10 Round19: Add TORCH_CHECK for size >= nz consistency with other overloads
        TORCH_CHECK(metric.size(0) >= nz,
            "extract1DVerticalMetric: 1D metric size ", metric.size(0),
            " is less than nz=", nz);
        // FIX 2025-01-10 Round19: Symmetric halo center-slice instead of front-slice
        if (metric.size(0) > nz) {
            int64_t halo = metric.size(0) - nz;
            if (halo % 2 == 0) {
                // Symmetric halo: center-slice
                return metric.slice(0, halo / 2, halo / 2 + nz);
            } else {
                // Asymmetric halo: warn and front-slice
                TORCH_WARN_ONCE("extract1DVerticalMetric: 1D metric has asymmetric halo (",
                                metric.size(0), " vs ", nz, "). Slicing from front.");
                return metric.slice(0, 0, nz);
            }
        }
        // Exact match: return as-is
        return metric;
    }

    if (metric.dim() == 2) {
        // 2D case: [nz, nx] or [ny, nz] - detect which dimension is k
        // FIX 2025-01-10 Round8: Allow k-dim halos (>= nz) and add FP32 upcast
        int64_t s0 = metric.size(0);
        int64_t s1 = metric.size(1);
        int64_t kdim = -1;
        int64_t k_size = 0;

        // Detect k-dimension: prefer exact match, then >= match
        // FIX 2025-01-10 Round9: Error when ambiguous (both dims >= nz, neither exact)
        if (s0 == nz) {
            kdim = 0; k_size = s0;  // [nz, nx] layout
        } else if (s1 == nz) {
            kdim = 1; k_size = s1;  // [ny, nz] layout
        } else if (s0 >= nz && s1 >= nz) {
            // FIX 2025-01-10 Round9: Both dims >= nz but neither equals nz exactly
            // Cannot determine which is k-dimension without grid context
            TORCH_CHECK(false,
                "extract1DVerticalMetric: Ambiguous 2D metric [", s0, ",", s1,
                "] - both dimensions >= nz=", nz, " but neither equals nz exactly. "
                "Cannot determine k-dimension layout. Use the 3-arg or 4-arg overload "
                "with ny/nx parameters for disambiguation.");
        } else if (s0 >= nz) {
            kdim = 0; k_size = s0;  // [nz+halo, nx] layout with k-halo
        } else if (s1 >= nz) {
            kdim = 1; k_size = s1;  // [ny, nz+halo] layout with k-halo
        } else {
            TORCH_CHECK(false,
                "extract1DVerticalMetric: 2D metric [", s0, ",", s1,
                "] has no dimension >= nz=", nz);
        }

        // Slice k-dimension if haloed
        torch::Tensor metric_sliced = metric;
        if (k_size > nz) {
            const int64_t halo_k = k_size - nz;
            TORCH_CHECK(halo_k % 2 == 0,
                "extract1DVerticalMetric: 2D metric k-halo must be symmetric (even), got halo_k=",
                halo_k, " for size(", kdim, ")=", k_size, " vs nz=", nz);
            int64_t k_start = halo_k / 2;
            metric_sliced = metric.slice(kdim, k_start, k_start + nz);
        }

        // FIX 2025-01-10 Round8: Upcast to FP32 for mean if FP16/BF16 to avoid underflow
        auto metric_for_mean = metric_sliced;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_sliced.to(torch::kFloat32);
        }
        auto reduced = (kdim == 0) ? metric_for_mean.mean(1) : metric_for_mean.mean(0);
        // Cast back to original dtype
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            reduced = reduced.to(metric.scalar_type());
        }
        return reduced.slice(0, 0, std::min(nz, reduced.size(0)));
    }

    if (metric.dim() == 3) {
        // FIX 2025-01-10 Round7: Handle halos in 3D path (like 4-arg overload)
        // Slice to interior before averaging to avoid mixing halos into mean
        torch::Tensor metric_interior = metric;
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);
        const int64_t m_nx = metric.size(2);

        // FIX 2025-01-10 Round9: Warn if horizontal dims suggest halos that cannot be sliced
        // Since 2-arg overload doesn't know ny/nx, we can only slice k-dimension
        // Horizontal halos contaminate the mean - recommend 3/4-arg overload
        if (m_ny > 1 && m_nx > 1) {
            TORCH_WARN_ONCE(
                "extract1DVerticalMetric (2-arg): 3D metric [", m_ny, ",", m_nz, ",", m_nx,
                "] has horizontal extent (ny>1, nx>1). Without ny/nx parameters, horizontal "
                "halos cannot be sliced and may contaminate the mean. Consider using the "
                "3-arg (nz,ny,nx) or 4-arg (nz,ny,nx,kdim) overload for proper halo handling.");
        }

        if (m_nz > nz) {
            // Haloed 3D metric - slice to interior
            // Since we only know nz (not ny/nx), slice only k-dimension
            const int64_t halo_z = m_nz - nz;
            TORCH_CHECK(halo_z % 2 == 0,
                "extract1DVerticalMetric: 3D metric k-halo must be symmetric (even), got halo_z=",
                halo_z, " for size(1)=", m_nz, " vs nz=", nz);
            int64_t k_start = halo_z / 2;
            metric_interior = metric.slice(1, k_start, k_start + nz);
        }
        // FIX 2025-01-10 Round8: Upcast to FP32 for mean if FP16/BF16 to avoid underflow
        auto metric_for_mean = metric_interior;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_interior.to(torch::kFloat32);
        }
        // OPT Pass34: Use explicit IntArrayRef to avoid mean() overload ambiguity
        auto result = metric_for_mean.mean(torch::IntArrayRef{0, 2}).slice(0, 0, std::min(nz, metric_interior.size(1)));
        // Cast back to original dtype
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            result = result.to(metric.scalar_type());
        }
        return result;
    }

    TORCH_CHECK(false,
        "extract1DVerticalMetric: unexpected metric dim=", metric.dim(),
        ", expected 1D/2D/3D");
}

/**
 * Extract 1D vertical metric with explicit kdim for 2D disambiguation.
 *
 * FIX 2025-01-10 Round14: Overload for when ny/nx are unknown but kdim is known.
 * FIX 2025-01-10 Round15: Added symmetric halo center-slice, FP16/BF16 upcast,
 *                         kdim axis validation, and kdim=-1 ambiguity check for haloed tensors.
 *
 * @param metric  Input 1D/2D/3D metric tensor
 * @param nz      Expected vertical dimension size
 * @param kdim    Explicit k-dimension for 2D (0=[nz,?], 1=[?,nz], -1=auto-detect non-cubic only)
 * @return        1D tensor [nz] suitable for vertical operations
 */
inline torch::Tensor extract1DVerticalMetric(const torch::Tensor& metric,
                                              int64_t nz, int64_t kdim) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();
    }

    // Helper: center-slice for symmetric halo, then upcast FP16/BF16 for mean
    auto sliceAndMean = [nz](const torch::Tensor& t, int64_t axis) -> torch::Tensor {
        const int64_t axis_size = t.size(axis);
        torch::Tensor sliced = t;

        // FIX Round16: Validate axis_size >= nz (prevents silent truncation)
        TORCH_CHECK(axis_size >= nz,
            "extract1DVerticalMetric: kdim axis size (", axis_size, ") < nz (", nz,
            "). Metric tensor too small for requested vertical dimension.");

        // FIX Round15: Symmetric halo center-slice if axis_size > nz
        if (axis_size > nz) {
            int64_t halo = axis_size - nz;
            TORCH_CHECK(halo % 2 == 0,
                "extract1DVerticalMetric: asymmetric halo on kdim axis (size=", axis_size,
                ", nz=", nz, ", halo=", halo, "). Use 4-arg overload with explicit ny/nx.");
            int64_t start = halo / 2;
            sliced = t.narrow(axis, start, nz);
        }

        // Squeeze or mean on non-k axis
        const int64_t other_axis = (axis == 0) ? 1 : 0;
        torch::Tensor result;
        if (sliced.size(other_axis) == 1) {
            result = sliced.squeeze(other_axis);
        } else {
            // FIX 2025-01-10 Round21: Warn when other-axis has multiple elements
            // Since we don't know expected ny/nx, we can't center-slice halo on other axis.
            // User should use 4-arg overload with explicit ny/nx for haloed inputs.
            TORCH_WARN_ONCE("extract1DVerticalMetric (2-arg kdim overload): other-axis size=",
                sliced.size(other_axis), " > 1. If this includes halo, use 4-arg overload "
                "with explicit ny/nx to properly center-slice before mean.");
            // FIX Round15: FP16/BF16 upcast to FP32 for mean (avoid underflow)
            torch::Tensor for_mean = sliced;
            bool needs_downcast = false;
            torch::ScalarType orig_dtype = sliced.scalar_type();
            if (orig_dtype == torch::kHalf || orig_dtype == torch::kBFloat16) {
                for_mean = sliced.to(torch::kFloat32);
                needs_downcast = true;
            }
            result = for_mean.mean(other_axis);
            if (needs_downcast) {
                result = result.to(orig_dtype);
            }
        }
        return result.slice(0, 0, std::min(nz, result.size(0)));
    };

    if (metric.dim() == 1) {
        // FIX 2025-01-10 Round24: Add size validation for consistency with 2D path
        TORCH_CHECK(metric.size(0) >= nz,
            "extract1DVerticalMetric: 1D metric size (", metric.size(0), ") < nz (", nz,
            "). Metric tensor too small for requested vertical dimension.");
        // 1D: symmetric halo center-slice
        if (metric.size(0) > nz) {
            int64_t halo = metric.size(0) - nz;
            if (halo % 2 == 0) {
                return metric.slice(0, halo / 2, halo / 2 + nz);
            }
            // Asymmetric: warn and slice from front (best effort)
            TORCH_WARN_ONCE("extract1DVerticalMetric: 1D asymmetric halo (size=", metric.size(0),
                            ", nz=", nz, "). Slicing from front.");
        }
        return metric.slice(0, 0, nz);  // Now guaranteed size >= nz
    }

    if (metric.dim() == 2) {
        const int64_t d0 = metric.size(0);
        const int64_t d1 = metric.size(1);

        // FIX Round15: Validate kdim axis length >= nz
        if (kdim == 0) {
            TORCH_CHECK(d0 >= nz,
                "extract1DVerticalMetric: kdim=0 axis size (", d0, ") < nz (", nz, ")");
            return sliceAndMean(metric, 0);
        } else if (kdim == 1) {
            TORCH_CHECK(d1 >= nz,
                "extract1DVerticalMetric: kdim=1 axis size (", d1, ") < nz (", nz, ")");
            return sliceAndMean(metric, 1);
        }

        // kdim == -1: Auto-detect for non-cubic only
        // FIX Round15: Error if both axes >= nz (haloed ambiguous case)
        if (d0 >= nz && d1 >= nz && d0 != d1) {
            // Both axes could be k-dim with halo - ambiguous
            TORCH_CHECK(false,
                "extract1DVerticalMetric: 2D metric shape [", d0, ",", d1,
                "] with both axes >= nz (", nz, ") is ambiguous with kdim=-1. "
                "Provide explicit kdim=0 or kdim=1.");
        }

        if (d0 != d1) {
            // Non-cubic, non-haloed: use heuristic
            if (d0 == nz || (d0 != nz && d1 != nz && d0 < d1)) {
                return sliceAndMean(metric, 0);
            } else {
                return sliceAndMean(metric, 1);
            }
        }

        // Cubic 2D with kdim=-1: error (ambiguous)
        TORCH_CHECK(false,
            "extract1DVerticalMetric: 2D metric shape [", d0, ",", d1,
            "] is ambiguous (cubic) and kdim=-1 (auto). Provide explicit kdim=0 or kdim=1.");
    }

    if (metric.dim() == 3) {
        // 3D: center-slice k-halo first, then average over horizontal dimensions
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);  // k is dim 1 in [ny, nz, nx]
        const int64_t m_nx = metric.size(2);

        // FIX Round16: Validate m_nz >= nz (consistent with other overloads)
        TORCH_CHECK(m_nz >= nz,
            "extract1DVerticalMetric: 3D k-dim size (", m_nz, ") < nz (", nz,
            "). Metric tensor too small for requested vertical dimension.");

        // FIX Round16: Warn about potential horizontal halo (3-arg can't slice it)
        if (m_ny > 1 && m_nx > 1) {
            TORCH_WARN_ONCE("extract1DVerticalMetric: 3D metric [", m_ny, ",", m_nz, ",", m_nx,
                            "] may include horizontal halo. Consider using 4-arg overload "
                            "with explicit ny/nx for accurate halo handling.");
        }

        torch::Tensor metric_sliced = metric;
        if (m_nz > nz) {
            int64_t k_halo = m_nz - nz;
            TORCH_CHECK(k_halo % 2 == 0,
                "extract1DVerticalMetric: 3D asymmetric k-halo (size=", m_nz,
                ", nz=", nz, "). Use 4-arg overload with explicit ny/nx.");
            int64_t k_start = k_halo / 2;
            metric_sliced = metric.narrow(1, k_start, nz);
        }
        // FIX Round15: FP16/BF16 upcast for mean
        torch::Tensor for_mean = metric_sliced;
        bool needs_downcast = false;
        torch::ScalarType orig_dtype = metric_sliced.scalar_type();
        if (orig_dtype == torch::kHalf || orig_dtype == torch::kBFloat16) {
            for_mean = metric_sliced.to(torch::kFloat32);
            needs_downcast = true;
        }
        // OPT Pass34: Use explicit IntArrayRef
        auto result = for_mean.mean(torch::IntArrayRef{0, 2});
        if (needs_downcast) {
            result = result.to(orig_dtype);
        }
        return result.slice(0, 0, std::min(nz, result.size(0)));
    }

    TORCH_CHECK(false,
        "extract1DVerticalMetric: unexpected metric dim=", metric.dim(),
        ", expected 1D/2D/3D");
}

/**
 * Extract 1D vertical metric with explicit grid dimensions for 2D disambiguation.
 *
 * FIX 2025-12-26: Overload to resolve ambiguity when nz==nx or nz==ny.
 * Use this when 2D metric could be [nz,nx] or [ny,nz] and dimensions match.
 *
 * @param metric  Input 1D/2D/3D metric tensor
 * @param nz      Expected vertical dimension size
 * @param ny      Grid ny dimension (for 2D disambiguation)
 * @param nx      Grid nx dimension (for 2D disambiguation)
 * @return        1D tensor [nz] suitable for vertical operations
 */
inline torch::Tensor extract1DVerticalMetric(const torch::Tensor& metric,
                                              int64_t nz, int64_t ny, int64_t nx) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();
    }

    if (metric.dim() == 1) {
        // FIX 2025-01-10 Round19: Add TORCH_CHECK for size >= nz to detect incomplete metrics early
        TORCH_CHECK(metric.size(0) >= nz,
            "extract1DVerticalMetric(3-arg): 1D metric size ", metric.size(0),
            " is less than nz=", nz);
        // FIX 2025-01-10 Round17: Center-slice for symmetric halo (consistent with other overloads)
        if (metric.size(0) > nz) {
            int64_t halo = metric.size(0) - nz;
            if (halo % 2 == 0) {
                return metric.slice(0, halo / 2, halo / 2 + nz);
            }
            TORCH_WARN_ONCE("extract1DVerticalMetric: 1D asymmetric halo (size=",
                            metric.size(0), ", nz=", nz, "). Slicing from front.");
            return metric.slice(0, 0, nz);
        }
        // Exact match: return as-is
        return metric;
    }

    if (metric.dim() == 2) {
        // FIX 2025-12-26: Disambiguate 2D using both dimensions
        // [nz, nx] layout: size(0)==nz AND size(1)==nx
        // [ny, nz] layout: size(0)==ny AND size(1)==nz
        int64_t s0 = metric.size(0), s1 = metric.size(1);
        int64_t kdim = -1;

        // FIX 2025-01-11 Round27: Include haloed cases in layout detection
        // Allow s0 >= nz (haloed nz) or s1 >= nx/nz (haloed)
        bool is_nz_nx = (s0 >= nz && s1 >= nx);
        bool is_ny_nz = (s0 >= ny && s1 >= nz);

        // For exact match, use the original logic
        bool exact_nz_nx = (s0 == nz && s1 == nx);
        bool exact_ny_nz = (s0 == ny && s1 == nz);

        if (exact_nz_nx && !exact_ny_nz) {
            kdim = 0;  // [nz, nx] layout
        } else if (exact_ny_nz && !exact_nz_nx) {
            kdim = 1;  // [ny, nz] layout
        } else if (exact_nz_nx && exact_ny_nz) {
            // FIX 2025-12-26: Error for ambiguous cubic grid case
            // Both [nz,nx] and [ny,nz] patterns match (ny==nz or nx==nz)
            // Force caller to use 4-arg overload with explicit kdim
            TORCH_CHECK(false,
                "extract1DVerticalMetric: Ambiguous 2D metric [", s0, ",", s1,
                "] for cubic grid [ny=", ny, ", nz=", nz, ", nx=", nx, "]. "
                "Cannot distinguish [nz,nx] from [ny,nz] layout. "
                "Use the 4-arg overload with explicit kdim parameter.");
        } else if (!exact_nz_nx && !exact_ny_nz) {
            // FIX 2025-01-11 Round27: Try haloed detection
            // If one pattern matches with halos but not the other, use that
            if (is_nz_nx && !is_ny_nz) {
                kdim = 0;  // [nz+halo, nx+halo] layout
            } else if (is_ny_nz && !is_nz_nx) {
                kdim = 1;  // [ny+halo, nz+halo] layout
            } else if (is_nz_nx && is_ny_nz) {
                // Both haloed patterns match - ambiguous
                TORCH_CHECK(false,
                    "extract1DVerticalMetric: Ambiguous haloed 2D metric [", s0, ",", s1,
                    "] for grid [ny=", ny, ", nz=", nz, ", nx=", nx, "]. "
                    "Use the 4-arg overload with explicit kdim parameter.");
            } else {
                // Fall back to nz-dimension matching
                if (s0 >= nz) kdim = 0;
                else if (s1 >= nz) kdim = 1;
                else {
                    TORCH_CHECK(false,
                        "extract1DVerticalMetric: 2D metric [", s0, ",", s1,
                        "] doesn't match grid [ny=", ny, ", nz=", nz, ", nx=", nx, "]");
                }
            }
        }

        // FIX 2025-01-11 Round27: Slice out halos before taking mean (like 4-arg version)
        torch::Tensor metric_sliced = metric;
        {
            int64_t k_size = metric.size(kdim);
            int64_t other_dim = 1 - kdim;
            int64_t other_size = metric.size(other_dim);
            int64_t expected_other = (kdim == 0) ? nx : ny;

            // Slice k-dimension if haloed
            if (k_size > nz) {
                const int64_t halo_k = k_size - nz;
                TORCH_CHECK(halo_k % 2 == 0,
                    "extract1DVerticalMetric: 2D metric k-dim halo must be symmetric (even), got halo_k=",
                    halo_k, " for size(", kdim, ")=", k_size, " vs nz=", nz);
                int64_t k_start = halo_k / 2;
                metric_sliced = metric_sliced.slice(kdim, k_start, k_start + nz);
            }

            // Slice other dimension if haloed
            if (other_size > expected_other) {
                const int64_t halo_other = other_size - expected_other;
                TORCH_CHECK(halo_other % 2 == 0,
                    "extract1DVerticalMetric: 2D metric other-dim halo must be symmetric (even), got halo_other=",
                    halo_other, " for size(", other_dim, ")=", other_size, " vs expected=", expected_other);
                int64_t other_start = halo_other / 2;
                metric_sliced = metric_sliced.slice(other_dim, other_start, other_start + expected_other);
            }
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_sliced;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_sliced.to(torch::kFloat32);
        }
        auto reduced = (kdim == 0) ? metric_for_mean.mean(1) : metric_for_mean.mean(0);
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            reduced = reduced.to(metric.scalar_type());
        }
        return reduced.slice(0, 0, std::min(nz, reduced.size(0)));
    }

    if (metric.dim() == 3) {
        // 3D: [ny, nz, nx] - FIX 2025-12-26: Validate full 3D layout before averaging
        // FIX 2025-01-10 Round7: Allow halo regions (>=) and slice to interior (like 4-arg)
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);
        const int64_t m_nx = metric.size(2);
        TORCH_CHECK(m_ny >= ny && m_nz >= nz && m_nx >= nx,
            "extract1DVerticalMetric: 3D metric expected >= [ny=", ny, ", nz=", nz, ", nx=", nx,
            "] but got [", m_ny, ",", m_nz, ",", m_nx, "].");

        // Slice to interior if halos present
        torch::Tensor metric_interior = metric;
        if (m_ny > ny || m_nz > nz || m_nx > nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_z = m_nz - nz;
            const int64_t halo_x = m_nx - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "extract1DVerticalMetric: 3D metric halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_z=", halo_z, ", halo_x=", halo_x);
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            metric_interior = metric.slice(0, j_start, j_start + ny)
                                    .slice(1, k_start, k_start + nz)
                                    .slice(2, i_start, i_start + nx);
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_interior;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_interior.to(torch::kFloat32);
        }
        // OPT Pass34: Use explicit IntArrayRef
        auto result = metric_for_mean.mean(torch::IntArrayRef{0, 2}).slice(0, 0, std::min(nz, metric_interior.size(1)));
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            result = result.to(metric.scalar_type());
        }
        return result;
    }

    TORCH_CHECK(false, "extract1DVerticalMetric: unexpected dim=", metric.dim());
}

/**
 * Extract 1D vertical metric with explicit k-dimension specification.
 * FIX 2025-12-26: For cubic grids (ny==nx==nz), the 3-arg overload is ambiguous.
 * Use this 4-arg version when the layout is known explicitly.
 *
 * @param metric  Input tensor (1D, 2D, or 3D)
 * @param nz      Grid nz dimension (vertical levels)
 * @param ny      Grid ny dimension
 * @param nx      Grid nx dimension
 * @param kdim    Explicit k-dimension for 2D tensors:
 *                  0 = [nz, nx] layout (k is first dimension)
 *                  1 = [ny, nz] layout (k is second dimension)
 *                  -1 = auto-detect (same as 3-arg version)
 * @return        1D tensor [nz] suitable for vertical operations
 */
inline torch::Tensor extract1DVerticalMetric(const torch::Tensor& metric,
                                              int64_t nz, int64_t ny, int64_t nx,
                                              int64_t kdim) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();
    }

    if (metric.dim() == 1) {
        // FIX 2025-01-10 Round19: Add TORCH_CHECK for size >= nz to detect incomplete metrics early
        TORCH_CHECK(metric.size(0) >= nz,
            "extract1DVerticalMetric(4-arg): 1D metric size ", metric.size(0),
            " is less than nz=", nz);
        // FIX 2025-01-10 Round17: Center-slice for symmetric halo
        if (metric.size(0) > nz) {
            int64_t halo = metric.size(0) - nz;
            if (halo % 2 == 0) {
                return metric.slice(0, halo / 2, halo / 2 + nz);
            }
            TORCH_WARN_ONCE("extract1DVerticalMetric: 1D asymmetric halo (size=",
                            metric.size(0), ", nz=", nz, "). Slicing from front.");
            return metric.slice(0, 0, nz);
        }
        // Exact match: return as-is
        return metric;
    }

    if (metric.dim() == 2) {
        // FIX 2025-12-26: Use explicit kdim if provided (not -1)
        int64_t effective_kdim = kdim;

        // FIX 2025-12-26: Validate explicit kdim matches nz before using
        // FIX 2025-12-27: Add cross-dimension validation for layout mismatch detection
        // FIX 2025-01-10 Round9: Allow halos (>= nz) and slice before mean
        if (kdim >= 0) {
            TORCH_CHECK(kdim < metric.dim(),
                "extract1DVerticalMetric: kdim=", kdim,
                " out of range for 2D metric with dim=", metric.dim());
            // FIX 2025-01-10 Round9: Allow k-dim halos (>= nz)
            TORCH_CHECK(metric.size(kdim) >= nz,
                "extract1DVerticalMetric: metric.size(kdim=", kdim, ")=", metric.size(kdim),
                " is less than nz=", nz,
                ". Verify kdim corresponds to vertical dimension.");

            // FIX 2025-12-27: Validate other_dim (allow halos)
            // FIX 2025-01-10 Round9: Change from warn to allow >= for halos
            int64_t other_dim = 1 - kdim;  // 0→1, 1→0
            int64_t expected_other = (kdim == 0) ? nx : ny;
            TORCH_CHECK(metric.size(other_dim) >= expected_other,
                "extract1DVerticalMetric: 2D metric.size(", other_dim, ")=", metric.size(other_dim),
                " is less than expected ", (kdim == 0 ? "nx" : "ny"), "=", expected_other,
                " for kdim=", kdim, " layout.");
        }

        if (kdim == -1) {
            // Auto-detect like 3-arg version
            // FIX 2025-01-10 Round9: Support haloed dimensions (>= matches)
            int64_t s0 = metric.size(0), s1 = metric.size(1);

            // Check exact matches first (preferred)
            bool exact_nz_nx = (s0 == nz && s1 == nx);
            bool exact_ny_nz = (s0 == ny && s1 == nz);
            // Check haloed matches (>= with valid halo)
            bool halo_nz_nx = (s0 >= nz && s1 >= nx && !exact_nz_nx);
            bool halo_ny_nz = (s0 >= ny && s1 >= nz && !exact_ny_nz);

            if (exact_nz_nx && !exact_ny_nz) {
                effective_kdim = 0;
            } else if (exact_ny_nz && !exact_nz_nx) {
                effective_kdim = 1;
            } else if (exact_nz_nx && exact_ny_nz) {
                // FIX 2025-12-26: Ambiguous cubic grid - require explicit kdim
                TORCH_CHECK(false,
                    "extract1DVerticalMetric: Ambiguous 2D metric [", s0, ",", s1,
                    "] for cubic grid [ny=", ny, ", nz=", nz, ", nx=", nx, "]. "
                    "Use explicit kdim=0 or kdim=1 parameter.");
            } else if (halo_nz_nx && !halo_ny_nz) {
                // [nz+halo, nx+halo] pattern - can only be kdim=0
                effective_kdim = 0;
            } else if (halo_ny_nz && !halo_nz_nx) {
                // [ny+halo, nz+halo] pattern - can only be kdim=1
                effective_kdim = 1;
            } else if (halo_nz_nx && halo_ny_nz) {
                // FIX 2025-01-10 Round9: Both haloed patterns match - ambiguous
                TORCH_CHECK(false,
                    "extract1DVerticalMetric: Ambiguous haloed 2D metric [", s0, ",", s1,
                    "] - both [nz+halo,nx+halo] and [ny+halo,nz+halo] patterns possible. "
                    "Use explicit kdim parameter.");
            } else {
                // Fallback: exact k-dimension match only
                if (s0 == nz) effective_kdim = 0;
                else if (s1 == nz) effective_kdim = 1;
                else {
                    TORCH_CHECK(false,
                        "extract1DVerticalMetric: 2D metric [", s0, ",", s1,
                        "] doesn't match grid [ny=", ny, ", nz=", nz, ", nx=", nx, "]");
                }
            }
        }

        TORCH_CHECK(effective_kdim == 0 || effective_kdim == 1,
            "extract1DVerticalMetric: kdim must be 0, 1, or -1 (auto), got ", kdim);

        // FIX 2025-01-10 Round9: Slice halos on both dimensions before mean
        // Layout: kdim=0 → [nz(+halo_k), nx(+halo_other)]
        //         kdim=1 → [ny(+halo_other), nz(+halo_k)]
        torch::Tensor metric_sliced = metric;
        {
            int64_t k_size = metric.size(effective_kdim);
            int64_t other_dim = 1 - effective_kdim;
            int64_t other_size = metric.size(other_dim);
            int64_t expected_other = (effective_kdim == 0) ? nx : ny;

            // Slice k-dimension if haloed
            if (k_size > nz) {
                const int64_t halo_k = k_size - nz;
                TORCH_CHECK(halo_k % 2 == 0,
                    "extract1DVerticalMetric: 2D metric k-dim halo must be symmetric (even), got halo_k=",
                    halo_k, " for size(", effective_kdim, ")=", k_size, " vs nz=", nz);
                int64_t k_start = halo_k / 2;
                metric_sliced = metric_sliced.slice(effective_kdim, k_start, k_start + nz);
            }

            // Slice other dimension if haloed
            if (other_size > expected_other) {
                const int64_t halo_other = other_size - expected_other;
                TORCH_CHECK(halo_other % 2 == 0,
                    "extract1DVerticalMetric: 2D metric other-dim halo must be symmetric (even), got halo_other=",
                    halo_other, " for size(", other_dim, ")=", other_size, " vs expected=", expected_other);
                int64_t other_start = halo_other / 2;
                metric_sliced = metric_sliced.slice(other_dim, other_start, other_start + expected_other);
            }
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_sliced;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_sliced.to(torch::kFloat32);
        }
        auto reduced = (effective_kdim == 0) ? metric_for_mean.mean(1) : metric_for_mean.mean(0);
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            reduced = reduced.to(metric.scalar_type());
        }
        return reduced.slice(0, 0, std::min(nz, reduced.size(0)));
    }

    if (metric.dim() == 3) {
        // 3D: [ny, nz, nx] - kdim is ignored, always use middle dimension
        // FIX 2025-12-26: Validate 3D layout before averaging (all dimensions)
        // FIX 2025-01-10 Round6: Allow halo regions (>=) and slice to interior
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);
        const int64_t m_nx = metric.size(2);
        TORCH_CHECK(m_ny >= ny && m_nz >= nz && m_nx >= nx,
            "extract1DVerticalMetric: 3D metric expected >= [ny=", ny, ", nz=", nz, ", nx=", nx,
            "] but got [", m_ny, ",", m_nz, ",", m_nx, "].");

        // Slice to interior if halos present
        torch::Tensor metric_interior = metric;
        if (m_ny > ny || m_nz > nz || m_nx > nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_z = m_nz - nz;
            const int64_t halo_x = m_nx - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "extract1DVerticalMetric: 3D metric halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_z=", halo_z, ", halo_x=", halo_x);
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            metric_interior = metric.slice(0, j_start, j_start + ny)
                                    .slice(1, k_start, k_start + nz)
                                    .slice(2, i_start, i_start + nx);
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_interior;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_interior.to(torch::kFloat32);
        }
        // OPT Pass34: Use explicit IntArrayRef
        auto result = metric_for_mean.mean(torch::IntArrayRef{0, 2}).slice(0, 0, std::min(nz, metric_interior.size(1)));
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            result = result.to(metric.scalar_type());
        }
        return result;
    }

    TORCH_CHECK(false, "extract1DVerticalMetric: unexpected dim=", metric.dim());
}

// ============================================================================
// W-Level Vertical Metric Extraction (for nz+1 quantities)
// ============================================================================
/**
 * Extract 1D vertical metric for W-level quantities (z_at_w, etc.).
 *
 * FIX 2025-12-27: Specialized extractor for w-level metrics with nz+1 elements.
 *
 * WRF W-Level Convention:
 * - z_at_w: [nz+1] heights at vertical velocity levels
 * - w: [ny, nz+1, nx] vertical velocity
 * - w-level = mass-level + 1/2 (staggered grid)
 *
 * This function expects the vertical dimension to have nz+1 elements,
 * NOT nz. Use extract1DVerticalMetric() for mass-level quantities (dn, dnw).
 *
 * Dimension handling:
 * - 1D [nz+1]: Return slice(0, 0, nz+1)
 * - 2D [nz+1,nx] or [ny,nz+1]: Detect k-axis by matching nz+1, reduce via mean
 * - 3D [ny,nz+1,nx]: mean({0,2}) to get [nz+1]
 *
 * @param metric  Input w-level metric tensor (z_at_w, etc.)
 * @param nz      Grid nz dimension (function uses nz+1 internally)
 * @param ny      Grid ny dimension (for 2D disambiguation)
 * @param nx      Grid nx dimension (for 2D disambiguation)
 * @param kdim    Explicit k-dimension for 2D tensors:
 *                  0 = [nz+1, nx] layout (k is first dimension)
 *                  1 = [ny, nz+1] layout (k is second dimension)
 *                  -1 = auto-detect (may fail on cubic grids)
 * @return        1D tensor [nz+1] suitable for w-level operations
 */

/**
 * Extract 1D w-level metric with explicit kdim for 2D disambiguation.
 *
 * FIX 2025-01-10 Round16: Overload for when ny/nx are unknown but kdim is known.
 * Mirrors extract1DVerticalMetric(metric, nz, kdim) but for w-level (nz+1) metrics.
 *
 * @param metric  Input w-level metric tensor
 * @param nz      Grid nz dimension (function uses nz+1 internally)
 * @param kdim    Explicit k-dimension for 2D (0=[nz+1,?], 1=[?,nz+1], -1=auto non-cubic)
 * @return        1D tensor [nz+1] suitable for w-level operations
 */
inline torch::Tensor extract1DVerticalMetricForW(const torch::Tensor& metric,
                                                  int64_t nz, int64_t kdim) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();
    }

    const int64_t nz_w = nz + 1;  // W-level has nz+1 elements

    // Helper: center-slice for symmetric halo, then upcast FP16/BF16 for mean
    auto sliceAndMean = [nz_w](const torch::Tensor& t, int64_t axis) -> torch::Tensor {
        const int64_t axis_size = t.size(axis);
        torch::Tensor sliced = t;

        // Validate axis_size >= nz_w
        TORCH_CHECK(axis_size >= nz_w,
            "extract1DVerticalMetricForW: kdim axis size (", axis_size, ") < nz+1 (", nz_w,
            "). Metric tensor too small for requested w-level dimension.");

        // Symmetric halo center-slice if axis_size > nz_w
        if (axis_size > nz_w) {
            int64_t halo = axis_size - nz_w;
            TORCH_CHECK(halo % 2 == 0,
                "extract1DVerticalMetricForW: asymmetric halo on kdim axis (size=", axis_size,
                ", nz+1=", nz_w, ", halo=", halo, "). Use 4-arg overload with explicit ny/nx.");
            int64_t start = halo / 2;
            sliced = t.narrow(axis, start, nz_w);
        }

        // Squeeze or mean on non-k axis
        const int64_t other_axis = (axis == 0) ? 1 : 0;
        torch::Tensor result;
        if (sliced.size(other_axis) == 1) {
            result = sliced.squeeze(other_axis);
        } else {
            // FIX 2025-01-10 Round21: Warn when other-axis has multiple elements
            // Since we don't know expected ny/nx, we can't center-slice halo on other axis.
            // User should use 4-arg overload with explicit ny/nx for haloed inputs.
            TORCH_WARN_ONCE("extract1DVerticalMetricForW (2-arg kdim overload): other-axis size=",
                sliced.size(other_axis), " > 1. If this includes halo, use 4-arg overload "
                "with explicit ny/nx to properly center-slice before mean.");
            // FP16/BF16 upcast to FP32 for mean
            torch::Tensor for_mean = sliced;
            bool needs_downcast = false;
            torch::ScalarType orig_dtype = sliced.scalar_type();
            if (orig_dtype == torch::kHalf || orig_dtype == torch::kBFloat16) {
                for_mean = sliced.to(torch::kFloat32);
                needs_downcast = true;
            }
            result = for_mean.mean(other_axis);
            if (needs_downcast) {
                result = result.to(orig_dtype);
            }
        }
        return result.slice(0, 0, std::min(nz_w, result.size(0)));
    };

    if (metric.dim() == 1) {
        TORCH_CHECK(metric.size(0) >= nz_w,
            "extract1DVerticalMetricForW: 1D metric size (", metric.size(0),
            ") < nz+1 (", nz_w, ")");
        if (metric.size(0) > nz_w) {
            int64_t halo = metric.size(0) - nz_w;
            if (halo % 2 == 0) {
                return metric.slice(0, halo / 2, halo / 2 + nz_w);
            }
            TORCH_WARN_ONCE("extract1DVerticalMetricForW: 1D asymmetric halo (size=",
                            metric.size(0), ", nz+1=", nz_w, "). Slicing from front.");
        }
        return metric.slice(0, 0, std::min(nz_w, metric.size(0)));
    }

    if (metric.dim() == 2) {
        const int64_t d0 = metric.size(0);
        const int64_t d1 = metric.size(1);

        if (kdim == 0) {
            return sliceAndMean(metric, 0);
        } else if (kdim == 1) {
            return sliceAndMean(metric, 1);
        }

        // kdim == -1: Auto-detect
        if (d0 >= nz_w && d1 >= nz_w && d0 != d1) {
            TORCH_CHECK(false,
                "extract1DVerticalMetricForW: 2D metric shape [", d0, ",", d1,
                "] with both axes >= nz+1 (", nz_w, ") is ambiguous with kdim=-1. "
                "Provide explicit kdim=0 or kdim=1.");
        }

        if (d0 != d1) {
            if (d0 == nz_w || (d0 != nz_w && d1 != nz_w && d0 < d1)) {
                return sliceAndMean(metric, 0);
            } else {
                return sliceAndMean(metric, 1);
            }
        }

        TORCH_CHECK(false,
            "extract1DVerticalMetricForW: 2D metric shape [", d0, ",", d1,
            "] is ambiguous (cubic) and kdim=-1. Provide explicit kdim=0 or kdim=1.");
    }

    if (metric.dim() == 3) {
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);
        const int64_t m_nx = metric.size(2);

        TORCH_CHECK(m_nz >= nz_w,
            "extract1DVerticalMetricForW: 3D k-dim size (", m_nz, ") < nz+1 (", nz_w, ")");

        if (m_ny > 1 && m_nx > 1) {
            TORCH_WARN_ONCE("extract1DVerticalMetricForW: 3D metric [", m_ny, ",", m_nz, ",", m_nx,
                            "] may include horizontal halo. Consider 4-arg overload.");
        }

        torch::Tensor metric_sliced = metric;
        if (m_nz > nz_w) {
            int64_t k_halo = m_nz - nz_w;
            TORCH_CHECK(k_halo % 2 == 0,
                "extract1DVerticalMetricForW: 3D asymmetric k-halo (size=", m_nz,
                ", nz+1=", nz_w, "). Use 4-arg overload.");
            metric_sliced = metric.narrow(1, k_halo / 2, nz_w);
        }

        torch::Tensor for_mean = metric_sliced;
        bool needs_downcast = false;
        torch::ScalarType orig_dtype = metric_sliced.scalar_type();
        if (orig_dtype == torch::kHalf || orig_dtype == torch::kBFloat16) {
            for_mean = metric_sliced.to(torch::kFloat32);
            needs_downcast = true;
        }
        // OPT Pass34: Use explicit IntArrayRef
        auto result = for_mean.mean(torch::IntArrayRef{0, 2});
        if (needs_downcast) {
            result = result.to(orig_dtype);
        }
        return result.slice(0, 0, std::min(nz_w, result.size(0)));
    }

    TORCH_CHECK(false,
        "extract1DVerticalMetricForW: unexpected metric dim=", metric.dim());
}

inline torch::Tensor extract1DVerticalMetricForW(const torch::Tensor& metric,
                                                  int64_t nz, int64_t ny, int64_t nx,
                                                  int64_t kdim = -1) {
    if (!metric.defined() || metric.numel() == 0) {
        return torch::Tensor();
    }

    const int64_t nz_w = nz + 1;  // W-level count

    if (metric.dim() == 1) {
        // Already 1D, ensure correct size for w-levels
        TORCH_CHECK(metric.size(0) >= nz_w,
            "extract1DVerticalMetricForW: 1D metric size ", metric.size(0),
            " is less than expected nz+1=", nz_w);
        // FIX 2025-01-10 Round17: Center-slice for symmetric halo
        if (metric.size(0) > nz_w) {
            int64_t halo = metric.size(0) - nz_w;
            if (halo % 2 == 0) {
                return metric.slice(0, halo / 2, halo / 2 + nz_w);
            }
            TORCH_WARN_ONCE("extract1DVerticalMetricForW: 1D asymmetric halo (size=",
                            metric.size(0), ", nz+1=", nz_w, "). Slicing from front.");
        }
        return metric.slice(0, 0, nz_w);
    }

    if (metric.dim() == 2) {
        int64_t effective_kdim = kdim;
        int64_t s0 = metric.size(0), s1 = metric.size(1);

        // Validate explicit kdim if provided
        // FIX 2025-01-10 Round10: Allow halos (>= nz_w) and validate other dim
        if (kdim >= 0) {
            TORCH_CHECK(kdim < metric.dim(),
                "extract1DVerticalMetricForW: kdim=", kdim,
                " out of range for 2D metric with dim=", metric.dim());
            TORCH_CHECK(metric.size(kdim) >= nz_w,
                "extract1DVerticalMetricForW: metric.size(kdim=", kdim, ")=",
                metric.size(kdim), " is less than nz+1=", nz_w,
                ". Verify kdim corresponds to w-level vertical dimension.");
            // Validate other dimension (allow halos)
            int64_t other_dim = 1 - kdim;
            int64_t expected_other = (kdim == 0) ? nx : ny;
            TORCH_CHECK(metric.size(other_dim) >= expected_other,
                "extract1DVerticalMetricForW: metric.size(", other_dim, ")=",
                metric.size(other_dim), " is less than expected ",
                (kdim == 0 ? "nx" : "ny"), "=", expected_other);
        }

        if (kdim == -1) {
            // Auto-detect based on nz+1
            // FIX 2025-01-10 Round10: Support haloed dimensions (>= matches)
            // Check exact matches first (preferred)
            bool exact_nzw_nx = (s0 == nz_w && s1 == nx);
            bool exact_ny_nzw = (s0 == ny && s1 == nz_w);
            // Check haloed matches
            bool halo_nzw_nx = (s0 >= nz_w && s1 >= nx && !exact_nzw_nx);
            bool halo_ny_nzw = (s0 >= ny && s1 >= nz_w && !exact_ny_nzw);

            if (exact_nzw_nx && !exact_ny_nzw) {
                effective_kdim = 0;  // [nz+1, nx] layout
            } else if (exact_ny_nzw && !exact_nzw_nx) {
                effective_kdim = 1;  // [ny, nz+1] layout
            } else if (exact_nzw_nx && exact_ny_nzw) {
                // Cubic-like grid: ny == nz+1 or nx == nz+1
                TORCH_CHECK(false,
                    "extract1DVerticalMetricForW: Ambiguous 2D metric [", s0, ",", s1,
                    "] for w-level grid [ny=", ny, ", nz+1=", nz_w, ", nx=", nx, "]. "
                    "Use explicit kdim=0 or kdim=1 parameter.");
            } else if (halo_nzw_nx && !halo_ny_nzw) {
                effective_kdim = 0;  // [nz+1+halo, nx+halo] layout
            } else if (halo_ny_nzw && !halo_nzw_nx) {
                effective_kdim = 1;  // [ny+halo, nz+1+halo] layout
            } else if (halo_nzw_nx && halo_ny_nzw) {
                TORCH_CHECK(false,
                    "extract1DVerticalMetricForW: Ambiguous haloed 2D metric [", s0, ",", s1,
                    "] - both patterns possible. Use explicit kdim parameter.");
            } else {
                // Fallback: exact k-dimension match only
                if (s0 == nz_w) effective_kdim = 0;
                else if (s1 == nz_w) effective_kdim = 1;
                else {
                    TORCH_CHECK(false,
                        "extract1DVerticalMetricForW: 2D metric [", s0, ",", s1,
                        "] doesn't match w-level grid [ny=", ny, ", nz+1=", nz_w, ", nx=", nx, "]");
                }
            }
        }

        TORCH_CHECK(effective_kdim == 0 || effective_kdim == 1,
            "extract1DVerticalMetricForW: kdim must be 0, 1, or -1 (auto), got ", kdim);

        // FIX 2025-01-10 Round10: Slice halos on both dimensions before mean
        // Layout: kdim=0 → [nz+1(+halo_k), nx(+halo_other)]
        //         kdim=1 → [ny(+halo_other), nz+1(+halo_k)]
        torch::Tensor metric_sliced = metric;
        {
            int64_t k_size = metric.size(effective_kdim);
            int64_t other_dim = 1 - effective_kdim;
            int64_t other_size = metric.size(other_dim);
            int64_t expected_other = (effective_kdim == 0) ? nx : ny;

            // Slice k-dimension if haloed (k-size > nz_w)
            if (k_size > nz_w) {
                const int64_t halo_k = k_size - nz_w;
                TORCH_CHECK(halo_k % 2 == 0,
                    "extract1DVerticalMetricForW: 2D metric k-dim halo must be symmetric (even), got halo_k=",
                    halo_k, " for size(", effective_kdim, ")=", k_size, " vs nz+1=", nz_w);
                int64_t k_start = halo_k / 2;
                metric_sliced = metric_sliced.slice(effective_kdim, k_start, k_start + nz_w);
            }

            // Slice other dimension if haloed
            if (other_size > expected_other) {
                const int64_t halo_other = other_size - expected_other;
                TORCH_CHECK(halo_other % 2 == 0,
                    "extract1DVerticalMetricForW: 2D metric other-dim halo must be symmetric (even), got halo_other=",
                    halo_other, " for size(", other_dim, ")=", other_size, " vs expected=", expected_other);
                int64_t other_start = halo_other / 2;
                metric_sliced = metric_sliced.slice(other_dim, other_start, other_start + expected_other);
            }
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_sliced;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_sliced.to(torch::kFloat32);
        }
        auto reduced = (effective_kdim == 0) ? metric_for_mean.mean(1) : metric_for_mean.mean(0);
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            reduced = reduced.to(metric.scalar_type());
        }
        return reduced.slice(0, 0, std::min(nz_w, reduced.size(0)));
    }

    if (metric.dim() == 3) {
        // 3D: [ny, nz+1, nx] - validate full layout
        // FIX 2025-01-10 Round6: Allow halo regions (>=) and slice to interior
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz_w = metric.size(1);
        const int64_t m_nx = metric.size(2);
        TORCH_CHECK(m_ny >= ny && m_nz_w >= nz_w && m_nx >= nx,
            "extract1DVerticalMetricForW: 3D metric expected >= [ny=", ny, ", nz+1=", nz_w,
            ", nx=", nx, "] but got [", m_ny, ",", m_nz_w, ",", m_nx, "].");

        // Slice to interior if halos present
        torch::Tensor metric_interior = metric;
        if (m_ny > ny || m_nz_w > nz_w || m_nx > nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_z = m_nz_w - nz_w;
            const int64_t halo_x = m_nx - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "extract1DVerticalMetricForW: 3D metric halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_z=", halo_z, ", halo_x=", halo_x);
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            metric_interior = metric.slice(0, j_start, j_start + ny)
                                    .slice(1, k_start, k_start + nz_w)
                                    .slice(2, i_start, i_start + nx);
        }

        // FIX 2025-12-28: Upcast to FP32 for mean if FP16 to avoid underflow
        auto metric_for_mean = metric_interior;
        if (metric.scalar_type() == torch::kHalf || metric.scalar_type() == torch::kBFloat16) {
            metric_for_mean = metric_interior.to(torch::kFloat32);
        }
        // OPT Pass34: Use explicit IntArrayRef
        auto result = metric_for_mean.mean(torch::IntArrayRef{0, 2}).slice(0, 0, nz_w);
        // Cast back to original dtype for consistency
        if (metric.scalar_type() != metric_for_mean.scalar_type()) {
            result = result.to(metric.scalar_type());
        }
        return result;
    }

    TORCH_CHECK(false, "extract1DVerticalMetricForW: unexpected dim=", metric.dim());
}

// =============================================================================
// FIX 2025-01-10 Round13 OPT: Common helpers for fallback_options device/dtype
// =============================================================================

/**
 * Apply fallback device/dtype to TensorOptions if specified.
 * Used when creating new tensors with fallback configuration.
 */
inline torch::TensorOptions applyFallbackToOptions(
    torch::TensorOptions opts,
    const torch::TensorOptions& fallback_options) {
    if (fallback_options.has_device()) opts = opts.device(fallback_options.device());
    if (fallback_options.has_dtype()) opts = opts.dtype(fallback_options.dtype());
    return opts;
}

/**
 * Apply fallback device/dtype to existing tensor if different.
 * Returns tensor moved to fallback device/dtype, or original if no change needed.
 */
inline torch::Tensor applyFallbackToTensor(
    torch::Tensor tensor,
    const torch::TensorOptions& fallback_options) {
    // FIX 2025-01-10 Round17 OPT: Combine device+dtype into single .to() call to avoid redundant copy
    bool needs_device = fallback_options.has_device() && tensor.device() != fallback_options.device();
    bool needs_dtype = fallback_options.has_dtype() &&
                       tensor.scalar_type() != fallback_options.dtype().toScalarType();

    if (needs_device && needs_dtype) {
        // Both: single .to(device, dtype) call
        return tensor.to(fallback_options.device(), fallback_options.dtype().toScalarType());
    } else if (needs_device) {
        return tensor.to(fallback_options.device());
    } else if (needs_dtype) {
        return tensor.to(fallback_options.dtype().toScalarType());
    }
    return tensor;
}

/**
 * Normalize metric tensor for 3D broadcasting.
 * Applies NaN/Inf/negative correction, then expands to [ny, nz, nx].
 *
 * FIX 2025-12-27: Handle 1D/2D/3D input with proper preservation.
 * - If 3D input matches [ny,nz,nx], preserve horizontal variation (no 1D extraction)
 * - Only extract to 1D for 1D/2D inputs
 *
 * Input handling:
 * - 1D [nz]: Normalize and expand to 3D (original behavior)
 * - 2D [nz,nx] or [ny,nz]: Extract 1D via mean, then expand
 * - 3D [ny,nz,nx]: Preserve as 3D, apply normalize_positive directly
 * - 3D haloed [ny+h,nz+h,nx+h]: FIX 2025-01-10 Round10: Slice halos, preserve 3D
 *
 * FIX 2025-12-26: Use explicit fallback options when tensor undefined.
 * FIX 2025-01-10 Round10: Added optional kdim for 2D cubic grid disambiguation.
 *
 * @param metric    Input metric tensor
 * @param ny        Grid ny dimension
 * @param nz        Grid nz dimension
 * @param nx        Grid nx dimension
 * @param eps       Epsilon for normalization (0 = auto-detect)
 * @param fallback_options  Device/dtype for undefined tensor fallback
 * @param kdim      Explicit k-dimension for 2D (0=[nz,nx], 1=[ny,nz], -1=auto)
 */
inline torch::Tensor normalize_metric_3d(
    const torch::Tensor& metric,
    int ny, int nz, int nx,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_options = torch::TensorOptions(),
    int64_t kdim = -1) {

    if (!metric.defined() || metric.numel() == 0) {
        // FIX 2025-12-26: Properly merge fallback device and dtype
        // FIX 2025-01-10 Round13 OPT: Use common helper
        auto opts = applyFallbackToOptions(getDefaultMetricOptions(), fallback_options);
        float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(opts);
        return torch::full({ny, nz, nx}, effective_eps, opts);
    }

    // FIX 2025-12-27: Preserve 3D if input is already full [ny,nz,nx]
    // This avoids losing horizontal variation in metrics like terrain-following rdz
    // NOTE 2025-12-28: When 3D input matches expected shape, normalize_positive() is
    // applied element-wise to the full 3D tensor (NOT reduced to 1D via mean).
    if (metric.dim() == 3 && metric.size(0) == ny && metric.size(1) == nz && metric.size(2) == nx) {
        // Input is already 3D with correct shape - preserve horizontal variation
        // FIX 2025-12-28: Use actual dtype eps, not autocast eps (tensor may be upcast to FP32)
        float effective_eps = (eps > 0.0f) ? eps : getEpsForDtype(metric.scalar_type());
        auto result = normalize_positive(metric, effective_eps);
        // FIX 2025-01-10 Round11+Round13 OPT: Apply fallback_options device/dtype via helper
        return applyFallbackToTensor(result, fallback_options);
    }

    // FIX 2025-01-10 Round10: Preserve 3D for haloed 3D inputs (slice halos first)
    // This prevents losing horizontal variation when only halos differ from [ny,nz,nx]
    if (metric.dim() == 3) {
        const int64_t m_ny = metric.size(0);
        const int64_t m_nz = metric.size(1);
        const int64_t m_nx = metric.size(2);
        if (m_ny >= ny && m_nz >= nz && m_nx >= nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_z = m_nz - nz;
            const int64_t halo_x = m_nx - nx;
            // Only handle symmetric halos; asymmetric falls through to 1D extraction
            if (halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0) {
                // Slice to interior [ny,nz,nx]
                torch::Tensor metric_interior = metric;
                if (halo_y > 0 || halo_z > 0 || halo_x > 0) {
                    int64_t j_start = halo_y / 2;
                    int64_t k_start = halo_z / 2;
                    int64_t i_start = halo_x / 2;
                    metric_interior = metric.slice(0, j_start, j_start + ny)
                                            .slice(1, k_start, k_start + nz)
                                            .slice(2, i_start, i_start + nx);
                }
                float effective_eps = (eps > 0.0f) ? eps : getEpsForDtype(metric.scalar_type());
                auto result = normalize_positive(metric_interior, effective_eps);
                // FIX 2025-01-10 Round11+Round13 OPT: Apply fallback_options device/dtype via helper
                return applyFallbackToTensor(result, fallback_options);
            }
            // Asymmetric halos - fall through to 1D extraction
        }
        // 3D but smaller than [ny,nz,nx] - fall through to 1D extraction
    }

    // For 1D/2D or mismatched 3D, extract 1D and expand
    torch::Tensor metric_1d;
    if (metric.dim() == 1) {
        // FIX 2025-01-10 Round26: Use extract1DVerticalMetric for consistent center-slice
        // Previously just assigned metric directly, then front-sliced later. Now use the
        // same extraction logic as 2D/3D paths for k-alignment consistency.
        metric_1d = extract1DVerticalMetric(metric, nz, ny, nx, kdim);
        if (!metric_1d.defined() || metric_1d.numel() == 0) {
            auto opts = applyFallbackToOptions(metric.options(), fallback_options);
            float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(opts);
            return torch::full({ny, nz, nx}, effective_eps, opts);
        }
    } else if (metric.dim() == 2) {
        // Extract 1D vertical profile from 2D
        // FIX 2025-01-10 Round10: Use kdim parameter for cubic grid disambiguation
        metric_1d = extract1DVerticalMetric(metric, nz, ny, nx, kdim);
        if (!metric_1d.defined() || metric_1d.numel() == 0) {
            // FIX 2025-01-10 Round13 OPT: Use common helper
            auto opts = applyFallbackToOptions(metric.options(), fallback_options);
            float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(opts);
            return torch::full({ny, nz, nx}, effective_eps, opts);
        }
    } else if (metric.dim() == 3) {
        // 3D but shape doesn't match [ny,nz,nx] - extract 1D
        metric_1d = extract1DVerticalMetric(metric, nz, ny, nx);
        if (!metric_1d.defined() || metric_1d.numel() == 0) {
            // FIX 2025-01-10 Round13 OPT: Use common helper
            auto opts = applyFallbackToOptions(metric.options(), fallback_options);
            float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(opts);
            return torch::full({ny, nz, nx}, effective_eps, opts);
        }
    } else {
        TORCH_CHECK(false,
            "normalize_metric_3d: expected 1D/2D/3D metric, got dim=", metric.dim());
    }

    // FIX 2025-12-28: Use actual tensor dtype for eps, not autocast context.
    // Problem: getAutocastAwareEps() returns FP16 eps even when metric_1d is FP32,
    // causing over-clamping. Use the tensor's actual dtype instead.
    float effective_eps = (eps > 0.0f) ? eps : getEpsForDtype(metric_1d.scalar_type());

    // Normalize to positive (pass explicit eps to avoid double autocast check)
    auto normalized = normalize_positive(metric_1d, effective_eps);

    // Ensure we have exactly nz elements
    // FIX 2025-01-10 Round26: Use center-slice for consistency (shouldn't trigger after
    // extract1DVerticalMetric fix, but kept as safety net)
    if (normalized.size(0) > nz) {
        int64_t halo = normalized.size(0) - nz;
        if (halo % 2 == 0) {
            normalized = normalized.slice(0, halo / 2, halo / 2 + nz);
        } else {
            TORCH_WARN_ONCE("normalize_metric_3d: asymmetric halo after extraction (",
                            normalized.size(0), " vs ", nz, "). Using front-slice.");
            normalized = normalized.slice(0, 0, nz);
        }
    } else if (normalized.size(0) < nz) {
        TORCH_CHECK(false,
            "normalize_metric_3d: metric has ", normalized.size(0),
            " elements, but nz=", nz, " expected");
    }

    // Expand to 3D: [nz] -> [1, nz, 1] -> [ny, nz, nx]
    auto result = normalized.unsqueeze(0).unsqueeze(2).expand({ny, nz, nx});

    // FIX 2025-01-10 Round13+OPT: Apply fallback_options device/dtype via helper
    return applyFallbackToTensor(result, fallback_options);
}

/**
 * Compute safe 1/dn for vertical derivative at k (SCALAR version - breaks AD graph).
 * Handles NaN/Inf/zero in dn values.
 *
 * AD WARNING 2025-12-26: Uses .item<float>() internally.
 * Only use for static grid metrics (dn). For grad-requiring tensors, use safe_rdn_at_k_tensor().
 *
 * PERF WARNING 2025-12-28: This function causes GPU→CPU sync via .to(kCPU).item<float>().
 * Do NOT call in tight loops over k. Instead:
 *   - For loop-based vertical derivatives, pre-compute 1D rdn = safe_inv_dn_tensor(dn) once,
 *     then index rdn[k] in the loop (no sync).
 *   - Or use vectorized operations: dvar_dz = (var_upper - var_lower) * safe_inv_dn_tensor(dn)
 *   - For cached access, use MetricCache::getRdnCached() which caches the full 1D profile.
 */
inline torch::Tensor safe_rdn_at_k(
    const torch::Tensor& dn,
    int k,
    const torch::TensorOptions& options,
    float eps = 0.0f) {

    float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(options);

    if (!dn.defined() || k < 0 || k >= dn.numel()) {
        return torch::full({}, 1.0f / effective_eps, options);
    }

    // AD SAFETY: Check for misuse in debug builds
    requiresGradCheck(dn, "safe_rdn_at_k()");

    // FIX Round192: GPU sync performance warning - detect excessive calls
    // This helps identify call sites that should use vectorized safe_inv_dn_tensor() instead
    static thread_local int call_count = 0;
    static thread_local bool warned = false;
    if (dn.is_cuda() || (dn.device().type() == torch::kMPS)) {
        ++call_count;
        if (call_count >= 100 && !warned && g_sdirk3_config.debug_level >= 1) {
            std::cerr << "PERF WARNING: safe_rdn_at_k() called " << call_count
                      << "+ times on GPU. Consider using vectorized safe_inv_dn_tensor() "
                      << "or MetricCache::getRdnCached() instead." << std::endl;
            warned = true;
        }
    }

    // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
    // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
    torch::NoGradGuard no_grad;
    float dn_val = dn[k].to(torch::kCPU).item<float>();
    float safe_val = safe_inv_dn(dn_val, effective_eps);
    return torch::full({}, safe_val, options);
}

/**
 * Compute safe 1/dn for vertical derivative at k (TENSOR version - preserves AD graph).
 * Returns a 0D tensor with gradient connectivity to dn.
 * Use this when dn requires gradients.
 *
 * FIX 2025-12-26: Added fallback_opts parameter to avoid device mismatch when
 * dn is undefined but caller expects GPU tensor.
 */
inline torch::Tensor safe_rdn_at_k_tensor(
    const torch::Tensor& dn,
    int k,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_opts = torch::TensorOptions()) {

    // FIX 2025-12-26: Determine options with proper fallback for both device and dtype
    // If dn is defined, use its options as base; otherwise start from default
    auto opts = dn.defined() ? dn.options() : getDefaultMetricOptions();
    // Override with fallback_opts if specified (check both device and dtype)
    if (!dn.defined()) {
        if (fallback_opts.has_device()) opts = opts.device(fallback_opts.device());
        if (fallback_opts.has_dtype()) opts = opts.dtype(fallback_opts.dtype());
    }
    float effective_eps = (eps > 0.0f) ? eps : (dn.defined() ? getAutocastAwareEps(dn) : kMetricEps);

    if (!dn.defined() || k < 0 || k >= dn.numel()) {
        // FIX: Use opts to maintain device consistency
        return torch::scalar_tensor(1.0f / effective_eps, opts);
    }

    // Tensor path: preserves gradient graph
    auto dn_k = dn[k];  // 0D tensor, preserves grad
    auto safe_dn = torch::clamp_min(torch::abs(dn_k), effective_eps);
    return 1.0f / safe_dn;  // Returns 0D tensor with gradient connectivity
}

/**
 * Normalize tensor mean with safety (SCALAR version - breaks AD graph).
 * For use with STATIC grid metrics (dnw.mean() etc.) that don't require gradients.
 *
 * AD WARNING: This extracts a scalar via .item<float>(), breaking the gradient graph.
 * Only use for static grid metrics. For state tensors, use safe_mean_tensor().
 *
 * FIX 2025-12-30 Batch31 Issue 5: Float64 Precision Note:
 * This function always returns float (float32). If the input tensor is kFloat64,
 * the mean is computed in float64 but returned as float32, losing precision.
 * For full float64 precision, use safe_mean_double() instead.
 */
inline float safe_mean(const torch::Tensor& tensor, float eps = 0.0f) {
    if (!tensor.defined() || tensor.numel() == 0) {
        return (eps > 0.0f) ? eps : kMetricEps;
    }

    // AD SAFETY: Check for misuse in debug builds
    requiresGradCheck(tensor, "safe_mean()");

    // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
    torch::NoGradGuard no_grad;
    float effective_eps = (eps > 0.0f) ? eps : getAutocastAwareEps(tensor);
    // Explicit detach to make intent clear
    auto normalized = normalize_positive(tensor.detach(), effective_eps);
    // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
    return normalized.mean().to(torch::kCPU).item<float>();
}

/**
 * FIX 2025-12-30 Batch31 Issue 5: Double precision version of safe_mean.
 * Returns double for full precision when input is kFloat64.
 * Falls back to float conversion for other dtypes.
 *
 * Use this when:
 * - Input tensor may be kFloat64
 * - Full precision is required in the result
 * - Using in double-precision numerical algorithms
 *
 * AD WARNING: This extracts a scalar via .item<double>(), breaking the gradient graph.
 * Only use for static grid metrics. For state tensors, use safe_mean_tensor().
 */
inline double safe_mean_double(const torch::Tensor& tensor, double eps = 0.0) {
    if (!tensor.defined() || tensor.numel() == 0) {
        return (eps > 0.0) ? eps : static_cast<double>(kMetricEps);
    }

    // AD SAFETY: Check for misuse in debug builds
    requiresGradCheck(tensor, "safe_mean_double()");

    // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
    torch::NoGradGuard no_grad;

    // FIX 2025-01-10 Round25: Use actual tensor dtype for eps selection, not autocast context
    // getAutocastAwareEps() may return FP16 eps even for FP64 input under autocast
    double effective_eps = (eps > 0.0) ? eps : static_cast<double>(getEpsForDtype(tensor.scalar_type()));

    // FIX 2025-01-10 Round25: Upcast FP16/BF16 to FP32 before mean to avoid half-precision computation
    // Half-precision mean can have significant accuracy loss for grid metrics
    torch::Tensor work_tensor = tensor.detach();
    if (tensor.scalar_type() == torch::kHalf || tensor.scalar_type() == torch::kBFloat16) {
        work_tensor = work_tensor.to(torch::kFloat32);
        // FIX 2025-01-10 Round26: Only use FP32 eps if caller didn't provide explicit eps
        // Preserves caller's eps preference when explicitly specified
        if (eps <= 0.0) {
            effective_eps = static_cast<double>(kMetricEpsFP32);
        }
    }

    // Explicit detach to make intent clear
    auto normalized = normalize_positive(work_tensor, static_cast<float>(effective_eps));
    auto mean_tensor = normalized.mean().to(torch::kCPU);

    // Use dtype-appropriate extraction
    if (tensor.scalar_type() == torch::kFloat64) {
        return mean_tensor.item<double>();
    } else {
        return static_cast<double>(mean_tensor.item<float>());
    }
}

/**
 * Normalize tensor mean with safety (TENSOR version - preserves AD graph).
 * Returns a 0D tensor that maintains gradient connectivity.
 * Use this when the input tensor requires gradients.
 *
 * FIX 2025-12-26: Added fallback_opts parameter to avoid device mismatch when
 * tensor is undefined but caller expects GPU tensor.
 */
inline torch::Tensor safe_mean_tensor(
    const torch::Tensor& tensor,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_opts = torch::TensorOptions()) {

    // FIX 2025-12-26: Determine options with proper fallback for both device and dtype
    // If tensor is defined, use its options as base; otherwise start from default
    auto opts = tensor.defined() ? tensor.options() : getDefaultMetricOptions();
    // Override with fallback_opts if specified (check both device and dtype)
    if (!tensor.defined()) {
        if (fallback_opts.has_device()) opts = opts.device(fallback_opts.device());
        if (fallback_opts.has_dtype()) opts = opts.dtype(fallback_opts.dtype());
    }

    if (!tensor.defined() || tensor.numel() == 0) {
        float effective_eps = (eps > 0.0f) ? eps : kMetricEps;
        // FIX: Use opts to maintain device consistency
        return torch::scalar_tensor(effective_eps, opts);
    }

    float effective_eps = (eps > 0.0f) ? eps : getAutocastAwareEps(tensor);
    auto normalized = normalize_positive(tensor, effective_eps);
    auto mean = normalized.mean();  // Returns 0D tensor, preserves grad

    // FIX 2025-12-26: Transfer to fallback device if tensor is defined but on wrong device
    // This handles case where grid.dnw is CPU but operations expect GPU tensor
    if (fallback_opts.has_device() && mean.device() != fallback_opts.device()) {
        mean = mean.to(fallback_opts.device());
    }

    return mean;
}

// ============================================================================
// MetricCache: CPU Pointer-Based Access for Loop Optimization
// ============================================================================
// PERFORMANCE FIX 2025-12-26: Avoid .item<float>() inside loops.
// Pre-copy 1D metric tensor to CPU and access via pointer for O(1) access.
// This eliminates GPU sync and avoids autograd issues.
//
// AD SAFETY WARNING: MetricCache copies data to CPU, breaking gradient flow.
// ONLY use for STATIC grid metrics (dn, dnw, rdnw, rdn, z_at_w, fnm, fnp, etc.)
// that do NOT require gradients. For state tensors (u, v, w, theta), use
// tensor operations directly to preserve the computation graph.

/**
 * Cache for 1D metric tensor with pointer-based access.
 * Use this to avoid repeated .item<float>() calls inside loops.
 *
 * AD WARNING: This breaks the gradient graph! Only use for static grid metrics.
 *
 * Usage:
 *   MetricCache dn_cache(grid.dn);  // Once, before loop (dn is static)
 *   for (int k = 0; k < nz; k++) {
 *       float dn_k = dn_cache.safe_dn(k);  // O(1), no sync
 *   }
 */
class MetricCache {
private:
    torch::Tensor cpu_tensor_;
    const float* data_ptr_;
    int64_t size_;
    float eps_;

public:
    MetricCache() : data_ptr_(nullptr), size_(0), eps_(kMetricEps) {}

    explicit MetricCache(const torch::Tensor& tensor, float eps = kMetricEps)
        : eps_(eps) {
        if (!tensor.defined() || tensor.numel() == 0) {
            data_ptr_ = nullptr;
            size_ = 0;
            return;
        }

        // AD SAFETY: Check for misuse in debug builds
        requiresGradCheck(tensor, "MetricCache");

        // PERF FIX 2025-12-26: Skip copy if already CPU FP32 contiguous
        auto detached = tensor.detach();
        bool is_cpu_fp32_contiguous = detached.is_cpu() &&
                                      detached.scalar_type() == torch::kFloat32 &&
                                      detached.is_contiguous();

        if (is_cpu_fp32_contiguous) {
            // Zero-copy path: just use existing data
            cpu_tensor_ = detached;
        } else {
            // Copy path: convert to CPU FP32 contiguous
            cpu_tensor_ = detached.to(torch::kCPU, torch::kFloat32).contiguous();
        }
        data_ptr_ = cpu_tensor_.data_ptr<float>();
        size_ = cpu_tensor_.numel();
    }

    bool valid() const { return data_ptr_ != nullptr && size_ > 0; }
    int64_t size() const { return size_; }

    // Raw access (no safety check - for trusted indices)
    float operator[](int64_t k) const {
        return (k >= 0 && k < size_) ? data_ptr_[k] : eps_;
    }

    // Safe access with bounds check and NaN/Inf/negative handling
    float safe_at(int64_t k) const {
        if (k < 0 || k >= size_) return eps_;
        float val = data_ptr_[k];
        return std::isfinite(val) ? std::abs(val) : eps_;
    }

    // Safe dn access (for use as multiplier)
    float safe_dn(int64_t k) const {
        float val = safe_at(k);
        return (val >= eps_) ? val : eps_;
    }

    // Safe 1/dn access (for use as inverse)
    float safe_inv_dn(int64_t k) const {
        float val = safe_dn(k);
        return 1.0f / val;
    }

    // Safe rdnw access
    float safe_rdnw(int64_t k) const {
        return safe_dn(k);  // Same logic - ensure positive
    }
};

/**
 * Create MetricCache from tensor with FP32 epsilon.
 *
 * FIX 2025-12-26: Use kMetricEpsFP32 unconditionally since MetricCache always
 * converts data to CPU FP32. Using source dtype eps (e.g., FP16 1e-4) would
 * over-clamp the FP32 cached data. The cache dtype is always FP32, not source dtype.
 */
inline MetricCache makeMetricCache(const torch::Tensor& tensor) {
    // Always use FP32 eps since cache stores data as FP32, regardless of source dtype
    return MetricCache(tensor, kMetricEpsFP32);
}

/**
 * Pre-compute safe mean of tensor (call once before loop).
 * Avoids repeated .mean().item<float>() calls.
 */
inline float computeSafeMeanOnce(const torch::Tensor& tensor, float eps = 0.0f) {
    return safe_mean(tensor, eps);  // Already handles undefined/empty
}

// ============================================================================
// AD Hybrid Path Utilities
// ============================================================================
//
// DECISION GUIDE: When to use which path for metric operations
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                        AD PATH SELECTION MATRIX                          │
// ├─────────────────────┬───────────────────────┬───────────────────────────┤
// │ Condition           │ Recommended           │ Notes                      │
// ├─────────────────────┼───────────────────────┼───────────────────────────┤
// │ Static grid metric  │ MetricCache           │ dn, dnw, rdnw, rdn,       │
// │ + k-loop            │ (scalar path)         │ z_at_w, msft*, fnm, fnp   │
// │                     │                       │ No .requires_grad()       │
// ├─────────────────────┼───────────────────────┼───────────────────────────┤
// │ Static grid metric  │ safe_inv_tensor()     │ Best for GPU performance  │
// │ + vectorized ops    │ + view({1,nz,1})      │ No k-loop sync overhead   │
// ├─────────────────────┼───────────────────────┼───────────────────────────┤
// │ State tensor        │ safe_inv_tensor()     │ u, v, w, theta, q*        │
// │ (requires_grad)     │ or normalize_positive │ MUST preserve AD graph    │
// ├─────────────────────┼───────────────────────┼───────────────────────────┤
// │ Unknown/mixed       │ HybridMeanResult      │ Auto-selects at runtime   │
// │ (library interface) │ HybridRdnResult       │ based on requires_grad()  │
// ├─────────────────────┼───────────────────────┼───────────────────────────┤
// │ Conditional AD      │ makeMetricCacheIfSafe │ Returns empty cache if    │
// │                     │ + cache.valid() check │ tensor requires grad      │
// └─────────────────────┴───────────────────────┴───────────────────────────┘
//
// EXAMPLES:
//
// 1. FULLY VECTORIZED (recommended for GPU, preserves AD):
//    auto rdnw_1d = metric_utils::safe_inv_tensor(grid.dnw);  // [nz]
//    auto rdnw_3d = rdnw_1d.view({1, nz, 1});                  // broadcast
//    auto dw_dz = (w.slice(1,1,nz+1) - w.slice(1,0,nz)) * rdnw_3d;
//
// 2. CACHED k-LOOP (CPU-optimized, no AD needed):
//    auto dnw_cache = metric_utils::makeMetricCache(grid.dnw);
//    for (int k = 0; k < nz; k++) {
//        float rdnw = dnw_cache.safe_inv_dn(k);  // O(1), no GPU sync
//        // ... scalar operations ...
//    }
//
// 3. HYBRID (auto-selects based on tensor state):
//    auto cache = metric_utils::makeMetricCacheIfSafe(grid.dnw);
//    if (cache.valid()) {
//        // Scalar path: k-loop with cache
//    } else {
//        // Tensor path: vectorized with safe_inv_tensor
//    }
//
// AD SAFETY RULES:
// - NEVER use MetricCache or safe_mean() on tensors with requires_grad()
// - ALWAYS use NoGradGuard when scalar loops cannot be avoided
// - PREFER vectorized tensor ops for GPU performance and AD compatibility
// ============================================================================

/**
 * AD-hybrid safe mean: returns scalar if no grad needed, tensor if grad required.
 *
 * Usage pattern:
 *   auto mean_result = safe_mean_hybrid(grid.dn);
 *   // If grid.dn requires grad, mean_result.tensor is valid and preserves graph
 *   // If grid.dn is static, mean_result.scalar is valid for efficient loop use
 *
 * FIX 2025-12-26: Provides unified interface for both scalar and tensor paths.
 */
struct HybridMeanResult {
    float scalar;           // Valid if !requires_grad
    torch::Tensor tensor;   // Valid if requires_grad
    bool is_tensor;         // True if tensor path was used

    // Convenience: get as tensor (works for both cases)
    torch::Tensor as_tensor(const torch::TensorOptions& opts) const {
        if (is_tensor) return tensor;
        return torch::scalar_tensor(scalar, opts);
    }
};

inline HybridMeanResult safe_mean_hybrid(
    const torch::Tensor& tensor,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_opts = torch::TensorOptions()) {

    HybridMeanResult result;

    if (!tensor.defined() || tensor.numel() == 0) {
        float effective_eps = (eps > 0.0f) ? eps : kMetricEps;
        result.scalar = effective_eps;
        result.tensor = torch::Tensor();  // Undefined
        result.is_tensor = false;
        return result;
    }

    if (tensor.requires_grad()) {
        // TENSOR PATH: Preserve gradient graph
        result.tensor = safe_mean_tensor(tensor, eps, fallback_opts);
        result.scalar = 0.0f;  // Not valid in this case
        result.is_tensor = true;
    } else {
        // SCALAR PATH: More efficient for static metrics
        result.scalar = safe_mean(tensor, eps);
        result.tensor = torch::Tensor();  // Undefined
        result.is_tensor = false;
    }

    return result;
}

/**
 * AD-hybrid inverse dn at k: returns scalar if no grad needed, tensor if grad required.
 *
 * Usage in loops:
 *   for (int k = 0; k < nz; k++) {
 *       auto rdnw = safe_rdn_hybrid(grid.dnw, k, opts);
 *       if (rdnw.is_tensor) {
 *           // Use rdnw.tensor for AD-compatible ops
 *       } else {
 *           // Use rdnw.scalar for efficient scalar ops
 *       }
 *   }
 *
 * FIX 2025-12-26: Provides unified interface for loop-based metric access.
 */
struct HybridRdnResult {
    float scalar;           // Valid if !requires_grad
    torch::Tensor tensor;   // Valid if requires_grad
    bool is_tensor;         // True if tensor path was used

    // Convenience: get as tensor (works for both cases)
    torch::Tensor as_tensor(const torch::TensorOptions& opts) const {
        if (is_tensor) return tensor;
        return torch::scalar_tensor(scalar, opts);
    }
};

inline HybridRdnResult safe_rdn_hybrid(
    const torch::Tensor& dn,
    int k,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_opts = torch::TensorOptions()) {

    HybridRdnResult result;
    float effective_eps = (eps > 0.0f) ? eps : (dn.defined() ? getAutocastAwareEps(dn) : kMetricEps);

    if (!dn.defined() || k < 0 || k >= dn.numel()) {
        result.scalar = 1.0f / effective_eps;
        result.tensor = torch::Tensor();
        result.is_tensor = false;
        return result;
    }

    if (dn.requires_grad()) {
        // TENSOR PATH: Preserve gradient graph
        result.tensor = safe_rdn_at_k_tensor(dn, k, eps, fallback_opts);
        result.scalar = 0.0f;
        result.is_tensor = true;
    } else {
        // SCALAR PATH: Use cache-compatible scalar extraction
        // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        torch::NoGradGuard no_grad;
        result.scalar = safe_rdn_at_k(dn, k, dn.options(), eps).to(torch::kCPU).item<float>();
        result.tensor = torch::Tensor();
        result.is_tensor = false;
    }

    return result;
}

/**
 * Create MetricCache or return nullptr-equivalent if tensor requires grad.
 * Use this for safe cache creation that won't break AD.
 *
 * Usage:
 *   auto cache = makeMetricCacheIfSafe(grid.dn);
 *   if (cache.valid()) {
 *       // Use scalar path with cache
 *   } else {
 *       // Use tensor operations directly
 *   }
 *
 * FIX 2025-12-26: Returns empty cache if tensor requires grad, allowing caller
 * to detect and use tensor path instead.
 */
inline MetricCache makeMetricCacheIfSafe(const torch::Tensor& tensor) {
    if (tensor.defined() && tensor.requires_grad()) {
        // Return empty cache to signal tensor path should be used
        return MetricCache();
    }
    return makeMetricCache(tensor);
}

/**
 * Get vectorized 1/dn as GPU tensor (preferred for AD-compatible loops).
 * Replaces k-loop + scalar with single vectorized operation.
 *
 * Usage:
 *   auto rdnw_3d = safe_rdn_vectorized(grid.dnw, nz, phi.options());
 *   auto result = (phi_upper - phi_lower) * rdnw_3d;  // Vectorized, AD-safe
 *
 * FIX 2025-12-26: Preferred over loops for both AD safety and GPU efficiency.
 */
inline torch::Tensor safe_rdn_vectorized(
    const torch::Tensor& dn,
    int nz,
    const torch::TensorOptions& opts,
    float eps = 0.0f) {

    if (!dn.defined() || dn.numel() == 0 || nz <= 0) {
        float effective_eps = (eps > 0.0f) ? eps : kMetricEpsFP32;
        return torch::full({1, nz, 1}, 1.0f / effective_eps, opts);
    }

    // Normalize and compute inverse
    auto dn_safe = normalize_positive(dn.to(opts).slice(0, 0, nz), eps);
    auto rdn_1d = 1.0f / dn_safe;  // [nz]
    return rdn_1d.view({1, nz, 1});  // [1, nz, 1] for broadcast
}

// ============================================================================
// Stagger-Aware Metric Alignment Helpers (FIX 2025-12-27)
// ============================================================================
// Common utilities for aligning grid metrics (rdz, rdzw, msf*) to proper shapes
// for mass, U-staggered, and V-staggered grids. Prevents shape mismatch bugs
// and ensures device/dtype consistency.
//
// WRF Stagger Convention:
// - Mass:   [ny, nz, nx]     - theta, p, density, qv, etc.
// - U-stag: [ny, nz, nx+1]   - u wind component
// - V-stag: [ny+1, nz, nx]   - v wind component
// - W-stag: [ny, nz+1, nx]   - w wind component (vertical)
//
// Map Scale Factor Shapes:
// - msftx/msfty (mass):   [ny, nx] or [ny, nz, nx]
// - msfux/msfuy (U-stag): [ny, nx+1] or [ny, nz, nx+1]
// - msfvx/msfvy (V-stag): [ny+1, nx] or [ny+1, nz, nx]
//
// Vertical Metric Shapes:
// - rdz/rdzw: [nz] or [ny, nz, nx]
//   - U-staggered ops need [ny, nz, nx+1] → pad X boundary
//   - V-staggered ops need [ny+1, nz, nx] → pad Y boundary
// ============================================================================

/**
 * Grid stagger type enumeration.
 * Determines expected tensor shapes and boundary padding requirements.
 */
enum class Stagger {
    Mass,   // [ny, nz, nx]     - No padding
    U,      // [ny, nz, nx+1]   - X-direction padding
    V,      // [ny+1, nz, nx]   - Y-direction padding
    W       // [ny, nz+1, nx]   - Z-direction stagger (vertical)
};

/**
 * Get expected 3D shape for given stagger type.
 */
inline std::vector<int64_t> getStaggeredShape(int64_t ny, int64_t nz, int64_t nx, Stagger stag) {
    switch (stag) {
        case Stagger::Mass: return {ny, nz, nx};
        case Stagger::U:    return {ny, nz, nx + 1};
        case Stagger::V:    return {ny + 1, nz, nx};
        case Stagger::W:    return {ny, nz + 1, nx};
    }
    return {ny, nz, nx};  // Default to mass
}

/**
 * Pad 3D tensor in X-direction (nx → nx+1) using edge extrapolation.
 * Used for U-staggered operations when metric is [ny,nz,nx].
 */
inline torch::Tensor pad_x(const torch::Tensor& m3d) {
    TORCH_CHECK(m3d.dim() == 3, "pad_x requires 3D tensor, got dim=", m3d.dim());
    int64_t ny = m3d.size(0), nz = m3d.size(1), nx = m3d.size(2);
    auto result = torch::zeros({ny, nz, nx + 1}, m3d.options());
    result.index_put_({torch::indexing::Slice(), torch::indexing::Slice(), torch::indexing::Slice(0, nx)}, m3d);
    result.index_put_({torch::indexing::Slice(), torch::indexing::Slice(), nx}, m3d.index({torch::indexing::Slice(), torch::indexing::Slice(), nx - 1}));
    return result;
}

/**
 * Pad 3D tensor in Y-direction (ny → ny+1) using edge extrapolation.
 * Used for V-staggered operations when metric is [ny,nz,nx].
 */
inline torch::Tensor pad_y(const torch::Tensor& m3d) {
    TORCH_CHECK(m3d.dim() == 3, "pad_y requires 3D tensor, got dim=", m3d.dim());
    int64_t ny = m3d.size(0), nz = m3d.size(1), nx = m3d.size(2);
    auto result = torch::zeros({ny + 1, nz, nx}, m3d.options());
    result.index_put_({torch::indexing::Slice(0, ny), torch::indexing::Slice(), torch::indexing::Slice()}, m3d);
    result.index_put_({ny, torch::indexing::Slice(), torch::indexing::Slice()}, m3d.index({ny - 1, torch::indexing::Slice(), torch::indexing::Slice()}));
    return result;
}

/**
 * Pad 3D tensor for specified stagger type.
 * Mass: no padding, U: X-padding, V: Y-padding, W: unchanged (handled differently).
 */
inline torch::Tensor pad_for_stagger(const torch::Tensor& m3d, Stagger stag) {
    if (stag == Stagger::Mass || stag == Stagger::W) return m3d;
    if (stag == Stagger::U) return pad_x(m3d);
    if (stag == Stagger::V) return pad_y(m3d);
    return m3d;
}

/**
 * Align map scale factor (msf*) to 3D with proper stagger.
 *
 * Handles 2D→3D expansion and 3D shape validation. Ensures device/dtype
 * alignment to avoid runtime mismatches in mixed precision or GPU contexts.
 *
 * @param msf     Input map scale factor (2D or 3D)
 * @param ny      Grid ny dimension
 * @param nz      Grid nz dimension
 * @param nx      Grid nx dimension
 * @param stag    Target stagger type (Mass, U, V)
 * @param opts    Target tensor options (device, dtype)
 * @return        3D tensor [ny,nz,nx] or staggered variant
 *
 * WRF Map Factor Convention:
 * - Mass:  msfty/msftx [ny, nx]     → [ny, nz, nx]
 * - U:     msfuy/msfux [ny, nx+1]   → [ny, nz, nx+1]
 * - V:     msfvy/msfvx [ny+1, nx]   → [ny+1, nz, nx]
 */
inline torch::Tensor align_msf_3d(
    const torch::Tensor& msf,
    int64_t ny, int64_t nz, int64_t nx,
    Stagger stag,
    const torch::TensorOptions& opts)
{
    auto expected_shape = getStaggeredShape(ny, nz, nx, stag);

    // Fallback: return ones if undefined
    // FIX 2025-12-28: Force requires_grad(false) for fallback to avoid AD graph overhead
    if (!msf.defined() || msf.numel() == 0) {
        TORCH_WARN_ONCE("align_msf_3d: msf undefined, using ones fallback");
        auto safe_opts = opts.requires_grad(false);
        return torch::ones(expected_shape, safe_opts);
    }

    // FIX 2025-12-27: Check requires_grad - map scale factors should be static
    // If input has requires_grad, it's a misuse (AD graph would be lost in expand/pad)
    requiresGradCheck(msf, "align_msf_3d: map scale factors should not require gradients");

    // FIX 2025-01-10 Round22 PERF: Helper to skip .to(opts) if already on target device/dtype
    auto toOptsIfNeeded = [&opts](const torch::Tensor& t) -> torch::Tensor {
        if (t.device() == opts.device() &&
            t.scalar_type() == opts.dtype().toScalarType()) {
            return t;  // Already on target, no copy needed
        }
        return t.to(opts);
    };

    // 2D → 3D expansion
    if (msf.dim() == 2) {
        // Validate 2D shape based on stagger
        const int64_t exp_2d_ny = (stag == Stagger::V) ? ny + 1 : ny;
        const int64_t exp_2d_nx = (stag == Stagger::U) ? nx + 1 : nx;
        const int64_t m_ny = msf.size(0);
        const int64_t m_nx = msf.size(1);

        // Exact expected 2D staggered shape match
        if (m_ny == exp_2d_ny && m_nx == exp_2d_nx) {
            return toOptsIfNeeded(msf).unsqueeze(1).expand(expected_shape);
        }

        // FIX 2025-01-10 Round8: Check staggered 2D shape with halos FIRST
        if (m_ny >= exp_2d_ny && m_nx >= exp_2d_nx) {
            const int64_t halo_y = m_ny - exp_2d_ny;
            const int64_t halo_x = m_nx - exp_2d_nx;
            // FIX 2025-01-10 Round11: TORCH_CHECK for asymmetric halos instead of fallthrough
            // Asymmetric halos cause edge shift when using symmetric offset calculation
            TORCH_CHECK(halo_y % 2 == 0 && halo_x % 2 == 0,
                "align_msf_3d: 2D msf staggered shape [", m_ny, ",", m_nx,
                "] has asymmetric halos (halo_y=", halo_y, ", halo_x=", halo_x,
                ") relative to expected [", exp_2d_ny, ",", exp_2d_nx,
                "]. Asymmetric halos cause edge shift and are not supported.");
            int64_t j_start = halo_y / 2;
            int64_t i_start = halo_x / 2;
            auto msf_interior = msf.slice(0, j_start, j_start + exp_2d_ny)
                                   .slice(1, i_start, i_start + exp_2d_nx);
            return toOptsIfNeeded(msf_interior).unsqueeze(1).expand(expected_shape);
        }

        // Exact mass 2D shape - expand and pad
        if (m_ny == ny && m_nx == nx) {
            auto msf_aligned = toOptsIfNeeded(msf).unsqueeze(1).expand({ny, nz, nx});
            return pad_for_stagger(msf_aligned, stag);
        }

        // FIX 2025-01-10 Round8: Mass 2D shape with halos - slice to mass, then pad
        if (m_ny >= ny && m_nx >= nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_x = m_nx - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_x % 2 == 0,
                "align_msf_3d: 2D msf halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_x=", halo_x);
            int64_t j_start = halo_y / 2;
            int64_t i_start = halo_x / 2;
            auto msf_interior = msf.slice(0, j_start, j_start + ny)
                                   .slice(1, i_start, i_start + nx);
            auto msf_aligned = toOptsIfNeeded(msf_interior).unsqueeze(1).expand({ny, nz, nx});
            return pad_for_stagger(msf_aligned, stag);
        }

        TORCH_CHECK(false,
            "align_msf_3d: 2D msf shape [", m_ny, ",", m_nx,
            "] doesn't match expected [", exp_2d_ny, ",", exp_2d_nx,
            "] or mass [", ny, ",", nx, "]");
    }

    // 3D validation
    if (msf.dim() == 3) {
        const int64_t m_ny = msf.size(0);
        const int64_t m_nz = msf.size(1);
        const int64_t m_nx = msf.size(2);
        const int64_t exp_ny = expected_shape[0];
        const int64_t exp_nz = expected_shape[1];
        const int64_t exp_nx = expected_shape[2];

        // Exact expected shape match
        if (m_ny == exp_ny && m_nz == exp_nz && m_nx == exp_nx) {
            return toOptsIfNeeded(msf);
        }

        // FIX 2025-01-10 Round8: Check staggered shape with halos FIRST
        // Slice directly to expected shape (no padding needed)
        if (m_ny >= exp_ny && m_nz >= exp_nz && m_nx >= exp_nx) {
            const int64_t halo_y = m_ny - exp_ny;
            const int64_t halo_z = m_nz - exp_nz;
            const int64_t halo_x = m_nx - exp_nx;
            // FIX 2025-01-10 Round11: TORCH_CHECK for asymmetric halos instead of fallthrough
            // Asymmetric halos cause edge shift when using symmetric offset calculation
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "align_msf_3d: 3D msf staggered shape [", m_ny, ",", m_nz, ",", m_nx,
                "] has asymmetric halos (halo_y=", halo_y, ", halo_z=", halo_z,
                ", halo_x=", halo_x, ") relative to expected [", exp_ny, ",", exp_nz,
                ",", exp_nx, "]. Asymmetric halos cause edge shift and are not supported.");
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            auto msf_sliced = msf.slice(0, j_start, j_start + exp_ny)
                                 .slice(1, k_start, k_start + exp_nz)
                                 .slice(2, i_start, i_start + exp_nx);
            return toOptsIfNeeded(msf_sliced);
        }

        // Exact mass shape needing padding
        if (m_ny == ny && m_nz == nz && m_nx == nx) {
            return pad_for_stagger(toOptsIfNeeded(msf), stag);
        }

        // FIX 2025-01-10 Round8: Mass shape with halos - slice to mass, then pad
        if (m_ny >= ny && m_nz >= nz && m_nx >= nx) {
            const int64_t halo_y = m_ny - ny;
            const int64_t halo_z = m_nz - nz;
            const int64_t halo_x = m_nx - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "align_msf_3d: 3D msf halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_z=", halo_z, ", halo_x=", halo_x);
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            auto msf_interior = msf.slice(0, j_start, j_start + ny)
                                   .slice(1, k_start, k_start + nz)
                                   .slice(2, i_start, i_start + nx);
            return pad_for_stagger(toOptsIfNeeded(msf_interior), stag);
        }

        TORCH_CHECK(false,
            "align_msf_3d: 3D msf shape [", m_ny, ",", m_nz, ",", m_nx,
            "] doesn't match expected [", exp_ny, ",", exp_nz, ",", exp_nx,
            "] or mass [", ny, ",", nz, ",", nx, "]");
    }

    TORCH_CHECK(false, "align_msf_3d: msf must be 2D or 3D, got dim=", msf.dim());
}

/**
 * Align vertical metric (rdz, rdzw) to 3D with proper stagger and device.
 *
 * Handles 1D→3D broadcast, 2D→3D extraction, and 3D validation. Applies
 * padding for U/V-staggered operations.
 *
 * @param metric    Input vertical metric (1D, 2D, or 3D)
 * @param ny        Grid ny dimension
 * @param nz        Grid nz dimension
 * @param nx        Grid nx dimension
 * @param stag      Target stagger type (Mass, U, V)
 * @param opts      Target tensor options (device, dtype)
 * @param kdim      For 2D inputs: k-dimension index (0 or 1), -1 for auto
 * @param normalize FIX 2025-12-29 Batch11 Issue 3: If true, apply normalize_positive()
 *                  to handle NaN/Inf/negative values before expansion. Default=false
 *                  for backwards compatibility. Enable when input may have invalid values.
 * @return          3D tensor with proper shape for stagger
 *
 * Shape handling:
 * - 1D [nz]           → view({1,nz,1}).expand({ny,nz,nx}) → pad for stagger
 * - 2D [nz,nx]/[ny,nz] → extract1DVerticalMetric → 1D path
 * - 3D [ny,nz,nx]     → validate → pad for stagger
 */
// FIX 2025-12-31 Batch36 Issue 6: Changed default normalize to true for safety.
inline torch::Tensor align_rdz_or_rdzw_3d(
    const torch::Tensor& metric,
    int64_t ny, int64_t nz, int64_t nx,
    Stagger stag,
    const torch::TensorOptions& opts,
    int64_t kdim = -1,
    bool normalize = true)
{
    auto expected_shape = getStaggeredShape(ny, nz, nx, stag);

    // Fallback: return ones if undefined
    // FIX 2025-12-28: Force requires_grad(false) for fallback to avoid AD graph overhead
    if (!metric.defined() || metric.numel() == 0) {
        TORCH_WARN_ONCE("align_rdz_or_rdzw_3d: metric undefined, using ones fallback");
        auto safe_opts = opts.requires_grad(false);
        return torch::ones(expected_shape, safe_opts);
    }

    // FIX 2025-12-27: Check requires_grad - grid metrics should be static
    // If input has requires_grad, it's a misuse (AD graph would be lost in expand/pad)
    requiresGradCheck(metric, "align_rdz_or_rdzw_3d: grid metrics should not require gradients");

    torch::Tensor m = metric;

    // FIX 2025-12-30 Batch14 Issue 5: For 2D metrics, extract 1D first, then normalize
    // This avoids normalizing the entire 2D tensor when we only need 1D result
    // TODO 2025-01-10 Round23 PERF: Consider caching 1D profile extraction results
    // for repeated calls (similar to RdzwMassCache pattern) if timestep-level profiling
    // shows significant overhead from repeated mean reductions.
    if (m.dim() == 2) {
        // FIX 2025-01-10 Round23: Validate kdim range before processing
        // Silently falling through on invalid kdim (e.g., typo kdim=5) hides bugs.
        TORCH_CHECK(kdim == -1 || kdim == 0 || kdim == 1,
            "align_rdz_or_rdzw_3d: kdim must be -1 (auto), 0, or 1 for 2D metric, got kdim=", kdim);

        // FIX 2025-01-10 Round20: Use extract1DVerticalMetricForW for Stagger::W
        // rdzw has nz+1 elements at w-levels, so we need the W-level extraction
        if (stag == Stagger::W) {
            m = extract1DVerticalMetricForW(m, nz, ny, nx, kdim);  // Extracts nz+1 elements
        } else {
            // FIX 2025-01-10 Round20: Detect w-level 2D input (k-dim = nz+1) for mass/u/v target
            // If detected, extract all nz+1 elements and let 1D path do w→mass averaging
            // FIX 2025-01-10 Round21: Handle k-halo case (k_size >= nz+1, not just ==)
            // FIX 2025-01-10 Round22: Use parity check to disambiguate w-level vs mass with halo
            //   w-level: (k_size - (nz+1)) % 2 == 0 (symmetric halo around nz+1)
            //   mass:    (k_size - nz) % 2 == 0     (symmetric halo around nz)
            int64_t k_size = 0;
            int64_t detected_kdim = kdim;
            if (kdim >= 0 && kdim < m.dim()) {
                k_size = m.size(kdim);
            } else {
                // Auto-detect: check which dimension could be k
                // FIX 2025-01-10 Round22: Warn when both dims qualify and kdim=-1
                const int64_t d0 = m.size(0);
                const int64_t d1 = m.size(1);
                const bool d0_could_be_w = (d0 >= nz + 1) && ((d0 - (nz + 1)) % 2 == 0);
                const bool d0_could_be_mass = (d0 >= nz) && ((d0 - nz) % 2 == 0);
                const bool d1_could_be_w = (d1 >= nz + 1) && ((d1 - (nz + 1)) % 2 == 0);
                const bool d1_could_be_mass = (d1 >= nz) && ((d1 - nz) % 2 == 0);
                const bool d0_qualifies = d0_could_be_w || d0_could_be_mass;
                const bool d1_qualifies = d1_could_be_w || d1_could_be_mass;

                // FIX 2025-01-10 Round23: Use ny/nx proximity to disambiguate when both dims qualify
                // Check if each dim could be horizontal (matches ny or nx with symmetric halo)
                const bool d0_could_be_ny = (d0 >= ny) && ((d0 - ny) % 2 == 0);
                const bool d0_could_be_nx = (d0 >= nx) && ((d0 - nx) % 2 == 0);
                const bool d1_could_be_ny = (d1 >= ny) && ((d1 - ny) % 2 == 0);
                const bool d1_could_be_nx = (d1 >= nx) && ((d1 - nx) % 2 == 0);
                const bool d0_could_be_horiz = d0_could_be_ny || d0_could_be_nx;
                const bool d1_could_be_horiz = d1_could_be_ny || d1_could_be_nx;

                if (d0_qualifies && d1_qualifies) {
                    // Both dims could be k-dim. Use horizontal match to disambiguate.
                    if (d1_could_be_horiz && !d0_could_be_horiz) {
                        // d1 is likely horizontal → d0 is k-dim
                        detected_kdim = 0;
                    } else if (d0_could_be_horiz && !d1_could_be_horiz) {
                        // d0 is likely horizontal → d1 is k-dim
                        detected_kdim = 1;
                    } else {
                        // Truly ambiguous (cubic grid or both could be horizontal)
                        // Force explicit kdim to prevent silent wrong axis selection
                        TORCH_CHECK(false,
                            "align_rdz_or_rdzw_3d: 2D metric [", d0, ",", d1,
                            "] is ambiguous with nz=", nz, ", ny=", ny, ", nx=", nx,
                            " (both dims qualify as k-dim and neither is clearly horizontal). "
                            "Set grid.rdz_kdim/rdzw_kdim explicitly for this grid configuration.");
                    }
                }

                // Select k-dim based on detected_kdim or by priority
                if (detected_kdim == 0 || (detected_kdim == -1 && d0_could_be_w)) {
                    k_size = d0;
                    detected_kdim = 0;
                } else if (detected_kdim == 1 || (detected_kdim == -1 && d1_could_be_w)) {
                    k_size = d1;
                    detected_kdim = 1;
                } else if (d0_could_be_mass) {
                    k_size = d0;
                    detected_kdim = 0;
                } else if (d1_could_be_mass) {
                    k_size = d1;
                    detected_kdim = 1;
                } else {
                    // Neither dim qualifies - fall through to extract with best guess
                    k_size = (d0 >= d1) ? d0 : d1;
                    detected_kdim = (d0 >= d1) ? 0 : 1;
                }
            }
            // FIX 2025-01-10 Round22: Use parity to determine w-level vs mass
            const bool is_w_level = (k_size >= nz + 1) && ((k_size - (nz + 1)) % 2 == 0);
            if (is_w_level) {
                // w-level source (possibly with halo): extract nz+1 elements, 1D path will average to mass
                m = extract1DVerticalMetricForW(m, nz, ny, nx, detected_kdim);  // Extracts nz+1 elements
            } else {
                // Mass-level source (possibly with halo): extract nz elements
                m = extract1DVerticalMetric(m, nz, ny, nx, detected_kdim);  // Extracts nz elements
            }
        }
        if (!m.defined() || m.numel() == 0) {
            // FIX 2025-12-28: Force requires_grad(false) for fallback
            auto safe_opts = opts.requires_grad(false);
            return torch::ones(expected_shape, safe_opts);
        }
        // Now normalize the 1D result (much smaller than 2D)
        if (normalize) {
            float eps = getEpsForOptions(m.options());
            m = normalize_positive(m.detach(), eps);
        }
    } else if (normalize && m.dim() == 1) {
        // FIX 2025-12-29 Batch11 Issue 3: Apply normalization for 1D case
        // FIX 2025-12-29 Batch13 Issue 2: Use metric's dtype for eps selection, not output opts
        // opts may be FP16 (compute dtype) while metric is FP32, causing over-clamping
        float eps = getEpsForOptions(m.options());
        m = normalize_positive(m.detach(), eps);
    }
    // FIX 2025-01-10 Round8: For 3D, normalize is deferred until after halo slicing
    // to avoid contamination from halo values. See 3D section below.

    // 1D → 3D broadcast + padding
    if (m.dim() == 1) {
        // FIX 2025-01-10 Round18: Use expected_shape[1] for k-dim size
        // For Stagger::W, exp_nz = nz+1; for Mass/U/V, exp_nz = nz
        const int64_t exp_ny = expected_shape[0];
        const int64_t exp_nz = expected_shape[1];
        const int64_t exp_nx = expected_shape[2];
        TORCH_CHECK(m.size(0) >= exp_nz,
            "align_rdz_or_rdzw_3d: 1D metric size ", m.size(0),
            " is less than expected k-dim=", exp_nz, " for stagger=",
            (stag == Stagger::W ? "W" : (stag == Stagger::Mass ? "Mass" :
             (stag == Stagger::U ? "U" : "V"))));

        // FIX 2025-01-10 Round20: Handle w→mass conversion for rdzw(nz+1)→mass(nz)
        // FIX 2025-01-10 Round21: Handle haloed w-level input: center-slice to (exp_nz+1) first, then average
        // FIX 2025-01-10 Round22: Use parity check to disambiguate w-level vs mass with halo
        //   w-level: (size - (exp_nz+1)) % 2 == 0 (symmetric halo around nz+1)
        //   mass:    (size - exp_nz) % 2 == 0     (symmetric halo around nz)
        // When input is w-level and target is not W-stagger,
        // average adjacent w-levels to get mass-level values (WRF convention)
        torch::Tensor m_prepared = m;
        const int64_t m_size = m.size(0);
        const bool is_w_level = (m_size >= exp_nz + 1) && ((m_size - (exp_nz + 1)) % 2 == 0);
        if (is_w_level && stag != Stagger::W) {
            // First, center-slice to exactly exp_nz+1 if larger (handle halo)
            torch::Tensor m_w_level = m;
            if (m.size(0) > exp_nz + 1) {
                int64_t w_halo = m.size(0) - (exp_nz + 1);
                if (w_halo % 2 == 0) {
                    // Symmetric halo on w-level: center-slice to (exp_nz+1)
                    m_w_level = m.slice(0, w_halo / 2, w_halo / 2 + exp_nz + 1);
                } else {
                    // Asymmetric halo on w-level: warn and front-slice
                    TORCH_WARN_ONCE("align_rdz_or_rdzw_3d: 1D w-level metric has asymmetric halo (",
                                    m.size(0), " vs ", exp_nz + 1, "). Slicing from front.");
                    m_w_level = m.slice(0, 0, exp_nz + 1);
                }
            }
            // w→mass averaging: m_mass[k] = 0.5 * (m[k] + m[k+1]) for k = 0..exp_nz-1
            m_prepared = 0.5 * (m_w_level.slice(0, 0, exp_nz) + m_w_level.slice(0, 1, exp_nz + 1));
        }

        // FIX 2025-01-10 Round18: Use center-slice for symmetric halo handling (mass-level input)
        torch::Tensor m_sliced;
        if (m_prepared.size(0) > exp_nz) {
            int64_t halo = m_prepared.size(0) - exp_nz;
            if (halo % 2 == 0) {
                // Symmetric halo: center-slice
                m_sliced = m_prepared.slice(0, halo / 2, halo / 2 + exp_nz);
            } else {
                // Asymmetric halo: warn and front-slice
                TORCH_WARN_ONCE("align_rdz_or_rdzw_3d: 1D metric has asymmetric halo (",
                                m_prepared.size(0), " vs ", exp_nz, "). Slicing from front.");
                m_sliced = m_prepared.slice(0, 0, exp_nz);
            }
        } else {
            // No halo: exact match
            m_sliced = m_prepared;
        }
        // FIX 2025-01-10 Round21 OPT: Skip .to(opts) if already on target device/dtype
        torch::Tensor m_final = m_sliced;
        if (m_sliced.device() != opts.device() ||
            m_sliced.scalar_type() != opts.dtype().toScalarType()) {
            m_final = m_sliced.to(opts);
        }
        auto base = m_final.view({1, exp_nz, 1}).expand({exp_ny, exp_nz, exp_nx});
        // FIX 2025-12-27: Skip clone for Mass stagger (pad_for_stagger returns unchanged)
        // Clone only needed for U/V where padding creates a new tensor from expanded view
        if (stag == Stagger::Mass || stag == Stagger::W) {
            return base;  // No padding needed, expand() result is safe to return
        }
        return pad_for_stagger(base.clone(), stag);  // clone needed before padding U/V
    }

    // 3D validation + padding
    if (m.dim() == 3) {
        const int64_t m_ny = m.size(0);
        const int64_t m_nz = m.size(1);
        const int64_t m_nx = m.size(2);
        const int64_t exp_ny = expected_shape[0];
        const int64_t exp_nz = expected_shape[1];
        const int64_t exp_nx = expected_shape[2];

        // FIX 2025-01-10 Round8: Helper lambda to apply normalize after slicing
        auto normalize_if_needed = [&](torch::Tensor t) -> torch::Tensor {
            if (normalize) {
                float eps = getEpsForOptions(t.options());
                return normalize_positive(t.detach(), eps);
            }
            return t;
        };

        // FIX 2025-01-10 Round21: Detect w-level 3D input [ny, nz+1, nx] when stag != W
        // Apply w→mass averaging before other checks
        torch::Tensor m_work = m;
        if (stag != Stagger::W && m_nz == nz + 1 && m_ny == ny && m_nx == nx) {
            // w-level metric with exact horizontal shape: convert to mass-level
            // WRF convention: m_mass[k] = 0.5 * (m_w[k] + m_w[k+1])
            m_work = 0.5 * (m.slice(1, 0, nz) + m.slice(1, 1, nz + 1));
            // After averaging, shape is [ny, nz, nx] - falls through to mass check
        } else if (stag != Stagger::W && m_nz >= nz + 1 && m_ny >= ny && m_nx >= nx) {
            // w-level metric with possible halos: center-slice to [ny, nz+1, nx] first
            const int64_t w_halo_y = m_ny - ny;
            const int64_t w_halo_z = m_nz - (nz + 1);
            const int64_t w_halo_x = m_nx - nx;
            // FIX 2025-01-10 Round22: Use parity check for k-dim to confirm w-level
            const bool k_is_w_level = (w_halo_z % 2 == 0);  // k-dim matches nz+1 with symmetric halo
            if (k_is_w_level && w_halo_y % 2 == 0 && w_halo_x % 2 == 0) {
                int64_t j_start = w_halo_y / 2;
                int64_t k_start = w_halo_z / 2;
                int64_t i_start = w_halo_x / 2;
                auto m_w_sliced = m.slice(0, j_start, j_start + ny)
                                   .slice(1, k_start, k_start + nz + 1)
                                   .slice(2, i_start, i_start + nx);
                // w→mass averaging
                m_work = 0.5 * (m_w_sliced.slice(1, 0, nz) + m_w_sliced.slice(1, 1, nz + 1));
            } else if (k_is_w_level) {
                // FIX 2025-01-10 Round23: Warn when w-level input has asymmetric y/x halo
                // This case would silently skip w→mass and potentially be processed incorrectly
                TORCH_WARN_ONCE("align_rdz_or_rdzw_3d: 3D w-level metric [", m_ny, ",", m_nz, ",", m_nx,
                    "] has asymmetric y/x halo (halo_y=", w_halo_y, ", halo_x=", w_halo_x,
                    ") relative to [", ny, ",", nz + 1, ",", nx,
                    "]. Skipping w→mass conversion - verify this is intentional.");
            }
            // If k-dim parity doesn't match w-level, treat as mass with halo (fall through)
        }
        // Update dimensions for further checks (if averaging was applied)
        const int64_t m_ny_work = m_work.size(0);
        const int64_t m_nz_work = m_work.size(1);
        const int64_t m_nx_work = m_work.size(2);

        // Check if already expected shape (staggered)
        if (m_ny_work == exp_ny && m_nz_work == exp_nz && m_nx_work == exp_nx) {
            return normalize_if_needed(m_work).to(opts);
        }

        // FIX 2025-01-10 Round7: Check if staggered shape with halos FIRST
        // This prevents slicing to mass and re-padding (which shifts edges)
        // FIX 2025-01-10 Round21: Use m_work dimensions after possible w→mass averaging
        if (m_ny_work >= exp_ny && m_nz_work >= exp_nz && m_nx_work >= exp_nx) {
            // Compute halo sizes relative to expected staggered shape
            const int64_t halo_y = m_ny_work - exp_ny;
            const int64_t halo_z = m_nz_work - exp_nz;
            const int64_t halo_x = m_nx_work - exp_nx;
            // Validate symmetric (even) halos
            if (halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0) {
                // Slice to expected staggered shape directly (no padding needed)
                int64_t j_start = halo_y / 2;
                int64_t k_start = halo_z / 2;
                int64_t i_start = halo_x / 2;
                auto sliced = m_work.slice(0, j_start, j_start + exp_ny)
                                    .slice(1, k_start, k_start + exp_nz)
                                    .slice(2, i_start, i_start + exp_nx);
                // FIX 2025-01-10 Round8: Normalize AFTER slicing to avoid halo contamination
                return normalize_if_needed(sliced).to(opts);
            }
            // Asymmetric halos relative to expected shape - fall through to mass check
        }

        // Check if mass shape needing padding
        // FIX 2025-01-10 Round21: Use m_work dimensions after possible w→mass averaging
        if (m_ny_work == ny && m_nz_work == nz && m_nx_work == nx) {
            return pad_for_stagger(normalize_if_needed(m_work).to(opts), stag);
        }

        // FIX 2025-01-10 Round6: Handle mass shape with halos
        // Slice to mass interior, then pad for stagger
        // FIX 2025-01-10 Round21: Use m_work dimensions after possible w→mass averaging
        if (m_ny_work >= ny && m_nz_work >= nz && m_nx_work >= nx) {
            // Compute halo sizes relative to mass shape
            const int64_t halo_y = m_ny_work - ny;
            const int64_t halo_z = m_nz_work - nz;
            const int64_t halo_x = m_nx_work - nx;
            TORCH_CHECK(halo_y % 2 == 0 && halo_z % 2 == 0 && halo_x % 2 == 0,
                "align_rdz_or_rdzw_3d: 3D metric halo must be symmetric (even), got halo_y=",
                halo_y, ", halo_z=", halo_z, ", halo_x=", halo_x,
                " for metric [", m_ny_work, ",", m_nz_work, ",", m_nx_work,
                "] vs mass [", ny, ",", nz, ",", nx, "]");

            // Slice to mass interior with symmetric offset
            int64_t j_start = halo_y / 2;
            int64_t k_start = halo_z / 2;
            int64_t i_start = halo_x / 2;
            auto m_interior = m_work.slice(0, j_start, j_start + ny)
                                    .slice(1, k_start, k_start + nz)
                                    .slice(2, i_start, i_start + nx);
            // FIX 2025-01-10 Round8: Normalize AFTER slicing to avoid halo contamination
            return pad_for_stagger(normalize_if_needed(m_interior).to(opts), stag);
        }

        TORCH_CHECK(false,
            "align_rdz_or_rdzw_3d: 3D metric shape [", m_ny_work, ",", m_nz_work, ",", m_nx_work,
            "] doesn't match mass [", ny, ",", nz, ",", nx,
            "] or expected [", exp_ny, ",", exp_nz, ",", exp_nx, "]",
            " (original input [", m_ny, ",", m_nz, ",", m_nx, "])");
    }

    TORCH_CHECK(false, "align_rdz_or_rdzw_3d: metric must be 1D/2D/3D, got dim=", m.dim());
}

/**
 * Convenience wrapper: align rdz for U-staggered operations.
 * FIX 2025-12-29 Batch11 Issue 3: Added normalize flag.
 * FIX 2025-12-31 Batch36 Issue 6: Changed default to true for consistency.
 * All call sites were already passing normalize=true explicitly.
 */
inline torch::Tensor align_rdz_for_u(
    const torch::Tensor& rdz,
    int64_t ny, int64_t nz, int64_t nx,
    const torch::TensorOptions& opts,
    int64_t kdim = -1,
    bool normalize = true)
{
    return align_rdz_or_rdzw_3d(rdz, ny, nz, nx, Stagger::U, opts, kdim, normalize);
}

/**
 * Convenience wrapper: align rdz for V-staggered operations.
 * FIX 2025-12-29 Batch11 Issue 3: Added normalize flag.
 * FIX 2025-12-31 Batch36 Issue 6: Changed default to true for consistency.
 * All call sites were already passing normalize=true explicitly.
 */
inline torch::Tensor align_rdz_for_v(
    const torch::Tensor& rdz,
    int64_t ny, int64_t nz, int64_t nx,
    const torch::TensorOptions& opts,
    int64_t kdim = -1,
    bool normalize = true)
{
    return align_rdz_or_rdzw_3d(rdz, ny, nz, nx, Stagger::V, opts, kdim, normalize);
}

/**
 * Convenience wrapper: align rdzw for mass operations (no padding).
 * FIX 2025-12-29 Batch11 Issue 3: Added normalize flag.
 * FIX 2025-12-31 Batch36 Issue 6: Changed default to true for consistency.
 * All call sites were already passing normalize=true explicitly.
 */
inline torch::Tensor align_rdzw_for_mass(
    const torch::Tensor& rdzw,
    int64_t ny, int64_t nz, int64_t nx,
    const torch::TensorOptions& opts,
    int64_t kdim = -1,
    bool normalize = true)
{
    return align_rdz_or_rdzw_3d(rdzw, ny, nz, nx, Stagger::Mass, opts, kdim, normalize);
}

// ============================================================================
// Static 3D Metric Normalization Cache (FIX 2025-12-28 Issue 5)
// ============================================================================
// For static grid metrics (msf, dn, dnw) that don't change during simulation,
// avoid repeated normalize_positive() calls by caching the normalized result.
//
// Cache invalidation is based on:
// 1. data_ptr: If memory address changes, metric has been reallocated
// 2. epoch: Manual invalidation counter for same-address updates
// 3. signature: First/middle/last values to detect in-place modifications
//
// USAGE:
//   static StaticMetricCache3D rdz_cache;  // One per metric type
//   auto rdz_3d = normalize_metric_3d_cached(grid.rdz, ny, nz, nx, rdz_cache);
//
// INVALIDATION:
//   rdz_cache.invalidate();  // Manual invalidation after grid changes
//   // Or automatically detected via data_ptr/signature change
//   metric_utils::invalidateStaticMetricCaches();  // Invalidate ALL caches globally
//
// THREAD SAFETY (FIX 2025-12-29 Issue 5):
//   StaticMetricCache3D is NOT thread-safe. Each thread must use its own cache.
//   RECOMMENDED: Use thread_local storage for per-thread caches:
//
//     namespace {
//     thread_local StaticMetricCache3D rdz_cache_;
//     thread_local StaticMetricCache3D dnw_cache_;
//     }
//
//   ALTERNATIVE: Use external mutex if shared caches are required.
//
//   GLOBAL INVALIDATION: invalidateStaticMetricCaches() IS thread-safe (atomic).
//   It increments a global epoch that all caches check on access.
// ============================================================================

// FIX 2025-12-29: Global epoch for moving grid cache invalidation
// Increment this when vertical metrics change (moving nest, regrid, etc.)
inline std::atomic<uint64_t>& getGlobalMetricEpoch() {
    static std::atomic<uint64_t> global_epoch{0};
    return global_epoch;
}

/**
 * Invalidate all static metric caches by incrementing global epoch.
 * Call when vertical metrics change (moving nest, regrid, restart).
 */
inline void invalidateStaticMetricCaches() {
    getGlobalMetricEpoch().fetch_add(1, std::memory_order_release);
}

// FIX 2025-12-29 Issue 2: Global moving_nest flag for signature check optimization
// When false, signature checks are skipped (only data_ptr + epoch checked).
// Set to true when using moving nests or dynamic regridding.
inline std::atomic<bool>& isMovingNestMode() {
    static std::atomic<bool> moving_nest_mode{false};
    return moving_nest_mode;
}

// FIX 2025-12-29 Issue 3: Periodic full signature verification period
// When moving_nest=false, signature checks are normally skipped for performance.
// Set this to >0 to enable periodic signature verification every N calls.
// This catches rare in-place modifications that bypass global invalidation.
// Default: 0 (disabled), typical values: 100-1000 for paranoid mode
inline std::atomic<int>& getStaticMetricFullVerifyPeriod() {
    static std::atomic<int> period{0};
    return period;
}

inline void setStaticMetricFullVerifyPeriod(int p) {
    getStaticMetricFullVerifyPeriod().store(p, std::memory_order_release);
}

/**
 * Set moving nest mode for signature check optimization.
 * When true, full 9-point signature is checked every call (safer, slower).
 * When false (default for performance), signature is skipped - only data_ptr + epoch are verified.
 *
 * FIX 2025-12-29 Batch6 Issue 3: Default is false (matches config default).
 * Use setStaticMetricFullVerifyPeriod() for periodic verification in static mode.
 *
 * Call setMovingNestMode(true) if using:
 * - Moving nests (domain follows feature)
 * - Dynamic regridding
 * - Any scenario where metric tensors may be modified in-place without reallocation
 *
 * Call setMovingNestMode(false) for static grids to reduce GPU overhead.
 */
inline void setMovingNestMode(bool enabled) {
    isMovingNestMode().store(enabled, std::memory_order_release);
}

/**
 * FIX 2025-12-30 Batch27 Issue 5: moving_nest_strict mode for GPU performance tradeoff
 * When moving_nest=true AND moving_nest_strict=false:
 *   - GPU tensors use periodic verification instead of every-call verification
 *   - Default GPU period: 10 (vs 1 for strict mode)
 * When moving_nest=true AND moving_nest_strict=true (default):
 *   - All tensors verify every call (original behavior)
 * This allows GPU performance optimization at the cost of accuracy for moving_nest scenarios.
 */
inline std::atomic<bool>& isMovingNestStrictMode() {
    static std::atomic<bool> strict_mode{true};  // Default: strict (original behavior)
    return strict_mode;
}

inline void setMovingNestStrictMode(bool strict) {
    isMovingNestStrictMode().store(strict, std::memory_order_release);
}

inline int getMovingNestGpuPeriod() {
    // GPU verification period when moving_nest=true but strict=false
    // Lower = more accurate, higher = better performance
    return 10;  // Verify every 10 calls instead of every call
}

// FIX 2025-12-30 Batch18 Issue 1: Runtime debug_level accessor for strict mode
// When debug_level >= 2, use aggressive cache verification (period=1)
inline std::atomic<int>& getMetricDebugLevel() {
    static std::atomic<int> debug_level{0};
    return debug_level;
}

inline void setMetricDebugLevel(int level) {
    getMetricDebugLevel().store(level, std::memory_order_release);
}

/**
 * FIX 2025-12-30 Batch16 Issue 1 & 3: Unified verify_period accessor.
 * FIX 2025-12-30 Batch17 Issue 1: Default values when period==0 (auto)
 * FIX 2025-12-30 Batch18 Issue 1: Add debug_level>=2 strict mode
 * FIX 2025-12-30 Batch27 Issue 5: moving_nest_strict option for GPU tradeoff
 * Priority order:
 * 1. moving_nest + strict: return 1 (verify every call)
 * 2. moving_nest + !strict: return GPU period (relaxed for performance)
 * 3. debug_level >= 2: return 1 (strict mode for debugging)
 * 4. period > 0: return configured period
 * 5. period == 0 (auto): return default (Debug: 10, Release: 100)
 * This ensures consistent verify_period policy across all caches.
 *
 * FIX 2025-12-31 Batch35 Issue 4: GPU performance recommendation:
 * For GPU tensors, verify_period >= 2 is recommended to avoid excessive D2H sync.
 * Each full verification requires index_select + D2H transfer for sample extraction.
 * With period=1 (every call), this can degrade GPU throughput significantly.
 * Default moving_nest GPU period is 10 (see getMovingNestGpuPeriod()).
 * For safety-critical scenarios, use moving_nest_strict=true to force period=1.
 */
inline uint64_t getVerifyPeriod() {
    // Priority 1 & 2: moving_nest mode with strict option
    if (isMovingNestMode().load(std::memory_order_acquire)) {
        // FIX 2025-12-30 Batch27 Issue 5: Check strict mode for GPU performance tradeoff
        if (isMovingNestStrictMode().load(std::memory_order_acquire)) {
            return 1;  // Strict mode: verify every call (original behavior)
        } else {
            return getMovingNestGpuPeriod();  // Relaxed: verify periodically on GPU
        }
    }
    // Priority 3: debug_level >= 2 enables strict mode
    if (getMetricDebugLevel().load(std::memory_order_acquire) >= 2) {
        return 1;
    }
    // Priority 4: explicit user-configured period
    int period = getStaticMetricFullVerifyPeriod().load(std::memory_order_acquire);
    if (period > 0) {
        return static_cast<uint64_t>(period);
    }
    // Priority 5: auto (period == 0) uses sensible defaults
#if defined(NDEBUG)
    return 100;  // Release: less frequent verification for performance
#else
    return 10;   // Debug: more frequent verification for safety
#endif
}

// ============================================================================
// FIX 2025-01-10 Round11: 3-point signature extraction helper
// ============================================================================
// Unified helper for extracting (first, middle, last) signature from tensor.
// Used by various caches for content staleness detection.
// Handles CPU contiguous (fast data_ptr), CPU non-contiguous, and GPU paths.

/**
 * Structure for 3-point tensor content signature (first, middle, last elements).
 * Used for cache staleness detection with minimal overhead.
 */
struct Signature3Point {
    double first = 0.0;
    double middle = 0.0;
    double last = 0.0;

    // FIX 2025-01-10 Round18: NaN-safe equality check
    // IEEE 754: NaN != NaN, but for cache purposes we want NaN == NaN (same content)
    // Also handles Inf correctly (Inf == Inf, -Inf == -Inf)
    bool operator==(const Signature3Point& other) const {
        auto nanSafeEquals = [](double a, double b) -> bool {
            if (std::isnan(a) && std::isnan(b)) return true;  // Both NaN: equal
            if (std::isnan(a) || std::isnan(b)) return false; // One NaN: not equal
            return a == b;  // Normal comparison (handles Inf correctly)
        };
        return nanSafeEquals(first, other.first) &&
               nanSafeEquals(middle, other.middle) &&
               nanSafeEquals(last, other.last);
    }
    bool operator!=(const Signature3Point& other) const {
        return !(*this == other);
    }

    /**
     * FIX 2025-01-10 Round19: NaN-safe comparison against individual values.
     * Use this when comparing against cache structures that store first/middle/last separately.
     */
    bool matches(double f, double m, double l) const {
        return *this == Signature3Point{f, m, l};
    }
};

/**
 * Extract 3-point signature (first, middle, last) from tensor.
 * Optimized for different tensor locations and contiguity:
 * - CPU contiguous: direct data_ptr access (zero-copy)
 * - CPU non-contiguous: contiguous() + data_ptr
 * - GPU: index_select with optional cached indices + D2H transfer
 *
 * @param tensor        Input tensor (any shape, will be flattened)
 * @param idx_cache     Optional: cached index tensor [3] on same device as tensor
 *                      Pass nullptr to create temporary indices (slower for GPU)
 * @param cached_numel  Optional: pointer to numel value the idx_cache was created for.
 *                      FIX 2025-01-10 Round12: If provided and matches tensor.numel(),
 *                      skip D2H sync validation. Updated when new indices are created.
 * @return Signature3Point with first/middle/last as doubles
 *
 * Note: For GPU tensors, caller should cache both the index tensor and the numel
 * to avoid repeated D2H sync for validation. See AcousticGravityCoupling::SignatureIndexCache.
 */
inline Signature3Point extractSignature3Point(
    const torch::Tensor& tensor,
    torch::Tensor* idx_cache = nullptr,
    int64_t* cached_numel = nullptr)
{
    Signature3Point sig;
    if (!tensor.defined() || tensor.numel() == 0) {
        return sig;
    }

    torch::NoGradGuard no_grad;

    // Flatten tensor
    bool is_contiguous = tensor.is_contiguous();
    auto flat = is_contiguous ? tensor.reshape({-1}) : tensor.flatten();
    int64_t n = flat.numel();
    if (n == 0) return sig;

    bool is_cpu = flat.device().is_cpu();
    auto dtype = flat.scalar_type();

    if (is_cpu && is_contiguous && dtype == torch::kFloat64) {
        // CPU contiguous FP64: direct access
        auto* ptr = flat.data_ptr<double>();
        sig.first = ptr[0];
        sig.middle = ptr[n / 2];
        sig.last = ptr[n - 1];
    } else if (is_cpu && is_contiguous && dtype == torch::kFloat32) {
        // CPU contiguous FP32: direct access
        auto* ptr = flat.data_ptr<float>();
        sig.first = static_cast<double>(ptr[0]);
        sig.middle = static_cast<double>(ptr[n / 2]);
        sig.last = static_cast<double>(ptr[n - 1]);
    } else if (is_cpu && !is_contiguous) {
        // CPU non-contiguous: make contiguous copy
        auto flat_contig = flat.contiguous();
        if (dtype == torch::kFloat64) {
            auto* ptr = flat_contig.data_ptr<double>();
            sig.first = ptr[0];
            sig.middle = ptr[n / 2];
            sig.last = ptr[n - 1];
        } else if (dtype == torch::kFloat32) {
            auto* ptr = flat_contig.data_ptr<float>();
            sig.first = static_cast<double>(ptr[0]);
            sig.middle = static_cast<double>(ptr[n / 2]);
            sig.last = static_cast<double>(ptr[n - 1]);
        } else {
            // Other CPU dtypes: convert to FP64
            auto flat_fp64 = flat_contig.to(torch::kFloat64);
            auto* ptr = flat_fp64.data_ptr<double>();
            sig.first = ptr[0];
            sig.middle = ptr[n / 2];
            sig.last = ptr[n - 1];
        }
    } else {
        // GPU: use index_select
        torch::Tensor idx;
        if (idx_cache && idx_cache->defined() && idx_cache->numel() == 3 &&
            idx_cache->device() == flat.device()) {
            // Caller provided valid cached indices
            // FIX 2025-01-10 Round12: Use cached_numel to validate without D2H sync
            // If cached_numel matches current n, indices are valid (they're [0, n/2, n-1])
            if (cached_numel && *cached_numel == n) {
                idx = *idx_cache;  // Cache hit without D2H sync
            }
            // If cached_numel not provided or doesn't match, create new indices
        }
        if (!idx.defined()) {
            // Create indices (caller should cache for repeated use)
            // OPT Pass34: Use std::vector to avoid TensorDataContainer overload ambiguity
            std::vector<int64_t> idx_vals = {static_cast<int64_t>(0), n / 2, n - 1};
            idx = torch::tensor(idx_vals,
                torch::TensorOptions().dtype(torch::kLong).device(flat.device()));
            if (idx_cache) {
                *idx_cache = idx;  // Update caller's cache
            }
            if (cached_numel) {
                *cached_numel = n;  // FIX 2025-01-10 Round12: Update cached numel
            }
        }
        auto vals = flat.index_select(0, idx).to(torch::kCPU, torch::kFloat64);
        auto acc = vals.accessor<double, 1>();
        sig.first = acc[0];
        sig.middle = acc[1];
        sig.last = acc[2];
    }

    return sig;
}

/**
 * Cache for static 3D normalized metric tensors.
 * Avoids redundant normalize_positive() calls for unchanging grid metrics.
 *
 * FIX 2025-12-29: Major improvements:
 * 1. Sparse index_select sampling instead of full D2H copy (9 sample points)
 * 2. Device/dtype included in cache key to prevent multi-GPU collision
 * 3. requires_grad tensors bypass cache to preserve AD graph
 * 4. Stride/contiguity check to prevent false hits on transposed views
 * 5. int16_t device index for >127 GPU support
 * 6. FP64-aware signature using raw bytes for double precision inputs
 * 7. Global epoch for moving grid cache invalidation
 * 8. Optimized CPU path avoiding tensor creation in signature computation
 */
class StaticMetricCache3D {
private:
    // Cache key components
    void* cached_data_ptr_ = nullptr;        // Source tensor data_ptr
    int64_t cached_numel_ = 0;               // Source tensor element count
    std::array<uint64_t, 9> cached_sig_bits_ = {0};  // FIX: Raw bits for FP32/FP64 precision
    torch::DeviceType cached_device_type_ = torch::kCPU;  // Device type
    int16_t cached_device_index_ = -1;       // FIX: int16_t for >127 GPU support
    torch::ScalarType cached_dtype_ = torch::kFloat32;  // Source dtype
    bool cached_is_contiguous_ = false;      // FIX: Contiguity check
    bool cached_is_undefined_ = false;       // FIX 2025-12-29 Issue 4: Track undefined source
    // FIX 2025-12-29 Issue 4: Fallback options for undefined source caching
    torch::DeviceType cached_fallback_device_type_ = torch::kCPU;
    int16_t cached_fallback_device_index_ = -1;
    torch::ScalarType cached_fallback_dtype_ = torch::kFloat32;
    int64_t cached_ny_ = 0, cached_nz_ = 0, cached_nx_ = 0;  // Target shape
    // FIX 2025-01-10 Round11: kdim parameter for 2D cubic grid disambiguation
    int64_t cached_kdim_ = -1;               // kdim used for normalization (-1=auto, 0/1=explicit)
    // FIX 2025-01-10 Round18: eps parameter for cache key (different eps → different normalization)
    float cached_eps_ = 0.0f;                // eps used for normalize_positive (0=auto)
    uint64_t cached_global_epoch_ = 0;       // FIX: Global epoch snapshot

    // Cached result
    torch::Tensor cached_result_;            // Cached 3D normalized tensor
    uint64_t local_epoch_ = 0;               // Manual invalidation epoch
    // FIX 2025-12-29 Issue 3: Counter for periodic signature verification
    mutable uint64_t sig_verify_counter_ = 0;
    // FIX 2025-12-29 Batch8 Issue 5: Track last-seen config values for counter reset
    // If moving_nest or verify_period changes at runtime, we need to reset the counter
    // to ensure the new policy takes effect immediately
    mutable bool last_moving_nest_ = false;
    mutable int last_verify_period_ = 0;

    // Pre-computed sample indices (computed once per shape)
    // OPT Pass34: mutable allows lazy computation in const methods (cache pattern)
    mutable torch::Tensor sample_indices_;           // [9] indices for sparse sampling
    mutable int64_t sample_indices_numel_ = 0;       // numel for which indices were computed

    // Pre-computed CPU sample indices (for fast CPU path)
    // OPT Pass34: mutable allows computation in const methods without const_cast
    mutable std::array<int64_t, 9> cpu_sample_indices_ = {0};
    mutable int64_t cpu_sample_indices_numel_ = 0;

    // FIX 2025-12-29 Issue 5: Non-contiguous view caching
    // When same view pattern repeats, reuse cached contiguous copy
    torch::Tensor cached_noncontig_src_;     // Contiguous copy of non-contiguous source
    void* cached_noncontig_data_ptr_ = nullptr;  // Original non-contig data_ptr
    std::vector<int64_t> cached_noncontig_strides_;  // Stride pattern for matching
    // FIX 2025-12-29 Batch10 Issue 2: Also store sizes to prevent edge case mismatch
    // Same numel/stride can theoretically have different shapes (e.g., [6] vs [2,3] with stride [1])
    std::vector<int64_t> cached_noncontig_sizes_;
    int64_t cached_noncontig_numel_ = 0;
    // FIX 2025-12-29 Issue 4: dtype/device for non-contiguous view cache key
    torch::DeviceType cached_noncontig_device_type_ = torch::kCPU;
    int16_t cached_noncontig_device_index_ = -1;
    torch::ScalarType cached_noncontig_dtype_ = torch::kFloat32;
    // FIX 2025-12-29 Batch6 Issue 2: epoch for non-contiguous cache invalidation
    uint64_t cached_noncontig_epoch_ = 0;

    // FIX 2025-12-29 Issue 3: 4-slot LRU cache for device/dtype conversions
    // Avoids repeated .to() calls in multi-device environments (CPU+GPU0+GPU1+mixed dtype)
    static constexpr int kMaxConvertedSlots = 4;
    struct ConvertedSlot {
        torch::Tensor tensor;
        torch::DeviceType device_type = torch::kCPU;
        int16_t device_index = -1;
        torch::ScalarType dtype = torch::kFloat32;
        uint64_t access_count = 0;  // For LRU tracking
        bool valid = false;

        void clear() {
            tensor = torch::Tensor();
            valid = false;
            access_count = 0;
        }

        bool matches(const torch::Device& dev, torch::ScalarType dt) const {
            if (!valid) return false;
            if (dev.type() != device_type) return false;
            // FIX Round178: Use safe_device_index for range-checked int16_t conversion
            int16_t dev_idx = dev.has_index() ? safe_device_index(dev.index()) : -1;
            if (dev_idx != device_index) return false;
            return dtype == dt;
        }

        void set(const torch::Tensor& t, const torch::Device& dev, torch::ScalarType dt, uint64_t count) {
            tensor = t;
            device_type = dev.type();
            // FIX Round178: Use safe_device_index for range-checked int16_t conversion
            device_index = dev.has_index() ? safe_device_index(dev.index()) : -1;
            dtype = dt;
            access_count = count;
            valid = true;
        }
    };
    std::array<ConvertedSlot, kMaxConvertedSlots> converted_slots_;
    uint64_t global_access_count_ = 0;  // Monotonic counter for LRU tracking

    /**
     * FIX 2025-12-29 Issue 5: Check if non-contiguous view matches cached pattern.
     * FIX 2025-12-29 Issue 4: Also check dtype/device to prevent cross-device cache misuse.
     * FIX 2025-12-29 Batch6 Issue 2: Also check global epoch for cache invalidation.
     */
    bool matchesNonContigView(const torch::Tensor& src) const {
        if (!cached_noncontig_src_.defined()) return false;
        // FIX 2025-12-29 Batch6 Issue 2: Check global epoch first (quick rejection)
        if (getGlobalMetricEpoch().load(std::memory_order_acquire) != cached_noncontig_epoch_) {
            return false;
        }
        // FIX 2025-12-29 Batch9 Issue 4: In moving_nest mode, always re-contiguify
        // Non-contig view cache doesn't track in-place modifications to original data.
        // If invalidate() is missed, stale contiguous copy could be reused.
        // Conservative approach: skip cache entirely in moving_nest mode.
        //
        // FIX 2025-12-31 Batch35 Issue 6: Why bypass is necessary:
        // Moving nests can modify underlying storage without changing data_ptr or numel.
        // A cached contiguous copy would reference stale data since .contiguous() creates
        // a new allocation with copied values. The original non-contig view would still
        // pass data_ptr/numel checks but contain different values after in-place update.
        // Bypassing ensures we always call .contiguous() to get fresh data.
        if (isMovingNestMode().load(std::memory_order_acquire)) {
            return false;  // Force fresh contiguous copy
        }
        if (src.data_ptr() != cached_noncontig_data_ptr_) return false;
        if (src.numel() != cached_noncontig_numel_) return false;
        // FIX 2025-12-29 Issue 4: Check device type/index and dtype
        // FIX Round163: Use safe device index to prevent overflow on large GPU clusters
        if (src.device().type() != cached_noncontig_device_type_) return false;
        // FIX Round191: Use centralized safe_device_index() for consistent range checking
        int16_t src_dev_idx = wrf::sdirk3::safe_device_index(src);
        if (src_dev_idx != cached_noncontig_device_index_) return false;
        if (src.scalar_type() != cached_noncontig_dtype_) return false;
        auto strides = src.strides();
        if (static_cast<size_t>(strides.size()) != cached_noncontig_strides_.size()) return false;
        for (size_t i = 0; i < cached_noncontig_strides_.size(); ++i) {
            if (strides[i] != cached_noncontig_strides_[i]) return false;
        }
        // FIX 2025-12-29 Batch10 Issue 2: Also check sizes to prevent edge case mismatch
        auto sizes = src.sizes();
        if (static_cast<size_t>(sizes.size()) != cached_noncontig_sizes_.size()) return false;
        for (size_t i = 0; i < cached_noncontig_sizes_.size(); ++i) {
            if (sizes[i] != cached_noncontig_sizes_[i]) return false;
        }
        return true;
    }

    /**
     * FIX 2025-12-29 Issue 5: Cache contiguous copy of non-contiguous source.
     * FIX 2025-12-29 Issue 4: Also store dtype/device for proper cache key matching.
     * FIX 2025-12-29 Batch6 Issue 2: Also store epoch for invalidation support.
     */
    void cacheNonContigView(const torch::Tensor& src, const torch::Tensor& contig) {
        cached_noncontig_src_ = contig;
        cached_noncontig_data_ptr_ = src.data_ptr();
        cached_noncontig_numel_ = src.numel();
        // FIX 2025-12-29 Issue 4: Store device and dtype
        // FIX Round163: Use safe device index to prevent overflow on large GPU clusters
        // FIX Round191: Use centralized safe_device_index() for consistent range checking
        cached_noncontig_device_type_ = src.device().type();
        cached_noncontig_device_index_ = wrf::sdirk3::safe_device_index(src);
        cached_noncontig_dtype_ = src.scalar_type();
        // FIX 2025-12-29 Batch6 Issue 2: Store global epoch for invalidation check
        cached_noncontig_epoch_ = getGlobalMetricEpoch().load(std::memory_order_acquire);
        auto strides = src.strides();
        cached_noncontig_strides_.assign(strides.begin(), strides.end());
        // FIX 2025-12-29 Batch10 Issue 2: Also store sizes for shape validation
        auto sizes = src.sizes();
        cached_noncontig_sizes_.assign(sizes.begin(), sizes.end());
    }

    /**
     * Compute sample indices (integer array, not tensor).
     * OPT Pass34: const because members are mutable (cache pattern)
     */
    void computeSampleIndicesArray(int64_t numel) const {
        if (cpu_sample_indices_numel_ == numel) return;

        cpu_sample_indices_[0] = 0;
        cpu_sample_indices_[1] = numel / 8;
        cpu_sample_indices_[2] = numel / 4;
        cpu_sample_indices_[3] = 3 * numel / 8;
        cpu_sample_indices_[4] = numel / 2;
        cpu_sample_indices_[5] = 5 * numel / 8;
        cpu_sample_indices_[6] = 3 * numel / 4;
        cpu_sample_indices_[7] = 7 * numel / 8;
        cpu_sample_indices_[8] = numel - 1;

        for (auto& idx : cpu_sample_indices_) {
            idx = std::max(int64_t(0), std::min(idx, numel - 1));
        }
        cpu_sample_indices_numel_ = numel;
    }

    /**
     * Compute sample indices tensor for GPU path.
     * OPT Pass34: const because sample_indices_ is mutable (cache pattern)
     */
    void ensureSampleIndicesTensor(int64_t numel, const torch::Device& device) const {
        if (sample_indices_.defined() && sample_indices_numel_ == numel &&
            sample_indices_.device() == device) {
            return;
        }

        computeSampleIndicesArray(numel);
        sample_indices_ = torch::tensor(
            std::vector<int64_t>(cpu_sample_indices_.begin(), cpu_sample_indices_.end()),
            torch::TensorOptions().dtype(torch::kLong).device(device));
        sample_indices_numel_ = numel;
    }

    /**
     * Convert sample value to raw bits for comparison.
     * FIX 2025-12-29: Handles FP32, FP64, FP16, and BF16 with full precision.
     */
    static uint64_t valueToBits(float val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;  // NaN sentinel
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    static uint64_t valueToBits(double val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;  // NaN sentinel
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return bits;
    }

    // FIX 2025-12-29 Issue 2: Direct FP16/BF16 handling without full-copy conversion
    static uint64_t valueToBits(at::Half val) {
        // at::Half stores 16-bit IEEE FP16 representation
        uint16_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        // Check for NaN/Inf: FP16 exponent=0x1F and mantissa!=0 is NaN, mantissa==0 is Inf
        uint16_t exp = (bits >> 10) & 0x1F;
        // Note: mantissa check could distinguish NaN vs Inf but both map to sentinel
        (void)(bits & 0x3FF);  // Suppress unused warning - mant extracted but only exp checked
        if (exp == 0x1F) {
            return 0xFFFFFFFFFFFFFFFFULL;  // NaN/Inf sentinel
        }
        return static_cast<uint64_t>(bits);
    }

    static uint64_t valueToBits(at::BFloat16 val) {
        // at::BFloat16 stores 16-bit BF16 representation (truncated FP32)
        uint16_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        // Check for NaN/Inf: BF16 exponent=0xFF and mantissa!=0 is NaN, mantissa==0 is Inf
        uint8_t exp = (bits >> 7) & 0xFF;
        // Note: mantissa check could distinguish NaN vs Inf but both map to sentinel
        (void)(bits & 0x7F);  // Suppress unused warning - mant extracted but only exp checked
        if (exp == 0xFF) {
            return 0xFFFFFFFFFFFFFFFFULL;  // NaN/Inf sentinel
        }
        return static_cast<uint64_t>(bits);
    }

    /**
     * Compute sparse signature using optimized path based on device/contiguity.
     * FIX 2025-12-29: CPU+contiguous uses direct pointer, GPU uses index_select.
     * OPT Pass34: const because cpu_sample_indices_ is mutable (cache pattern)
     */
    std::array<uint64_t, 9> computeSignatureSparse(const torch::Tensor& src) const {
        std::array<uint64_t, 9> sig = {0};
        if (!src.defined() || src.numel() == 0) return sig;

        int64_t n = src.numel();
        computeSampleIndicesArray(n);

        // FIX 2025-12-29 Issue 5: Fast CPU path for contiguous tensors
        // FIX 2025-12-29 Issue 2: Direct FP16/BF16 pointer access (avoids O(n) copy)
        if (src.is_cpu() && src.is_contiguous()) {
            switch (src.scalar_type()) {
                case torch::kFloat64: {
                    const double* ptr = src.data_ptr<double>();
                    for (int i = 0; i < 9; ++i) {
                        sig[i] = valueToBits(ptr[cpu_sample_indices_[i]]);
                    }
                    break;
                }
                case torch::kFloat32: {
                    const float* ptr = src.data_ptr<float>();
                    for (int i = 0; i < 9; ++i) {
                        sig[i] = valueToBits(ptr[cpu_sample_indices_[i]]);
                    }
                    break;
                }
                case torch::kFloat16: {
                    // FIX 2025-12-29 Issue 2: Direct at::Half pointer (no .to() copy)
                    const at::Half* ptr = src.data_ptr<at::Half>();
                    for (int i = 0; i < 9; ++i) {
                        sig[i] = valueToBits(ptr[cpu_sample_indices_[i]]);
                    }
                    break;
                }
                case torch::kBFloat16: {
                    // FIX 2025-12-29 Issue 2: Direct at::BFloat16 pointer (no .to() copy)
                    const at::BFloat16* ptr = src.data_ptr<at::BFloat16>();
                    for (int i = 0; i < 9; ++i) {
                        sig[i] = valueToBits(ptr[cpu_sample_indices_[i]]);
                    }
                    break;
                }
                default: {
                    // Other dtypes: fallback to FP32 conversion (rare in WRF)
                    auto src_f32 = src.to(torch::kFloat32);
                    const float* ptr = src_f32.data_ptr<float>();
                    for (int i = 0; i < 9; ++i) {
                        sig[i] = valueToBits(ptr[cpu_sample_indices_[i]]);
                    }
                    break;
                }
            }
            return sig;
        }

        // GPU or non-contiguous path: use index_select
        ensureSampleIndicesTensor(n, src.device());
        auto flat = src.reshape({-1});
        auto samples = torch::index_select(flat, 0, sample_indices_);

        // FIX 2025-12-29 Issue 3: Preserve FP64 precision
        if (src.scalar_type() == torch::kFloat64) {
            auto samples_cpu = samples.to(torch::kCPU, torch::kFloat64).contiguous();
            const double* ptr = samples_cpu.data_ptr<double>();
            for (int i = 0; i < 9; ++i) {
                sig[i] = valueToBits(ptr[i]);
            }
        } else {
            auto samples_cpu = samples.to(torch::kCPU, torch::kFloat32).contiguous();
            const float* ptr = samples_cpu.data_ptr<float>();
            for (int i = 0; i < 9; ++i) {
                sig[i] = valueToBits(ptr[i]);
            }
        }
        return sig;
    }

public:
    StaticMetricCache3D() = default;

    /**
     * Check if cache is valid for given source tensor and target shape.
     * FIX 2025-12-29: Includes device/dtype/contiguity/global_epoch in key check.
     * FIX 2025-12-29 Issue 4: Handles undefined source tensors (cached fallback).
     * FIX 2025-01-10 Round11: Added kdim parameter for 2D cubic grid cache key.
     * FIX 2025-01-10 Round18: Added eps parameter for cache key.
     */
    bool isValid(const torch::Tensor& src, int64_t ny, int64_t nz, int64_t nx,
                 float eps,
                 const torch::TensorOptions& fallback_opts = torch::TensorOptions(),
                 int64_t kdim = -1) const {
        if (!cached_result_.defined()) return false;

        // FIX 2025-12-29 Issue 4: Check global epoch for moving grid invalidation
        if (getGlobalMetricEpoch().load(std::memory_order_acquire) != cached_global_epoch_) {
            return false;
        }

        // Check target shape first (quick rejection)
        if (ny != cached_ny_ || nz != cached_nz_ || nx != cached_nx_) return false;

        // FIX 2025-01-10 Round11: Check kdim for 2D cubic grid disambiguation
        if (kdim != cached_kdim_) return false;

        // FIX 2025-01-10 Round18: Check eps for normalization consistency
        // Note: eps=0 means auto-detect, so different eps values produce different results
        if (eps != cached_eps_) return false;

        // FIX 2025-12-29 Issue 4: Handle undefined source specially
        // If src is undefined and we cached an undefined result, check fallback options match
        if (!src.defined()) {
            if (!cached_is_undefined_) return false;  // Cached defined, src undefined
            // Check fallback options match (avoid repeated conversion on option changes)
            torch::DeviceType fb_device_type = fallback_opts.has_device() ?
                fallback_opts.device().type() : torch::kCPU;
            // FIX Round164: Use safe device index to prevent overflow on large GPU clusters
            // FIX Round191: Use centralized safe_device_index() for consistent range checking
            int16_t fb_device_index = fallback_opts.has_device()
                ? wrf::sdirk3::safe_device_index(fallback_opts.device()) : -1;
            torch::ScalarType fb_dtype = fallback_opts.has_dtype() ?
                fallback_opts.dtype().toScalarType() : torch::kFloat32;
            if (fb_device_type != cached_fallback_device_type_) return false;
            if (fb_device_index != cached_fallback_device_index_) return false;
            if (fb_dtype != cached_fallback_dtype_) return false;
            return true;  // Undefined source with matching fallback options
        }

        // If src is defined but we cached undefined, it's a miss
        if (cached_is_undefined_) return false;

        // Check data_ptr (memory location)
        if (src.data_ptr() != cached_data_ptr_) return false;

        // Check numel
        if (src.numel() != cached_numel_) return false;

        // FIX 2025-12-29 Issue 1: Check contiguity (transposed views have same ptr/numel)
        if (src.is_contiguous() != cached_is_contiguous_) return false;

        // Check device type and index
        if (src.device().type() != cached_device_type_) return false;
        // FIX 2025-12-29 Issue 2: Use int16_t comparison
        // FIX Round164: Use safe device index to prevent overflow on large GPU clusters
        // FIX Round191: Use centralized safe_device_index() for consistent range checking
        int16_t src_index = wrf::sdirk3::safe_device_index(src);
        if (src_index != cached_device_index_) return false;

        // Check source dtype
        if (src.scalar_type() != cached_dtype_) return false;

        // FIX 2025-01-10 Round14: Check that cached result matches expected device/dtype from fallback_options
        // Without this, different fallback_options would incorrectly hit the cache and return wrong device/dtype
        torch::Device expected_device = fallback_opts.has_device() ? fallback_opts.device() : src.device();
        if (cached_result_.device() != expected_device) return false;
        torch::ScalarType expected_dtype = fallback_opts.has_dtype() ?
            fallback_opts.dtype().toScalarType() : src.scalar_type();
        if (cached_result_.scalar_type() != expected_dtype) return false;

        // FIX 2025-12-29 Issue 2: Skip signature check when not in moving nest mode
        // For static grids, data_ptr + epoch are sufficient for cache validity.
        // This avoids GPU index_select + D2H overhead on every call.
        // FIX 2025-12-29 Issue 3: But do periodic check if full_verify_period > 0
        bool current_moving_nest = isMovingNestMode().load(std::memory_order_acquire);
        int current_verify_period = getStaticMetricFullVerifyPeriod().load(std::memory_order_acquire);

        // FIX 2025-12-29 Batch8 Issue 5: Detect config changes and reset counter
        // This ensures new policy takes effect immediately when settings change at runtime
        if (current_moving_nest != last_moving_nest_ || current_verify_period != last_verify_period_) {
            sig_verify_counter_ = 0;
            last_moving_nest_ = current_moving_nest;
            last_verify_period_ = current_verify_period;
        }

        bool do_signature_check = current_moving_nest;
        if (!do_signature_check) {
            if (current_verify_period > 0) {
                ++sig_verify_counter_;
                if ((sig_verify_counter_ % static_cast<uint64_t>(current_verify_period)) == 0) {
                    do_signature_check = true;  // Periodic verification
                }
            }
        }

        // FIX 2025-12-30 Batch20 Issue 3: CPU cheap signature even when verify_period==0
        // from_blob CPU in-place updates won't change data_ptr but will change values
        // For CPU contiguous tensors, do cheap 3-point check every call (no D2H overhead)
        if (!do_signature_check && src.device().type() == torch::kCPU && src.is_contiguous()) {
            // Cheap 3-point signature: first, middle, last values
            int64_t n = src.numel();
            if (n >= 3) {
                constexpr uint64_t NONFINITE_SENTINEL = 0xFFFFFFFFFFFFFFFFULL;
                uint64_t sig[3];
                if (src.scalar_type() == torch::kFloat64) {
                    const double* ptr = src.data_ptr<double>();
                    sig[0] = valueToBits(ptr[0]);
                    sig[1] = valueToBits(ptr[n/2]);
                    sig[2] = valueToBits(ptr[n-1]);
                } else if (src.scalar_type() == torch::kFloat32) {
                    const float* ptr = src.data_ptr<float>();
                    sig[0] = valueToBits(ptr[0]);
                    sig[1] = valueToBits(ptr[n/2]);
                    sig[2] = valueToBits(ptr[n-1]);
                } else {
                    // Other dtypes: skip cheap check, rely on periodic full check
                    return true;
                }
                // Quick 3-point comparison against cached signature (indices 0, 4, 8 of 9-point)
                if (sig[0] != cached_sig_bits_[0] || sig[1] != cached_sig_bits_[4] || sig[2] != cached_sig_bits_[8]) {
                    return false;  // CPU in-place change detected
                }
                if (sig[0] == NONFINITE_SENTINEL || sig[1] == NONFINITE_SENTINEL || sig[2] == NONFINITE_SENTINEL) {
                    return false;  // Non-finite detected
                }
            }
            return true;  // CPU cheap check passed
        }

        if (!do_signature_check) {
            return true;  // GPU tensor: data_ptr + epoch checks passed, skip signature
        }

        // Check sparse signature (detect in-place modifications)
        // Executed when isMovingNestMode() == true OR periodic verification triggered
        // OPT Pass34: No const_cast needed - computeSignatureSparse is now const
        auto current_sig = computeSignatureSparse(src);

        // FIX 2025-12-29 Batch13 Issue 4: Cache bypass when non-finite values detected
        // Non-finite sentinel (0xFFFFFFFFFFFFFFFFULL) in either cached or current signature
        // indicates potentially unstable data - force recomputation to avoid stale cache
        constexpr uint64_t NONFINITE_SENTINEL = 0xFFFFFFFFFFFFFFFFULL;
        for (int i = 0; i < 9; ++i) {
            if (cached_sig_bits_[i] == NONFINITE_SENTINEL ||
                current_sig[i] == NONFINITE_SENTINEL) {
                return false;  // Bypass cache for non-finite data
            }
            if (current_sig[i] != cached_sig_bits_[i]) {
                return false;
            }
        }

        return true;
    }

    /**
     * Get cached result if valid, or compute and cache new result.
     * FIX 2025-12-29: Bypasses cache for requires_grad/non-contiguous tensors.
     * FIX 2025-01-10 Round11: Added kdim parameter for 2D cubic grid disambiguation.
     */
    torch::Tensor getOrCompute(
        const torch::Tensor& src,
        int64_t ny, int64_t nz, int64_t nx,
        float eps,
        const torch::TensorOptions& fallback_opts,
        int64_t kdim = -1)
    {
        // Bypass cache for requires_grad tensors (preserve AD graph)
        if (src.defined() && src.requires_grad()) {
            requiresGradCheck(src, "normalize_metric_3d_cached: metric has requires_grad=true");
            return normalize_metric_3d(src, ny, nz, nx, eps, fallback_opts, kdim);
        }

        // FIX 2025-12-29 Issue 5: Handle non-contiguous tensors with view caching
        // If same view pattern repeats, reuse cached contiguous copy
        // FIX 2025-12-29 Issue 2: Re-enter getOrCompute() with contiguous source to use main cache
        if (src.defined() && !src.is_contiguous()) {
            torch::Tensor contig_src;
            if (matchesNonContigView(src)) {
                // Reuse cached contiguous copy
                contig_src = cached_noncontig_src_;
            } else {
                // Make contiguous and cache for future use
                contig_src = src.contiguous();
                cacheNonContigView(src, contig_src);
            }
            // Recursive call with contiguous source - allows main cache to work
            // FIX 2025-01-10 Round11: Pass kdim to recursive call
            return getOrCompute(contig_src, ny, nz, nx, eps, fallback_opts, kdim);
        }

        // Check cache validity (pass fallback_opts for undefined source matching)
        // FIX 2025-01-10 Round11: Include kdim in cache validity check
        // FIX 2025-01-10 Round18: Include eps in cache validity check
        if (isValid(src, ny, nz, nx, eps, fallback_opts, kdim)) {
            auto target_device = fallback_opts.has_device() ?
                fallback_opts.device() : cached_result_.device();
            auto target_dtype = fallback_opts.has_dtype() ?
                fallback_opts.dtype().toScalarType() : cached_result_.scalar_type();

            // Direct hit: cached result matches target device/dtype
            if (cached_result_.device() == target_device &&
                cached_result_.scalar_type() == target_dtype) {
                return cached_result_;
            }

            // FIX 2025-12-29 Issue 3: Check 4-slot LRU cache for converted results
            // This avoids repeated .to() calls in multi-device environments
            ++global_access_count_;
            // FIX 2025-12-29 Issue 5: Handle uint64_t overflow (extremely rare, ~10^18 ops)
            // When counter wraps to 0, reset all slot counters to prevent LRU confusion
            if (global_access_count_ == 0) {
                for (int j = 0; j < kMaxConvertedSlots; ++j) {
                    converted_slots_[j].access_count = 0;
                }
                global_access_count_ = 1;  // Start fresh
            }
            for (int i = 0; i < kMaxConvertedSlots; ++i) {
                if (converted_slots_[i].matches(target_device, target_dtype)) {
                    // Update access count for LRU tracking
                    converted_slots_[i].access_count = global_access_count_;
                    return converted_slots_[i].tensor;
                }
            }

            // No cached conversion found - find LRU slot to evict
            int lru_slot = 0;
            uint64_t min_access = converted_slots_[0].access_count;
            for (int i = 1; i < kMaxConvertedSlots; ++i) {
                if (!converted_slots_[i].valid) {
                    lru_slot = i;  // Use empty slot first
                    break;
                }
                if (converted_slots_[i].access_count < min_access) {
                    min_access = converted_slots_[i].access_count;
                    lru_slot = i;
                }
            }

            // Convert and store in LRU slot
            auto converted = cached_result_.to(target_device, target_dtype);
            converted_slots_[lru_slot].set(converted, target_device, target_dtype, global_access_count_);
            return converted;
        }

        // Cache miss - compute
        torch::Tensor result;

        if (!src.defined() || src.numel() == 0) {
            auto opts = getDefaultMetricOptions();
            if (fallback_opts.has_device()) opts = opts.device(fallback_opts.device());
            if (fallback_opts.has_dtype()) opts = opts.dtype(fallback_opts.dtype());
            float effective_eps = (eps > 0.0f) ? eps : getEpsForOptions(opts);
            result = torch::full({ny, nz, nx}, effective_eps, opts);
        } else if (src.dim() == 3 && src.size(0) == ny && src.size(1) == nz && src.size(2) == nx) {
            float effective_eps = (eps > 0.0f) ? eps : getEpsForDtype(src.scalar_type());
            result = normalize_positive(src, effective_eps);
            if (fallback_opts.has_device() && result.device() != fallback_opts.device()) {
                result = result.to(fallback_opts.device());
            }
        } else {
            // FIX 2025-01-10 Round11: Pass kdim to normalize_metric_3d
            result = normalize_metric_3d(src, ny, nz, nx, eps, fallback_opts, kdim);
        }

        // Update cache
        // FIX 2025-12-29 Issue 4: Track whether source was undefined and store fallback options
        cached_is_undefined_ = !src.defined();
        cached_data_ptr_ = src.defined() ? src.data_ptr() : nullptr;
        cached_numel_ = src.defined() ? src.numel() : 0;
        cached_sig_bits_ = computeSignatureSparse(src);
        cached_device_type_ = src.defined() ? src.device().type() : torch::kCPU;
        // FIX Round164: Use safe device index to prevent overflow on large GPU clusters
        // FIX Round191: Use centralized safe_device_index() for consistent range checking
        cached_device_index_ = src.defined() ? wrf::sdirk3::safe_device_index(src) : -1;
        cached_dtype_ = src.defined() ? src.scalar_type() : torch::kFloat32;
        cached_is_contiguous_ = src.defined() ? src.is_contiguous() : false;
        // FIX 2025-12-29 Issue 4: Store fallback options for undefined source matching
        cached_fallback_device_type_ = fallback_opts.has_device() ?
            fallback_opts.device().type() : torch::kCPU;
        // FIX Round164: Use safe device index to prevent overflow on large GPU clusters
        // FIX Round191: Use centralized safe_device_index() for consistent range checking
        cached_fallback_device_index_ = fallback_opts.has_device()
            ? wrf::sdirk3::safe_device_index(fallback_opts.device()) : -1;
        cached_fallback_dtype_ = fallback_opts.has_dtype() ?
            fallback_opts.dtype().toScalarType() : torch::kFloat32;
        cached_global_epoch_ = getGlobalMetricEpoch().load(std::memory_order_acquire);
        cached_ny_ = ny;
        cached_nz_ = nz;
        cached_nx_ = nx;
        // FIX 2025-01-10 Round11: Store kdim for cache key matching
        cached_kdim_ = kdim;
        // FIX 2025-01-10 Round18: Store eps for cache key matching
        cached_eps_ = eps;
        cached_result_ = result;
        // FIX 2025-12-29 Issue 3: Clear all converted slots on primary cache update
        for (int i = 0; i < kMaxConvertedSlots; ++i) {
            converted_slots_[i].clear();
        }
        global_access_count_ = 0;
        // FIX 2025-12-29 Batch6 Issue 4: Reset signature verify counter on cache rebuild
        sig_verify_counter_ = 0;

        return result;
    }

    /**
     * Manual cache invalidation.
     */
    void invalidate() {
        cached_data_ptr_ = nullptr;
        cached_numel_ = 0;
        cached_sig_bits_ = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        cached_is_undefined_ = false;  // FIX 2025-12-29 Issue 4: Reset undefined flag
        // FIX 2025-01-10 Round18: Reset eps to default (auto)
        cached_eps_ = 0.0f;
        cached_kdim_ = -1;  // Also reset kdim for completeness
        cached_result_ = torch::Tensor();
        sample_indices_ = torch::Tensor();
        sample_indices_numel_ = 0;
        // FIX 2025-12-29 Issue 3: Clear all converted slots on invalidation
        for (int i = 0; i < kMaxConvertedSlots; ++i) {
            converted_slots_[i].clear();
        }
        global_access_count_ = 0;
        // FIX 2025-12-29 Batch6 Issue 4: Reset signature verify counter to ensure immediate check
        sig_verify_counter_ = 0;
        // FIX 2025-12-29 Issue 5: Clear non-contiguous view cache
        cached_noncontig_src_ = torch::Tensor();
        cached_noncontig_data_ptr_ = nullptr;
        cached_noncontig_strides_.clear();
        cached_noncontig_numel_ = 0;
        ++local_epoch_;
    }

    uint64_t epoch() const { return local_epoch_; }
    bool hasCachedResult() const { return cached_result_.defined(); }
};

/**
 * Normalize metric tensor for 3D broadcasting with caching for static metrics.
 *
 * FIX 2025-12-28 Issue 5: For static grid metrics (msf, dn, dnw, rdz, rdzw),
 * this function caches the normalized result to avoid repeated tensor creation.
 *
 * USAGE:
 *   static StaticMetricCache3D rdz_cache;  // Declare once per metric
 *   auto rdz_3d = normalize_metric_3d_cached(grid.rdz, ny, nz, nx, rdz_cache);
 *
 * THREAD SAFETY (FIX 2025-12-29 Issue 5):
 * StaticMetricCache3D is NOT thread-safe. Each thread must have its own cache
 * instance. The recommended pattern is to use thread_local storage:
 *
 *   namespace {
 *   thread_local StaticMetricCache3D rdz_cache_;
 *   thread_local StaticMetricCache3D dnw_cache_;
 *   }  // anonymous namespace
 *
 *   void myFunction(const WRFGridInfo& grid) {
 *       auto rdz = normalize_metric_3d_cached(grid.rdz, ny, nz, nx, rdz_cache_);
 *       auto dnw = normalize_metric_3d_cached(grid.dnw, ny, nz, nx, dnw_cache_);
 *   }
 *
 * If shared caches are required (e.g., memory-constrained environments),
 * external synchronization (mutex) must be used around all cache access.
 *
 * Global cache invalidation via invalidateStaticMetricCaches() is thread-safe
 * (uses atomic operations), but individual cache instances are not.
 *
 * @param metric          Input metric tensor (1D, 2D, or 3D)
 * @param ny, nz, nx      Target 3D shape
 * @param cache           Reference to cache object (must be persistent)
 * @param eps             Epsilon for normalization (0 = auto-detect)
 * @param fallback_options Target device/dtype options
 * @param kdim            FIX 2025-01-10 Round11: Explicit k-dim for 2D cubic grid disambiguation
 *                        (-1=auto, 0=[nz,nx], 1=[ny,nz]). Part of cache key.
 * @return                Normalized 3D tensor [ny, nz, nx]
 */
inline torch::Tensor normalize_metric_3d_cached(
    const torch::Tensor& metric,
    int ny, int nz, int nx,
    StaticMetricCache3D& cache,
    float eps = 0.0f,
    const torch::TensorOptions& fallback_options = torch::TensorOptions(),
    int64_t kdim = -1)
{
    return cache.getOrCompute(metric, ny, nz, nx, eps, fallback_options, kdim);
}

} // namespace metric_utils

// ============================================================================
// Grid Metric Cache Invalidation
// ============================================================================
// FIX 2025-12-26: When grid metrics (z_at_w, dz, dn, dnw) are modified in-place,
// call these functions to invalidate cached reductions:
//
//   #include "wrf_sdirk3_rayleigh_damping_ad.h"  // For invalidateZ1DCache()
//   #include "wrf_sdirk3_boundary_ad.h"         // For invalidateDzMinCache()
//
//   // After modifying grid metrics in-place:
//   wrf::sdirk3::invalidateZ1DCache();
//   wrf::sdirk3::invalidateDzMinCache();
//
// Typical call sites:
//   - After grid initialization
//   - After regridding/domain decomposition
//   - After restart from checkpoint
//   - After vertical level adjustment
// ============================================================================

} // namespace sdirk3
} // namespace wrf
