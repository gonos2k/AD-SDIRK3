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
static bool g_use_finite_diff_jvp = true;  // Default to fast finite differences
static int g_jvp_call_count = 0;

// Enable/disable finite difference JVP
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
// Uses central difference for O(ε²) accuracy. See compute_jvp_dual for
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
// Computes J(u)*v where J is the Jacobian of F at u
torch::Tensor compute_jvp_autograd(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    int halo_width) {  // Default argument only in header
    
    // Track performance
    // FIX Round160: Gate periodic log with debug_level >= 2
    if (++g_jvp_call_count % 100 == 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cout << "[JVP] Call count: " << g_jvp_call_count
                  << ", method: " << (g_use_finite_diff_jvp ? "FD" : "AD") << std::endl;
    }
    
    // Use finite differences for speed if enabled
    if (g_use_finite_diff_jvp) {
        return compute_jvp_finite_diff(F, u, v, 1e-5f, halo_width);
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
        {v},             // grad_outputs (the vector v in Jv)
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
torch::Tensor compute_jvp_dual(
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

// Batched JVP computation for multiple vectors
torch::Tensor compute_jvp_batch(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& V) {  // V has shape [batch, ...]
    
    int batch_size = V.size(0);
    std::vector<torch::Tensor> jvps;
    jvps.reserve(batch_size);

    // Enable gradient computation
    // FIX 2025-01-25: Use detach().clone() by default to ensure fresh leaf tensor
    torch::Tensor u_var = g_sdirk3_config.ad_strict_mode
        ? u.clone().requires_grad_(true)
        : u.detach().clone().requires_grad_(true);
    torch::Tensor F_u = F(u_var);
    
    // Compute JVP for each vector in the batch
    for (int i = 0; i < batch_size; ++i) {
        auto v_i = V[i];
        // Only retain graph for all but the last iteration
        bool retain = (i < batch_size - 1);
        auto jvp_i = torch::autograd::grad(
            {F_u},
            {u_var},
            {v_i},
            /*retain_graph=*/retain,  // Only retain for intermediate iterations
            /*create_graph=*/false     // Not needed for first-order Newton-Krylov
        )[0];
        jvps.push_back(jvp_i);
    }
    
    return torch::stack(jvps);
}


// ============================================================================
// JVPContext Implementation - Graph Reuse Optimization
// ============================================================================

bool JVPContext::prepare(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    int halo_width) {

    // Release any existing graph
    release();

    halo_width_ = halo_width;

    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        // Create input tensor with gradient tracking
        // FIX 2025-01-25: Use detach().clone() by default for fresh leaf tensor
        u_var_ = g_sdirk3_config.ad_strict_mode
            ? u.clone().requires_grad_(true)
            : u.detach().clone().requires_grad_(true);

        // Evaluate F(u) - this builds the computation graph
        // This is the expensive part (~15 seconds for 1.08M variables)
        F_u_ = F(u_var_);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        // OPT Pass33+: Diagnostic sampling for prepare() verbose output
        // Reduces D2H overhead from .item<float>() calls
        const int current_debug_level = g_sdirk3_config.debug_level;
        bool do_diag_sample = false;
        if (current_debug_level >= 1) {
            const uint64_t diag_counter = prepare_diag_call_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
            const int diag_period = g_sdirk3_config.debug_sample_period;
            do_diag_sample = (diag_period == 0) || ((diag_counter % diag_period) == 0) || (diag_counter == 1);

            // OPT Pass34: Gate timing output + use \n (fires per Newton iteration)
            std::cerr << "[JVPContext] Graph prepared in " << duration << " ms\n";

            // OPT Pass33+: Gate .item<float>() diagnostic with sampling
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>()
            // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
            if (do_diag_sample) {
                torch::NoGradGuard no_grad;
                std::cerr << "[JVPContext] F_u shape: " << F_u_.sizes() << ", norm: " << F_u_.norm().to(torch::kCPU).item<float>() << std::endl;
            }
        }

        is_prepared_ = true;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[JVPContext] ERROR: Failed to prepare graph: " << e.what() << std::endl;
        release();
        return false;
    }
}

torch::Tensor JVPContext::compute_jvp(const torch::Tensor& v, bool retain_graph_for_next) {
    if (!is_prepared_) {
        throw std::runtime_error("[JVPContext] Graph not prepared. Call prepare() first.");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Apply halo masking to direction vector
    torch::Tensor v_safe = v;
    if (halo_width_ > 0 && v.dim() >= 3) {
        v_safe = v.clone();
        auto sizes = v_safe.sizes();
        if (sizes.size() == 3) {
            int nj = sizes[0];
            int ni = sizes[2];

            if (nj > 2 * halo_width_) {
                v_safe.slice(0, 0, halo_width_).zero_();
                v_safe.slice(0, nj - halo_width_, nj).zero_();
            }
            if (ni > 2 * halo_width_) {
                v_safe.slice(2, 0, halo_width_).zero_();
                v_safe.slice(2, ni - halo_width_, ni).zero_();
            }
        }
    }

    // Compute JVP using existing graph
    // This is much faster than rebuilding the graph
    // NOTE 2025-01-25: allow_unused=true - see note in compute_jvp_autograd()
    auto grad_result = torch::autograd::grad(
        {F_u_},          // outputs (already computed)
        {u_var_},        // inputs
        {v_safe},        // grad_outputs (direction vector)
        /*retain_graph=*/retain_graph_for_next,  // Keep graph for next JVP
        /*create_graph=*/false,                  // Not needed for Newton-Krylov
        /*allow_unused=*/true                    // FIX 2025-01-25: Changed for consistency
    );

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    if (g_sdirk3_config.debug_level >= 2) {
        std::cerr << "[JVPContext] JVP computed in " << duration << " ms (retain="
                  << retain_graph_for_next << ")" << std::endl;
    }

    if (!grad_result.empty() && grad_result[0].defined()) {
        return grad_result[0];
    } else {
        // Undefined → zeros (see note in compute_jvp_autograd)
        // FIX 2025-01-25: Use requires_grad(false) to prevent graph pollution
        return torch::zeros(v.sizes(), v.options().requires_grad(false));
    }
}

void JVPContext::release() {
    if (is_prepared_) {
        // Clear tensors to release graph memory
        u_var_ = torch::Tensor();
        F_u_ = torch::Tensor();
        is_prepared_ = false;

        if (g_sdirk3_config.debug_level >= 2) {
            std::cerr << "[JVPContext] Graph released" << std::endl;
        }
    }
}

} // namespace sdirk3
} // namespace wrf