#ifndef WRF_SDIRK3_NEWTON_SOLVER_H
#define WRF_SDIRK3_NEWTON_SOLVER_H

#include <torch/torch.h>
#include <functional>
#include <memory>
#include <vector>

namespace wrf {
namespace sdirk3 {

// Forward declarations
class WRFPreconditioner;

/**
 * WRF SDIRK-3 Newton-Krylov Solver
 * 
 * 파일명: wrf_sdirk3_newton_solver.h
 * 목적: Implicit stage를 위한 Newton-Krylov 솔버
 */

// Solver options
struct WRFNewtonKrylovOptions {
    // Newton settings
    int max_newton_iter;
    float newton_tol;
    float newton_rtol;
    
    // Krylov settings
    enum KrylovMethod { GMRES, BICGSTAB, TFQMR };
    KrylovMethod krylov_method;
    int gmres_restart;
    int max_krylov_iter;
    float krylov_tol;
    
    // Adaptive control
    bool use_adaptive_tolerances = true;  // Enable Eisenstat-Walker adaptive forcing
    bool use_adaptive_timestep = false;
    float dt_min = 1.0f;
    float dt_max = 600.0f;

    // Eisenstat-Walker adaptive forcing parameters
    float ew_eta_max = 0.9f;     // Maximum forcing term
    float ew_eta_min = 0.02f;    // Minimum forcing term (raised from 1e-4)
    float ew_gamma = 0.9f;       // Safety factor for convergence
    float ew_alpha = 1.5f;       // Superlinear convergence exponent
    
    // Preconditioning
    bool use_preconditioner;
    enum PrecondType { NONE, JACOBI, BLOCK_JACOBI, ILU0 };
    PrecondType precond_type;
    
    // Line search
    bool use_line_search;
    float line_search_alpha;
    
    // Diagnostics
    bool verbose;
    bool compute_condition_number;
    
    // 4DVAR support
    bool save_trajectory;       // Save state for adjoint
    int checkpoint_interval;    // Steps between checkpoints (e.g., 360 = 1 hour)
    bool retain_graph_for_adjoint;  // Keep autograd graph for 4DVAR

    // Block diagonal scaling for GMRES conditioning
    // S transforms A·dK = -R into S⁻¹·A·S·(S⁻¹·dK) = -S⁻¹·R
    //
    // CRITICAL: The Newton equation A·dK = -R operates in TENDENCY space (K = dU/dt),
    // NOT state space (U). The scaling S must reflect tendency magnitudes.
    //
    // S is computed per-block from R₀ at each Newton solve (iter 0),
    // with these values as lower bounds to prevent zero-scaling.
    // v20.14r27f: Wired to config env vars WRF_SDIRK3_SCALE_{U,PH,T,MU}.
    float scale_u  = 1.0f;     // Floor for momentum tendency blocks (ru, rv, rw)
    float scale_ph = 1.0f;     // Floor for geopotential tendency block
    float scale_t  = 1.0f;     // Floor for temperature tendency block
    float scale_mu = 1.0f;     // Floor for mass tendency block

    // Grid dimensions for exact state layout computation
    // If set to 0 (default), layout will be inferred heuristically from state size
    // For exact per-block epsilon scaling, set these to actual grid dimensions
    int nx = 0;      // Number of mass points in x-direction
    int ny = 0;      // Number of mass points in y-direction
    int nz = 0;      // Number of mass points in z-direction (vertical levels)
    int nx_u = 0;    // Number of u-stagger points in x (typically nx+1)
    int ny_v = 0;    // Number of v-stagger points in y (typically ny+1)
    int nz_w = 0;    // Number of w-stagger points in z (typically nz+1)

    // v20.14r20: Periodic BC flags (instance state, replaces global config reads).
    // Set by tile solver from grid BC detection, consumed by halo mask builder.
    bool periodic_x = false;
    bool periodic_y = false;

    // v20.14r27i: Multi-tile flag for halo diagnostics.
    // Set by tile solver from MPI process grid (nprocx*nprocy > 1).
    // Single-tile runs don't need halo exchange warnings.
    bool is_multi_tile = false;

    // Constructor with WRF defaults
    WRFNewtonKrylovOptions() :
        max_newton_iter(50),      // Increased from 10 for better convergence
        newton_tol(1e-3f),        // Relaxed from 1e-5f for em_b_wave test
        newton_rtol(1e-5f),       // Relaxed from 1e-7f
        krylov_method(GMRES),
        gmres_restart(30),        // MEMORY: Reduced from 60 to limit Arnoldi workspace (61 vectors * state_size)
        max_krylov_iter(500),     // Increased from 200 to allow more restarts
        krylov_tol(1e-3f),        // Relaxed from 1e-4f
        use_preconditioner(true),
        precond_type(BLOCK_JACOBI),
        use_line_search(true),    // Enabled for better convergence
        line_search_alpha(1e-4f),
        verbose(false),
        compute_condition_number(false),
        save_trajectory(false),
        checkpoint_interval(360),  // 1 hour with dt=10s
        retain_graph_for_adjoint(false) {}
};

/**
 * Newton-Krylov solver for SDIRK-3 stages
 */
class WRFNewtonKrylovSolver {
public:
    explicit WRFNewtonKrylovSolver(const WRFNewtonKrylovOptions& options, int mu_size = 0);
    ~WRFNewtonKrylovSolver();
    
    /**
     * Result structure for GMRES solver
     */
    struct GMRESResult {
        torch::Tensor x;           // Solution vector
        bool success;              // Success status (false if NaN failures exceeded)
        int iterations;            // Number of GMRES iterations performed
        float final_residual;      // Final residual norm ||r_true||
        float rel_error;           // Relative error ||r_true||/||b|| for trust region predicted (added 2025-11-28)
        std::string message;       // Status/error message
        torch::Tensor r_true;     // v20.11: Final true residual tensor b-A(x) for per-block diagnostics
        // PR 8 (Stage-3 diagnostics): termination metadata for the opt-in
        // SDIRK3_FGMRES_DIAG record. Default member initializers keep every
        // existing aggregate `return {…}` valid (missing fields value-init);
        // the return sites populate them where the trackers are in scope.
        int restarts = 0;          // completed outer restart cycles
        bool breakdown = false;    // Arnoldi early/happy breakdown occurred
        bool stagnation = false;   // terminated by a stagnation guard
    };

    /**
     * Result structure for Newton solver
     */
    struct NewtonResult {
        torch::Tensor K;           // Stage derivative solution
        bool converged;            // Convergence status
        int iterations;            // Number of Newton iterations
        float final_residual;      // Final residual norm
        std::string message;       // Status/error message
    };
    
    /**
     * Solve implicit stage: (I - dt*γ*J)K = F(U)
     * 
     * @param U_n Initial state at time n
     * @param K_prev Previous stage derivatives (can be empty for stage 1)
     * @param compute_rhs Function to compute F(U)
     * @param dt Time step
     * @param gamma SDIRK diagonal coefficient
     * @param stage Stage number (1, 2, or 3)
     * @return NewtonResult containing K and convergence status
     */
    NewtonResult solve_stage_with_status(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        float dt,
        float gamma,
        int stage
    );
    
    // Backward compatibility: old interface without F_phys
    torch::Tensor solve_stage(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        float dt,
        float gamma,
        int stage
    );
    
    // New interface with physical forcing F_phys
    torch::Tensor solve_stage(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        float dt,
        float gamma,
        int stage,
        const torch::Tensor& F_phys  // Physical forcing term for SDIRK3
    );

    // Extended interface: pass fast-mode RHS for stage predictor refinement.
    // compute_rhs controls the actual Newton residual equation.
    // compute_rhs_fast is used only for cheap predictor quality checks/corrections.
    torch::Tensor solve_stage(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs_fast,
        float dt,
        float gamma,
        int stage,
        const torch::Tensor& F_phys = torch::Tensor()
    );
    
    /**
     * Get convergence statistics
     */
    struct ConvergenceStats {
        int newton_iterations;
        int total_krylov_iterations;
        std::vector<float> newton_residuals;
        float final_residual;
        float initial_unscaled_residual;  // v20.14r39: L2 ||R_0|| at Newton iter 0 (for diagnostics)
        torch::Tensor initial_residual_vector;  // Detached packed R_0 used by WRMS stage-gate growth metric
        float condition_number;
        bool converged;
    };
    
    ConvergenceStats get_stats() const;
    void reset_stats();
    
    /**
     * Set preconditioner to use in GMRES solver
     */
    void set_preconditioner(WRFPreconditioner* precond);

    /**
     * Get preconditioner (for external refresh after setBaseState)
     */
    WRFPreconditioner* get_preconditioner();

    /**
     * Update grid dimensions for StateLayout (called when staggered dims change).
     * This re-initializes the cached StateLayout used for per-variable residual
     * decomposition and JVP epsilon scaling.
     */
    void update_grid_dimensions(int nx, int ny, int nz,
                                int nx_u, int ny_v, int nz_w);

    /**
     * Update periodic BC flags (v20.14r21).
     * Called from tile solver's setBoundaryConditions() when BC flags change.
     * Invalidates halo mask so it is rebuilt on next GMRES call.
     */
    void update_boundary_periodicity(bool periodic_x, bool periodic_y);

    /**
     * Set physics-based scaling vector for GMRES conditioning.
     * Called by tile solver before each solve_stage when scaling_mode=PHYSICS.
     * Overwrites S_diag_/S_inv_diag_ and prevents R₀-based rebuild.
     */
    void set_physics_scaling(const torch::Tensor& S_diag);

    /**
     * Clear physics scaling flag. Called before building new physics S.
     * If set_physics_scaling() is NOT called after this, R₀-based RMS rebuild activates.
     */
    void clear_physics_scaling();

    /**
     * 4DVAR checkpoint accessors.
     * Trajectory states are detached snapshots saved at stage-1 checkpoints.
     */
    std::vector<torch::Tensor> get_saved_trajectory() const;
    void clear_saved_trajectory();
    size_t get_saved_trajectory_count() const;
    int get_saved_global_timestep() const;
    void maybe_save_trajectory_checkpoint(const torch::Tensor& state, int stage);

    /**
     * Replace saved trajectory snapshots (for replay/restart workflows).
     * If global_timestep >= 0, overwrite internal timestep counter as well.
     */
    void set_saved_trajectory(const std::vector<torch::Tensor>& checkpoints,
                              int global_timestep = -1);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * Krylov subspace methods
 */
// Forward declaration for GMRES per-block diagnostics (defined in newton_solver.cpp)
struct StateLayout;

namespace krylov_methods {

    /**
     * GMRES (Generalized Minimal Residual) with right preconditioning.
     * Best for nonsymmetric systems from Newton-Krylov.
     *
     * Returns GMRESResult {x, success, iterations, final_residual, rel_error, message, r_true}.
     *   - final_residual: ||r_true|| (absolute, halo-zeroed norm)
     *   - rel_error:      ||r_true||/||b|| (relative, halo-zeroed norms)
     *   - r_true:         RAW residual b-A(x) (NOT halo-zeroed). Callers must apply
     *                     halo zeroing before per-block diagnostics.
     *
     * @param layout     Per-block diagnostics layout (nullptr disables per-block logging)
     * @param halo_mask  Halo region mask for boundary artifact suppression
     * @param periodic_x True if x-direction is periodic (skip x-halo zeroing)
     * @param periodic_y True if y-direction is periodic (skip y-halo zeroing)
     */
    WRFNewtonKrylovSolver::GMRESResult solve_gmres(
        const std::function<torch::Tensor(const torch::Tensor&)>& A,
        const torch::Tensor& b,
        const torch::Tensor& x0,
        int stage_id,
        float ru_share_hint,
        int restart,
        float tol,
        int max_iter,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_inv = nullptr,
        const StateLayout* layout = nullptr,
        const torch::Tensor* halo_mask = nullptr,
        bool periodic_x = false,
        bool periodic_y = false
    );

    // FGMRES (flexible GMRES): stores the per-step preconditioned basis
    // Z[j] = M_j^{-1} V[j] and reconstructs all corrections from Z, so a
    // preconditioner that VARIES across Arnoldi steps (ratio-guard identity
    // lock, warn_only, defect-refinement toggling) stays mathematically
    // consistent. MANDATORY for every production right-preconditioned
    // (M_inv != nullptr) forward Newton-Krylov solve; solve_gmres remains for
    // unpreconditioned and operator-folded (adjoint) paths. Same signature
    // and GMRESResult contract as solve_gmres.
    WRFNewtonKrylovSolver::GMRESResult solve_fgmres(
        const std::function<torch::Tensor(const torch::Tensor&)>& A,
        const torch::Tensor& b,
        const torch::Tensor& x0,
        int stage_id,
        float ru_share_hint,
        int restart,
        float tol,
        int max_iter,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_inv = nullptr,
        const StateLayout* layout = nullptr,
        const torch::Tensor* halo_mask = nullptr,
        bool periodic_x = false,
        bool periodic_y = false
    );
    
    /**
     * BiCGSTAB (Biconjugate Gradient Stabilized)
     * Good for nonsymmetric systems, less memory than GMRES
     */
    torch::Tensor solve_bicgstab(
        const std::function<torch::Tensor(const torch::Tensor&)>& A,
        const torch::Tensor& b,
        const torch::Tensor& x0,
        float tol,
        int max_iter,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_inv = nullptr
    );
    
    /**
     * TFQMR (Transpose-Free Quasi-Minimal Residual)
     * Alternative to BiCGSTAB
     */
    torch::Tensor solve_tfqmr(
        const std::function<torch::Tensor(const torch::Tensor&)>& A,
        const torch::Tensor& b,
        const torch::Tensor& x0,
        float tol,
        int max_iter,
        const std::function<torch::Tensor(const torch::Tensor&)>& M_inv = nullptr
    );
}

/**
 * Preconditioners for acoustic systems
 */
class WRFPreconditioner {
public:
    virtual ~WRFPreconditioner() = default;
    virtual torch::Tensor apply(const torch::Tensor& r) = 0;
    virtual void update(const torch::Tensor& state, float dt, float gamma) {}
    // v20.3: Newton progress feedback for adaptive regularization.
    // ratio = ||S⁻¹R||_rms / ||S⁻¹R₀||_rms (1.0 at start, → 0 as Newton converges).
    // Default no-op for preconditioners that don't use adaptive α.
    virtual void set_newton_residual_ratio(float ratio) { (void)ratio; }

    // v20.14 r46g: Newton iteration index for dynamic cap schedule.
    // Called by Newton solver before each GMRES solve.
    virtual void set_newton_iteration(int iter) { (void)iter; }

    // v20.14 r47c-fix2: ru_share from BLOCK_RES for adaptive Phase2 scaling.
    // Called by Newton solver after BLOCK_RES computation.
    virtual void set_newton_ru_share(float ru_share) { (void)ru_share; }
};

/**
 * Jacobi preconditioner (diagonal scaling)
 */
class JacobiPreconditioner : public WRFPreconditioner {
public:
    JacobiPreconditioner(const torch::Tensor& diagonal);
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    torch::Tensor diagonal_inv_;
};

/**
 * Block Jacobi for acoustic system
 * Blocks correspond to velocity and pressure
 */
class BlockJacobiPreconditioner : public WRFPreconditioner {
public:
    BlockJacobiPreconditioner(int nx, int ny, int nz);
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    
private:
    torch::Tensor velocity_block_inv_;
    torch::Tensor pressure_block_inv_;
};

/**
 * Physics-based preconditioner using approximate Schur complement
 */
class SchurComplementPreconditioner : public WRFPreconditioner {
public:
    SchurComplementPreconditioner(
        float dx, float dy, float dz,
        float c_sound, float dt, float gamma
    );
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    float dx_, dy_, dz_;
    float c_sound_;
    float dt_;
    float gamma_;
    
    torch::Tensor solve_helmholtz(const torch::Tensor& rhs);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_NEWTON_SOLVER_H
