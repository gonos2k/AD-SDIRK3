#ifndef NEWTON_KRYLOV_SOLVER_H
#define NEWTON_KRYLOV_SOLVER_H

#include "torch_standalone.h"
#include "unified_state_vector.h"
#include "unified_rhs.h"
#include <memory>
#include <functional>

namespace wrf {
namespace sdirk3 {

// Forward declarations
class GMRESSolver;
class AcousticPreconditioner;
struct SDIRKCoefficients;

// Solver options
struct NewtonKrylovOptions {
    // Newton parameters
    int max_newton_iter = 4;
    double newton_tol = 1e-6;
    double newton_tol_relax = 1e-5;
    bool use_adaptive_tol = true;
    
    // GMRES parameters
    int gmres_restart = 30;
    int max_gmres_iter = 50;
    double gmres_tol = 1e-4;
    
    // Performance options
    bool reuse_jacobian = true;
    bool cache_residual = true;
    
    // Output options
    bool verbose = false;
    int print_frequency = 100;
};

// SDIRK coefficients structure
struct SDIRKCoefficients {
    double gamma;                    // Diagonal coefficient
    std::vector<std::vector<double>> a;  // Butcher tableau A
    std::vector<double> b;           // Final weights
    std::vector<double> c;           // Stage nodes
    int num_stages;
    
    SDIRKCoefficients() : num_stages(3) {
        // Initialize with default SDIRK-3 coefficients
        gamma = 0.4358665215084590;
        
        a.resize(3, std::vector<double>(3, 0.0));
        a[0][0] = gamma;
        a[1][0] = 0.2820667392457705;  // tau - gamma
        a[1][1] = gamma;
        a[2][0] = -0.1201074456293559;
        a[2][1] = 0.6842407771498714;
        a[2][2] = gamma;
        
        b = {-0.1201074456293559, 0.6842407771498714, gamma};
        c = {gamma, 0.7179332607542295, 1.0};
    }
};

// GMRES solver for linear systems
class GMRESSolver {
private:
    int restart_;
    int max_iter_;
    double tol_;
    
    // Workspace
    std::vector<torch::Tensor> V_;    // Krylov basis
    torch::Tensor H_;                 // Hessenberg matrix
    torch::Tensor g_;                 // RHS for least squares
    torch::Tensor c_, s_;             // Givens rotations
    
public:
    GMRESSolver(int restart = 30, int max_iter = 50, double tol = 1e-4)
        : restart_(restart), max_iter_(max_iter), tol_(tol) {}
    
    // Solve Ax = b using GMRES with optional preconditioner
    torch::Tensor solve(
        const std::function<torch::Tensor(const torch::Tensor&)>& A_op,
        const torch::Tensor& b,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_op = nullptr,
        const torch::Tensor& x0 = torch::Tensor()
    );
    
private:
    void apply_givens_rotation(double& h_i, double& h_ip1, double c_i, double s_i);
    void compute_givens_rotation(double h_i, double h_ip1, double& c_i, double& s_i);
    torch::Tensor solve_least_squares(const torch::Tensor& H, const torch::Tensor& g, int k);
};

// Acoustic preconditioner for GMRES
class AcousticPreconditioner {
private:
    std::shared_ptr<WRFGridInfo> grid_info_;
    double dt_gamma_;
    
    // Discrete operators
    torch::Tensor L_horizontal_;     // Horizontal acoustic operator
    torch::Tensor L_vertical_;       // Vertical implicit operator
    torch::Tensor div_op_;           // Divergence operator
    torch::Tensor grad_op_;          // Gradient operator
    
    // Factorization for efficient solves
    bool is_factored_;
    torch::Tensor L_horizontal_factored_;
    float coupling_factor_;
    
    // Vertical factors for each column
    struct VerticalFactors {
        torch::Tensor l, d_inv, u;   // LU factorization components
    };
    std::vector<std::vector<VerticalFactors>> L_vertical_factors_;
    
    // Multigrid hierarchy
    struct MultigridLevel {
        int nx, ny;
        torch::Tensor operator_;
        torch::Tensor restriction;
        torch::Tensor prolongation;
    };
    std::vector<MultigridLevel> mg_hierarchy_;
    
    // Solver options
    bool use_multigrid_ = true;
    bool use_direct_solver_ = false;
    bool use_smoothing_ = true;
    bool use_rigid_lid_ = true;
    
    enum HorizontalSolverType { DIRECT, MULTIGRID, CONJUGATE_GRADIENT };
    HorizontalSolverType horizontal_solver_type_ = MULTIGRID;
    
    // Multigrid parameters
    int mg_smoother_iters_ = 2;
    int mg_vcycles_ = 1;
    
    // Iterative solver parameters
    double horizontal_tol_ = 1e-4;
    int horizontal_max_iter_ = 50;
    
public:
    AcousticPreconditioner(std::shared_ptr<WRFGridInfo> grid_info)
        : grid_info_(grid_info), dt_gamma_(0.0), is_factored_(false) {}
    
    // Setup preconditioner for given time step
    void setup(double dt_gamma);
    
    // Apply preconditioner: solve (I - dt*gamma*L_acoustic)z = r
    torch::Tensor apply(const torch::Tensor& r);
    
    // Apply transpose (for BiCGSTAB if needed)
    torch::Tensor apply_transpose(const torch::Tensor& r);
    
private:
    // Build acoustic operator approximation
    void build_acoustic_operator();
    void build_vertical_operator();
    void build_divergence_gradient_operators();
    
    // Factor the preconditioner matrix
    void factor_preconditioner();
    void factor_horizontal_direct();
    void factor_vertical_tridiagonal();
    void setup_horizontal_iterative();
    void compute_schur_complement();
    
    // Vertical implicit solve (block tridiagonal)
    torch::Tensor solve_vertical_implicit(const torch::Tensor& r);
    torch::Tensor solve_vertical_coupled(const torch::Tensor& r_vel, const torch::Tensor& r_pres);
    torch::Tensor solve_vertical_transpose(const torch::Tensor& r);
    torch::Tensor thomas_algorithm(const VerticalFactors& factors, const torch::Tensor& r_column);
    
    // Horizontal acoustic solve
    torch::Tensor solve_horizontal_acoustic(const torch::Tensor& r);
    torch::Tensor solve_horizontal_acoustic_coupled(const torch::Tensor& r_intermediate);
    
    // Multigrid methods
    void setup_multigrid();
    torch::Tensor multigrid_vcycle(const torch::Tensor& r, int level);
    torch::Tensor smooth(const torch::Tensor& z, const torch::Tensor& r, int level);
    torch::Tensor coarse_grid_solve(const torch::Tensor& r, int level);
    void build_transfer_operators(int level);
    torch::Tensor apply_level_operator(const torch::Tensor& x, int level);
    int compute_multigrid_levels(int nx, int ny);
    
    // Helper methods
    std::pair<torch::Tensor, torch::Tensor> extract_velocity_pressure(const torch::Tensor& r);
    torch::Tensor apply_jacobi_smoothing(const torch::Tensor& z, const torch::Tensor& r, int n_smooth);
    void apply_vertical_boundary_conditions();
    torch::Tensor get_diagonal();
    torch::Tensor apply_operator(const torch::Tensor& x);
    VerticalFactors factor_block_tridiagonal(const torch::Tensor& M);
    torch::Tensor build_column_operator(int i, int j);
    
    // Iterative solvers
    torch::Tensor conjugate_gradient(const torch::Tensor& A, const torch::Tensor& b,
                                    double tol, int max_iter);
    
    // Additional helper methods
    torch::Tensor get_diagonal_at_level(int level);
    torch::Tensor build_restriction_2d(int nx_fine, int ny_fine, 
                                       int nx_coarse, int ny_coarse);
    torch::Tensor build_prolongation_2d(int nx_coarse, int ny_coarse,
                                        int nx_fine, int ny_fine);
};

// Main Newton-Krylov solver for SDIRK stages
class NewtonKrylovSDIRK {
private:
    std::shared_ptr<UnifiedRHS> rhs_module_;
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<GMRESSolver> gmres_solver_;
    std::shared_ptr<AcousticPreconditioner> preconditioner_;
    NewtonKrylovOptions options_;
    
    // Cached data
    torch::Tensor last_jacobian_;
    int last_stage_;
    bool jacobian_valid_;
    
public:
    NewtonKrylovSDIRK(
        std::shared_ptr<UnifiedRHS> rhs_module,
        std::shared_ptr<WRFGridInfo> grid_info,
        const NewtonKrylovOptions& options = NewtonKrylovOptions()
    );
    
    // Solve stage i: Find k_i such that R_i(k_i) = 0
    torch::Tensor solve_stage(
        int stage,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt
    );
    
    // Get solver statistics
    struct SolverStats {
        int newton_iterations;
        int total_gmres_iterations;
        double final_residual;
        double initial_residual;
        bool converged;
    };
    
    SolverStats get_last_stats() const { return last_stats_; }
    
private:
    // Compute residual for stage i
    torch::Tensor compute_residual(
        const torch::Tensor& delta_i,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage
    );
    
    // Jacobian-vector product via automatic differentiation
    torch::Tensor jacobian_vector_product(
        const torch::Tensor& v,
        const torch::Tensor& delta_i,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage
    );
    
    // Matrix-free operator for GMRES: (I - dt*gamma*J)v
    std::function<torch::Tensor(const torch::Tensor&)> create_linear_operator(
        const torch::Tensor& delta_i,
        const torch::Tensor& U_n,
        const std::vector<torch::Tensor>& k_prev,
        const SDIRKCoefficients& coeffs,
        double dt,
        int stage
    );
    
    // Check convergence criteria
    bool check_convergence(const torch::Tensor& residual, int stage);
    
    // Adaptive tolerance selection
    double get_newton_tolerance(int stage, int newton_iter);
    
    SolverStats last_stats_;
};

// Stage residual computation - simplified without custom autograd function
class SDIRKStageResidual {
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

// Helper functions for JVP computation
namespace jvp_utils {
    
    // Compute JVP using forward-mode AD
    torch::Tensor compute_jvp_forward(
        const std::function<torch::Tensor(const torch::Tensor&)>& func,
        const torch::Tensor& x,
        const torch::Tensor& v
    );
    
    // Compute JVP using dual numbers (if available)
    torch::Tensor compute_jvp_dual(
        const std::function<torch::Tensor(const torch::Tensor&)>& func,
        const torch::Tensor& x,
        const torch::Tensor& v
    );
    
    // Finite difference approximation (for debugging)
    torch::Tensor compute_jvp_finite_diff(
        const std::function<torch::Tensor(const torch::Tensor&)>& func,
        const torch::Tensor& x,
        const torch::Tensor& v,
        double eps = 1e-7
    );
}

} // namespace sdirk3
} // namespace wrf

#endif // NEWTON_KRYLOV_SOLVER_H