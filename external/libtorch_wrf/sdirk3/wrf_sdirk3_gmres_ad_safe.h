#ifndef WRF_SDIRK3_GMRES_AD_SAFE_H
#define WRF_SDIRK3_GMRES_AD_SAFE_H

#include <torch/torch.h>
#include <vector>
#include <functional>
#include <cmath>
#include "wrf_sdirk3_state_vector.h"
#include "wrf_sdirk3_ad_safe_helpers.h"

namespace wrf {
namespace sdirk3 {

/**
 * AD-Safe GMRES Solver with Givens Rotation
 * Maintains PyTorch computational graph for automatic differentiation
 * 
 * CRITICAL AD-SAFE IMPROVEMENTS:
 * - All operations use tensor arithmetic instead of .item()
 * - Givens rotations computed with tensor operations
 * - Convergence checks use tensor comparisons
 */
class GMRESSolverADSafe {
public:
    struct Config {
        int max_iter = 100;
        int restart = 30;
        float tol = 1e-6;
        bool verbose = false;
    };
    
    struct GivensRotationADSafe {
        torch::Tensor c, s;
        
        GivensRotationADSafe(const torch::Tensor& a, const torch::Tensor& b) {
            // AD-SAFE: Compute Givens rotation without .item()
            auto b_zero = torch::abs(b) < 1e-10f;
            auto b_larger = torch::abs(b) > torch::abs(a);
            
            // Case 1: b == 0
            auto c1 = torch::ones_like(a);
            auto s1 = torch::zeros_like(a);
            
            // Case 2: |b| > |a|
            auto tau2 = -a / torch::where(torch::abs(b) < 1e-10f, torch::ones_like(b), b);
            auto s2 = 1.0f / torch::sqrt(1.0f + tau2 * tau2);
            auto c2 = s2 * tau2;
            
            // Case 3: |a| >= |b|
            auto tau3 = -b / torch::where(torch::abs(a) < 1e-10f, torch::ones_like(a), a);
            auto c3 = 1.0f / torch::sqrt(1.0f + tau3 * tau3);
            auto s3 = c3 * tau3;
            
            // Select based on conditions (AD-safe branching)
            c = torch::where(b_zero, c1, torch::where(b_larger, c2, c3));
            s = torch::where(b_zero, s1, torch::where(b_larger, s2, s3));
        }
        
        void apply(torch::Tensor& H, int i, int j) {
            // AD-SAFE: Apply rotation without .item()
            auto h1 = H.index({i, j});
            auto h2 = H.index({i + 1, j});
            
            H.index_put_({i, j}, c * h1 - s * h2);
            H.index_put_({i + 1, j}, s * h1 + c * h2);
        }
        
        void apply_to_vector(torch::Tensor& g, int i) {
            // AD-SAFE: Apply to vector without .item()
            auto g1 = g.index({i, 0});
            auto g2 = g.index({i + 1, 0});
            
            g.index_put_({i, 0}, c * g1 - s * g2);
            g.index_put_({i + 1, 0}, s * g1 + c * g2);
        }
    };
    
    /**
     * AD-Safe GMRES solve
     * Maintains computational graph throughout the solution process
     */
    static StateVector solve(
        std::function<StateVector(const StateVector&)> A_times_v,
        const StateVector& b,
        const StateVector& x0,
        const Config& config,
        std::function<StateVector(const StateVector&)> preconditioner = nullptr) {
        
        StateVector x = x0.clone();
        StateVector r = b - A_times_v(x);
        
        // Apply preconditioner if provided
        if (preconditioner) {
            r = preconditioner(r);
        }
        
        // AD-SAFE: Keep beta as tensor
        auto beta = r.norm();
        auto tol_tensor = torch::full_like(beta, config.tol);
        
        // AD-SAFE: Early convergence check without .item()
        auto converged = beta < tol_tensor;
        if (config.verbose) {
            // Only use .item() for debug output, not in computation
            std::cout << "Initial residual norm: " << beta << std::endl;
        }
        
        auto b_norm = b.norm();
        auto min_norm = torch::full_like(b_norm, 1e-12f);
        b_norm = torch::where(b_norm < min_norm, torch::ones_like(b_norm), b_norm);
        
        // Main GMRES iteration
        for (int outer = 0; outer < config.max_iter / config.restart; ++outer) {
            
            // Initialize Arnoldi process
            std::vector<StateVector> V;
            V.reserve(config.restart + 1);
            V.push_back(r * (1.0f / beta));
            
            // Hessenberg matrix and residual vector
            auto H = torch::zeros({config.restart + 1, config.restart}, x0.data().options());
            auto g = torch::zeros({config.restart + 1, 1}, x0.data().options());
            g.index_put_({0, 0}, beta);
            
            // Store Givens rotations
            std::vector<GivensRotationADSafe> rotations;
            
            // Arnoldi iteration
            int iter;
            for (iter = 0; iter < config.restart; ++iter) {
                
                // Compute w = A * v_iter
                StateVector w = A_times_v(V[iter]);
                if (preconditioner) {
                    w = preconditioner(w);
                }
                
                // Modified Gram-Schmidt
                for (int i = 0; i <= iter; ++i) {
                    auto h_ij = V[i].dot(w);
                    H.index_put_({i, iter}, h_ij);
                    w = w - h_ij * V[i];
                }
                
                auto h_norm = w.norm();
                H.index_put_({iter + 1, iter}, h_norm);
                
                // AD-SAFE: Check for breakdown without .item()
                auto breakdown = h_norm < torch::full_like(h_norm, 1e-12f);
                
                // Normalize and store new basis vector
                V.push_back(w * ADSafeMath::safe_reciprocal(h_norm));
                
                // Apply previous Givens rotations
                for (int i = 0; i < rotations.size(); ++i) {
                    rotations[i].apply(H, i, iter);
                }
                
                // Compute and apply new Givens rotation
                auto h_iter = H.index({iter, iter});
                auto h_iter_plus = H.index({iter + 1, iter});
                GivensRotationADSafe rot(h_iter, h_iter_plus);
                rotations.push_back(rot);
                rot.apply(H, iter, iter);
                rot.apply_to_vector(g, iter);
                
                // AD-SAFE: Check convergence without .item()
                auto residual = torch::abs(g.index({iter + 1, 0}));
                auto converged = residual < (config.tol * b_norm);
                
                if (config.verbose && (iter % 10 == 0)) {
                    std::cout << "  GMRES iter " << iter 
                              << ", residual: " << residual / b_norm << std::endl;
                }
                
                // AD-SAFE: Use tensor comparison for convergence
                // We'll check convergence after the loop
            }
            
            // Back substitution to solve H*y = g
            auto y = torch::zeros({iter, 1}, x0.data().options());
            for (int i = iter - 1; i >= 0; --i) {
                auto sum = g.index({i, 0});
                for (int j = i + 1; j < iter; ++j) {
                    sum = sum - H.index({i, j}) * y.index({j, 0});
                }
                y.index_put_({i, 0}, sum / H.index({i, i}));
            }
            
            // Update solution
            for (int i = 0; i < iter; ++i) {
                x = x + y.index({i, 0}) * V[i];
            }
            
            // Compute new residual
            r = b - A_times_v(x);
            if (preconditioner) {
                r = preconditioner(r);
            }
            
            beta = r.norm();
            
            // AD-SAFE: Check for overall convergence
            auto final_converged = beta < (tol_tensor * b_norm);
            
            if (config.verbose) {
                std::cout << "Outer iteration " << outer 
                          << ", residual norm: " << beta / b_norm << std::endl;
            }
            
            // Note: In AD-safe mode, we continue iterations even if converged
            // to maintain consistent computational graph structure
        }
        
        return x;
    }
    
    /**
     * AD-Safe back substitution for upper triangular system
     */
    static torch::Tensor back_substitution(const torch::Tensor& H, 
                                          const torch::Tensor& g,
                                          int n) {
        auto y = torch::zeros({n, 1}, H.options());
        
        for (int i = n - 1; i >= 0; --i) {
            auto sum = g.index({i, 0});
            for (int j = i + 1; j < n; ++j) {
                sum = sum - H.index({i, j}) * y.index({j, 0});
            }
            // AD-SAFE: Use safe division
            y.index_put_({i, 0}, ADSafeMath::safe_divide(sum, H.index({i, i})));
        }
        
        return y;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_GMRES_AD_SAFE_H