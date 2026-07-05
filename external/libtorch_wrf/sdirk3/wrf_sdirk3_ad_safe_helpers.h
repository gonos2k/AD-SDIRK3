/**
 * @file wrf_sdirk3_ad_safe_helpers.h
 * @brief AD-safe helper functions for PyTorch automatic differentiation
 * @author WRF-SDIRK3 Team
 * @date 2025-08-17
 * 
 * This header provides AD-safe alternatives to .item() calls and other
 * operations that break the PyTorch computational graph.
 */

#ifndef WRF_SDIRK3_AD_SAFE_HELPERS_H
#define WRF_SDIRK3_AD_SAFE_HELPERS_H

#include <torch/torch.h>
#include <iostream>
#include <string>
#include "wrf_sdirk3_config.h"  // OPT Pass33+: For g_sdirk3_config.debug_level

namespace wrf {
namespace sdirk3 {

/**
 * AD-safe validation class
 * Performs tensor validation without breaking the computational graph
 */
class ADSafeValidation {
public:
    /**
     * Check if tensor contains NaN without .item()
     * Returns a tensor that can be used in conditional operations
     */
    static torch::Tensor has_nan(const torch::Tensor& tensor) {
        return torch::any(torch::isnan(tensor));
    }
    
    /**
     * Check if tensor contains Inf without .item()
     */
    static torch::Tensor has_inf(const torch::Tensor& tensor) {
        return torch::any(torch::isinf(tensor));
    }
    
    /**
     * Check if tensor is in valid range without .item()
     * Returns a tensor mask for where operation
     */
    static torch::Tensor in_range(const torch::Tensor& tensor, 
                                  float min_val, float max_val) {
        auto min_tensor = torch::full_like(tensor, min_val);
        auto max_tensor = torch::full_like(tensor, max_val);
        return (tensor >= min_tensor) & (tensor <= max_tensor);
    }
    
    /**
     * AD-safe min/max computation
     * Returns tensors, not scalar values
     */
    static std::pair<torch::Tensor, torch::Tensor> minmax(const torch::Tensor& tensor) {
        return {tensor.min(), tensor.max()};
    }
    
    /**
     * Conditional execution without .item()
     * Uses torch::where for AD-safe branching
     */
    static torch::Tensor conditional_value(const torch::Tensor& condition,
                                          const torch::Tensor& true_value,
                                          const torch::Tensor& false_value) {
        // Expand condition to match value dimensions if needed
        auto cond_expanded = condition;
        if (condition.dim() == 0 && true_value.dim() > 0) {
            cond_expanded = condition.expand_as(true_value);
        }
        return torch::where(cond_expanded, true_value, false_value);
    }
    
    /**
     * AD-safe clamping with tensor bounds
     */
    static torch::Tensor safe_clamp(const torch::Tensor& tensor,
                                   const torch::Tensor& min_tensor,
                                   const torch::Tensor& max_tensor) {
        return torch::clamp(tensor, min_tensor, max_tensor);
    }
    
    /**
     * Print tensor statistics without .item()
     * For debugging only - doesn't break graph but prints tensor objects
     * OPT Pass33+: Added global config gate to prevent accidental output
     *
     * PERFORMANCE NOTE: Contains reduce operations (min/max/mean/std) which
     * can be expensive on large tensors. Default is debug_level=2 to avoid
     * production overhead. Pass debug_level=1 for critical diagnostics only.
     */
    static void print_stats(const torch::Tensor& tensor,
                           const std::string& name,
                           int debug_level = 2) {
        // OPT Pass33+: Double gate - parameter AND global config must both allow output
        if (debug_level > 0 && g_sdirk3_config.debug_level >= debug_level) {
            auto min_t = tensor.min();
            auto max_t = tensor.max();
            auto mean_t = tensor.mean();
            auto std_t = tensor.std();

            std::cerr << "AD-Safe Stats for " << name << ":" << std::endl;
            std::cerr << "  Shape: " << tensor.sizes() << std::endl;
            std::cerr << "  Min: " << min_t << std::endl;
            std::cerr << "  Max: " << max_t << std::endl;
            std::cerr << "  Mean: " << mean_t << std::endl;
            std::cerr << "  Std: " << std_t << std::endl;
            std::cerr << "  Has NaN: " << has_nan(tensor) << std::endl;
            std::cerr << "  Has Inf: " << has_inf(tensor) << std::endl;
        }
    }
    
    /**
     * AD-safe validation with fallback
     * Returns validated tensor without breaking graph
     */
    static torch::Tensor validate_and_fix(const torch::Tensor& tensor,
                                         float min_valid = -1e10f,
                                         float max_valid = 1e10f,
                                         float fallback_value = 0.0f) {
        // Create masks for invalid values
        auto nan_mask = torch::isnan(tensor);
        auto inf_mask = torch::isinf(tensor);
        auto invalid_mask = nan_mask | inf_mask;
        
        // Create range mask
        auto min_tensor = torch::full_like(tensor, min_valid);
        auto max_tensor = torch::full_like(tensor, max_valid);
        auto out_of_range = (tensor < min_tensor) | (tensor > max_tensor);
        
        // Combine all invalid conditions
        auto all_invalid = invalid_mask | out_of_range;
        
        // Replace invalid values with fallback
        auto fallback_tensor = torch::full_like(tensor, fallback_value);
        return torch::where(all_invalid, fallback_tensor, tensor);
    }
};

/**
 * AD-safe mathematical operations
 */
class ADSafeMath {
public:
    /**
     * Safe reciprocal avoiding division by zero
     */
    static torch::Tensor safe_reciprocal(const torch::Tensor& x, 
                                        float epsilon = 1e-10f) {
        auto safe_x = torch::where(torch::abs(x) < epsilon,
                                  torch::full_like(x, epsilon) * torch::sign(x),
                                  x);
        return 1.0f / safe_x;
    }
    
    /**
     * Safe division
     */
    static torch::Tensor safe_divide(const torch::Tensor& numerator,
                                    const torch::Tensor& denominator,
                                    float epsilon = 1e-10f) {
        auto safe_denom = torch::where(torch::abs(denominator) < epsilon,
                                      torch::full_like(denominator, epsilon) * torch::sign(denominator),
                                      denominator);
        return numerator / safe_denom;
    }
    
    /**
     * Safe square root (handles negative values)
     */
    static torch::Tensor safe_sqrt(const torch::Tensor& x,
                                  float min_value = 0.0f) {
        auto safe_x = torch::clamp(x, min_value);
        return torch::sqrt(safe_x);
    }
    
    /**
     * Safe logarithm
     */
    static torch::Tensor safe_log(const torch::Tensor& x,
                                 float min_value = 1e-10f) {
        auto safe_x = torch::clamp(x, min_value);
        return torch::log(safe_x);
    }
};

/**
 * AD-safe flux computation for advection
 */
class ADSafeFlux {
public:
    /**
     * 5th-order flux computation without .item() calls
     */
    static torch::Tensor flux5(const torch::Tensor& q_m2,
                              const torch::Tensor& q_m1,
                              const torch::Tensor& q_0,
                              const torch::Tensor& q_p1,
                              const torch::Tensor& q_p2,
                              const torch::Tensor& q_p3,
                              const torch::Tensor& vel) {
        // Upwind-biased 5th order flux
        auto vel_sign = torch::sign(vel);
        auto vel_positive = (vel_sign + 1.0f) * 0.5f;  // 1 if vel > 0, 0 otherwise
        auto vel_negative = 1.0f - vel_positive;        // 1 if vel <= 0, 0 otherwise
        
        // Positive velocity flux
        auto flux_pos = (37.0f * q_0 + 37.0f * q_p1
                        - 8.0f * q_m1 - 8.0f * q_p2
                        + q_m2 + q_p3) / 60.0f;
        
        // Negative velocity flux (mirror of positive)
        auto flux_neg = (37.0f * q_p1 + 37.0f * q_0
                        - 8.0f * q_p2 - 8.0f * q_m1
                        + q_p3 + q_m2) / 60.0f;
        
        // Select based on velocity sign
        return vel_positive * flux_pos + vel_negative * flux_neg;
    }
    
    /**
     * 3rd-order flux computation
     */
    static torch::Tensor flux3(const torch::Tensor& q_m1,
                              const torch::Tensor& q_0,
                              const torch::Tensor& q_p1,
                              const torch::Tensor& q_p2,
                              const torch::Tensor& vel) {
        auto vel_sign = torch::sign(vel);
        auto vel_positive = (vel_sign + 1.0f) * 0.5f;
        auto vel_negative = 1.0f - vel_positive;
        
        // 3rd order upwind-biased
        auto flux_pos = (7.0f * q_0 + 7.0f * q_p1 - q_m1 - q_p2) / 12.0f;
        auto flux_neg = (7.0f * q_p1 + 7.0f * q_0 - q_p2 - q_m1) / 12.0f;
        
        return vel_positive * flux_pos + vel_negative * flux_neg;
    }
};

/**
 * Macro to replace .item() calls in existing code
 * Usage: Replace tensor.min().item<float>() with AD_SAFE_SCALAR(tensor.min())
 *
 * FIX 2025-12-31 Batch41: Added NoGradGuard + LINT:DIAG_OK for lint recognition
 */
#ifdef ENABLE_AD_SAFE_MODE
    #define AD_SAFE_SCALAR(tensor_expr) (tensor_expr)
    #define AD_SAFE_BOOL(tensor_expr) (tensor_expr)
#else
    // In non-AD mode, extract scalar for diagnostics only
    // FIX 2025-12-27: Add .to(torch::kCPU) before .item<>() to avoid GPU->CPU sync overhead
    // FIX 2025-12-31: Add NoGradGuard to prevent AD graph pollution
    // LINT:DIAG_OK - These macros are safe for diagnostic use
    #define AD_SAFE_SCALAR(tensor_expr) \
        ([&]() -> float { \
            torch::NoGradGuard no_grad; \
            return (tensor_expr).to(torch::kCPU).item<float>(); \
        }())
    #define AD_SAFE_BOOL(tensor_expr) \
        ([&]() -> bool { \
            torch::NoGradGuard no_grad; \
            return (tensor_expr).to(torch::kCPU).item<bool>(); \
        }())
#endif

/**
 * AD_SAFE_DOUBLE - For double precision diagnostics
 * FIX 2025-12-31 Batch41: Added for Newton solver convergence checks
 */
#ifdef ENABLE_AD_SAFE_MODE
    #define AD_SAFE_DOUBLE(tensor_expr) (tensor_expr)
#else
    // LINT:DIAG_OK - Safe for diagnostic use
    #define AD_SAFE_DOUBLE(tensor_expr) \
        ([&]() -> double { \
            torch::NoGradGuard no_grad; \
            return (tensor_expr).to(torch::kCPU).item<double>(); \
        }())
#endif

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AD_SAFE_HELPERS_H