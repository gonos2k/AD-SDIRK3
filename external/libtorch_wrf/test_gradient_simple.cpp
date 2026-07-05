// Simple gradient flow test for WRF-SDIRK3
#include <torch/torch.h>
#include <iostream>
#include "sdirk3/wrf_sdirk3_types.h"
#include "sdirk3/wrf_sdirk3_unified_rhs.h"
#include "sdirk3/wrf_sdirk3_jvp_autograd.h"

using namespace wrf::sdirk3;

int main() {
    std::cout << "=== WRF-SDIRK3 Gradient Flow Test ===" << std::endl;
    std::cout << "Focus: Automatic Differentiation for Newton-Krylov Solver" << std::endl;
    std::cout << "Principle: No clamping, maintain WRF calculation order\n" << std::endl;
    
    // Test dimensions (small for quick testing)
    int nx = 10, ny = 10, nz = 5;
    
    // Create test state with requires_grad=true for autodiff
    torch::Tensor U = torch::randn({5, ny, nz, nx}, torch::requires_grad(true));
    std::cout << "State tensor shape: " << U.sizes() << std::endl;
    std::cout << "State requires_grad: " << U.requires_grad() << std::endl;
    
    // Create minimal grid info
    auto grid_info = std::make_shared<WRFGridInfo>();
    grid_info->nx = nx;
    grid_info->ny = ny;
    grid_info->nz = nz;
    grid_info->dx = 1000.0f;
    grid_info->dy = 1000.0f;
    grid_info->dz = 100.0f;
    grid_info->rdx = torch::tensor(1.0f / grid_info->dx);
    grid_info->rdy = torch::tensor(1.0f / grid_info->dy);
    grid_info->rdz = torch::tensor(1.0f / grid_info->dz);
    grid_info->g = 9.81f;
    
    // Initialize base state (essential for RHS computation)
    grid_info->th_base = torch::full({ny, nz, nx}, 300.0f);  // Base potential temperature
    grid_info->p_base = torch::full({ny, nz, nx}, 100000.0f); // Base pressure
    grid_info->mub = torch::full({ny, nx}, 10000.0f);         // Base column mass
    
    std::cout << "\n--- Test 1: RHS Forward Pass ---" << std::endl;
    try {
        UnifiedRHS rhs_func(grid_info);
        torch::Tensor F = rhs_func.forward(U);
        
        std::cout << "✅ RHS output shape: " << F.sizes() << std::endl;
        std::cout << "   RHS norm: " << F.norm().item<float>() << std::endl;
        std::cout << "   Has NaN: " << torch::any(torch::isnan(F)).item<bool>() << std::endl;
        std::cout << "   Has Inf: " << torch::any(torch::isinf(F)).item<bool>() << std::endl;
        
        // Test gradient computation
        std::cout << "\n--- Test 2: Gradient Computation ---" << std::endl;
        torch::Tensor loss = F.sum();
        
        // Compute gradient with create_graph=true for second-order derivatives
        auto grad = torch::autograd::grad({loss}, {U}, {}, /*retain_graph=*/true, /*create_graph=*/true);
        
        if (!grad.empty() && grad[0].defined()) {
            std::cout << "✅ Gradient shape: " << grad[0].sizes() << std::endl;
            std::cout << "   Gradient norm: " << grad[0].norm().item<float>() << std::endl;
            std::cout << "   Gradient has NaN: " << torch::any(torch::isnan(grad[0])).item<bool>() << std::endl;
            
            // Test second-order derivatives (essential for implicit solver)
            std::cout << "\n--- Test 3: Second-Order Derivatives ---" << std::endl;
            torch::Tensor grad_loss = grad[0].sum();
            auto grad2 = torch::autograd::grad({grad_loss}, {U}, {}, /*retain_graph=*/false, /*create_graph=*/false);
            
            if (!grad2.empty() && grad2[0].defined()) {
                std::cout << "✅ Second derivative exists!" << std::endl;
                std::cout << "   Second derivative norm: " << grad2[0].norm().item<float>() << std::endl;
                std::cout << "   CRITICAL: Second-order derivatives working for SDIRK3!" << std::endl;
            } else {
                std::cout << "❌ Second-order derivatives NOT working!" << std::endl;
                std::cout << "   This will break the implicit solver!" << std::endl;
            }
        } else {
            std::cout << "❌ First-order gradient computation failed!" << std::endl;
        }
        
        // Test JVP computation (core of Newton-Krylov)
        std::cout << "\n--- Test 4: JVP Computation ---" << std::endl;
        torch::Tensor v = torch::randn_like(U);
        
        // Create function wrapper for JVP computation
        auto F_lambda = [&rhs_func](const torch::Tensor& x) -> torch::Tensor {
            UnifiedRHS* rhs_ptr = const_cast<UnifiedRHS*>(&rhs_func);
            return rhs_ptr->forward(x);
        };
        std::function<torch::Tensor(const torch::Tensor&)> F_wrapper(F_lambda);
        
        torch::Tensor jvp = compute_jvp_autograd(F_wrapper, U, v);
        std::cout << "✅ JVP shape: " << jvp.sizes() << std::endl;
        std::cout << "   JVP norm: " << jvp.norm().item<float>() << std::endl;
        std::cout << "   JVP has NaN: " << torch::any(torch::isnan(jvp)).item<bool>() << std::endl;
        
        // Verify JVP correctness using finite differences
        float epsilon = 1e-6f;
        torch::Tensor F_plus = rhs_func.forward(U + epsilon * v);
        torch::Tensor F_base = rhs_func.forward(U);
        torch::Tensor jvp_fd = (F_plus - F_base) / epsilon;
        
        torch::Tensor jvp_error = (jvp - jvp_fd).norm() / jvp_fd.norm();
        std::cout << "   JVP relative error vs finite difference: " << jvp_error.item<float>() << std::endl;
        if (jvp_error.item<float>() < 1e-3f) {
            std::cout << "   ✅ JVP computation is accurate!" << std::endl;
        } else {
            std::cout << "   ⚠️ JVP error is large, may affect convergence" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Gradient Flow Test Complete ===" << std::endl;
    std::cout << "Ready for em_b_wave long-term testing!" << std::endl;
    
    return 0;
}