#ifndef WRF_SDIRK3_JVP_OPERATORS_H
#define WRF_SDIRK3_JVP_OPERATORS_H

#include <torch/torch.h>
#include <functional>
#include "wrf_sdirk3_jvp_convergence.h"
#include "wrf_sdirk3_config.h"  // FIX 2025-12-28: For ad_strict_mode

namespace wrf {
namespace sdirk3 {

/**
 * JVP Operators for WRF SDIRK3
 * Implements finite difference and AD-based Jacobian-vector products
 */

struct JVPOperator {
    JVPMode mode;
    bool freeze_limiters;
    torch::Tensor scale_vector;
    JVPConvergenceParams convergence_params;
    
    // Cache for finite difference
    mutable torch::Tensor cached_residual;
    mutable bool has_cached_residual = false;
    
    JVPOperator(JVPMode mode_, int nx, int ny, int nz) 
        : mode(mode_), freeze_limiters(true) {
        
        // Initialize scale vector for WRF coupled variables
        scale_vector = get_wrf_scale_vector(nx, ny, nz);
        
        // Set convergence parameters based on mode
        convergence_params.set_jvp_mode(mode);
    }
    
    /**
     * Compute JVP: J*v where J = I - gamma*dt*dR/dU
     */
    torch::Tensor compute_jvp(
        const torch::Tensor& U,
        const torch::Tensor& v,
        float dt,
        float gamma,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func) const {
        
        torch::Tensor jvp_result;
        
        switch(mode) {
            case JVPMode::FD_CENTRAL:
                jvp_result = compute_fd_central(U, v, residual_func);
                break;
            case JVPMode::FD_FORWARD:
                jvp_result = compute_fd_forward(U, v, residual_func);
                break;
            case JVPMode::AD_JVP:
                jvp_result = compute_ad_jvp(U, v, residual_func);
                break;
        }
        
        // Return J*v = v - gamma*dt*dR/dU*v
        return v - gamma * dt * jvp_result;
    }
    
    /**
     * Central difference JVP: O(eps^(2/3)) accuracy
     */
    torch::Tensor compute_fd_central(
        const torch::Tensor& U,
        const torch::Tensor& v,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func) const {
        
        // Compute optimal epsilon
        float eps = compute_fd_epsilon(U, v, scale_vector, JVPMode::FD_CENTRAL);
        
        // Central difference: (R(U + eps*v) - R(U - eps*v)) / (2*eps)
        torch::Tensor U_plus = U + eps * v;
        torch::Tensor U_minus = U - eps * v;
        
        // Evaluate with frozen limiters/switches if requested
        torch::Tensor R_plus = residual_func(U_plus, freeze_limiters);
        torch::Tensor R_minus = residual_func(U_minus, freeze_limiters);
        
        return (R_plus - R_minus) / (2.0f * eps);
    }
    
    /**
     * Forward difference JVP: O(sqrt(eps)) accuracy
     */
    torch::Tensor compute_fd_forward(
        const torch::Tensor& U,
        const torch::Tensor& v,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func) const {
        
        // Compute optimal epsilon
        float eps = compute_fd_epsilon(U, v, scale_vector, JVPMode::FD_FORWARD);
        
        // Forward difference: (R(U + eps*v) - R(U)) / eps
        torch::Tensor U_pert = U + eps * v;
        
        // Use cached residual if available (for GMRES iterations)
        if (!has_cached_residual) {
            cached_residual = residual_func(U, freeze_limiters);
            has_cached_residual = true;
        }
        
        torch::Tensor R_pert = residual_func(U_pert, freeze_limiters);
        
        return (R_pert - cached_residual) / eps;
    }
    
    /**
     * Automatic differentiation JVP: Machine precision accuracy
     *
     * FIX 2025-12-27: Use detach().requires_grad_(true) to avoid modifying caller's tensor.
     * Previously U.requires_grad_(true) would set requires_grad on the original tensor,
     * causing unintended graph construction in subsequent operations.
     */
    torch::Tensor compute_ad_jvp(
        const torch::Tensor& U,
        const torch::Tensor& v,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func) const {

        // FIX 2025-12-28: Use detach() by default, clone() in AD strict mode
        // detach() shares storage - faster but in-place ops could corrupt original
        auto U_ad = g_sdirk3_config.ad_strict_mode
            ? U.clone().requires_grad_(true)
            : U.detach().requires_grad_(true);

        // Compute residual with AD
        auto R = residual_func(U_ad, false);  // Don't freeze for AD

        // Compute JVP using autograd
        auto outputs = torch::autograd::grad(
            {R},
            {U_ad},
            {v},
            /*retain_graph=*/true,
            /*create_graph=*/false,
            /*allow_unused=*/false
        );

        return outputs[0];
    }
    
    void clear_cache() {
        has_cached_residual = false;
        cached_residual = torch::Tensor();
    }
    
    float get_tau_jvp() const {
        return convergence_params.get_tau_jvp(mode);
    }
    
    float get_eta_min() const {
        return convergence_params.eta_min;
    }
};

/**
 * Frozen limiter wrapper for residual function
 * Captures limiter coefficients at reference state
 */
class FrozenLimiterWrapper {
    struct LimiterState {
        torch::Tensor flux_limiters;
        torch::Tensor moisture_clipping;
        torch::Tensor diffusion_coeffs;
        bool initialized = false;
    };
    
    mutable LimiterState frozen_state;
    
public:
    /**
     * Wrap residual function with optional limiter freezing
     */
    std::function<torch::Tensor(const torch::Tensor&, bool)> 
    wrap_residual_func(
        const std::function<torch::Tensor(const torch::Tensor&)>& original_func) {
        
        return [this, original_func](const torch::Tensor& U, bool freeze_limiters) -> torch::Tensor {
            if (!freeze_limiters) {
                // Normal evaluation
                return original_func(U);
            }
            
            // For frozen limiters, we need to modify the evaluation
            // This is a simplified placeholder - actual implementation would
            // need to interface with WRF physics modules
            
            // TODO: Implement actual limiter freezing logic
            // For now, just call original function
            return original_func(U);
        };
    }
    
    void reset() {
        frozen_state.initialized = false;
    }
};

/**
 * Hybrid JVP strategy selector
 * Automatically switches between FD and AD based on performance
 */
class HybridJVPStrategy {
    JVPModeSelector mode_selector;
    std::unique_ptr<JVPOperator> current_operator;
    
    int nx, ny, nz;
    float prev_residual_norm = -1.0f;
    int line_search_failures = 0;
    
public:
    HybridJVPStrategy(int nx_, int ny_, int nz_) 
        : nx(nx_), ny(ny_), nz(nz_) {
        
        // Start with forward difference for initial iterations
        current_operator = std::make_unique<JVPOperator>(
            JVPMode::FD_FORWARD, nx, ny, nz);
    }
    
    torch::Tensor compute_jvp(
        const torch::Tensor& U,
        const torch::Tensor& v,
        float dt,
        float gamma,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func,
        float current_residual_norm,
        bool line_search_failed) {
        
        // Update mode based on convergence behavior
        if (prev_residual_norm > 0) {
            float ratio = current_residual_norm / prev_residual_norm;
            JVPMode new_mode = mode_selector.update_mode(ratio, line_search_failed);
            
            // Switch operator if mode changed
            if (new_mode != current_operator->mode) {
                current_operator = std::make_unique<JVPOperator>(new_mode, nx, ny, nz);
            }
        }
        
        prev_residual_norm = current_residual_norm;
        
        // Compute JVP with current mode
        return current_operator->compute_jvp(U, v, dt, gamma, residual_func);
    }
    
    void reset_for_new_stage() {
        mode_selector.reset();
        prev_residual_norm = -1.0f;
        line_search_failures = 0;
        
        // Reset to forward difference for new stage
        current_operator = std::make_unique<JVPOperator>(
            JVPMode::FD_FORWARD, nx, ny, nz);
    }
    
    JVPMode get_current_mode() const {
        return current_operator->mode;
    }
    
    float get_tau_jvp() const {
        return current_operator->get_tau_jvp();
    }
    
    float get_eta_min() const {
        return current_operator->get_eta_min();
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_JVP_OPERATORS_H