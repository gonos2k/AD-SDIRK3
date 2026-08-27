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
#include <cstring>
#include <cstdint>   // uint64_t: libc++ pulls it in transitively, libstdc++ does not -- a header
                     // that names fixed-width types must say so, or it builds on macOS and fails
                     // on the Linux CI runner.

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
    // R13.21 (external review P1-1 / P0-3): the metric this solve STOPPED on. Without it a
    // stage-worst objective mismatch cannot name its layer, and two tied solves that stopped on
    // different metrics look identical to the tie check.
    int  stopping_metric = 0;       // KrylovStoppingMetric

};

// R13.21 (external review P0-3): the layer the record would emit for ONE solve's receipt.
// Defined after the layer maps; declared here because the tie reducer below needs it.
inline const char* krylov_specific_layer_for(const KrylovSolveMechanism& m);

// R13.21 (external review P0-3): TWO SOLVES ARE INTERCHANGEABLE ONLY IF THEY SEND YOU TO THE SAME
// PLACE. R13.20 narrowed the tie check from field equality to CATEGORY equality, on the ground
// that the classifier does not branch on `tolerance_source`. That was right about the category and
// wrong about the consequence: `specific_layer` for `KrylovForcingTermLimited` IS derived from the
// tolerance source, and for `KrylovObjectiveMismatch` from the stopping metric -- and the
// stage-worst receipt is updated on a STRICT `>`, so on an exact tie the first arrival's source is
// what the emitter reads. Category was order-invariant; the next place to work was not.
//
// Comparing the DERIVED LAYER rather than the raw fields keeps R13.20's gain: two solves that both
// imply `KrylovStagnated` derive no specific layer, so a differing tolerance source does not make
// them ambiguous and the refusal does not fire toward the operator/split layer for a difference
// that cannot change the answer.
inline bool implies_same_action_as(const KrylovSolveMechanism& a, const KrylovSolveMechanism& b);

// The category ONE solve's receipt implies. Defined once and called from both the tie-set
// ambiguity check and the classifier's four-way, so the two cannot drift apart -- the drift the
// duplicated-threshold comment elsewhere in this file warns about.
inline StageFailure krylov_mechanism_category(const KrylovSolveMechanism& m) {
    if (m.D_reached && !m.S_reached)   return StageFailure::KrylovObjectiveMismatch;
    if (m.S_reached || m.met_tolerance) return StageFailure::KrylovForcingTermLimited;
    if (m.budget_exhausted)            return StageFailure::KrylovBudgetExhausted;
    return StageFailure::KrylovStagnated;
}



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
        if (!implies_same_action_as(*first, x)) return true;
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

// R13.20 (round 9, R9-5): `EisenstatWalker` used to cover THREE different levers, only one of
// which is the forcing term. At Newton iteration 0 the E-W block does not compute
// gamma*(||R_k||/||R_{k-1}||)^alpha at all -- it seeds `max(krylov_tol, WRF_SDIRK3_EW_ETA_INITIAL)`
// and sets `ew_eta_updated_this_iter = false`, which is the code's own statement that E-W updated
// nothing -- and BOTH arms then clamp into [ew_eta_min, ew_eta_max]. Recording all three as
// `EisenstatWalker` routes a tolerance-limited verdict to `eisenstat_walker_forcing`, i.e. to
// tuning ew_gamma/ew_alpha, on solves where neither knob was read. The clamp case is not
// hypothetical: eta_max saturates exactly in the failing regime (see
// converged-is-not-solved-adaptive-tolerance). Each value below is now decided AT the site by
// comparing the candidate values, so the recorded source names the knob that actually bound.
enum class KrylovToleranceSource {
    Unknown,
    Base,             // options_.krylov_tol is the binding value
    EisenstatWalker,  // the computed forcing term eta_k is binding (neither clamped nor floored)
    EwEtaClamp,       // the [ew_eta_min, ew_eta_max] clamp moved the value -- eta_max/min is the knob
    EwInitialFloor,   // the iteration-0 seed constant WRF_SDIRK3_EW_ETA_INITIAL is binding
    StageOverride,
    InnRamp,
    Other
};

// R13.18 (deep review P0-2): the layer a tolerance-limited verdict should send work to, DERIVED
// from the recorded source. Consumed beside the category so the two cannot disagree.
inline const char* krylov_forcing_layer_for(KrylovToleranceSource s) {
    switch (s) {
        case KrylovToleranceSource::EisenstatWalker: return "eisenstat_walker_forcing";
        case KrylovToleranceSource::EwEtaClamp:      return "ew_eta_min_max_clamp";
        case KrylovToleranceSource::EwInitialFloor:  return "ew_initial_eta_floor";
        case KrylovToleranceSource::StageOverride:   return "stage_tolerance_override";
        case KrylovToleranceSource::InnRamp:         return "inn_tolerance_ramp";
        case KrylovToleranceSource::Base:            return "base_inner_tolerance";
        case KrylovToleranceSource::Unknown:
        case KrylovToleranceSource::Other:           return "inner_tolerance_source_unrecorded";
    }
    return "inner_tolerance_source_unrecorded";
}

// R13.21 (external review P0-3): the definitions promised above, now that both layer maps exist.
inline const char* krylov_specific_layer_for(const KrylovSolveMechanism& m) {
    switch (krylov_mechanism_category(m)) {
        case StageFailure::KrylovForcingTermLimited:
            return krylov_forcing_layer_for(
                static_cast<KrylovToleranceSource>(m.tolerance_source));
        case StageFailure::KrylovObjectiveMismatch:
            return krylov_stopping_layer_for(
                static_cast<KrylovStoppingMetric>(m.stopping_metric));
        default:
            // Every other category names a layer from the category alone, so two solves that
            // imply it are interchangeable whatever their provenance fields say.
            return "n/a";
    }
}

inline bool implies_same_action_as(const KrylovSolveMechanism& a, const KrylovSolveMechanism& b) {
    if (krylov_mechanism_category(a) != krylov_mechanism_category(b)) return false;
    const char* la = krylov_specific_layer_for(a);
    const char* lb = krylov_specific_layer_for(b);
    return la == lb || std::strcmp(la, lb) == 0;
}

inline const char* krylov_tolerance_source_name(KrylovToleranceSource s) {
    switch (s) {
        case KrylovToleranceSource::Unknown:         return "unknown";
        case KrylovToleranceSource::Base:            return "base";
        case KrylovToleranceSource::EisenstatWalker: return "eisenstat_walker";
        case KrylovToleranceSource::EwEtaClamp:      return "ew_eta_clamp";
        case KrylovToleranceSource::EwInitialFloor:  return "ew_initial_floor";
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
    // R13.22: candidates the total-failure rule discarded, and how many of those would have
    // reduced the nonlinear residual had the trust region been allowed to test them. Reported,
    // not classified on -- the counter answers "how often", the policy question is separate.
    int    discarded_candidates_seen = 0;
    int    discarded_candidates_descent = 0;
    // R13.20 (numerics referee, claim 1): the last Newton iteration the Taylor-defect probe
    // measured, or -1. Reported, not classified on -- it says whether the campaign's tau evidence
    // covers the iteration that ended the loop, which it structurally cannot when that iteration's
    // step was rejected.
    int    taylor_probe_last_iter = -1;
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
    // R13.21 (external review P1-1): the stage-worst twin of `exit_stopping_metric`.
    KrylovStoppingMetric worst_krylov_stopping_metric = KrylovStoppingMetric::Unknown;
    bool   worst_krylov_budget_exhausted = false;
    // R13.20 (claim 7.4): the worst solve in the ladder's coordinate (rho_D, b-normalised) and in
    // the success coordinate (rho_S). Reported, not classified on.
    float  worst_krylov_rho_D = -1.0f;
    float  worst_krylov_rho_S = -1.0f;
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
    // R13.24 (external review P1-3): a SECOND, independent authority for "which Newton iteration
    // ended the loop". The exit receipt's own iteration is `exit_krylov_iter`; comparing that
    // field with itself -- which is what the view did -- can never detect a receipt promoted from
    // another iteration, the very thing the completeness rule was written to catch.
    // -1 = never stamped, in which case the comparison is skipped rather than passed.
    int    newton_exit_event_iter = -1;
    // R13.21 SELF-REVIEW ROUND 2: the exit solve's Arnoldi budget, so `krylov_receipt_complete`
    // has something to judge. Without these the completeness rule had no production consumer at
    // all -- a rule with a producer and no consumer, written in the increment that closes that
    // class.
    // R13.23 (P0-4): the tolerance the exit solve actually applied, so the reached flags can be
    // re-derived rather than trusted.
    double exit_tolerance_applied = -1.0;
    int    exit_arnoldi_spent = -1;
    int    exit_arnoldi_allowed = -1;
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

// R13.24: the sentinel `measured()` rejects. Named because "-1.0" appears at a dozen call sites
// and a reader cannot tell a sentinel from a legitimate negative without knowing the convention.
constexpr double kMetricNotMeasured = -1.0;

inline bool measured(double v) {
    return v == v && v >= 0.0 && v < std::numeric_limits<double>::infinity();
}

// R13.21 (external review P0-1): the entry verdict, as a PURE rule so a fixture can reject its
// negation. The solver used to decide this inline from the termination LABEL, and the label cannot
// tell a convergence from a stopping-objective hit.
//
// TWO STATES share one return path. The Krylov loop returns before any Arnoldi step whenever the
// inner stopping objective is already under tolerance:
//
//   rho_stop < eta AND rho_S <  eta   -- converged; nothing downstream should second-guess it
//   rho_stop < eta AND rho_S >= eta   -- an OBJECTIVE MISMATCH: it minimised what it was asked to
//                                        and the result is still useless to the Newton merit
//
// R13.19 made the receipt distinguish them (`success` and `rel_error` carry the S coordinate).
// This is the consumer catching up: only the first may exempt a solve from the total-failure
// rules, and only the first may have its full step taken without a nonlinear check.
struct KrylovEntryVerdict {
    bool S_reached = false;           // converged in the coordinate `success` is judged by
    bool objective_mismatch = false;  // stop metric met, S not met
};

inline KrylovEntryVerdict krylov_entry_verdict(bool stop_metric_reached_on_entry,
                                               bool S_tolerance_reached) {
    KrylovEntryVerdict v;
    v.S_reached = stop_metric_reached_on_entry && S_tolerance_reached;
    v.objective_mismatch = stop_metric_reached_on_entry && !S_tolerance_reached;
    return v;
}

// The ONLY entry state that may exempt a solve from the total-failure rules. Deliberately not
// `stop_metric_reached_on_entry`: that is the label, and reading it here was the defect.
inline bool entry_exempts_total_failure(const KrylovEntryVerdict& v) {
    return v.S_reached;
}

// An objective mismatch may still take its step -- but only against a measured nonlinear decrease,
// which is what the other two acceptance sites in the Newton loop already require of everyone else.
inline bool entry_requires_nonlinear_decrease(const KrylovEntryVerdict& v) {
    return v.objective_mismatch;
}

// R13.21 (external review P0-2): the exit solve's METRIC ATTRIBUTION, as a separate answer to a
// separate question. `first_failure_of` reports the EVENT that ended the stage; this reports what
// the receipt of the solve that ended it says about WHY. Returns `None` when the exit receipt
// cannot support a subtype -- absence of evidence, reported as such.
// R13.21 (external review P1-2): IS THIS RECEIPT COMPLETE?
//
// Six sites return a Krylov result. Four fill the metric/budget receipt R13.18-R13.20 added; the
// two `NanRetryExhausted` early returns filled only the constructor fields plus
// `initial_rel_error`, so every new field stayed at its sentinel. A terminal solve that ends on
// that path therefore hands the classifier an exit receipt it cannot subtype, and the record falls
// back to the generic event or to insufficient evidence -- the classifier reporting absence where
// the solve knew the answer.
//
// The rule lives here, as a view over the values rather than over the type, so a fixture can
// reject its negation without pulling in the solver header. `tolerance_applied` and the two
// tolerance-reached flags are deliberately NOT required: a path that never evaluated a tolerance
// has nothing to report, and demanding it would push a producer into inventing one.
struct KrylovReceiptView {
    double rho_D_final = -1.0;
    double rho_S_final = -1.0;
    int    stopping_metric = -1;   // KrylovStoppingMetric; 0 == Unknown, -1 == not stamped
    int    arnoldi_spent = -1;
    int    arnoldi_allowed = -1;
    // R13.23 (deep review P0-4): the receipt must be SELF-CONSISTENT, not merely populated.
    double tolerance_applied = -1.0;
    bool   D_reached = false;
    bool   S_reached = false;
    int    receipt_iter = -1;      // which Newton iteration this receipt belongs to
    int    exit_iter = -1;         // which iteration ended the loop (independent authority)
};

inline bool krylov_receipt_complete(const KrylovReceiptView& r) {
    if (!measured(r.rho_D_final) || !measured(r.rho_S_final)) return false;
    // R13.23 (P0-4): `Unknown` is 0 and the old test was `>= 0`, so an UNSTAMPED metric passed as
    // complete -- and an attribution derived from it named a layer the receipt could not support.
    // The enum's own "I do not know" value must fail a completeness test.
    if (r.stopping_metric <= static_cast<int>(KrylovStoppingMetric::Unknown)) return false;
    if (r.arnoldi_spent < 0 || r.arnoldi_allowed <= 0) return false;
    // Work spent cannot exceed work allowed. A receipt that says otherwise is not describing one
    // solve.
    if (r.arnoldi_spent > r.arnoldi_allowed) return false;
    // The reached flags must follow from the numbers. A tolerance that was never applied cannot
    // have been reached -- claiming otherwise is the fabrication this rule exists to catch.
    if (!measured(r.tolerance_applied) || !(r.tolerance_applied > 0.0)) {
        return !r.D_reached && !r.S_reached;
    }
    if (r.D_reached != (r.rho_D_final < r.tolerance_applied)) return false;
    if (r.S_reached != (r.rho_S_final < r.tolerance_applied)) return false;
    // And it must belong to the solve it is being read as. Both unstamped is the pre-R13.18 record
    // and is allowed through; one stamped and disagreeing is not.
    if (r.receipt_iter >= 0 && r.exit_iter >= 0 && r.receipt_iter != r.exit_iter) return false;
    return true;
}

// R13.24 (external review, section 11): the line-search landmines, PINNED not opened.
//
// The Armijo path is unreachable today. Flipping its guard would not restore it -- three separate
// defects fire the moment it runs, and each is recorded here as a rule so a future reopening has
// to confront them instead of discovering them in a dt=600 run:
//
//   (1) BUDGET EXHAUSTION. `alpha` is only assigned on Armijo success or at ls_iter == 9. The
//       loop also stops when `rhs_budget` hits 0, which happens first (budget starts at 5, the
//       loop assumes 9 arms). Neither assignment runs, so `alpha` keeps its initial 1.0 and the
//       FULL step -- the one Armijo just rejected -- is applied.
//   (2) MAX ITERATIONS. At ls_iter == 9 the last trial alpha is taken whether or not it satisfied
//       the Armijo condition. The search is "use the last thing tried", not fail-closed.
//   (3) STALE BOOKKEEPING. A successful backtrack updates `residual_after_step` but not
//       `accepted_residual` / `accepted_residual_norm`, so the ledger and the Newton diagnostics
//       describe a step at alpha=1 while alpha < 1 was applied.
//
// A correct line search must return "did Armijo accept?" as its own answer, not leave it implicit
// in whatever alpha survived.
// R13.25 (external review, section 10): this had FIXTURES AND NO CALLER -- the "rule with no
// consumer is a comment with a test" shape, committed by me in the increment where I fixed it for
// another rule. Someone flipping `skip_line_search` would have re-run the dead Armijo block
// without ever reaching this, and every fixture would still have passed. It is now called from the
// accept decision inside that block, so reopening the guard cannot bypass it.
inline bool line_search_alpha_is_trustworthy(bool armijo_satisfied, int arms_tried,
                                             int rhs_budget_remaining) {
    if (armijo_satisfied) return true;          // the only case that earns the step
    (void)arms_tried; (void)rhs_budget_remaining;
    return false;                                // exhausting budget or arms is a REJECT
}

// R13.25 (external review P0-1): A SIGNAL IS NOT AN OUTCOME.
//
// R13.24 stopped the original veto from re-rejecting an admitted candidate inside trust. The
// defect moved one transition later. `gmres_total_failure` is assigned exactly twice, both BEFORE
// the trial (:11529 declaration, :11532 the veto branch), and never recomputed afterwards. So when
// the trust region evaluates an ADMITTED candidate and refuses every attempt:
//
//   runtime  gmres_total_failure = false   -> the zero-update break does not take the typed exit,
//                                            the loop keeps iterating on an unchanged K, and the
//                                            stage ends as a generic stall
//   statistics                             -> a post-hoc correction counts the same iteration as
//                                            a linear total failure
//
// One iteration, two contradictory records, and the difference is not cosmetic: it changes the
// Newton iteration count, the RHS/JVP call count, the trust radius trajectory, the stage exit
// reason, the exit receipt and the classifier's verdict.
//
// The cause is that `CandidateDisposition` (R13.24) describes ADMISSION and was read as though it
// described the candidate's fate. Splitting the two types makes the missing state impossible to
// omit: a signal is what the linear solve reported, an outcome is what the globalizer decided.
enum class LinearSignal {
    None,                 // the linear solve raised nothing
    TotalFailure,         // the residual-ratio veto fired
    EntryMetricMismatch,  // the stop metric was met but the S tolerance was not
};

enum class TrialOutcome {
    NotOffered,           // no globalizer saw the candidate
    Vetoed,               // the signal stood; the candidate was never evaluated
    AcceptedDirect,       // the direct-accept shortcut took it
    AcceptedTrust,        // a trust attempt took it
    AcceptedRecovery,     // the -M^-1 R fallback took it
    RejectedTrust,        // OFFERED and refused -- the state R13.24 could not express
    RejectedRecovery,
    ZeroUpdate,           // nothing was applied
};

// POLICY A, chosen explicitly. An admitted candidate that the globalizer refuses ends the stage
// with the TYPED exit, not a generic stall: the linear solve did signal total failure, a nonlinear
// mechanism was given one chance to overrule it, and it declined. Reaching a generic
// `ZeroStepStall` because a boolean happened to be false is not a policy -- it is the absence of
// one. (Policy B, a bounded retry on a changed trust radius, is a different design; it needs a
// retry budget and its own exhaustion category, and is not what this repairs.)
inline bool linear_failure_stands_after_trial(LinearSignal signal, TrialOutcome outcome) {
    if (signal == LinearSignal::None) return false;
    switch (outcome) {
        case TrialOutcome::AcceptedDirect:
        case TrialOutcome::AcceptedTrust:
        case TrialOutcome::AcceptedRecovery:
            return false;      // a globalizer took the step: the signal was overruled on merit
        default:
            return true;       // vetoed, refused, or nothing applied
    }
}

// What the statistics must count, kept separate so one counter cannot mean three things.
// R13.24's comment claimed `gmres_total_failures` counts "did the linear solve fail". It does not:
// the predicate also fires on `entry_mismatch_step_rejected`, which is a NONLINEAR check failing
// after a solve that raised no total-failure signal at all.
inline bool is_linear_total_failure_signal(LinearSignal s) {
    return s == LinearSignal::TotalFailure;
}
inline bool is_entry_metric_mismatch_event(LinearSignal s) {
    return s == LinearSignal::EntryMetricMismatch;
}

// R13.24 (external review P0-1): ONE veto, read by every consumer.
//
// R13.23 cleared the DERIVED flag `gmres_total_failure` when the arbitration admitted a candidate,
// and concluded the candidate was "handed to the globalizer to accept or reject on its own terms".
// It is not. Trust acceptance reads the ORIGINAL `gmres_total_failure_candidate`, which is `const`
// and which the arbitration never touched, and rejects on it unconditionally
// (newton_solver.cpp:12049). So an admitted candidate entered the loop and was refused by every
// attempt: admission, not rescue.
//
// That is this campaign's signature defect -- a rule whose producer and first consumer agree while
// a LATER consumer still reads the old quantity -- committed one level below where R13.23 fixed
// it. The correction is not another flag but a single function every consumer calls, so a future
// reader cannot find two vetoes to choose between.
//
// Naming matters here and the old name was doing damage: `rescued` asserts an outcome the
// mechanism cannot deliver. Admission to a trial is what it actually grants.
inline bool effective_total_failure_veto(bool total_failure_candidate, bool arbitration_admitted) {
    return total_failure_candidate && !arbitration_admitted;
}

// The disposition of a candidate, as one value instead of three booleans that can disagree.
// `AdmittedThenRejected` is the state R13.23 could not express: the linear solve DID signal total
// failure, the arbitration DID admit the candidate, and the globalizer then refused it. Counting
// that as a non-total-failure -- which is what reading the cleared derived flag does -- loses the
// linear signal from the statistics entirely.
enum class CandidateDisposition {
    OrdinaryProposal,        // no total-failure signal; the usual path
    LinearFailureVetoed,     // signalled and not admitted: the veto stands
    AdmittedToTrial,         // signalled, admitted, outcome not yet known
};

inline CandidateDisposition candidate_disposition(bool total_failure_candidate,
                                                  bool arbitration_admitted) {
    if (!total_failure_candidate) return CandidateDisposition::OrdinaryProposal;
    return arbitration_admitted ? CandidateDisposition::AdmittedToTrial
                                : CandidateDisposition::LinearFailureVetoed;
}

// Did the LINEAR solve signal total failure? This is what the statistics must count, and it is
// independent of what any globalizer later decided. R13.23 made the counter read the derived flag,
// so an admitted candidate silently moved from `gmres_total_failures` to `gmres_non_total_failures`
// -- the linear signal disappearing because a nonlinear mechanism was given a chance.
inline bool counts_as_linear_total_failure(CandidateDisposition d) {
    return d == CandidateDisposition::LinearFailureVetoed ||
           d == CandidateDisposition::AdmittedToTrial;
}

// R13.23 (self-review round 5): the boundary receipt's dedup key.
//
// The receipt re-emits only when its content changes. That makes the key a CONTRACT: every field
// the receipt prints must be in it, or a change to that field is silently suppressed and the
// record keeps showing the old value. Hand-packing bit ranges made this easy to get wrong -- the
// first version printed the rank position `my=(mypx,mypy)` without keying it, so an interior-rank
// move that left the effective flags alone would have gone unreported.
//
// A mixing hash removes the bit-budget question entirely, and the fixtures pin the contract by
// varying each printed field ALONE and requiring the key to move.
//
// The setter call counter is deliberately NOT keyed: including it would re-emit on every call and
// destroy the dedup. Which is exactly why the receipt labels it `first_emit_at_setter_call` --
// frozen at first emission, it is not a tally of how many times the setter ran.
inline uint64_t boundary_receipt_key(uint64_t raw_flag_bits, uint64_t effective_flag_bits,
                                     int nprocx, int nprocy, int mypx, int mypy) {
    auto mix = [](uint64_t h, uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    };
    uint64_t h = 0x9e3779b97f4a7c15ULL;   // nonzero seed: an all-false state must not look "never emitted"
    h = mix(h, raw_flag_bits);
    h = mix(h, effective_flag_bits);
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(nprocx)));
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(nprocy)));
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(mypx)));
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(mypy)));
    return h == 0 ? 1 : h;               // 0 is the "nothing emitted yet" sentinel
}

// R13.23 (self-review, round 2): on the direct-accept path, WHEN is the trust loop entered?
//
// This corrects the round-1 note. "The loop is not gated on nk_trust_region" is true
// syntactically and misleading in practice: with the flag false, every path through the shortcut
// that does NOT accept also raises the total-failure flag, and the loop's condition needs both to
// be false. So the loop body does not execute -- which is what the shortcut's own comment already
// says ("trust neither accepted nor rejected, it never ran").
//
// The one exception is the arbitration rescue, and that makes it the SOLE entry into the trust
// loop on the shipped configuration:
//
//     step_accepted  = !candidate && !entry_mismatch_rejected
//     total_failure  = !step_accepted && !rescued
//     loop entered   = !step_accepted && !total_failure   ==   rescued
//
// Worth stating plainly because it sets what the rescue is really doing: not "handing the step
// back to a mechanism that was running anyway", but STARTING a mechanism this configuration
// otherwise never runs.
inline bool shortcut_path_enters_trust_loop(bool total_failure_candidate,
                                            bool entry_mismatch_rejected,
                                            bool arbitration_rescued) {
    // The arbitration only fires on a total-failure candidate, so a rescue without one is not a
    // reachable input; treat it as no rescue rather than inventing an entry.
    const bool rescued = arbitration_rescued && total_failure_candidate;
    const bool step_accepted = !total_failure_candidate && !entry_mismatch_rejected;
    const bool total_failure = !step_accepted && !rescued;
    return !step_accepted && !total_failure;
}

// R13.23 (self-review): where does a RESCUED candidate go?
//
// The arbitration clears the total-failure veto without accepting the step, which is only
// coherent if something downstream then judges it. The worry was that on the shipped
// configuration nothing does -- `nk_trust_region` is effectively FALSE there, so the direct-accept
// shortcut is what runs, and it has already been skipped by the time the arbitration fires.
//
// MEASURED, by reading the control flow rather than the flag name: the trust loop is NOT gated on
// `nk_trust_region`. Only the shortcut is. The loop's own condition is the gate, `max_trust_attempts`
// is 3 unconditionally, and no enclosing block between the shortcut and the loop tests the flag. So
// a rescue lands in a real globalizer on every configuration, and `nk_trust_region = false` means
// "try a direct accept first", not "no trust region".
//
// The rule is extracted so the loop's condition is the same object a fixture pins: the state a
// rescue produces (no step, no signal) must ENTER the loop. A fourth, silent outcome -- veto
// cleared, step not taken, loop not entered -- would be the zero-update loop the shortcut's own
// comment warns about, and the pins make it unrepresentable.
inline bool trust_loop_continues(bool step_accepted, bool gmres_total_failure,
                                 int attempts_remaining, int rhs_budget) {
    return !step_accepted && !gmres_total_failure && attempts_remaining > 0 && rhs_budget > 0;
}

// R13.23 (self-review): is the Armijo line search reachable? MEASURED: no.
//
// The production guard is two lines and they form a closure:
//
//     bool skip = !step_accepted;                          // (A) covers the not-accepted case
//     if (step_accepted || rhs_budget <= 0) skip = true;   // (B) covers the accepted case
//
// (A) and (B) between them cover both values of step_accepted, so `skip` is unconditionally true
// and neither the dK-magnitude refinement below it nor the line search itself can run. This is
// the "coarse guard preempts its refinement" shape: (A) was the original coarse skip, (B) was
// added later for a different reason, and together they close the door.
//
// The rule is extracted here -- and CONSUMED by the solver, so it cannot drift from what the
// fixtures pin -- deliberately WITHOUT repairing it. Making the line search reachable would put
// a globalization strategy back into a solver whose convergence behaviour is under measurement,
// which is a numerics change that has to be opt-in and measured, not a drive-by fix. What the
// fixtures pin is the FINDING: as shipped, `nk_line_search` has no reachable consumer, whatever
// the Registry sets it to. If someone deliberately reopens the path, the pins fail and say so.
inline bool line_search_skipped(bool step_accepted, int rhs_budget) {
    bool skip = !step_accepted;                           // (A)
    if (step_accepted || rhs_budget <= 0) skip = true;    // (B)
    return skip;
}

// R13.23 6.3: the boundary-flag projection, as a rule a fixture can reject.
//
// Two flag sets exist and conflating them is the whole hazard: `raw` is global-domain metadata
// as Fortran passed it, `effective` is that projected onto ONE rank. The projection is not
// uniform -- periodicity is global and passes through untouched, while a symmetric/open EDGE
// flag applies only on a rank that owns that edge. A rank in the interior of the decomposition
// has effective open_xs = false while raw open_xs = true, and code that reads the raw flag there
// would apply a physical boundary condition in the middle of the domain.
struct RankEdgeOwnership {
    bool on_west = true, on_east = true, on_south = true, on_north = true;
};

inline RankEdgeOwnership rank_edge_ownership(int nprocx, int nprocy, int mypx, int mypy) {
    RankEdgeOwnership e;
    e.on_west  = (nprocx <= 1) || (mypx == 0);
    e.on_east  = (nprocx <= 1) || (mypx == nprocx - 1);
    e.on_south = (nprocy <= 1) || (mypy == 0);
    e.on_north = (nprocy <= 1) || (mypy == nprocy - 1);
    return e;
}

inline bool effective_edge_flag(bool raw_flag, bool rank_owns_edge) {
    return raw_flag && rank_owns_edge;
}

inline bool effective_periodic_flag(bool raw_flag, const RankEdgeOwnership&) {
    return raw_flag;   // global-domain metadata: never masked by rank position
}

// R13.23 (deep review P0-1): may a total-failure signal be OVERRULED for this candidate?
//
// The signal is a statement about the LINEAR residual ratio. Using it to discard the step removes
// the candidate from the trust region entirely, so the mechanism whose job is to judge a step
// never sees it. The rule below decides whether to hand it to globalization instead -- and it is
// deliberately narrow:
//
//   * the decision is made in the norm the TRUST REGION minimises (||S^-1 R||), not the raw packed
//     L2. Those disagree: at dt=600 the discarded candidates improve the raw L2 by 12.5% and 60%
//     while the S-weighted merit gets worse by 2.5% and 46x. Deciding on the raw norm would have
//     rescued two steps the trust region would then have rejected;
//   * without the S norm there is no basis to overrule the signal, so the signal stands;
//   * a rescue is NOT an acceptance. It clears the veto so the ordinary globalization path can
//     accept or reject on its own terms.
// R13.25 (external review, section 7): the mask state belongs in the DECISION, not only in the
// telemetry. R13.24 recorded `s_halo_masked` in a log line while the admission contract carried
// only "was an S merit measured" -- so a merit that needed the halo mask and could not apply it
// was still handed to the decision, and read as the trust merit. The claim "a merit that could not
// apply the mask cannot be read as the trust merit" was true of the prose and not of the code.
enum class HaloMaskStatus {
    NotRequired,            // no mask in play; the two merits coincide
    Applied,                // masked exactly as trust acceptance masks
    RequiredButUnavailable, // a mask was initialised but unusable -- this merit is NOT comparable
};

struct CandidateArbitration {
    bool   s_merit_measured = false;
    double s_before = -1.0;
    double s_after = -1.0;
    HaloMaskStatus halo = HaloMaskStatus::NotRequired;
};

inline bool candidate_arbitration_rescues(const CandidateArbitration& a) {
    if (!a.s_merit_measured) return false;              // fail-closed
    // R13.25: a merit that needed the halo mask and could not apply it is not the trust merit, so
    // it cannot overrule a signal the trust merit would judge. Fail closed rather than compare
    // two different quantities.
    if (a.halo == HaloMaskStatus::RequiredButUnavailable) return false;
    if (!measured(a.s_before) || !measured(a.s_after)) return false;
    return a.s_after < a.s_before;                      // strict: a tie is not an improvement
}

// R13.23 (deep review P0-5): a Krylov return must hand back a solution and a residual that belong
// to the SAME solve. The NaN-retry path returned `x = 0` beside the current iterate's residual, so
// `r_true != b - A(x)` and one result carried two solves -- downstream that residual feeds trust
// prediction, per-block analysis and the exit receipt, none of which correspond to the x handed
// back.
//
// Stated over the two norms a caller can see, so a fixture can reject its negation. At `x = 0` the
// residual is `b`, hence ||r|| == ||b|| and rho == 1 exactly; any other pairing with a zero
// solution is inconsistent.
struct KrylovReturnPairing {
    bool   x_is_zero = false;
    double r_norm = -1.0;
    double b_norm = -1.0;
};

inline bool krylov_return_pairing_consistent(const KrylovReturnPairing& p) {
    if (!measured(p.r_norm) || !measured(p.b_norm)) return false;
    if (!p.x_is_zero) return true;          // only the zero-solution case is pinned here
    if (!(p.b_norm > 0.0)) return p.r_norm == 0.0;
    const double ratio = p.r_norm / p.b_norm;
    return ratio > 1.0 - 1.0e-6 && ratio < 1.0 + 1.0e-6;
}

// R13.23 (P0-4): the exit receipt as the classifier sees it, in one place so the attribution and
// the emitted `exit_receipt_complete` cannot judge different things.

inline KrylovReceiptView exit_receipt_view(const StageFailureSignals& s) {
    KrylovReceiptView v;
    v.rho_D_final = s.exit_rho_stop_final;
    v.rho_S_final = s.exit_rho_S_final;
    v.stopping_metric = static_cast<int>(s.exit_stopping_metric);
    v.arnoldi_spent = s.exit_arnoldi_spent;
    v.arnoldi_allowed = s.exit_arnoldi_allowed;
    v.tolerance_applied = s.exit_tolerance_applied;
    v.D_reached = s.exit_D_reached;
    v.S_reached = s.exit_S_reached;
    // R13.24 (external review P1-3): these were BOTH filled from `exit_krylov_iter`, so the
    // completeness rule's `receipt_iter != exit_iter` test was an identity in production -- it
    // could only ever fire in a fixture, which is the "verifying a state the producer cannot
    // emit" shape this repository has flagged before. `newton_exit_event_iter` is stamped
    // independently at the Newton exit site; when it was never stamped the comparison is skipped
    // rather than satisfied by construction.
    // R13.25 (external review, section 8): pass BOTH authorities through unchanged. R13.24 fell
    // back to `exit_krylov_iter` when the event stamp was missing, which does not "skip the
    // comparison" -- it copies one authority into the other so the comparison passes by
    // construction, restoring exactly the identity the split was made to remove. A missing stamp
    // must leave the receipt INCOMPLETE, which is what the completeness rule is for.
    v.receipt_iter = s.exit_krylov_iter;
    v.exit_iter = s.newton_exit_event_iter;
    return v;
}

inline StageFailure krylov_exit_attribution_of(const StageFailureSignals& s) {
    if (s.newton_termination != NewtonTerminationReason::ZeroUpdateAfterTotalFailure) {
        return StageFailure::None;
    }
    if (s.exit_krylov_iter < 0) return StageFailure::None;
    // R13.23 (deep review P0-4): THE RECEIPT MUST EARN THE ATTRIBUTION. `exit_receipt_complete`
    // was computed and EMITTED but never consulted here, so a row could read
    // `exit_receipt_complete=0` beside a specific `exit_attribution` derived from that same
    // receipt -- the producer/emitter/no-consumer shape this repository has fixed repeatedly.
    if (!krylov_receipt_complete(exit_receipt_view(s))) return StageFailure::None;
    // Minimised what it was asked to, and the result is still useless to the Newton merit.
    if (s.exit_D_reached && !s.exit_S_reached) {
        return StageFailure::KrylovObjectiveMismatch;
    }
    // Both solver metrics met and the ENTRY-weighted linear one not -- a real seam between the
    // metrics the solve was steered by. NOT the stage gate, so it may not be claimed while the
    // gate itself is recorded as having passed; `gate_metric_ok` is read so the two cannot
    // disagree.
    //
    // R13.20 (round 9, R9-10): OPT-IN ONLY, PROVEN rather than suspected. Reaching the
    // zero-update exit needs `gmres_total_failure`. Under the DEFAULT rule that is
    // `raw > 1 || rel >= 0.999` on rho_S, so it requires rho_S >= 0.999; and `exit_S_reached` is
    // `rho_S < tol` with `tol` clamped into [ew_eta_min, ew_eta_max] = [0.02, 0.9]. rho_S < 0.9
    // and rho_S >= 0.999 cannot both hold. Reachable only under the opt-in vs-r0 rule.
    if (s.exit_D_reached && s.exit_S_reached &&
        measured(s.exit_rho_E_final) && !s.exit_E_reached && !s.gate_metric_ok) {
        return StageFailure::KrylovEntryMetricMismatch;
    }
    return StageFailure::None;
}


// R13.21 (external review section 8): HOW CLOSE WAS THE CALL?
//
// The no-progress boundary is a DECISION BOUNDARY, not a mechanism classifier, and this campaign
// measured how sharp that is: on the stage-3 budget sweep `vs_r0` read 0.907 at one setting and
// 0.8993 at the next -- a 0.8 % difference in the ratio -- and the attribution flipped
// `krylov_stagnated` <-> `newton_stagnated`, taking the layer with it, while the primary event was
// stage-3 `zero_update_after_total_failure` in both. `attribution_basis` already exposes WHICH
// side of the boundary a row landed on. This says HOW FAR from it.
//
// A row inside the band is not wrong -- the classifier still answers -- but its specific layer is
// not a safe work order for another code layer, so `threshold_sensitive` gates that.
inline constexpr double kThresholdSensitivityBand = 0.02;

struct ThresholdProximity {
    double threshold = -1.0;   // the boundary actually applied
    double distance = 0.0;     // progress - threshold; signed, so the side is readable
    bool   sensitive = false;  // |distance| < band
    bool   measured_ = false;  // false when no r0 progress was measured at all
};

inline ThresholdProximity threshold_proximity(double progress, double threshold,
                                              double band = kThresholdSensitivityBand) {
    ThresholdProximity t;
    if (!measured(progress) || !(threshold > 0.0)) return t;   // measured_ stays false
    t.measured_ = true;
    t.threshold = threshold;
    t.distance = progress - threshold;
    t.sensitive = (t.distance < 0.0 ? -t.distance : t.distance) < band;
    return t;
}

// A specific layer is a work order. Do not issue one from a verdict that a 1 % change in the
// ratio would have reversed.
inline bool threshold_permits_specific_layer(const ThresholdProximity& t) {
    return !(t.measured_ && t.sensitive);
}

// R13.20 (round 9, R9-2): WHICH BODY OF EVIDENCE the returned category came from.
//
// `attribution_from_metric` used to be inferred AFTER the fact, from `measured(worst_..._vs_r0)
// || measured(best_..._vs_r0) || krylov_solves_measured_vs_r0 > 0` -- a test for "does an r0
// reading exist anywhere on the record", not for "did an r0 reading decide this". The two come
// apart in exactly the case the field was written for: when r0 IS measured and the worst ratio is
// BELOW the no-progress threshold, the classifier falls through to the aggregate reconstruction
// (`residual_first`/`residual_last`, iteration counts) -- and the flag read 1, because the branch
// it fell through was entered on `measured(worst_..._vs_r0)`. It was 1 by construction in the one
// case its own comment defines as 0.
//
// So the deciding clause now SAYS which evidence it used. Set at the top of each region that is
// guaranteed to return, rather than at all ~25 return sites, to keep the classifier readable --
// and never inferred from the signals afterwards, which is what went wrong.
enum class StageDecisionBasis {
    NotRecorded,
    Precondition,             // provenance / finiteness / absent measurement
    ExplicitStageGate,        // the explicit stage's own RHS / admissibility / publish checks
    NewtonResidualTrace,      // residual_first vs residual_last divergence growth
    StepAcceptance,           // accepted/rejected step counters
    KrylovDivergedFlag,       // the solver's own divergence flag
    ExitReceipt,              // exit_* -- the solve that ENDED the loop
    KrylovR0Receipt,          // the four-way: worst_krylov_* against r0-relative progress
    LegacyKrylovAggregate,    // gmres_total_failures / best_krylov_rel_error, ||b|| coordinate
    NewtonExitReason,         // s.newton_termination
    AggregateReconstruction,  // residual trend + iteration counts; no Krylov receipt read
    Postcondition             // gate / publish / converged
};

inline const char* stage_decision_basis_name(StageDecisionBasis b) {
    switch (b) {
        case StageDecisionBasis::NotRecorded:            return "not_recorded";
        case StageDecisionBasis::Precondition:           return "precondition";
        case StageDecisionBasis::ExplicitStageGate:      return "explicit_stage_gate";
        case StageDecisionBasis::NewtonResidualTrace:    return "newton_residual_trace";
        case StageDecisionBasis::StepAcceptance:         return "step_acceptance";
        case StageDecisionBasis::KrylovDivergedFlag:     return "krylov_diverged_flag";
        case StageDecisionBasis::ExitReceipt:            return "exit_receipt";
        case StageDecisionBasis::KrylovR0Receipt:        return "krylov_r0_receipt";
        case StageDecisionBasis::LegacyKrylovAggregate:  return "legacy_krylov_aggregate";
        case StageDecisionBasis::NewtonExitReason:       return "newton_exit_reason";
        case StageDecisionBasis::AggregateReconstruction:return "aggregate_reconstruction";
        case StageDecisionBasis::Postcondition:          return "postcondition";
    }
    return "not_recorded";
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
    // R13.20 (round 9, R9-2): the evidence class the deciding clause actually used, reported by
    // that clause rather than inferred from the signals afterwards. `attribution_from_metric` is
    // now a reading of `attribution_basis`, not an independent guess at it.
    StageDecisionBasis primary_event_basis = StageDecisionBasis::NotRecorded;
    // R13.21 (external review P0-2): what the EXIT solve's receipt says about why the loop ended,
    // kept beside the event instead of replacing it. `None` = the receipt cannot support a subtype.
    StageFailure exit_attribution = StageFailure::None;
    // R13.21 (external review section 8): how close the r0 progress was to the boundary that
    // decided the attribution.
    ThresholdProximity threshold = {};
    StageDecisionBasis attribution_basis   = StageDecisionBasis::NotRecorded;
    // R13.19 SELF-REVIEW (round 8, P0-C): WHICH solve's receipt decided the category. The
    // emitter derived `specific_layer` from the EXIT solve's source/metric while the four-way
    // clauses can be reached from the STAGE-WORST receipt -- a layer describing one solve
    // annotating a category decided by another. `worst_krylov_tolerance_source` was produced,
    // plumbed and printed, and read by nothing.
    bool decided_by_exit_receipt = false;
};

inline StageFailure first_failure_of(const StageFailureSignals& s,
                                     StageDecisionBasis* basis = nullptr) {
    // The default for every path below; each region that returns overwrites it.
    if (basis) *basis = StageDecisionBasis::Precondition;
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
        // Every path out of this block returns, and none of them is a precondition on the
        // implicit machinery -- they are the explicit stage's own gates.
        //
        // R13.20 (adversarial loop, iteration 2) -- REACHABILITY, stated rather than implied.
        // Until R13.20 `explicit_rhs_measured` had NO producer in production (only fixtures wrote
        // it), so this block returned `InsufficientEvidence` on its first line every time, and
        // the explicit stage was never gated at all: `last_stage_converged_` was hard-set true
        // and all three gate metrics hard-set 0, so `handle_stage_gate` early-returned. A
        // non-finite explicit tendency reached the NEXT stage and was reported there as
        // `EntryStateNotFinite` -- one stage late, wrong stage.
        //
        // Both are fixed, and from the single production call site (inside `handle_stage_gate`)
        // the reachable set is now exactly:
        //   InsufficientEvidence   -- k_fast undefined/empty
        //   ExplicitRhsNotFinite   -- measured and non-finite; the gate now fires on it
        // and these two are NOT reachable from that call site, by arithmetic:
        //   ExplicitAdmissibilityRejected -- needs a FINITE tendency with a bad gate metric, but
        //       a finite explicit tendency sets all three metrics to 0, so the gate early-returns
        //   ExplicitPublishRejected / None -- the call site sets `state_published = false`
        //       unconditionally ("the gate is upstream of any publish"), and it is only entered
        //       when the gate is already unhappy
        // They are kept for a publish-site classifier call that does not exist yet. Do not read
        // them as live classifications of anything.
        if (basis) *basis = StageDecisionBasis::ExplicitStageGate;
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
        if (basis) *basis = StageDecisionBasis::NewtonResidualTrace;
        if (!measured(s.residual_last)) return StageFailure::NewtonDiverged;
        if (measured(s.residual_first) && s.residual_first > 0.0 &&
            s.residual_last > kDivergenceGrowth * s.residual_first) {
            return StageFailure::NewtonDiverged;
        }
        if (basis) *basis = StageDecisionBasis::Precondition;
        // The linear solve before the outer one: Newton cannot converge on top of a solve
        // that does not solve, so this is upstream of any statement about the iteration.
        // Time order, where the record has it. A rejection that happened BEFORE the first
        // Krylov failure is upstream of it (it shrank the radius and changed the next solve's
        // x0 and budget), and is reported first.
        const bool rejection_first =
            (s.first_rejection_iter >= 0 && s.first_krylov_failure_iter >= 0 &&
             s.first_rejection_iter < s.first_krylov_failure_iter);
        if (rejection_first && s.accepted_steps == 0) {
            if (basis) *basis = StageDecisionBasis::StepAcceptance;
            return StageFailure::AllStepsRejected;
        }
        if (s.krylov_diverged) {
            if (basis) *basis = StageDecisionBasis::KrylovDivergedFlag;
            return StageFailure::KrylovDiverged;
        }
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
            // Every path out of this block returns, and all three read `exit_*`.
            if (basis) *basis = StageDecisionBasis::ExitReceipt;
            // R13.21 (external review P0-2): THE EVENT, ALWAYS. This branch used to return the
            // exit solve's metric SUBTYPE here -- `KrylovObjectiveMismatch` or
            // `KrylovEntryMetricMismatch` -- so a field documented as "what actually ended the
            // stage" was overwritten by a CAUSE, and the fixtures pinned that as the contract.
            // R13.19 added `attribution` and R13.20 added `event_basis` on the way to separating
            // the two questions; this closes it. The subtype is preserved as
            // `StageDiagnosis::exit_attribution`, computed by `krylov_exit_attribution_of` below,
            // so nothing is lost -- it simply stops impersonating the event.
            return StageFailure::ZeroUpdateAfterTotalFailure;
        }
        // ...and an exception in the linear solve is not the outer iteration's failure either.
        if (s.newton_termination == NewtonTerminationReason::Exception) {
            if (basis) *basis = StageDecisionBasis::NewtonExitReason;
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
                // Every path out of this block returns, and all of them read the stage-worst
                // r0 receipt. This is the ONLY region that may be reported as r0-decided.
                if (basis) *basis = StageDecisionBasis::KrylovR0Receipt;
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
                // R13.20 (self-review while fixing round 9's R9-6): a
                // `!s.all_near_worst_met_tolerance -> KrylovStagnated` refusal stood here and
                // made TWO of the four answers below unreachable in production.
                //
                // `near_worst_all_met` compares `worst_unmet` against `worst`. When the strictly
                // -worst solve is itself an unmet one those are the SAME number, so the predicate
                // is false by construction -- and `worst_krylov_met_tolerance` belongs to that
                // same strictly-worst solve. Reaching the clauses below therefore implied
                // `worst_krylov_met_tolerance == true`, which `S_reached || met_tolerance` always
                // accepted. `KrylovBudgetExhausted` and the trailing `KrylovStagnated` had NO
                // producer. The fixture that pinned `krylov_budget_exhausted` reached it from
                // `all_near_worst_met_tolerance = true` (the struct default) with
                // `worst_krylov_met_tolerance = false` -- a pair the solver cannot emit, so the
                // category read as covered while never firing.
                //
                // What the refusal was FOR is tie DISAGREEMENT, and `near_worst_mechanism_ambiguous`
                // -- added later, order-independent, and comparing exactly the fields the answer
                // depends on -- does that and only that. The older approximation is retired from
                // the classifier and stays on the record as telemetry.
                //
                // R13.19 SELF-REVIEW: solves tied at the worst ratio that hit DIFFERENT mechanisms
                // cannot name one. Reporting the first arrival's was order-dependent; reporting
                // the general category is not.
                if (s.near_worst_mechanism_ambiguous) {
                    return StageFailure::KrylovStagnated;
                }
                KrylovSolveMechanism worst_receipt;
                worst_receipt.met_tolerance    = s.worst_krylov_met_tolerance;
                worst_receipt.D_reached        = s.worst_krylov_D_reached;
                worst_receipt.S_reached        = s.worst_krylov_S_reached;
                worst_receipt.budget_exhausted = s.worst_krylov_budget_exhausted;
                return krylov_mechanism_category(worst_receipt);
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
            if (basis) *basis = StageDecisionBasis::LegacyKrylovAggregate;
            if (s.gmres_total_failures > 0) return StageFailure::KrylovStagnated;
            if (measured(s.best_krylov_rel_error) &&
                s.best_krylov_rel_error >= kKrylovNoProgress) {
                return StageFailure::KrylovStagnated;
            }
        }
        // The linear solve worked and nothing it proposed was taken.
        if (s.accepted_steps == 0 && s.rejected_steps > 0) {
            if (basis) *basis = StageDecisionBasis::StepAcceptance;
            return StageFailure::AllStepsRejected;
        }
        // R13.17 (external review P0-3): when the loop RECORDED why it stopped, that is the
        // answer -- not a precedence over aggregates. The reconstruction below cannot separate
        // "the residual stopped moving" from "the budget ran out at a flat residual", and the
        // campaign's reading of the 12x-budget run ("the failure moved outward") rests on exactly
        // that distinction.
        if (basis) *basis = StageDecisionBasis::NewtonExitReason;
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
        if (basis) *basis = StageDecisionBasis::AggregateReconstruction;
        if (still_falling && s.newton_iteration_budget > 0 &&
            s.newton_iterations >= s.newton_iteration_budget) {
            return StageFailure::NewtonBudgetExhausted;
        }
        return StageFailure::NewtonStagnated;
    }

    // R13.20 (adversarial loop, iteration 3): the SAME reachability statement as the explicit
    // branch above, which iteration 2 wrote for the explicit half and left off this one.
    // Reaching here means `newton_converged` was true, and the single production call site
    // (`handle_stage_gate`) is entered only when `stage_failed || gate_metric_bad` -- so a
    // converged stage that got here necessarily has `gate_metric_ok == false` and answers
    // `AdmissibilityRejected`. `PublishRejected` and `None` are therefore NOT reachable from
    // that site: it sets `state_published = false` unconditionally because it is upstream of any
    // publish. Both are kept for a publish-site classifier call that does not exist yet, and the
    // contract test marks their fixtures RESERVED so a precedence pin is not read as coverage.
    if (basis) *basis = StageDecisionBasis::Postcondition;
    if (!s.gate_metric_ok)   return StageFailure::AdmissibilityRejected;
    if (!s.state_published)  return StageFailure::PublishRejected;
    return StageFailure::None;
}

// The metric evidence on its own, with the recorded exit event REMOVED. Running the same
// classifier over a copy whose termination is NotRecorded is deliberate: it reuses one set of
// clauses instead of duplicating them, so the two answers cannot drift apart.
inline StageDiagnosis stage_diagnosis_of(const StageFailureSignals& s) {
    StageDiagnosis d;
    d.primary_event = first_failure_of(s, &d.primary_event_basis);
    StageFailureSignals metric_only = s;
    metric_only.newton_termination = NewtonTerminationReason::NotRecorded;
    d.attribution = first_failure_of(metric_only, &d.attribution_basis);
    // The weak flag: the attribution is not itself an absence-of-evidence verdict.
    d.attribution_measured =
        (d.attribution != StageFailure::InsufficientEvidence) &&
        (d.attribution != StageFailure::StageSignalMissing);
    // The one that matters: does the attribution rest on a real r0/Krylov reading, or on the
    // aggregate fallback the campaign spent four rounds moving away from? Without this a pure
    // reconstruction naming an operator layer would read as "measured".
    //
    // R13.20 (round 9, R9-2): read from the clause that returned. The r0 four-way is the only
    // region that reads the stage-worst r0 receipt, so it is the only basis that answers yes.
    // `LegacyKrylovAggregate` is deliberately NOT included -- it is a Krylov reading, but in the
    // ||b|| coordinate this line of work exists to stop trusting, and it is what the field's
    // comment calls "the aggregate fallback". `metric_only` clears the termination, so
    // `ExitReceipt` is unreachable here by construction; it is listed for the reader, not as a
    // live arm.
    d.attribution_from_metric =
        d.attribution_measured &&
        d.attribution_basis == StageDecisionBasis::KrylovR0Receipt;
    // R13.20 (round 9, R9-3): likewise derived from the basis the deciding clause reported,
    // instead of re-testing the signals the clause read. The previous form re-derived the
    // condition and could not be true for `KrylovForcingTermLimited`, leaving the emitter's
    // `exit_tolerance_source` arm selectable by no input.
    d.decided_by_exit_receipt =
        (d.primary_event_basis == StageDecisionBasis::ExitReceipt);
    // R13.21 (P0-2): the subtype the zero-update branch used to return as the event.
    d.exit_attribution = krylov_exit_attribution_of(s);
    // R13.21 (section 8): the same threshold resolution the classifier applies, so the record
    // cannot report a distance from a boundary other than the one that was used.
    {
        const double applied =
            (s.krylov_threshold_observed && s.krylov_no_progress_threshold > 0.0 &&
             s.krylov_no_progress_threshold <= 1.0)
                ? s.krylov_no_progress_threshold : kKrylovNoProgressVsR0;
        d.threshold = threshold_proximity(s.worst_krylov_rel_error_vs_r0, applied);
    }
    return d;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_FIRST_FAILURE_H
