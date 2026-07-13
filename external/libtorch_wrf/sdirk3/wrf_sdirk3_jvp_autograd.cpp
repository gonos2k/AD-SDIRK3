#include <atomic>
#include "wrf_sdirk3_jvp_autograd.h"
#include "wrf_sdirk3_config.h"
#include <torch/torch.h>
#include <torch/csrc/autograd/anomaly_mode.h>  // OPT 2025-01-25: For AnomalyMode
#include <iostream>
#include <chrono>
#include <optional>  // OPT 2025-01-25: For std::optional anomaly guard

namespace wrf {
namespace sdirk3 {

// Global flag to control JVP method for performance
// Full-repo review P1-3: these were unsynchronized process-globals — a data
// race once multiple tiles/OpenMP threads call the helpers. Atomics (relaxed
// semantics are fine: a method toggle and a diagnostics counter).
static std::atomic<bool> g_use_finite_diff_jvp{true};  // Default to fast finite differences
static std::atomic<int> g_jvp_call_count{0};

// Enable/disable finite difference JVP
// NOTE (Codex round-2): this toggle no longer affects compute_vjp_autograd —
// the FD dispatch that made a "VJP" return J*v was removed. Kept because
// tests/test_gradient_flow.cpp exercises the setter; production JVP method
// selection lives in the fwad router, not here.
void set_use_finite_diff_jvp(bool use_fd) {
    g_use_finite_diff_jvp = use_fd;
    // FIX Round160: Gate log with debug_level >= 1
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cout << "[JVP] Switched to " << (use_fd ? "finite differences" : "autograd") << std::endl;
    }
}

// Get current JVP method setting
bool get_use_finite_diff_jvp() {
    return g_use_finite_diff_jvp;
}

// Fast finite difference JVP (no autograd overhead)
// Uses central difference for O(ε²) accuracy. See compute_jvp_forward_diff for
// epsilon scaling guidelines. For central diff, optimal eps ≈ cbrt(eps_machine):
//   FP32: eps ≈ 5e-3 (default 1e-5 is conservative)
//   FP64: eps ≈ 6e-6
torch::Tensor compute_jvp_finite_diff(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    float epsilon,
    int halo_width) {

    // Central difference: J*v ≈ (F(u + ε*v) - F(u - ε*v)) / (2ε)
    // O(ε²) truncation error (vs O(ε) for forward difference)
    // Doubles RHS evaluations but significantly improves accuracy
    // NOTE 2025-01-25: Use NoGradGuard instead of InferenceMode because F might
    // internally use autograd (e.g., for sub-computations). InferenceMode would
    // completely block autograd and cause runtime errors in such cases.
    torch::NoGradGuard no_grad;

    // CRITICAL FIX: Zero perturbation vector in halo regions
    // This prevents artificial gradients at halo boundaries
    torch::Tensor v_safe = v.clone();
    if (halo_width > 0 && v_safe.dim() >= 3) {
        // Assuming tensor layout: (j, k, i) for 3D fields
        // or could be flattened - need to handle both cases
        auto sizes = v_safe.sizes();
        if (sizes.size() == 3) {
            // 3D field: (j, k, i)
            int nj = sizes[0];
            [[maybe_unused]] int nk = sizes[1];
            int ni = sizes[2];

            // Zero halo regions in j dimension
            if (nj > 2 * halo_width) {
                v_safe.slice(0, 0, halo_width).zero_();
                v_safe.slice(0, nj - halo_width, nj).zero_();
            }

            // Zero halo regions in i dimension
            if (ni > 2 * halo_width) {
                v_safe.slice(2, 0, halo_width).zero_();
                v_safe.slice(2, ni - halo_width, ni).zero_();
            }
        }
        // For flattened vectors, the caller should handle halo masking
    }

    // Central difference stencil
    auto u_plus = u + epsilon * v_safe;   // Forward perturbation
    auto u_minus = u - epsilon * v_safe;  // Backward perturbation
    auto F_plus = F(u_plus);
    auto F_minus = F(u_minus);

    return (F_plus - F_minus) / (2.0f * epsilon);
}

// JVP computation using PyTorch's autograd engine
// Computes J(u)^T * v (reverse-mode VJP) — ALWAYS. Codex round-2 on the
// rename: the old body first dispatched on g_use_finite_diff_jvp (default
// true) to compute_jvp_finite_diff, i.e. the "VJP" function returned the
// FORWARD product J*v by default and J^T v only with the toggle off — one
// name, two different mathematical objects. The FD dispatch is removed: a
// caller who wants a JVP uses compute_jvp_finite_diff/compute_jvp_forward_diff (or
// the production fwad router); this function is now coherently reverse-mode.
torch::Tensor compute_vjp_autograd(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    int halo_width) {  // Default argument only in header
    
    // Track performance
    // FIX Round160: Gate periodic log with debug_level >= 2
    if (++g_jvp_call_count % 100 == 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cout << "[VJP] Call count: " << g_jvp_call_count << " (reverse-mode)" << std::endl;
    }

    // OPT 2025-01-25: Enable anomaly detection for debugging in-place op issues
    // DetectAnomalyGuard catches: in-place ops breaking gradients, NaN/Inf in backward, double backward errors
    // WARNING: Significant performance overhead - only enable for debugging
    std::optional<torch::autograd::DetectAnomalyGuard> anomaly_guard;
    if (g_sdirk3_config.detect_autograd_anomaly) {
        anomaly_guard.emplace();
    }

    // Original autograd implementation (accurate but slow)
    // FIX 2025-01-25: Use detach().clone() to ensure u_var is a fresh leaf tensor
    // - detach(): Prevents upstream graph from being pulled into JVP computation
    // - clone(): Avoids in-place modification of original tensor
    // - requires_grad_(true): Makes u_var a leaf that tracks gradients
    // For higher-order derivatives (4DVAR), use ad_strict_mode to preserve upstream graph.
    torch::Tensor u_var = g_sdirk3_config.ad_strict_mode
        ? u.clone().requires_grad_(true)           // Preserves upstream graph for higher-order AD
        : u.detach().clone().requires_grad_(true); // Isolates JVP (default for Newton-Krylov)
    torch::Tensor v_normalized = v;
    
    // Create a wrapper that properly handles gradients
    [[maybe_unused]] auto F_wrapper = [&F](const torch::Tensor& x) -> torch::Tensor {
        return F(x);
    };
    
    // PyTorch C++ API doesn't have torch::jvp, use reverse-mode autodiff
    torch::Tensor F_u = F(u_var);
    
    // Compute JVP using reverse-mode autodiff
    // NOTE 2025-01-25: allow_unused=true policy
    // If F(u) doesn't depend on some input elements (e.g., boundary values excluded
    // from physics), autograd returns undefined. We convert undefined → zeros.
    // This is the correct mathematical behavior: ∂F/∂u_i = 0 for unused inputs.
    auto grad_result = torch::autograd::grad(
        {F_u},           // outputs
        {u_var},         // inputs
        {v},             // grad_outputs => this computes J^T v (VJP), NOT Jv
        /*retain_graph=*/false,  // Don't retain graph to prevent memory leak
        /*create_graph=*/false,  // Not needed for first-order Newton-Krylov
        /*allow_unused=*/true    // FIX 2025-01-25: Changed to true for consistency
    );

    if (!grad_result.empty() && grad_result[0].defined()) {
        return grad_result[0];
    } else {
        // Undefined result means F doesn't depend on input → gradient is zero
        // This is mathematically correct, not an error
        // FIX 2025-01-25: Use requires_grad(false) to prevent graph pollution
        return torch::zeros(v.sizes(), v.options().requires_grad(false));
    }
}

// Alternative implementation using dual numbers (finite difference) approach
//
// FD Epsilon Auto-Scaling Guidelines (2025-01-25):
// ================================================
// Optimal epsilon balances truncation error (decreases with smaller eps) vs
// cancellation error (increases with smaller eps due to floating-point limits).
//
// Recommended formula: eps = sqrt(eps_machine) * max(1, ||u||)
//   - FP32: eps_machine = 1.19e-7, so sqrt ≈ 3.5e-4, use ~1e-4
//   - FP64: eps_machine = 2.22e-16, so sqrt ≈ 1.5e-8, use ~1e-7
//
// For WRF-scale problems (||u|| ~ 1e3 to 1e6):
//   eps = 1e-4 * max(1, ||u|| / 1e5)  // Scale with problem magnitude
//
// Central difference (not implemented here) has O(eps^2) truncation error vs
// forward difference O(eps), so optimal eps is different:
//   Central: eps = cbrt(eps_machine) ≈ 5e-3 for FP32, 6e-6 for FP64
//
// See tests/test_jvp_fixes_validation.cpp for empirical convergence tests.
//
torch::Tensor compute_jvp_forward_diff(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    float epsilon,
    int halo_width) {  // Default argument only in header

    // Dual number approach: F(u + ε*v) ≈ F(u) + ε*J(u)*v
    // Extract J(u)*v = (F(u + ε*v) - F(u)) / ε
    // NOTE 2025-01-25: Use NoGradGuard instead of InferenceMode because F might
    // internally use autograd (e.g., for sub-computations). InferenceMode would
    // completely block autograd and cause runtime errors in such cases.
    // NoGradGuard only disables gradient tracking for new tensors but allows
    // explicit autograd calls if F needs them.
    torch::NoGradGuard no_grad;

    // CRITICAL FIX: Zero perturbation vector in halo regions
    torch::Tensor v_safe = v.clone();
    if (halo_width > 0 && v_safe.dim() >= 3) {
        auto sizes = v_safe.sizes();
        if (sizes.size() == 3) {
            int nj = sizes[0];
            [[maybe_unused]] int nk = sizes[1];
            int ni = sizes[2];

            // Zero halo regions
            if (nj > 2 * halo_width) {
                v_safe.slice(0, 0, halo_width).zero_();
                v_safe.slice(0, nj - halo_width, nj).zero_();
            }
            if (ni > 2 * halo_width) {
                v_safe.slice(2, 0, halo_width).zero_();
                v_safe.slice(2, ni - halo_width, ni).zero_();
            }
        }
    }
    
    torch::Tensor u_pert = u + epsilon * v_safe;  // Use v_safe with zeroed halos
    torch::Tensor F_u = F(u);
    torch::Tensor F_u_pert = F(u_pert);
    
    return (F_u_pert - F_u) / epsilon;
}
// LEGACY VJP-MISLABELED APIs REMOVED (full-repo review follow-up):
// compute_jvp_batch and JVPContext::compute_jvp both used
// torch::autograd::grad(..., grad_outputs=v) — i.e. they returned J^T v
// (VJPs) while their names and docs claimed J*v. Neither had any
// production caller (the Newton/GMRES matvec uses the forward-mode fwad
// router). Deleted rather than renamed: an unused, misnamed batched VJP
// and a graph-reuse context whose cached-graph reuse assumptions were
// documented but never exercised are exactly the kind of dormant API the
// review flagged for future silent misuse.


} // namespace sdirk3
} // namespace wrf