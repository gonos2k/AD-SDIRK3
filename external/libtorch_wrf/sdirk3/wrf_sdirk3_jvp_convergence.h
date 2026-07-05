#ifndef WRF_SDIRK3_JVP_CONVERGENCE_H
#define WRF_SDIRK3_JVP_CONVERGENCE_H

#include <torch/torch.h>
#include <cmath>
#include <algorithm>

namespace wrf {
namespace sdirk3 {

// FIX 2025-12-27: Helpers for safe scalar extraction (handles GPU sync + AD)
// FIX Round184: Dtype-aware versions that preserve FP64 precision when needed
//
// FIX Round185: DOWNCAST WARNING
// ==============================
// The _auto() functions return double to preserve FP64 precision, but callers
// may still store results to float variables, causing silent precision loss:
//
//   float val = safe_scalar_auto(tensor);  // WARNING: silent downcast if FP64!
//   double val = safe_scalar_auto(tensor); // OK: preserves precision
//
// For FP64 runs, either:
// 1. Store results in double variables (recommended)
// 2. Use safe_scalar_f64() / safe_norm_f64() explicitly
// 3. Enable -Wconversion warnings to catch implicit downcasts
//
namespace {

// Single-precision version (default for FP32 tensors)
inline float safe_scalar_f32(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<float>();
}

// Double-precision version for FP64 tensors
inline double safe_scalar_f64(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<double>();
}

// Dtype-aware dispatcher - returns double to preserve precision for both
// WARNING: Storing result to float causes silent downcast - see note above
inline double safe_scalar_auto(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    auto cpu_t = t.to(torch::kCPU);
    if (cpu_t.scalar_type() == torch::kFloat64) {
        return cpu_t.item<double>();
    }
    return static_cast<double>(cpu_t.item<float>());
}

// Original float version for backward compatibility (most WRF code is FP32)
inline float safe_scalar(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<float>();
}

// Single-precision norm
inline float safe_norm_f32(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().to(torch::kCPU).item<float>();
}

// Double-precision norm for FP64 tensors
inline double safe_norm_f64(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().to(torch::kCPU).item<double>();
}

// Dtype-aware norm dispatcher - returns double to preserve precision for both
// WARNING: Storing result to float causes silent downcast - see note above
inline double safe_norm_auto(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    auto norm_t = t.norm().to(torch::kCPU);
    if (t.scalar_type() == torch::kFloat64) {
        return norm_t.item<double>();
    }
    return static_cast<double>(norm_t.item<float>());
}

// Original float version for backward compatibility
inline float safe_norm(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().to(torch::kCPU).item<float>();
}

} // anonymous namespace

/**
 * JVP Convergence Criteria for WRF SDIRK3
 *
 * Implements precision-aware convergence criteria for:
 * - Finite Difference JVP (forward/central)
 * - Automatic Differentiation JVP
 *
 * Based on flux-form WRF equations with coupled variables
 *
 * NOTE: Convergence checks require scalar values (GPU sync unavoidable)
 *       but NoGradGuard is used to prevent AD graph construction.
 */

enum class JVPMode {
    AD_JVP,        // Automatic differentiation (machine precision)
    FD_FORWARD,    // Forward difference O(sqrt(eps_mach))
    FD_CENTRAL     // Central difference O(eps_mach^(2/3))
};

struct JVPConvergenceParams {
    // Machine epsilon for float32
    static constexpr float eps_mach_f32 = 1.192093e-7f;
    
    // JVP error estimates (tau_JVP)
    static constexpr float tau_ad = eps_mach_f32;                    // ~1e-7
    static constexpr float tau_fd_forward = 5.0f * std::sqrt(eps_mach_f32);  // ~1.7e-3
    static constexpr float tau_fd_central = 5.0f * std::cbrt(eps_mach_f32 * eps_mach_f32);  // ~2.5e-5
    
    // Eisenstat-Walker parameters
    float eta_max = 0.9f;      // Maximum forcing term
    float eta_min = 1e-4f;     // Minimum forcing term (will be adjusted based on JVP mode)
    float ew_alpha = 1.5f;     // Superlinear convergence exponent
    float ew_gamma = 0.9f;     // Safety factor for EW
    
    // Scaling factor for eta_min based on JVP accuracy
    float kappa = 10.0f;       // eta_min = kappa * tau_JVP
    
    // Nonlinear tolerance factors
    float rtol = 1e-3f;        // Relative tolerance
    float atol = 1e-6f;        // Absolute tolerance (will be adjusted)
    
    // Initialize based on JVP mode
    void set_jvp_mode(JVPMode mode) {
        switch(mode) {
            case JVPMode::AD_JVP:
                eta_min = kappa * tau_ad;              // ~1e-6 for float32
                atol = std::max(1e-8f, tau_ad);
                break;
            case JVPMode::FD_FORWARD:
                eta_min = kappa * tau_fd_forward;      // ~1e-2
                atol = std::max(1e-4f, tau_fd_forward);
                break;
            case JVPMode::FD_CENTRAL:
                eta_min = kappa * tau_fd_central;      // ~2.5e-4
                atol = std::max(1e-5f, tau_fd_central);
                break;
        }
    }
    
    // Get JVP error estimate
    float get_tau_jvp(JVPMode mode) const {
        switch(mode) {
            case JVPMode::AD_JVP: return tau_ad;
            case JVPMode::FD_FORWARD: return tau_fd_forward;
            case JVPMode::FD_CENTRAL: return tau_fd_central;
            default: return tau_fd_forward;
        }
    }
};

/**
 * Compute optimal finite difference epsilon
 */
inline float compute_fd_epsilon(
    const torch::Tensor& U,
    const torch::Tensor& v,
    const torch::Tensor& scale_vec,
    JVPMode mode) {

    // Normalize by scale vector
    auto U_tilde = U / scale_vec;
    auto v_tilde = v / scale_vec;

    // FIX 2025-12-27: Use safe_norm helper for GPU tensors
    float U_norm = safe_norm(U_tilde);
    float v_norm = safe_norm(v_tilde);

    // Prevent division by zero
    v_norm = std::max(v_norm, 1e-10f);
    
    float eps;
    if (mode == JVPMode::FD_CENTRAL) {
        // Central difference: eps = eps_mach^(1/3) * (1 + ||U||) / ||v||
        float eps_mach_cbrt = std::cbrt(JVPConvergenceParams::eps_mach_f32);
        eps = eps_mach_cbrt * (1.0f + U_norm) / v_norm;
    } else {
        // Forward difference: eps = sqrt(eps_mach) * (1 + ||U||) / ||v||
        float eps_mach_sqrt = std::sqrt(JVPConvergenceParams::eps_mach_f32);
        eps = eps_mach_sqrt * (1.0f + U_norm) / v_norm;
    }
    
    // Apply bounds for stability
    eps = std::max(eps, 1e-8f);   // Minimum to avoid underflow
    eps = std::min(eps, 1.0f);    // Maximum to maintain linearity
    
    return eps;
}

/**
 * Eisenstat-Walker forcing term computation
 */
inline float compute_ew_forcing(
    float residual_norm,
    float prev_residual_norm,
    const JVPConvergenceParams& params,
    int newton_iter) {
    
    float eta;
    
    if (newton_iter == 0 || prev_residual_norm <= 0) {
        // First iteration or invalid previous residual
        eta = params.eta_max;
    } else {
        // Eisenstat-Walker formula: eta = gamma * (||G_k|| / ||G_{k-1}||)^alpha
        float ratio = residual_norm / prev_residual_norm;
        eta = params.ew_gamma * std::pow(ratio, params.ew_alpha);
        
        // Apply bounds
        eta = std::max(params.eta_min, std::min(params.eta_max, eta));
    }
    
    return eta;
}

/**
 * WRF-specific scale vector for coupled variables
 * Based on reference state: rho_0, c_sound, theta_0
 */
inline torch::Tensor get_wrf_scale_vector(
    int nx, int ny, int nz,
    float rho_0 = 1.0f,        // Reference density kg/m³
    float c_sound = 340.0f,    // Sound speed m/s
    float theta_0 = 300.0f) {  // Reference potential temperature K
    
    // State vector: [U, V, W, Theta, Phi, mu_d]
    // where U = mu_d * u / m_y (coupled momentum)

    // FIX Round192: Use int64_t to prevent overflow on large grids (nx*ny*nz > 2^31)
    int64_t state_size = static_cast<int64_t>(nx+1)*ny*nz    // U on u-staggered grid
                       + static_cast<int64_t>(nx)*(ny+1)*nz   // V on v-staggered grid
                       + static_cast<int64_t>(nx)*ny*(nz+1)   // W on w-staggered grid
                       + static_cast<int64_t>(nx)*ny*nz       // Theta
                       + static_cast<int64_t>(nx)*ny*(nz+1)   // Phi on w-staggered grid
                       + static_cast<int64_t>(nx)*ny;         // mu_d (2D)

    auto scale = torch::ones({state_size}, torch::dtype(torch::kFloat32));

    int64_t idx = 0;

    // U scale: rho_0 * c_sound (momentum scale)
    int64_t u_size = static_cast<int64_t>(nx+1)*ny*nz;
    scale.slice(0, idx, idx + u_size).fill_(rho_0 * c_sound);
    idx += u_size;

    // V scale: rho_0 * c_sound
    int64_t v_size = static_cast<int64_t>(nx)*(ny+1)*nz;
    scale.slice(0, idx, idx + v_size).fill_(rho_0 * c_sound);
    idx += v_size;

    // W scale: rho_0 * c_sound (could be smaller for vertical)
    int64_t w_size = static_cast<int64_t>(nx)*ny*(nz+1);
    scale.slice(0, idx, idx + w_size).fill_(rho_0 * c_sound * 0.1f);  // Vertical typically smaller
    idx += w_size;

    // Theta scale: rho_0 * c_p * theta_0 (energy scale)
    int64_t theta_size = static_cast<int64_t>(nx)*ny*nz;
    float c_p = 1004.0f;  // Specific heat at constant pressure
    scale.slice(0, idx, idx + theta_size).fill_(rho_0 * c_p * theta_0);
    idx += theta_size;

    // Phi scale: c_sound^2 (geopotential scale)
    int64_t phi_size = static_cast<int64_t>(nx)*ny*(nz+1);
    scale.slice(0, idx, idx + phi_size).fill_(c_sound * c_sound);
    idx += phi_size;

    // mu_d scale: rho_0 * H (H ~ 10km scale height)
    int64_t mu_size = static_cast<int64_t>(nx)*ny;
    float H = 10000.0f;  // Scale height in meters
    scale.slice(0, idx, idx + mu_size).fill_(rho_0 * H);
    
    return scale;
}

/**
 * Check nonlinear convergence with JVP-aware tolerances
 */
inline bool check_nonlinear_convergence(
    const torch::Tensor& residual,
    const torch::Tensor& rhs,
    const JVPConvergenceParams& params) {

    // FIX 2025-12-27: Use safe_norm helper for GPU tensors
    float res_norm = safe_norm(residual);
    float rhs_norm = safe_norm(rhs);

    // Convergence: ||G|| <= max(atol, rtol * ||b||)
    float tol = std::max(params.atol, params.rtol * rhs_norm);

    return res_norm <= tol;
}

/**
 * Adaptive timestep adjustment considering JVP accuracy
 */
inline float compute_adaptive_timestep(
    float dt_current,
    float error_estimate,
    float tau_jvp,
    int order = 3,  // SDIRK3 order
    float safety = 0.9f,
    float dt_min_factor = 0.1f,
    float dt_max_factor = 2.0f) {
    
    // Consider both truncation error and JVP error
    float effective_error = std::max(error_estimate, 5.0f * tau_jvp);
    
    // Compute timestep factor: dt_new = dt * safety * (1/error)^(1/(order+1))
    float factor = safety * std::pow(1.0f / effective_error, 1.0f / (order + 1));
    
    // Apply bounds
    factor = std::max(dt_min_factor, std::min(dt_max_factor, factor));
    
    return dt_current * factor;
}

/**
 * Monitor and switch between FD and AD based on convergence
 */
struct JVPModeSelector {
    int stagnation_count = 0;
    int max_stagnation = 2;
    float stagnation_threshold = 0.95f;
    
    JVPMode current_mode = JVPMode::FD_FORWARD;
    
    JVPMode update_mode(float residual_ratio, bool line_search_failed) {
        // Check for stagnation
        if (residual_ratio > stagnation_threshold) {
            stagnation_count++;
        } else {
            stagnation_count = 0;
        }
        
        // Switch logic
        if (current_mode == JVPMode::FD_FORWARD) {
            // Upgrade to central difference if stagnating
            if (stagnation_count >= max_stagnation || line_search_failed) {
                current_mode = JVPMode::FD_CENTRAL;
                stagnation_count = 0;
            }
        } else if (current_mode == JVPMode::FD_CENTRAL) {
            // Upgrade to AD if still struggling
            if (stagnation_count >= max_stagnation && line_search_failed) {
                current_mode = JVPMode::AD_JVP;
                stagnation_count = 0;
            }
        }
        // AD_JVP doesn't downgrade unless explicitly requested
        
        return current_mode;
    }
    
    void reset() {
        stagnation_count = 0;
        current_mode = JVPMode::FD_FORWARD;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_JVP_CONVERGENCE_H