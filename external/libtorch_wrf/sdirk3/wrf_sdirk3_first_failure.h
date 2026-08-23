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

#include <limits>

namespace wrf {
namespace sdirk3 {

enum class StageFailure {
    None = 0,
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

struct StageFailureSignals {
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
inline StageFailure first_failure_of(const StageFailureSignals& s) {
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
        if (measured(s.worst_krylov_rel_error_vs_r0)) {
            if (s.worst_krylov_rel_error_vs_r0 >= kKrylovNoProgressVsR0) {
                return StageFailure::KrylovStagnated;
            }
            // Every solve moved. Whatever refused the step is downstream, and the later
            // clauses name it.
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

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_FIRST_FAILURE_H
