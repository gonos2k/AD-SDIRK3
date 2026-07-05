#ifndef WRF_SDIRK3_PRECONDITIONER_ADVANCED_H
#define WRF_SDIRK3_PRECONDITIONER_ADVANCED_H

#include <torch/torch.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include "wrf_memory_layout.h"

namespace wrf {
namespace sdirk3 {

/**
 * Advanced Preconditioner System for SDIRK3
 * 
 * Implements sophisticated preconditioning strategies for the
 * Newton-Krylov solver in atmospheric modeling
 */

// =============================================================================
// Base Preconditioner Interface
// =============================================================================

class AdvancedPreconditioner {
public:
    virtual ~AdvancedPreconditioner() = default;
    
    // Apply preconditioner: solve M*z = r
    virtual torch::Tensor apply(const torch::Tensor& r) = 0;
    
    // Update preconditioner based on current state
    virtual void update(const torch::Tensor& state, float dt, float gamma) = 0;
    
    // Get condition number estimate
    virtual float get_condition_number() const { return -1.0f; }
    
    // Get memory usage in bytes
    virtual size_t get_memory_usage() const = 0;
    
    // Get preconditioner statistics
    struct PreconditionerStats {
        int update_count = 0;
        int apply_count = 0;
        float avg_apply_time = 0.0f;
        float avg_update_time = 0.0f;
        float effectiveness = 1.0f;  // Reduction in iterations
    };
    
    virtual PreconditionerStats get_stats() const { return stats_; }
    
protected:
    PreconditionerStats stats_;
};

// =============================================================================
// Physics-Based Block Preconditioner
// =============================================================================

class PhysicsBlockPreconditioner : public AdvancedPreconditioner {
private:
    struct BlockStructure {
        // Variable indices
        static constexpr int RHO_BLOCK = 0;
        static constexpr int VEL_BLOCK = 1;  // u, v, w
        static constexpr int THETA_BLOCK = 2;
        static constexpr int PRESSURE_BLOCK = 3;
        
        // Block sizes
        int n_rho = 1;
        int n_vel = 3;
        int n_theta = 1;
        int n_pressure = 2;  // phi, mu
        
        // Block offsets
        int offset_rho = 0;
        int offset_vel = 1;
        int offset_theta = 4;
        int offset_pressure = 5;
    } block_structure_;
    
    // Grid information
    int nx_, ny_, nz_;
    float dx_, dy_;
    torch::Tensor dz_;
    
    // Physical parameters
    float c_sound_ = 340.0f;
    float g_ = 9.81f;
    float cp_ = 1004.0f;
    float cv_ = 717.0f;
    float rd_ = 287.0f;
    
    // Preconditioner blocks
    torch::Tensor acoustic_block_inv_;
    torch::Tensor advection_block_inv_;
    torch::Tensor buoyancy_block_inv_;
    torch::Tensor diffusion_block_inv_;
    
    // Timestep parameters
    float dt_;
    float gamma_;
    
    // Physical parameters
    float theta_ref_ = 300.0f;       // Reference potential temperature (K)
    float pressure_ref_ = 100000.0f; // Reference pressure (Pa)
    float rho_ref_ = 1.225f;         // Reference density (kg/m³)
    float sound_speed_sq_ = 340.0f * 340.0f; // Sound speed squared (m²/s²)
    float characteristic_height_ = 10000.0f;  // Characteristic height (m)
    
public:
    // Approximation level
    enum ApproximationLevel {
        DIAGONAL,           // Diagonal approximation
        BLOCK_DIAGONAL,     // Block diagonal
        BLOCK_TRIDIAGONAL,  // Tridiagonal within blocks
        FULL_PHYSICS        // Full physics coupling
    };
    
private:
    ApproximationLevel approx_level_ = BLOCK_DIAGONAL;
    
public:
    PhysicsBlockPreconditioner(int nx, int ny, int nz, 
                              float dx, float dy, const torch::Tensor& dz,
                              ApproximationLevel level = BLOCK_DIAGONAL)
        : nx_(nx), ny_(ny), nz_(nz), dx_(dx), dy_(dy), dz_(dz),
          approx_level_(level) {
        initialize_blocks();
    }
    
    torch::Tensor apply(const torch::Tensor& r) override {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        torch::Tensor z = torch::zeros_like(r);
        
        // Extract variable blocks
        auto r_rho = extract_block(r, block_structure_.offset_rho, block_structure_.n_rho);
        auto r_vel = extract_block(r, block_structure_.offset_vel, block_structure_.n_vel);
        auto r_theta = extract_block(r, block_structure_.offset_theta, block_structure_.n_theta);
        auto r_pressure = extract_block(r, block_structure_.offset_pressure, block_structure_.n_pressure);
        
        // Apply physics-based preconditioning
        switch (approx_level_) {
            case DIAGONAL:
                z = apply_diagonal_preconditioner(r);
                break;
                
            case BLOCK_DIAGONAL:
                z = apply_block_diagonal_preconditioner(r_rho, r_vel, r_theta, r_pressure);
                break;
                
            case BLOCK_TRIDIAGONAL:
                z = apply_block_tridiagonal_preconditioner(r_rho, r_vel, r_theta, r_pressure);
                break;
                
            case FULL_PHYSICS:
                z = apply_full_physics_preconditioner(r_rho, r_vel, r_theta, r_pressure);
                break;
        }
        
        // Update statistics
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.apply_count++;
        stats_.avg_apply_time = (stats_.avg_apply_time * (stats_.apply_count - 1) + 
                                duration.count() / 1000.0f) / stats_.apply_count;
        
        return z;
    }
    
    void update(const torch::Tensor& state, float dt, float gamma) override {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        dt_ = dt;
        gamma_ = gamma;
        
        // Update acoustic block (most important for stiff acoustic modes)
        update_acoustic_block(state);
        
        // Update advection block
        update_advection_block(state);
        
        // Update buoyancy block
        update_buoyancy_block(state);
        
        // Update diffusion block if needed
        if (approx_level_ >= BLOCK_TRIDIAGONAL) {
            update_diffusion_block(state);
        }
        
        // Update statistics
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.update_count++;
        stats_.avg_update_time = (stats_.avg_update_time * (stats_.update_count - 1) + 
                                 duration.count() / 1000.0f) / stats_.update_count;
    }
    
private:
    void initialize_blocks() {
        // FIX Round194: Explicit CPU device for preconditioner tensors
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);

        // Initialize diagonal blocks
        acoustic_block_inv_ = torch::ones({nz_, ny_, nx_}, options);
        advection_block_inv_ = torch::ones({3, nz_, ny_, nx_}, options);
        buoyancy_block_inv_ = torch::ones({nz_, ny_, nx_}, options);
        diffusion_block_inv_ = torch::ones({nz_, ny_, nx_}, options);
    }
    
    // Coupling solver functions
    torch::Tensor solve_velocity_with_coupling(const torch::Tensor& r_vel, 
                                              const torch::Tensor& pressure, 
                                              const torch::Tensor& theta);
    torch::Tensor solve_pressure_with_coupling(const torch::Tensor& r_pressure, 
                                               const torch::Tensor& velocity);
    torch::Tensor solve_temperature_with_coupling(const torch::Tensor& r_theta, 
                                                  const torch::Tensor& velocity);
    torch::Tensor solve_density_with_coupling(const torch::Tensor& r_rho, 
                                             const torch::Tensor& pressure, 
                                             const torch::Tensor& theta);
    
    // Helper functions
    torch::Tensor compute_buoyancy_force(const torch::Tensor& theta);
    torch::Tensor compute_velocity_divergence(const torch::Tensor& velocity);
    
    // Block preconditioner application functions
    torch::Tensor apply_block_tridiagonal_preconditioner(
        const torch::Tensor& r_rho,
        const torch::Tensor& r_vel,
        const torch::Tensor& r_theta,
        const torch::Tensor& r_pressure);
    
    torch::Tensor apply_full_physics_preconditioner(
        const torch::Tensor& r_rho,
        const torch::Tensor& r_vel,
        const torch::Tensor& r_theta,
        const torch::Tensor& r_pressure);
    
    // Update functions for different physics blocks
    void update_acoustic_block(const torch::Tensor& state);
    void update_advection_block(const torch::Tensor& state);
    void update_buoyancy_block(const torch::Tensor& state);
    void update_diffusion_block(const torch::Tensor& state) {
        // Simplified diffusion block update
        diffusion_block_inv_ = torch::ones_like(acoustic_block_inv_) * 0.1f;
    }
    
    // Vertical solver for acoustic modes
    std::pair<torch::Tensor, torch::Tensor> solve_vertical_acoustic_system(
        const torch::Tensor& r_vel_col,
        const torch::Tensor& r_pressure_col,
        int i, int j);
    
    // Utility functions
    torch::Tensor extract_vertical_column(const torch::Tensor& field, int i, int j);
    void set_vertical_column(torch::Tensor& field, const torch::Tensor& column, int i, int j);
    torch::Tensor combine_blocks(const torch::Tensor& rho,
                                const torch::Tensor& vel,
                                const torch::Tensor& theta,
                                const torch::Tensor& pressure);
    torch::Tensor solve_velocity_block(const torch::Tensor& r_vel, 
                                      const torch::Tensor& r_pressure);
    torch::Tensor compute_pressure_gradient(const torch::Tensor& pressure);
    torch::Tensor apply_buoyancy_preconditioner(const torch::Tensor& r_theta,
                                               const torch::Tensor& r_vel);
    torch::Tensor gradient_z(const torch::Tensor& field);
    
    torch::Tensor extract_block(const torch::Tensor& vec, int offset, int size) {
        return vec.narrow(1, offset, size);
    }
    
    torch::Tensor apply_diagonal_preconditioner(const torch::Tensor& r) {
        // Simple diagonal scaling
        return r / (1.0f + dt_ * gamma_ * acoustic_block_inv_);
    }
    
    torch::Tensor apply_block_diagonal_preconditioner(
        const torch::Tensor& r_rho,
        const torch::Tensor& r_vel,
        const torch::Tensor& r_theta,
        const torch::Tensor& r_pressure) {
        
        torch::Tensor z = torch::zeros_like(r_rho);
        
        // Acoustic mode preconditioner (couples pressure and velocity)
        // Approximate (I - dt*gamma*J_acoustic)^{-1}
        auto z_pressure = solve_acoustic_block(r_pressure, r_vel);
        auto z_vel = solve_velocity_block(r_vel, r_pressure);
        
        // Buoyancy preconditioner
        auto z_theta = r_theta / (1.0f + dt_ * gamma_ * buoyancy_block_inv_);
        
        // Density update
        auto z_rho = r_rho / (1.0f + dt_ * gamma_ * acoustic_block_inv_);
        
        // Combine results
        return combine_blocks(z_rho, z_vel, z_theta, z_pressure);
    }
    
    torch::Tensor solve_acoustic_block(const torch::Tensor& r_pressure, 
                                      const torch::Tensor& r_vel) {
        // Acoustic system:
        // [I + dt*gamma*c²∇²    -dt*gamma*∇·] [p]   [r_p]
        // [-dt*gamma*∇         I           ] [v] = [r_v]
        
        float c2_dt_gamma = c_sound_ * c_sound_ * dt_ * gamma_;
        
        // Approximate with Helmholtz operator
        auto laplacian = compute_laplacian_operator();
        auto helmholtz = 1.0f + c2_dt_gamma * laplacian;
        
        // Solve using FFT or iterative method
        return r_pressure / helmholtz;
    }
    
    torch::Tensor compute_laplacian_operator() {
        // Discrete Laplacian eigenvalues
        auto kx = torch::arange(nx_, torch::kFloat32) * 2.0f * M_PI / nx_;
        auto ky = torch::arange(ny_, torch::kFloat32) * 2.0f * M_PI / ny_;
        auto kz = torch::arange(nz_, torch::kFloat32) * 2.0f * M_PI / nz_;
        
        auto kx2 = torch::pow(torch::sin(kx / 2.0f) * 2.0f / dx_, 2);
        auto ky2 = torch::pow(torch::sin(ky / 2.0f) * 2.0f / dy_, 2);
        auto kz2 = torch::pow(torch::sin(kz / 2.0f) * 2.0f / torch::mean(dz_), 2);
        
        // 3D Laplacian
        auto laplacian = -(kx2.unsqueeze(0).unsqueeze(0) + 
                          ky2.unsqueeze(0).unsqueeze(-1) + 
                          kz2.unsqueeze(-1).unsqueeze(-1));
        
        return laplacian;
    }
    
    size_t get_memory_usage() const override {
        size_t total = 0;
        total += acoustic_block_inv_.numel() * sizeof(float);
        total += advection_block_inv_.numel() * sizeof(float);
        total += buoyancy_block_inv_.numel() * sizeof(float);
        total += diffusion_block_inv_.numel() * sizeof(float);
        return total;
    }
};

// =============================================================================
// Multigrid Preconditioner
// =============================================================================

class MultigridPreconditioner : public AdvancedPreconditioner {
private:
    struct GridLevel {
        int nx, ny, nz;
        float dx, dy;
        torch::Tensor dz;
        float dz_mean_cached = 0.0f;  // PERF FIX 2025-12-28: Cache dz mean to avoid repeated reductions
        torch::Tensor restriction_op;
        torch::Tensor prolongation_op;
        // std::unique_ptr<CGridDerivatives> derivatives;  // TODO: Implement if needed
    };
    
    std::vector<GridLevel> grid_levels_;
    int n_levels_;
    int n_pre_smooth_ = 2;
    int n_post_smooth_ = 2;
    int n_coarse_iter_ = 10;
    
    // Physical parameters needed by member functions
    float dt_ = 0.0f;
    double gamma_ = 0.43586652150845899942;  // v20.14 r50-fix: float→double
    float c_sound_ = 340.0f;
    
    // Smoothers
    enum SmootherType {
        JACOBI,
        GAUSS_SEIDEL,
        RED_BLACK_GAUSS_SEIDEL,
        ILU0
    } smoother_type_ = RED_BLACK_GAUSS_SEIDEL;
    
    // Cycle type
    enum CycleType {
        V_CYCLE,
        W_CYCLE,
        F_CYCLE
    } cycle_type_ = V_CYCLE;
    
public:
    MultigridPreconditioner(int nx, int ny, int nz,
                           float dx, float dy, const torch::Tensor& dz,
                           int n_levels = 3)
        : n_levels_(n_levels) {
        setup_grid_hierarchy(nx, ny, nz, dx, dy, dz);
    }
    
    torch::Tensor apply(const torch::Tensor& r) override {
        // Start V-cycle from finest level
        return multigrid_cycle(0, r);
    }
    
    void update(const torch::Tensor& state, float dt, float gamma) override {
        // Update operators on all levels if needed
        dt_ = dt;
        gamma_ = gamma;
        for (auto& level : grid_levels_) {
            // Update based on current state
        }
    }
    
private:
    void setup_grid_hierarchy(int nx, int ny, int nz,
                             float dx, float dy, const torch::Tensor& dz) {
        grid_levels_.reserve(n_levels_);

        // Finest level
        GridLevel finest;
        finest.nx = nx; finest.ny = ny; finest.nz = nz;
        finest.dx = dx; finest.dy = dy; finest.dz = dz;
        // PERF FIX 2025-12-28: Cache dz mean at setup to avoid repeated reductions
        {
            torch::NoGradGuard no_grad;
            finest.dz_mean_cached = torch::mean(dz.to(torch::kCPU)).item<float>();
        }
        // finest.derivatives = std::make_unique<CGridDerivatives>(dx, dy, dz);
        grid_levels_.push_back(std::move(finest));

        // Coarser levels
        for (int level = 1; level < n_levels_; level++) {
            GridLevel coarse;
            coarse.nx = (grid_levels_[level-1].nx + 1) / 2;
            coarse.ny = (grid_levels_[level-1].ny + 1) / 2;
            coarse.nz = (grid_levels_[level-1].nz + 1) / 2;
            coarse.dx = grid_levels_[level-1].dx * 2.0f;
            coarse.dy = grid_levels_[level-1].dy * 2.0f;

            // Coarsen vertical spacing
            auto dz_coarse = torch::zeros(coarse.nz, dz.options());
            for (int k = 0; k < coarse.nz; k++) {
                dz_coarse[k] = grid_levels_[level-1].dz[2*k] +
                              grid_levels_[level-1].dz[2*k+1];
            }
            coarse.dz = dz_coarse;
            // PERF FIX 2025-12-28: Cache dz mean at setup to avoid repeated reductions
            {
                torch::NoGradGuard no_grad;
                coarse.dz_mean_cached = torch::mean(dz_coarse.to(torch::kCPU)).item<float>();
            }

            // coarse.derivatives = std::make_unique<CGridDerivatives>(
            //     coarse.dx, coarse.dy, coarse.dz
            // );

            // Setup restriction and prolongation operators
            setup_transfer_operators(level);

            grid_levels_.push_back(std::move(coarse));
        }
    }
    
    void setup_transfer_operators(int level) {
        // Full-weighting restriction (27-point stencil in 3D)
        // Linear interpolation for prolongation
    }
    
    torch::Tensor multigrid_cycle(int level, const torch::Tensor& r) {
        auto& grid = grid_levels_[level];
        
        if (level == n_levels_ - 1) {
            // Coarsest level - solve directly
            return coarse_grid_solve(r);
        }
        
        // Pre-smoothing
        auto v = torch::zeros_like(r);
        for (int i = 0; i < n_pre_smooth_; i++) {
            v = smooth(level, v, r);
        }
        
        // Compute residual
        auto residual = r - apply_operator(level, v);
        
        // Restrict to coarser grid
        auto r_coarse = restrict_to_coarse(level, residual);
        
        // Recursive call
        auto v_coarse = torch::zeros_like(r_coarse);
        if (cycle_type_ == V_CYCLE) {
            v_coarse = multigrid_cycle(level + 1, r_coarse);
        } else if (cycle_type_ == W_CYCLE) {
            v_coarse = multigrid_cycle(level + 1, r_coarse);
            v_coarse = multigrid_cycle(level + 1, r_coarse);
        }
        
        // Prolongate and correct
        v = v + prolongate_to_fine(level, v_coarse);
        
        // Post-smoothing
        for (int i = 0; i < n_post_smooth_; i++) {
            v = smooth(level, v, r);
        }
        
        return v;
    }
    
    torch::Tensor smooth(int level, const torch::Tensor& v, const torch::Tensor& r) {
        switch (smoother_type_) {
            case JACOBI:
                return jacobi_smooth(level, v, r);
            case RED_BLACK_GAUSS_SEIDEL:
                return red_black_gauss_seidel_smooth(level, v, r);
            default:
                return v;
        }
    }
    
    torch::Tensor red_black_gauss_seidel_smooth(int level, 
                                                const torch::Tensor& v, 
                                                const torch::Tensor& r);
    
    // Additional multigrid helper functions
    torch::Tensor jacobi_smooth(int level, const torch::Tensor& v, const torch::Tensor& r) {
        auto diag = get_diagonal(level);
        auto Av = apply_operator(level, v);
        auto residual = r - Av;
        const float omega = 0.67f;
        return v + omega * residual / diag;
    }
    
    // Missing function declarations
    torch::Tensor coarse_grid_solve(const torch::Tensor& r);
    torch::Tensor apply_operator(int level, const torch::Tensor& v);
    torch::Tensor compute_laplacian(int level, const torch::Tensor& v);
    torch::Tensor get_diagonal(int level);
    torch::Tensor restrict_to_coarse(int level, const torch::Tensor& fine);
    torch::Tensor prolongate_to_fine(int level, const torch::Tensor& coarse);
    
    size_t get_memory_usage() const override {
        size_t total = 0;
        for (const auto& level : grid_levels_) {
            total += level.nx * level.ny * level.nz * sizeof(float) * 7;
        }
        return total;
    }
    
};

// =============================================================================
// Approximate Schur Complement Preconditioner
// =============================================================================

class SchurComplementPreconditioner : public AdvancedPreconditioner {
private:
    // System structure: [A  B] [u] = [f]
    //                  [C  D] [p]   [g]
    // Schur complement: S = D - C*A^{-1}*B
    
    torch::Tensor A_inv_;  // Velocity block inverse (approximate)
    torch::Tensor D_;      // Pressure block
    torch::Tensor B_;      // Gradient operator
    torch::Tensor C_;      // Divergence operator
    torch::Tensor S_inv_;  // Schur complement inverse (approximate)
    
    // Approximation methods
    enum SchurApproximation {
        PRESSURE_CONVECTION_DIFFUSION,  // PCD
        LEAST_SQUARES_COMMUTATOR,       // LSC
        BOUNDARY_ADJUSTED_COARSE,       // BAC
        AUGMENTED_LAGRANGIAN           // AL
    } schur_approx_ = PRESSURE_CONVECTION_DIFFUSION;
    
    // Member variables for Schur complement
    float characteristic_length_ = 1000.0f;  // Characteristic length scale
    torch::Tensor pressure_mass_matrix_;
    
    // Private member variables
    float dt_;
    float gamma_;
    float c_sound_ = 340.0f;
    
public:
    torch::Tensor apply(const torch::Tensor& r) override {
        // Split residual
        auto r_u = r.slice(1, 0, 3);  // Velocity components
        auto r_p = r.slice(1, 3, 5);  // Pressure components
        
        // Step 1: Solve S*p = g - C*A^{-1}*f
        auto tmp = apply_A_inverse(r_u);
        auto rhs_p = r_p - apply_C(tmp);
        auto z_p = apply_S_inverse(rhs_p);
        
        // Step 2: Solve A*u = f - B*p
        auto rhs_u = r_u - apply_B(z_p);
        auto z_u = apply_A_inverse(rhs_u);
        
        // Combine results
        return torch::cat({z_u, z_p}, 1);
    }
    
    void update(const torch::Tensor& state, float dt, float gamma) override {
        // Update operator approximations based on current state
        update_velocity_block(state, dt, gamma);
        update_schur_complement(state, dt, gamma);
    }
    
private:
    // Additional helper functions
    torch::Tensor apply_B(const torch::Tensor& p) { return compute_gradient(p); }
    torch::Tensor apply_C(const torch::Tensor& u) { return compute_divergence(u); }
    torch::Tensor apply_A_inverse(const torch::Tensor& r_u);
    torch::Tensor apply_S_inverse(const torch::Tensor& r_p);
    torch::Tensor apply_pcd_inverse(const torch::Tensor& r_p);
    torch::Tensor apply_lsc_inverse(const torch::Tensor& r_p) { return r_p / S_inv_; }
    torch::Tensor apply_al_inverse(const torch::Tensor& r_p) { return r_p / S_inv_; }
    torch::Tensor apply_pressure_convection_diffusion(const torch::Tensor& p);
    void update_velocity_block(const torch::Tensor& state, float dt, float gamma);
    void update_pcd_approximation(const torch::Tensor& state, float dt, float gamma);
    void update_lsc_approximation(const torch::Tensor& state, float dt, float gamma);
    torch::Tensor get_velocity_field() { return torch::zeros({3, 32, 32, 32}); }
    torch::Tensor compute_gradient(const torch::Tensor& p) { return torch::zeros({3, 32, 32, 32}); }
    torch::Tensor compute_divergence(const torch::Tensor& u) { return torch::zeros({32, 32, 32}); }
    torch::Tensor compute_laplacian(const torch::Tensor& p) { return torch::zeros_like(p); }
    
    size_t get_memory_usage() const override {
        return (A_inv_.numel() + D_.numel() + B_.numel() + 
                C_.numel() + S_inv_.numel()) * sizeof(float);
    }
    
    void update_schur_complement(const torch::Tensor& state, float dt, float gamma) {
        switch (schur_approx_) {
            case PRESSURE_CONVECTION_DIFFUSION:
                update_pcd_approximation(state, dt, gamma);
                break;
            case LEAST_SQUARES_COMMUTATOR:
                update_lsc_approximation(state, dt, gamma);
                break;
            default:
                break;
        }
    }
    
};

// =============================================================================
// Adaptive Preconditioner Selection
// =============================================================================

class AdaptivePreconditioner : public AdvancedPreconditioner {
private:
    std::vector<std::unique_ptr<AdvancedPreconditioner>> preconditioners_;
    std::vector<float> effectiveness_scores_;
    int current_preconditioner_ = 0;
    int adaptation_interval_ = 10;
    int iteration_count_ = 0;
    
public:
    AdaptivePreconditioner() {
        // Initialize with different preconditioner types
    }
    
    torch::Tensor apply(const torch::Tensor& r) override {
        iteration_count_++;
        
        // Adapt preconditioner selection
        if (iteration_count_ % adaptation_interval_ == 0) {
            select_best_preconditioner();
        }
        
        return preconditioners_[current_preconditioner_]->apply(r);
    }
    
    void update(const torch::Tensor& state, float dt, float gamma) override {
        // Update all preconditioners
        for (auto& precond : preconditioners_) {
            precond->update(state, dt, gamma);
        }
    }
    
private:
    void update_effectiveness_scores();
    
    void select_best_preconditioner() {
        update_effectiveness_scores();
        // Select based on effectiveness scores
        auto best_it = std::max_element(effectiveness_scores_.begin(), 
                                       effectiveness_scores_.end());
        current_preconditioner_ = std::distance(effectiveness_scores_.begin(), best_it);
    }
    
    size_t get_memory_usage() const override {
        size_t total = 0;
        for (const auto& precond : preconditioners_) {
            total += precond->get_memory_usage();
        }
        return total;
    }
};

// =============================================================================
// Factory Function
// =============================================================================

std::unique_ptr<AdvancedPreconditioner> create_preconditioner(
    const std::string& type,
    int nx, int ny, int nz,
    float dx, float dy, const torch::Tensor& dz,
    const std::unordered_map<std::string, float>& params = {}) {
    
    if (type == "physics_block") {
        auto level = PhysicsBlockPreconditioner::BLOCK_DIAGONAL;
        if (params.count("approximation_level")) {
            level = static_cast<PhysicsBlockPreconditioner::ApproximationLevel>(
                static_cast<int>(params.at("approximation_level"))
            );
        }
        return std::make_unique<PhysicsBlockPreconditioner>(nx, ny, nz, dx, dy, dz, level);
        
    } else if (type == "multigrid") {
        int n_levels = 3;
        if (params.count("n_levels")) {
            n_levels = static_cast<int>(params.at("n_levels"));
        }
        return std::make_unique<MultigridPreconditioner>(nx, ny, nz, dx, dy, dz, n_levels);
        
    } else if (type == "schur") {
        return std::make_unique<SchurComplementPreconditioner>();
        
    } else if (type == "adaptive") {
        return std::make_unique<AdaptivePreconditioner>();
        
    } else {
        throw std::runtime_error("Unknown preconditioner type: " + type);
    }
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_PRECONDITIONER_ADVANCED_H