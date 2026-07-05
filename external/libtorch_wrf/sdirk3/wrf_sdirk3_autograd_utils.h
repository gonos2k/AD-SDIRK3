#ifndef WRF_SDIRK3_AUTOGRAD_UTILS_H
#define WRF_SDIRK3_AUTOGRAD_UTILS_H

#include <torch/torch.h>
#include "wrf_sdirk3_config.h"

namespace wrf {
namespace sdirk3 {

// ============================================================================
// AUTOGRAD-AWARE SCALAR EXTRACTION UTILITIES
// ============================================================================
// These utilities provide conditional behavior based on use_autograd flag:
//   - use_autograd=false: Use NoGradGuard + .item() (FD production mode)
//   - use_autograd=true:  Use .detach().item() to minimize graph pollution
//
// IMPORTANT: .item() ALWAYS breaks the graph because it extracts a scalar.
// The key difference is:
//   - NoGradGuard: Disables gradient computation entirely in scope
//   - detach(): Creates a view that doesn't track gradients
//
// For true end-to-end AD, use tensor_* comparison functions instead.
// ============================================================================

/**
 * @brief Extract scalar from tensor with automatic gradient guard
 *
 * This utility ensures that scalar extraction from tensors never breaks
 * the autograd graph. Use this instead of raw .item<T>() calls.
 *
 * BEHAVIOR:
 *   - use_autograd=false: NoGradGuard + .item() (production mode)
 *   - use_autograd=true:  .detach().item() (minimize graph pollution)
 *
 * @tparam T Scalar type (float, bool, int, etc.)
 * @param v Tensor to extract scalar from
 * @return T Extracted scalar value
 *
 * @example
 * float norm = guarded_item<float>(residual.norm());
 * bool converged = guarded_item<bool>(error < tol);
 */
template<typename T>
inline T guarded_item(const torch::Tensor& v) {
    // FIX 2025-12-27: Add .to(kCPU) before .item<T>() to avoid GPU->CPU sync overhead
    // This ensures tensor is on CPU before calling .item(), preventing blocking GPU sync
    if (g_sdirk3_config.use_autograd) {
        // Autograd mode: Use detach() to minimize graph pollution
        // The main computation path stays intact; only this extraction detaches
        return v.detach().to(torch::kCPU).item<T>();
    } else {
        // FD mode: Use NoGradGuard for cleaner scope management
        torch::NoGradGuard guard;
        return v.to(torch::kCPU).item<T>();
    }
}

/**
 * @brief Skip scalar extraction in autograd mode, return default value
 *
 * Use this for diagnostic/logging .item() calls that can be skipped entirely
 * in autograd mode to preserve the graph completely.
 *
 * @tparam T Scalar type
 * @param v Tensor to extract from
 * @param default_val Value to return in autograd mode
 * @return T Extracted value or default
 */
template<typename T>
inline T guarded_item_or_default(const torch::Tensor& v, T default_val) {
    if (g_sdirk3_config.use_autograd) {
        // Skip extraction entirely in autograd mode
        return default_val;
    } else {
        // FIX 2025-12-27: Add .to(kCPU) before .item<T>() to avoid GPU->CPU sync overhead
        torch::NoGradGuard guard;
        return v.to(torch::kCPU).item<T>();
    }
}

// ============================================================================
// TENSOR-BASED COMPARISON UTILITIES (Graph-Preserving)
// ============================================================================
// These functions perform comparisons without breaking the graph.
// Use these for convergence checks in autograd mode.
// ============================================================================

/**
 * @brief Check if tensor value is less than threshold (graph-preserving)
 *
 * Returns a boolean tensor that can be used in control flow.
 * In autograd mode, caller should use .all().item<bool>() ONLY at top-level
 * control flow (loop termination), not within the computation.
 *
 * @param v Value tensor
 * @param threshold Threshold value
 * @return torch::Tensor Boolean tensor (true if v < threshold)
 */
inline torch::Tensor tensor_less_than(const torch::Tensor& v, float threshold) {
    return v < threshold;
}

/**
 * @brief Check if tensor value is greater than threshold (graph-preserving)
 */
inline torch::Tensor tensor_greater_than(const torch::Tensor& v, float threshold) {
    return v > threshold;
}

/**
 * @brief Check if any element is NaN (graph-preserving check)
 */
inline torch::Tensor tensor_has_nan(const torch::Tensor& v) {
    return torch::isnan(v).any();
}

/**
 * @brief Check if any element is Inf (graph-preserving check)
 */
inline torch::Tensor tensor_has_inf(const torch::Tensor& v) {
    return torch::isinf(v).any();
}

/**
 * @brief Safe convergence check for autograd mode
 *
 * In FD mode: Uses .item() for immediate boolean result
 * In autograd mode: Uses detached tensor to minimize graph breaks
 *
 * @param residual_norm Current residual norm tensor
 * @param tolerance Convergence tolerance
 * @return bool True if converged
 */
inline bool check_convergence(const torch::Tensor& residual_norm, float tolerance) {
    auto converged_tensor = residual_norm < tolerance;
    // FIX 2025-12-27: Add .to(kCPU) before .item<bool>() to avoid GPU sync
    if (g_sdirk3_config.use_autograd) {
        // Autograd mode: Detach for control flow, preserve main graph
        return converged_tensor.detach().all().to(torch::kCPU).item<bool>();
    } else {
        torch::NoGradGuard guard;
        return converged_tensor.all().to(torch::kCPU).item<bool>();
    }
}

/**
 * @brief Safe NaN/Inf check for control flow
 *
 * @param v Tensor to check
 * @return bool True if NaN or Inf detected
 */
inline bool check_nan_or_inf(const torch::Tensor& v) {
    auto has_bad = torch::isnan(v).any() | torch::isinf(v).any();
    // FIX 2025-12-27: Add .to(kCPU) before .item<bool>() to avoid GPU sync
    if (g_sdirk3_config.use_autograd) {
        return has_bad.detach().to(torch::kCPU).item<bool>();
    } else {
        torch::NoGradGuard guard;
        return has_bad.to(torch::kCPU).item<bool>();
    }
}

// ============================================================================
// AUTOGRAD MODE CONFIGURATION HELPERS
// ============================================================================

/**
 * @brief Check if autograd mode is active
 */
inline bool is_autograd_mode() {
    return g_sdirk3_config.use_autograd;
}

/**
 * @brief Get maximum iterations for autograd mode
 *
 * In autograd mode, we may want to run fixed iterations instead of
 * convergence-based termination to avoid graph breaks from .item() calls.
 *
 * @param default_max Default maximum iterations
 * @param autograd_fixed Fixed iterations for autograd mode (0 = use default)
 * @return int Maximum iterations to use
 */
inline int get_max_iterations(int default_max, int autograd_fixed = 0) {
    if (g_sdirk3_config.use_autograd && autograd_fixed > 0) {
        return autograd_fixed;
    }
    return default_max;
}

// ============================================================================
// TENSOR CREATION UTILITIES
// ============================================================================

/**
 * @brief Create TensorOptions for from_blob with CPU device
 *
 * FIX 2025-12-30 Batch27 Issue 6: Shared helper for from_blob CPU device safety
 *
 * CRITICAL: torch::from_blob() with host pointers MUST specify .device(torch::kCPU).
 * Without explicit device, the tensor may inherit an incorrect default device
 * or have undefined behavior on GPU builds.
 *
 * Use this helper instead of manually creating TensorOptions for from_blob calls.
 *
 * CORRECT USAGE:
 *   auto t = torch::from_blob(ptr, {n}, make_cpu_from_blob_opts());
 *   auto t = torch::from_blob(ptr, {n,m}, make_cpu_from_blob_opts(torch::kFloat64));
 *
 * INCORRECT (will compile but may cause runtime issues):
 *   auto t = torch::from_blob(ptr, {n}, torch::TensorOptions());  // BAD: no device
 *   auto t = torch::from_blob(ptr, {n}, torch::kFloat32);         // BAD: no device
 *   auto t = torch::from_blob(ptr, {n});                          // BAD: no options
 *
 * CI CHECK: Use `cmake --build . --target lint_from_blob` to verify compliance.
 *
 * FIX Round151 AUDIT STATUS:
 * Files with from_blob calls audited for .device(torch::kCPU) compliance:
 *   - wrf_sdirk3_tile_unified_impl.cpp: Uses make_cpu_from_blob_opts() ✓
 *   - wrf_sdirk3_zerocopy.cpp: Uses make_cpu_from_blob_opts() ✓
 *   - jvp_bridge.cpp: Uses explicit .device(torch::kCPU) ✓
 *   - device_manager.cpp: NEEDS REVIEW (from_blob without explicit device)
 *   - wrf_sdirk3_unified_solver.cpp: NEEDS REVIEW (from_blob with torch::kFloat32 only)
 *   - wrf_sdirk3_base_state_zerocopy.cpp: NEEDS REVIEW
 *   - wrf_sdirk3_acoustic_gravity_coupling.cpp: Has LINT_EXCEPTION markers ✓
 *
 * @param dtype Scalar type for the tensor (default: kFloat32)
 * @return TensorOptions with CPU device and specified dtype
 */
inline torch::TensorOptions make_cpu_from_blob_opts(torch::ScalarType dtype = torch::kFloat32) {
    return torch::TensorOptions().dtype(dtype).device(torch::kCPU);
}

// ============================================================================
// FORWARD-MODE AD SAFE OPERATIONS
// ============================================================================
// PyTorch 2.8.0-2.10.0 forward-mode AD has a bug with norm() without keepdim:
//   - norm() returns scalar [] but tangent is computed as [1,1,1] shape
//   - This causes "forward gradient size mismatch" error
//
// WORKAROUND: Use norm(2, all_dims, keepdim=true).squeeze()
//   - keepdim=true ensures primal and tangent have same shape
//   - squeeze() removes the extra dimensions for scalar result
// ============================================================================

/**
 * @brief Forward-AD-safe norm computation
 *
 * Works around PyTorch forward-mode AD bug with norm() without keepdim.
 * Uses norm(p, dims, keepdim=true).squeeze() pattern.
 *
 * @param t Tensor to compute norm of
 * @param p Norm type (default 2 for L2 norm)
 * @return torch::Tensor Scalar tensor containing the norm
 *
 * @example
 * // Instead of: auto n = v.norm();        // FAILS in forward-mode AD
 * auto n = fwd_ad_safe_norm(v);            // WORKS in forward-mode AD
 */
inline torch::Tensor fwd_ad_safe_norm(const torch::Tensor& t, float p = 2.0f) {
    // Build dimension list for all dimensions
    std::vector<int64_t> dims;
    for (int64_t i = 0; i < t.dim(); ++i) {
        dims.push_back(i);
    }

    // Use keepdim=true to maintain shape compatibility with tangent
    // Then squeeze to get scalar-like result
    if (dims.empty()) {
        // Already a scalar, just return abs for norm
        return t.abs();
    }

    return t.norm(p, dims, /*keepdim=*/true).squeeze();
}

/**
 * @brief Forward-AD-safe squared norm (avoids sqrt for efficiency)
 *
 * @param t Tensor to compute squared norm of
 * @return torch::Tensor Scalar tensor containing ||t||^2
 */
inline torch::Tensor fwd_ad_safe_norm_squared(const torch::Tensor& t) {
    // Build dimension list for all dimensions
    std::vector<int64_t> dims;
    for (int64_t i = 0; i < t.dim(); ++i) {
        dims.push_back(i);
    }

    if (dims.empty()) {
        return t * t;
    }

    // ||t||^2 = sum(t^2)
    return (t * t).sum(dims, /*keepdim=*/true).squeeze();
}

/**
 * @brief Conditional norm: uses fwd_ad_safe_norm in autograd mode
 *
 * This automatically selects the right norm function based on config.
 * - autograd=true: Uses forward-AD-safe norm (slower but works)
 * - autograd=false: Uses standard norm (faster)
 *
 * @param t Tensor to compute norm of
 * @return torch::Tensor Scalar tensor containing the norm
 */
inline torch::Tensor safe_tensor_norm(const torch::Tensor& t) {
    if (g_sdirk3_config.use_autograd) {
        return fwd_ad_safe_norm(t);
    } else {
        return t.norm();
    }
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AUTOGRAD_UTILS_H
