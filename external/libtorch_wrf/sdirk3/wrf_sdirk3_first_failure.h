#ifndef WRF_SDIRK3_FIRST_FAILURE_H
#define WRF_SDIRK3_FIRST_FAILURE_H

// WHICH gate refused FIRST, and on what evidence.
//
// THE PROBLEM THIS EXISTS FOR. "The step did not complete" is one bucket, and the campaign
// has been sweeping dt against it -- 600, 300, 120, 60, 20, all the same answer. That sweep
// cannot converge on a cause because the bucket holds at least seven distinguishable
// outcomes, and they point at different layers of the model:
//
//   the stage equation was never defined    -> the state, the EOS, the metric, the boundary
//   Newton's iteration diverged             -> the linearization, or dt against the physics
//   Newton stalled with a working operator   -> the residual has a floor; look at the split
//   the LINEAR solve made no progress        -> the preconditioner, or operator definiteness
//   every step was rejected                  -> trust region / line search policy
//   it converged and the gate refused        -> the admissibility threshold
//   it was admissible and never published    -> the publish gate
//
// A dt sweep is the right experiment for exactly one of these. Naming the first refusal is
// what tells you which.
//
// THE RULE IS A FREE FUNCTION over plain data, and its CONSUMER is here too
// (first_failure_of + the evidence accessors), because the recurring defect in this tree has
// been a correct rule whose consumer read something else. A classifier whose emit site
// re-derives the category from raw signals would be that defect again.

#include <algorithm>
#include <vector>
#include <limits>

namespace wrf {
namespace sdirk3 {

enum class StageFailure {
    None = 0,
    // R13.15 (external review P1-4): the signals belong to a DIFFERENT stage than the one being
    // classified. `signals_from_stage` was emitted beside the category and the category was
    // computed anyway, so a record could name a layer from another stage's evidence. A
    // provenance mismatch is not weak evidence, it is the wrong evidence.
    StageSignalMismatch,
    // R13.17 (external review P0-4): the stamp is ABSENT. The mismatch gate required both stamps
    // to be >= 0, so the documented "-1 = not stamped" sentinel skipped it entirely and the rest
    // of the classifier ran on signals whose owner is unknown -- fail-closed on disagreement,
    // fail-OPEN on absence, which is the weaker half of the same contract.
    StageSignalMissing,
    // Explicit (ARK) stages do not run Newton, so every implicit category is inapplicable to
    // them and they used to collapse into InsufficientEvidence -- which reads as "we could not
    // tell" when the truth is "a different set of things can fail here".
    ExplicitRhsNotFinite,
    ExplicitAdmissibilityRejected,
    ExplicitPublishRejected,
    // R13.16 (round 6, R6-2): the linear solve made little progress AND stopped because it met
    // its own adaptive tolerance. That is the Eisenstat-Walker forcing term, capped at 0.9 and
    // saturating there exactly when the Newton residual stops falling -- so "reached tolerance"
    // can mean "removed 10%". Naming this KrylovStagnated sends the work to the operator and the
    // preconditioner; the layer actually responsible had no category at all.
    KrylovForcingTermLimited,
    // R13.17 (external review P0-2): the D-objective tolerance was met and the S-coordinate
    // residual was not. The solve ENDED ITS SEARCH legitimately -- it minimised what it was asked
    // to minimise -- and is nonetheless useless to the Newton merit. That is neither a stall
    // (the solve worked) nor a forcing-term problem (tightening eta does not align two different
    // objectives): it is the two metrics disagreeing, which is a formulation question about D.
    KrylovObjectiveMismatch,
    // R13.18 (deep review P0-1 remainder): both RECORDED metrics were satisfied and the STAGE
    // GATE still refuses. The gate accepts on ||E^-1 R||, a third metric the receipt did not
    // carry, so rho_D < eta and rho_S < eta with rho_E >= eta had no category at all -- the solve
    // met everything the classifier could see and the step was rejected anyway. That is not the
    // operator, not the forcing term and not the budget: it is the gate's metric disagreeing with
    // the ones the solve was steered by.
    // R13.19 (precision review P0-2): RENAMED. This was called StageGateMetricMismatch and it
    // does not measure the stage gate. `exit_rho_E` is the StageEntry-weighted LINEAR Krylov
    // residual; the real gate re-evaluates the NONLINEAR stage residual at U_new against
    // `stage_gate_rel_threshold` under one of three gate_metric_modes -- a different residual, a
    // different denominator, a different weighting point and a different threshold. The
    // classifier also never read `gate_metric_ok`, and the fixture asserted the mismatch on a
    // record whose gate was marked OK, so CI was pinning a contract that reported the gate
    // refusing while the record said it passed.
    KrylovEntryMetricMismatch,
    // Neither tolerance met and the Arnoldi budget ran out. Distinct from stagnation: the solve
    // was still descending when it was cut off, so the work is the budget, not the operator.
    // R13.19 (precision review P1-2): renamed to the FACT it measures. "Limited" asserted that
    // the residual was still descending when the budget cut it off, and nothing measures that --
    // a solve flat from its first restart classifies identically. The tail slope would settle it;
    // until then the category says only that the budget ran out, and its layer names the
    // remaining ambiguity instead of resolving it.
    KrylovBudgetExhausted,
    // R13.17 self-review: the linear solve THREW. That is a real event and it is not the outer
    // iteration's failure -- but it is also not divergence, which is a specific measured
    // behaviour (the residual grew) that an exception does not establish. Borrowing
    // KrylovDiverged for it would name a mechanism nothing measured.
    KrylovSolveThrew,
    // R13.18 (round 7, P0-B): the Newton loop broke because the accepted update was numerically
    // ZERO and the solve had been flagged a total failure. Naming that `KrylovStagnated` -- as an
    // earlier version of this file did -- claims the linear solve produced nothing, and it did
    // not: under the default configuration the flag is `raw > 1 || rel >= 0.999` on ||r||/||b||,
    // the very coordinate R13.12-R13.16 moved this classifier OFF, and the run it was used to
    // reclassify had a worst solve that removed 13.8% of its own residual. What is observed is
    // the zero update; the cause is ambiguous between the ||b|| rule firing on a warm start it
    // was retracted for and a genuine failure to produce a step. The layer says so.
    ZeroUpdateAfterTotalFailure,
    // The signals do not support ANY verdict. Reported instead of guessing -- a classifier
    // that always names a layer will name a wrong one.
    InsufficientEvidence,
    // The stage equation G_s(K) = K - F(Y_base + dt*a_ss*K) is not defined at its own entry.
    // Nothing downstream is meaningful; a dt sweep cannot reach this.
    EntryStateNotFinite,
    // The entry state is finite and G evaluated at the predictor is not. The operator, not
    // the state, is where this lives.
    InitialResidualNotFinite,
    // Newton's residual GREW. The iteration is unstable, which is a statement about the
    // linearization or about dt against the physics -- and the one case a dt sweep answers.
    NewtonDiverged,
    // The linear solve made no progress: GMRES returned with its relative error essentially
    // unchanged. Newton cannot converge on top of a linear solve that does not solve, and
    // the cause is the operator or the preconditioner -- never the outer iteration.
    // The linear solve made no progress. NOTE the disjunction in the layer: this does NOT
    // separate the mathematical operator from solver POLICY. GMRESResult::termination_reason
    // already distinguishes arnoldi_stagnation, mid_budget_hopeless, restart_stagnation,
    // nan_retry_exhausted, max_budget and true-residual divergence; until that reaches here,
    // an early-stop policy and an indefinite operator arrive as the same category.
    KrylovStagnated,
    // The linear residual GREW. Divergence is not stagnation and does not point at the same
    // place; the old rule folded raw_rel_error > 1 into KrylovStagnated.
    KrylovDiverged,
    // The linear solve worked and every step it proposed was rejected. That is a trust-region
    // or line-search policy statement, not a numerical one.
    AllStepsRejected,
    // Newton was still converging and ran out of ITERATIONS. Not a numerical finding at all:
    // the residual was falling, every step was accepted and the linear solve was working. The
    // answer is the budget.
    //
    // ADDED AFTER THE FIRST REAL RUN REFUTED THE TAXONOMY. em_b_wave at dt=600 classified as
    // NewtonStagnated with R 1 -> 0.4855 over newton_iters=3 against max_newton_iter=3 --
    // a monotonically falling residual reported as a stall, sending the work to "residual
    // floor or split" when the measurement says "the budget is 3 and the tolerance is 0.2".
    NewtonBudgetExhausted,
    // Newton neither diverged nor stalled for either reason above, and it did NOT simply run
    // out of budget -- the residual stopped moving. The residual has a floor.
    NewtonStagnated,
    // Newton converged by its own test and the stage gate refused the result anyway. The
    // disagreement is between two different measures of "converged", and that is where to
    // look -- not at the solve.
    AdmissibilityRejected,
    // Everything converged and was admissible, and the state was still not published.
    PublishRejected
};

inline const char* stage_failure_name(StageFailure f) {
    switch (f) {
        case StageFailure::None:                     return "none";
        case StageFailure::StageSignalMismatch:  return "stage_signal_mismatch";
        case StageFailure::StageSignalMissing:   return "stage_signal_missing";
        case StageFailure::ExplicitRhsNotFinite: return "explicit_rhs_not_finite";
        case StageFailure::ExplicitAdmissibilityRejected:
            return "explicit_admissibility_rejected";
        case StageFailure::ExplicitPublishRejected: return "explicit_publish_rejected";
        case StageFailure::KrylovForcingTermLimited:
            return "krylov_forcing_term_limited";
        case StageFailure::KrylovObjectiveMismatch:
            return "krylov_objective_mismatch";
        case StageFailure::KrylovEntryMetricMismatch:
            return "krylov_entry_metric_mismatch";
        case StageFailure::KrylovBudgetExhausted:
            return "krylov_budget_exhausted";
        case StageFailure::KrylovSolveThrew:
            return "krylov_solve_threw";
        case StageFailure::ZeroUpdateAfterTotalFailure:
            return "zero_update_after_total_failure";
        case StageFailure::InsufficientEvidence:     return "insufficient_evidence";
        case StageFailure::KrylovDiverged:           return "krylov_diverged";
        case StageFailure::EntryStateNotFinite:      return "entry_state_not_finite";
        case StageFailure::InitialResidualNotFinite: return "initial_residual_not_finite";
        case StageFailure::NewtonDiverged:           return "newton_diverged";
        case StageFailure::KrylovStagnated:          return "krylov_stagnated";
        case StageFailure::AllStepsRejected:         return "all_steps_rejected";
        case StageFailure::NewtonBudgetExhausted:    return "newton_budget_exhausted";
        case StageFailure::NewtonStagnated:          return "newton_stagnated";
        case StageFailure::AdmissibilityRejected:    return "admissibility_rejected";
        default:                                     return "publish_rejected";
    }
}

// Which layer the category points at. The whole purpose of classifying is to stop working on
// the wrong one, so the mapping is data rather than something a reader has to reconstruct.
inline const char* stage_failure_layer(StageFailure f) {
    switch (f) {
        case StageFailure::None:                     return "none";
        case StageFailure::StageSignalMismatch:  return "wrong_stage_signals_no_verdict";
        case StageFailure::StageSignalMissing:
            return "unstamped_signals_no_verdict";
        case StageFailure::ExplicitRhsNotFinite: return "explicit_rhs_operator_or_state";
        case StageFailure::ExplicitAdmissibilityRejected:
            return "explicit_stage_gate_threshold";
        case StageFailure::ExplicitPublishRejected: return "explicit_publish_gate";
        case StageFailure::KrylovForcingTermLimited:
            // R13.18 (deep review P0-2): SOURCE-NEUTRAL. This used to name Eisenstat-Walker while
            // the tolerance could have come from a stage override or a ramp -- the source was
            // produced, emitted, and read by nothing. The specific source is on the record and
            // `krylov_forcing_layer_for` turns it into a layer.
            return "krylov_tolerance_policy_or_inner_budget";
        case StageFailure::KrylovObjectiveMismatch:
            // R13.19 (precision review P0-4): SOURCE-NEUTRAL. This said "D_vs_newton_merit" while
            // under the WRMS experiment the metric actually satisfied is E^-1 S at the Newton
            // linearization point, not block D -- the enum was recorded and the layer ignored it.
            // `krylov_stopping_layer_for` turns the recorded metric into the specific layer.
            return "krylov_stop_metric_vs_newton_merit";
        case StageFailure::KrylovEntryMetricMismatch:
            return "krylov_entry_E_metric_vs_solver_metrics";
        case StageFailure::KrylovBudgetExhausted:
            return "inner_budget_or_unresolved_stagnation";
        case StageFailure::KrylovSolveThrew:
            return "linear_solve_exception";
        case StageFailure::ZeroUpdateAfterTotalFailure:
            return "zero_update_bnorm_rule_or_step_recovery";
        case StageFailure::InsufficientEvidence:     return "unknown";
        case StageFailure::KrylovDiverged:           return "operator_or_timestep_or_jvp";
        case StageFailure::EntryStateNotFinite:      return "nonfinite_entry_state";
        case StageFailure::InitialResidualNotFinite: return "nonfinite_initial_residual";
        case StageFailure::NewtonDiverged:           return "linearization_or_timestep";
        // The honest width. Naming two candidates invited reading the other five as excluded.
        case StageFailure::KrylovStagnated:
            return "operator_or_timestep_or_jvp_or_scaling_or_preconditioner_or_policy";
        case StageFailure::AllStepsRejected:         return "trust_region_policy";
        case StageFailure::NewtonBudgetExhausted:    return "newton_iteration_budget";
        case StageFailure::NewtonStagnated:          return "residual_floor_or_split";
        case StageFailure::AdmissibilityRejected:    return "gate_threshold";
        default:                                     return "publish_gate";
    }
}

// Where a Krylov tolerance actually came from. A category that names Eisenstat-Walker must have
// read this, not assumed it.
// R13.17 (external review P0-3): why the Newton loop ACTUALLY stopped, recorded at the site that
// stopped it. The classifier reconstructed this from accepted/rejected counts, residual first/last
// and budget usage -- a fixed precedence over aggregates, which cannot tell "the residual stopped
// moving" from "the budget ran out at a residual that happened to be flat".
enum class NewtonTerminationReason {
    // R13.18 (deep review P1-5): `TrustRejected` and `NonfiniteResidual` were removed -- both had
    // ZERO producers, and neither corresponds to an actual Newton-loop exit: the trust region's
    // "all attempts rejected" path keeps K and CONTINUES, and no non-finite-residual site breaks
    // the loop. An enum value nothing writes is the same defect as a field nothing reads, and
    // keeping them made the inventory look more complete than the instrumentation was. If such an
    // exit is added later, the value comes back WITH its producer.
    NotRecorded = 0, Converged, BudgetExhausted, ResidualStall, ZeroStepStall,
    ZeroUpdateAfterTotalFailure, Exception
};

inline const char* newton_termination_name(NewtonTerminationReason r) {
    switch (r) {
        case NewtonTerminationReason::NotRecorded:        return "not_recorded";
        case NewtonTerminationReason::Converged:          return "converged";
        case NewtonTerminationReason::BudgetExhausted:    return "budget_exhausted";
        case NewtonTerminationReason::ResidualStall:      return "residual_stall";
        case NewtonTerminationReason::ZeroStepStall:      return "zero_step_stall";
        case NewtonTerminationReason::ZeroUpdateAfterTotalFailure:
            return "zero_update_after_total_failure";
        case NewtonTerminationReason::Exception:          return "exception";
    }
    return "not_recorded";
}

// R13.18 (deep review P0-3): the near-worst tie fold, as a PURE FUNCTION so its
// order-independence can be tested. The streaming version inside the solver never dropped the old
// tie set when a strictly larger worst arrived, so A(0.90, not-met) then B(0.99, met) gave false
// while B then A gave true -- the same solve set, two verdicts, and the verdict decides whether
// the forcing-term / objective-mismatch categories may be read at all.
//
// The state is (worst_so_far, all_near_worst_met). A solve either: starts the set, joins it (its
// ratio is within the band of the current worst), replaces it (strictly worse and outside the
// band), or is ignored (clearly better than the worst).
struct NearWorstFold {
    double worst = -1.0;
    // R13.19 (precision review P0-3): the largest progress among solves that met NO tolerance.
    // The R13.18 state was (worst, all_met), and a running boolean cannot RETRACT a solve that
    // leaves the band when a larger worst arrives later -- "near" is NON-TRANSITIVE (A~B and B~C
    // with A!~C), so the three-element chain A(0.9985,not-met) B(0.9994,met) C(1.0,met) gave
    // `false` on two of six permutations and `true` on four. Both fields here are MAXIMA, so the
    // fold is order-independent by construction and the predicate is evaluated at the end.
    double worst_unmet = -1.0;
};

// R13.19 SELF-REVIEW: this struct briefly carried a `mixed_mechanism_in_band` flag whose comment
// said "set by the caller's two-pass check" -- a caller that did not exist. Zero producers, zero
// consumers: the recurring defect of this campaign, introduced by the very commit that fixed an
// instance of it. Removed rather than wired, because the thing it was reaching for is a SEPARATE
// open item (the review's point that the MECHANISM attribution is still order-dependent: worst_*
// updates on strict `>`, so on an exact tie the first-arriving solve's D/S/source/budget receipt
// wins). That needs per-solve receipts and a two-pass reduction, not a boolean, and it is recorded
// as open rather than papered over with a field nothing sets.

inline constexpr double kNearWorstTieBand = 1.0e-3;

// R13.19 SELF-REVIEW / deep-review P0-3 remainder: the MECHANISM attribution was still
// order-dependent even after the `all_met` predicate became order-independent, because the
// D/S/source/budget receipt rides on `worst_*`, which updates on strict `>` -- so two solves at the
// SAME worst ratio let whichever arrived first name the layer:
//   A: progress=0.99, D reached, S not  -> KrylovObjectiveMismatch
//   B: progress=0.99, D reached, S reached -> KrylovForcingTermLimited
// A boolean cannot fix that; the answer depends on the SET of near-worst solves, so the set has to
// be carried. This is the per-solve receipt the review asked for, kept tiny (Newton budgets here
// are single digits) and reduced in one pass at the end.
struct KrylovSolveMechanism {
    double progress = -1.0;
    bool met_tolerance = false;
    bool D_reached = false;
    bool S_reached = false;
    bool budget_exhausted = false;
    int  tolerance_source = 0;      // KrylovToleranceSource

    bool same_mechanism_as(const KrylovSolveMechanism& o) const {
        return met_tolerance == o.met_tolerance && D_reached == o.D_reached &&
               S_reached == o.S_reached && budget_exhausted == o.budget_exhausted &&
               tolerance_source == o.tolerance_source;
    }
};

// True when the solves tied at the worst ratio do NOT agree on their mechanism -- in which case no
// single mechanism may be named, whatever order they arrived in.
inline bool near_worst_mechanism_ambiguous(const std::vector<KrylovSolveMechanism>& solves) {
    double worst = -1.0;
    for (const auto& x : solves) worst = std::max(worst, x.progress);
    if (!(worst >= 0.0)) return false;
    const KrylovSolveMechanism* first = nullptr;
    for (const auto& x : solves) {
        if (!(x.progress >= worst * (1.0 - kNearWorstTieBand))) continue;
        if (first == nullptr) { first = &x; continue; }
        if (!first->same_mechanism_as(x)) return true;
    }
    return false;
}

inline NearWorstFold near_worst_accumulate(NearWorstFold st, double progress,
                                          bool met_tolerance) {
    if (!(progress >= 0.0)) return st;
    st.worst = std::max(st.worst, progress);
    if (!met_tolerance) st.worst_unmet = std::max(st.worst_unmet, progress);
    return st;
}

// Evaluated once, at the end, from two maxima: no solve that met no tolerance lies within the tie
// band of the final worst. Order-independent because max is.
inline bool near_worst_all_met(const NearWorstFold& st) {
    if (!(st.worst >= 0.0)) return true;              // no solves: nothing to contradict
    if (!(st.worst_unmet >= 0.0)) return true;        // every solve met something
    return st.worst_unmet < st.worst * (1.0 - kNearWorstTieBand);
}

// R13.18 (deep review P0-1): the metric the Krylov loop actually stopped on. The receipt used to
// call it D unconditionally, but `WRF_SDIRK3_KRYLOV_WRMS_METRIC` swaps the block-constant D^-1 for
// E^-1 S and the objective becomes rho_E.
enum class KrylovStoppingMetric { Unknown, IdentityS, BlockD, StageWRMS };

// R13.19 (P0-4): the layer an objective mismatch should send work to, DERIVED from the metric the
// solve actually stopped on. Consumed beside the category so the two cannot disagree.
inline const char* krylov_stopping_layer_for(KrylovStoppingMetric m) {
    switch (m) {
        case KrylovStoppingMetric::IdentityS: return "identity_S_vs_newton_merit";
        case KrylovStoppingMetric::BlockD:    return "block_D_vs_newton_merit";
        case KrylovStoppingMetric::StageWRMS: return "newton_WRMS_E_vs_newton_merit";
        case KrylovStoppingMetric::Unknown:   return "stop_metric_unrecorded";
    }
    return "stop_metric_unrecorded";
}

inline const char* krylov_stopping_metric_name(KrylovStoppingMetric m) {
    switch (m) {
        case KrylovStoppingMetric::Unknown:   return "unknown";
        case KrylovStoppingMetric::IdentityS: return "identity_S";
        case KrylovStoppingMetric::BlockD:    return "block_D";
        case KrylovStoppingMetric::StageWRMS: return "stage_wrms_E";
    }
    return "unknown";
}

enum class KrylovToleranceSource { Unknown, Base, EisenstatWalker, StageOverride, InnRamp, Other };

// R13.18 (deep review P0-2): the layer a tolerance-limited verdict should send work to, DERIVED
// from the recorded source. Consumed beside the category so the two cannot disagree.
inline const char* krylov_forcing_layer_for(KrylovToleranceSource s) {
    switch (s) {
        case KrylovToleranceSource::EisenstatWalker: return "eisenstat_walker_forcing";
        case KrylovToleranceSource::StageOverride:   return "stage_tolerance_override";
        case KrylovToleranceSource::InnRamp:         return "inn_tolerance_ramp";
        case KrylovToleranceSource::Base:            return "base_inner_tolerance";
        case KrylovToleranceSource::Unknown:
        case KrylovToleranceSource::Other:           return "inner_tolerance_source_unrecorded";
    }
    return "inner_tolerance_source_unrecorded";
}

inline const char* krylov_tolerance_source_name(KrylovToleranceSource s) {
    switch (s) {
        case KrylovToleranceSource::Unknown:         return "unknown";
        case KrylovToleranceSource::Base:            return "base";
        case KrylovToleranceSource::EisenstatWalker: return "eisenstat_walker";
        case KrylovToleranceSource::StageOverride:   return "stage_override";
        case KrylovToleranceSource::InnRamp:         return "inn_ramp";
        case KrylovToleranceSource::Other:           return "other";
    }
    return "unknown";
}

struct StageFailureSignals {
    // R13.15 (external review P1-4): PROVENANCE, consumed rather than printed. -1 = the tile
    // layer did not stamp it, which is itself not a licence to classify.
    int    signals_from_stage = -1;
    int    classifying_stage = -1;
    // An explicit (ARK) stage runs no Newton iteration, so the implicit signals below are all
    // at their defaults and mean nothing. These are what CAN fail there.
    bool   is_explicit_stage = false;
    bool   explicit_rhs_measured = false;
    bool   explicit_rhs_finite = false;
    // Measured at stage entry, before anything is solved.
    bool entry_state_finite = true;
    // measured BEFORE finite. An unmeasured R0 is not a finite one -- the tile layer used to
    // derive this from isfinite(initial_unscaled_residual), and that member initialises to
    // 0.0, so a solve that never evaluated R0 reported R0_finite=1.
    bool initial_residual_measured = false;
    bool initial_residual_finite = false;
    // Newton's residual at its first and last iteration. -1 = not measured.
    double residual_first = -1.0;
    // R13.8: measured BEFORE finite, here too. R13.6 fixed this for R0 and left it for the
    // FINAL residual, where -1.0 (the not-measured sentinel) was mapped to NewtonDiverged
    // alongside NaN/Inf -- absence of a measurement reported as the strongest finding
    // available.
    bool   final_residual_measured = false;
    double residual_last = -1.0;
    int    newton_iterations = 0;
    // What the iteration was allowed to spend. Without it, "ran out of budget" and "stopped
    // moving" are the same observation, and they point at opposite work.
    int    newton_iteration_budget = -1;
    bool   newton_converged = false;
    // The BEST relative error any GMRES call reached in this stage, as ||r||/||b||. This is
    // NOT progress: on a warm start ||r0|| != ||b||, so 1.0 means "the step is predicted to
    // leave the nonlinear residual where it is", not "the solve went nowhere". Progress is
    // `best_krylov_rel_error_vs_r0` below. -1 = not measured.
    double best_krylov_rel_error = -1.0;
    int    krylov_iterations = 0;
    int    gmres_total_failures = 0;
    // NOT successes. This counts solves that were not TOTAL failures, so a solve that ended
    // at rho = 0.5 without meeting tolerance is included. Named for what it counts.
    int    gmres_non_total_failures = 0;
    // Solves that actually reached tolerance. The quantity the old name implied.
    int    gmres_tolerance_reached = 0;
    // The linear residual GREW in at least one solve. Divergence, not stagnation: the
    // total-failure predicate folds raw_rel_error > 1 together with rel_error >= 0.999.
    bool   krylov_diverged = false;
    // Trust-region / line-search accounting.
    int    accepted_steps = 0;
    int    rejected_steps = 0;
    // R13.11 (referee C7): iteration indices of the first events, so that when two signals
    // are both true the one that happened FIRST can be named. -1 = did not happen. Without
    // these the classifier is a fixed precedence over aggregates, which the header used to
    // call "causal order" and is not.
    int    first_krylov_failure_iter = -1;
    int    first_rejection_iter = -1;
    int    argmin_residual_iter = -1;
    // R13.12 (red team R3-1/R3-2): the two readings of the production total-failure
    // predicate and the rule that was in force. They were computed in the solver and read by
    // nothing -- the ninth instance in this tree of a measurement added and never consumed.
    // On the record they make a disagreement between the rules visible instead of silent.
    int    total_failure_vs_b_count = 0;
    int    total_failure_vs_r0_count = 0;
    bool   krylov_failure_vs_r0 = false;
    // The best relative error measured against where each solve STARTED. This -- not
    // `best_krylov_rel_error`, which is ||r||/||b|| -- is the quantity that answers "did the
    // Krylov solve make progress". -1 = not measured, in which case the classifier says so
    // rather than substituting the other coordinate.
    double best_krylov_rel_error_vs_r0 = -1.0;
    // R13.13: the WORST r0-relative error over the stage's solves, and where it happened.
    // `best_krylov_rel_error_vs_r0` is a MIN over the same solves: it answers "did any solve
    // work", which a late stall behind an early success passes. "Did the linear solve stop
    // working" is a max question. -1 = no solve measured r0.
    double worst_krylov_rel_error_vs_r0 = -1.0;
    int    worst_krylov_iter = -1;
    // How many solves the max is over, so a one-solve stage is not read as a twelve-solve one.
    int    krylov_solves_measured_vs_r0 = 0;
    // Solves excluded from the max: zero-work (converged on entry) or finished (reached
    // tolerance). Neither is evidence about whether Krylov works.
    int    krylov_solves_trivial = 0;
    // Did the WORST-progress solve stop because it met its (adaptive, 0.9-capped) tolerance?
    bool   worst_krylov_met_tolerance = false;
    double worst_krylov_eta = -1.0;
    // R13.17 (external review P0-2): which METRIC the worst solve satisfied, and where its
    // tolerance came from. `met_tolerance` alone cannot separate "met the D objective" from "met
    // the S one", and the layer string hardcoded Eisenstat-Walker while the value can come from a
    // stage override or a ramp -- a category must not name a source it did not read.
    bool   worst_krylov_D_reached = false;
    bool   worst_krylov_S_reached = false;
    KrylovToleranceSource worst_krylov_tolerance_source = KrylovToleranceSource::Unknown;
    bool   worst_krylov_budget_exhausted = false;
    // R13.17 (external review P0-2): with eta saturated at its cap, two solves can tie on the
    // worst ratio while ending for different reasons, and a strict `>` update let whichever came
    // first decide the category. True only if EVERY near-worst solve met a tolerance.
    bool   all_near_worst_met_tolerance = true;
    // R13.19 SELF-REVIEW: the near-worst solves disagree about WHICH mechanism they hit, so naming
    // one would be naming whichever arrived first.
    bool   near_worst_mechanism_ambiguous = false;
    // R13.17 (external review P0-3): the loop's OWN exit reason, from the site that took it.
    NewtonTerminationReason newton_termination = NewtonTerminationReason::NotRecorded;
    // R13.18 (deep review P0-4): the receipt of the solve that ENDED the loop. The worst_* fields
    // belong to the stage's largest-ratio solve, which need not be the same iteration -- subtyping
    // a terminal event from another iteration's evidence describes a solve that ended nothing.
    int    exit_krylov_iter = -1;
    bool   exit_D_reached = false;
    bool   exit_S_reached = false;
    bool   exit_budget_exhausted = false;
    // The stage gate's own metric at the exit solve. -1 = not measured.
    // The exit solve's three readings, raw, so a reader can recompute the category from the
    // record instead of trusting the booleans derived from it.
    double exit_rho_stop_final = -1.0;
    double exit_rho_S_final = -1.0;
    double exit_rho_E_final = -1.0;
    bool   exit_E_reached = false;
    KrylovStoppingMetric exit_stopping_metric = KrylovStoppingMetric::Unknown;
    KrylovToleranceSource exit_tolerance_source = KrylovToleranceSource::Unknown;
    // The Arnoldi budget the WORST solve was given, so the ratio and the budget it was read
    // at are paired by construction rather than by the stage's last assignment.
    int    worst_krylov_restart_budget = -1;
    // Solves with no r0 measurement, and how many of those made the opt-in r0 rule fall back.
    int    krylov_r0_unmeasured_solves = 0;
    int    krylov_rule_fellback_to_b = 0;
    // The boundary in force. <= 0 means "use the header default" -- a record taken before the
    // knob existed must classify exactly as it did then.
    double krylov_no_progress_threshold = -1.0;
    // The INNER budget. The outer one is `newton_iteration_budget`; `krylov_iterations` is
    // iterations SPENT, not the budget. rho_vs_r0 cannot be read without this. -1 = not
    // observed.
    // Whether the threshold site was reached at all (else the value below is this struct's
    // default, not a measurement).
    bool   krylov_threshold_observed = false;
    int    krylov_restart_budget = -1;
    int    krylov_max_restarts = -1;
    // Whether a total-failure rule was in force at all (else the label below is a default).
    bool   krylov_rule_observed = false;
    // The stage gate's own verdict, and whether the step reached the driver.
    bool   gate_metric_ok = false;
    bool   state_published = false;
};

// How much the Newton residual must grow before "diverged" is the honest word. Below this a
// residual that ends slightly above where it started is stagnation with noise, not divergence.
inline constexpr double kDivergenceGrowth = 2.0;
// ||r||/||b|| this close to 1 means the step is predicted to leave the nonlinear residual
// where it is. Calibrated in ||b|| coordinates, where a healthy solve reads ~1e-3.
inline constexpr double kKrylovNoProgress = 0.99;
// ||r||/||r0|| at or above this means the solve did not solve. This is a SEPARATE constant
// because the coordinate change invalidates the calibration of the one above: in r0
// coordinates a healthy solve reads ~0.55 (em_b_wave iteration 0) and a solve that reaches
// tolerance reads ~1e-3, while twelve consecutive solves each removing 2% of their own
// residual read 0.98 -- a stall by any operational standard, and one that a 0.99 threshold
// inherited from ||b|| coordinates would call healthy and route to "the split". A linear
// solve that cannot remove a tenth of its own residual is not solving.
//
// R13.14 (red team round 5): this value is a JUDGMENT, and the honest statement of its evidence
// is that the calibration argues for SOME constant in (0.55, 0.98) and does not select 0.90 over
// 0.85 or 0.95. Two facts make that matter. The tree's own default-budget run reads 0.8795 --
// 2.3% below the boundary, inside run-to-run variation -- and the two sides of the boundary are
// the campaign's two competing explanations (`newton_iteration_budget` vs the operator and the
// preconditioner). And rho_vs_r0 is BUDGET-dependent: a healthy operator given 7 Arnoldi vectors
// on a hard RHS reads 0.92, so this constant cannot by itself separate "the operator is hard"
// from "the inner budget is small" -- which is why the layer string for KrylovStagnated ends in
// `_or_policy` and why `krylov_restart_budget` / `krylov_max_restarts` are on the record beside
// the ratio. (Round 5 caught that sentence naming a field NOTHING PRODUCED -- the recurring class,
// in the comment written to close an instance of it. The fields exist now.) Overridable per run
// via WRF_SDIRK3_KRYLOV_NOPROGRESS_VS_R0.
inline constexpr double kKrylovNoProgressVsR0 = 0.90;
// A residual that ended at or above this fraction of where it started has stopped moving.
// Below it, the iteration was still working and being cut off is a budget statement.
inline constexpr double kResidualStillFalling = 0.95;

inline bool measured(double v) {
    return v == v && v >= 0.0 && v < std::numeric_limits<double>::infinity();
}

// FIRST in causal order, not worst. A run whose entry state is already non-finite will also
// show a stagnating Krylov solve and a rejected step, and reporting either of those sends the
// next week of work to the wrong layer.
// R13.19 (precision review P1-4): EVENT and CAUSE are two questions, and one enum answering both
// is why the ZeroUpdateAfterTotalFailure branch could simultaneously say "a recorded event
// outranks the aggregate reconstruction" and "it must not override the r0 evidence" -- while
// returning immediately, which overrides it.
//
// The fix keeps `first_failure_of` returning the PRIMARY EVENT (four rounds of fixtures depend on
// it) and adds the metric attribution beside it, so the r0 evidence is PRESERVED rather than
// discarded. A reader gets "the loop ended HERE, and the metric evidence says THIS", which is what
// the comment always claimed and the code did not do.
struct StageDiagnosis {
    // What actually ended the stage.
    StageFailure primary_event = StageFailure::None;
    // R13.19 SELF-REVIEW: named precisely. This is **the classification with the recorded exit
    // event removed** -- NOT "what the metric evidence says", which is what an earlier comment
    // claimed. The difference matters: with the termination cleared, the clauses that consume it
    // fall back to the AGGREGATE RECONSTRUCTION, so on a stage with no r0 evidence this field is
    // the old precedence, not a measurement. `attribution_from_metric` below says which it is.
    StageFailure attribution = StageFailure::None;
    // True only when the attribution rests on an actual r0/Krylov reading rather than on the
    // aggregate fallback. Excluding two enum values -- which is all the first version did -- would
    // have called a pure reconstruction "measured".
    bool attribution_from_metric = false;
    bool attribution_measured = false;   // the weaker "not an absence-of-evidence verdict"
};

inline StageFailure first_failure_of(const StageFailureSignals& s) {
    // PROVENANCE FIRST. Classifying stage 3 from stage 2's signals produces a confident,
    // wrong layer -- and the record used to print exactly that, with the mismatch beside it.
    if (s.signals_from_stage < 0 || s.classifying_stage < 0) {
        return StageFailure::StageSignalMissing;
    }
    if (s.signals_from_stage != s.classifying_stage) {
        return StageFailure::StageSignalMismatch;
    }
    // An explicit stage has its own failure set; the implicit clauses below cannot speak to it.
    if (s.is_explicit_stage) {
        if (!s.explicit_rhs_measured) return StageFailure::InsufficientEvidence;
        if (!s.explicit_rhs_finite)   return StageFailure::ExplicitRhsNotFinite;
        if (!s.gate_metric_ok)        return StageFailure::ExplicitAdmissibilityRejected;
        if (!s.state_published)       return StageFailure::ExplicitPublishRejected;
        return StageFailure::None;
    }
    if (!s.entry_state_finite)      return StageFailure::EntryStateNotFinite;
    // Absence of a measurement is not evidence of anything. Checked BEFORE finiteness, so an
    // R0 that was never evaluated cannot be reported as finite or as non-finite.
    if (!s.initial_residual_measured) return StageFailure::InsufficientEvidence;
    if (!s.initial_residual_finite) return StageFailure::InitialResidualNotFinite;

    if (!s.newton_converged) {
        // Three states, not two. A residual that was never measured is not a diverged one:
        // `measured()` rejects the -1 sentinel and NaN/Inf alike, so the sentinel must be
        // separated first or "never ran" becomes "blew up".
        if (!s.final_residual_measured) return StageFailure::InsufficientEvidence;
        // A non-finite final residual IS divergence that overflowed.
        if (!measured(s.residual_last)) return StageFailure::NewtonDiverged;
        if (measured(s.residual_first) && s.residual_first > 0.0 &&
            s.residual_last > kDivergenceGrowth * s.residual_first) {
            return StageFailure::NewtonDiverged;
        }
        // The linear solve before the outer one: Newton cannot converge on top of a solve
        // that does not solve, so this is upstream of any statement about the iteration.
        // Time order, where the record has it. A rejection that happened BEFORE the first
        // Krylov failure is upstream of it (it shrank the radius and changed the next solve's
        // x0 and budget), and is reported first.
        const bool rejection_first =
            (s.first_rejection_iter >= 0 && s.first_krylov_failure_iter >= 0 &&
             s.first_rejection_iter < s.first_krylov_failure_iter);
        if (rejection_first && s.accepted_steps == 0) return StageFailure::AllStepsRejected;
        if (s.krylov_diverged)          return StageFailure::KrylovDiverged;
        // R13.12 (red team R3-2): stagnation is a statement about PROGRESS, so it is measured
        // against where the solve started. Two quantities were being read as if they were one:
        //   ||r||/||b||  -- is the proposed step predicted to reduce the NONLINEAR residual?
        //                   (b = -R, so > 1 is a legitimate trust-region reason to refuse a
        //                   step, and says nothing about whether Krylov worked)
        //   ||r||/||r0|| -- did the LINEAR solve move at all?
        // `gmres_total_failures` is the first question by default and the second under the
        // opt-in flag; `best_krylov_rel_error` is always the first, under a comment claiming
        // the second. On the em_b_wave warm start (r0/||b|| = 1.054) a solve that reduced its
        // residual by 3% reads 1.02 and trips both -- KrylovStagnated for a solve that made
        // progress, which sends the work to the wrong place.
        //
        // So: when r0-relative progress was measured, it decides ALONE -- the total-failure
        // count is not read in that branch, because under the default rule it may be
        // reporting the step question and there is no way here to tell which. When progress
        // was NOT measured the old precedence stands, count included, so the classifier does
        // not get weaker on records that lack the field.
        //
        // WHICH solve. Not the stage's best -- that is a min-over-solves answering "did any
        // solve work", and one early success clears a stage of stalls (em_b_wave iteration 0
        // reaches 0.55 while iteration 3 goes nowhere). Not the first solve to trip the
        // production predicate either: under the default rule that predicate asks the ||b||
        // question, so the solve would be SELECTED in one coordinate and its value REPORTED in
        // another -- and a genuine cold-start stall at raw=0.995 never trips it at all. The
        // max over the solves that measured r0 has no selector and no seam.
        // R13.17 self-review: the exit reason must be honoured whether or not any solve measured
        // r0. Placing this test INSIDE the measured() branch left the misrouting it was written
        // to fix alive on exactly the path with no Krylov evidence -- where the classifier is
        // most likely to fall through to a Newton category and name `residual_floor_or_split`.
        // A loop that stopped because the linear solve produced nothing has its first failure
        // there; without receipts we cannot say WHICH kind, so we say the general one.
        // R13.18 (round 7, P0-B): a RECORDED event outranks the aggregate reconstruction -- but
        // it may only claim what it observed. This one observed a zero update after a
        // total-failure flag, so it gets its own category and layer; it does NOT get to say the
        // linear solve failed.
        //
        // R13.19 (precision review P1-4): "and it must not override the r0 evidence" used to be
        // written here while this branch RETURNED, which is overriding it. That is now true
        // rather than aspirational: `stage_diagnosis_of` reports the metric attribution beside
        // this event, computed from the same clauses with the recorded exit removed, so the r0
        // evidence is preserved on the record instead of discarded by the event that outranks it.
        if (s.newton_termination == NewtonTerminationReason::ZeroUpdateAfterTotalFailure) {
            // R13.18 (deep review P0-4): subtype from the EXIT solve when its receipt is present.
            // A terminal event describes the solve that ended the loop; the stage-worst receipt
            // describes a possibly different iteration and is telemetry, not attribution.
            if (s.exit_krylov_iter >= 0 && s.exit_D_reached && !s.exit_S_reached) {
                return StageFailure::KrylovObjectiveMismatch;
            }
            // R13.19 (precision review P0-2): both solver metrics met and the ENTRY-weighted
            // linear one not. This is a real seam between the metrics the solve was steered by,
            // and it is NOT the stage gate -- so it may not be claimed while the gate itself is
            // recorded as having passed, and it must not be promoted over the event that actually
            // ended the loop. `gate_metric_ok` is read here so the two cannot disagree.
            if (s.exit_krylov_iter >= 0 && s.exit_D_reached && s.exit_S_reached &&
                measured(s.exit_rho_E_final) && !s.exit_E_reached && !s.gate_metric_ok) {
                return StageFailure::KrylovEntryMetricMismatch;
            }
            return StageFailure::ZeroUpdateAfterTotalFailure;
        }
        // ...and an exception in the linear solve is not the outer iteration's failure either.
        if (s.newton_termination == NewtonTerminationReason::Exception) {
            return StageFailure::KrylovSolveThrew;
        }
        if (measured(s.worst_krylov_rel_error_vs_r0)) {
            // R13.14 (round 5): the boundary is a judgment and must be movable per run without
            // a rebuild. An unset/invalid value falls back to the header constant, so records
            // taken before the knob existed classify exactly as they did then.
            // R13.16 (round 6, R6-4): the threshold the classifier APPLIES and the one the
            // emitter PRINTS were two predicates on two different fields, so a row could say
            // `not_reached` while 0.90 silently decided the category. Both key on the same flag.
            const double no_progress =
                (s.krylov_threshold_observed &&
                 s.krylov_no_progress_threshold > 0.0 &&
                 s.krylov_no_progress_threshold <= 1.0)
                    ? s.krylov_no_progress_threshold : kKrylovNoProgressVsR0;
            // R13.17 (external review P0-3), MEASURED: the loop's own exit outranks the ratio.
            //
            // At dt=600 BOTH the default and the 12x-budget runs exit with
            // `newton_exit=linear_solve_failure` -- the solver's own message is "[Newton] GMRES
            // total failure + zero update". Only the ratio moved (0.9941 -> 0.8622), and it
            // crossed the 0.90 boundary, so the 12x run was classified `newton_stagnated`, layer
            // `residual_floor_or_split` -- routing to the split-explicit rebuild a run whose loop
            // stopped because THE LINEAR SOLVE GAVE IT NOTHING. The campaign read that flip as
            // "the failure moved outward to the Newton iteration"; the exit reason says it never
            // moved. A loop that stopped because the linear solve failed has its first failure in
            // the linear solve whatever the progress ratio reads, and WHICH kind is answered by
            // the same receipts below.
            if (s.worst_krylov_rel_error_vs_r0 >= no_progress) {
                // R13.16 (round 6, R6-2) / R13.17 (external review P0-2): WHY it made no
                // progress. Four different answers, three of which are not the operator's fault,
                // and they route to four different layers.
                //
                //   D met, S not  -> the solve minimised WHAT IT WAS ASKED TO and the result is
                //                    still useless to the Newton merit. Not a stall (it worked)
                //                    and not a forcing-term problem (tightening eta does not
                //                    align two objectives) -- the D objective is the question.
                //                    `InternalConvergenceStop` IS this state, and the round-6
                //                    rule missed it entirely: `met_tolerance` was
                //                    `reason == ToleranceReached` only, so the one termination
                //                    that means "met its own tolerance" fell through to
                //                    KrylovStagnated -- the exact misclassification the category
                //                    was added to prevent.
                //   S met         -> it did what was asked in the coordinate that matters, and
                //                    little progress means the forcing term asked for little.
                //   neither, budget gone -> it was cut off while still descending.
                //   otherwise     -> it could not progress. That is the operator.
                //
                // A tie on the worst ratio is refused rather than resolved: with eta saturated at
                // its cap two solves can share the worst value and end for different reasons, and
                // a strict `>` update let whichever came first name the layer.
                if (!s.all_near_worst_met_tolerance) {
                    return StageFailure::KrylovStagnated;
                }
                // R13.19 SELF-REVIEW: solves tied at the worst ratio that hit DIFFERENT mechanisms
                // cannot name one. Reporting the first arrival's was order-dependent; reporting
                // the general category is not.
                if (s.near_worst_mechanism_ambiguous) {
                    return StageFailure::KrylovStagnated;
                }
                if (s.worst_krylov_D_reached && !s.worst_krylov_S_reached) {
                    return StageFailure::KrylovObjectiveMismatch;
                }
                if (s.worst_krylov_S_reached || s.worst_krylov_met_tolerance) {
                    return StageFailure::KrylovForcingTermLimited;
                }
                if (s.worst_krylov_budget_exhausted) {
                    return StageFailure::KrylovBudgetExhausted;
                }
                return StageFailure::KrylovStagnated;
            }
            // Every solve moved. Whatever refused the step is downstream, and the later
            // clauses name it.
        } else if (s.krylov_r0_unmeasured_solves > 0 || s.krylov_solves_trivial > 0) {
            // R13.16 (round 6, R6-3): `worst == -1` has FOUR causes and they were collapsed into
            // one branch applying the ||b||-coordinate rule -- the very rule this line of work
            // exists to get away from. Two are now separated by counters that were added in the
            // commits that created the ambiguity and then read by nothing: every solve did zero
            // work, or no solve measured r0. In both, the ||b|| reading says nothing about
            // whether Krylov works, so there is no Krylov evidence and the later clauses get
            // their case. (An old record, or no solves at all, keeps the old precedence below.)
        } else {
            if (s.gmres_total_failures > 0) return StageFailure::KrylovStagnated;
            if (measured(s.best_krylov_rel_error) &&
                s.best_krylov_rel_error >= kKrylovNoProgress) {
                return StageFailure::KrylovStagnated;
            }
        }
        // The linear solve worked and nothing it proposed was taken.
        if (s.accepted_steps == 0 && s.rejected_steps > 0) {
            return StageFailure::AllStepsRejected;
        }
        // R13.17 (external review P0-3): when the loop RECORDED why it stopped, that is the
        // answer -- not a precedence over aggregates. The reconstruction below cannot separate
        // "the residual stopped moving" from "the budget ran out at a flat residual", and the
        // campaign's reading of the 12x-budget run ("the failure moved outward") rests on exactly
        // that distinction.
        if (s.newton_termination == NewtonTerminationReason::BudgetExhausted) {
            return StageFailure::NewtonBudgetExhausted;
        }
        if (s.newton_termination == NewtonTerminationReason::ResidualStall ||
            s.newton_termination == NewtonTerminationReason::ZeroStepStall) {
            return StageFailure::NewtonStagnated;
        }
        // Still converging when the budget ran out. This is the em_b_wave dt=600 case, and
        // it is not a numerical failure -- calling it one sends the work to the split.
        const bool still_falling =
            measured(s.residual_first) && measured(s.residual_last) &&
            s.residual_first > 0.0 &&
            s.residual_last < kResidualStillFalling * s.residual_first;
        if (still_falling && s.newton_iteration_budget > 0 &&
            s.newton_iterations >= s.newton_iteration_budget) {
            return StageFailure::NewtonBudgetExhausted;
        }
        return StageFailure::NewtonStagnated;
    }

    if (!s.gate_metric_ok)   return StageFailure::AdmissibilityRejected;
    if (!s.state_published)  return StageFailure::PublishRejected;
    return StageFailure::None;
}

// The metric evidence on its own, with the recorded exit event REMOVED. Running the same
// classifier over a copy whose termination is NotRecorded is deliberate: it reuses one set of
// clauses instead of duplicating them, so the two answers cannot drift apart.
inline StageDiagnosis stage_diagnosis_of(const StageFailureSignals& s) {
    StageDiagnosis d;
    d.primary_event = first_failure_of(s);
    StageFailureSignals metric_only = s;
    metric_only.newton_termination = NewtonTerminationReason::NotRecorded;
    d.attribution = first_failure_of(metric_only);
    // The weak flag: the attribution is not itself an absence-of-evidence verdict.
    d.attribution_measured =
        (d.attribution != StageFailure::InsufficientEvidence) &&
        (d.attribution != StageFailure::StageSignalMissing);
    // The one that matters: does the attribution rest on a real r0/Krylov reading, or on the
    // aggregate fallback the campaign spent four rounds moving away from? Without this a pure
    // reconstruction naming an operator layer would read as "measured".
    d.attribution_from_metric =
        d.attribution_measured &&
        (measured(s.worst_krylov_rel_error_vs_r0) ||
         measured(s.best_krylov_rel_error_vs_r0) ||
         s.krylov_solves_measured_vs_r0 > 0);
    return d;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_FIRST_FAILURE_H
