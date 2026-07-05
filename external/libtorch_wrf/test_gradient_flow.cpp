#include <torch/torch.h>
#include <iostream>
#include "sdirk3/wrf_sdirk3_types.h"
#include "sdirk3/wrf_sdirk3_unified_rhs.h"
#include "sdirk3/wrf_sdirk3_jvp_autograd.h"
#include "sdirk3/wrf_sdirk3_newton_solver.h"

using namespace wrf::sdirk3;

int main() {
    std::cout << "=== Gradient Flow Test for WRF-SDIRK3 ===" << std::endl;
    
    // Test dimensions
    int nx = 10, ny = 10, nz = 5;
    
    // Create test state with requires_grad
    torch::Tensor U = torch::randn({5, ny, nz, nx}, torch::requires_grad(true));
    
    // Create grid info (minimal for testing)
    auto grid_info = std::make_shared<GridInfo>();
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
    
    // Initialize base state
    grid_info->th_base = torch::full({ny, nz, nx}, 300.0f);
    grid_info->p_base = torch::full({ny, nz, nx}, 100000.0f);
    grid_info->mub = torch::full({ny, nx}, 10000.0f);
    
    // Create RHS function
    UnifiedRHS rhs_func(grid_info);
    
    // Test 1: Direct gradient computation
    std::cout << "\nTest 1: Direct gradient computation" << std::endl;
    {
        torch::Tensor F = rhs_func.forward(U);
        torch::Tensor loss = F.sum();
        
        auto grad = torch::autograd::grad({loss}, {U}, {}, true, true)[0];
        
        std::cout << "  Input requires_grad: " << U.requires_grad() << std::endl;
        std::cout << "  RHS output shape: " << F.sizes() << std::endl;
        std::cout << "  Gradient shape: " << grad.sizes() << std::endl;
        std::cout << "  Gradient norm: " << grad.norm().item<float>() << std::endl;
        std::cout << "  Has NaN in gradient: " << torch::any(torch::isnan(grad)).item<bool>() << std::endl;
    }
    
    // Test 2: JVP computation (for Newton-Krylov)
    std::cout << "\nTest 2: JVP computation" << std::endl;
    {
        torch::Tensor v = torch::randn_like(U);
        
        auto F_wrapper = [&rhs_func](const torch::Tensor& x) {
            return rhs_func.forward(x);
        };
        
        torch::Tensor jvp = compute_jvp_autograd(F_wrapper, U, v);
        
        std::cout << "  JVP shape: " << jvp.sizes() << std::endl;
        std::cout << "  JVP norm: " << jvp.norm().item<float>() << std::endl;
        std::cout << "  Has NaN in JVP: " << torch::any(torch::isnan(jvp)).item<bool>() << std::endl;
    }
    
    // Test 3: Second-order derivatives (for implicit solver)
    std::cout << "\nTest 3: Second-order derivatives" << std::endl;
    {
        torch::Tensor U_var = U.clone().requires_grad_(true);
        torch::Tensor F = rhs_func(U_var);
        
        // First derivative
        torch::Tensor v1 = torch::randn_like(U);
        auto grad1 = torch::autograd::grad({F}, {U_var}, {v1}, true, true)[0];
        
        // Second derivative
        torch::Tensor v2 = torch::randn_like(U);
        auto grad2 = torch::autograd::grad({grad1}, {U_var}, {v2}, false, false);
        
        if (!grad2.empty() && grad2[0].defined()) {
            std::cout << "  Second derivative shape: " << grad2[0].sizes() << std::endl;
            std::cout << "  Second derivative norm: " << grad2[0].norm().item<float>() << std::endl;
            std::cout << "  ✅ Second-order derivatives working!" << std::endl;
        } else {
            std::cout << "  ❌ Second-order derivatives NOT working!" << std::endl;
        }
    }
    
    // Test 4: Newton solver with gradient flow
    std::cout << "\nTest 4: Newton solver gradient test" << std::endl;
    {
        NewtonSolverOptions options;
        options.max_iter = 3;
        options.tol = 1e-6f;
        options.verbose = false;
        
        NewtonSolver solver(options);
        
        float dt = 1.0f;
        double gamma = 0.5;
        
        auto compute_rhs = [&rhs_func](const torch::Tensor& x) {
            return rhs_func.forward(x);
        };
        
        torch::Tensor K = solver.solve(U, torch::zeros_like(U), compute_rhs, dt, gamma, 1);
        
        std::cout << "  Newton solution shape: " << K.sizes() << std::endl;
        std::cout << "  Newton solution norm: " << K.norm().item<float>() << std::endl;
        std::cout << "  Solution requires_grad: " << K.requires_grad() << std::endl;
        
        if (K.requires_grad()) {
            torch::Tensor loss = K.sum();
            auto grad = torch::autograd::grad({loss}, {U}, {}, false, false);
            if (!grad.empty() && grad[0].defined()) {
                std::cout << "  ✅ Gradient flows through Newton solver!" << std::endl;
                std::cout << "  Gradient norm: " << grad[0].norm().item<float>() << std::endl;
            } else {
                std::cout << "  ❌ Gradient does NOT flow through Newton solver" << std::endl;
            }
        }
    }
    
    std::cout << "\n=== Gradient Flow Test Complete ===" << std::endl;
    
    return 0;
}