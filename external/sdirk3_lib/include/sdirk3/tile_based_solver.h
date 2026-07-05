#ifndef TILE_BASED_SOLVER_H
#define TILE_BASED_SOLVER_H

#include "torch_standalone.h"
#include "memory_layout_converter.h"
#include "cpu_optimization.h"  // FIX Round194: For CHECK_CPU_CONTIGUOUS_FLOAT32 macro
#include <mpi.h>

namespace wrf {
namespace sdirk3 {

// Tile-based SDIRK-3 solver that respects WRF's parallel structure
class TileBasedSDIRKSolver {
private:
    // WRF tile information
    struct TileInfo {
        int tile_id;
        int its, ite, jts, jte, kts, kte;  // Tile bounds
        int ims, ime, jms, jme, kms, kme;  // Memory bounds (includes halo)
        int ips, ipe, jps, jpe, kps, kpe;  // Patch bounds
        
        // Halo widths
        int halo_i, halo_j;
        
        // Check if point is in interior (not halo)
        bool is_interior(int i, int j) const {
            return (i >= its && i <= ite && j >= jts && j <= jte);
        }
    };
    
    TileInfo tile_info_;
    MPI_Comm comm_;
    int mpi_rank_, mpi_size_;
    
public:
    TileBasedSDIRKSolver(MPI_Comm comm = MPI_COMM_WORLD) : comm_(comm) {
        MPI_Comm_rank(comm_, &mpi_rank_);
        MPI_Comm_size(comm_, &mpi_size_);
    }
    
    void set_tile_info(int tile_id,
                      int its, int ite, int jts, int jte, int kts, int kte,
                      int ims, int ime, int jms, int jme, int kms, int kme,
                      int ips, int ipe, int jps, int jpe, int kps, int kpe) {
        tile_info_.tile_id = tile_id;
        tile_info_.its = its; tile_info_.ite = ite;
        tile_info_.jts = jts; tile_info_.jte = jte;
        tile_info_.kts = kts; tile_info_.kte = kte;
        tile_info_.ims = ims; tile_info_.ime = ime;
        tile_info_.jms = jms; tile_info_.jme = jme;
        tile_info_.kms = kms; tile_info_.kme = kme;
        tile_info_.ips = ips; tile_info_.ipe = ipe;
        tile_info_.jps = jps; tile_info_.jpe = jpe;
        tile_info_.kps = kps; tile_info_.kpe = kpe;
        
        // Calculate halo widths
        tile_info_.halo_i = its - ims;
        tile_info_.halo_j = jts - jms;
    }
    
    // Main SDIRK-3 stage solver with tile awareness
    torch::Tensor solve_sdirk_stage_tile(
        const torch::Tensor& U_n,           // Current state (full domain with halo)
        const std::vector<torch::Tensor>& k_prev,  // Previous stages
        double dt,
        double gamma,
        int stage,
        std::function<torch::Tensor(const torch::Tensor&)> rhs_eval) {
        
        // 1. Extract interior points only (no halo) for Newton solve
        torch::Tensor U_n_interior = extract_interior(U_n);
        std::vector<torch::Tensor> k_prev_interior;
        for (const auto& k : k_prev) {
            k_prev_interior.push_back(extract_interior(k));
        }
        
        // 2. Solve implicit equation on interior points only
        // R(δ) = δ - dt*γ*F(U_n + Σa_ij*k_j + γ*δ) = 0
        torch::Tensor delta = solve_newton_interior(
            U_n_interior, k_prev_interior, dt, gamma, stage, rhs_eval);
        
        // 3. Compute k_i = F(U_n + Σa_ij*k_j + γ*δ) with proper halo handling
        torch::Tensor k_i = compute_stage_tendency(
            U_n, k_prev, delta, dt, gamma, stage, rhs_eval);
        
        return k_i;
    }
    
private:
    // Extract interior points from tensor with halo
    torch::Tensor extract_interior(const torch::Tensor& tensor_with_halo) {
        // Assuming tensor shape: [nj_mem, nk, ni_mem, n_vars]
        int ni = tile_info_.ite - tile_info_.its + 1;
        int nj = tile_info_.jte - tile_info_.jts + 1;
        int nk = tile_info_.kte - tile_info_.kts + 1;
        int n_vars = tensor_with_halo.size(-1);
        
        // Extract interior region
        using namespace torch::indexing;
        return tensor_with_halo.index({
            Slice(tile_info_.jts - tile_info_.jms, tile_info_.jte - tile_info_.jms + 1),
            Slice(tile_info_.kts - tile_info_.kms, tile_info_.kte - tile_info_.kms + 1),
            Slice(tile_info_.its - tile_info_.ims, tile_info_.ite - tile_info_.ims + 1),
            Slice()
        }).contiguous();
    }
    
    // Embed interior solution back into full domain with halo
    torch::Tensor embed_interior(const torch::Tensor& interior, 
                                const torch::Tensor& full_template) {
        torch::Tensor result = full_template.clone();
        
        using namespace torch::indexing;
        result.index_put_({
            Slice(tile_info_.jts - tile_info_.jms, tile_info_.jte - tile_info_.jms + 1),
            Slice(tile_info_.kts - tile_info_.kms, tile_info_.kte - tile_info_.kms + 1),
            Slice(tile_info_.its - tile_info_.ims, tile_info_.ite - tile_info_.ims + 1),
            Slice()
        }, interior);
        
        return result;
    }
    
    // Newton solver for interior points only
    torch::Tensor solve_newton_interior(
        const torch::Tensor& U_n_interior,
        const std::vector<torch::Tensor>& k_prev_interior,
        double dt,
        double gamma,
        int stage,
        std::function<torch::Tensor(const torch::Tensor&)> rhs_eval) {
        
        // Initial guess
        torch::Tensor delta = torch::zeros_like(U_n_interior);
        
        const int max_newton_iter = 10;
        const double newton_tol = 1e-6;
        
        for (int iter = 0; iter < max_newton_iter; ++iter) {
            // Build stage state (interior only)
            torch::Tensor U_stage = U_n_interior.clone();
            
            // Add previous stages
            for (int j = 0; j < stage; ++j) {
                // Note: a_ij coefficients would come from SDIRK tableau
                double a_ij = get_sdirk_coefficient(stage, j);
                U_stage = U_stage + a_ij * k_prev_interior[j];
            }
            
            // Add current stage
            U_stage = U_stage + gamma * delta;
            
            // For RHS evaluation, we need full domain with updated halo
            // This is where spatial coupling happens
            torch::Tensor U_stage_full = prepare_for_rhs_eval(U_stage, U_n_interior);
            
            // Evaluate RHS (includes spatial derivatives)
            torch::Tensor F = rhs_eval(U_stage_full);
            
            // Extract interior F
            torch::Tensor F_interior = extract_interior(F);
            
            // Compute residual: R = δ - dt*γ*F
            torch::Tensor residual = delta - dt * gamma * F_interior;
            
            // Check convergence
            // FIX 2025-12-31 Batch41: NoGradGuard + to(kCPU) for diagnostic norm
            double residual_norm;
            {
                torch::NoGradGuard no_grad;
                residual_norm = torch::norm(residual).to(torch::kCPU).item<double>();
            }
            if (residual_norm < newton_tol) {
                break;
            }
            
            // Compute Jacobian-vector products for GMRES
            // J = I - dt*γ*∂F/∂U
            auto jvp_op = [&](const torch::Tensor& v) -> torch::Tensor {
                // Finite difference approximation
                const double eps = 1e-7;
                
                torch::Tensor U_stage_pert = U_stage + eps * v;
                torch::Tensor U_stage_pert_full = prepare_for_rhs_eval(U_stage_pert, U_n_interior);
                torch::Tensor F_pert = rhs_eval(U_stage_pert_full);
                torch::Tensor F_pert_interior = extract_interior(F_pert);
                
                torch::Tensor dF_dU_v = (F_pert_interior - F_interior) / eps;
                return v - dt * gamma * dF_dU_v;
            };
            
            // Solve linear system with GMRES
            torch::Tensor delta_update = gmres_solve(jvp_op, -residual);
            
            // Update delta
            delta = delta + delta_update;
        }
        
        return delta;
    }
    
    // Prepare state for RHS evaluation by handling halo exchange
    torch::Tensor prepare_for_rhs_eval(const torch::Tensor& U_interior,
                                      const torch::Tensor& U_n_full) {
        // This would interface with WRF's RSL_LITE for halo exchange
        // For now, simplified version
        torch::Tensor U_full = embed_interior(U_interior, U_n_full);
        
        // In real implementation, would call:
        // - RSL_LITE halo exchange routines
        // - Apply boundary conditions
        
        return U_full;
    }
    
    // Compute final stage tendency with proper spatial handling
    torch::Tensor compute_stage_tendency(
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const torch::Tensor& delta_interior,
        double dt,
        double gamma,
        int stage,
        std::function<torch::Tensor(const torch::Tensor&)> rhs_eval) {
        
        // Build full stage state
        torch::Tensor U_stage = U_n.clone();
        
        for (int j = 0; j < stage; ++j) {
            double a_ij = get_sdirk_coefficient(stage, j);
            U_stage = U_stage + a_ij * k_prev[j];
        }
        
        // Add converged delta (interior only)
        torch::Tensor delta_full = embed_interior(delta_interior, U_n);
        U_stage = U_stage + gamma * delta_full;
        
        // Evaluate RHS with full spatial coupling
        return rhs_eval(U_stage);
    }
    
    // Simplified GMRES solver
    torch::Tensor gmres_solve(
        std::function<torch::Tensor(const torch::Tensor&)> A_op,
        const torch::Tensor& b,
        int restart = 30,
        double tol = 1e-7) {
        
        // Simplified GMRES implementation
        // In practice, would use optimized version
        torch::Tensor x = torch::zeros_like(b);
        torch::Tensor r = b - A_op(x);

        // FIX 2025-12-31 Batch41: NoGradGuard + to(kCPU) for diagnostic norms
        double b_norm, r_norm;
        {
            torch::NoGradGuard no_grad;
            b_norm = torch::norm(b).to(torch::kCPU).item<double>();
            r_norm = torch::norm(r).to(torch::kCPU).item<double>();
        }

        if (r_norm < tol * b_norm) {
            return x;
        }
        
        // Arnoldi process and solution update...
        // (simplified for brevity)
        
        return x;
    }
    
    // Get SDIRK-3 coefficient
    double get_sdirk_coefficient(int i, int j) {
        // SDIRK-3 tableau
        const double gamma = 0.43586652150845906;
        
        if (i == 0 && j == 0) return gamma;
        if (i == 1 && j == 0) return 0.5 - gamma;
        if (i == 1 && j == 1) return gamma;
        if (i == 2 && j == 0) return 2.0 * gamma;
        if (i == 2 && j == 1) return 1.0 - 4.0 * gamma;
        if (i == 2 && j == 2) return gamma;
        
        return 0.0;
    }
};

// MPI gradient synchronization for distributed tiles
class MPIGradientSync {
private:
    MPI_Comm comm_;
    int rank_, size_;
    
public:
    MPIGradientSync(MPI_Comm comm = MPI_COMM_WORLD) : comm_(comm) {
        MPI_Comm_rank(comm_, &rank_);
        MPI_Comm_size(comm_, &size_);
    }
    
    // Synchronize gradients across all tiles after backward pass
    void sync_gradients(torch::Tensor& grad_tensor) {
        if (!grad_tensor.requires_grad() || !grad_tensor.grad().defined()) {
            return;
        }
        
        torch::Tensor grad = grad_tensor.grad();
        // FIX Round194: Use Float32-aware check for data_ptr<float>() safety
        CHECK_CPU_CONTIGUOUS_FLOAT32(grad);

        float* grad_data = grad.data_ptr<float>();
        int count = grad.numel();
        
        // All-reduce gradients across MPI ranks
        MPI_Allreduce(MPI_IN_PLACE, grad_data, count, MPI_FLOAT, MPI_SUM, comm_);
        
        // Average by number of ranks
        grad = grad / float(size_);
    }
    
    // Synchronize multiple gradient tensors
    void sync_gradient_list(std::vector<torch::Tensor>& grad_tensors) {
        for (auto& tensor : grad_tensors) {
            sync_gradients(tensor);
        }
    }
    
    // Halo exchange for gradient tensors (interfaces with RSL_LITE)
    void exchange_gradient_halos(torch::Tensor& grad_tensor,
                                int halo_width_i, int halo_width_j) {
        // This would interface with WRF's RSL_LITE
        // For now, placeholder implementation
        
        // In actual implementation:
        // 1. Pack halo data
        // 2. Call RSL_LITE communication routines
        // 3. Unpack received halo data
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // TILE_BASED_SOLVER_H