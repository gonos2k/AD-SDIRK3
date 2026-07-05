#ifndef SDIRK3_SOLVER_H
#define SDIRK3_SOLVER_H

#include "torch_standalone.h"
#include "unified_state_vector.h"
#include "unified_rhs.h"
#include "newton_krylov_solver.h"
#include <memory>

namespace wrf {
namespace sdirk3 {

// SDIRK-3 solver options
struct SDIRK3Options {
    // Newton-Krylov options
    int max_newton_iter = 4;
    double newton_tol = 1e-6;
    int gmres_restart = 30;
    int max_gmres_iter = 50;
    double gmres_tol = 1e-4;
    
    // Adaptive time stepping
    bool adaptive = false;
    double dt_min = 1e-6;
    double dt_max = 10.0;
    int max_dt_attempts = 10;
    
    // Output options
    bool verbose = false;
};

// Solver statistics structure
struct SolverStats {
    int stages_computed = 0;
    int newton_iterations = 0;
    int gmres_iterations = 0;
    double dt_used = 0.0;
    double dt_next = 0.0;
    double error_estimate = 0.0;
    double initial_residual = 0.0;
    double final_residual = 0.0;
    bool converged = false;
};

// Main SDIRK-3 time integrator
class SDIRK3Solver {
public:  // Make newton_solver_ accessible for interface
    std::shared_ptr<UnifiedRHS> rhs_module_;
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<NewtonKrylovSDIRK> newton_solver_;
    SDIRK3Options options_;
    
private:
    // Statistics
    SolverStats last_stats_;
    
public:
    SDIRK3Solver(
        std::shared_ptr<UnifiedRHS> rhs_module,
        std::shared_ptr<WRFGridInfo> grid_info,
        const SDIRK3Options& options = SDIRK3Options()
    );
    
    // Take a single time step
    torch::Tensor step(const torch::Tensor& U_n, double dt);
    
    // Take adaptive time step
    torch::Tensor adaptive_step(
        const torch::Tensor& U_n, 
        double& dt,
        double tolerance = 1e-6
    );
    
    // Get solver statistics
    SolverStats last_stats() const { return last_stats_; }
    SolverStats get_stats() const { return last_stats_; }
    
private:
    // Error estimation for adaptive stepping
    double estimate_error(
        const torch::Tensor& U_n,
        const torch::Tensor& U_new,
        double dt
    );
};

// C interface for Fortran interoperability
extern "C" {
    
    // Create solver instance
    void* create_sdirk3_solver(
        void* rhs_module,
        void* grid_info,
        int max_newton_iter,
        double newton_tol,
        int gmres_restart,
        int verbose
    );
    
    // Destroy solver instance
    void destroy_sdirk3_solver(void* solver);
    
    // Take single time step
    int sdirk3_step(
        void* solver,
        float* U_new,           // Output: new state
        const float* U_n,       // Input: current state
        int n_points,          // Number of grid points
        int n_vars,            // Number of variables
        double dt              // Time step
    );
    
    // Take adaptive time step
    int sdirk3_adaptive_step(
        void* solver,
        float* U_new,          // Output: new state
        const float* U_n,      // Input: current state
        int n_points,          // Number of grid points
        int n_vars,            // Number of variables
        double* dt,            // Input/Output: time step
        double tolerance       // Error tolerance
    );
}

} // namespace sdirk3
} // namespace wrf

#endif // SDIRK3_SOLVER_H