#ifndef SDIRK3_AUTOGRAD_H
#define SDIRK3_AUTOGRAD_H

#include "torch_standalone.h"
#include <memory>
#include <unordered_map>
#include "unified_rhs.h"
#include "newton_krylov_solver.h"

namespace wrf {
namespace sdirk3 {

// Context storage for SDIRK stage residual backward pass
struct SDIRKContext {
    torch::Tensor delta_i;
    torch::Tensor U_n;
    std::vector<torch::Tensor> k_prev;
    std::shared_ptr<UnifiedRHS> rhs_module;
    SDIRKCoefficients coeffs;
    double dt;
    int stage;
    torch::Tensor U_arg;  // Saved intermediate computation
    torch::Tensor F_val;  // Saved RHS evaluation
};

// Global context storage (workaround for autograd limitations)
class SDIRKContextManager {
private:
    static std::unordered_map<void*, std::shared_ptr<SDIRKContext>> contexts_;
    static std::mutex mutex_;
    
public:
    static void save_context(void* key, std::shared_ptr<SDIRKContext> ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        contexts_[key] = ctx;
    }
    
    static std::shared_ptr<SDIRKContext> get_context(void* key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = contexts_.find(key);
        if (it != contexts_.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    static void clear_context(void* key) {
        std::lock_guard<std::mutex> lock(mutex_);
        contexts_.erase(key);
    }
};

// Custom autograd function for SDIRK stage residual - simplified version
class SDIRKStageResidualFunction {
public:
    static torch::Tensor apply(
        const torch::Tensor& delta_i,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        std::shared_ptr<UnifiedRHS> rhs_module,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage
    );
};

// Jacobian-Vector Product computation using automatic differentiation
class JacobianVectorProduct {
private:
    std::shared_ptr<UnifiedRHS> rhs_module_;
    torch::Tensor base_state_;
    
public:
    JacobianVectorProduct(std::shared_ptr<UnifiedRHS> rhs_module, 
                         const torch::Tensor& base_state)
        : rhs_module_(rhs_module), base_state_(base_state) {}
    
    // Compute J*v where J = ∂F/∂U at base_state
    torch::Tensor apply(const torch::Tensor& v) {
        // Enable gradient computation
        base_state_.requires_grad_(true);
        
        // Forward pass
        auto F = rhs_module_->forward(base_state_);
        
        // Compute JVP using autograd
        auto jvp = torch::autograd::grad(
            {F}, {base_state_}, {v},
            /*retain_graph=*/true,
            /*create_graph=*/false,
            /*allow_unused=*/false
        )[0];
        
        base_state_.requires_grad_(false);
        return jvp;
    }
    
    // Compute v^T * J (Vector-Jacobian Product)
    torch::Tensor apply_transpose(const torch::Tensor& v) {
        // Enable gradient computation
        base_state_.requires_grad_(true);
        
        // Forward pass
        auto F = rhs_module_->forward(base_state_);
        
        // Compute VJP (which is JVP for the transposed Jacobian)
        auto vjp = torch::autograd::grad(
            {F}, {base_state_}, {v},
            /*retain_graph=*/true,
            /*create_graph=*/false,
            /*allow_unused=*/false
        )[0];
        
        base_state_.requires_grad_(false);
        return vjp;
    }
};

// Helper function to create JVP operator for Newton-Krylov
inline std::function<torch::Tensor(const torch::Tensor&)> 
create_jvp_operator(std::shared_ptr<UnifiedRHS> rhs_module,
                   const torch::Tensor& U_current,
                   double dt_gamma) {
    auto jvp_computer = std::make_shared<JacobianVectorProduct>(rhs_module, U_current);
    
    return [jvp_computer, dt_gamma](const torch::Tensor& v) -> torch::Tensor {
        // Compute (I - dt*gamma*J)*v
        auto Jv = jvp_computer->apply(v);
        return v - dt_gamma * Jv;
    };
}

} // namespace sdirk3
} // namespace wrf

#endif // SDIRK3_AUTOGRAD_H