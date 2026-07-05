/**
 * SDIRK3 Solver Header
 * Core time integration implementation with device support
 */

#ifndef WRF_SDIRK3_SOLVER_H
#define WRF_SDIRK3_SOLVER_H

#include "device_manager.h"
#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

// ============================================================================
// SDIRK3 Solver Implementation
// ============================================================================

class SDIRK3Solver {
private:
    DeviceManager& device_manager_;
    bool enable_diagnostics_;
    
    // SDIRK3 Butcher tableau coefficients
    const double gamma_ = 0.43586652;
    const double a21_ = 0.43586652;
    const double a31_ = 0.24291996;
    const double a32_ = 0.19294656;
    const double b1_ = 0.24291996;
    const double b2_ = 0.19294656;
    const double b3_ = 0.56413648;
    
    // Newton solver parameters
    float newton_tolerance_ = 1e-8f;
    int max_newton_iterations_ = 10;
    float line_search_alpha_ = 1.0f;
    
    // Convergence tracking
    bool last_converged_ = false;
    int last_iterations_ = 0;
    float last_residual_ = 0.0f;
    
    // Stability monitoring
    std::unique_ptr<class StabilityMonitor> stability_monitor_;
    
public:
    /**
     * Constructor
     * @param device_manager Device manager for tensor operations
     * @param enable_diagnostics Enable diagnostic monitoring
     */
    SDIRK3Solver(DeviceManager& device_manager, bool enable_diagnostics = false);
    
    /**
     * Advance state by one timestep using SDIRK3
     * @param state Current WRF state
     * @param dt Time step size
     * @return New state after time integration
     */
    WRFState advance(const WRFState& state, float dt);
    
    /**
     * Set Newton solver tolerance
     * @param tol Convergence tolerance
     */
    void setNewtonTolerance(float tol) { newton_tolerance_ = tol; }
    
    /**
     * Set maximum Newton iterations
     * @param max_iter Maximum iterations
     */
    void setMaxNewtonIterations(int max_iter) { max_newton_iterations_ = max_iter; }
    
    /**
     * Get last convergence status
     * @return True if last solve converged
     */
    bool getLastConvergenceStatus() const { return last_converged_; }
    
    /**
     * Get last iteration count
     * @return Number of iterations in last solve
     */
    int getLastIterationCount() const { return last_iterations_; }
    
    /**
     * Get last residual norm
     * @return Final residual from last solve
     */
    float getLastResidual() const { return last_residual_; }
    
private:
    /**
     * Solve implicit stage using Newton-Krylov method
     * @param U_stage Stage state
     * @param U_n Initial state
     * @param K_prev Previous stage derivatives
     * @param dt Time step
     * @param stage Stage number (1, 2, or 3)
     * @return Stage derivative K
     */
    WRFState solveImplicitStage(
        const WRFState& U_stage,
        const WRFState& U_n,
        const std::vector<WRFState>& K_prev,
        float dt,
        int stage
    );
    
    /**
     * Compute right-hand side (dynamics + physics)
     * @param state Current state
     * @return RHS tendencies
     */
    WRFState computeRHS(const WRFState& state);
    
    /**
     * Newton solver for implicit stage
     * @param residual_func Residual function
     * @param initial_guess Initial guess for solution
     * @param dt Time step
     * @return Converged solution
     */
    WRFState newtonSolve(
        std::function<WRFState(const WRFState&)> residual_func,
        const WRFState& initial_guess,
        float dt
    );
    
    /**
     * GMRES solver for Newton linear system
     * @param A_func Matrix-vector product function
     * @param b Right-hand side
     * @param x0 Initial guess
     * @param tol Tolerance
     * @param max_iter Maximum iterations
     * @return Solution vector
     */
    WRFState gmresSolve(
        std::function<WRFState(const WRFState&)> A_func,
        const WRFState& b,
        const WRFState& x0,
        float tol,
        int max_iter
    );
    
    /**
     * Compute Jacobian-vector product using automatic differentiation
     * @param state Current state
     * @param vector Direction vector
     * @return J*v product
     */
    WRFState computeJVP(const WRFState& state, const WRFState& vector);
    
    /**
     * Apply stage update
     * @param state Base state
     * @param K Stage derivative
     * @param coeff Coefficient
     * @return Updated state
     */
    WRFState applyStageUpdate(const WRFState& state, const WRFState& K, float coeff);
    
    /**
     * Check convergence with AD-safe operations
     * @param residual Current residual
     * @param tolerance Convergence tolerance
     * @return Convergence status
     */
    bool checkConvergence(const torch::Tensor& residual, float tolerance);
};

// ============================================================================
// Stability Monitor
// ============================================================================

class StabilityMonitor {
private:
    bool enable_diagnostics_;
    DeviceManager& device_manager_;
    
    // Statistics tracking
    struct VariableStats {
        double min_val = std::numeric_limits<double>::max();
        double max_val = std::numeric_limits<double>::lowest();
        int nan_count = 0;
        int inf_count = 0;
        int negative_count = 0;
    };
    
    std::map<std::string, VariableStats> stats_;
    
public:
    StabilityMonitor(DeviceManager& device_manager, bool enable_diagnostics);
    
    /**
     * Check tensor for numerical issues
     * @param tensor Tensor to check
     * @param var_name Variable name for logging
     * @param should_be_positive Whether variable should be positive
     */
    void checkTensor(
        const torch::Tensor& tensor,
        const std::string& var_name,
        bool should_be_positive = false
    );
    
    /**
     * Check WRF state for stability
     * @param state WRF state to check
     */
    void checkState(const WRFState& state);
    
    /**
     * Print diagnostic summary
     */
    void printSummary() const;
    
private:
    void logInstability(
        const torch::Tensor& tensor,
        const std::string& var_name,
        const torch::Tensor& mask
    );
};

// ============================================================================
// Dynamics and Physics Operators
// ============================================================================

/**
 * Compute WRF dynamics tendencies
 * @param state Current state
 * @param device_manager Device manager for operations
 * @return Dynamics tendencies
 */
WRFState computeDynamicsTendencies(
    const WRFState& state,
    DeviceManager& device_manager
);

/**
 * Compute physics parameterization tendencies
 * @param state Current state
 * @param dt Time step
 * @param device_manager Device manager for operations
 * @return Physics tendencies
 */
WRFState computePhysicsTendencies(
    const WRFState& state,
    float dt,
    DeviceManager& device_manager
);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Compute L2 norm of WRF state
 * @param state WRF state
 * @return L2 norm
 */
torch::Tensor stateNorm(const WRFState& state);

/**
 * Add two WRF states
 * @param a First state
 * @param b Second state
 * @return Sum a + b
 */
WRFState addStates(const WRFState& a, const WRFState& b);

/**
 * Scale WRF state by scalar
 * @param state WRF state
 * @param scalar Scaling factor
 * @return Scaled state
 */
WRFState scaleState(const WRFState& state, float scalar);

/**
 * Compute inner product of two states
 * @param a First state
 * @param b Second state
 * @return Inner product
 */
torch::Tensor innerProduct(const WRFState& a, const WRFState& b);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_SOLVER_H