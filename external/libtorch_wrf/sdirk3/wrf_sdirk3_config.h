#ifndef WRF_SDIRK3_CONFIG_H
#define WRF_SDIRK3_CONFIG_H

#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include <string>

namespace wrf {
namespace sdirk3 {

/**
 * SDIRK3 Solver Configuration
 * 
 * This structure holds all configurable parameters for the SDIRK3 solver.
 * Default values are provided, but can be overridden through namelist or API.
 */
struct SDIRK3Config {
    // Newton-Krylov solver parameters
    int max_newton_iter = 50;  // Increased for stiff problems (was 4)
    float newton_tol = 0.2f;     // Scaled-RMS norm: ||S⁻¹R||/√N < 0.2 (per-DOF, grid-size independent)
    float newton_rtol = 0.01f;   // Relative criterion: converge when ||R||/||R₀|| < 1%
    
    // GMRES parameters
    // FIX (2025-12-04): Reduced budget for realistic inexact Newton
    // Old: restart=30, max_iter=100 → 3000 JVPs max (excessive)
    // New: restart=15, max_iter=3 → 45 JVPs max (practical for inexact Newton)
    int gmres_restart = 15;      // Reduced from 30 for faster stagnation detection
    int max_krylov_iter = 5;     // Increased from 3: 5 restarts × 15 = 75 JVPs max
    float krylov_tol = 0.1f;     // Relaxed from 1e-3 - inexact Newton allows loose GMRES

    // v20.14r48: True residual check frequency control.
    // Old: hess_est < 3*tol triggers expensive A(x_trial) check every time → JVP/Arnoldi ratio ~1.6.
    // New: periodic sampling (j >= start_j && j % period == 0, always check last j).
    // Expected: JVP/Arnoldi ratio → ~1.1, linear solve time ~30% reduction.
    // Set via env: WRF_SDIRK3_GMRES_TRUE_RESIDUAL_START_J, WRF_SDIRK3_GMRES_TRUE_RESIDUAL_PERIOD
    int gmres_true_residual_start_j = 12;  // min Arnoldi steps before first true_err check
    int gmres_true_residual_period = 8;    // check every N Arnoldi steps after start_j

    // v20.14r48: GMRES Arnoldi-level stagnation early termination.
    // If true_err ratio (current/previous) > threshold for K consecutive checks,
    // early-terminate restart cycle. Prevents 320 Arnoldi exhaustion when stagnant.
    // Different from gmres_stagnation_threshold (:1723) which is restart-level.
    // Set via env: WRF_SDIRK3_GMRES_ARNOLDI_STAG_WINDOW, WRF_SDIRK3_GMRES_ARNOLDI_STAG_RATIO
    int gmres_arnoldi_stag_window = 3;        // consecutive stagnant checks before early exit
    float gmres_arnoldi_stag_ratio = 0.995f;  // ratio > this = no progress

    // v20.14r48: Mixed FD strategy for JVP cost reduction.
    // Newton iter < threshold → forward diff (1 RHS, O(ε)).
    // Newton iter >= threshold → central diff (2 RHS, O(ε²)).
    // 0 = disabled (use jvp_use_forward_diff for all). -1 = always forward.
    // Set via env: WRF_SDIRK3_JVP_MIXED_FD_NEWTON_SWITCH
    int jvp_mixed_fd_newton_switch = 0;

    // v20.14 r49: Stage-aware GMRES budget.
    // stage2_*: when >0, override base values for stages >=2.
    // stage3_*: when >0, override stage2/base values for stages >=3.
    // When =0, keep inherited/base values (default-off, regression-neutral).
    // Set via env:
    //   WRF_SDIRK3_STAGE2_GMRES_RESTART / _MAX_KRYLOV_RESTARTS / _KRYLOV_TOL
    //   WRF_SDIRK3_STAGE3_GMRES_RESTART / _MAX_KRYLOV_RESTARTS / _KRYLOV_TOL
    int stage2_gmres_restart = 0;
    int stage2_max_krylov_restarts = 0;
    float stage2_krylov_tol = 0.0f;  // true override (=), not floor
    float stage2_ew_eta_min = 0.0f;  // true override (=0 means off)
    float stage2_ew_eta_max = 0.0f;  // true override (=0 means off)
    int stage3_gmres_restart = 0;
    int stage3_max_krylov_restarts = 0;
    float stage3_krylov_tol = 0.0f;  // true override (=), not floor
    float stage3_ew_eta_min = 0.0f;  // true override (=0 means off)
    float stage3_ew_eta_max = 0.0f;  // true override (=0 means off)

    // v20.14 r49: JVP auto-selection.
    // 0 = off. N > 0 = benchmark N calls each for forward-FD and central-FD.
    // Result locked to solver-local member (not g_sdirk3_config) for thread safety.
    // Set via env: WRF_SDIRK3_JVP_AUTO_BENCH_CALLS
    int jvp_auto_bench_calls = 0;
    bool jvp_auto_bench_quality_gate = false;  // if on, reject forward lock when eps-vs-halfeps mismatch is large
    int jvp_auto_bench_warmup = 0;             // warmup loops per method before timing
    int jvp_auto_bench_seed = 0;               // deterministic probe vector seed
    bool jvp_auto_bench_lock_reset_stage1 = false;  // default-off: keep lock persistent unless explicitly reset each stage-1

    // v20.14 r63: Solver telemetry and variable-preconditioner diagnostics (default-off).
    bool solver_telemetry = false;
    // v20.14 r64: Progress invariant tracker (default-off, regression-neutral).
    bool progress_invariant_enable = false;
    float progress_invariant_min_ratio = 0.0f;  // A=||dU||/(||dt*k1||+eps), <=0 disables
    int progress_invariant_streak_limit = 0;    // consecutive low-progress steps to flag
    bool variable_pc_event_log = false;

    // v20.14 r63: GMRES warm-start cache (default-off, regression-neutral).
    bool gmres_warmstart = false;
    float gmres_warmstart_quality_gate = 0.6f;  // use warm-start only when previous rel_error <= gate
    // INN-assisted warm-start controls (default-off, fail-safe).
    bool inn_warmstart_enable = false;
    bool inn_residual_gate_enable = false;
    float inn_q_min = 0.0f;            // q=(||r_base||-||r_inn||)/max(||r_base||,eps)
    float inn_gate_rel_tol = 1.0e-6f;  // allow tiny relative residual growth from numerical noise
    float inn_gate_q_noise = 5.0e-7f;  // q noise floor used when inn_q_min<=0
    float inn_beta_max = 0.25f;        // blend factor cap in [0, 1]
    bool inn_ood_guard_enable = false; // guard oversized corrections
    bool inn_tol_ramp_enable = false;  // tighten tol only on accepted INN proposal
    float inn_tol_ramp_gamma = 1.0f;   // tol <- gamma*tol, gamma in (0,1]
    bool rhs_bc_parity = false;                 // extra RHS boundary parity guards (default-off)
    bool mass_pgf_bc_guard = false;             // extra mass/PGF boundary guard corrections (default-off)

    // Preconditioner options
    int precond_type = 2;  // 0=None, 2=Unified block-Jacobi, 3=Multigrid
    
    // Adaptive timestep control
    bool adaptive_timestep = false;
    float safety_factor = 0.9f;
    float dt_min_factor = 0.1f;
    float dt_max_factor = 2.0f;
    float error_tol = 1e-4f;
    
    // Physics options
    bool use_physics = true;
    bool frozen_physics_jvp = false;
    
    // Performance options
    int n_threads = 0;  // LibTorch threads per tile (0 = auto-detect, default: use all available cores)
    bool use_autograd = false;  // Disabled for finite difference stability
    bool use_jvp_cache = true;
    
    // JVP computation method
    enum JVPMethod {
        JVP_FINITE_DIFF = 0,    // Traditional finite difference (default)
        JVP_AUTOGRAD = 1,       // PyTorch autograd
        JVP_DUAL_NUMBER = 2,    // Dual number approach
        JVP_OPTIMIZED = 3       // Optimized batched method
    };
    JVPMethod jvp_method = JVP_FINITE_DIFF;  // Use finite difference for stability in SDIRK3
    float jvp_epsilon = 1e-4f;  // Increased for better numerical stability in WRF scales
    bool jvp_use_forward_diff = false;  // Central diff (2 RHS) for O(ε²) accuracy; forward diff is O(ε)
    // v20.14r27q: Block-aware FD epsilon (EXPERIMENTAL, default off).
    // v20.14r27r: Current implementation computes J*delta (not J*v) because
    // per-block scaling distorts the direction. Needs redesign to preserve
    // direction while adapting scale. Keep off until then.
    // EXPERIMENTAL — DO NOT USE IN PRODUCTION (v20.14r27r).
    // Per-block epsilon scaling distorts the perturbation direction:
    // computes J * delta (block-rescaled) instead of J * v (original direction).
    // Needs redesign to preserve direction while scaling magnitude.
    bool jvp_block_epsilon = false;  // Set via env: WRF_SDIRK3_JVP_BLOCK_EPSILON

    // Advanced JVP Optimization Options
    bool jvp_checkpointing = false;        // Enable checkpointing for memory efficiency
    int jvp_checkpoint_segments = 4;       // Number of segments for checkpointing
    bool jvp_graph_caching = true;         // Cache computation graph for repeated JVPs
    bool jvp_batched = true;               // Use batched JVP for multiple vectors
    int jvp_batch_size = 8;                // Maximum batch size for JVP
    bool jvp_mixed_precision = false;      // Use mixed precision for JVP (FP16 compute, FP32 accumulate)
    
    // Selective Implicit/Explicit Treatment Options
    bool implicit_acoustic = true;         // Treat acoustic waves implicitly
    bool implicit_gravity = true;          // Treat gravity waves implicitly
    bool implicit_rayleigh = true;         // Treat Rayleigh damping implicitly
    bool implicit_wdamp = true;            // Treat W-damping implicitly
    bool implicit_vdiff = true;            // Treat vertical diffusion implicitly
    bool implicit_hdiff = false;           // Treat horizontal diffusion implicitly (usually explicit)
    bool implicit_divergence = true;       // Treat divergence damping implicitly
    
    // Preconditioner Optimization Options
    bool precond_diagonal_only = false;    // Use only diagonal preconditioner (faster, less accurate)
    bool precond_block_jacobi = true;      // Use block Jacobi preconditioner
    int precond_block_size = 0;            // Block size for block Jacobi (0=use fixed structure)
    bool precond_ilu = false;              // Use ILU preconditioner (more expensive)
    int precond_ilu_level = 0;             // ILU fill level
    bool precond_multigrid = false;        // Use multigrid preconditioner (most expensive)
    int precond_mg_levels = 3;             // Number of multigrid levels

    // Preconditioner Diagonal Tuning (config-driven, no code edits needed)
    // FIX (2025-11-30): Clamp range must match μ scale (~8.8e4 Pa for em_b_wave)
    // Old [100, 1000] was way too small, causing diagonal shrinkage → GMRES divergence
    bool precond_enable_clamps = true;              // Enable soft clamps (false = raw physics for debugging)
    float precond_momentum_clamp_min = 1e4f;        // Min momentum coupling - scaled for μ ~ 1e4-1e5 Pa
    float precond_momentum_clamp_max = 1e5f;        // Max momentum coupling - scaled for μ ~ 1e4-1e5 Pa
    float precond_damping_factor = 0.05f;           // Baseline damping to prevent diagonal=1.0
    bool precond_log_raw_values = true;             // Log raw physics before clamps (always useful)

    // Preconditioner Gauss-Seidel Adaptive Iteration Control
    int precond_gs_min_iterations = 2;              // Minimum Gauss-Seidel iterations (fast path)
    int precond_gs_max_iterations = 6;              // Maximum iterations when ratio > threshold
    float precond_gs_ratio_threshold = 0.9f;        // Switch to max_iterations if ||M⁻¹r||/||r|| > this
    float precond_gs_fast_threshold = 0.7f;         // Use min_iterations if first sweep achieves this
    // FIX (2025-11-30): Reduced from 0.70 to 0.25 to prevent diagonal erosion from coupling sum
    // FIX (2026-02-03): Further reduced from 0.25 to 0.1 to address GMRES stall from over-correction
    float precond_mu_coupling_damping = 0.1f;       // Damp U-V-μ coupling corrections - lower prevents over-correction
    // Preconditioner relaxation: z_final = ω * M⁻¹r + (1-ω) * r
    // When ratio ||M⁻¹r||/||r|| < 0.1, the preconditioner over-corrects, distorting
    // the error profile and causing GMRES stall. Relaxation softens M⁻¹ toward identity.
    // v20.3: Additive shift with adaptive decay. z = M⁻¹r + α_eff*r.
    // α_eff = α₀ * min(1, ||R||/||R₀||) decays from α₀ to 0 as Newton converges.
    // Prevents near-zero eigenvalues early (regularization), pure M⁻¹ later (accuracy).
    // v20.3: α₀ raised to 0.1 with adaptive decay α_eff = α₀ * min(1, ||R||/||R₀||).
    // At Newton start (ratio=1): full shift for robustness.
    // As Newton converges (ratio→0): pure M⁻¹ for accuracy.
    // v20.6: Set to 0 — the 0.1 additive shift was the dominant GMRES failure mode.
    // With operator spectral radius ~500K, the 0.1*I term gives M⁻¹·A eigenvalues
    // ~50K, overwhelming the Schur solve (which reduces to ~960). Pure M⁻¹ is required.
    float precond_relaxation = 0.0f;                // Additive shift α₀: z += α_eff*r (0=pure M⁻¹)

    // v20.14r27q: Φ→W GS correction damping coefficient.
    // The off-diagonal GS sweep w -= beta * A_wΦ * Δφ can amplify certain
    // Krylov vectors when beta=1.0 (full correction). Reducing beta damps
    // the correction while preserving linearity (constant coefficient).
    // Set via env: WRF_SDIRK3_PRECOND_GS_BETA
    float precond_gs_beta = 0.5f;  // 0=skip GS, 1=full correction

    // v20.14r27z: U/V acoustic diagonal vertical blend fraction.
    // D_u = 1 + dt*γ * c_s² * (rdx² + rdy² + fraction * dz_inv²)
    // fraction=0: pure horizontal (D_u≈1, physically correct but under-conditioned).
    // fraction=1: full vertical (D_u≈500, matches W-block conditioning).
    // Default 1.0: full vertical (prevents ru under-conditioning that causes GMRES stagnation).
    // Reduce via env WRF_SDIRK3_PRECOND_UV_VERTICAL_FRACTION if U/V over-damped.
    float precond_uv_vertical_fraction = 1.0f;

    // v20.14r39: UV fraction warmup ramp (DISABLED by default since r43).
    // r42 showed warmup=0.01 kills Stage 2/3 GMRES (D_u too weak, true_err=0.9955).
    // S1 is ru-dominated so D_u irrelevant; S2/S3 need full fraction.
    // Growth gate (r39) already prevents the false catastrophic abort that
    // originally motivated warmup. Keep 0=disabled as default.
    // Set via env: WRF_SDIRK3_UV_VFRAC_WARMUP_START (>0 to enable)
    float uv_vfrac_warmup_start = 0.0f;

    // Preconditioner defect-correction refinement passes (v20.14)
    // z = M⁻¹(v); for each extra pass: z += M⁻¹(v - A(z))
    // Each extra pass costs 1 JVP + 1 precond apply per Arnoldi step.
    // Set via env: WRF_SDIRK3_PRECOND_REFINEMENT_PASSES
    int precond_refinement_passes = 1;  // 1=baseline, 2=one defect correction

    // v20.14 r46: Per-level phi-feedback from W residual.
    // When true, δφ receives same-level correction from W residual via 2×2 formula,
    // then GS uses improved Φ for off-level W←Φ gradient correction.
    // Set via env: WRF_SDIRK3_PRECOND_COUPLED_PHI_W
    bool precond_coupled_phi_w = false;

    // v20.14 r46: Coupling scale for phi-feedback.
    // 0=GS (dt·γ·g/(mc·μ·dz)), 1=physics (dt·γ·g/dz), 2=acoustic (dt·γ·c_s/dz)
    // Set via env: WRF_SDIRK3_PRECOND_PHI_W_COUPLING_SCALE
    int precond_phi_w_coupling_scale = 0;  // default: GS-scale

    // v20.14 r46b: Phi-feedback relaxation factor.
    // φ_new = φ_old + η*(φ_fb - φ_old). η=1.0 = full replacement, η<1 = blending.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_RELAX
    float precond_phi_feedback_relax = 1.0f;  // default: full replacement

    // v20.14 r46b: Separate GS cap for coupled (phi-feedback) path.
    // -1 = use precond_gs_awphi_cap (legacy default). >=0 = override for coupled path.
    // Set via env: WRF_SDIRK3_PRECOND_GS_AWPHI_CAP_COUPLED
    float precond_gs_awphi_cap_coupled = -1.0f;  // default: use base cap

    // v20.14 r46e: Phi-feedback 2a coupling strength control.
    // beta scales A_wph in the 2×2 formula: A_eff = min(beta * A_raw, cap).
    // cap limits maximum effective coupling (-1 = disabled, >=0 = active).
    // Together with gs_beta/gs_cap for 2b, these control 2a/2b strength balance.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_BETA, _CAP
    float precond_phi_feedback_beta = 1.0f;   // default: no scaling
    float precond_phi_feedback_cap = -1.0f;   // default: disabled (2×2 self-regulates)

    // v20.14 r46e: D_W_nosboost floor for phi-feedback coefficient cache.
    // Prevents det → A² spike when Schur subtraction leaves D_W ≈ 0.
    // Set via env: WRF_SDIRK3_PRECOND_DW_NOSBOOST_FLOOR
    float precond_dw_nosboost_floor = 0.1f;   // default: conservative

    // v20.14 r46f: Correction magnitude limiter for phi-feedback 2a.
    // If ||correction||/||phi_old|| > max_corr, scale correction down.
    // -1 = disabled (default), >=0 = active limiter.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_MAX_CORR
    float precond_phi_feedback_max_corr = -1.0f;  // default: disabled

    // v20.14 r46f: Allow legacy GS fallback when coupled=ON but feedback not ready.
    // false = skip W←Φ entirely (r46e behavior, prevents operator discontinuity).
    // true = fall back to legacy GS (keeps W←Φ active even without phi-feedback).
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_FALLBACK_GS
    bool precond_phi_feedback_fallback_gs = false;  // default: skip (r46e)

    // v20.14 r46g: Dynamic cap schedule — N0 strong (cap_low), N2+ weak (cap_high).
    // cap_eff = cap + (cap_high - cap) * min(newton_iter / 2.0, 1.0)
    // -1 = disabled (use cap for all iterations).
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_CAP_HIGH
    //              WRF_SDIRK3_PRECOND_GS_AWPHI_CAP_COUPLED_HIGH
    float precond_phi_feedback_cap_high = -1.0f;   // default: disabled
    float precond_gs_awphi_cap_coupled_high = -1.0f;  // default: disabled

    // v20.14 r46g: Soft cap using tanh instead of hard min().
    // A_eff = cap * tanh(A_raw / cap) — smooth transition, no discontinuity.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_SOFT_CAP
    bool precond_phi_feedback_soft_cap = false;  // default: hard min()

    // v20.14 r46j: Cap schedule mode — how dynamic cap ramps from base→high.
    // 0 = iteration-based (t = min(N/2, 1)) — current default.
    // 1 = ratio-based (t = clamp(newton_residual_ratio, 0, 1)) — quality-driven.
    // 2 = combined (t = max(iter-based, ratio-based)) — most aggressive ramp.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_CAP_MODE
    int precond_phi_feedback_cap_mode = 0;  // default: iteration-based

    // v20.14 r46j: Stage-aware coupling — scale phi-feedback/GS strength for S2/S3.
    // Multiplied into phi_fb_beta and gs_beta when current_stage_ >= 2.
    // 1.0 = same for all stages (default). 0.0 = disable for S2/S3.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_STAGE_SCALE
    float precond_phi_feedback_stage_scale = 1.0f;  // default: same for all stages

    // v20.14 r46i: δw partial blend — post-Thomas W correction from 2×2 estimate.
    // δw_est = (D_phi * r_w - A_num * r_phi_eff) / det computed in 2a loop.
    // After Thomas: w_final = (1-κ)*w_thomas + κ*δw_est. κ=0 disables.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_DW_BLEND
    float precond_phi_feedback_dw_blend = 0.0f;  // default: disabled

    // v20.14 r46i: Internal 2-pass — iterate 2a→2b multiple times.
    // Pass 1: standard (δφ from W residual, then GS with improved Φ).
    // Pass 2+: δφ re-corrected from GS-updated W, then GS with further-improved Φ.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_FEEDBACK_PASSES
    int precond_phi_feedback_passes = 1;  // default: single pass

    // v20.14 r47: Phase 2 Schur A_eff cap.
    // A_eff[k] = min(A_wph[k], sqrt(cap * D_phi[k] * |vd_w[k]|))
    // Uses D_w_full (= vertical_diag_w_[k], same as Thomas vd_w).
    // cap=0.5 → A²/D_phi ≤ 0.5*vd_w. Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BOOST_CAP
    float precond_phi_w_schur_boost_cap = 0.5f;

    // v20.14 r47: Phase 2 Schur back-substitution relaxation.
    // phi += eta_bs * (A_eff/D_phi) * w_sol. eta_bs=1 is unrelaxed (full Schur).
    // Constant → GMRES stationary. Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BACKSUB_RELAX
    float precond_phi_w_schur_backsub_relax = 1.0f;

    // v20.14 r47c: Phase 2 ablation flags — independent on/off for each Schur operation.
    // Enables 2^3 = 8 combinations to isolate which operation drives improvement.
    // All constant → GMRES stationary.
    bool precond_phi_w_schur_boost_on = true;      // diagonal boost: vd_w += A²/D_phi
    bool precond_phi_w_schur_rhs_inject_on = true;  // RHS injection: rhs_w -= A*δφ
    bool precond_phi_w_schur_backsub_on = true;     // back-sub: δφ += (A/D_phi)*δw

    // v20.14 r47c-fix2: Phase 2 cross-coupling threshold for auto-downgrade.
    // |S_phi_mu|/|D_phi| max ratio. When exceeded, Phase2 is disabled (Schur diagonal
    // assumption violated). 0 = disabled (never gate on cross-coupling).
    // b_wave: cross≈0.7 constant. Set high enough to not disable for static geometry.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_CROSS_THRESH
    float precond_phi_w_schur_cross_thresh = 0.0f;  // 0=disabled

    // v20.14 r47c-fix2: Phase 2 N3 collapse adaptive mechanisms.
    // (1) Cap decay: cap_eff = cap * decay^newton_iter. Reduces diagonal boost as
    //     Newton progresses (ph subdued → boost less useful). 1.0=no decay, 0.5=halve each iter.
    //     Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_CAP_DECAY
    float precond_phi_w_schur_cap_decay = 1.0f;  // 1.0=disabled (no decay)

    // (2) ru_share scaling: A_eff *= max(0, 1 - scale*(ru_share - thresh)).
    //     When residual becomes ru-dominated (ru_share>thresh), weaken Phase2 coupling.
    //     0=disabled. Requires Newton solver to call set_newton_ru_share() before GMRES.
    //     Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_RU_SCALE, _RU_THRESH
    float precond_phi_w_schur_ru_scale = 0.0f;   // 0=disabled
    float precond_phi_w_schur_ru_thresh = 0.5f;   // ru_share threshold before scaling

    // (3) Staged alpha_gs: legacy GS only activates at newton_iter >= this value.
    //     Phase2 alone handles N0-N2, legacy GS supplements N3+.
    //     0=always active (original behavior). Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_ALPHA_GS_START
    int precond_phi_w_schur_alpha_gs_start = 0;  // 0=always

    // v20.14 r47c-fix3: (4) D_u weak scaling for ru-dominated residuals.
    // When ru_share > du_weak_ru_thresh, scale D_u in the 4×4 Schur by du_weak_factor.
    // Smaller D_u → larger U corrections → better ru reduction.
    // Previous r44c adaptive vfrac (full update()) caused 40× ph amplification without Phase2.
    // This approach: lightweight D_u-only scaling in 4×4, Phase2 simultaneously handles W/Φ.
    // 1.0=disabled (no scaling). Set via env: WRF_SDIRK3_PRECOND_DU_WEAK_FACTOR, _DU_WEAK_RU_THRESH
    float precond_du_weak_factor = 1.0f;      // 1.0=disabled
    float precond_du_weak_ru_thresh = 0.95f;   // ru_share threshold

    // v20.14 r49: Horizontal smoothing for 1D packed preconditioner.
    // Interior-only Jacobi on U/V blocks. Boundary rows/cols preserved.
    // Set via env: WRF_SDIRK3_PRECOND_HORIZONTAL_SMOOTH_ALPHA, _ITERS
    float precond_horizontal_smooth_alpha = 0.0f;  // 0 = off. ~0.1 recommended.
    int precond_horizontal_smooth_iters = 1;

    // v20.14 r49: Vertical smoothing for U/V blocks.
    // Boundary-aware Thomas: diag=1+α at k=0,nz-1; 1+2α interior.
    // Set via env: WRF_SDIRK3_PRECOND_VERTICAL_SMOOTH_ALPHA
    float precond_vertical_smooth_alpha = 0.0f;  // 0 = off. ~0.1 recommended.
    bool precond_smooth_boundary_guard = false;   // default-off: skip non-periodic boundary columns during vertical smoothing

    // v20.14 r47c: Phase 2 + legacy GS hybrid coefficient.
    // When > 0 and Phase2 active, run legacy GS AFTER Thomas with this scaling.
    // Restores off-level (k-k-1) Φ gradient correction that Phase2 suppresses.
    // Set via env: WRF_SDIRK3_PRECOND_PHI_W_SCHUR_ALPHA_GS
    float precond_phi_w_schur_alpha_gs = 0.0f;

    // v20.14r35: Near-fail GMRES floor (fraction of actual residual decrease required).
    // When GMRES rel_error is 0.99-0.999, require at least this fraction of actual decrease.
    // Set via env: WRF_SDIRK3_NEAR_FAIL_FLOOR
    float near_fail_floor = 0.003f;  // 0.3%

    // v20.14r35: Newton stall detection threshold (fraction).
    // If Newton residual decreased < this fraction for 2 consecutive iterations, exit early.
    // Must be <= near_fail_floor to avoid accepting steps then immediately stalling.
    // Set via env: WRF_SDIRK3_NEWTON_STALL_THRESHOLD
    float newton_stall_threshold = 0.003f;  // 0.3% (was hardcoded 5%)

    // v20.14r35: Catastrophic gate rel_R_full absolute cap (OR condition).
    // If rel_R_full alone exceeds this, catastrophic abort regardless of scaled_rms.
    // v20.14r36: OR-cap now also requires R_full_norm > abs_floor to prevent
    // false triggers when ||K|| is small (inflating rel_R_full).
    // Set via env: WRF_SDIRK3_CATASTROPHIC_REL_CAP, WRF_SDIRK3_CATASTROPHIC_ABS_FLOOR
    float catastrophic_rel_cap = 10000.0f;
    float catastrophic_abs_floor = 100.0f;  // R_full_norm must exceed this for OR-cap

    // v20.14r35: Explosion guard thresholds.
    // threshold = max(abs_floor, rel_multiplier * baseline_unscaled_rms).
    // Set via env: WRF_SDIRK3_EXPLOSION_ABS_FLOOR, WRF_SDIRK3_EXPLOSION_REL_MULTIPLIER
    float explosion_abs_floor = 1e4f;        // was hardcoded 1e6
    float explosion_rel_multiplier = 100.0f;  // was hardcoded 1000

    // v20.14r35: FD consistency check sample count.
    // Number of initial JVP calls on which to run eps vs eps/2 consistency check.
    // Each check costs 2 extra RHS evaluations. Set 0 to disable.
    // Set via env: WRF_SDIRK3_FD_CONSISTENCY_SAMPLES
    int fd_consistency_samples = 0;  // v20.14r48: default 0 (bench mode). Was 3. Set >0 to enable.

    // v20.14r36: Newton zero-step stagnation limit.
    // Break Newton early after this many consecutive zero-update iterations.
    // Set via env: WRF_SDIRK3_NEWTON_ZERO_STEP_STALL_LIMIT
    int newton_zero_step_stall_limit = 3;

    // v20.14r36: Φ→W GS coupling cap.
    // Hard cap on A_w_phi in GS sweep to prevent degenerate mc_k/dz_w blowup.
    // Set via env: WRF_SDIRK3_PRECOND_GS_AWPHI_CAP
    float precond_gs_awphi_cap = 1.0f;

    // v20.14r38: Stage gate/damp thresholds (were hardcoded constexpr).
    // stage_gate_rel_threshold: rel_R_full above this triggers stage failure evaluation.
    // stage_damp_rel_threshold: rel_R_full above this triggers post-damp K scaling.
    // Set via env: WRF_SDIRK3_STAGE_GATE_REL_THRESHOLD, WRF_SDIRK3_STAGE_DAMP_REL_THRESHOLD
    float stage_gate_rel_threshold = 100.0f;
    float stage_damp_rel_threshold = 10.0f;
    // Optional stage-3 override (0=use stage_gate_rel_threshold).
    // Set via env: WRF_SDIRK3_STAGE3_GATE_REL_THRESHOLD
    float stage3_gate_rel_threshold = 0.0f;

    // v20.14r39: Stage gate growth cap for catastrophic OR condition.
    // Replaces rel_R_full (= R_full/K_norm) which is preconditioner-scale-sensitive.
    // growth = R_full_norm / R0_unscaled (Newton initial L2).
    // growth > cap means Newton failed to reduce residual below initial level.
    // Set via env: WRF_SDIRK3_STAGE_GATE_GROWTH_CAP
    float stage_gate_growth_cap = 10.0f;

    // v20.14r40: Stage gate K_norm floor for rel_R_full computation.
    // Prevents rel_R_full = R_full/K_norm from exploding when ||K|| is small.
    // Gate uses max(||K||, K_floor) as denominator; raw ratio logged separately.
    // Set via env: WRF_SDIRK3_STAGE_GATE_K_FLOOR
    float stage_gate_K_floor = 1.0f;

    // v20.14r65/P1: Stage gate metric mode.
    // 0 = WRMS-relative growth metric, 1 = scaled absolute metric (R_full_norm),
    // 2 = legacy ratio metric (rel_R_full) fallback.
    // Set via env: WRF_SDIRK3_GATE_METRIC_MODE
    int gate_metric_mode = 0;

    // v20.14r67/P2: Stagnation gate (opt-in, default OFF). The WRMS-growth gate is blind to
    // stagnation: a stage with converged=0 but wrms_growth ~= 1 (no residual reduction) passes
    // the growth threshold, so today only the legacy "severe" rel_R_full backstop catches it.
    // When enabled, a stage that did NOT converge AND barely reduced its WRMS residual
    // (stage_gate_growth_value() > stagnation_growth_floor) AND whose scaled residual is still
    // above newton_tol is treated as catastrophic (fail-closed) regardless of stage_fail_action.
    // Default OFF = no behavior change. Set via env: WRF_SDIRK3_STAGNATION_GATE_ENABLE,
    // WRF_SDIRK3_STAGNATION_GROWTH_FLOOR
    bool stagnation_gate_enable = false;
    float stagnation_growth_floor = 0.9f;

    // v20.14r68: Honest non-convergence fail-close for the RETRY-LESS paths (split_mode=0
    // full-implicit baseline + IMEX mode-2). The WRMS-growth gate is blind to *stable*
    // non-convergence (converged=0 with low growth, e.g. the full-implicit baseline at dt=600
    // soft-continues all stages converged=0 and reports a false "SUCCESS COMPLETE WRF"). The
    // only reliable signal for stable non-convergence is the Newton flag itself: a stage that
    // reports converged=0 did NOT solve its implicit system, so advancing it is silently wrong.
    // When true, a converged=0 stage in the retry-less paths is treated as catastrophic
    // (fail-closed), regardless of growth/residual. Does NOT touch mode-3 (its handle_stage_gate
    // retry path is untouched) and never fires on converged=1 stages. Default FALSE = OPT-IN
    // (repo guardrail: new knobs must not change default behavior, same as the stagnation gate);
    // enable to make the split-0/mode-2 silent-non-convergence honest (P0 philosophy). Set via
    // namelist sdirk3_stage_require_convergence or env WRF_SDIRK3_STAGE_REQUIRE_CONVERGENCE.
    bool stage_require_convergence = false;

    // v20.15: HEVI split (horizontally-explicit / vertically-implicit). When true (and split_mode>=2),
    // the horizontal-acoustic terms (U-PGF, V-PGF, horizontal mass divergence) are rerouted from the
    // IMPLICIT operator (k_fast) to the EXPLICIT tableau (k_slow), leaving only the vertical W-phi
    // acoustic implicit. Fixes the indefinite saddle-point Stage-3 operator (Rayleigh -766; SPD
    // preconditioners provably can't fix it, Sylvester). Implemented via a snapshot/restore in the
    // monolithic RHS block (baseline byte-identical when off). Default FALSE = OPT-IN (repo guardrail).
    // Set via env WRF_SDIRK3_HEVI_SPLIT (validation) or namelist sdirk3_hevi_split (production).
    bool hevi_split = false;

    // Split-explicit core (opt-in, default OFF = no behavior change). WIP differentiable
    // RK3 + acoustic-substep integrator mirroring dyn_em; replaces the ARK324 implicit path
    // when ON. Set via env WRF_SDIRK3_SPLIT_EXPLICIT or namelist sdirk3_split_explicit.
    bool split_explicit = false;

    // Split-explicit acoustic-loop parameters: pass-throughs of EXISTING WRF namelist values
    // (time_step_sound, epssm, smdiv, emdiv, top_lid) — no new Registry entries. Consumed ONLY
    // when split_explicit is ON; defaults match the em_b_wave namelist, so unset = current
    // behavior. Set via Fortran set_config (module_implicit_sdirk3) or env
    // WRF_SDIRK3_SPLIT_EXPLICIT_{TIME_STEP_SOUND,EPSSM,SMDIV,EMDIV,TOP_LID}.
    int split_explicit_time_step_sound = 4;  // WRF time_step_sound; split guard REQUIRES explicit even >= 4 (WRF's 0=auto formula not implemented)
    float split_explicit_epssm = 0.1f;       // WRF epssm: acoustic vertical off-centering
    float split_explicit_smdiv = 0.1f;       // WRF smdiv: 3D divergence damping
    float split_explicit_emdiv = 0.01f;      // WRF emdiv: external-mode filter
    bool split_explicit_top_lid = false;     // WRF config_flags%top_lid (rigid upper lid)

    // v20.14r66: Mode3 Stage4 severe non-convergence abort toggle.
    // true (default): keep current safety behavior (stage4 severe -> abort).
    // false: skip severe_nonconv abort at stage>=4 in mode3 gate (catastrophic still aborts).
    // Set via env: WRF_SDIRK3_MODE3_STAGE4_SEVERE_ABORT
    bool mode3_stage4_severe_abort = true;
    // v20.14r67: Mode3 retry-path non-finite sanitize (default-off).
    // false (default): keep strict fail-fast behavior.
    // true: on stage_fail_action=2 retry, replace non-finite values with zeros in
    //       U_stage/U_full_exch_stage/k_fast before health checks.
    // Set via env: WRF_SDIRK3_MODE3_RETRY_NAN_SANITIZE
    bool mode3_retry_nan_sanitize = false;
    // v20.14r68: Mode3 first-hit probe (default-off).
    // false (default): no additional diagnostics.
    // true: emit one-shot non-finite origin log in mode3 stage loop.
    // Set via env: WRF_SDIRK3_MODE3_FIRSTHIT_PROBE
    bool mode3_firsthit_probe = false;
    // v20.14r69: Stage4 preclip after retry path (default-off).
    // false: disabled.
    // true: when previous stage used recoverable retry (stage_fail_action=2), clip
    //       Stage4 U_stage/U_full_exch before implicit solve to +/- mode3_retry_stage4_clip_abs.
    // Set via env: WRF_SDIRK3_MODE3_RETRY_STAGE4_PRECLIP
    bool mode3_retry_stage4_preclip = false;
    // Absolute clip bound used when mode3_retry_stage4_preclip is enabled.
    // Set via env: WRF_SDIRK3_MODE3_RETRY_STAGE4_CLIP_ABS
    float mode3_retry_stage4_clip_abs = 1.0e6f;
    // v20.14r70: Retry handoff clip (default-off).
    // false: disabled.
    // true: when a stage used recoverable retry (stage_fail_action=2), clip the
    //       retried stage k_fast before passing history to subsequent stages.
    //       Also used for mode3 stage-abort bounded handoff: when final update is
    //       aborted, try a finite/clipped partial-progress commit instead of
    //       unconditional identity update (U_new = U_n).
    // Set via env: WRF_SDIRK3_MODE3_RETRY_HANDOFF_CLIP
    bool mode3_retry_handoff_clip = false;
    // Absolute clip bound for mode3 retry handoff clip.
    // Set via env: WRF_SDIRK3_MODE3_RETRY_HANDOFF_CLIP_ABS
    float mode3_retry_handoff_clip_abs = 1.0e6f;

    // v20.14r40: Trust-region max relative update fraction.
    // Limits ||dK_scaled|| to max_relative_update * ||K||.
    // When ||K|| is small, this clips steps aggressively. Configurable to relax.
    // Set via env: WRF_SDIRK3_TRUST_REGION_MAX_REL_UPDATE
    float trust_region_max_relative_update = 0.5f;

    // v20.14 r49: Block-aware trust-region acceptance.
    // Uses S_inv/halo-masked norm (same basis as rho) for consistency.
    // Set via env: WRF_SDIRK3_TRUST_REGION_BLOCK_AWARE_THRESH
    float trust_region_block_aware_thresh = 0.0f;  // 0 = off. 0.95 recommended.
    bool trust_fallback_relax = false;             // default-off: relaxed fallback acceptance on GMRES total-failure
    float trust_fallback_ratio = 0.98f;            // fallback accepted when R_new/R_old <= this (only when relax=on)

    // v20.14 r49-fix: Direct U Solve.
    // When ru_share > threshold, override GMRES U-block with δK_u = -R_u.
    // 0 = off. 0.95 recommended for S2 where S_U ≈ I.
    // Set via env: WRF_SDIRK3_DIRECT_U_SOLVE_THRESH
    float direct_u_solve_thresh = 0.0f;

    // v20.14 r50: GMRES block-scaling (left-preconditioning with D⁻¹).
    // Scales the GMRES residual per-block so all blocks (u,v,w,phi,theta,mu)
    // contribute equally to the GMRES norm. Without this, phi/theta O(10⁴)
    // dominate u O(1-10) and GMRES declares convergence while ru stays at 0.999.
    // D[block] = ||r0[block]||₂, so D⁻¹r₀ has each block normalized to 1.
    // 1 = enabled (default), 0 = off.
    // Set via env: WRF_SDIRK3_GMRES_BLOCK_SCALE
    int gmres_block_scale = 1;

    // v20.14: Adaptive theta tuning constants (configurable for different cases).
    // Defaults tuned for em_b_wave (dt=600, 41x81x64).
    // Set via env: WRF_SDIRK3_ADAPTIVE_*
    float adaptive_high_threshold = 0.45f;   // L2 norm fraction above this → component dominates
    float adaptive_low_threshold = 0.35f;    // L2 norm fraction below this → component is OK
    float adaptive_step_size = 0.04f;        // theta factor adjustment per trigger
    float adaptive_quality_gate = 0.9f;      // skip adaptive if GMRES rel_error >= this
    // 0=once per run (default), 1=re-tune at each timestep stage 1
    // Set via env: WRF_SDIRK3_ADAPTIVE_RETUNE_MODE
    int adaptive_retune_mode = 0;

    // Newton-Krylov Optimization Options
    bool nk_adaptive_tol = true;           // Use Eisenstat-Walker adaptive tolerance
    // v20.2: Raised from 0.1 to 0.15.
    // v20.3: Lowered back to 0.1 — caused blowup (but preconditioner was weaker then).
    // v20.4: Raised to 0.3 — 1D TDMA preconditioner achieves true_err ≈ 0.22-0.23,
    // so eta_max=0.1 makes GMRES exhaust budget on every Newton step.
    // With adaptive α and W-Φ coupling improvements, Newton can converge
    // even with looser GMRES tolerance; E-W will tighten as Newton progresses.
    float nk_forcing_eta_max = 0.3f;       // Maximum forcing term (relaxed for 1D preconditioner)
    float nk_forcing_eta_min = 0.02f;      // Raised from 1e-4: prevents GMRES from chasing unachievable tight tolerances
    float nk_forcing_gamma = 0.9f;         // Eisenstat-Walker gamma parameter
    float nk_forcing_alpha = 1.5f;         // Eisenstat-Walker alpha parameter
    bool nk_line_search = false;           // Use line search for Newton steps (default false for performance)
    // v20.14r26: Newton non-convergence policy.
    // false (default) = warn + return best-effort K (matches unified_solver.cpp behavior).
    // true = throw std::runtime_error (old tile_unified_impl.cpp behavior).
    // Set via env: WRF_SDIRK3_HARD_ABORT_ON_NEWTON_FAIL
    bool hard_abort_on_newton_fail = false;
    // v20.14r27e/r51: Stage non-convergence policy.
    // 0 = continue/best-effort (legacy behavior, mode-dependent)
    // 1 = abort remaining stages (safety rollback)
    // 2 = recoverable retry once (mode3): retry failed stage with same implicit equation,
    //     reset predictor history, and abort if retry still fails
    // Set via env: WRF_SDIRK3_STAGE_FAIL_ACTION
    int stage_fail_action = 0;
    // v20.14 r63: Default-off hopeless cap relaxer.
    // false = keep existing hopeless cap behavior (regression-neutral).
    // true  = keep stage budget overrides even when hopeless mode is active.
    bool hopeless_relax = false;
    // v20.14 r63: Default-off guard mode.
    // false = keep current behavior (abnormal precond ratio locks to identity).
    // true  = warn only, do not lock to identity.
    bool precond_ratio_guard_warn_only = false;
    // v20.14r27f: RHS NaN sanitizer mode.
    // 0 = report only (count + warn, no replacement) — fail-fast, default
    // 1 = sanitize (replace NaN/Inf tendencies with zeros) — masks root cause
    // Set via env: WRF_SDIRK3_NAN_SANITIZE_MODE
    int nan_sanitize_mode = 0;
    // v20.14r27f: GMRES variable scaling floors.
    // S[block] = max(RMS(R₀[block]), floor). Default floor=1.0 makes S near-identity
    // when R₀ block RMS is ~1.0, causing ru to dominate S (S[ru]≈2.5, rest≈1.0).
    // Physics-based floors should reflect typical tendency magnitudes:
    //   scale_u: coupled momentum ~1e5-1e6 → try 100.0
    //   scale_ph: geopotential ~10-100 → try 10.0
    //   scale_t: temperature ~1-10 → keep 1.0
    //   scale_mu: surface pressure ~0.01-0.1 → keep 1.0
    // Set via env: WRF_SDIRK3_SCALE_U, WRF_SDIRK3_SCALE_PH, etc.
    float scale_u  = 1.0f;
    float scale_ph = 1.0f;
    float scale_t  = 1.0f;
    float scale_mu = 1.0f;
    bool nk_trust_region = true;           // Use trust region to handle GMRES failures (enabled by default)
    float nk_trust_radius = 1.0f;          // Initial trust region radius
    float nk_line_search_alpha = 1e-4f;    // Armijo sufficient decrease parameter (c)
    int nk_gmres_max_nan_retries = 2;      // Max NaN/Inf in apply_jacobian before marking GMRES as failed
    
    // Memory Optimization Options
    bool memory_pool = true;               // Use memory pool for tensor allocation
    int memory_pool_size_mb = 1024;        // Memory pool size in MB
    bool tensor_checkpointing = false;     // Checkpoint intermediate tensors
    bool gradient_checkpointing = false;   // Checkpoint gradients for autodiff

    // 4DVAR checkpoint/replay options
    // Keep default-off for regression neutrality in forecast mode.
    bool save_trajectory = false;          // Save detached forward states for replay
    int checkpoint_interval = 360;         // Timesteps between saved checkpoints
    bool retain_graph_for_adjoint = false; // Debug-only: retain graph in short windows
    // PR 9E: diagnosis-only stage-operand decomposition capture. Default OFF;
    // when ON it OBSERVES the exact production evaluations (no extra RHS/JVP
    // calls, no numerical branch change) at record stage 2/3 Newton iter-0 on
    // a single rank / single tile. Wired through the config loader so it is a
    // cached bool, never a hot-path getenv.
    bool stage_operand_diag = false;
    bool obs_aware_4dvar = false;          // Enable observation-aware terminal forcing path
    int obs_source_mode = 0;               // 0=off,1=FDDA,2=WRFDA-compatible payload
    int obs_window_sync_mode = 0;          // 0=off,1=strict endpoint sync,2=relaxed sync
    
    // GMRES variable scaling mode
    // 0 = RMS: Per-block scalar from initial residual R₀ (default, current)
    // 1 = PHYSICS: Per-element from base state (mu_full, ph_base, th_base)
    int scaling_mode = 0;

    // Preconditioner acoustic coupling mode
    // 0 = 3×3 U-V-μ + diagonal Φ (legacy)
    // 1 = 4×4 U-V-μ-Φ batched Schur complement (v20)
    int precond_acoustic_4x4 = 1;

    // v20.2: W diagonal acoustic Schur boost factor
    // The single-level Schur complement underestimates the effective W-Φ coupling
    // in the multi-level block-tridiagonal system. This multiplier amplifies
    // acoustic_schur to better approximate the dominant eigenvalue.
    // Physical basis: for nz vertical levels, the worst-case amplification is ~4
    // (from the tridiagonal eigenvalue spread: λ_max/λ_diag ≈ 1+2cos(π/(nz+1)) ≈ 3).
    // Values: 1.0 = no boost (original), 3.0 = recommended, higher = more damping.
    // Note: boost=1.0 causes GMRES to converge to wrong direction in Stage 3.
    // boost=2.0 causes GMRES stagnation but trust-region adapts better.
    float precond_w_acoustic_boost = 2.0f;

    // v20.11: θ acoustic scaling factor for D_θ diagonal
    // Controls: D_θ_acoustic = factor · dt·γ·c_s²/dz²
    // 0.0 = off (D_θ≈1, identity for θ) — safe default for general use
    // 0.18 = optimal for dt=600 b_wave (D_θ≈80, true_err 0.82→0.59)
    // 0.16 = (R_d/c_v)² (indirect EOS coupling)
    // 1.0 = full acoustic scaling (legacy, over-dampens at large dt: D_θ≈439)
    // Set via namelist: sdirk3_precond_theta_acoustic_factor
    // Set via env: WRF_SDIRK3_PRECOND_THETA_ACOUSTIC_FACTOR
    float precond_theta_acoustic_factor = 0.0f;
    uint32_t theta_config_generation = 0;  // v20.14: bumped on every explicit config write

    // Debug options
    int debug_level = 0;  // Level 0: production (no debug), Level 1: profiling only, Level 2+: expensive .item() diagnostics
    bool check_nan = false;
    bool check_conservation = false;

    // OPT 2025-01-25: Autograd anomaly detection for JVP debugging
    // When true, enables torch::autograd::AnomalyMode to catch in-place ops that
    // break gradients, NaN/Inf in backward, and other autograd issues.
    // WARNING: Significant performance overhead - use only for debugging.
    // Enable via set_config_int("detect_autograd_anomaly", 1) from Fortran.
    bool detect_autograd_anomaly = false;

    // OPT 2025-01-25: Batch JVP chunk size for memory-bounded processing
    // When batch_size > this threshold, JVP computation is chunked to bound peak memory.
    // Default 16 is empirically good for most GPU memory sizes (8-32GB).
    // Increase for systems with large GPU memory, decrease for memory-constrained systems.
    // Set to 0 to auto-select based on available memory (not yet implemented).
    int batch_jvp_chunk_size = 16;

    // GPU sync optimization options (FIX Round192)
    // These control GPU→CPU sync frequency for performance on GPU runs
    int gpu_nan_check_interval = 10;      // Check NaN/Inf every N Newton iterations (0=every, 1=disabled on GPU)
    int gpu_diagnostic_interval = 5;       // Sample CFL/energy diagnostics every N steps (0=every step)
    bool gpu_batch_scalar_transfer = true; // Batch multiple scalars into single D2H transfer

    // OPT Pass33+: Configurable debug diagnostic sampling periods
    // Controls frequency of debug output to reduce D2H transfers while maintaining visibility
    //
    // Semantics:
    //   0 = every iteration (no sampling - maximum debug output)
    //   N>0 = every Nth iteration (reduces D2H sync overhead)
    // Usage pattern: (period == 0) || ((counter % period) == 0) || (counter == 1)
    //
    // Debug level thresholds:
    //   debug_sample_period:       debug_level >= 1 (basic profiling/warnings)
    //   debug_heavy_sample_period: debug_level >= 3 (verbose heavy diagnostics)
    //
    // Level gate vs sampling gate (important distinction):
    //   - Some diagnostics have HIGHER level gates than the sampling counter threshold
    //   - Example: tile_unified entry message requires debug_level >= 2 even though
    //     counter increments at >= 1. This makes debug_level=1 intentionally quieter.
    //   - Effective output = (level_gate) AND (do_*_sample)
    //
    // Counter behavior (OPT Pass33+):
    //   - Standard and heavy counters are INDEPENDENT per component (tile_unified, preconditioner)
    //   - Counters are NOT reset on solver restart - they persist across Newton/timestep boundaries
    //   - When debug_level rises to 3 mid-run, heavy counter starts from 1 (not from standard counter)
    //
    // WARN_ONCE scope:
    //   - period=0 warnings only trigger via set_config_int() Fortran interface
    //   - Direct C++ assignment (g_sdirk3_config.debug_sample_period = 0) does NOT warn
    //
    // Applied files (OPT Pass33+ checklist):
    //   [x] wrf_sdirk3_tile_unified_impl.cpp - standard/heavy counters (entry diag at >=2)
    //   [x] wrf_sdirk3_unified_preconditioner.cpp - standard/heavy counters
    //   [x] wrf_sdirk3_newton_krylov_solver.cpp - standard only (no heavy sampling)
    //   [x] wrf_sdirk3_jvp_autograd.cpp - standard only (legacy JVPContext removed)
    //
    // NOT applicable (control-flow essential .item() or already optimized):
    //   [-] wrf_sdirk3_full_physics.cpp - .item() for return values, batched D2H
    //   [-] wrf_adaptive_control.h - .item() for control flow, cached/batched
    //   [-] wrf_physics_constraints.h - single CPU transfer pattern
    //
    int debug_sample_period = 5;           // Standard diagnostics: GMRES progress, Newton steps
    int debug_heavy_sample_period = 10;    // Heavy diagnostics: H-matrix conditioning, tensor stats
    // NOTE: period=0 disables sampling (outputs every call) - use period>=1 for production
    //
    // SAMPLING PARAMETER IMPACT TABLE (OPT Pass34 Extended):
    // Shows approximate performance and log volume impact for period changes.
    // Baseline: 1000 Newton iterations at debug_level=2.
    //
    // ⚠️ EXAMPLE VALUES ONLY: Actual impact varies by workload (grid size,
    // physics complexity, GMRES iterations). Use as rough guidance, not exact.
    //
    // ┌─────────────────────────────────────────────────────────────────────────┐
    // │ debug_sample_period │ ΔD2H Calls  │ Δlog Lines │ Use Case              │
    // ├─────────────────────────────────────────────────────────────────────────┤
    // │ 0 (every call)      │ +1000 (~5x) │ +1000      │ Deep debug only       │
    // │ 1 (default dev)     │ +200 (~1x)  │ +200       │ Development baseline  │
    // │ 5 (default prod)    │ 0 (baseline)│ 0          │ Production default    │
    // │ 10                  │ -100 (~0.5x)│ -100       │ Low-noise production  │
    // │ 50                  │ -180 (~0.1x)│ -180       │ Long-run monitoring   │
    // └─────────────────────────────────────────────────────────────────────────┘
    //
    // ┌─────────────────────────────────────────────────────────────────────────┐
    // │ debug_heavy_sample_period │ ΔD2H Calls  │ Δlog Lines │ Use Case        │
    // ├─────────────────────────────────────────────────────────────────────────┤
    // │ 0 (every call)            │ +500 (~10x) │ +500       │ Deep debug only │
    // │ 1                         │ +450 (~9x)  │ +450       │ Intensive debug │
    // │ 5                         │ +150 (~3x)  │ +150       │ Active profiling│
    // │ 10 (default)              │ 0 (baseline)│ 0          │ Production      │
    // │ 50                        │ -80 (~0.2x) │ -80        │ Minimal overhead│
    // │ 100                       │ -90 (~0.1x) │ -90        │ Long-run only   │
    // └─────────────────────────────────────────────────────────────────────────┘
    //
    // NOTES:
    // - ΔD2H = change in GPU→CPU transfers per 1000 iterations vs baseline
    // - Heavy sampling (conditioning, stats) costs ~5x more than standard
    // - Setting both to 0 can cause log files >100MB in long runs
    // - Recommended: period≥5 for any run >10min, period≥10 for overnight
    //
    // UPDATE CYCLE: Re-measure example values semi-annually or after major
    // workload changes (new physics, grid resolution, GPU architecture).
    // Run em_b_wave benchmark with period=0 to capture actual D2H counts.
    //

    // FIX Round103/Round104/Round105/Round107: Warning throttle options
    // Controls minimum interval between repeated warnings to prevent log spam
    // during oscillation scenarios (e.g., dz/rdz device mismatch that keeps recurring).
    //
    // Semantics (FIX Round104):
    //   - 0 = "Sticky" warnings - once warned, warning flag stays set forever.
    //         Device mismatch warnings will NOT re-trigger even after resolution.
    //         Use for production where single notification is sufficient.
    //         (No counter increments occur - no wasted work)
    //   - >0 = Allow warning re-trigger after this many calls in resolved state.
    //         Useful for debugging oscillating device configurations.
    //
    // Per-solver counter (FIX Round104): Counter is stored in WRFGridInfo, not
    // thread_local, so each solver tracks independently in multi-solver scenarios.
    //
    // ⚠️ WARNING (FIX Round107): Setting to 0 makes warnings PERMANENTLY sticky!
    // If you need to re-detect mismatches after they are resolved (e.g., debugging
    // intermittent device migration issues), DO NOT use 0. Use a high value like
    // 1000 or 10000 instead to allow eventual re-detection while limiting spam.
    // To manually re-enable warnings with throttle=0, call resetPerSolverState().
    //
    // FIX Round122/Round127: RUNTIME CONFIG CHANGE BEHAVIOR
    // If warn_throttle_count or warn_on_device_index_clamp is changed at runtime,
    // per-solver counters and flags (in WRFGridInfo) are NOT automatically reset.
    // This means:
    //   - warn_throttle_count: warnings may trigger "later than expected" if threshold
    //     is lowered (counter accumulated under old threshold), or "sooner than expected"
    //     if threshold is raised.
    //   - warn_on_device_index_clamp: new setting takes effect immediately, but previously
    //     suppressed warnings won't retroactively appear.
    //
    // FIX Round127: For deterministic behavior after runtime config changes, call
    // sdirk3_tile_solver_reset_state() on affected solvers. This resets:
    //   - dz_rdz_warn_throttle_counter (to 0)
    //   - warned_no_reference_device (to false)
    //   - warned_dz_rdz_device_mismatch (to false)
    //   - Device caches
    //
    // Fortran usage:
    //   CALL wrf_sdirk3_set_config_int('warn_throttle_count'//C_NULL_CHAR, new_value)
    //   CALL sdirk3_tile_solver_reset_state(solver_ptr)  ! Reset per-solver counters
    //
    // Note: For uint64 values > INT_MAX, use wrf_sdirk3_set_config_uint64() instead.
    uint64_t warn_throttle_count = 100;  // Min calls before warning can re-trigger (0=sticky/never re-warn)
    // FIX 2025-01-11 Round71/Round72/Round73: Option to enable solver pointer in logs
    // When false (default), logs show "solver=<hidden>" instead of actual pointer address.
    //
    // ⚠️ BEHAVIOR CHANGE (Round72): Default changed from TRUE to FALSE
    //   - Previous default: true (showed solver pointer addresses in logs)
    //   - New default: false (shows "solver=<hidden>" for security)
    //   - Reason: Pointer addresses may be sensitive in ASLR bypass scenarios
    //   - To restore old behavior: set g_sdirk3_config.log_solver_pointer = true
    //
    // Set to true for debugging multi-solver issues where solver identity matters.
    bool log_solver_pointer = false;

    // =========================================================================
    // OPT Pass33+: WARN_ONCE ATOMICITY POLICY
    // =========================================================================
    // The codebase uses two patterns for warn-once flags:
    //
    // 1. NON-ATOMIC: `static bool warned = false;`
    //    - Used in single-threaded contexts OR where duplicate warnings are acceptable
    //    - Files: wrf_sdirk3_physics_kslab_base.cpp (validation)
    //             wrf_sdirk3_unified_preconditioner.cpp (fallback warnings)
    //             wrf_sdirk3_config.cpp (profiler warnings)
    //    - BEHAVIOR: In multi-threaded code, may print 2-N warnings (benign race)
    //    - RATIONALE: Simpler, no atomic overhead, duplicate warnings harmless
    //
    // 2. ATOMIC: `static std::atomic<bool> warned{false};`
    //    - Used in multi-threaded hot paths where single warning is required
    //    - Files: wrf_sdirk3_wsm6_kslab.cpp (GPU sync)
    //             device_manager.cpp (stagger warnings)
    //             wrf_sdirk3_cache_utils.cpp (overflow/underflow)
    //             wrf_sdirk3_tile_unified_impl.cpp (various)
    //    - BEHAVIOR: Guaranteed single warning via compare_exchange
    //    - RATIONALE: Prevents log spam in high-frequency parallel paths
    //
    // POLICY: Use atomic when warning is in OpenMP parallel region or hot path.
    //         Use non-atomic when single-threaded or duplicate warnings acceptable.
    // =========================================================================

    // =========================================================================
    // OPT Pass33+: DEVELOPER REFERENCE - CI/BUILD/DEBUG POLICIES
    // =========================================================================
    //
    // RELATED DOCUMENTATION:
    //   docs/OPT_PASS34_SAFETY_GUIDE.md - MPI/atexit safety, unique_ptr patterns,
    //                                      std::endl hot-path policy
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 1. CI COMPILE OPTIONS QUICK REFERENCE                                  │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ Flag                              │ Purpose                            │
    // ├───────────────────────────────────┼────────────────────────────────────┤
    // │ -DSDIRK3_QUIET_TESTS              │ Suppress verbose test output       │
    // │ -DWRF_SDIRK3_ABI_STRICT=1         │ Enable strict ABI checks (DSO)     │
    // │ -DWRF_SDIRK3_ENABLE_DEPRECATED_API│ Enable deprecated APIs (not rec.)  │
    // │ -DDEBUG                           │ Enable debug assertions            │
    // │ -DNDEBUG                          │ Disable assertions (Release)       │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ CI TEMPLATE EXAMPLES:                                                  │
    // │                                                                        │
    // │ # CMake (add to CMakeLists.txt test target):                          │
    // │   target_compile_definitions(sdirk3_tests PRIVATE SDIRK3_QUIET_TESTS) │
    // │                                                                        │
    // │ # GitHub Actions / CI script:                                          │
    // │   cmake -DCMAKE_CXX_FLAGS="-DSDIRK3_QUIET_TESTS -DNDEBUG" ..          │
    // │                                                                        │
    // │ # Make (legacy):                                                       │
    // │   CXXFLAGS="-DSDIRK3_QUIET_TESTS" make test                           │
    // │                                                                        │
    // │ # Strict ABI validation (CI release builds):                           │
    // │   cmake -DCMAKE_CXX_FLAGS="-DWRF_SDIRK3_ABI_STRICT=1" ..              │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 2. TLS CACHE HIT-RATE & INVALIDATION DECISION CRITERIA                 │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ Metric source: wrf_sdirk3_tile_unified.h TLS counters (debug_level>=2) │
    // │   fast_hit + reval_hit + slow_miss = total_lookups                     │
    // │   hit_rate = (fast_hit + reval_hit) / total_lookups                    │
    // │   inval_rate = invalidations / total_lookups                           │
    // │                                                                        │
    // │ Decision matrix (hit_rate vs inval_rate):                              │
    // │   ┌─────────────┬────────────────┬────────────────┬───────────────────┐│
    // │   │             │ inval <10%     │ inval 10-30%   │ inval >30%        ││
    // │   ├─────────────┼────────────────┼────────────────┼───────────────────┤│
    // │   │ hit >70%    │ ✓ EXPAND       │ ✓ EXPAND       │ ⚠ REVIEW cause   ││
    // │   │ hit 50-70%  │ ~ REVIEW       │ ⚠ HOLD         │ ✗ SHRINK/disable ││
    // │   │ hit <50%    │ ⚠ HOLD         │ ✗ SHRINK       │ ✗ DISABLE cache  ││
    // │   └─────────────┴────────────────┴────────────────┴───────────────────┘│
    // │                                                                        │
    // │ High invalidation causes: solver ptr changes, grid resize, device      │
    // │ migration. If inval >30% with good hit rate, check for oscillation.    │
    // │                                                                        │
    // │ Counter reset: Counters persist across Newton iterations. To profile   │
    // │ a fresh window, call sdirk3_tile_solver_reset_state() or restart run.  │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 3. DEPRECATED API DOCUMENTATION LOCATIONS                              │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ Canonical:  wrf_sdirk3_interface.h (lines 199-365)                     │
    // │ Fortran:    module_implicit_sdirk3.F (sole bridge; uses v2 APIs only)  │
    // │ README:     SDIRK3_OPTIMIZATION_OPTIONS.md (namelist reference)        │
    // │                                                                        │
    // │ SYNC CHECK: All three files should agree that:                         │
    // │   - Default: WRF_SDIRK3_ENABLE_DEPRECATED_API=0                        │
    // │   - Production APIs: sdirk3_tile_*_zerocopy_v2()                       │
    // │   - Deprecated APIs: wrf_sdirk3_init_solver, pack/unpack, etc.         │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 3A. DEPRECATED API NECESSITY CHECK (OPT Pass34 Extended)              │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ QUARTERLY REVIEW: Determine if WRF_SDIRK3_ENABLE_DEPRECATED_API can   │
    // │ be completely removed from the codebase.                               │
    // │                                                                        │
    // │ CHECKLIST:                                                             │
    // │   □ Run: grep -rn "WRF_SDIRK3_ENABLE_DEPRECATED_API" --include="*.F"  │
    // │         --include="*.f90" dyn_em/ phys/ share/                        │
    // │     → Any hits outside tests/examples? If YES, removal blocked.       │
    // │                                                                        │
    // │   □ Check external consumers: Are any downstream WRF forks or plugins │
    // │         using deprecated API? (GitHub search, issue tracker)          │
    // │     → If known consumers exist, plan deprecation notice timeline.     │
    // │                                                                        │
    // │   □ Verify v2 API coverage: All functionality that deprecated API     │
    // │         provided must be available in *_zerocopy_v2() functions.      │
    // │     → Missing feature? Implement before deprecation removal.          │
    // │                                                                        │
    // │ DECISION MATRIX:                                                       │
    // │   ┌────────────────┬────────────────────────────────────────────────┐ │
    // │   │ Finding        │ Action                                         │ │
    // │   ├────────────────┼────────────────────────────────────────────────┤ │
    // │   │ Zero usage     │ Remove deprecated API code entirely            │ │
    // │   │ Test-only use  │ Remove API, update tests to v2                 │ │
    // │   │ Active use     │ Keep macro, set deprecation removal date       │ │
    // │   │ External deps  │ Add removal warning, 2-quarter notice period   │ │
    // │   └────────────────┴────────────────────────────────────────────────┘ │
    // │                                                                        │
    // │ AUDIT LOG:                                                             │
    // │   Q[N] [YEAR]: Usage=[count], Decision=[KEEP/REMOVE], Reason=[...]    │
    // │                                                                        │
    // │ CI BUILD TRACKING (OPT Pass34 Extended):                               │
    // │   Track whether any CI builds use ENABLE_DEPRECATED_API=1 as evidence  │
    // │   for complete removal decision.                                       │
    // │                                                                        │
    // │   CI SCRIPT (add to pipeline):                                         │
    // │   #!/bin/bash                                                          │
    // │   # log_deprecated_api_usage.sh - Log deprecated API build flag usage  │
    // │   BUILD_LOG="${CI_PROJECT_DIR}/build_flags.log"                        │
    // │   CMAKE_CACHE="${BUILD_DIR}/CMakeCache.txt"                            │
    // │                                                                        │
    // │   # Check if deprecated API is enabled in this build                   │
    // │   DEPRECATED_ENABLED=$(grep "ENABLE_DEPRECATED_API" "$CMAKE_CACHE" \   │
    // │       2>/dev/null | grep -c "=1\|=ON\|=TRUE")                          │
    // │                                                                        │
    // │   # Log to persistent file (append, preserved across builds)           │
    // │   echo "[$(date +%Y-%m-%d)] Job=$CI_JOB_NAME Deprecated=$DEPRECATED_ENABLED" \
    // │       >> "$BUILD_LOG"                                                  │
    // │                                                                        │
    // │   # Emit warning if deprecated API is enabled                          │
    // │   if [ "$DEPRECATED_ENABLED" -gt 0 ]; then                             │
    // │       echo "::warning::Deprecated API enabled (ENABLE_DEPRECATED_API=1)"
    // │   fi                                                                   │
    // │                                                                        │
    // │   QUARTERLY ANALYSIS:                                                  │
    // │     grep "Deprecated=1" build_flags.log | wc -l                        │
    // │     → If count = 0 for 2+ quarters, safe to remove deprecated API code │
    // │                                                                        │
    // │   REMOVAL EVIDENCE FORMAT:                                             │
    // │     Date: [YYYY-MM-DD]                                                 │
    // │     Last deprecated build: [date or "never"]                           │
    // │     Consecutive zero-usage quarters: [N]                               │
    // │     Decision: [REMOVE/KEEP]                                            │
    // │                                                                        │
    // │   REMOVAL PROMOTION RULES (OPT Pass34 Extended):                       │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   Automatic promotion to removal candidate based on non-usage period:  │
    // │                                                                        │
    // │   ┌────────────────────┬────────────────────────────────────────────┐ │
    // │   │ Non-Usage Period   │ Status & Action                            │ │
    // │   ├────────────────────┼────────────────────────────────────────────┤ │
    // │   │ < 3 months         │ ACTIVE: Keep, monitor usage                │ │
    // │   │ 3 months (1 qtr)   │ WARNING: Flag for review, notify consumers │ │
    // │   │ 6 months (2 qtrs)  │ CANDIDATE: Promote to removal candidate    │ │
    // │   │ 9 months (3 qtrs)  │ APPROVED: Safe to remove, schedule removal │ │
    // │   │ 12 months (4 qtrs) │ OVERDUE: Remove immediately or justify     │ │
    // │   └────────────────────┴────────────────────────────────────────────┘ │
    // │                                                                        │
    // │   PROMOTION WORKFLOW:                                                  │
    // │     1. [3 mo] Issue warning in CI: "Deprecated API unused for 3 mo"   │
    // │     2. [6 mo] Create removal PR (draft), notify known consumers       │
    // │     3. [9 mo] Merge removal PR if no objections                       │
    // │     4. [12 mo] Force removal unless explicit extension granted        │
    // │                                                                        │
    // │   EXCEPTION PROCESS (to delay removal):                                │
    // │     - File issue: "Deprecated API Extension Request"                  │
    // │     - Provide: Consumer name, migration timeline, justification       │
    // │     - Maximum extension: 6 additional months (total 18 mo max)        │
    // │                                                                        │
    // │   DEFERRAL REASON TEMPLATE (OPT Pass34 Extended):                      │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   Standardized template ensures consistent exception handling.         │
    // │                                                                        │
    // │   ## Deprecated API Deferral Request                                   │
    // │                                                                        │
    // │   **API Name:** [e.g., wrf_sdirk3_init_solver]                        │
    // │   **Request Date:** [YYYY-MM-DD]                                      │
    // │   **Current Non-Usage Period:** [N] months                            │
    // │   **Requested Extension:** [N] months (max 6)                         │
    // │                                                                        │
    // │   **Consumer Information:**                                            │
    // │   - Organization/Project: [name]                                      │
    // │   - Contact: [email/GitHub handle]                                    │
    // │   - Estimated users affected: [count]                                 │
    // │                                                                        │
    // │   **Deferral Reason:** (check one)                                    │
    // │   □ Active migration in progress (provide timeline below)             │
    // │   □ Dependency on upstream library (provide dependency info)          │
    // │   □ Resource constraints (justify why migration delayed)              │
    // │   □ Feature gap in v2 API (specify missing functionality)             │
    // │   □ Other (explain in detail below)                                   │
    // │                                                                        │
    // │   **Migration Timeline:**                                              │
    // │   - Phase 1: [description] by [date]                                  │
    // │   - Phase 2: [description] by [date]                                  │
    // │   - Complete migration by: [date]                                     │
    // │                                                                        │
    // │   **Approval:** □ Approved / □ Denied  Date: ____  Reviewer: ____     │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │                                                                        │
    // │   DEFERRAL DECISION CRITERIA:                                          │
    // │   ┌─────────────────────┬─────────────────────────────────────────┐   │
    // │   │ Reason              │ Typical Decision                        │   │
    // │   ├─────────────────────┼─────────────────────────────────────────┤   │
    // │   │ Active migration    │ APPROVE if timeline <6 mo               │   │
    // │   │ Feature gap         │ APPROVE + prioritize v2 feature         │   │
    // │   │ Resource constraint │ APPROVE 3 mo max, require plan          │   │
    // │   │ No clear reason     │ DENY, proceed with removal              │   │
    // │   └─────────────────────┴─────────────────────────────────────────┘   │
    // │                                                                        │
    // │   AUDIT LOG ENTRY FOR PROMOTION:                                       │
    // │     [DATE]: API [name] promoted to [status] after [N] months non-use  │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 4. BUILD PROFILE DEBUG_LEVEL RECOMMENDATIONS                           │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ Profile          │ debug_level │ Rationale                             │
    // ├──────────────────┼─────────────┼───────────────────────────────────────┤
    // │ Release          │ 0           │ No diagnostics, maximum performance   │
    // │ RelWithDebInfo   │ 1           │ Profiling counters, no .item() calls  │
    // │ Debug            │ 2           │ Full diagnostics including .item()    │
    // │ Deep Debug       │ 3+          │ Heavy sampling, tensor stats, H-cond  │
    // ├──────────────────┴─────────────┴───────────────────────────────────────┤
    // │ Note: debug_level >= 2 enables expensive .item() calls that break      │
    // │ GPU async and add D2H transfer overhead. Use sparingly in production.  │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 5. NoGradGuard CHECKLIST FOR NEW .item() CALLS                         │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ Before adding .item<T>(), .min(), .max(), or any scalar extraction:    │
    // │                                                                        │
    // │   □ Is this inside an AD-tracked code path?                            │
    // │     → YES: Wrap with torch::NoGradGuard guard; OR use .detach() first  │
    // │     → NO:  Proceed (e.g., already in diagnostic-only block)            │
    // │                                                                        │
    // │   □ Is this in a GPU hot path?                                         │
    // │     → YES: Add .to(torch::kCPU) before .item<T>()                       │
    // │     → YES: Gate with (debug_level >= N) to avoid production overhead   │
    // │                                                                        │
    // │   □ Is this tensor potentially on MPS/CUDA?                            │
    // │     → YES: .item<T>() triggers sync; prefer batched D2H if multiple    │
    // │                                                                        │
    // │ Pattern template:                                                      │
    // │   if (g_sdirk3_config.debug_level >= 2) {                              │
    // │       torch::NoGradGuard guard;                                        │
    // │       float val = tensor.to(torch::kCPU).item<float>();                │
    // │   }                                                                    │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 6. NON-AD WORKSPACE REUSE PATH SEPARATION (FUTURE API)                 │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ CONTEXT:                                                               │
    // │   Current TensorFactory::create_*() uses use_pool=true by default.     │
    // │   Pooled tensors are pre-allocated and reused, saving allocation time. │
    // │   However, pooled tensors cannot be used in AD paths because:          │
    // │     1. Reuse breaks gradient tape continuity                           │
    // │     2. In-place ops on pooled tensors cause grad_fn corruption         │
    // │                                                                        │
    // │ CURRENT WORKAROUND:                                                    │
    // │   Callers must manually specify use_pool=false for AD paths:           │
    // │     auto tensor = TensorFactory::create_3d(ny, nz, nx, opts, false);   │
    // │                                                                        │
    // │ PROPOSED FUTURE API:                                                   │
    // │   Split into explicit function variants:                               │
    // │                                                                        │
    // │   // For non-AD paths (workspace, temporaries)                         │
    // │   auto ws = TensorFactory::create_workspace_3d(ny, nz, nx, opts);      │
    // │                                                                        │
    // │   // For AD paths (gradient-tracked tensors)                           │
    // │   auto t = TensorFactory::create_ad_3d(ny, nz, nx, opts);              │
    // │                                                                        │
    // │ BENEFITS:                                                              │
    // │   - Compile-time clarity: API name documents intent                    │
    // │   - Static analysis: Can grep for misuse patterns                      │
    // │   - Default safety: create_ad_*() never uses pool                      │
    // │                                                                        │
    // │ MIGRATION PATH:                                                        │
    // │   1. Add new create_workspace_*() and create_ad_*() functions          │
    // │   2. Deprecate use_pool parameter on create_*() functions              │
    // │   3. Grep for create_3d(..., true) → create_workspace_3d(...)          │
    // │   4. Grep for create_3d(..., false) → create_ad_3d(...)                │
    // │   5. Remove use_pool parameter in next major version                   │
    // │                                                                        │
    // │ TEST PLAN (OPT Pass34):                                                │
    // │   Before implementing, validate with these autograd test cases:        │
    // │                                                                        │
    // │   TEST 1: AD path gradient flow                                        │
    // │     auto t = create_ad_3d(ny, nz, nx, opts);                           │
    // │     t.requires_grad_(true);                                            │
    // │     auto y = t.sum();                                                  │
    // │     y.backward();                                                      │
    // │     ASSERT(t.grad().defined());  // grad must exist                    │
    // │     ASSERT(t.grad().sum().item<float>() == ny*nz*nx); // all ones      │
    // │                                                                        │
    // │   TEST 2: Workspace path no grad pollution                             │
    // │     auto ws = create_workspace_3d(ny, nz, nx, opts);                   │
    // │     ASSERT(!ws.requires_grad());  // no grad tracking                  │
    // │     ws.fill_(1.0f);               // in-place op should not fail       │
    // │     release(ws);                  // should return to pool             │
    // │     auto ws2 = create_workspace_3d(ny, nz, nx, opts);                  │
    // │     ASSERT(ws2.sum().item<float>() == 0.0f);  // zeroed on reuse       │
    // │                                                                        │
    // │   TEST 3: Mixed path isolation                                         │
    // │     auto t = create_ad_3d(...);  t.requires_grad_(true);               │
    // │     auto ws = create_workspace_3d(...);                                │
    // │     ws = t * 2;  // intermediate result in workspace                   │
    // │     auto y = ws.sum();                                                 │
    // │     y.backward();                                                      │
    // │     ASSERT(t.grad().defined());  // AD path still has grad             │
    // │     // ws should not retain grad_fn after release                      │
    // │                                                                        │
    // │ STATUS: TEST PLAN READY - IMPLEMENTATION PENDING                       │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 7. InferenceMode vs NoGradGuard SELECTION GUIDE (OPT Pass34)           │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ CONTEXT:                                                               │
    // │   Both disable gradient computation, but differ in behavior:           │
    // │                                                                        │
    // │   torch::NoGradGuard:                                                  │
    // │     - Disables gradient computation for operations inside scope        │
    // │     - Still creates autograd metadata for tensors                      │
    // │     - Safe with view operations and in-place modifications             │
    // │     - REQUIRED when tensors escape the scope (return values, caching)  │
    // │                                                                        │
    // │   c10::InferenceMode:                                                  │
    // │     - Disables gradient computation AND autograd metadata creation     │
    // │     - ~10-20% faster for compute-heavy diagnostic blocks               │
    // │     - ⚠️ UNSAFE if views/inplace ops modify tensors used outside scope │
    // │     - ⚠️ Tensors created inside cannot require_grad() later            │
    // │                                                                        │
    // │ DECISION TABLE:                                                        │
    // │   ┌──────────────────────────────────────┬──────────┬─────────────────┐│
    // │   │ Use Case                             │ Recommend│ Notes           ││
    // │   ├──────────────────────────────────────┼──────────┼─────────────────┤│
    // │   │ .item() for debug logging            │ Either   │ InferenceMode ok││
    // │   │ .min()/.max()/.norm() for stats      │ Either   │ InferenceMode ok││
    // │   │ Creating tensors that escape scope   │ NoGrad   │ Avoid Inference ││
    // │   │ Modifying views of external tensors  │ NoGrad   │ Avoid Inference ││
    // │   │ Cache-stored tensors (TLS cache)     │ NoGrad   │ Avoid Inference ││
    // │   │ Pure diagnostic computation loops    │ Inference│ Best perf       ││
    // │   │ Performance-critical stat gathering  │ Inference│ Best perf       ││
    // │   └──────────────────────────────────────┴──────────┴─────────────────┘│
    // │                                                                        │
    // │ MIGRATION CANDIDATES (diagnostic-only, no escaping tensors):           │
    // │   - wrf_sdirk3_newton_krylov_solver.cpp: convergence norm checks       │
    // │   - wrf_sdirk3_validation_enhanced.h: scalar range validation          │
    // │   - metric_utils.h: diagnostic counters and statistics                 │
    // │                                                                        │
    // │ DO NOT MIGRATE (tensors escape or views modified):                     │
    // │   - TensorViewCache: cached tensors persist across calls               │
    // │   - jvp_bridge.cpp: computed values may be returned                    │
    // │   - Any function returning tensors created in the scope                │
    // │                                                                        │
    // │ USAGE PATTERN:                                                         │
    // │   // Safe for pure diagnostics (nothing escapes):                      │
    // │   if (debug_level >= 2) {                                              │
    // │       c10::InferenceMode guard;                                        │
    // │       float norm = x.norm().item<float>();                             │
    // │       std::cerr << "norm=" << norm << std::endl;                       │
    // │   }                                                                    │
    // │                                                                        │
    // │   // Required when tensors escape or are cached:                       │
    // │   torch::NoGradGuard guard;                                            │
    // │   auto cached_view = cache.getTensorView(ptr, shape);                  │
    // │                                                                        │
    // │ STATUS: REVIEW COMPLETE - Migration optional, not urgent               │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 7A. InferenceMode A/B SAFETY VERIFICATION (OPT Pass34 Extended)        │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ PURPOSE:                                                               │
    // │   Verify InferenceMode produces identical results to NoGradGuard       │
    // │   for diagnostic blocks before deploying InferenceMode in production.  │
    // │                                                                        │
    // │ A/B TEST METHODOLOGY:                                                  │
    // │                                                                        │
    // │   Step 1: Create parallel code paths with compile-time switch          │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   #ifndef USE_INFERENCE_MODE_DIAGNOSTICS                               │
    // │   #define USE_INFERENCE_MODE_DIAGNOSTICS 0  // A=0 (NoGrad), B=1       │
    // │   #endif                                                               │
    // │                                                                        │
    // │   #if USE_INFERENCE_MODE_DIAGNOSTICS                                   │
    // │       c10::InferenceMode guard;  // B: InferenceMode                   │
    // │   #else                                                                │
    // │       torch::NoGradGuard guard;  // A: NoGradGuard (baseline)          │
    // │   #endif                                                               │
    // │   float norm = x.norm().item<float>();                                 │
    // │   SDIRK3_DEBUG_PRINT(2, "norm=" << norm);                              │
    // │                                                                        │
    // │   Step 2: Run em_b_wave test with A (baseline)                         │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   cmake -DUSE_INFERENCE_MODE_DIAGNOSTICS=0 ..                          │
    // │   make && ./test_em_b_wave > output_A.log 2>&1                         │
    // │   # Save all diagnostic outputs and final results                      │
    // │                                                                        │
    // │   Step 3: Run em_b_wave test with B (InferenceMode)                    │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   cmake -DUSE_INFERENCE_MODE_DIAGNOSTICS=1 ..                          │
    // │   make && ./test_em_b_wave > output_B.log 2>&1                         │
    // │                                                                        │
    // │   Step 4: Compare outputs (TOLERANCE-BASED, not raw diff)              │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   ⚠️ DO NOT use raw `diff` - log ordering and float formatting vary!  │
    // │                                                                        │
    // │   # Extract numeric summaries (more robust than log diff):             │
    // │   grep -E "norm=|residual=|converged" output_A.log | sort > nums_A.txt │
    // │   grep -E "norm=|residual=|converged" output_B.log | sort > nums_B.txt │
    // │                                                                        │
    // │   # Compare with tolerance (Python example):                           │
    // │   import re                                                            │
    // │   def extract_floats(f): return [float(x) for x in re.findall(...)]    │
    // │   vals_A, vals_B = extract_floats('nums_A.txt'), ...                   │
    // │   for a, b in zip(vals_A, vals_B):                                     │
    // │       assert abs(a - b) < 1e-5 * max(abs(a), 1.0), f"{a} != {b}"       │
    // │                                                                        │
    // │   # Acceptable: timing, addresses, log ordering                        │
    // │   # NOT acceptable: >1e-5 relative error on numerics, iteration count  │
    // │                                                                        │
    // │ VERIFICATION CHECKLIST:                                                │
    // │   [ ] Diagnostic values (.item()) match to FP32 precision              │
    // │   [ ] Newton convergence iterations identical                          │
    // │   [ ] GMRES residuals identical                                        │
    // │   [ ] Final solution norms match                                       │
    // │   [ ] NaN/Inf counts identical (regression check - OPT Pass34)         │
    // │   [ ] No warnings/errors specific to InferenceMode                     │
    // │   [ ] Performance improvement measurable (expect 10-20%)               │
    // │                                                                        │
    // │ NaN/Inf REGRESSION CHECK (OPT Pass34):                                 │
    // │   # Extract NaN/Inf counts from logs:                                  │
    // │   grep -c "NaN\|Inf\|nan\|inf" output_A.log > nancount_A.txt           │
    // │   grep -c "NaN\|Inf\|nan\|inf" output_B.log > nancount_B.txt           │
    // │   diff nancount_A.txt nancount_B.txt                                   │
    // │   # Any difference indicates regression (InferenceMode may expose bugs)│
    // │                                                                        │
    // │   # For tensor-level check (add to diagnostic code):                   │
    // │   auto nan_count = torch::sum(torch::isnan(x)).item<int64_t>();        │
    // │   auto inf_count = torch::sum(torch::isinf(x)).item<int64_t>();        │
    // │   SDIRK3_DEBUG_PRINT(2, "NaN=" << nan_count << " Inf=" << inf_count);  │
    // │                                                                        │
    // │ KNOWN SAFE PATTERNS (verified):                                        │
    // │   ✅ tensor.item<T>()          - Pure scalar extraction                │
    // │   ✅ tensor.norm()             - Pure computation, no views            │
    // │   ✅ tensor.min() / .max()     - Pure computation, no views            │
    // │   ✅ tensor.mean() / .sum()    - Pure computation, no views            │
    // │   ✅ std::cerr << value        - Output only, no tensor state change   │
    // │                                                                        │
    // │ KNOWN UNSAFE PATTERNS (avoid InferenceMode):                           │
    // │   ❌ tensor.view(...) returned/stored  - View alias escapes scope      │
    // │   ❌ tensor.slice(...) stored in cache - View persists after scope     │
    // │   ❌ in-place ops on external tensors  - Modifies aliased data         │
    // │   ❌ tensor creation that escapes scope - Metadata mismatch risk       │
    // │                                                                        │
    // │ INFERENCEMODE SAFETY CHECKLIST (OPT Pass34 Refinement):                │
    // │   Before applying InferenceMode to a diagnostic block, verify:         │
    // │                                                                        │
    // │   [ ] 1. NO VIEWS ESCAPE SCOPE                                         │
    // │       - All .view(), .slice(), .narrow(), .select() results are        │
    // │         consumed within the same scope (not returned or stored)        │
    // │       - Pattern check: grep for "= tensor.view\|slice\|narrow"         │
    // │                                                                        │
    // │   [ ] 2. NO IN-PLACE OPS ON EXTERNAL TENSORS                           │
    // │       - No .add_(), .mul_(), .zero_(), .copy_() on tensors             │
    // │         passed into the scope from outside                             │
    // │       - Pattern check: grep for "_\(" on tensor parameter names        │
    // │                                                                        │
    // │   [ ] 3. NO TENSORS ESCAPE SCOPE                                       │
    // │       - Tensors created inside InferenceMode scope are not             │
    // │         returned, cached, or stored in external containers             │
    // │       - Pattern check: return statements, cache.put(), member assign   │
    // │                                                                        │
    // │   [ ] 4. PURE COMPUTATION ONLY                                         │
    // │       - Block contains only: .item(), .norm(), .min(), .max(),         │
    // │         .mean(), .sum(), std::cerr output                              │
    // │       - Any deviation requires NoGradGuard instead                     │
    // │                                                                        │
    // │   If ALL 4 conditions are met → InferenceMode is SAFE                  │
    // │   If ANY condition fails → Use NoGradGuard instead                     │
    // │                                                                        │
    // │ UNIT TEST FOR A/B EQUIVALENCE:                                         │
    // │   // test_inference_mode_ab.cpp                                        │
    // │   torch::manual_seed(42);  // REQUIRED: Fixed seed for reproducibility │
    // │   torch::Tensor x = torch::randn({100, 100}, torch::kFloat32);         │
    // │                                                                        │
    // │   // Path A: NoGradGuard                                               │
    // │   float norm_A, min_A, max_A;                                          │
    // │   {                                                                    │
    // │       torch::NoGradGuard guard;                                        │
    // │       norm_A = x.norm().item<float>();                                 │
    // │       min_A = x.min().item<float>();                                   │
    // │       max_A = x.max().item<float>();                                   │
    // │   }                                                                    │
    // │                                                                        │
    // │   // Path B: InferenceMode                                             │
    // │   float norm_B, min_B, max_B;                                          │
    // │   {                                                                    │
    // │       c10::InferenceMode guard;                                        │
    // │       norm_B = x.norm().item<float>();                                 │
    // │       min_B = x.min().item<float>();                                   │
    // │       max_B = x.max().item<float>();                                   │
    // │   }                                                                    │
    // │                                                                        │
    // │   // Tolerance-based comparison (handles FP noise from reordering)     │
    // │   auto close = [](float a, float b, float rtol=1e-5f) {                │
    // │       return std::abs(a - b) < rtol * std::max(std::abs(a), 1.0f);     │
    // │   };                                                                   │
    // │   assert(close(norm_A, norm_B) && "norm mismatch");                    │
    // │   assert(close(min_A, min_B) && "min mismatch");                       │
    // │   assert(close(max_A, max_B) && "max mismatch");                       │
    // │   std::cout << "InferenceMode A/B test PASSED" << std::endl;           │
    // │                                                                        │
    // │ DEPLOYMENT RECOMMENDATION:                                             │
    // │   1. Run A/B test on representative workload (em_b_wave)               │
    // │   2. If all checks pass, enable InferenceMode for diagnostic blocks    │
    // │   3. Keep NoGradGuard as fallback with runtime switch option           │
    // │   4. Monitor for any regression in production                          │
    // │                                                                        │
    // │ STATUS: A/B methodology documented - ready for implementation          │
    // │                                                                        │
    // │ INFERENCEMODE APPLICATION HISTORY (OPT Pass34 Extended):               │
    // │   Tracks files where 4-point checklist was verified and InferenceMode  │
    // │   was safely applied (vs NoGradGuard fallback).                        │
    // │                                                                        │
    // │   ENHANCED TABLE WITH RISK FACTORS (OPT Pass34 Extended):              │
    // │   ┌───────────────────────────────────────────────────────────────────────────┐
    // │   │ File                    │ View │ InPl │ Esc │ Pure │ Status    │ Decision │
    // │   │                         │ Risk │ Risk │ Risk│ Comp │           │          │
    // │   ├───────────────────────────────────────────────────────────────────────────┤
    // │   │ Legend: ✓=safe ✗=risk ?=unverified                                       │
    // │   ├───────────────────────────────────────────────────────────────────────────┤
    // │   │ wrf_sdirk3_solver.cpp   │  ✓   │  ✓   │  ✗  │  ✓   │ NoGradGrd │ REJECT   │
    // │   │   → Escape risk: tensors returned to caller                              │
    // │   │ wrf_sdirk3_diagnostics  │  ✓   │  ✓   │  ✓  │  ✓   │ CANDIDATE │ APPROVE  │
    // │   │   → All 4 safe: .item()/.norm() only, no escaping                        │
    // │   │ wrf_sdirk3_newton_*.cpp │  ✓   │  ✗   │  ✓  │  ✓   │ NoGradGrd │ REJECT   │
    // │   │   → InPlace risk: in-place ops on external residual                      │
    // │   │ wrf_sdirk3_tile_*.cpp   │  ?   │  ?   │  ?  │  ?   │ PENDING   │ REVIEW   │
    // │   │   → Unverified: needs 4-point checklist verification                     │
    // │   └───────────────────────────────────────────────────────────────────────────┘
    // │                                                                        │
    // │   RISK FACTOR DEFINITIONS:                                             │
    // │     View Risk:  Views created and potentially escaping scope           │
    // │     InPl Risk:  In-place operations on external tensors                │
    // │     Esc Risk:   Tensors escaping scope (return, assign to external)    │
    // │     Pure Comp:  Only .item()/.norm()/.min()/.max()/.mean()/.sum()      │
    // │                                                                        │
    // │   DECISION RULES:                                                      │
    // │     All ✓ → InferenceMode SAFE (APPROVE)                               │
    // │     Any ✗ → Use NoGradGuard (REJECT with reason)                       │
    // │     Any ? → Verify before deciding (REVIEW)                            │
    // │                                                                        │
    // │   APPROVAL TRACKING (OPT Pass34 Extended):                             │
    // │   ┌───────────────────────────────────────────────────────────────────┐│
    // │   │ File                    │ Decision │ Date       │ Approver │ PR#  ││
    // │   ├───────────────────────────────────────────────────────────────────┤│
    // │   │ wrf_sdirk3_solver.cpp   │ REJECT   │ 2025-01-22 │ yhlee    │ -    ││
    // │   │ wrf_sdirk3_diagnostics  │ APPROVE  │ pending    │ -        │ -    ││
    // │   │ wrf_sdirk3_newton_*.cpp │ REJECT   │ 2025-01-22 │ yhlee    │ -    ││
    // │   │ wrf_sdirk3_tile_*.cpp   │ REVIEW   │ -          │ -        │ -    ││
    // │   └───────────────────────────────────────────────────────────────────┘│
    // │                                                                        │
    // │   EXPECTED BENEFIT PRIORITIZATION (OPT Pass34 Extended):               │
    // │   ───────────────────────────────────────────────────────────────────  │
    // │   Add benefit column to determine application order:                   │
    // │                                                                        │
    // │   ┌───────────────────────────────────────────────────────────────────┐│
    // │   │ File                   │ Benefit │ Call Freq │ Tensor Size │ Prio ││
    // │   ├───────────────────────────────────────────────────────────────────┤│
    // │   │ wrf_sdirk3_diagnostics │ LOW     │ Per-step  │ Small       │ 3    ││
    // │   │   → Only .item() calls, minimal autograd overhead                 ││
    // │   │ wrf_sdirk3_tile_*.cpp  │ HIGH    │ Per-tile  │ Large 3D    │ 1    ││
    // │   │   → Hot path, many ops, significant graph construction            ││
    // │   │ wrf_sdirk3_newton_*.cpp│ MEDIUM  │ Per-iter  │ Medium      │ 2    ││
    // │   │   → Solver core, moderate ops, but has InPlace risk               ││
    // │   │ wrf_sdirk3_solver.cpp  │ LOW     │ Init/fin  │ N/A         │ 4    ││
    // │   │   → Setup/teardown only, not in hot path                          ││
    // │   └───────────────────────────────────────────────────────────────────┘│
    // │                                                                        │
    // │   BENEFIT CRITERIA:                                                    │
    // │     HIGH:   Hot path + large tensors + many operations                 │
    // │     MEDIUM: Moderate frequency + medium tensors OR many small ops      │
    // │     LOW:    Infrequent + small tensors OR few operations               │
    // │                                                                        │
    // │   PRIORITY SCORING:                                                    │
    // │     Prio = Benefit * Safety → HIGH+SAFE=1, MEDIUM+SAFE=2, LOW+SAFE=3   │
    // │     REJECT files excluded from priority (not applicable)               │
    // │                                                                        │
    // │   APPLICATION ORDER: Apply InferenceMode in Prio order (1→2→3→...)    │
    // │     → Maximize perf gain while minimizing risk exposure                │
    // │                                                                        │
    // │   APPROVAL PROCESS:                                                    │
    // │     1. Fill risk table (✓/✗/?) for all 4 factors                       │
    // │     2. Apply DECISION RULES to get Decision                            │
    // │     3. For APPROVE: Run A/B test, record Date + Approver + PR#         │
    // │     4. For REJECT: Document reason in risk table notes                 │
    // │     5. Update APPROVAL TRACKING table with final decision              │
    // │                                                                        │
    // │   CHECKLIST BEFORE ADDING TO HISTORY:                                  │
    // │   1. Verify all 4 safety conditions documented above                   │
    // │   2. Run A/B test with tolerance comparison + NaN/Inf check            │
    // │   3. Add entry with date, approver, and justification notes            │
    // │   4. If ANY doubt → use NoGradGuard and document reason                │
    // │                                                                        │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 8. TLS CACHE INVALIDATION COVERAGE VERIFICATION (OPT Pass34)           │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ PROBLEM:                                                               │
    // │   Thread-local TensorViewCache entries can become stale if:            │
    // │   1. Solver is reinitialized without TLS clear                         │
    // │   2. Grid dimensions change mid-simulation                             │
    // │   3. Fortran deallocates/reallocates arrays at same address            │
    // │                                                                        │
    // │ SOLUTION: Epoch-based cache keys (OPT Pass34)                          │
    // │   - Global atomic solver epoch counter                                 │
    // │   - Cache key = (pointer, epoch) tuple                                 │
    // │   - Epoch increments automatically invalidate stale entries            │
    // │                                                                        │
    // │ VERIFIED INVALIDATION PATHS:                                           │
    // │   ┌────────────────────────────────────────┬────────────────────┐      │
    // │   │ Path                                   │ Action             │      │
    // │   ├────────────────────────────────────────┼────────────────────┤      │
    // │   │ sdirk3_init()                          │ incrementEpoch()   │      │
    // │   │ sdirk3_cleanup()                       │ incrementEpoch()   │      │
    // │   │ wrf_sdirk3_tile_unified_impl cleanup   │ TLS.clear()        │      │
    // │   │ Grid dimension change                  │ shape mismatch→miss│      │
    // │   └────────────────────────────────────────┴────────────────────┘      │
    // │                                                                        │
    // │ MULTI-THREADED CONSIDERATIONS:                                         │
    // │   - Each thread has its own TLS cache (independent)                    │
    // │   - incrementSolverEpoch() uses atomic - all threads see new epoch     │
    // │   - Thread joining OpenMP region late will use new epoch automatically │
    // │   - Explicit TLS.clear() only affects calling thread                   │
    // │                                                                        │
    // │ FORTRAN-SIDE VERSION COUNTER (OPTIONAL):                               │
    // │   If Fortran provides an allocation version counter, pass it to:       │
    // │     getTensorView(ptr, shape, strides, fortran_version)                │
    // │   This allows cache hits across solver reinit if arrays unchanged.     │
    // │                                                                        │
    // │ EPOCH INCREMENT COVERAGE CHECKLIST (OPT Pass34 Extended):               │
    // │                                                                        │
    // │   CORE LIFECYCLE (implemented):                                        │
    // │   [x] sdirk3_init()      → incrementSolverEpoch() at start             │
    // │   [x] sdirk3_cleanup()   → incrementSolverEpoch() before reset         │
    // │   [x] TLS.clear()        → Called in tile_unified_impl cleanup paths   │
    // │   [x] Cache key          → Uses (ptr, epoch) composite key             │
    // │                                                                        │
    // │   GRID/DOMAIN CHANGES (review needed):                                 │
    // │   [ ] wrf_sdirk3_set_grid_dimensions() → Add incrementEpoch()          │
    // │   [ ] Nested domain initialization    → Check if init called           │
    // │   [ ] Regridding paths (AMR)         → Add incrementEpoch() if any     │
    // │   [ ] Domain resize operations       → Add incrementEpoch()            │
    // │                                                                        │
    // │   MEMORY REALLOCATION (review needed):                                 │
    // │   [ ] Fortran array reallocation     → Document need for version bump  │
    // │   [ ] Tile boundary changes          → Check if cleanup called         │
    // │   [ ] Memory pool resize             → Consider epoch increment        │
    // │                                                                        │
    // │   RULE: If ANY Fortran pointer could change, increment epoch OR        │
    // │         document why not needed (e.g., always new address).            │
    // │                                                                        │
    // │ STALE POINTER UNIT TEST SKELETON:                                      │
    // │                                                                        │
    // │   // TEST 1: Epoch increment invalidates cache (same pointer)          │
    // │   auto& cache = getThreadLocalTensorViewCache();                       │
    // │   float data1[100]; std::fill_n(data1, 100, 1.0f);                     │
    // │   auto t1 = cache.getTensorView(data1, {10, 10});                      │
    // │   assert(cache.getHitRate() == 0.0); // first access = miss            │
    // │   auto t2 = cache.getTensorView(data1, {10, 10});                      │
    // │   assert(t2.data_ptr() == data1); // cache hit, same pointer           │
    // │   incrementSolverEpoch();  // simulate reinit                          │
    // │   auto t3 = cache.getTensorView(data1, {10, 10});                      │
    // │   // t3 should be cache MISS because epoch changed                     │
    // │   // Even though pointer is same, (ptr, new_epoch) != (ptr, old_epoch) │
    // │                                                                        │
    // │   // TEST 2: Pointer reuse after free+realloc (OPT Pass34 Refinement)  │
    // │   // Simulates Fortran deallocate → reallocate at same address         │
    // │   float* heap1 = new float[100];                                       │
    // │   void* addr1 = heap1;                                                 │
    // │   auto t4 = cache.getTensorView(heap1, {10, 10});                      │
    // │   delete[] heap1;  // Free memory                                      │
    // │   incrementSolverEpoch();  // Epoch bump BEFORE realloc                │
    // │   float* heap2 = new float[100];  // May get same address              │
    // │   if (heap2 == addr1) {                                                │
    // │       // Same address reused - epoch keying must prevent stale hit     │
    // │       auto t5 = cache.getTensorView(heap2, {10, 10});                  │
    // │       // t5 MUST be cache MISS even though pointer matches             │
    // │       // Key: (addr1, old_epoch) != (addr1, new_epoch)                 │
    // │       std::cout << "Pointer reuse test PASSED (epoch prevented stale hit)" << std::endl;│
    // │   } else {                                                             │
    // │       std::cout << "Pointer reuse test SKIPPED (different address)" << std::endl;      │
    // │   }                                                                    │
    // │   delete[] heap2;                                                      │
    // │                                                                        │
    // │   // TEST 3: Shape change with same pointer (OPT Pass34 Refinement)    │
    // │   // Simulates grid dimension change without memory reallocation       │
    // │   float data3[200];  // Large enough for both shapes                   │
    // │   auto t6 = cache.getTensorView(data3, {10, 10});  // 100 elements     │
    // │   // Simulate grid resize: same buffer, different logical shape        │
    // │   auto t7 = cache.getTensorView(data3, {20, 5});   // Still 100 elems  │
    // │   // t7 should be cache MISS because shape differs                     │
    // │   // Cache key includes shape, so (ptr, epoch, {10,10}) != (ptr, epoch, {20,5})│
    // │   assert(t7.sizes() == torch::IntArrayRef({20, 5}));                   │
    // │                                                                        │
    // │   // TEST 3b: Same ptr, same epoch, different numel (strides change)   │
    // │   auto t8 = cache.getTensorView(data3, {10, 20});  // 200 elements now │
    // │   // t8 MUST be cache MISS - different total size                      │
    // │   assert(t8.numel() == 200);                                           │
    // │   std::cout << "Shape change test PASSED" << std::endl;                │
    // │                                                                        │
    // │   // KEY INVARIANT: Cache miss when ANY of these differ:               │
    // │   //   - pointer address                                               │
    // │   //   - solver epoch                                                  │
    // │   //   - tensor shape                                                  │
    // │   //   - tensor strides (if stride-aware caching enabled)              │
    // │                                                                        │
    // │ STATUS: Core paths verified, grid/realloc paths need review            │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 9. TLS CACHE RESET PATH REGISTRY (OPT Pass34 Refinement)               │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ PURPOSE:                                                               │
    // │   Centralized list of all paths that MUST clear/invalidate TLS cache.  │
    // │   When adding new reset paths, update this registry.                   │
    // │                                                                        │
    // │ MANDATORY TLS CLEAR PATHS:                                             │
    // │   ┌──────────────────────────────────────┬─────────────────────────┐   │
    // │   │ Path                                 │ Action Required         │   │
    // │   ├──────────────────────────────────────┼─────────────────────────┤   │
    // │   │ sdirk3_init()                        │ incrementSolverEpoch()  │   │
    // │   │ sdirk3_cleanup()                     │ incrementSolverEpoch()  │   │
    // │   │ wrf_sdirk3_tile_unified_impl cleanup │ TLS.clear() per thread  │   │
    // │   │ Grid dimension change                │ incrementSolverEpoch()  │   │
    // │   │ Domain resize/regrid                 │ incrementSolverEpoch()  │   │
    // │   │ Fortran array reallocation           │ incrementSolverEpoch()  │   │
    // │   │ Memory pool reset                    │ incrementSolverEpoch()  │   │
    // │   └──────────────────────────────────────┴─────────────────────────┘   │
    // │                                                                        │
    // │ NEW PATH CHECKLIST (copy when adding reset paths):                     │
    // │   [ ] Path name: ____________________                                  │
    // │   [ ] File/line: ____________________                                  │
    // │   [ ] Action: incrementSolverEpoch() / TLS.clear() / both              │
    // │   [ ] Reason: pointer change / shape change / lifecycle event          │
    // │   [ ] Updated this registry: YES                                       │
    // │   [ ] Unit test added: YES / N/A                                       │
    // │                                                                        │
    // │ INVARIANT: After ANY reset path executes, cached TensorViews from      │
    // │            previous epoch MUST NOT be used (epoch mismatch → miss).    │
    // │                                                                        │
    // │ VERIFICATION: Run epoch pointer-reuse test after adding new paths.     │
    // │                                                                        │
    // │ CI AUTO-CHECK (OPT Pass34 Refinement):                                 │
    // │   Add to CI pipeline to detect unregistered reset paths:               │
    // │                                                                        │
    // │   #!/bin/bash                                                          │
    // │   # check_tls_registry.sh - Verify TLS reset paths are registered      │
    // │   #                                                                    │
    // │   # Find incrementSolverEpoch() calls not in config.h registry         │
    // │   EPOCH_CALLS=$(grep -rn "incrementSolverEpoch\(\)" \                  │
    // │       --include="*.cpp" --include="*.h" \                              │
    // │       | grep -v "wrf_sdirk3_config.h\|wrf_sdirk3_tensor_cache.h")      │
    // │                                                                        │
    // │   if [ -n "$EPOCH_CALLS" ]; then                                       │
    // │       echo "Found incrementSolverEpoch() calls - verify registry:"     │
    // │       echo "$EPOCH_CALLS"                                              │
    // │       echo ""                                                          │
    // │       echo "CHECK: Are all paths listed in config.h §9 TLS REGISTRY?"  │
    // │       # Advisory only - manual verification needed                     │
    // │   fi                                                                   │
    // │                                                                        │
    // │   # Run on PR: git diff --name-only | xargs grep incrementSolverEpoch  │
    // └────────────────────────────────────────────────────────────────────────┘
    //
    // ┌────────────────────────────────────────────────────────────────────────┐
    // │ 10. ASAN/UBSAN CI PROFILE (OPT Pass34 Extended)                        │
    // ├────────────────────────────────────────────────────────────────────────┤
    // │ PURPOSE: Early detection of undefined behavior and memory issues       │
    // │ COST: 2-5x build time, 2-10x runtime slowdown → DEV-ONLY               │
    // │                                                                        │
    // │ CMAKE FLAGS FOR SANITIZER BUILDS:                                      │
    // │   # ASAN (Address Sanitizer) - heap/stack buffer overflow, use-after-free
    // │   cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
    // │            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"               │
    // │                                                                        │
    // │   # UBSAN (Undefined Behavior Sanitizer) - signed overflow, null deref │
    // │   cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize-recover"
    // │            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"             │
    // │                                                                        │
    // │   # Combined (recommended for thorough dev testing)                    │
    // │   cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    // │            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"     │
    // │                                                                        │
    // │   # TSAN (Thread Sanitizer) - data races, deadlocks (OPT Pass34 Ext)   │
    // │   cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
    // │            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"                │
    // │                                                                        │
    // │   NOTE: TSAN cannot be combined with ASAN (mutually exclusive)         │
    // │   Run TSAN in separate CI job from ASAN+UBSAN                          │
    // │                                                                        │
    // │   TSAN TARGET PATHS (concurrency-critical):                            │
    // │     - TLS cache access (TensorViewCache::get/put)                      │
    // │     - Atomic counters (solver_epoch_, pool stats)                      │
    // │     - Pool mutex (bucket_mutexes_, pool operations)                    │
    // │     - Config reads (g_sdirk3_config concurrent access)                 │
    // │                                                                        │
    // │   TSAN RESULT TRIAGE PROCEDURE (OPT Pass34 Extended):                  │
    // │   ─────────────────────────────────────────────────────────────────    │
    // │   Classify weekly TSAN reports quickly using these criteria:           │
    // │                                                                        │
    // │   ┌─────────────────┬────────────────────────────────────────────────┐ │
    // │   │ Classification  │ Criteria                                       │ │
    // │   ├─────────────────┼────────────────────────────────────────────────┤ │
    // │   │ REAL RACE       │ - Non-atomic access to shared mutable state    │ │
    // │   │ (fix required)  │ - Missing lock in concurrent write path        │ │
    // │   │                 │ - Use-after-free in multi-threaded context     │ │
    // │   │                 │ - Affects correctness (not just performance)   │ │
    // │   ├─────────────────┼────────────────────────────────────────────────┤ │
    // │   │ BENIGN          │ - Stats counters (hits_, misses_) read races   │ │
    // │   │ (suppress OK)   │ - Debug logging races (output order only)      │ │
    // │   │                 │ - LibTorch internal races (not our code)       │ │
    // │   │                 │ - Races on read-only-after-init data           │ │
    // │   └─────────────────┴────────────────────────────────────────────────┘ │
    // │                                                                        │
    // │   TRIAGE WORKFLOW:                                                     │
    // │     1. Check if race is in our code vs LibTorch (grep for "sdirk3")    │
    // │     2. If stats/logging → add to tsan_suppressions.txt as benign       │
    // │     3. If shared state → investigate, file issue, assign owner         │
    // │     4. Log triage result: Date | Race | Classification | Action        │
    // │                                                                        │
    // │   TSAN RUNTIME OPTIONS:                                                │
    // │     export TSAN_OPTIONS="history_size=7:second_deadlock_stack=1"       │
    // │                                                                        │
    // │ CI INTEGRATION RECOMMENDATION:                                         │
    // │   ┌─────────────────────────────────────────────────────────────────┐  │
    // │   │ Build Profile    │ When           │ Sanitizers │ Performance   │  │
    // │   ├─────────────────────────────────────────────────────────────────┤  │
    // │   │ PR-fast          │ Every PR       │ None       │ Full speed    │  │
    // │   │ Nightly-dev      │ Daily/nightly  │ ASAN+UBSAN │ 2-10x slower  │  │
    // │   │ Weekly-tsan      │ Weekly         │ TSAN       │ 5-15x slower  │  │
    // │   │ Release-final    │ Pre-release    │ None       │ Full speed    │  │
    // │   └─────────────────────────────────────────────────────────────────┘  │
    // │                                                                        │
    // │ CI SCRIPT EXECUTION COST MANAGEMENT (OPT Pass34 Extended):             │
    // │   As lint/validation scripts grow, manage CI time via job separation.  │
    // │                                                                        │
    // │   ┌─────────────────────────────────────────────────────────────────┐  │
    // │   │ Script Category      │ PR Jobs │ Nightly │ Rationale            │  │
    // │   ├─────────────────────────────────────────────────────────────────┤  │
    // │   │ Compilation          │ ✅      │ ✅      │ Essential            │  │
    // │   │ Unit tests           │ ✅      │ ✅      │ Essential            │  │
    // │   │ markWorkersStarted() │ ✅      │ ✅      │ Fast grep check      │  │
    // │   │ from_blob lint       │ ✅      │ ✅      │ Fast, prevents bugs  │  │
    // │   │ Domain log adoption  │ ❌      │ ✅      │ Slower, trend-only   │  │
    // │   │ safe_memcpy audit    │ ❌      │ ✅      │ Non-blocking metric  │  │
    // │   │ ASAN+UBSAN build     │ ❌      │ ✅      │ 2-10x slower         │  │
    // │   │ TSAN build           │ ❌      │ Weekly  │ 5-15x slower         │  │
    // │   │ Full trend reports   │ ❌      │ ✅      │ Git history analysis │  │
    // │   │ Deprecated API check │ ❌      │ ✅      │ Quarterly sufficient │  │
    // │   └─────────────────────────────────────────────────────────────────┘  │
    // │                                                                        │
    // │   SEPARATION CRITERIA:                                                 │
    // │     PR Jobs (every PR, <10 min target):                               │
    // │       - Compile + unit tests                                          │
    // │       - Fast lint checks (<5s each)                                   │
    // │       - Blocking on failure (exit 1)                                  │
    // │                                                                        │
    // │     Nightly Jobs (daily, <60 min target):                             │
    // │       - Full sanitizer builds                                         │
    // │       - Comprehensive lint/audit reports                              │
    // │       - Trend analysis with git history                               │
    // │       - Non-blocking (warning only) for metrics                       │
    // │                                                                        │
    // │   PROMOTION RULE: Script moves from Nightly→PR when:                  │
    // │     - Execution time <5 seconds                                       │
    // │     - False positive rate <1% over 1 month                            │
    // │     - Catches real bugs (proven value)                                │
    // │                                                                        │
    // │ ⚠️  CI CRITICAL: TSAN/ASAN MUTUAL EXCLUSION (OPT Pass34 Extended)      │
    // │   ┌─────────────────────────────────────────────────────────────────┐  │
    // │   │ TSAN and ASAN CANNOT run in the same build.                     │  │
    // │   │ Both intercept the same runtime hooks → crashes or false alarms.│  │
    // │   │ ───────────────────────────────────────────────────────────────  │  │
    // │   │ CORRECT CI CONFIG:                                              │  │
    // │   │   job_asan:  -fsanitize=address,undefined    (Nightly-dev)      │  │
    // │   │   job_tsan:  -fsanitize=thread               (Weekly-tsan)      │  │
    // │   │ ───────────────────────────────────────────────────────────────  │  │
    // │   │ WRONG: -fsanitize=address,thread  ← build will fail/crash       │  │
    // │   └─────────────────────────────────────────────────────────────────┘  │
    // │                                                                        │
    // │ KNOWN LIBTORCH INTERACTIONS:                                           │
    // │   - ASAN may report false positives in PyTorch's memory pool           │
    // │   - Suppress with: ASAN_OPTIONS=detect_odr_violation=0                 │
    // │   - UBSAN may flag intentional signed overflow in hash functions       │
    // │   - Suppress specific: -fno-sanitize=signed-integer-overflow           │
    // │                                                                        │
    // │ RUNTIME ENVIRONMENT VARIABLES:                                         │
    // │   export ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1" │
    // │   export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"            │
    // │                                                                        │
    // │ SUPPRESS FILE FOR CI REPRODUCIBILITY (OPT Pass34 Extended):            │
    // │   Create sanitizer suppress files and share as CI artifacts:           │
    // │                                                                        │
    // │   # asan_suppressions.txt (upload to CI artifacts)                     │
    // │   leak:libtorch_cpu.so                                                 │
    // │   leak:libc10.so                                                       │
    // │   odr_violation:torch::*                                               │
    // │                                                                        │
    // │   # ubsan_suppressions.txt                                             │
    // │   signed-integer-overflow:at::native::*hash*                           │
    // │   alignment:c10::*                                                     │
    // │                                                                        │
    // │   # CI usage (.github/workflows/sanitizer.yml):                        │
    // │   env:                                                                 │
    // │     ASAN_OPTIONS: suppressions=${{ github.workspace }}/asan_supp.txt   │
    // │     UBSAN_OPTIONS: suppressions=${{ github.workspace }}/ubsan_supp.txt │
    // │   steps:                                                               │
    // │     - uses: actions/upload-artifact@v3                                 │
    // │       with:                                                            │
    // │         name: sanitizer-suppressions                                   │
    // │         path: |                                                        │
    // │           asan_suppressions.txt                                        │
    // │           ubsan_suppressions.txt                                       │
    // │                                                                        │
    // │ LOG REDIRECTION FOR VERBOSE OUTPUT (OPT Pass34 Extended):              │
    // │   When sanitizer output is excessive, redirect to files and show       │
    // │   summary only in CI console:                                          │
    // │                                                                        │
    // │   env:                                                                 │
    // │     ASAN_OPTIONS: >-                                                   │
    // │       suppressions=asan_supp.txt:log_path=asan_log:                    │
    // │       print_summary=1:halt_on_error=0                                  │
    // │     UBSAN_OPTIONS: >-                                                  │
    // │       suppressions=ubsan_supp.txt:log_path=ubsan_log:                  │
    // │       print_stacktrace=0                                               │
    // │                                                                        │
    // │   # Post-run: upload logs as artifacts, show summary in CI             │
    // │   - run: |                                                             │
    // │       if [ -f asan_log.* ]; then                                       │
    // │         echo "::warning::ASAN issues detected - see artifacts"         │
    // │         echo "ASAN_ISSUES=true" >> $GITHUB_ENV                         │
    // │       fi                                                               │
    // │   - uses: actions/upload-artifact@v3                                   │
    // │     if: always()                                                       │
    // │     with:                                                              │
    // │       name: sanitizer-logs                                             │
    // │       path: |                                                          │
    // │         asan_log.*                                                     │
    // │         ubsan_log.*                                                    │
    // │                                                                        │
    // │ STATUS: Profile documented - ready for CI integration                  │
    // └────────────────────────────────────────────────────────────────────────┘
    // =========================================================================

    // FIX Round122: Separate control for device index clamp warnings
    // When true, safe_device_index() logs warnings when device index is clamped
    // (overflow >INT16_MAX or underflow <INT16_MIN). This can happen in large
    // GPU clusters with >32767 GPUs where such clamping is expected behavior.
    //
    // Note: This is independent of debug_level to allow users to:
    //   - Enable clamp warnings even at debug_level=0 (for cluster admins)
    //   - Disable clamp warnings at high debug_level (to reduce noise)
    //
    // Default: false (clamp silently - expected in large clusters)
    // Set to true if you want to be notified of device index clamping.
    bool warn_on_device_index_clamp = false;

    // AD FIX 2025-12-26: AD strict mode - enforce gradient checks even in Release builds
    // When enabled, requiresGradCheck() in metric_utils.h will throw TORCH_CHECK errors
    // instead of just warnings when requires_grad tensors are used with scalar extraction.
    // 0 = Normal mode (throw in Debug, warn in Release)
    // 1 = Strict mode (throw in both Debug and Release)
    bool ad_strict_mode = false;

    // ═══════════════════════════════════════════════════════════════════════════
    // FIX Round138: UINT64 CONFIG KEYS - POLICY SUMMARY (brief)
    // FIX Round141: DOC ROLE SEPARATION: Summary here, detailed table in config.cpp
    //   - Zero meaning: KEY-DEPENDENT (0=auto for periods, 0=sticky for throttle)
    //   - Unit: call count (not time) for all period keys
    //   - Range: [0, UINT64_MAX], but usage sites may use smaller types
    // ⚠️ OVERFLOW WARNING: Values > INT_MAX may cause issues. Safe max: ~10^9.
    // See wrf_sdirk3_config.cpp for detailed policy table and setter documentation.
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // PARITY FIX 2025-12-24: Separate grid metric memcmp period from debug_level
    // Controls how often refreshGridMetricEpochs() does full CPU memcmp for change detection.
    // 0 = auto (1 for debug_level>=2, 10 otherwise)
    // 1 = strict (every call - required for moving nest accuracy)
    // N>1 = every N calls (performance mode, may miss up to N-1 in-place changes)
    uint64_t grid_metric_memcmp_period = 0;

    // PARITY FIX 2025-12-24: Separate CUDA sampling periods for finer-grained control.
    // These allow independent tuning of GPU sample rates for different metric categories.
    // 0 = auto (uses grid_metric_memcmp_period if set, else debug_level-based default)
    // N>0 = sample every N calls on GPU (CPU always samples every call)
    uint64_t cuda_grid_metric_sample_period = 0;  // For refreshGridMetricEpochs() CUDA path
    uint64_t cuda_phbase_sample_period = 0;       // For updatePhBaseSignature() CUDA path

    // FIX 2025-12-27: Runtime-tunable sample check period for LatCpuCache
    // Controls how often the cache verifies sample signature for xlat changes
    // 0 = auto (100 for Release, 10 for Debug builds)
    // N>0 = check every N cache hits
    uint64_t lat_cpu_sample_check_period = 0;

    // FIX 2025-12-29 Batch9 Issue 3: Runtime-tunable async threshold for spatial derivatives
    // Controls minimum grid elements for CPU async parallelism in BatchSpatialDerivatives
    // 0 = use default (131072 = 64*64*32)
    // N>0 = use N as threshold (larger grids use async, smaller use serial)
    // GPU/MPS tensors always use serial (GPU handles parallelism internally)
    int64_t spatial_async_min_elems = 0;

    // Numerical scheme options
    int advection_order = 5;        // Advection order (1, 3, 5)
    int diffusion_option = 0;       // 0=none, 1=2nd order, 2=4th order

    // PARITY FIX 2025-12-23: Configurable ztop parameters for fallback rdnw/rdn computation
    // z_top_min: Minimum ztop value for clamping (prevents unrealistic values in shallow/idealized cases)
    // z_top_default: Default ztop when no geopotential data available
    // Set z_top_min=0 to disable clamping for idealized shallow domain tests
    float z_top_min = 1000.0f;       // Minimum ztop (meters) - clamp floor for shallow domains
    float z_top_default = 10000.0f;  // Default ztop when ph_base unavailable (10km typical)

    // Omega/vertical flux computation for Newton iterations
    // Blending factor for w_ref vs current w in omega = mu * w computation
    // 0.0 = use pure w_ref (reference state, may cause JVP mismatch if w_ref=0)
    // 1.0 = use pure current w (Fortran parity - matches WRF exactly)
    // 0.5 = balanced blend (stability knob - reduce if Newton diverges)
    // FORTRAN PARITY (2025-12-05): Changed default from 0.5 to 1.0 for exact Fortran match
    // v20.9: GMRES restart stagnation threshold.
    // After each restart cycle, if rel_error > this threshold, skip remaining restarts.
    // Set to 1.0 to DISABLE early termination (let all restarts run).
    // Set to 0.95 to restore previous behavior (aggressive early exit).
    // Default 1.0 = disabled — ensures GMRES uses all allocated restarts.
    float gmres_stagnation_threshold = 1.0f;

    // v20.9d: Newton trust-region GMRES quality gate threshold.
    // When gmres_rel_error > this threshold, the trust-region unconditionally
    // rejects the step and shrinks the radius.  If ALL attempts are rejected,
    // dK_scaled is set to zero (no Newton update).
    // Set to 1.0 to DISABLE the hard reject (let trust-region evaluate naturally).
    // Set to 0.9 to restore previous behavior (reject any GMRES with <10% reduction).
    // Default 1.0 = disabled — allows the trust-region/line-search to decide.
    float newton_gmres_quality_threshold = 1.0f;

    // v20.8: Smooth sign function delta for upwind advection and CFL damping.
    // Replaces discontinuous sign(vel) with smooth approximation:
    //   smooth_sign(v) = v / sqrt(v² + δ²)
    // This eliminates FD JVP artifacts where sign() flips at vel≈0.
    // em_b_wave at timestep 1: w ≈ 5e-7 m/s, FD perturbation ≈ 2e-6/DOF.
    // δ must be >> perturbation to avoid sign-flip artifacts.
    // δ = 1e-3 m/s: smooth over [-1mm/s, +1mm/s] — negligible vs physical velocities.
    // Set to 0 to disable smoothing (revert to hard sign + detach).
    float sign_smooth_delta = 1e-3f;

    float omega_w_blend = 1.0f;     // Blend factor: omega = mu * (blend*w + (1-blend)*w_ref)
                                     // 1.0 = fully implicit w (no stabilization) — BASELINE
                                     // 0.0 = fully lagged w_ref (semi-implicit, most stable)
                                     // 0.5 = balanced blend
    bool omega_update_ref_per_newton = false;  // FIX 2026-02-05: false = keep w_ref fixed per stage
                                               // Enables lagged advection to prevent nonlinear feedback

    // IMEX split control (2026-01-31)
    // When false (default): Newton uses Full RHS — baseline behavior
    // When true: Newton uses ImplicitOnly RHS + frozen F_exp from stage start
    // WARNING: IMEX + blend=1.0 causes explosion. Use with blend<=0.5 and tol>=0.03
    bool imex_enabled = false;

    // IMEX split mode (2026-02-01; mode 3 added 2026-02-18)
    // 0 = off (full implicit baseline); if imex_enabled==true, auto-maps to 1
    // 1 = frozen IMEX (existing imex_enabled behavior)
    // 2 = post-solve IMEX SDIRK3 (Newton fast-only, K_slow at U_converged)
    // 3 = post-solve IMEX ARK324L2SA (4-stage, Newton fast-only, explicit first stage)
    // PRIORITY: imex_split_mode > imex_enabled. When split_mode != 0, imex_enabled is ignored.
    int imex_split_mode = 0;

    // Effective IMEX mode actually used by the solver. Single source of truth for the
    // imex_enabled backward-compat promotion replicated at tile_unified_impl.cpp:5275-5277
    // (stage loop) and :8156-8158 (Newton compute_rhs): raw mode 0 + imex_enabled -> frozen mode 1.
    // HEVI activation MUST gate on this (effective_imex_split_mode() >= 1), NOT the raw
    // imex_split_mode, so the imex_enabled mode-1 path (which runs RhsMode::ImplicitOnly) keeps the
    // RHS operator J_fast and the preconditioner M consistently HEVI.
    int effective_imex_split_mode() const {
        return (imex_split_mode == 0 && imex_enabled) ? 1 : imex_split_mode;
    }

    // DA tangent-linear consistency flags for IMEX post-solve modes (mode=2/3)
    // imex_slow_in_tangent: true = K_slow participates in AD graph (correct tangent)
    //                       false = K_slow detached (model-error assumption)
    bool imex_slow_in_tangent = true;

    // imex_phys_in_tangent: true = F_phys included in AD graph (exact tangent)
    //                       false = F_phys added to forward but detached from graph
    //                               (weak-constraint: absorbed by Q_phys)
    bool imex_phys_in_tangent = false;

    // Stage-1 explicit switch (default-off, regression-neutral).
    // false: baseline SDIRK stage-1 implicit solve (Newton/GMRES).
    // true: stage-1 uses explicit RHS evaluation K1 = F(U_stage), skipping Newton/GMRES.
    // Note: this changes stage-1 solve policy without altering the Butcher coefficients.
    bool stage1_explicit = false;

    // Stage-3 warm-start switch (default-off, regression-neutral).
    // false: skip Stage-3 predictor preconditioned correction path (current stable default).
    // true: allow Stage-3 warm-start path when stage3 budget override is active.
    // Recommended only for controlled experiments.
    bool stage3_warmstart = false;

    // Preconditioner-RHS scope consistency (2026-02-01)
    // When true, preconditioner diagonal terms are auto-gated to match the Jacobian scope.
    // mode=0: all terms; mode>=1: fast-only (Newton J is fast-only in both frozen and post-solve).
    // When false, preconditioner always includes all diagonal terms regardless of IMEX mode.
    bool precond_match_rhs = true;

    // Per-term overrides: force a term INTO the preconditioner even when precond_match_rhs
    // would exclude it. Useful for stabilizing mode>=1 if a slow term's diagonal is large.
    // Only effective when precond_match_rhs=true AND the term would otherwise be excluded.
    bool precond_extra_rayleigh = false;
    bool precond_extra_wdamp = false;
    bool precond_extra_vdiff = false;
    bool precond_extra_divergence = false;

    float diff_coef_h = 0.0f;       // Horizontal diffusion coefficient
    float diff_coef_v = 0.0f;       // Vertical diffusion coefficient
    float diff4_coef = 0.0f;        // Biharmonic diffusion coefficient
    bool use_stress_tensor = true;  // Use full stress tensor for vertical diffusion
    float kdamp = 0.2f;             // Divergence damping coefficient (from WRF namelist dampcoef) - DEFAULT CHANGED FOR STABILITY
    float khdif = 500.0f;           // Horizontal eddy diffusivity (m²/s) from namelist - DEFAULT CHANGED FOR STABILITY
    float kvdif = 100.0f;           // Vertical eddy diffusivity (m²/s) from namelist - DEFAULT CHANGED FOR STABILITY

    // Fortran parity options (2025-12-05)
    // These control strict 1:1 parity with WRF Fortran code
    bool mu_tend_fortran_parity = true;   // Disable mean-subtract and clamp for μ_tend (default: true for parity)
    bool ph_use_rdnw_not_rdzw = true;     // Use rdnw (eta coords) not rdzw (physical) for φ vertical advection
    bool buoyancy_use_current_w = true;   // Use current w instead of w_ref for buoyancy term

    // Boundary condition options
    int lateral_bc_option = 1;      // 0=periodic, 1=open, 2=specified
    bool periodic_x = false;        // Periodic boundary in X direction
    bool periodic_y = false;        // Periodic boundary in Y direction
    int top_bc_option = 1;          // 0=rigid, 1=rayleigh damping
    float rayleigh_damp_coef = 0.2f;  // Rayleigh damping coefficient
    float rayleigh_damp_depth = 5000.0f; // Damping layer depth (m)
    bool use_max_height_for_damping = false; // FIX 2025-12-28: Use max instead of mean for terrain

    // Boundary relaxation zone configuration
    int relax_zone = 5;             // Width of relaxation zone (grid points)
    float relax_tau = 600.0f;       // Relaxation time scale (seconds)
    float relax_zone_coef = 0.2f;   // Relaxation coefficient
    bool open_xs = false;           // Open boundary at x-start
    bool open_xe = false;           // Open boundary at x-end
    bool open_ys = false;           // Open boundary at y-start
    bool open_ye = false;           // Open boundary at y-end
    bool specified = true;          // Use specified boundary conditions
    bool nested = false;            // Nested domain flag (affects vxgm/curvature boundary reduction)
    bool moving_nest = false;       // FIX 2025-12-28: Moving nest flag - requires aggressive cache checking

    // Tile boundary optimization
    bool enable_boundary_optimization = false;  // Enable multi-tile boundary optimization
    
    // Halo exchange configuration
    bool use_wrf_halo_exchange = true;  // Use WRF's RSL_LITE halo exchange (disable C++ version)
    int halo_width = 5;                 // Halo width for boundary handling (default 5 for 5th-order schemes)

    // Inter-stage halo exchange for multi-tile implicit accuracy (level (b)):
    //   (a) Exact: exchange every Newton iteration (expensive, not implemented)
    //   (b) Accurate: exchange between SDIRK stages (2 exchanges/step) ← this flag
    //   (c) Approximate: post-timestep only via WRF (current default)
    // Enable for multi-tile runs where stencil width > 1 reaches into stale halos.
    // Single-tile runs should leave this false (no benefit, tile boundary == domain boundary).
    bool enable_stage_halo_exchange = false;
    // Override RSL-LITE priority and allow C++ stage-boundary halo sync.
    // Default-off to preserve baseline behavior.
    bool stage_boundary_sync = false;

    // AD-aware halo exchange for DA mode (split_mode>=2, slow_in_tangent=true)
    // When enabled + DA mode + multi-tile: uses custom autograd function for halo exchange
    // that preserves the computational graph for cross-tile tangent propagation.
    // Requires: CPU/FP32 tensors, halo_width >= 3, C++ halo exchange (not WRF RSL_LITE),
    //           sym/open BC at all physical boundaries. Falls back with warning otherwise.
    // In DA mode: hard error if AD halo requested but not viable (prevents silent stale halos).
    bool enable_ad_halo_exchange = false;

    // Physical constants and reference values
    float t_ref = 300.0f;           // Reference potential temperature (K) (WRF naming: t)
    float pressure_ref = 100000.0f; // Reference pressure (Pa)
    float rho_ref = 1.225f;         // Reference density (kg/m³)
    float coriolis_f = 1.0e-4f;     // Coriolis parameter (1/s) - f-plane
    
    // Map projection and coordinate system
    int map_proj = 1;               // 0=lat-lon, 1=Lambert, 2=polar stereographic, 3=Mercator

    // Curvature term control (2025-12-05 PARITY FIX)
    // Fortran: do_curvature in namelist controls curvature for all map projections
    // TEMPORARILY DISABLED for debugging - vxgm boundary logic needs fixes
    bool do_curvature = false;       // Enable curvature terms for momentum equations
    bool polar = false;             // Polar boundary condition flag (affects curvature formula choice)
    // NOTE: Fortran map_proj values: 1=Lambert, 2=Polar Stereo, 3=Mercator, 6=lat-lon(Cassini)
    // For map_proj==6 OR polar: use tan(lat) formula
    // Otherwise: use vxgm (velocity cross grad mapfactor) formula

    // W-damping parameters for acoustic mode control
    float w_damp_alpha = 0.3f;      // W-damping coefficient
    float w_crit_cfl = 1.0f;        // Critical vertical CFL for W-damping
    // PR 9C (WRF W-damping parity): the WRF namelist activation flag and the
    // implicit-vertical-advection switch, wired from config_flags by the
    // Fortran bridge. Defaults match the WRF Registry (both 0), so the
    // parity-gated damping is OFF unless the WRF namelist enables it —
    // exactly WRF's own default behavior.
    int wrf_w_damping = 0;          // config_flags%w_damping (Registry default 0)
    int wrf_zadvect_implicit = 0;   // config_flags%zadvect_implicit / ieva (default 0)
    // The WRF namelist w_crit_cfl (Registry default 1.0) — a SEPARATE field
    // from the legacy sdirk3 w_crit_cfl knob above, because the bridge sets
    // that one from config_flags%sdirk3_w_crit_cfl AFTER the WRF
    // pass-through (it would silently overwrite a shared field). The
    // parity-gated damping reads THIS value for its excess offset (and for
    // the gate when ieva > 0).
    float wrf_w_crit_cfl = 1.0f;
    bool damp_t = false;            // Apply damping to potential temperature (WRF naming: t)
    
    // Time step for dynamics (used in W-damping CFL calculation)
    float dt_dynamics = 1.0f;       // Dynamics time step (seconds)
    
    // Test mode configuration
    bool enable_test_tendencies = false;  // Enable minimal test tendencies
    float test_w_damp_coef = 0.0001f;    // Vertical velocity damping coefficient
    float test_t_relax_coef = 0.00001f;     // Potential temperature relaxation coefficient (WRF naming: t)
    
    // CFL monitoring
    bool check_cfl = true;          // Enable CFL condition checking
    float cfl_target = 0.5f;        // Target CFL number for warnings
    
    // Performance optimization options
    bool use_vectorized_advection = false;  // Use vectorized advection operations
    bool use_cache_blocking = false;        // Use cache-blocked vertical operations
    int cache_block_size = 4;               // Vertical cache block size
    int horizontal_block_size = 64;         // Horizontal cache block size

    // PERF FIX 2025-12-25: c1f/c2f signature verification options
    // 3-point may miss interior-only changes; 5-point adds 1/4 and 3/4 samples
    int c1f_c2f_signature_points = 3;       // Signature points: 3 (fast) or 5 (safer)
    int c1f_c2f_full_verify_interval = 0;   // CPU memcmp every N calls (0=disable, >0 for paranoid mode)
    int c1f_c2f_signature_period = 1;       // GPU→CPU signature sampling period (1=every call, N>1=every N calls)

    // GRID METRIC VERIFY 2025-12-25: rdnw/rdn periodic full verification options
    // At debug_level < 2, O(n) comparison is disabled to save cost. This option enables
    // periodic full comparison every N calls to catch non-sample changes (e.g., moving nest).
    // Set to 0 to disable (rely on signature only), or >0 for periodic O(n) verification.
    //
    // FIX 2025-12-29 Batch10 Issue 5: SAFETY RECOMMENDATION
    // When moving_nest=false, signature checks are skipped for performance.
    // If invalidateVerticalMetricCaches() is missed after in-place grid modifications,
    // stale cached values could be used. To prevent this:
    // - Set grid_metric_full_verify_period=100 for periodic safety checks (recommended)
    // - Set grid_metric_full_verify_period=1000 for infrequent checks (minimal overhead)
    // - Set grid_metric_full_verify_period=0 only when grid is truly static
    int grid_metric_full_verify_period = 0;  // O(n) compare every N calls (0=disable, recommend 100)

    // Advanced memory management
    bool use_optimized_pool = false;        // Use OptimizedTensorPool for memory management
    int pool_bucket_count = 8;              // Number of size buckets for pool
    bool track_memory_stats = false;        // Track detailed memory statistics
    size_t pool_max_size_mb = 4096;         // Maximum pool size in MB
    
    // Enhanced validation options
    int validation_level = 1;               // 0=OFF, 1=CRITICAL, 2=STANDARD, 3=PARANOID
    bool check_staggered_consistency = false; // Verify staggered grid consistency
    float validation_min_valid = -1e10f;    // Minimum valid value for general fields
    float validation_max_valid = 1e10f;     // Maximum valid value for general fields
    bool collect_validation_stats = false;  // Collect detailed validation statistics
    
    // Conservation checking
    float conservation_tol_mass = 1e-8f;    // Mass conservation tolerance
    float conservation_tol_energy = 1e-6f;  // Energy conservation tolerance
    float conservation_tol_momentum = 1e-7f; // Momentum conservation tolerance
    
    // Performance benchmarking options
    bool enable_benchmarking = false;       // Enable performance profiling
    int benchmark_warmup_iter = 5;          // Warmup iterations for benchmarks
    int benchmark_measure_iter = 20;        // Measurement iterations
    bool benchmark_memory_tracking = true;  // Track memory usage in benchmarks
    std::string benchmark_output_format = "json"; // Output format: json, csv, text
    bool benchmark_auto_export = false;     // Auto-export results after run
    
    // Load from namelist-style string
    void load_from_namelist(const std::string& namelist_content);
    
    // Load from environment variables
    void load_from_env();
    
    // K-Slab Advanced Vertical Operations
    bool use_advanced_kslab = false;        // Enable k-slab optimization
    int kslab_base_size = 8;                // Base slab size (auto-tuned if kslab_auto_tune)
    int kslab_min_size = 4;                 // Minimum slab size
    int kslab_max_size = 16;                // Maximum slab size
    bool kslab_auto_tune = true;            // Enable adaptive tuning
    
    // K-Slab Vectorization
    bool kslab_enable_avx2 = true;          // Enable AVX2 SIMD
    bool kslab_enable_avx512 = false;       // Enable AVX512 SIMD (if available)
    int kslab_vector_width = 8;             // Vector width (floats)
    
    // K-Slab Operation Fusion
    bool kslab_enable_fusion = true;        // Enable operation fusion
    bool kslab_fuse_advection_diffusion = true; // Fuse vertical advection+diffusion
    
    // K-Slab Performance
    int kslab_num_threads = 0;              // 0=auto, >0=fixed thread count
    bool kslab_thread_per_j = false;        // Thread parallelism strategy
    bool kslab_enable_prefetch = true;      // Enable data prefetching
    int kslab_prefetch_distance = 2;        // Slabs to prefetch ahead
    
    // K-Slab Monitoring
    bool kslab_collect_metrics = true;      // Collect performance metrics
    bool kslab_adaptive_tuning = true;      // Runtime performance tuning
    bool kslab_print_metrics = true;        // Print metrics at intervals
    int kslab_metric_interval = 3600;       // Metric print interval (seconds)
    
    // Normalize adaptive thresholds: clamp ranges + enforce min gap.
    // Call after any path that modifies adaptive_high/low_threshold.
    void normalize_adaptive_thresholds();

    // Validate configuration
    bool validate() const;
    
    // Print configuration
    void print() const;
};

// ═══════════════════════════════════════════════════════════════════════════
// FIX Round123: GLOBAL CONFIG MODIFICATION POLICY
// ═══════════════════════════════════════════════════════════════════════════
//
// g_sdirk3_config is the global configuration instance. While runtime
// modification is supported via wrf_sdirk3_set_config_*() APIs, there are
// important considerations:
//
// THREAD SAFETY:
//   - Modifications are NOT thread-safe by default
//   - Modify config ONLY from the main thread before parallel regions
//   - WRF's typical usage pattern (one-time setup before simulation) is safe
//
// CONSISTENCY:
//   - Some config changes require explicit cache invalidation or state reset
//   - warn_throttle_count: Call resetPerSolverState() for deterministic behavior
//   - moving_nest: Synced with metric_utils::setMovingNestMode() automatically
//
// RECOMMENDED PATTERN:
//   1. Set all config values during initialization (before any solver creation)
//   2. Avoid runtime config changes during simulation (potential race conditions)
//   3. If runtime changes are required, pause parallel activity first
//
// IMMUTABILITY OPTION:
//   For production deployments, consider making config effectively immutable:
//   - Load config from namelist once at startup
//   - Never call set_config_*() during simulation
//   - This eliminates race conditions and cache consistency concerns
//
// ───────────────────────────────────────────────────────────────────────────
// RUNTIME CONFIG CHANGE MONITORING (OPT Pass34 Extended, debug only)
// ───────────────────────────────────────────────────────────────────────────
//
// PURPOSE: Detect accidental config modifications after worker threads start.
//          This is a debug-only feature to catch runtime tuning misuse early.
//
// IMPLEMENTATION PATTERN (add to wrf_sdirk3_config.cpp):
//
//   // Global flag set once parallel region starts
//   static std::atomic<bool> g_workers_started{false};
//
//   void markWorkersStarted() {
//       g_workers_started.store(true, std::memory_order_release);
//   }
//
//   // Call from set_config_* functions (debug builds only):
//   #ifndef NDEBUG
//   inline void warnIfWorkersStarted(const char* config_name) {
//       if (g_workers_started.load(std::memory_order_acquire)) {
//           SDIRK3_WARN_ONCE("CONFIG_RACE",
//               "Config '" << config_name << "' modified after workers started. "
//               "This may cause race conditions or inconsistent behavior.");
//       }
//   }
//   #else
//   inline void warnIfWorkersStarted(const char*) {}  // No-op in release
//   #endif
//
// INTEGRATION POINTS:
//   1. Call markWorkersStarted() at OpenMP parallel region entry or thread pool init
//   2. Each set_config_*() function calls warnIfWorkersStarted(name)
//   3. Debug build: WARN_ONCE triggers once per config field on violation
//
// ─────────────────────────────────────────────────────────────────────────
// markWorkersStarted() CALL SITE GUIDE (OPT Pass34 Extended)
// ─────────────────────────────────────────────────────────────────────────
//
// CORRECT CALL LOCATIONS (choose ONE):
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │ Location                    │ When to Use                           │
//   ├──────────────────────────────────────────────────────────────────────┤
//   │ sdirk3_tile_solver_init()   │ After solver fully initialized,       │
//   │   (fortran_c_interface.cpp) │ before returning to Fortran caller.   │
//   │                             │ RECOMMENDED for WRF integration.      │
//   ├──────────────────────────────────────────────────────────────────────┤
//   │ First OpenMP parallel region│ At entry to first #pragma omp parallel│
//   │   (tile_unified_impl.cpp)   │ if solver init is not clear boundary. │
//   ├──────────────────────────────────────────────────────────────────────┤
//   │ sdirk3_step() first call    │ At start of first time step, before   │
//   │   (interface_zerocopy.cpp)  │ any parallel work begins.             │
//   └──────────────────────────────────────────────────────────────────────┘
//
// TIMING DIAGRAM:
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │ Phase          │ Config Changes │ markWorkersStarted │ Workers      │
//   ├──────────────────────────────────────────────────────────────────────┤
//   │ 1. Namelist    │ ✅ ALLOWED     │                    │              │
//   │ 2. Solver init │ ✅ ALLOWED     │ ← CALL HERE        │              │
//   │ 3. First step  │ ⚠️ WARNED     │ (already set)      │ ← RUNNING    │
//   │ 4. Simulation  │ ⚠️ WARNED     │ (already set)      │ ← RUNNING    │
//   └──────────────────────────────────────────────────────────────────────┘
//
// EXAMPLE PLACEMENT (fortran_c_interface.cpp):
//   extern "C" void sdirk3_tile_solver_init(...) {
//       // ... solver initialization code ...
//       // Config is now frozen for this run
//       markWorkersStarted();  // ← ADD HERE
//   }
//
// DO NOT CALL FROM:
//   - Config loading routines (too early, config still being set)
//   - Destructor paths (meaningless, simulation already done)
//   - Per-iteration callbacks (too late, workers already active)
//
// CI CHECKLIST ITEM (OPT Pass34 Extended):
//   Add to PR review checklist to prevent omission:
//
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │ □ CONFIG FREEZE CHECK (for PRs touching solver init or threading)  │
//   │                                                                     │
//   │   Verify markWorkersStarted() call exists:                         │
//   │   grep -n "markWorkersStarted" fortran_c_interface.cpp             │
//   │                                                                     │
//   │   Expected: 1 hit in sdirk3_tile_solver_init() or equivalent       │
//   │   If missing: Add call at end of solver initialization             │
//   │                                                                     │
//   │   CI SCRIPT (add to pipeline):                                     │
//   │   #!/bin/bash                                                      │
//   │   CALL_COUNT=$(grep -c "markWorkersStarted" \                      │
//   │       external/libtorch_wrf/sdirk3/fortran_c_interface.cpp)        │
//   │   if [ "$CALL_COUNT" -eq 0 ]; then                                 │
//   │       echo "::error::markWorkersStarted() call missing!"           │
//   │       echo "Add to sdirk3_tile_solver_init() per config.h guide"   │
//   │       exit 1                                                       │
//   │   fi                                                               │
//   │   echo "OK: markWorkersStarted() present ($CALL_COUNT calls)"      │
//   └─────────────────────────────────────────────────────────────────────┘
//
// ─────────────────────────────────────────────────────────────────────────
//
// EXAMPLE USAGE:
//   void wrf_sdirk3_set_config_int(const char* name, int value) {
//       warnIfWorkersStarted(name);
//       // ... existing implementation ...
//   }
//
// DETECTION AUDIT LOG:
//   [DATE]: Config field [name] modified post-workers - investigate call stack
//
// ═══════════════════════════════════════════════════════════════════════════
// Global configuration instance
extern SDIRK3Config g_sdirk3_config;

// ═══════════════════════════════════════════════════════════════════════════
// C/Fortran Interface for Runtime Configuration
// ═══════════════════════════════════════════════════════════════════════════
//
// SSoT (Single Source of Truth): wrf_sdirk3_config.cpp set_config_* functions
// Last synced: 2025-01-19 (OPT Pass33+)
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ wrf_sdirk3_set_config_bool(name, value)  - Boolean options (0/1)       │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ adaptive_timestep      │ Enable adaptive time stepping                 │
// │ use_physics            │ Enable physics terms                          │
// │ frozen_physics_jvp     │ Freeze physics in JVP computation             │
// │ use_autograd           │ Enable automatic differentiation              │
// │ use_jvp_cache          │ Cache JVP computations                        │
// │ check_nan              │ Enable NaN checking                           │
// │ check_conservation     │ Enable conservation checking                  │
// │ check_cfl              │ Enable CFL checking                           │
// │ nk_adaptive_tol        │ Use adaptive tolerance in Newton-Krylov       │
// │ implicit_acoustic      │ Treat acoustic waves implicitly               │
// │ implicit_gravity       │ Treat gravity waves implicitly                │
// │ implicit_rayleigh      │ Treat Rayleigh damping implicitly             │
// │ implicit_wdamp         │ Treat W-damping implicitly                    │
// │ implicit_vdiff         │ Treat vertical diffusion implicitly           │
// │ implicit_hdiff         │ Treat horizontal diffusion implicitly         │
// │ implicit_divergence    │ Treat divergence damping implicitly           │
// │ precond_diagonal_only  │ Use only diagonal preconditioner              │
// │ precond_block_jacobi   │ Use block Jacobi preconditioner               │
// │ jvp_use_forward_diff   │ Use forward diff for JVP                      │
// │ ad_strict_mode         │ Enable AD strict mode (throw in Release)      │
// │ use_max_height_for_damping │ Use max height for damping                │
// │ moving_nest            │ Enable moving nest mode                       │
// │ warn_on_device_index_clamp │ Warn on device index overflow/underflow   │
// └─────────────────────────────────────────────────────────────────────────┘
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ wrf_sdirk3_set_config_int(name, value)  - Integer options              │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ debug_level            │ Debug verbosity (0=off, 1=warn, 2=info, 3=verbose)│
// │ debug_sample_period    │ Standard diagnostic sampling period (0=every) │
// │ debug_heavy_sample_period │ Heavy diagnostic sampling period           │
// │ max_newton_iter        │ Maximum Newton iterations                     │
// │ gmres_restart          │ GMRES restart parameter                       │
// │ max_krylov_iter        │ Maximum Krylov iterations                     │
// │ precond_type           │ Preconditioner type                           │
// │ precond_block_size     │ Block size for block Jacobi                   │
// │ jvp_method             │ JVP method (0=FD, 1=AD, 2=dual, 3=optimized)  │
// │ n_threads              │ Number of threads                             │
// │ advection_order        │ Advection order (1, 3, 5)                     │
// │ diffusion_option       │ Diffusion option (0=none, 1=2nd, 2=4th)       │
// │ lateral_bc_option      │ Lateral boundary condition option             │
// │ top_bc_option          │ Top boundary condition option                 │
// │ validation_level       │ Tensor validation level (0-3)                 │
// │ validation_sampling_threshold │ Validation sampling threshold          │
// │ warn_throttle_count    │ Warning throttle count (0=sticky)             │
// │ grid_metric_memcmp_period │ Grid metric memcmp period                  │
// │ cuda_grid_metric_sample_period │ CUDA grid metric sample period        │
// │ cuda_phbase_sample_period │ CUDA ph_base sample period                 │
// │ lat_cpu_sample_check_period │ Lat CPU cache sample check period        │
// │ grid_metric_full_verify_period │ Grid metric full verify period        │
// │ spatial_async_min_elems │ Spatial async minimum elements               │
// │ c1f_c2f_signature_points │ c1f/c2f signature verification points (3/5)│
// │ c1f_c2f_full_verify_interval │ c1f/c2f full memcmp interval            │
// │ c1f_c2f_signature_period │ c1f/c2f signature sampling period           │
// └─────────────────────────────────────────────────────────────────────────┘
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ wrf_sdirk3_set_config_float(name, value)  - Float options              │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ newton_tol             │ Newton solver tolerance                       │
// │ newton_rtol            │ Newton relative tolerance                     │
// │ krylov_tol             │ Krylov solver tolerance                       │
// │ safety_factor          │ Time step safety factor                       │
// │ error_tol              │ Error tolerance                               │
// │ dt_min_factor          │ Minimum dt factor                             │
// │ dt_max_factor          │ Maximum dt factor                             │
// │ diff_coef_h            │ Horizontal diffusion coefficient              │
// │ diff_coef_v            │ Vertical diffusion coefficient                │
// │ diff4_coef             │ 4th-order diffusion coefficient               │
// │ rayleigh_damp_coef     │ Rayleigh damping coefficient                  │
// │ rayleigh_damp_depth    │ Rayleigh damping depth (m)                    │
// │ cfl_target             │ Target CFL number                             │
// │ jvp_epsilon            │ JVP finite difference epsilon                 │
// │ khdif                  │ Horizontal diffusion (WRF namelist)           │
// │ kvdif                  │ Vertical diffusion (WRF namelist)             │
// │ kdamp / dampcoef       │ Damping coefficient (aliases)                 │
// │ z_top_min              │ Minimum ztop for clamping                     │
// │ z_top_default          │ Default ztop when unavailable                 │
// └─────────────────────────────────────────────────────────────────────────┘
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ wrf_sdirk3_set_config_uint64(name, value)  - 64-bit unsigned options   │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ warn_throttle_count         │ Warning throttle (0=sticky/never re-warn)│
// │ grid_metric_memcmp_period   │ Grid metric memcmp period                │
// │ cuda_grid_metric_sample_period │ CUDA grid metric sample period        │
// │ cuda_phbase_sample_period   │ CUDA ph_base sample period               │
// │ lat_cpu_sample_check_period │ Lat CPU cache sample check period        │
// └─────────────────────────────────────────────────────────────────────────┘
//
// NOTE: int setter accepts uint64 keys with clamping (negative→0).
//       Use uint64 setter for values > INT_MAX.
//
// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass34 Extended: NUMERICAL STABILITY TESTING (FP32/FP64 MIXED PRECISION)
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: FP32/FP64 mixed paths can produce subtly different results.
// Critical areas: CFL computation, time integration, pressure gradient.
//
// A/B TEST METHOD (small-scale, quick iteration):
// ┌────────────────────────────────────────────────────────────────────────────┐
// │ 1. ISOLATE COMPONENT                                                       │
// │    - Single function: computeCFL(), pressureGradient(), timeIntegrate()   │
// │    - Small grid: 10x10x5 or 20x20x10 (fast iteration)                     │
// │    - Fixed seed: torch::manual_seed(42) for reproducibility               │
// │                                                                            │
// │ 2. RUN BOTH PRECISIONS                                                     │
// │    // Path A: Full FP64                                                    │
// │    torch::set_default_dtype(torch::kFloat64);                             │
// │    auto result_f64 = computeCFL(input.to(torch::kFloat64));               │
// │                                                                            │
// │    // Path B: FP32 (production)                                           │
// │    torch::set_default_dtype(torch::kFloat32);                             │
// │    auto result_f32 = computeCFL(input.to(torch::kFloat32));               │
// │                                                                            │
// │ 3. COMPARE WITH TOLERANCE                                                  │
// │    auto diff = (result_f32.to(torch::kFloat64) - result_f64).abs();       │
// │    auto rel_err = diff / (result_f64.abs() + 1e-10);                      │
// │    auto max_rel_err = rel_err.max().item<double>();                       │
// │                                                                            │
// │    // Thresholds (component-specific):                                    │
// │    //   CFL:               1e-5 (accumulation-sensitive)                  │
// │    //   Pressure gradient: 1e-4 (differencing amplifies error)            │
// │    //   Time integration:  1e-3 (iterative, error accumulates)            │
// │    TORCH_CHECK(max_rel_err < THRESHOLD, "Precision drift: ", max_rel_err);│
// └────────────────────────────────────────────────────────────────────────────┘
//
// CRITICAL COMPONENTS TO TEST:
// ┌─────────────────────────┬──────────┬─────────────────────────────────────┐
// │ Component               │ Threshold│ Notes                               │
// ├─────────────────────────┼──────────┼─────────────────────────────────────┤
// │ CFL computation         │ 1e-5     │ Max/min operations, division        │
// │ Pressure gradient       │ 1e-4     │ Finite differencing amplifies error │
// │ Advection terms         │ 1e-4     │ Upwind/centered schemes             │
// │ SDIRK stage values      │ 1e-3     │ Iterative, depends on Newton tol    │
// │ Newton residual norm    │ 1e-6     │ Should be near machine epsilon      │
// │ GMRES Arnoldi vectors   │ 1e-5     │ Orthogonality sensitive             │
// └─────────────────────────┴──────────┴─────────────────────────────────────┘
//
// QUARTERLY CHECK: Run A/B tests after any change to numerical kernels.
//
// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass34 Extended: BOUNDARY/STAGGER CONSISTENCY TESTING
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: Vectorized path vs loop path may differ at boundaries due to
// different index handling, padding, or stagger offset calculations.
//
// SNAPSHOT COMPARISON TEST (small grid cases):
// ┌────────────────────────────────────────────────────────────────────────────┐
// │ TEST CASES (minimal grids for fast execution):                            │
// │   Case 1: 5x5x3 (smallest valid, tests corner handling)                   │
// │   Case 2: 8x8x5 (tests edge padding)                                      │
// │   Case 3: 10x6x4 (asymmetric, tests index math)                           │
// │                                                                            │
// │ TEST PROCEDURE:                                                            │
// │   for each case in [case1, case2, case3]:                                 │
// │       result_vec = runVectorizedPath(input);                              │
// │       result_loop = runLoopPath(input);                                   │
// │                                                                            │
// │       // Interior check (should be identical)                             │
// │       auto interior_diff = extractInterior(result_vec) -                  │
// │                           extractInterior(result_loop);                   │
// │       TORCH_CHECK(interior_diff.abs().max() < 1e-10, "Interior mismatch");│
// │                                                                            │
// │       // Boundary check (may have legitimate differences)                 │
// │       auto boundary_diff = extractBoundary(result_vec) -                  │
// │                           extractBoundary(result_loop);                   │
// │       if (boundary_diff.abs().max() > 1e-10) {                            │
// │           LOG_WARNING("Boundary diff at ", case, ": ", boundary_diff);    │
// │           // Document if intentional (e.g., different BC handling)        │
// │       }                                                                    │
// └────────────────────────────────────────────────────────────────────────────┘
//
// STAGGER OFFSET VERIFICATION:
//   // U-stagger (offset in i)
//   auto u_vec = computeU_vectorized(grid);
//   auto u_loop = computeU_loop(grid);
//   TORCH_CHECK(torch::allclose(u_vec, u_loop, 1e-10, 1e-10), "U-stagger mismatch");
//
//   // V-stagger (offset in j)
//   auto v_vec = computeV_vectorized(grid);
//   auto v_loop = computeV_loop(grid);
//   TORCH_CHECK(torch::allclose(v_vec, v_loop, 1e-10, 1e-10), "V-stagger mismatch");
//
//   // W-stagger (offset in k)
//   auto w_vec = computeW_vectorized(grid);
//   auto w_loop = computeW_loop(grid);
//   TORCH_CHECK(torch::allclose(w_vec, w_loop, 1e-10, 1e-10), "W-stagger mismatch");
//
// QUARTERLY CHECK: After vectorization changes, run all 3 cases.
//
// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass34 Extended: AD PATH PROTECTION CHECKLIST
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: .detach() and NoGradGuard in cache/diagnostics/sampling can
// unintentionally break gradient flow if used in wrong context.
//
// PERIODIC VERIFICATION CHECKLIST (quarterly):
// ┌────────────────────────────────────────────────────────────────────────────┐
// │ 1. GREP ALL GRAPH-BREAKING OPERATIONS:                                    │
// │    grep -rn "\.detach()\|NoGradGuard\|InferenceMode" \                    │
// │        --include="*.cpp" external/libtorch_wrf/sdirk3/                    │
// │                                                                            │
// │ 2. FOR EACH OCCURRENCE, VERIFY INTENT:                                    │
// │    ┌────────────────────────────────────────────────────────────────────┐ │
// │    │ Location Type      │ Expected │ If Unexpected                      │ │
// │    ├────────────────────────────────────────────────────────────────────┤ │
// │    │ Diagnostic/logging │ ✅ OK    │ -                                  │ │
// │    │ Cache storage      │ ✅ OK    │ -                                  │ │
// │    │ Sampling output    │ ✅ OK    │ -                                  │ │
// │    │ Boundary update    │ ⚠️ Check │ May need grad if adjoint BC       │ │
// │    │ RHS computation    │ ❌ Wrong │ Remove detach, need grad flow     │ │
// │    │ Jacobian path      │ ❌ Wrong │ Remove, breaks AD                 │ │
// │    └────────────────────────────────────────────────────────────────────┘ │
// │                                                                            │
// │ 3. DOCUMENT EACH OCCURRENCE:                                              │
// │    // AD_BREAK_OK: diagnostic output, no grad needed                      │
// │    auto value = tensor.detach().item<float>();  // AD_BREAK_OK           │
// │                                                                            │
// │ 4. FLAG UNDOCUMENTED OCCURRENCES IN CI:                                   │
// │    grep "\.detach()\|NoGradGuard" *.cpp | grep -v "AD_BREAK_OK"          │
// └────────────────────────────────────────────────────────────────────────────┘
//
// AD PATH UNIT TEST:
//   torch::Tensor x = torch::randn({10}, torch::requires_grad());
//   torch::Tensor y = functionUnderTest(x);  // Should preserve grad
//   y.sum().backward();
//   TORCH_CHECK(x.grad().defined(), "Gradient flow broken in functionUnderTest");
//
// ═══════════════════════════════════════════════════════════════════════════
// FIX Round142: C API EXPOSURE VERIFICATION
// All APIs below are in extern "C" and properly exposed for Fortran/C linkage.
// Headers to include for external packages:
//   - wrf_sdirk3_config.h: Config setters (wrf_sdirk3_set_config_*)
//   - wrf_sdirk3_interface.h: Solver APIs + sdirk3_clear_tls_solver_cache()
// ═══════════════════════════════════════════════════════════════════════════
extern "C" {
    void wrf_sdirk3_set_config_int(const char* name, int value);
    void wrf_sdirk3_set_config_float(const char* name, float value);
    void wrf_sdirk3_set_config_bool(const char* name, int value);
    void wrf_sdirk3_load_config_from_namelist(const char* filename);
    void wrf_sdirk3_print_config();

    // FIX Round127/Round128: 64-bit unsigned integer setter for config values that need
    // full uint64_t range (e.g., warn_throttle_count). Avoids truncation when
    // callers pass values > INT_MAX.
    //
    // Available uint64 config options (all stored as uint64_t internally - no narrowing):
    //   - warn_throttle_count: Calls before warning re-triggers (0=sticky/never re-warn)
    //   - grid_metric_memcmp_period: Period for full grid metric comparison
    //   - cuda_grid_metric_sample_period: CUDA sampling period for grid metrics
    //   - cuda_phbase_sample_period: CUDA sampling period for ph_base
    //   - lat_cpu_sample_check_period: LatCpuCache sample check period
    //
    // FIX Round128: TYPE SAFETY NOTE
    // All config items supported by this setter are stored as uint64_t internally.
    // No narrowing conversion or clamping occurs. If future config items with
    // narrower storage types are added to this setter, they MUST include:
    //   1. Range checking against the target type's limits
    //   2. Warning when clamping occurs (similar to set_config_int for warn_throttle_count)
    //   3. Documentation of the clamping policy in this comment block
    //
    // FIX Round129: NEGATIVE VALUE HANDLING (Fortran C_INT64_T compatibility)
    // Fortran INTEGER(C_INT64_T) is SIGNED (-2^63 to 2^63-1), but this setter
    // takes unsigned uint64_t. When Fortran passes a negative value:
    //   -1 (Fortran) → 0xFFFFFFFFFFFFFFFF (C++) = 18446744073709551615
    //   -N (Fortran) → 2^64 - N (C++)
    // POLICY: Values > INT64_MAX are detected as likely negative Fortran input.
    //         A warning is logged and value is CLAMPED to 0 for safety.
    //
    // Fortran usage: Pass uint64 via Fortran INTEGER(C_INT64_T)
    void wrf_sdirk3_set_config_uint64(const char* name, uint64_t value);

    // FIX 2025-01-25: Config freeze mechanism to prevent config changes after workers start
    // markWorkersStarted() should be called at OpenMP parallel region entry or solver init.
    // After this call, all config writes are REJECTED and logged.
    // v20.14r27m: Documentation updated to match actual behavior (reject, not warn-and-apply).
    //
    // Usage:
    //   1. Call wrf_sdirk3_mark_workers_started() after all initial config is set
    //   2. All subsequent config changes will be rejected with "[CONFIG REJECT]" log
    //
    // Note: This is a BLOCKING mechanism. Config changes after freeze are silently dropped.
    // Runtime tuning uses instance-level setters (e.g., set_theta_acoustic_factor).
    void wrf_sdirk3_mark_workers_started(void);
    int wrf_sdirk3_is_workers_started(void);  // Returns 1 if workers started, 0 otherwise
    void wrf_sdirk3_reset_workers_started(void);  // v20.14r27n: Reset freeze for reinit/restart
}

// DIAGNOSTIC 2026-02-01: Mask activation counters for JVP discontinuity diagnosis
// Accumulated during computeUnifiedRHS, logged and reset at function exit.
// Thread-local to avoid contention in future multi-threaded scenarios.
struct MaskActivationCounters {
    int64_t mu_floor_active = 0;       // cells where mu_full <= 0
    int64_t mu_floor_total = 0;        // total cells checked
    int64_t extreme_w_active = 0;      // cells where |w| > 1e10
    int64_t extreme_w_total = 0;       // total cells checked
    int64_t sign_vel_zero = 0;         // cells where vel == 0 (sign undefined)
    int64_t sign_vel_total = 0;        // total cells checked
    int64_t ri_stable_active = 0;      // cells where Ri > 0 (stable)
    int64_t ri_stable_total = 0;       // total cells checked
    int64_t rhs_call_id = 0;           // monotonically increasing call counter

    void reset() {
        mu_floor_active = mu_floor_total = 0;
        extreme_w_active = extreme_w_total = 0;
        sign_vel_zero = sign_vel_total = 0;
        ri_stable_active = ri_stable_total = 0;
    }
};

// Global thread-local instance (accessed from _impl.cpp, diffusion_ad.cpp, etc.)
extern thread_local MaskActivationCounters g_mask_counters;

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_CONFIG_H
