#ifndef WRF_ACOUSTIC_PRECONDITIONER_H
#define WRF_ACOUSTIC_PRECONDITIONER_H

#include <torch/torch.h>
#include "wrf_memory_layout_adapter.h"
#include "newton_krylov_solver.h"
#include <memory>
#include <vector>

namespace wrf {
namespace sdirk3 {

/**
 * WRF Acoustic-Gravity Wave Preconditioner
 * 
 * Handles the strong coupling of fast acoustic and gravity waves
 * in the (W, φ, μ_d) system for efficient Krylov convergence.
 * 
 * The preconditioner approximates: M ≈ [I - γΔt·J_acoustic]
 * where J_acoustic includes only the fast wave terms.
 * 
 * Strategy:
 * 1. Vertical implicit solve (tridiagonal) for each column
 * 2. Horizontal smoothing for divergence damping
 * 3. Schur complement for (U,V) coupling
 */
class WRFAcousticPreconditioner : public AcousticPreconditioner {
private:
    // Grid information
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<WRFMemoryLayoutAdapter> layout_adapter_;
    
    // Time step parameters
    double dt_gamma_;  // γ * Δt for SDIRK3
    
    // Acoustic wave parameters
    float cs_;        // Sound speed
    float cs2_;       // Sound speed squared
    
    // Vertical coupling matrices (per column)
    struct VerticalCoupling {
        torch::Tensor L_ww;      // W-W coupling (tridiagonal)
        torch::Tensor L_wphi;    // W-φ coupling
        torch::Tensor L_phiw;    // φ-W coupling  
        torch::Tensor L_phiphi;  // φ-φ coupling (tridiagonal)
        torch::Tensor L_muw;     // μ-W coupling
        torch::Tensor L_mumu;    // μ-μ coupling
        
        // LU factorization for tridiagonal solve
        torch::Tensor L_diag;    // Diagonal
        torch::Tensor L_upper;   // Upper diagonal
        torch::Tensor L_lower;   // Lower diagonal
        torch::Tensor pivot;     // Pivot indices
    };
    
    // Store vertical coupling for each (i,j) column
    std::vector<std::vector<VerticalCoupling>> vertical_systems_;
    
    // Horizontal smoothing operator
    torch::Tensor horizontal_smoother_;
    
    // Divergence damping coefficient
    float kdamp_;
    
    // Multigrid hierarchy for horizontal elliptic solver
    struct MultigridLevel {
        int nx, ny;
        torch::Tensor restriction;   // Fine to coarse
        torch::Tensor prolongation;  // Coarse to fine
        torch::Tensor smoother;      // Jacobi/Gauss-Seidel weights
    };
    std::vector<MultigridLevel> mg_levels_;
    
public:
    WRFAcousticPreconditioner(
        std::shared_ptr<WRFGridInfo> grid_info,
        std::shared_ptr<WRFMemoryLayoutAdapter> layout_adapter)
        : AcousticPreconditioner(grid_info),
          grid_info_(grid_info),
          layout_adapter_(layout_adapter),
          cs_(340.0f),  // Default sound speed
          cs2_(cs_ * cs_),
          kdamp_(0.01f) {}  // Default divergence damping
    
    /**
     * Setup preconditioner for given time step
     */
    void setup(double dt_gamma) override {
        dt_gamma_ = dt_gamma;
        
        auto dims = layout_adapter_->grid_dims();
        int ni = dims.ni();
        int nj = dims.nj();
        int nk = dims.nk();
        
        // Initialize vertical coupling matrices for each column
        vertical_systems_.resize(nj);
        for (int j = 0; j < nj; j++) {
            vertical_systems_[j].resize(ni);
            for (int i = 0; i < ni; i++) {
                setup_vertical_coupling(i, j, nk);
            }
        }
        
        // Setup horizontal smoother (2D elliptic operator)
        setup_horizontal_smoother(ni, nj);
        
        // Build multigrid hierarchy
        setup_multigrid_hierarchy(ni, nj);
    }
    
    /**
     * Apply preconditioner: z = M^{-1} * r
     * where M approximates acoustic-gravity wave coupling
     */
    torch::Tensor apply(const torch::Tensor& residual) override {
        auto dims = layout_adapter_->grid_dims();
        int ni = dims.ni();
        int nj = dims.nj();
        int nk = dims.nk();
        
        // Extract components from residual vector
        // With (j,k,i) layout - shapes: W(nj,nk+1,ni), φ(nj,nk,ni), μ(nj,ni)
        auto r_w = extract_w_component(residual, nk, nj, ni);
        auto r_phi = extract_phi_component(residual, nk, nj, ni);
        auto r_mu = extract_mu_component(residual, nj, ni, nk);
        
        // Step 1: Vertical implicit solve for each column
        auto [z_w, z_phi, z_mu] = solve_vertical_coupling(r_w, r_phi, r_mu);
        
        // Step 2: Horizontal smoothing (multigrid V-cycle)
        apply_horizontal_smoothing(z_w, z_phi, z_mu);
        
        // Step 3: Apply Schur complement for (U,V) coupling
        auto z_uv = apply_schur_complement(residual, z_w, z_phi);
        
        // Reassemble solution vector
        return assemble_solution(z_uv, z_w, z_phi, z_mu);
    }
    
private:
    /**
     * Setup vertical coupling matrix for column (i,j)
     * Handles hydrostatic constraint: ∂φ/∂η = -α_d * μ_d
     */
    void setup_vertical_coupling(int i, int j, int nk) {
        auto& vc = vertical_systems_[j][i];
        
        // Initialize tridiagonal matrices
        vc.L_ww = torch::zeros({nk+1, 3}, torch::kFloat32);      // W staggered
        vc.L_phiphi = torch::zeros({nk, 3}, torch::kFloat32);
        
        // Vertical grid coefficients
        auto rdnw = grid_info_->dnw.reciprocal();  // 1/Δη
        auto c1h = grid_info_->c1h;
        auto c2h = grid_info_->c2h;

        // FIX 2025-12-31 Batch41: CPU contiguous + data_ptr to avoid k-loop sync
        auto rdnw_cpu = rdnw.to(torch::kCPU, torch::kFloat32).contiguous();
        const float* rdnw_ptr = rdnw_cpu.data_ptr<float>();

        // Build vertical acoustic operator
        // Simplified form: ∂²W/∂z² - (1/cs²)∂²φ/∂t² = 0
        for (int k = 1; k < nk; k++) {
            float dz = 1.0f / rdnw_ptr[k];
            float dz2 = dz * dz;
            
            // W equation: vertical momentum
            // d²W/dz² term (tridiagonal)
            vc.L_ww.index_put_({k, 0}, -dt_gamma_ * dt_gamma_ / dz2);  // Lower
            vc.L_ww.index_put_({k, 1}, 1.0f + 2.0f * dt_gamma_ * dt_gamma_ / dz2);  // Diagonal
            vc.L_ww.index_put_({k, 2}, -dt_gamma_ * dt_gamma_ / dz2);  // Upper
            
            // φ equation: hydrostatic balance
            // ∂φ/∂η = -α_d * μ_d coupling
            vc.L_phiphi.index_put_({k, 0}, -dt_gamma_ / dz);  // Lower
            vc.L_phiphi.index_put_({k, 1}, 1.0f);             // Diagonal
            vc.L_phiphi.index_put_({k, 2}, dt_gamma_ / dz);   // Upper
        }
        
        // Boundary conditions
        // Bottom: W = 0 (no-flux)
        vc.L_ww.index_put_({0, 1}, 1.0f);
        vc.L_ww.index_put_({0, 2}, 0.0f);
        
        // Top: Rigid lid or absorbing layer
        vc.L_ww.index_put_({nk, 0}, 0.0f);
        vc.L_ww.index_put_({nk, 1}, 1.0f);
        
        // Factorize for efficient solve
        factorize_tridiagonal(vc);
    }
    
    /**
     * Factorize tridiagonal system using Thomas algorithm
     */
    void factorize_tridiagonal(VerticalCoupling& vc) {
        int n = vc.L_ww.size(0);
        
        vc.L_diag = torch::zeros(n, torch::kFloat32);
        vc.L_upper = torch::zeros(n-1, torch::kFloat32);
        vc.L_lower = torch::zeros(n-1, torch::kFloat32);
        
        // Forward elimination
        vc.L_diag[0] = vc.L_ww.index({0, 1});
        
        for (int i = 1; i < n; i++) {
            vc.L_lower[i-1] = vc.L_ww.index({i, 0});
            float m = vc.L_lower[i-1] / vc.L_diag[i-1];
            vc.L_diag[i] = vc.L_ww.index({i, 1}) - m * vc.L_ww.index({i-1, 2});
            if (i < n-1) {
                vc.L_upper[i-1] = vc.L_ww.index({i-1, 2});
            }
        }
    }
    
    /**
     * Solve vertical coupling system for all columns
     */
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> 
    solve_vertical_coupling(const torch::Tensor& r_w,
                          const torch::Tensor& r_phi,
                          const torch::Tensor& r_mu) {
        
        auto dims = layout_adapter_->grid_dims();
        int ni = dims.ni();
        int nj = dims.nj();
        int nk = dims.nk();
        
        auto z_w = torch::zeros_like(r_w);
        auto z_phi = torch::zeros_like(r_phi);
        auto z_mu = torch::zeros_like(r_mu);
        
        // Parallel solve for each column
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                // Extract column residuals with (j,k,i) layout
                // More efficient: r_w[j] gives (nk+1,ni), then select column i
                auto r_w_col = r_w[j].select(1, i);   // Shape: (nk+1,)
                auto r_phi_col = r_phi[j].select(1, i); // Shape: (nk,)
                
                // Solve tridiagonal system using Thomas algorithm
                auto z_w_col = solve_tridiagonal_thomas(
                    vertical_systems_[j][i], r_w_col
                );
                
                // Apply hydrostatic constraint for φ
                auto z_phi_col = apply_hydrostatic_constraint(
                    z_w_col, r_phi_col, r_mu.index({j, i})
                );
                
                // Store solution with (j,k,i) layout
                for (int k = 0; k < nk+1; k++) {
                    if (k < nk+1) z_w[j].index_put_({k, i}, z_w_col[k]);
                    if (k < nk) z_phi[j].index_put_({k, i}, z_phi_col[k]);
                }
                
                // Surface pressure tendency
                z_mu.index_put_({j, i}, 
                               -dt_gamma_ * cs2_ * divergence_at_surface(z_w_col));
            }
        }
        
        return {z_w, z_phi, z_mu};
    }
    
    /**
     * Thomas algorithm for tridiagonal solve
     */
    torch::Tensor solve_tridiagonal_thomas(const VerticalCoupling& vc,
                                          const torch::Tensor& rhs) {
        int n = rhs.size(0);
        auto solution = torch::zeros(n, rhs.options());
        auto y = torch::zeros(n, rhs.options());
        
        // Forward substitution
        y[0] = rhs[0] / vc.L_diag[0];
        for (int i = 1; i < n; i++) {
            y[i] = (rhs[i] - vc.L_lower[i-1] * y[i-1]) / vc.L_diag[i];
        }
        
        // Back substitution
        solution[n-1] = y[n-1];
        for (int i = n-2; i >= 0; i--) {
            solution[i] = y[i] - (vc.L_upper[i] / vc.L_diag[i]) * solution[i+1];
        }
        
        return solution;
    }
    
    /**
     * Apply hydrostatic constraint: ∂φ/∂η = -α_d * μ_d
     */
    torch::Tensor apply_hydrostatic_constraint(const torch::Tensor& z_w,
                                              const torch::Tensor& r_phi,
                                              const torch::Tensor& mu_value) {
        int nk = r_phi.size(0);
        auto z_phi = torch::zeros_like(r_phi);
        
        // Use hydrostatic relation to couple φ with W and μ_d
        auto alpha_d = 1.0f / grid_info_->rho_base;  // Simplified
        auto rdnw = grid_info_->dnw.reciprocal();

        // FIX 2025-12-31 Batch41: CPU contiguous + data_ptr to avoid k-loop sync
        auto rdnw_cpu = rdnw.to(torch::kCPU, torch::kFloat32).contiguous();
        const float* rdnw_ptr = rdnw_cpu.data_ptr<float>();

        // Bottom boundary from r_phi
        z_phi[0] = r_phi[0];

        // Integrate upward using hydrostatic constraint
        for (int k = 1; k < nk; k++) {
            float dz = 1.0f / rdnw_ptr[k-1];
            z_phi[k] = z_phi[k-1] - dt_gamma_ * alpha_d[k-1] * mu_value * dz;
        }
        
        return z_phi;
    }
    
    /**
     * Setup horizontal smoothing operator
     */
    void setup_horizontal_smoother(int ni, int nj) {
        // 2D Laplacian for divergence damping
        // ∇²ψ = ∂²ψ/∂x² + ∂²ψ/∂y²
        
        horizontal_smoother_ = torch::zeros({nj, ni, 5}, torch::kFloat32);
        
        float dx2 = grid_info_->dx * grid_info_->dx;
        float dy2 = grid_info_->dy * grid_info_->dy;
        
        // 5-point stencil coefficients
        float cx = kdamp_ * dt_gamma_ * dt_gamma_ / dx2;
        float cy = kdamp_ * dt_gamma_ * dt_gamma_ / dy2;
        float cc = 1.0f + 2.0f * (cx + cy);
        
        // Fill stencil (order: center, east, west, north, south)
        horizontal_smoother_.index_put_({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), 0}, cc);   // Center
        horizontal_smoother_.index_put_({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), 1}, -cx);  // East
        horizontal_smoother_.index_put_({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), 2}, -cx);  // West
        horizontal_smoother_.index_put_({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), 3}, -cy);  // North
        horizontal_smoother_.index_put_({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), 4}, -cy);  // South
    }
    
    /**
     * Apply horizontal smoothing using multigrid V-cycle
     */
    void apply_horizontal_smoothing(torch::Tensor& z_w,
                                   torch::Tensor& z_phi,
                                   torch::Tensor& z_mu) {
        
        // Apply divergence damping to μ_d (surface pressure)
        // This couples with W through continuity equation
        
        int nsmooth = 2;  // Number of smoothing iterations
        
        for (int iter = 0; iter < nsmooth; iter++) {
            // Compute divergence of W with (j,k,i) layout
            // Note: for W-only divergence, we pass dummy U,V components
            auto dummy_u = torch::zeros({nj, nk, ni+1}, z_w.options());
            auto dummy_v = torch::zeros({nj+1, nk, ni}, z_w.options());
            
            auto div_w = CGridStaggering::compute_divergence(
                dummy_u,  // U component (zero for vertical-only)
                dummy_v,  // V component (zero for vertical-only)
                z_w,      // W component with shape (nj, nk+1, ni)
                grid_info_->dx, grid_info_->dy, grid_info_->dnw
            );
            
            // Apply smoothing to surface pressure
            z_mu = apply_2d_smoother(z_mu, horizontal_smoother_);
            
            // Update W based on smoothed pressure gradient
            update_w_from_pressure(z_w, z_mu);
        }
    }
    
    /**
     * Apply 2D smoother (Jacobi iteration)
     */
    torch::Tensor apply_2d_smoother(const torch::Tensor& field,
                                    const torch::Tensor& stencil) {
        auto result = torch::zeros_like(field);
        int nj = field.size(0);
        int ni = field.size(1);
        
        for (int j = 1; j < nj-1; j++) {
            for (int i = 1; i < ni-1; i++) {
                float sum = stencil.index({j, i, 0}) * field.index({j, i});
                sum += stencil.index({j, i, 1}) * field.index({j, i+1});
                sum += stencil.index({j, i, 2}) * field.index({j, i-1});
                sum += stencil.index({j, i, 3}) * field.index({j+1, i});
                sum += stencil.index({j, i, 4}) * field.index({j-1, i});
                
                result.index_put_({j, i}, sum / stencil.index({j, i, 0}));
            }
        }
        
        return result;
    }
    
    /**
     * Setup multigrid hierarchy for efficient horizontal solve
     */
    void setup_multigrid_hierarchy(int ni, int nj) {
        mg_levels_.clear();
        
        // Create coarse levels by factor of 2
        int level_ni = ni;
        int level_nj = nj;
        
        while (level_ni > 4 && level_nj > 4) {
            MultigridLevel level;
            level.nx = level_ni;
            level.ny = level_nj;
            
            // Setup restriction (fine to coarse)
            // Simple injection for now
            level.restriction = torch::ones({level_nj/2, level_ni/2, 4}, 
                                           torch::kFloat32) * 0.25f;
            
            // Setup prolongation (coarse to fine)
            // Bilinear interpolation
            level.prolongation = torch::ones({level_nj, level_ni, 4}, 
                                            torch::kFloat32);
            
            mg_levels_.push_back(level);
            
            level_ni /= 2;
            level_nj /= 2;
        }
    }
    
    // ... Additional helper methods ...
    
    torch::Tensor extract_w_component(const torch::Tensor& residual, 
                                      int nk, int nj, int ni) {
        // Extract W component from packed residual vector
        // With (j,k,i) layout: W shape is (nj, nk+1, ni)
        return residual.narrow(0, 2 * nj * nk * ni, nj * (nk+1) * ni)
                      .reshape({nj, nk+1, ni});
    }
    
    torch::Tensor extract_phi_component(const torch::Tensor& residual,
                                        int nk, int nj, int ni) {
        // Extract φ component
        // With (j,k,i) layout: φ shape is (nj, nk, ni)
        return residual.narrow(0, 4 * nj * nk * ni, nj * nk * ni)
                      .reshape({nj, nk, ni});
    }
    
    torch::Tensor extract_mu_component(const torch::Tensor& residual,
                                       int nj, int ni, int nk) {
        // Extract μ_d component
        // With (j,k,i) layout: μ_d shape is (nj, ni)
        return residual.narrow(0, 5 * nj * nk * ni, nj * ni)
                      .reshape({nj, ni});
    }
    
    void update_w_from_pressure(torch::Tensor& z_w,
                                const torch::Tensor& z_mu) {
        // Update W based on pressure gradient
        // Simplified implementation
        auto grad_mu = torch::zeros_like(z_w);
        
        // Compute pressure gradient and update W
        // ... implementation ...
    }
    
    float divergence_at_surface(const torch::Tensor& w_col) {
        // FIX 2025-12-31 Batch41: CPU fast-path + GPU batch transfer
        torch::NoGradGuard no_grad;

        // CPU fast-path: direct data_ptr access (zero sync overhead)
        // FIX Round194: Added dtype checks for Float32 to match data_ptr<float>()
        if (w_col.is_cpu() && w_col.is_contiguous() &&
            w_col.scalar_type() == torch::kFloat32 &&
            grid_info_->dnw.is_cpu() && grid_info_->dnw.is_contiguous() &&
            grid_info_->dnw.scalar_type() == torch::kFloat32) {
            const float* w_ptr = w_col.data_ptr<float>();
            const float* dnw_ptr = grid_info_->dnw.data_ptr<float>();
            return w_ptr[0] / dnw_ptr[0];
        }

        // GPU, non-contiguous, or non-Float32: batch transfer with conversion
        // Use kFloat32 to ensure type safety for data_ptr<float>()
        auto vals = torch::stack({w_col.index({0}), grid_info_->dnw.index({0})})
                        .to(torch::kCPU, torch::kFloat32).contiguous();
        const float* vals_ptr = vals.data_ptr<float>();
        return vals_ptr[0] / vals_ptr[1];
    }
    
    torch::Tensor apply_schur_complement(const torch::Tensor& residual,
                                         const torch::Tensor& z_w,
                                         const torch::Tensor& z_phi) {
        // Apply Schur complement for (U,V) coupling
        // Simplified implementation
        return torch::zeros_like(residual);
    }
    
    torch::Tensor assemble_solution(const torch::Tensor& z_uv,
                                   const torch::Tensor& z_w,
                                   const torch::Tensor& z_phi,
                                   const torch::Tensor& z_mu) {
        // Reassemble solution vector from components
        // Implementation depends on variable ordering
        return torch::cat({z_uv.flatten(), z_w.flatten(), 
                          z_phi.flatten(), z_mu.flatten()}, 0);
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_ACOUSTIC_PRECONDITIONER_H