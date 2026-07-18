#ifndef UNIFIED_PRECONDITIONER_ENHANCED_H
#define UNIFIED_PRECONDITIONER_ENHANCED_H

#include <torch/torch.h>
#include <memory>
#include <cstdint>
#include <atomic>  // OPT Pass33+: For diagnostic sampling counter
#include <limits>  // PR 9D: sentinel NaN for the W-damping policy signature
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_wdamp_preconditioner_policy.h"  // PR 9D: WdampPreconditionerSignature

namespace wrf {
namespace sdirk3 {

// Forward declarations
struct WRFGridInfo;
class PhysicsConfig;

/**
 * Enhanced Unified Preconditioner for SDIRK3 with Acoustic-Gravity Wave Coupling
 * 
 * Purpose: Accelerate Newton-Krylov convergence for the unified implicit system
 *          with proper handling of BOTH acoustic and gravity waves
 * 
 * Key Features:
 * - Acoustic wave coupling (cs² terms, ~340 m/s)
 * - Gravity wave coupling (N² terms, ~100 m/s) 
 * - W-Theta buoyancy coupling for gravity oscillations
 * - Variable-specific diagonal coefficients
 * - Block tridiagonal solver for coupled system
 * 
 * This preconditioner approximates (I - dt*gamma*J_unified)^{-1}
 * where J_unified includes both acoustic and gravity wave physics.
 *
 * IMEX scope gating (2026-02-01): When precond_match_rhs=true and
 * imex_split_mode>=1 (frozen or post-solve), diagonal terms are auto-gated
 * to match the fast-only Jacobian scope (Newton J is fast-only in both modes
 * because F_exp is frozen/detached in mode=1, absent in mode=2).
 * Slow terms (Rayleigh, vdiff) are excluded unless precond_extra_* flags
 * force them back in. Coefficients sourced from g_sdirk3_config (not
 * PhysicsConfig) for RHS/precond consistency.
 */
class UnifiedPreconditioner : public WRFPreconditioner {
public:
    /**
     * Constructor
     * @param grid_info WRF grid information (includes base state)
     * @param physics_config Physics configuration
     * @param dt Time step
     * @param gamma SDIRK3 diagonal coefficient
     */
    UnifiedPreconditioner(
        std::shared_ptr<WRFGridInfo> grid_info,
        std::shared_ptr<PhysicsConfig> physics_config,
        float dt, float gamma
    );

    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: std::atomic diagnostic sampling counters (precond_diag_call_counter_, precond_heavy_call_counter_)
    UnifiedPreconditioner(const UnifiedPreconditioner&) = delete;
    UnifiedPreconditioner& operator=(const UnifiedPreconditioner&) = delete;
    UnifiedPreconditioner(UnifiedPreconditioner&&) = delete;
    UnifiedPreconditioner& operator=(UnifiedPreconditioner&&) = delete;

    /**
     * Apply enhanced preconditioner M^{-1} to residual
     * @param residual Input residual vector
     * @return Preconditioned residual
     */
    torch::Tensor apply(const torch::Tensor& residual) override;
    
    /**
     * Update preconditioner if parameters change
     * @param state Current state (for state-dependent preconditioning)
     * @param dt New time step
     * @param gamma New SDIRK3 coefficient
     */
    void update(const torch::Tensor& state, float dt, float gamma) override;
    
    // v20.3: Adaptive α - Newton solver sets progress ratio before each GMRES
    void set_newton_residual_ratio(float ratio) override {
        newton_residual_ratio_ = ratio;
    }

    // v20.14 r46g: Newton iteration index for dynamic cap schedule
    void set_newton_iteration(int iter) override {
        newton_iteration_index_ = iter;
    }

    // v20.14 r47c-fix2: ru_share from BLOCK_RES for adaptive Phase2 scaling
    // v20.14 r47c-fix3: Also computes du_scale_ for dual-phase D_u strategy.
    // Defined in .cpp (needs g_sdirk3_config, not available in header).
    void set_newton_ru_share(float ru_share) override;

    /**
     * v20.5: Set stage-specific state for preconditioner adaptation
     * When mu_pert changes significantly between stages, the preconditioner
     * coefficients need to be recomputed to avoid GMRES stagnation.
     * Internally computes mu_full = mu_base + mu_pert using grid_info_.
     * @param mu_pert 2D tensor of column mass perturbation (j,i) from state vector
     * @param stage SDIRK3 stage number (1, 2, or 3)
     */
    void set_stage_state(const torch::Tensor& mu_pert, int stage);

    /**
     * v20.14: Set theta acoustic factor (for adaptive tuning).
     * Updates the instance-local cached value and immediately triggers
     * coefficient refresh (initialize_acoustic_gravity_solver + horizontal_smoother).
     * Sets override flag so subsequent init calls won't clobber the value.
     * Thread-safe: does not modify global config.
     * @param factor New theta acoustic factor [0.0, 0.35]
     */
    void set_theta_acoustic_factor(float factor);

    /**
     * v20.14: Clear override, restore config theta, and refresh coefficients.
     * Ensures immediate effect (no deferred update needed).
     */
    void clear_theta_acoustic_override();

    /** v20.14: Get current instance-local theta acoustic factor. */
    float get_theta_acoustic_factor() const { return theta_acoustic_factor_cached_; }

    /**
     * Get condition number estimate (for diagnostics)
     */
    float estimate_condition_number() const { return condition_estimate_; }
    
private:
    // Grid and physics info
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<PhysicsConfig> physics_config_;
    
    // Time integration parameters
    float dt_;
    float gamma_;
    bool dt_received_update_ = false;  // v20.14 r46e: true after first update() call
    int newton_iteration_index_ = 0;   // v20.14 r46g: set by Newton solver for dynamic cap
    int cross_downgrade_logged_newton_ = -1;  // v20.14 r47c-fix2: rate-limit auto-downgrade log
    int phase2_summary_logged_newton_ = -1;  // v20.14 r47c-fix2: once-per-Newton summary log
    float newton_ru_share_ = 0.0f;    // v20.14 r47c-fix2: ru fraction from BLOCK_RES
    float du_scale_ = 1.0f;           // v20.14 r47c-fix3: D_u scaling for dual-phase (1.0=normal)
    
    // === ENHANCED VERTICAL SOLVER COMPONENTS ===
    
    // Variable-specific diagonal coefficients
    torch::Tensor vertical_diag_w_;      // W-momentum (acoustic + gravity)
    torch::Tensor vertical_diag_theta_;  // Potential temperature (gravity)
    torch::Tensor vertical_diag_mu_;     // Column mass (acoustic)
    torch::Tensor vertical_diag_phi_;    // Geopotential (hydrostatic)
    torch::Tensor vertical_diag_u_;      // U-momentum (acoustic)
    torch::Tensor vertical_diag_v_;      // V-momentum (acoustic)

    // Off-diagonal arrays for each variable
    torch::Tensor vertical_upper_w_;
    torch::Tensor vertical_lower_w_;
    torch::Tensor vertical_upper_theta_;
    torch::Tensor vertical_lower_theta_;

    // W-Theta coupling arrays for gravity waves
    torch::Tensor w_theta_coupling_upper_;
    torch::Tensor w_theta_coupling_lower_;
    torch::Tensor theta_w_coupling_upper_;
    torch::Tensor theta_w_coupling_lower_;

    // U-V-μ coupling coefficients for acoustic waves (per-level scalars)
    torch::Tensor C_u_mu_;    // Pressure gradient effect: μ → u
    torch::Tensor C_v_mu_;    // Pressure gradient effect: μ → v
    torch::Tensor C_mu_u_;    // Divergence effect: u → μ
    torch::Tensor C_mu_v_;    // Divergence effect: v → μ

    // Phase 4.1: Φ coupling coefficients for 4×4 acoustic block (per-level scalars, w-staggered)
    torch::Tensor C_u_phi_;   // Pressure gradient effect: Φ → u  (1/μ₀ scaling)
    torch::Tensor C_v_phi_;   // Pressure gradient effect: Φ → v  (1/μ₀ scaling)
    torch::Tensor C_phi_u_;   // Divergence effect: u → Φ  (c²/μ₀ scaling)
    torch::Tensor C_phi_v_;   // Divergence effect: v → Φ  (c²/μ₀ scaling)
    torch::Tensor C_phi_mu_;  // Hydrostatic balance: μ → Φ  (c² scaling)
    // NOTE: A_μΦ = A_Φμ (symmetric) for stable Schur complement

    // Brunt-Väisälä frequency squared at each level
    torch::Tensor N_squared_;

    // Sound speed squared at each level (for acoustic coupling)
    torch::Tensor c_sound_squared_;
    
    // Horizontal smoothing parameters
    float horizontal_smooth_factor_;
    int n_smooth_iters_ = 3;
    
    // Condition number estimate
    mutable float condition_estimate_ = 1.0f;

    // v20.3: Newton residual ratio for adaptive α
    // Set by Newton solver via set_newton_residual_ratio() before each GMRES.
    // 1.0 = first Newton iter, → 0 as Newton converges.
    float newton_residual_ratio_ = 1.0f;

    // Cache invalidation generation counter
    // Incremented whenever coefficients change (update, initialize_acoustic_gravity_solver)
    uint64_t coefficient_generation_ = 0;

    // v20.5: Cached per-k coefficients for Φ-W GS correction
    std::vector<float> momentum_coupling_k_cached_;   // [nz_w], from initialize_acoustic_gravity_solver
    std::vector<float> dz_effective_cached_;           // [nz], from initialize_acoustic_gravity_solver
    uint64_t gs_cache_generation_ = 0;                 // tracks coefficient_generation_ at cache time
    float mu_representative_cached_ = 88000.0f;         // v20.14r27t: mu used for mc_k computation
    float mu_scale_correction_ = 1.0f;                  // v20.14r27t: mu_full_mean / mu_representative

    // v20.13/v20.14: Cached preconditioner tuning parameters.
    // Refreshed in initialize_acoustic_gravity_solver() instead of static-cached in apply().
    // Override flags prevent initialize_acoustic_gravity_solver() from clobbering setter values.
    float w_acoustic_boost_cached_ = 2.0f;
    float theta_acoustic_factor_cached_ = 0.0f;
    float uv_vertical_fraction_cached_ = 0.01f;  // v20.14r37: track for tuning_changed
    int cached_coupling_scale_ = -1;  // v20.14 r46: detect coupling_scale change
    float cached_dw_nosboost_floor_ = 0.1f;  // v20.14 r46h: detect dw_floor change for cache invalidation
    int no_correction_count_ = 0;    // v20.14 r46h: count apply() calls with no W←Φ correction
    float s_phi_phi_max_dev_ = 1.0f;  // v20.14 r46: S_ΦΦ/D_Φ deviation for phi-feedback guard
                                      // Sentinel 1.0 > 0.1 threshold → phi-feedback auto-disabled
                                      // until first valid S_phi_phi computation sets actual value
                                      // v20.14 r47c-fix: Now updated EVERY apply() (was generation-gated)
    float s_phi_mu_cross_ratio_ = 0.0f;  // v20.14 r47c-fix: |S_phi_mu|/|D_phi| cross-coupling mean
    float s_phi_mu_cross_max_ = 0.0f;   // v20.14 r47c-fix2: |S_phi_mu|/|D_phi| max (local spike detection)
    uint64_t skip_seen_gen_ = 0;      // v20.14 r45c: suppress repeated SKIP logs per generation
    bool precond_params_initialized_ = false;
    bool theta_factor_override_active_ = false;  // v20.14: set by set_theta_acoustic_factor()
    uint32_t theta_override_config_gen_ = 0;     // v20.14: config generation at override time

    // v20.5: Stage-specific state for adaptive preconditioner
    // When set, mu_full_stage_ is used instead of mu_base for coefficient computation
    torch::Tensor mu_full_stage_;  // 2D (j,i) mean across k, or empty if not set
    int current_stage_ = 0;        // SDIRK3 stage (1, 2, 3), 0 = not set
    // stage_state_dirty_ removed in v20.14 (was set but never read)

    // Cached base state generation counter from grid_info_
    // Used to detect when setBaseState() has provided fresh mub/th_base
    uint64_t cached_base_state_generation_ = 0;

    // FIX 2025-12-31 Batch41 Issue 1: Cached ScalarMeanCache epoch
    // Detects when map factors (msfty), mub, c1f/c2f, or mu_base have been invalidated
    uint64_t cached_scalar_mean_epoch_ = 0;

    // Preconditioner-RHS scope consistency (2026-02-01)
    // Effective IMEX precond scope: 0=all, 1=all(frozen), 2=fast-only.
    // When scope or precond flags change, diagonal terms must be recomputed.
    int cached_precond_scope_ = -1;  // -1 = not yet initialized
    // Packed bitmask of precond config flags for change detection:
    // bit 0: precond_match_rhs, bits 1-4: precond_extra_{rayleigh,wdamp,vdiff,divergence}
    // bit 5: precond_coupled_phi_w (v20.14 Phase 2)
    uint8_t cached_precond_flags_ = 0xFF;  // 0xFF = sentinel (never matches initial config)
    // PR 9D: W-damping policy fingerprint. The bitmask above already captures
    // the precond_extra_wdamp toggle; this additionally captures a w_damp_alpha
    // change WHILE the extra regularization is on (normalized_extra_alpha is 0
    // when extra is off, so an alpha change with extra OFF is a no-op) and the
    // rhs_config_enabled state (telemetry). Sentinel true/true/NaN never
    // matches the first resolved policy, forcing an initial build.
    wrf::sdirk3::WdampPreconditionerSignature cached_wdamp_signature_{
        true, true, std::numeric_limits<float>::quiet_NaN()};

    // =========================================================================
    // OPT Pass33+: DIAGNOSTIC SAMPLING COUNTERS (INDEPENDENT)
    // =========================================================================
    // Separate counters for standard (debug_level >= 1) and heavy (debug_level >= 3).
    // This prevents phase coupling when periods differ and ensures heavy sampling
    // starts from call #1 when debug_level is raised to 3 mid-run.
    // Pattern: (period == 0) || ((counter % period) == 0) || (counter == 1)
    // =========================================================================
    mutable std::atomic<uint64_t> precond_diag_call_counter_{0};       // Standard diagnostic counter
    mutable std::atomic<uint64_t> precond_heavy_call_counter_{0};      // Heavy diagnostic counter

    // v20.14 Phase 2a: Generation-based diagnostic throttle (instance-scoped)
    // Uses target_gen = coefficient_generation_ + 1 (post-increment alignment).
    // FIX r45c: SKIP does NOT set logged_gen (leaves retry open for next gen).
    // Success: sets logged_gen AND increments cap counter.
    uint64_t coupled_diag_logged_gen_ = 0;   // 3a landscape: last logged target generation
    int coupled_diag_log_count_ = 0;         // successful log count (cap: 2@debug=1, 10@debug>=2)
    uint64_t coupled_diag_4x4_gen_ = 0;      // 3b 4x4 ratio: last logged generation

    // v20.14 r46: Cached phi-feedback coefficients.
    // Recomputed when coefficient_generation_ changes.
    // All vectors sized nz_w (= nz+1), indexed by k.
    std::vector<float> phi_w_coupling_wph_;    // A_wφ[k]: coupling (positive)
    std::vector<float> phi_w_coupling_det_;    // det[k] = D_φ*D_w + A²
    std::vector<float> phi_w_D_w_nosboost_;    // D_W_nosboost[k]
    uint64_t phi_w_cached_gen_ = 0;            // generation for cache validity

    // === ENHANCED METHODS ===
    
    /**
     * Initialize acoustic-gravity wave vertical solver
     * Computes N², sets up variable-specific diagonals and coupling terms
     */
    void initialize_acoustic_gravity_solver();
    
    /**
     * Initialize horizontal smoother for acoustic modes
     */
    void initialize_horizontal_smoother();
    
    /**
     * Apply enhanced vertical solve with acoustic-gravity coupling
     */
    torch::Tensor apply_enhanced_vertical_solve(const torch::Tensor& r);
    
    /**
     * Apply horizontal smoothing iterations
     */
    torch::Tensor apply_horizontal_smoothing(const torch::Tensor& r);
    
    /**
     * Apply physical scaling based on variable type
     */
    torch::Tensor apply_physical_scaling(const torch::Tensor& r);

    // LEGACY solve_coupled_block_tridiagonal() REMOVED
    // Now using unified solve_coupled_w_theta_column() for both 1D and 4D paths

    /**
     * Solve tridiagonal system with variable-specific diagonal
     * @param rhs Right-hand side
     * @param diag Variable-specific diagonal coefficients
     * @return Solution vector
     */
    torch::Tensor solve_tridiagonal_with_variable_diag(
        const torch::Tensor& rhs,
        const torch::Tensor& diag
    );
    
    /**
     * Standard Thomas algorithm for tridiagonal system
     */
    torch::Tensor solve_tridiagonal(
        const torch::Tensor& lower,
        const torch::Tensor& diag,
        const torch::Tensor& upper,
        const torch::Tensor& rhs
    );

    /**
     * Coupled W-theta solver for gravity wave modes
     * Solves the 2x2 block system with buoyancy coupling
     */
    std::pair<torch::Tensor, torch::Tensor> solve_coupled_w_theta_column(
        const torch::Tensor& r_w,
        const torch::Tensor& r_theta,
        int nz,
        int nz_w
    );

    /**
     * Batched W-θ coupled solver: processes ALL (j,i) columns simultaneously.
     * Uses batched Thomas algorithm where tridiagonal coefficients are shared
     * across all columns and only RHS varies per column.
     * Input/output: 3D tensors [ny, nz_w/nz, nx]
     */
    void solve_coupled_w_theta_batched(
        torch::Tensor& w_block,       // [ny, nz_w, nx] in/out
        torch::Tensor& theta_block,    // [ny, nz, nx] in/out
        int nz,
        int nz_w,
        // v20.14 r47: Phase 2 Schur complement — optional Φ-W coupling.
        torch::Tensor* phi_block = nullptr,   // [ny, nz_w, nx] δφ, updated in-place
        const float* phi_diag = nullptr,       // D_phi[k], size ≥ nz_w
        const float* A_eff = nullptr,          // capped coupling[k], size ≥ nz_w
        int phase2_nz_w = 0                    // bounds for Phase 2 arrays
    );

    /**
     * v20.14 r46: Compute and cache phi-feedback coupling coefficients.
     * Returns true if usable interior coefficients exist.
     * Returns false if scale=0 + mc cache empty, or other prerequisite missing.
     */
    bool compute_phi_w_coupling_coefficients(int nz, int nz_w);

    /**
     * Coupled U-V-μ solver for acoustic wave modes
     * Solves 3x3 block system per level (no vertical coupling for now)
     *
     * Physics:
     *   ∂u/∂t ~ -(c²/ρ₀) ∂μ/∂x   (pressure gradient)
     *   ∂v/∂t ~ -(c²/ρ₀) ∂μ/∂y
     *   ∂μ/∂t ~ -ρ₀ (∂u/∂x + ∂v/∂y)   (mass continuity)
     *
     * Per-level 3×3 system:
     *   [ D_u    0    C_uμ ] [ u ]   [ r_u ]
     *   [  0    D_v   C_vμ ] [ v ] = [ r_v ]
     *   [ C_μu  C_μv   D_μ ] [ μ ]   [ r_μ ]
     *
     * @return tuple of (u_solution, v_solution, mu_solution)
     */
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
    solve_coupled_uv_mu_column(
        const torch::Tensor& r_u,
        const torch::Tensor& r_v,
        const torch::Tensor& r_mu,
        int nz
    );

    /**
     * PERFORMANCE: Zero-allocation raw-pointer overload for 4D path
     * Solves U-V-μ coupled system directly with pointers
     *
     * @param r_u_data Input U residual (nz floats)
     * @param r_v_data Input V residual (nz floats)
     * @param r_mu_data Input MU residual (nz floats)
     * @param u_sol Output U solution (nz floats, preallocated)
     * @param v_sol Output V solution (nz floats, preallocated)
     * @param mu_sol Output MU solution (nz floats, preallocated)
     * @param nz Number of vertical levels
     */
    void solve_coupled_uv_mu_column_inplace(
        const float* r_u_data,
        const float* r_v_data,
        const float* r_mu_data,
        float* u_sol,
        float* v_sol,
        float* mu_sol,
        int nz
    );

    /**
     * PHASE 4.1: Coupled U-V-μ-Φ solver for 4×4 acoustic block
     * Extends Phase 3 by adding geopotential (Φ) for pressure gradient stiffness
     *
     * Solves 4×4 block system using Schur complement:
     *   [ D_u    0      A_uμ    A_uΦ  ] [ δu ]   [ r_u ]
     *   [ 0      D_v    A_vμ    A_vΦ  ] [ δv ] = [ r_v ]
     *   [ A_μu   A_μv   D_μ     A_μΦ  ] [ δμ ]   [ r_μ ]
     *   [ A_Φu   A_Φv   A_Φμ    D_Φ   ] [ δΦ ]   [ r_Φ ]
     *
     * Algorithm:
     *   1. Eliminate U,V from μ and Φ equations
     *   2. Solve 2×2 Schur complement for (δμ, δΦ)
     *   3. Back-substitute to get (δu, δv)
     *
     * NOTE: μ is a SINGLE SCALAR per column (2D field), accumulated from all levels
     *       Φ is w-staggered (nz_w levels), solved at mass levels (nz)
     *
     * Fallback: If |det| < 1e-10, falls back to 3×3 U-V-μ + diagonal Φ
     *
     * @param r_u U residual (nz levels)
     * @param r_v V residual (nz levels)
     * @param r_mu MU residual (single scalar)
     * @param r_phi PHI residual (nz_w levels, w-staggered)
     * @param nz Number of mass levels
     * @param nz_w Number of w-staggered levels (nz+1)
     * @param mu_0_local Per-column base state mass (μ₀ for this j,i column)
     * @return tuple of (u_solution, v_solution, mu_solution, phi_solution)
     */
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor>
    solve_4x4_acoustic_block(
        const torch::Tensor& r_u,
        const torch::Tensor& r_v,
        const torch::Tensor& r_mu,
        const torch::Tensor& r_phi,
        int nz,
        int nz_w,
        float mu_0_local
    );

    /**
     * PERFORMANCE: Optimized in-place tridiagonal solver using raw pointers
     * Avoids tensor allocations for maximum performance
     */
    void solve_tridiagonal_inplace(
        const float* lower,
        const float* diag,
        const float* upper,
        const float* rhs,
        float* solution,
        int n
    );
};

// FIX 2025-12-30 Batch31 Issue 2: Invalidation function for ScalarMeanCache
// Called from WRFGridInfo::invalidateVerticalMetricCaches() when grid metrics change
void invalidateScalarMeanCache();

} // namespace sdirk3
} // namespace wrf

#endif // UNIFIED_PRECONDITIONER_ENHANCED_H