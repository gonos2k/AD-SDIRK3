#ifndef WRF_SDIRK3_NEWTON_SOLVER_H
#define WRF_SDIRK3_NEWTON_SOLVER_H

#include <torch/torch.h>
#include <limits>
#include "wrf_sdirk3_state_layout.h"   // 9F.D93: THE packed-state layout
#include "wrf_sdirk3_operator_contract.h"  // FrozenStageWeights: the stage gate's weighting
#include <functional>
#include <cstdint>
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
     * PR 8.1 (review P1): the EXACT Krylov termination reason. The previous
     * single `stagnation` boolean conflated two different exits — the
     * consecutive true-residual Arnoldi stagnation detector and the
     * ru-dominant MID-BUDGET HOPELESS probe (forced at j == max(2,
     * restart/2)) — both produced "Arnoldi stagnation early exit", so the
     * classification report could not tell which policy actually fired.
     */
    enum class KrylovTerminationReason {
        InitialConverged,            // ||b - A x0|| already under tolerance
        ToleranceReached,            // converged during the Arnoldi sweep
        InternalConvergenceStop,     // internal-stop criterion before budget
        ArnoldiStagnation,           // consecutive true-residual ratio detector
        MidBudgetHopeless,           // forced ru-dominant mid-budget probe
        RestartStagnationThreshold,  // restart-to-restart stagnation guard
        HappyBreakdown,              // Arnoldi early/happy breakdown exit
        NanRetryExhausted,           // NaN failures exceeded max retries
        MaxBudget                    // ran the full restart budget
    };
    static const char* krylov_termination_reason_name(KrylovTerminationReason r) {
        switch (r) {
            case KrylovTerminationReason::InitialConverged:           return "initial_converged";
            case KrylovTerminationReason::ToleranceReached:           return "tolerance_reached";
            case KrylovTerminationReason::InternalConvergenceStop:    return "internal_convergence_stop";
            case KrylovTerminationReason::ArnoldiStagnation:          return "arnoldi_stagnation";
            case KrylovTerminationReason::MidBudgetHopeless:          return "mid_budget_hopeless";
            case KrylovTerminationReason::RestartStagnationThreshold: return "restart_stagnation_threshold";
            case KrylovTerminationReason::HappyBreakdown:             return "happy_breakdown";
            case KrylovTerminationReason::NanRetryExhausted:          return "nan_retry_exhausted";
            case KrylovTerminationReason::MaxBudget:                  return "max_budget";
        }
        return "unknown";
    }

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
        bool stagnation = false;   // terminated by a stagnation guard (either
                                   // detector — see termination_reason)
        // PR 8.1 (review P1): exact termination metadata.
        KrylovTerminationReason termination_reason =
            KrylovTerminationReason::MaxBudget;
        int probe_j = -1;                 // Arnoldi j at the deciding check (-1 = n/a)
        float probe_true_err = -1.0f;     // true relative error at that check
        float probe_hopeless_floor = -1.0f;  // max(0.9, 2*tol) when probed
        float stag_ratio_used = -1.0f;    // configured stagnation ratio
        int stag_count_final = 0;         // consecutive stagnating checks seen
        // R13.9: the LEFT WEIGHT this solve actually minimised under.
        //
        // FGMRES minimises ||D^-1 (b - A M^-1 z)||, and D is built from the initial residual
        // inside this function. A caller comparing two configurations therefore cannot form
        // that objective without D -- and reconstructing it from the same rule is the
        // duplicate-authority defect this tree has paid for repeatedly. The solver publishes
        // the weight it used; the caller applies it. Empty when block scaling is off, where
        // D = I.
        // R13.9: ||r0||/||b|| -- the SAME ratio at j=0. On a warm start x0 != 0 this can exceed
        // 1, and then rel_error > 1 after the solve means only that it began above 1, not that
        // the solve diverged. Divergence is rel_error > initial_rel_error, never rel_error > 1.
        // -1 when not measured. At the END of the struct: the aggregate initialisers at every
        // return site are positional.
        float initial_rel_error = -1.0f;
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
    
    // The stage gate's residual weighting, CAPTURED by the caller and handed over frozen, so a
    // diagnostic inside the solve can judge A*P^-1 in the metric that decides convergence rather
    // than inventing a second one. Built by the caller because that is where the gate's config
    // lives; delivered here rather than through three solve_stage overloads and solve_fgmres.
    //
    // Frozen, not referenced: FrozenStageWeights holds computed weights in a detached private
    // copy, so nothing the caller does afterwards can move them. It is stage-stamped, so a
    // weighting frozen for one stage is refused for another.
    void set_stage_weights(wrf::sdirk3::FrozenStageWeights weights);

    // This solver's process-unique id. A stage weighting is stamped with the id of the solver it
    // is FOR, not the object that built it: the caller has its own id from the same counter, so
    // stamping that one produced an identity that was correct for the producer and never matched
    // the consumer -- the probe stayed silent until this was measured.
    std::uint64_t solver_id() const;

    // R13 B1/B2: the state this solver carries ACROSS solves.
    //
    // A probe that runs the same solve twice and compares is measuring a derivative only if
    // both runs started from the same solver state. They do not by default: hopeless-budget
    // streaks, a trust radius, a preconditioner fallback latch and per-stage warm-start slots
    // all persist, and every one of them changes what the next solve does. R12 R4 is the worked
    // case -- a "certifying" second reference warm-started from the first and returned it
    // unchanged, and ref_agree=0 was read as agreement rather than as the signature of a
    // solve that never independently happened.
    //
    // Snapshot and restore rather than a fresh solver, deliberately and with the limit stated:
    // this covers the solver's own carried state, NOT the preconditioner's internals or any
    // cached linearization it holds. Arms isolated this way are more independent than shared
    // ones and less independent than separate processes. A digest is provided so a caller can
    // FAIL CLOSED when something outside the list moved, rather than assume it did not.
    struct CarriedState {
        bool  stage3_warmstart_disabled = false;
        bool  stage2_hopeless_budget_mode = false;
        int   stage2_hopeless_streak = 0;
        bool  stage3_hopeless_budget_mode = false;
        int   stage3_hopeless_streak = 0;
        int   precond_fallback_count = 0;
        float trust_radius = 0.0f;
        std::vector<torch::Tensor> warmstart_stage;
        std::vector<float>         warmstart_relerr;
    };
    CarriedState capture_carried_state() const;
    void restore_carried_state(const CarriedState& s);
    // Cheap order-sensitive fingerprint of the same state, for records and equality checks.
    std::uint64_t carried_state_digest() const;

    /**
     * Get convergence statistics
     */
    // R13.10 (red team P0-1): the per-solve reset, as a FREE FUNCTION over the struct.
    //
    // reset_stats() cleared the nine fields it had in 2025 and none of the ten added since
    // (R13.2/R13.5/R13.8), so best_krylov_rel_error, the GMRES counters, krylov_diverged,
    // accepted/rejected steps and initial_residual_measured accumulated for the LIFE OF THE
    // SOLVER -- one object per run. Every first-failure classification after the first failure
    // in a run was reading the run's history, not the stage's. Seventh occurrence of "a rule
    // computed and its consumer reading something else": the classifier reads per-stage,
    // the struct said per-stage, and nothing made it per-stage.
    //
    // A free function over the struct is testable; a private method on the Impl was not, which
    // is how ten fields went unreset. Everything in ConvergenceStats is per-solve; run-lifetime
    // state lives on the Impl and is reset (or deliberately not) there.
    struct ConvergenceStats;
    static void reset_per_solve(ConvergenceStats& s);

    struct ConvergenceStats {
        int newton_iterations;
        int total_krylov_iterations;
        std::vector<float> newton_residuals;
        float final_residual;
        float initial_unscaled_residual;  // v20.14r39: L2 ||R_0|| at Newton iter 0 (for diagnostics)
        torch::Tensor initial_residual_vector;  // Detached packed R_0 used by WRMS stage-gate growth metric
        float condition_number;
        bool converged;
        // R13.2 (first-failure classification): the signals that separate "Newton diverged"
        // from "the LINEAR solve never solved" from "every step was rejected". They are all
        // locals inside solve_stage_impl already; without them on the record the stage gate
        // sees only converged=0 and every failure looks the same, which is why a dt sweep has
        // been the only available experiment. Defaults keep every existing aggregate
        // initialisation valid.
        //
        // best_krylov_rel_error: the SMALLEST relative error any GMRES call reached in this
        // stage. Best rather than last, because one solve that worked disproves "the linear
        // solve cannot make progress" even if a later one stalled.
        float best_krylov_rel_error = -1.0f;
        int   gmres_total_failures = 0;
        // NOT successes: counts solves that were not TOTAL failures.
        int   gmres_non_total_failures = 0;
        // Solves that actually reached tolerance.
        int   gmres_tolerance_reached = 0;
        bool  krylov_diverged = false;
        int   accepted_steps = 0;
        int   rejected_steps = 0;
        // R13.5: MEASURED, then FINITE -- in that order, and both default false.
        //
        // The tile layer used to derive R0_finite as isfinite(initial_unscaled_residual). That
        // member initialises to 0.0, which is finite, so a solve that threw or exited before
        // ever evaluating R0 reported R0_finite=1: absence of a measurement printed as positive
        // evidence, which is the failure this project keeps closing elsewhere and reproduced
        // here. `initial_residual_finite` existed on this struct and nothing wrote it.
        bool  initial_residual_measured = false;
        bool  final_residual_measured = false;
        bool  initial_residual_finite = false;
        // The budget the Newton LOOP actually used. It reads options_.max_newton_iter; the
        // record was reading g_sdirk3_config.max_newton_iter, a second authority that a
        // stage-reference probe or a post-construction config change can move independently.
        int   newton_iteration_budget = -1;
        // R13.11 (referee C7): FIRST-EVENT iteration indices, so "first failure" can be a
        // measurement of time order rather than a fixed precedence over aggregates. -1 =
        // never happened in this solve.
        int   first_krylov_failure_iter = -1;
        int   first_rejection_iter = -1;
        int   argmin_residual_iter = -1;
        float min_residual_seen = std::numeric_limits<float>::infinity();
        // R13.11 (referee C7): both readings of the production total-failure predicate, so the
        // record shows when the ||b|| rule and the r0 rule disagree.
        int   total_failure_vs_b_count = 0;
        int   total_failure_vs_r0_count = 0;
        // Which rule the production predicate was RUNNING under, so a reader of the record
        // does not have to know the env var to read the two counts above.
        bool  krylov_failure_vs_r0 = false;
        // R13.12 (red team R3-2): the BEST relative error measured against where each solve
        // STARTED, not against ||b||. `best_krylov_rel_error` is ||r||/||b||, and the
        // classifier's no-progress clause read it under a comment claiming "ended where it
        // began" -- true only on a cold start, where r0 == b. On the em_b_wave warm start
        // (r0/||b|| = 1.054) a solve that reduced the residual by 3% still reads 1.02 and
        // trips a >= 0.99 test. -1 = not measured.
        float best_krylov_rel_error_vs_r0 = -1.0f;
        // PR 9E (diagnosis-only): RAW L2 norms at the FINAL accepted Newton
        // iteration, populated ONLY when g_sdirk3_config.stage_operand_diag is
        // on (else left at -1). final_fast_rhs_norm = ||F_fast(U_eval_final)||;
        // final_defect_l2_raw = ||K_final - F_fast(U_eval_final)|| (the raw
        // Newton defect). These OBSERVE F/R that the solve already built — no
        // extra RHS evaluation — and never influence the solve. Consumed by the
        // PR 9E stage-operand history summary.
        float final_fast_rhs_norm = -1.0f;
        float final_defect_l2_raw = -1.0f;
        // PR 9F (diagnosis-only): the COHERENT {K, F, R} triple captured
        // ATOMICALLY at the final residual evaluation (defect_R == defect_K -
        // defect_F bit-exactly, so ||K-F-R|| == 0 by construction), plus the
        // evaluation-point identifiers. Populated only under stage_operand_diag at
        // record stages; empty tensors / -1 otherwise. The stage-operand emitter
        // computes ||K-F-R||, ||R||, and the ratio DIRECTLY from these tensors
        // (the scalar norms above are telemetry only) and fails closed
        // (DEFECT_UNOBSERVED) unless defect_K equals the stage value the caller
        // actually returned -- i.e. the F/R belong to the returned evaluation
        // point, not a later trust-region/step update.
        torch::Tensor defect_K;
        torch::Tensor defect_F;
        torch::Tensor defect_R;
        int defect_newton_iter = -1;
        int defect_retry_generation = -1;
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
// 9F.D93: StateLayout is a COMPLETE type now (wrf_sdirk3_state_layout.h), included at the
// top of this header. It used to be forward-declared here and defined in newton_solver.cpp,
// which made it unusable outside that one translation unit -- the reason five other places
// grew their own copy of the block arithmetic.

// PR 9A: detached clones of the actual Krylov basis of one solve, exported
// for the opt-in directional consistency checker: V = Arnoldi basis, Z = the
// preconditioned vectors Z_j = M_j^{-1} V_j the operator was actually applied
// to. Only populated when a non-null pointer reaches solve_fgmres.
struct KrylovBasisCapture {
    std::vector<torch::Tensor> V;
    std::vector<torch::Tensor> Z;
    // PR 9B: actual in-situ operator outputs of the live Arnoldi loop.
    // A_Z[j] is the operator output w = A(Z_j) exactly as the solve used it
    // (in the space FGMRES iterates); J_w[j] is the raw production JVP
    // J*(dt*gamma*S*Z_j) computed inside that same matvec (unscaled space).
    // Probe / true-residual operator applications are NOT captured.
    std::vector<torch::Tensor> A_Z;
    std::vector<torch::Tensor> J_w;
    // Armed by solve_fgmres around exactly the Arnoldi operator application
    // so the operator implementation can attribute its JVP capture.
    bool arnoldi_call_active = false;
};

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
        bool periodic_y = false,
        KrylovBasisCapture* basis_capture = nullptr,
        // The stage gate's frozen weighting, for the opt-in A*P_j^-1 defect readout. DEFAULTED:
        // this is a diagnostic input, and existing callers -- including the standalone FGMRES
        // contract test, which links against this symbol directly -- must keep compiling and
        // linking untouched.
        const wrf::sdirk3::FrozenStageWeights* stage_weights = nullptr,
        // S, the map from the SCALED coordinates this loop iterates back to physical ones.
        // Null when scaling is inactive, in which case S = I. Without it a physically-weighted
        // defect would be computed on scaled vectors, which is only correct when S = I.
        const torch::Tensor* krylov_to_physical = nullptr,
        // R13.9: OUT -- the left weight D this solve minimised under, published so a caller
        // comparing two configurations can form FGMRES's OWN objective without rebuilding D
        // from the same rule (a second copy of a convention is how the two drift apart).
        // Empty when block scaling is off, where D = I.
        torch::Tensor* d_inv_out = nullptr
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
