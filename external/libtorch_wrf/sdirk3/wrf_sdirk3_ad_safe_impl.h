#ifndef WRF_SDIRK3_AD_SAFE_IMPL_H
#define WRF_SDIRK3_AD_SAFE_IMPL_H

#include "wrf_sdirk3_torch_wrapper.h"
#include <torch/torch.h>
#include <functional>

namespace wrf {
namespace sdirk3 {
namespace ad {

/**
 * AD-Safe Implementation Patterns for WRF SDIRK3
 * 
 * This header provides automatic differentiation-safe implementations
 * of common operations that preserve computational graphs while
 * maintaining performance.
 */

// ============================================================================
// Convergence Checking Patterns
// ============================================================================

/**
 * AD-safe convergence checker that preserves gradient flow
 */
class ConvergenceChecker {
public:
    enum Mode {
        HARD,     // Traditional boolean (requires .item() for control)
        SOFT,     // Differentiable approximation (no .item() needed)
        HYBRID    // Both hard and soft for flexibility
    };
    
private:
    Mode mode_;
    float sharpness_;  // For soft convergence sigmoid
    
public:
    ConvergenceChecker(Mode mode = HYBRID, float sharpness = 100.0f)
        : mode_(mode), sharpness_(sharpness) {}
    
    /**
     * Check convergence without breaking gradient flow
     * Returns: {hard_converged, soft_converged_tensor}
     */
    std::pair<bool, torch::Tensor> check(
        const torch::Tensor& residual,
        float tolerance,
        const torch::Tensor& reference = torch::Tensor()
    ) {
        torch::Tensor res_norm = residual.norm();
        
        // Relative or absolute tolerance
        torch::Tensor tol_tensor = torch::tensor(tolerance, residual.options());
        if (reference.defined() && reference.numel() > 0) {
            torch::Tensor ref_norm = reference.norm();
            tol_tensor = tol_tensor * torch::max(torch::ones_like(ref_norm), ref_norm);
        }
        
        // Soft convergence (differentiable)
        torch::Tensor soft_converged = torch::sigmoid(
            sharpness_ * (tol_tensor - res_norm)
        );

        // Hard convergence (for control flow only)
        // FIX 2025-12-27: Transfer to CPU before .item() to avoid GPU sync in hot path
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        bool hard_converged = false;
        if (mode_ != SOFT) {
            torch::NoGradGuard no_grad;
            torch::Tensor converged = res_norm < tol_tensor;
            hard_converged = converged.all().to(torch::kCPU).item<bool>();
        }

        return {hard_converged, soft_converged};
    }
    
    /**
     * Multi-criteria convergence check
     */
    std::pair<bool, torch::Tensor> check_multi(
        const torch::Tensor& residual,
        float rel_tol,
        float abs_tol,
        const torch::Tensor& initial_residual
    ) {
        torch::Tensor res_norm = residual.norm();
        torch::Tensor init_norm = initial_residual.norm();
        
        // Relative convergence
        torch::Tensor rel_error = res_norm / (init_norm + 1e-10f);
        torch::Tensor rel_converged = torch::sigmoid(
            sharpness_ * (rel_tol - rel_error)
        );
        
        // Absolute convergence
        torch::Tensor abs_converged = torch::sigmoid(
            sharpness_ * (abs_tol - res_norm)
        );
        
        // Combined (either criterion)
        torch::Tensor soft_converged = torch::max(rel_converged, abs_converged);

        // Hard decision for control flow
        // FIX 2025-12-27: Transfer to CPU before .item() to avoid GPU sync in hot path
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        bool hard_converged = false;
        if (mode_ != SOFT) {
            torch::NoGradGuard no_grad;
            torch::Tensor converged = (rel_error < rel_tol) | (res_norm < abs_tol);
            hard_converged = converged.all().to(torch::kCPU).item<bool>();
        }

        return {hard_converged, soft_converged};
    }
};

// ============================================================================
// Numerical Error Handling
// ============================================================================

/**
 * AD-safe numerical error handler
 */
class NumericalErrorHandler {
public:
    /**
     * Handle NaN/Inf without breaking gradients
     */
    static torch::Tensor sanitize(
        const torch::Tensor& x,
        const torch::Tensor& fallback = torch::Tensor()
    ) {
        torch::Tensor is_finite = torch::isfinite(x);
        
        // Use fallback or zeros
        torch::Tensor safe_fallback = fallback.defined() ? 
            fallback : torch::zeros_like(x);
        
        // Conditional selection preserves gradients
        return torch::where(is_finite, x, safe_fallback);
    }
    
    /**
     * Bounds enforcement with soft clamping
     */
    static torch::Tensor soft_clamp(
        const torch::Tensor& x,
        float lower,
        float upper,
        float hardness = 4.0f
    ) {
        if (hardness > 100.0f) {
            // Hard clamp for very large hardness
            return torch::clamp(x, lower, upper);
        }
        
        // Soft clamping using tanh
        torch::Tensor range = upper - lower;
        torch::Tensor centered = (x - lower) / range - 0.5f;
        torch::Tensor soft_clamped = torch::tanh(centered * hardness) * 0.5f + 0.5f;
        return soft_clamped * range + lower;
    }
    
    /**
     * Check for numerical issues without .item()
     */
    static torch::Tensor check_validity(
        const torch::Tensor& x,
        bool& has_issues  // Output parameter for control flow
    ) {
        torch::Tensor has_nan = torch::any(torch::isnan(x));
        torch::Tensor has_inf = torch::any(torch::isinf(x));
        torch::Tensor is_large = torch::any(torch::abs(x) > 1e10f);
        
        // Soft indicator for gradients
        torch::Tensor soft_valid = 1.0f - torch::max({
            has_nan.to(torch::kFloat32),
            has_inf.to(torch::kFloat32),
            is_large.to(torch::kFloat32)
        });

        // Hard decision for control flow
        // FIX 2025-12-27: Add to(kCPU) before .item() to avoid GPU sync overhead
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        {
            torch::NoGradGuard no_grad;
            has_issues = (has_nan | has_inf | is_large).any().to(torch::kCPU).item<bool>();
        }

        return soft_valid;
    }
};

// ============================================================================
// JVP Computation Strategies
// ============================================================================

/**
 * Adaptive JVP computer that switches between AD and FD
 */
class AdaptiveJVP {
public:
    enum Method {
        AUTOGRAD,          // Pure automatic differentiation
        FINITE_DIFF,       // Finite differences
        ADAPTIVE,          // Switch based on context
        COMPLEX_STEP       // Complex step differentiation
    };
    
private:
    Method method_;
    float fd_epsilon_;
    bool cache_enabled_;
    std::unordered_map<size_t, torch::Tensor> cache_;
    
public:
    AdaptiveJVP(Method method = ADAPTIVE, float epsilon = 1e-6f)
        : method_(method), fd_epsilon_(epsilon), cache_enabled_(false) {}
    
    /**
     * Compute Jacobian-vector product J*v
     */
    torch::Tensor compute(
        std::function<torch::Tensor(torch::Tensor)> func,
        const torch::Tensor& x,
        const torch::Tensor& v,
        int iteration = 0
    ) {
        // Check cache
        if (cache_enabled_) {
            size_t key = hash_tensors(x, v);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }
        
        torch::Tensor jvp;
        
        // Select method
        Method active_method = method_;
        if (method_ == ADAPTIVE) {
            active_method = select_method(x, iteration);
        }
        
        switch (active_method) {
            case AUTOGRAD:
                jvp = compute_autograd(func, x, v);
                break;
                
            case FINITE_DIFF:
                jvp = compute_finite_diff(func, x, v);
                break;
                
            case COMPLEX_STEP:
                jvp = compute_complex_step(func, x, v);
                break;
                
            default:
                jvp = compute_autograd(func, x, v);
        }
        
        // Cache result
        if (cache_enabled_) {
            cache_[hash_tensors(x, v)] = jvp;
        }
        
        return jvp;
    }
    
    void enable_cache(bool enable = true) {
        cache_enabled_ = enable;
        if (!enable) cache_.clear();
    }
    
private:
    Method select_method(const torch::Tensor& x, int iteration) {
        // Adaptive strategy based on iteration and tensor properties
        if (iteration == 0 && x.requires_grad()) {
            return AUTOGRAD;  // First iteration: accuracy important
        } else if (iteration < 3) {
            return FINITE_DIFF;  // Middle: speed important
        } else if (x.requires_grad()) {
            return AUTOGRAD;  // Final: convergence important
        } else {
            return FINITE_DIFF;  // No gradients available
        }
    }
    
    torch::Tensor compute_autograd(
        std::function<torch::Tensor(torch::Tensor)> func,
        const torch::Tensor& x,
        const torch::Tensor& v
    ) {
        // FIX 2025-01-25: Clone before requires_grad_ to avoid modifying original
        torch::Tensor x_grad = x.clone().requires_grad_(true);

        // Forward-mode AD
        // FIX 2025-01-25: create_graph=false - not needed for Newton-Krylov (first-order only)
        // create_graph=true would retain graph for second derivatives, causing memory growth
        auto [output, jvp] = torch::autograd::functional::jvp(
            func, x_grad, v, false  // create_graph=false for Newton-Krylov
        );

        return jvp;
    }
    
    torch::Tensor compute_finite_diff(
        std::function<torch::Tensor(torch::Tensor)> func,
        const torch::Tensor& x,
        const torch::Tensor& v
    ) {
        // Adaptive epsilon scaling for WRF coupled variables
        torch::Tensor x_norm = x.norm();
        torch::Tensor v_norm = v.norm();
        torch::Tensor scale = torch::max(
            torch::ones_like(x_norm),
            x_norm / 1e5f  // WRF scale
        );

        // PERF FIX 2025-12-27: Batch CPU copy to reduce 2 syncs to 1
        // Stack scalars and copy once, then extract both values
        auto stacked = torch::stack({scale, torch::max(torch::ones_like(v_norm), v_norm)});

        // Compute with finite differences (no grad needed)
        // FIX Round192: Move NoGradGuard before .item() calls to prevent AD graph pollution
        torch::NoGradGuard no_grad;

        auto stacked_cpu = stacked.to(torch::kCPU);
        float scale_val = stacked_cpu[0].item<float>();
        float v_scale_val = stacked_cpu[1].item<float>();
        float eps = fd_epsilon_ * scale_val * v_scale_val;
        
        torch::Tensor f_plus = func(x + eps * v);
        torch::Tensor f_base = func(x);
        
        return (f_plus - f_base) / eps;
    }
    
    torch::Tensor compute_complex_step(
        std::function<torch::Tensor(torch::Tensor)> func,
        const torch::Tensor& x,
        const torch::Tensor& v
    ) {
        // Complex step differentiation (if supported)
        // Note: Requires complex tensor support
        float h = 1e-30f;  // Very small step
        
        torch::NoGradGuard no_grad;
        
        // Convert to complex
        auto x_complex = torch::complex(x, h * v);
        auto f_complex = func(x_complex);
        
        // Extract imaginary part
        return torch::imag(f_complex) / h;
    }
    
    size_t hash_tensors(const torch::Tensor& x, const torch::Tensor& v) {
        size_t hash = 0;
        
        // Hash shapes
        for (auto s : x.sizes()) {
            hash ^= std::hash<int64_t>{}(s) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        // Sample some values (avoid .item() in hot path)
        // FIX 2025-12-27: Add to(kCPU) before .item() to avoid GPU sync overhead
        // FIX 2025-12-27: Use data_ptr<float>() for direct scalar access (more efficient)
        // FIX Round176: Ensure FP32 dtype - data_ptr<float>() requires FP32
        if (x.numel() > 0 && v.numel() > 0) {
            auto x_flat = x.flatten().to(torch::kCPU, torch::kFloat32).contiguous();
            auto v_flat = v.flatten().to(torch::kCPU, torch::kFloat32).contiguous();

            // Use first element as part of hash via data_ptr (avoids tensor indexing overhead)
            const float* x_ptr = x_flat.data_ptr<float>();
            const float* v_ptr = v_flat.data_ptr<float>();
            float x_val = x_ptr[0];
            float v_val = v_ptr[0];

            hash ^= std::hash<float>{}(x_val) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<float>{}(v_val) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        
        return hash;
    }
};

// ============================================================================
// Line Search with Gradient Preservation
// ============================================================================

/**
 * AD-safe line search implementation
 */
class LineSearchAD {
public:
    struct Options {
        int max_iterations = 20;
        float c1 = 1e-4f;  // Armijo constant
        float c2 = 0.9f;   // Wolfe constant
        float alpha_init = 1.0f;
        float alpha_min = 1e-8f;
        float backtrack_factor = 0.5f;
    };
    
private:
    Options options_;
    
public:
    LineSearchAD(const Options& opts = Options()) : options_(opts) {}
    
    /**
     * Perform line search preserving gradients
     */
    torch::Tensor search(
        std::function<torch::Tensor(torch::Tensor)> func,
        std::function<torch::Tensor(torch::Tensor)> grad_func,
        const torch::Tensor& x,
        const torch::Tensor& direction,
        const torch::Tensor& f0 = torch::Tensor(),
        const torch::Tensor& g0 = torch::Tensor()
    ) {
        // Initial function value and gradient
        torch::Tensor f_init = f0.defined() ? f0 : func(x);
        torch::Tensor g_init = g0.defined() ? g0 : grad_func(x);
        
        // Directional derivative
        torch::Tensor dg = torch::sum(g_init * direction);
        
        // Initialize alpha
        torch::Tensor alpha = torch::tensor(options_.alpha_init, x.options());
        
        for (int iter = 0; iter < options_.max_iterations; iter++) {
            // Evaluate at new point
            torch::Tensor x_new = x + alpha * direction;
            torch::Tensor f_new = func(x_new);
            
            // Armijo condition (sufficient decrease)
            torch::Tensor expected_decrease = options_.c1 * alpha * dg;
            torch::Tensor actual_decrease = f_new - f_init;
            torch::Tensor armijo_satisfied = actual_decrease <= expected_decrease;

            // Check with .item() only for control flow
            // FIX 2025-12-27: Add to(kCPU) before .item() to avoid GPU sync overhead
            // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
            bool armijo_ok, wolfe_ok, alpha_too_small;
            {
                torch::NoGradGuard no_grad;
                armijo_ok = armijo_satisfied.all().to(torch::kCPU).item<bool>();
            }
            if (armijo_ok) {
                // Check Wolfe condition if needed
                if (options_.c2 < 1.0f) {
                    torch::Tensor g_new = grad_func(x_new);
                    torch::Tensor dg_new = torch::sum(g_new * direction);
                    torch::Tensor wolfe_satisfied = dg_new >= options_.c2 * dg;

                    {
                        torch::NoGradGuard no_grad;
                        wolfe_ok = wolfe_satisfied.all().to(torch::kCPU).item<bool>();
                    }
                    if (wolfe_ok) {
                        return x_new;  // Both conditions satisfied
                    }
                }
                else {
                    return x_new;  // Only Armijo needed
                }
            }

            // Backtrack
            alpha = alpha * options_.backtrack_factor;

            // Check minimum alpha
            {
                torch::NoGradGuard no_grad;
                alpha_too_small = (alpha < options_.alpha_min).all().to(torch::kCPU).item<bool>();
            }
            if (alpha_too_small) {
                // Accept small step
                return x + options_.alpha_min * direction;
            }
        }
        
        // Fallback: small step
        return x + 0.1f * options_.alpha_min * direction;
    }
    
    /**
     * Simple backtracking line search (faster, less robust)
     */
    torch::Tensor backtrack(
        std::function<torch::Tensor(torch::Tensor)> merit_func,
        const torch::Tensor& x,
        const torch::Tensor& direction,
        const torch::Tensor& merit0 = torch::Tensor()
    ) {
        torch::Tensor merit_init = merit0.defined() ? merit0 : merit_func(x);
        torch::Tensor alpha = torch::tensor(options_.alpha_init, x.options());
        
        // FIX 2025-12-27: Add to(kCPU) before .item() to avoid GPU sync overhead
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        for (int iter = 0; iter < options_.max_iterations; iter++) {
            torch::Tensor x_new = x + alpha * direction;
            torch::Tensor merit_new = merit_func(x_new);

            // Simple decrease condition
            torch::Tensor improved = merit_new < merit_init * (1.0f - options_.c1 * alpha);

            bool is_improved, alpha_too_small;
            {
                torch::NoGradGuard no_grad;
                is_improved = improved.all().to(torch::kCPU).item<bool>();
            }
            if (is_improved) {
                return x_new;
            }

            alpha = alpha * options_.backtrack_factor;

            {
                torch::NoGradGuard no_grad;
                alpha_too_small = (alpha < options_.alpha_min).all().to(torch::kCPU).item<bool>();
            }
            if (alpha_too_small) {
                break;
            }
        }
        
        // Fallback
        return x + options_.alpha_min * direction;
    }
};

// ============================================================================
// Memory-Efficient Operations
// ============================================================================

/**
 * Gradient checkpointing utilities
 */
class CheckpointingUtils {
public:
    /**
     * Checkpointed function evaluation
     */
    template<typename Func>
    static torch::Tensor checkpoint(
        Func func,
        const torch::Tensor& input,
        bool use_reentrant = false
    ) {
        // Use PyTorch's checkpointing
        return torch::utils::checkpoint(func, input, use_reentrant);
    }
    
    /**
     * Segmented checkpointing for large computations
     */
    static torch::Tensor segmented_compute(
        std::function<torch::Tensor(torch::Tensor)> segment1,
        std::function<torch::Tensor(torch::Tensor)> segment2,
        std::function<torch::Tensor(torch::Tensor)> segment3,
        const torch::Tensor& input
    ) {
        // Checkpoint each segment
        auto out1 = checkpoint(segment1, input);
        auto out2 = checkpoint(segment2, out1);
        auto out3 = checkpoint(segment3, out2);
        
        return out3;
    }
};

/**
 * Selective gradient tracking
 */
class SelectiveGradients {
public:
    /**
     * Enable gradients only for specified tensors
     */
    static void set_selective_gradients(
        std::vector<torch::Tensor>& tensors,
        const std::vector<bool>& require_grad
    ) {
        assert(tensors.size() == require_grad.size());
        
        for (size_t i = 0; i < tensors.size(); i++) {
            if (require_grad[i]) {
                tensors[i].requires_grad_(true);
            } else {
                tensors[i] = tensors[i].detach();
            }
        }
    }
    
    /**
     * Context manager for temporary gradient disabling
     */
    class NoGradContext {
    private:
        std::vector<torch::Tensor> tensors_;
        std::vector<bool> original_requires_grad_;
        
    public:
        NoGradContext(std::vector<torch::Tensor>& tensors) : tensors_(tensors) {
            // Save original state
            for (const auto& t : tensors_) {
                original_requires_grad_.push_back(t.requires_grad());
                t.requires_grad_(false);
            }
        }
        
        ~NoGradContext() {
            // Restore original state
            for (size_t i = 0; i < tensors_.size(); i++) {
                tensors_[i].requires_grad_(original_requires_grad_[i]);
            }
        }
    };
};

// ============================================================================
// Performance Monitoring
// ============================================================================

/**
 * AD performance monitor
 */
class ADPerformanceMonitor {
public:
    struct Stats {
        double forward_time = 0.0;
        double backward_time = 0.0;
        size_t graph_nodes = 0;
        size_t peak_memory = 0;
        double overhead_percent = 0.0;
    };
    
private:
    Stats stats_;
    bool monitoring_ = false;
    
public:
    void start_monitoring() {
        monitoring_ = true;
        stats_ = Stats();
    }
    
    void stop_monitoring() {
        monitoring_ = false;
    }
    
    template<typename Func>
    torch::Tensor timed_forward(
        Func func,
        const torch::Tensor& input
    ) {
        if (!monitoring_) {
            return func(input);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        auto output = func(input);
        auto end = std::chrono::high_resolution_clock::now();
        
        stats_.forward_time += std::chrono::duration<double>(end - start).count();
        
        // Count graph nodes
        if (output.grad_fn()) {
            stats_.graph_nodes = count_graph_nodes(output);
        }
        
        return output;
    }
    
    const Stats& get_stats() const { return stats_; }
    
private:
    size_t count_graph_nodes(const torch::Tensor& tensor) {
        size_t count = 0;
        
        // Simple BFS traversal of computation graph
        std::queue<std::shared_ptr<torch::autograd::Node>> queue;
        std::set<torch::autograd::Node*> visited;
        
        if (tensor.grad_fn()) {
            queue.push(tensor.grad_fn());
        }
        
        while (!queue.empty()) {
            auto node = queue.front();
            queue.pop();
            
            if (!node || visited.count(node.get())) continue;
            visited.insert(node.get());
            count++;
            
            // Add next nodes (simplified)
            // Note: Actual implementation would need to traverse edges properly
        }
        
        return count;
    }
};

} // namespace ad
} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AD_SAFE_IMPL_H