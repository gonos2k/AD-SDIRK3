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
    KrylovStagnated,
    // The linear solve worked and every step it proposed was rejected. That is a trust-region
    // or line-search policy statement, not a numerical one.
    AllStepsRejected,
    // Newton neither diverged nor stalled for either reason above -- it simply ran out of
    // iterations with the residual roughly flat. The residual has a floor.
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
        case StageFailure::EntryStateNotFinite:      return "entry_state_not_finite";
        case StageFailure::InitialResidualNotFinite: return "initial_residual_not_finite";
        case StageFailure::NewtonDiverged:           return "newton_diverged";
        case StageFailure::KrylovStagnated:          return "krylov_stagnated";
        case StageFailure::AllStepsRejected:         return "all_steps_rejected";
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
        case StageFailure::EntryStateNotFinite:      return "state_eos_metric_boundary";
        case StageFailure::InitialResidualNotFinite: return "rhs_operator";
        case StageFailure::NewtonDiverged:           return "linearization_or_timestep";
        case StageFailure::KrylovStagnated:          return "preconditioner_or_operator";
        case StageFailure::AllStepsRejected:         return "trust_region_policy";
        case StageFailure::NewtonStagnated:          return "residual_floor_or_split";
        case StageFailure::AdmissibilityRejected:    return "gate_threshold";
        default:                                     return "publish_gate";
    }
}

struct StageFailureSignals {
    // Measured at stage entry, before anything is solved.
    bool entry_state_finite = true;
    bool initial_residual_finite = true;
    // Newton's residual at its first and last iteration. -1 = not measured.
    double residual_first = -1.0;
    double residual_last = -1.0;
    int    newton_iterations = 0;
    bool   newton_converged = false;
    // The BEST relative error any GMRES call reached in this stage. 1.0 means it ended where
    // it started. -1 = not measured.
    double best_krylov_rel_error = -1.0;
    int    krylov_iterations = 0;
    int    gmres_total_failures = 0;
    // Trust-region / line-search accounting.
    int    accepted_steps = 0;
    int    rejected_steps = 0;
    // The stage gate's own verdict, and whether the step reached the driver.
    bool   gate_metric_ok = false;
    bool   state_published = false;
};

// How much the Newton residual must grow before "diverged" is the honest word. Below this a
// residual that ends slightly above where it started is stagnation with noise, not divergence.
inline constexpr double kDivergenceGrowth = 2.0;
// A relative error this close to 1 means the linear solve ended where it began.
inline constexpr double kKrylovNoProgress = 0.99;

inline bool measured(double v) {
    return v == v && v >= 0.0 && v < std::numeric_limits<double>::infinity();
}

// FIRST in causal order, not worst. A run whose entry state is already non-finite will also
// show a stagnating Krylov solve and a rejected step, and reporting either of those sends the
// next week of work to the wrong layer.
inline StageFailure first_failure_of(const StageFailureSignals& s) {
    if (!s.entry_state_finite)      return StageFailure::EntryStateNotFinite;
    if (!s.initial_residual_finite) return StageFailure::InitialResidualNotFinite;

    if (!s.newton_converged) {
        // A non-finite final residual is divergence that overflowed, not a missing
        // measurement -- and `measured()` rejects it, so it is checked first.
        if (!measured(s.residual_last)) return StageFailure::NewtonDiverged;
        if (measured(s.residual_first) && s.residual_first > 0.0 &&
            s.residual_last > kDivergenceGrowth * s.residual_first) {
            return StageFailure::NewtonDiverged;
        }
        // The linear solve before the outer one: Newton cannot converge on top of a solve
        // that does not solve, so this is upstream of any statement about the iteration.
        if (s.gmres_total_failures > 0) return StageFailure::KrylovStagnated;
        if (measured(s.best_krylov_rel_error) &&
            s.best_krylov_rel_error >= kKrylovNoProgress) {
            return StageFailure::KrylovStagnated;
        }
        // The linear solve worked and nothing it proposed was taken.
        if (s.accepted_steps == 0 && s.rejected_steps > 0) {
            return StageFailure::AllStepsRejected;
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
