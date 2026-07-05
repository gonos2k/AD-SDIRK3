#ifndef WRF_SDIRK3_AUTOGRAD_REFINED_H
#define WRF_SDIRK3_AUTOGRAD_REFINED_H

#include "torch_standalone.h"
#include <memory>
#include <functional>
#include <chrono>
#include "sdirk3_types.h"
#include "unified_rhs.h"
#include "newton_krylov_solver.h"

namespace wrf {
namespace sdirk3 {

// Enhanced autograd function for SDIRK residual computation
class SDIRKResidualFunction : public torch::autograd::Function<SDIRKResidualFunction> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        const torch::Tensor& delta_i,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        std::shared_ptr<UnifiedRHS> rhs_module,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage);
    
    static torch::autograd::tensor_list backward(
        torch::autograd::AutogradContext* ctx,
        torch::autograd::tensor_list grad_outputs);
};

// Efficient Jacobian-vector product computation with caching
class JacobianVectorProduct {
public:
    explicit JacobianVectorProduct(std::shared_ptr<UnifiedRHS> rhs_module);
    
    // Setup computational graph at base point
    void setup(const torch::Tensor& U_base);
    
    // Compute J·v efficiently
    torch::Tensor apply(const torch::Tensor& v) const;
    
    // Compute J^T·v (transpose/adjoint)
    torch::Tensor apply_transpose(const torch::Tensor& v) const;
    
    // Clear cached data
    void clear();
    
    // Check if ready for JVP computation
    bool is_ready() const { return graph_created_; }
    
private:
    std::shared_ptr<UnifiedRHS> rhs_module_;
    torch::Tensor U_base_;
    torch::Tensor F_base_;
    bool graph_created_;
    
    // Cache for repeated computations
    struct JVPCache {
        torch::Tensor last_v;
        torch::Tensor last_jvp;
        bool valid;
    };
    mutable JVPCache cache_;
};

// Optimized Newton-Krylov solver with refined autograd
class OptimizedNewtonKrylovSDIRK : public NewtonKrylovSDIRK {
public:
    OptimizedNewtonKrylovSDIRK(
        std::shared_ptr<UnifiedRHS> rhs_module,
        std::shared_ptr<WRFGridInfo> grid_info,
        const NewtonKrylovOptions& options);
    
    // Override base class method with optimized version
    torch::Tensor solve_stage(
        int stage,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt) override;
    
    // Get solver statistics
    const SolverStats& get_stats() const { return last_stats_; }
    
protected:
    // Helper methods for optimized solving
    torch::Tensor compute_initial_guess(
        int stage,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt);
    
    torch::Tensor build_stage_state(
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const torch::Tensor& delta_i,
        const SDIRKCoefficients& coeffs,
        int stage);
    
    double line_search(
        const torch::Tensor& delta_current,
        const torch::Tensor& delta_update,
        double current_residual_norm,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage);
    
private:
    std::unique_ptr<JacobianVectorProduct> jvp_operator_;
    
    // Adaptive tolerance parameters
    struct AdaptiveTolerance {
        double base_tol;
        double safety_factor;
        double min_tol;
        double max_tol;
        
        double get_tolerance(int newton_iter, double residual_norm);
    };
    AdaptiveTolerance adaptive_tol_;
};

// Enhanced GMRES solver with flexible preconditioning
class OptimizedGMRESSolver : public GMRESSolver {
public:
    OptimizedGMRESSolver(int restart, int max_iter, double tol, bool flexible = false);
    
    // Optimized solve with better convergence monitoring
    torch::Tensor solve(
        const std::function<torch::Tensor(const torch::Tensor&)>& A_op,
        const torch::Tensor& b,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_op = nullptr,
        const torch::Tensor& x0 = torch::Tensor()) override;
    
    // Get iteration count from last solve
    int get_iteration_count() const { return iteration_count_; }
    
    // Set tolerance dynamically
    void set_tolerance(double tol) { tol_ = tol; }
    
private:
    bool use_flexible_gmres_;
    std::vector<torch::Tensor> Z_;  // Preconditioned vectors for FGMRES
    int iteration_count_;
    
    void apply_givens_rotations(int j);
    torch::Tensor solve_triangular_system(int size);
};

// Utilities for JVP computation and testing
namespace jvp_utils {

// Compute JVP using forward-mode AD
torch::Tensor compute_jvp_forward(
    const std::function<torch::Tensor(const torch::Tensor&)>& func,
    const torch::Tensor& x,
    const torch::Tensor& v);

// Compute JVP using finite differences (for testing)
torch::Tensor compute_jvp_finite_diff(
    const std::function<torch::Tensor(const torch::Tensor&)>& func,
    const torch::Tensor& x,
    const torch::Tensor& v,
    double eps = 1e-7);

// Test JVP implementation correctness
bool test_jvp_correctness(
    const std::function<torch::Tensor(const torch::Tensor&)>& func,
    const torch::Tensor& x,
    int num_tests = 10,
    double tol = 1e-6);

// Compute full Jacobian matrix (expensive, for debugging only)
torch::Tensor compute_full_jacobian(
    const std::function<torch::Tensor(const torch::Tensor&)>& func,
    const torch::Tensor& x);

} // namespace jvp_utils

// Factory function for creating optimized solver
std::unique_ptr<NewtonKrylovSDIRK> create_optimized_sdirk_solver(
    std::shared_ptr<UnifiedRHS> rhs_module,
    std::shared_ptr<WRFGridInfo> grid_info,
    const NewtonKrylovOptions& options);

// Performance monitoring utilities
class PerformanceMonitor {
public:
    void start_timing(const std::string& name);
    void end_timing(const std::string& name);
    
    void add_counter(const std::string& name, int64_t value = 1);
    
    void print_summary() const;
    void reset();
    
private:
    struct TimingInfo {
        std::chrono::high_resolution_clock::time_point start;
        double total_time = 0.0;
        int count = 0;
    };
    
    std::map<std::string, TimingInfo> timings_;
    std::map<std::string, int64_t> counters_;
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AUTOGRAD_REFINED_H