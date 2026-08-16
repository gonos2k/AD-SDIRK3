#ifndef UNIFIED_PRECONDITIONER_ENHANCED_H
#define UNIFIED_PRECONDITIONER_ENHANCED_H

#include <torch/torch.h>
#include <cstring>
#include <memory>
#include <set>
#include <tuple>
#include <cstdint>
#include <atomic>  // OPT Pass33+: For diagnostic sampling counter
#include <limits>  // PR 9D: sentinel NaN for the W-damping policy signature
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_wdamp_preconditioner_policy.h"  // PR 9D: WdampPreconditionerSignature

namespace wrf {
namespace sdirk3 {

// THE phi diagonal of the preconditioner, in ONE place and callable from tests.
//
// It had TWO independent expressions -- the stored array and a local recompute in the W Schur
// denominator -- so flipping one produced an operator that was neither the shipped nor the
// intended one. `unity` is passed EXPLICITLY (not read from the environment in here) so both
// branches are executable in a contract without depending on process environment or on when a
// function-local static was first initialized.
//
// FORM: 1 + h*c_s^2/dz^2 is dimensionally invalid ([h c_s^2/dz^2] = 1/s added to a dimensionless
// 1). The acoustic coupling is not a phi SELF-term -- dphi/dt = -c_s^2 dw/dz has no direct phi
// dependence -- so it belongs in the w<->phi round trip (acoustic_cfl_sq), where it already is.
inline float phi_diagonal_value(float dt_gamma, float c_s, float dz_inv2, bool unity) {
    return unity ? 1.0f : 1.0f + dt_gamma * c_s * c_s * dz_inv2;
}


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
// mu<->phi direct coupling, in one place so the three solve paths cannot diverge.
//
// MEASURED on the live operator (block probe, stage 2, dt=600, WRFParity): perturbing ph gives
// no mu response and perturbing mu gives a clear ph response --
//     A_mu_ph = 0 (exactly)      A_ph_mu = 0.0696 .. 0.0717  (three random directions)
// so the operator is ASYMMETRIC in exactly the direction the corrected mass equation implies.
//
// The magnitude above REPLACES an earlier 0.689 recorded here. That value does not reproduce --
// it is ~10x larger -- and it was stored without the mode/stage/build it came from, so the
// discrepancy is unexplained rather than refuted. The re-measurement was taken after the probe's
// off-diagonal label was corrected from "Arow" to Acol[in=q]: the old label named the entries by
// the INPUT block, which reads A_mu_ph as A_ph_mu. Direction was unaffected; magnitude is open.
// Both fields here still hold the hydrostatic coefficient, which is a numerics change pending
// the re-derivation. Note a_mu_phi = 0 does NOT zero S_mu_phi: the Schur complement over (u,v)
// still carries phi -> u,v -> mu unless HEVI removes those blocks too.
struct MuPhiDirectCoupling {
    float a_mu_phi;   // mu row, phi column
    float a_phi_mu;   // phi row, mu column  (vertical hydrostatic)
};

// The one line that changes when the asymmetry is derived.
// THE decision, now derived from the mass equation instead of asserted.
//
// mu_row_has_no_phi is effective_mu_horizontal_div_only(): under the corrected mass coordinate
// the mu tendency is the horizontal divergence of (mu u, mu v) and carries NO phi dependence, so
// the DIRECT mu <- phi entry is zero. Under legacy Omega = mu w the chain mu -> w -> phi was
// real, and the coupling with it.
//
// Measured on the live operator (block probe, stage 2, dt=600, WRFParity): A_mu_ph = 0 exactly
// while A_ph_mu is not -- the operator is asymmetric in exactly this direction, and one scalar
// serving both was the preconditioner asserting a symmetry its operator does not have.
//
// AND IT IS MEASURED HARMFUL IN THE GATE'S METRIC, so production passes false and keeps the
// symmetric value.
//
// Isolated on em_b_wave, stage 2, dt=600, WRFParity -- same build, same run, only this coupling
// changed -- read off the live FGMRES triplet AFTER the metric was corrected for the scaled
// Krylov coordinates (the first measurement of this mixed the two and is superseded):
//
//   eps_physical_wrms = ||E^-1 S (w-v)|| / ||E^-1 S v||     <- what the convergence gate weights by
//     symmetric (shipped) : 30.7, 0.258, 0.251, 0.265, 1.40, 0.515, 0.358
//     a_mu_phi = 0        : 268,  1.27,  0.587, 0.318, 0.967, 1.098, 1.601
//     -> worse on 6 of 7 directions (1.2x to 9x); iteration 4 improves
//
//   eps_krylov = ||w-v|| / ||v||                            <- what GMRES's own theory speaks about
//     symmetric (shipped) : 48.1, 139, 196, 300, 21.8, 99.9, 182
//     a_mu_phi = 0        : 48.8, 118, 235, 275, 88.1, 23.6, 44.1
//     -> MIXED: better on 4, worse on 2, unchanged on 1
//
// THE TWO METRICS DISAGREE, which is the reason they are reported separately. The decision rests
// on the physical one, because that is what decides whether the step converges.
//
// Why it is not a contradiction that a "correct" removal hurts: M approximates A^-1, not A.
// Nothing requires M to share A's sparsity, and the inverse of an asymmetric operator generally
// DOES carry the entry A lacks -- deleting it because A lacks it confuses the operator with its
// inverse. It may also be compensating another wrong coefficient, which this measurement cannot
// distinguish; it says the term is load-bearing HERE, not that the symmetry is correct.
//
// Third time in this campaign that a mathematically-correct removal degraded conditioning, after
// the W-phi 2x2 refinement and the spurious W-damping term. The corrected form stays available
// and tested, and is deliberately not enabled.
inline float mu_phi_from_phi_mu(float a_phi_mu, bool mu_row_has_no_phi) {
    return mu_row_has_no_phi ? 0.0f : a_phi_mu;
}

inline MuPhiDirectCoupling mu_phi_direct_coupling(float dt_gamma, float c2,
                                                  bool mu_row_has_no_phi) {
    const float a_phi_mu = dt_gamma * c2;   // vertical hydrostatic; NOT affected by the above
    return MuPhiDirectCoupling{mu_phi_from_phi_mu(a_phi_mu, mu_row_has_no_phi), a_phi_mu};
}

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

    // 9F.D90 (review section 9): the grad policy is a STACK-LOCAL ARGUMENT, not shared
    // mutable state.
    //
    // D80 shipped a public setter over a member bool that apply() consulted. A single thread
    // was exception-safe (the transpose restored it via RAII), but a forward apply() running
    // concurrently with a transpose would see the other's mode -- a data race on the
    // *meaning* of the call, which no amount of RAII fixes. Passing the policy down the call
    // chain makes concurrent use well-defined and deletes the setter, the member, and the
    // save/restore dance together.
    enum class GradPolicy {
        Disabled,   // production forward: NoGradGuard everywhere, as it has always been
        Track       // record a graph, so the VJP below has something to differentiate
    };

    // 9F.D90 (review section 10): NAMED FOR WHAT IT COMPUTES.
    //
    // apply() applies P ~ A^{-1}, so its transpose is P^T = (M^{-1})^T = M^{-T}, NOT "M^T".
    // The old apply_transpose_ad() and its comments said M^T throughout, and that notation
    // is on the list of things that made "is M^{-T} needed?" get answered wrongly more than
    // once. The name now states the operator.
    torch::Tensor apply_inverse_transpose(const torch::Tensor& cotangent);

    // VERIFIED at 1.54e-07 globally (D83) and block-locally across two seeds (D89).
    //
    // FAIL-CLOSED. If no graph was recorded, or the VJP comes back undefined, this THROWS
    // rather than returning its input. Returning the input is precisely the D80 failure --
    // a severed VJP is the identity, which is a valid linear operator, so a caller cannot
    // tell it from a real transpose and a preconditioned solve would silently become an
    // unpreconditioned one.
    
    /**
     * Update preconditioner if parameters change
     * @param state Current state (for state-dependent preconditioning)
     * @param dt New time step
     * @param gamma New SDIRK3 coefficient
     */
    // What update() ACTUALLY does, under its honest name. 9F.D91 established -- by reading the
    // whole body -- that update(state, dt, gamma) never reads `state`: it rebuilds the
    // coefficients on dt/gamma plus the base-state / cache / config generation counters, and
    // takes the stage mass state from mu_full_stage_, which is bound separately through
    // bind_stage_state_or_throw(). The three-argument signature repeatedly misled callers into
    // "I passed the checkpoint state, so the preconditioner matches it" -- D87 believed exactly
    // that, and D91 had to correct it.
    //
    // The name is the fix the D91 comment called for. update() remains as the base-interface
    // override and DELEGATES here, so there is one body and no behaviour change; new call sites
    // should say what they mean:
    //
    //     P.update_time_coefficients(dt, gamma);        // rebuild for THIS h = dt*gamma
    //     P.bind_stage_state_or_throw(mu_pert, stage);  // bind THIS stage's mass state
    void update_time_coefficients(float dt, float gamma);

    // Base-interface override; `state` is accepted and NOT read (see above).
    void update(const torch::Tensor& state, float dt, float gamma) override {
        (void)state;
        update_time_coefficients(dt, gamma);
    }
    
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

    // 9F.D98 (review section 5): the CHECKED binding, for callers that cannot proceed
    // without it.
    //
    // set_stage_state() above returns silently on five internal failures (undefined or
    // non-2D mu_pert, missing grid_info_, missing or non-2D mu_base, shape mismatch),
    // logging and continuing. That is tolerable for the production forward, which re-binds
    // every Newton stage. It is NOT tolerable for the adjoint replay: D94 measured that P
    // genuinely depends on the mass state, so a skipped bind means the transpose solve runs
    // against a preconditioner carrying the WRONG state, and a wrong gradient is worse than
    // no gradient.
    //
    // D95 made the mu EXTRACTION fail-closed and stopped there, so the binding was still
    // fail-open one call deeper. This closes it: verified by a RECEIPT read back from the
    // preconditioner, not by trusting that the setter did something.
    // 9F.D102 (review section 4): the receipt proves the FIELD, not a summary of it.
    //
    // D98's receipt checked mu_full_stage_ was defined, had the right numel, that
    // current_stage_ matched, and that the MEAN was finite. A stale field from another
    // checkpoint passes all four: mu_prime is a perturbation whose positive and negative
    // regions largely cancel, so two different checkpoints can share a mean to several
    // digits. The setter itself avoids the mean for exactly this reason and uses ||mu'||_2.
    //
    // So the receipt now compares POINTWISE against mu_base + mu_pert, and enforces the
    // physical constraint the preconditioner depends on: mu_full > 0 everywhere. The 4x4
    // Schur path forms 1/mu_full couplings, so one non-positive cell flips a coefficient's
    // sign and injects a local singularity -- invisible to any mean.
    struct StageBindingReceipt {
        int stage = -1;
        double mu_full_mean = 0.0;
        double mu_full_min = 0.0;      // the physical constraint lives here, not in the mean
        double mu_full_max = 0.0;
        double max_binding_error = 0.0;  // max |mu_full_stage_ - (mu_base + mu_pert)|
        int64_t mu_numel = 0;
        // DISTINCT counters: a small state change updates mu_full_stage_ without triggering a
        // coefficient rebuild, so coefficient_generation is not evidence that THIS state was
        // bound. stage_state_generation increments on every bind.
        uint64_t stage_state_generation = 0;
        uint64_t coefficient_generation = 0;
    };
    StageBindingReceipt bind_stage_state_or_throw(const torch::Tensor& mu_pert, int stage);

    // 9F.D109 (review section 9): snapshot/restore of the stage state the adjoint replay
    // mutates, so a replay cannot leave the PRODUCTION preconditioner bound to whichever
    // checkpoint it happened to visit last.
    //
    // Not a full replay-dedicated instance (the review's preferred structure) -- this is the
    // isolation GUARANTEE without the factory. It is what makes a digest-equality contract
    // possible on both the normal and the exception path, which is the property that was
    // missing: "the next forward step re-updates" is not a rollback.
    struct StageStateSnapshot {
        torch::Tensor mu_full_stage;
        torch::Tensor mu_pert_last_bound;
        int current_stage = -1;
        float mu_scale_correction = 1.0f;
    };
    StageStateSnapshot snapshot_stage_state() const {
        StageStateSnapshot s;
        if (mu_full_stage_.defined()) s.mu_full_stage = mu_full_stage_.detach().clone();
        if (mu_pert_last_bound_.defined())
            s.mu_pert_last_bound = mu_pert_last_bound_.detach().clone();
        s.current_stage = current_stage_;
        s.mu_scale_correction = mu_scale_correction_;
        return s;
    }
    void restore_stage_state(const StageStateSnapshot& s) {
        mu_full_stage_ = s.mu_full_stage;
        mu_pert_last_bound_ = s.mu_pert_last_bound;
        current_stage_ = s.current_stage;
        mu_scale_correction_ = s.mu_scale_correction;
        // A RESTORE IS A BINDING EVENT, so it takes a fresh generation.
        //
        // Without this the counter is not a faithful identity across rollback: a replay binds
        // checkpoints (counter G -> G+k, state S'), then restore puts the state back to S and
        // leaves the counter at G+k. One value, G+k, then denotes S' during the replay and S
        // after it -- two different linearizations comparing EQUAL, which is the one thing an
        // identity must never do.
        //
        // Restoring the counter to G instead would be worse: G+1..G+k were already issued, so
        // they would be handed out again for unrelated binds. Minting a new value keeps it
        // monotonic and keeps every value bound to exactly one state.
        //
        // Safe for numerics by inspection: this counter gates no cache. It is written at bind
        // (wrf_sdirk3_unified_preconditioner.cpp:5329, "evidence that THIS bind ran"), read into
        // StageBindingReceipt, and read by the accessor above. Nothing branches on its value.
        ++stage_state_generation_;

        // THE COEFFICIENTS ARE NOW STALE, and apply() must refuse until they are rebuilt.
        //
        // This snapshot carries stage fields only. The acoustic/gravity coefficients are DERIVED
        // from mu_full_stage_ (see :2436 and :3966, inv_mu0) and are rebuilt by update() ->
        // initialize_acoustic_gravity_solver(), which the adjoint replay calls with ITS OWN
        // linearization point. Rolling the stage fields back therefore leaves coefficients
        // computed from a state that is no longer bound.
        //
        // Restoring them is not something to guess at: update() takes the step state while this
        // guard saves U_ref_stage_, and I have not shown those are the same linearization point.
        // Choosing one would be inventing a recovery. What IS provable is that coefficients
        // derived from a rolled-back state must not be applied -- so this fails closed, and a
        // genuine rebuild clears it.
        coefficients_stale_ = true;
    }

    // True between a rollback and the next coefficient rebuild. Public so the condition is
    // observable to a contract rather than only to the code that throws on it.
    bool coefficients_stale() const { return coefficients_stale_; }
    // Digest of exactly the fields above, for the isolation contract. Deliberately includes
    // mu_full_stage's VALUES, not just its shape -- the whole failure mode is a field from
    // the wrong checkpoint, which has identical shape.
    // EXACT fingerprint of every field the snapshot restores.
    //
    // The previous digest was 1e6*stage + 1e3*scale + |mu_full|_1 and claimed, in its own comment,
    // to cover exactly those fields. It did not:
    //   * mu_pert_last_bound was ABSENT, so deleting its restore would have kept passing
    //   * mu_full collapsed to an L1 sum, so any rearrangement with the same sum was identical
    //   * the terms were ADDED, so a stage change could be offset by a field change
    //
    // A rollback either restores the state or it does not, so this is exact equality over the
    // raw values, not a tolerance on a summary scalar.
    uint64_t stage_state_fingerprint() const {
        torch::NoGradGuard g;
        uint64_t h = 1469598103934665603ULL;                  // FNV-1a offset basis
        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
        auto mix_tensor = [&](const torch::Tensor& t) {
            if (!t.defined()) { mix(0x9E3779B97F4A7C15ULL); return; }
            const auto c = t.detach().to(torch::kCPU).to(torch::kFloat64).contiguous();
            mix(static_cast<uint64_t>(c.numel()));
            const double* p = c.data_ptr<double>();
            for (int64_t i = 0; i < c.numel(); ++i) {
                uint64_t bits;
                std::memcpy(&bits, &p[i], sizeof(bits));      // exact bits, NaN and -0 included
                mix(bits);
            }
        };
        mix(static_cast<uint64_t>(static_cast<int64_t>(current_stage_)));
        uint32_t sc;
        std::memcpy(&sc, &mu_scale_correction_, sizeof(sc));
        mix(sc);
        mix_tensor(mu_full_stage_);
        mix_tensor(mu_pert_last_bound_);                      // the field the old digest omitted
        return h;
    }

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

    // The two counters that identify WHICH linearization this preconditioner currently is.
    // Read-only, and DISTINCT for the reason StageBindingReceipt already documents: a small state
    // change updates mu_full_stage_ without triggering a coefficient rebuild, so
    // coefficient_generation is not evidence that this state was bound.
    //
    // They serve two purposes for a diagnostic that judges A*P^-1: identity (an A and an M^-1 must
    // come from the same linearization) and purity (a probe must not advance them). The second is
    // what makes a faithful state digest possible -- these move exactly when this object rebinds
    // or rebuilds, so a digest over them is a real witness rather than a constant that cannot fail.
    // A SNAPSHOT of the phi diagonal this object actually built -- a detached clone, so the
    // caller cannot reach back into the preconditioner.
    //
    // NOT `const torch::Tensor&`: torch::Tensor const-ness is SHALLOW. A const reference still
    // hands out a handle whose underlying storage is writable -- `auto t = P.diag(); t.fill_(0);`
    // silently zeroes the live operator through a method that calls itself read-only, and the
    // const qualifier on the getter does nothing to stop it. Returning by const& would make the
    // accessor's own name a false claim, so it returns an owned copy instead.
    //
    // It exists so a contract can discriminate which branch the env-latched experiment took:
    // asserting phi_diagonal_value(..., true) == 1 with a literal `true` proves a property of the
    // pure function and NOTHING about the operator that was constructed.
    // The horizontal mu<->u,v couplings this object actually built, as detached clones.
    // Same reasoning as phi_diagonal_snapshot(): torch::Tensor const-ness is shallow, so a
    // const& would hand out writable live state.
    //
    // These exist so a contract can read the SIGNS production computes rather than re-typing the
    // formula. A test that hard-codes `-h*(c_s^2/mu0)*H_x` is asserting a property of its own
    // literals: it keeps passing if the production build changes underneath it, which is exactly
    // when the finding would need to be revisited.
    struct HorizontalCouplingSnapshot {
        torch::Tensor c_u_mu, c_v_mu, c_mu_u, c_mu_v;
    };
    HorizontalCouplingSnapshot horizontal_coupling_snapshot() const {
        auto cl = [](const torch::Tensor& t) {
            return t.defined() ? t.detach().clone() : torch::Tensor{};
        };
        return {cl(C_u_mu_), cl(C_v_mu_), cl(C_mu_u_), cl(C_mu_v_)};
    }

    torch::Tensor phi_diagonal_snapshot() const {
        return vertical_diag_phi_.defined() ? vertical_diag_phi_.detach().clone()
                                            : torch::Tensor{};
    }

    uint64_t stage_state_generation() const { return stage_state_generation_; }
    uint64_t coefficient_generation() const { return coefficient_generation_; }

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
    // This ONE member is used as the authority for BOTH directions, on the rationale that
    // A_μΦ = A_Φμ keeps the Schur complement stable. The live operator contradicts that: see the
    // measurement at the top of this file, where A_mu_ph is exactly 0 while A_ph_mu is not. The
    // symmetry is therefore an assumption of the preconditioner, not a property of the operator
    // it approximates -- and modelling a coupling the operator does not have is the same class of
    // defect as the Omega fix removed from the RHS.
    //
    // Splitting it is a NUMERICS change and is deliberately not made here; MuPhiDirectCoupling
    // already gives the two directions independent fields so the fix has somewhere to land.

    // Brunt-Väisälä frequency squared at each level
    torch::Tensor N_squared_;

    // Sound speed squared at each level (for acoustic coupling)
    torch::Tensor c_sound_squared_;
    
    // Horizontal smoothing parameters
    float horizontal_smooth_factor_;
    int n_smooth_iters_ = 3;
    
    // Condition number estimate
    // 9F.D102: increments on every set_stage_state, so a receipt can prove THIS bind
    // happened. coefficient_generation cannot: a small state change updates
    // mu_full_stage_ without triggering a coefficient rebuild.
    // 9F.D108 (review section 5): the previously bound mu_pert, so the recompute trigger can
    // ask "how much did this CHANGE?" rather than "how big is it?". Two checkpoints can each
    // be under the absolute threshold while differing by twice it.
    torch::Tensor mu_pert_last_bound_;
    uint64_t stage_state_generation_ = 0;
    mutable float condition_estimate_ = 1.0f;

    // v20.3: Newton residual ratio for adaptive α
    // Set by Newton solver via set_newton_residual_ratio() before each GMRES.
    // 1.0 = first Newton iter, → 0 as Newton converges.
    float newton_residual_ratio_ = 1.0f;

    // Cache invalidation generation counter
    // Incremented whenever coefficients change (update, initialize_acoustic_gravity_solver)
    uint64_t coefficient_generation_ = 0;

    // Set by restore_stage_state(), cleared by the rebuild in
    // initialize_acoustic_gravity_solver(). While true, apply() refuses: the coefficients in
    // memory were derived from a stage state that has since been rolled back.
    bool coefficients_stale_ = false;

    // Diagnostic latch: one slot, so it reports when the measured scope CHANGES.
    // O(1) and race-free by construction -- a set here would grow with coefficient_generation_
    // and would need a lock, since apply() runs under tile parallelism.
    static uint64_t diag_scope_key(int mode, int nz, uint64_t generation) {
        return (static_cast<uint64_t>(mode & 0xFF) << 56) |
               (static_cast<uint64_t>(nz & 0xFFFF) << 40) |
               (generation & 0xFFFFFFFFFFULL);
    }
    std::atomic<uint64_t> diag_mu_schur_key_{~0ULL};

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
    torch::Tensor apply_impl(const torch::Tensor& residual, GradPolicy policy);
    torch::Tensor apply_enhanced_vertical_solve(const torch::Tensor& r,
                                                GradPolicy policy);
    
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
        int phase2_nz_w = 0,                   // bounds for Phase 2 arrays
        // 9F.D90: threaded from apply_impl. This runs the w/theta Thomas sweeps and is the
        // second of the two places that must record a graph for the transpose. Defaulted so
        // the existing call sites keep production behaviour if one is ever missed.
        GradPolicy policy = GradPolicy::Disabled
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