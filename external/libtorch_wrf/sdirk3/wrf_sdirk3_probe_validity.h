#ifndef WRF_SDIRK3_PROBE_VALIDITY_H
#define WRF_SDIRK3_PROBE_VALIDITY_H

// When a diagnostic record is ENTITLED to carry a verdict.
//
// This campaign's recurring failure has not been wrong arithmetic -- it has been correct
// arithmetic reported as an answer to a question its preconditions did not permit. A severed
// VJP passed symmetry, linearity, repeatability and additivity while being the identity. A
// one-step tangent converged to machine precision across four arms that had all returned their
// input. A per-block epsilon scaled by a zero norm printed "never measured" as "perfect
// agreement". In each case the number was right and the VERDICT was not earned.
//
// The rules live here, as free functions over plain data, for one reason: a rule spelled out
// inline at the emit site cannot be tested, and every one of the above was spelled out inline.
// A rule that a test can reject is the only kind that stays true.

#include <limits>
#include <vector>

#include "wrf_sdirk3_tile_unified.h"

namespace wrf {
namespace sdirk3 {

struct ProbeVerdict {
    bool valid = false;
    // Stable machine-greppable token, not prose: the record is parsed by gates.
    const char* reason = "";
};

// ---------------------------------------------------------------------------------------
// Step-map probes (purity / advance / tangent)
// ---------------------------------------------------------------------------------------
//
// Only StepStatus::Complete corresponds to a published state transition. Anything else means
// the arm returned its input, and a quotient over returned inputs is the identity regardless of
// the dynamics -- so agreement between such arms measures the fail-closed rollback, not Phi_h.
//
// An EMPTY arm list is not valid either. "No arm ran" and "every arm succeeded" are the two
// readings of an all-Complete predicate over an empty set, and the useful one is the first.
inline ProbeVerdict step_map_verdict(const std::vector<StepStatus>& arms) {
    if (arms.empty()) {
        return {false, "no_arm_ran"};
    }
    for (StepStatus s : arms) {
        if (s != StepStatus::Complete) {
            return {false, "noncomplete_arm"};
        }
    }
    return {true, "ok"};
}

// ---------------------------------------------------------------------------------------
// Tangent probes (JVP spectra, directional derivatives, Taylor remainders)
// ---------------------------------------------------------------------------------------
//
// WHICH function was linearized is not a detail to recover from a config echo later; it changes
// what the number means. Three different operators are in this tree at once:
enum class TangentSemantics {
    // dF/dU of the function the forward actually integrates. The only semantics an exact
    // 4D-Var gradient may rest on.
    ExactPrimal,
    // The graph as production builds it, with imex_slow_in_tangent=false detaching the slow
    // channel. The forward integrates F_I + F_E; this differentiates approximately J_I. That is
    // not an approximation to the primal derivative, it is a different model -- admissible only
    // as a weak-constraint formulation whose dropped component is carried in an explicit Q.
    OperationalDetachedSlow,
    // A probe mapping with the reference state FROZEN at U_0 rather than following the
    // evaluation point. Useful for isolating dF/dU from dF/dU_ref; never the production map.
    DiagnosticFrozenReference
};

inline const char* tangent_semantics_name(TangentSemantics s) {
    switch (s) {
        case TangentSemantics::ExactPrimal:             return "exact_primal";
        case TangentSemantics::OperationalDetachedSlow: return "operational_detached_slow";
        default:                                        return "diagnostic_frozen_reference";
    }
}

struct TangentInputs {
    // The forward-mode dual came back undefined or F threw, so what was returned is a finite
    // difference. An FD quotient at float32 is noise-limited well above the tolerances these
    // probes report, and a spectrum computed on it is not the operator's spectrum.
    bool fd_fallback = false;
    // Step-level probes only: every arm advanced. Ignored (left true) by RHS-level probes.
    bool arms_complete = true;
    // The linearization point the probe used is the one production would have used.
    bool reference_matches = true;
    // Single rank AND this tile covers the whole patch. A tile-local operator is not the
    // global operator, and its spectrum is not comparable to anything.
    bool topology_ok = true;
};

// Deterministic order, so two runs disqualified for two reasons report the SAME one and a
// diff of their records is stable.
inline ProbeVerdict tangent_verdict(const TangentInputs& in) {
    if (!in.topology_ok)       return {false, "tile_local_operator"};
    if (!in.arms_complete)     return {false, "noncomplete_arm"};
    if (!in.reference_matches) return {false, "reference_mismatch"};
    if (in.fd_fallback)        return {false, "fd_fallback"};
    return {true, "ok"};
}

// The RELATION a tangent record may claim, decided by the verdict and the number TOGETHER.
//
// R13's three defects were one defect: a rule was computed and its consumer read something
// else. Here that had a specific and inverted consequence. The sentence was selected by
// `e_drop > 0`, and under an FD fallback FD cannot see a detach -- it returns the primal
// tangent, so e_drop ~ 0 and the record asserted "the operational tangent IS the primal
// derivative" from the one measurement incapable of supporting it. `e_drop = -1` (never
// measured) landed on the same sentence.
//
// So the verdict and the number are consumed in one function, and a test can reject the
// PAIR. A rule whose consumer is a separate expression is a rule nothing enforces.
enum class TangentRelation { Unavailable, MatchesPrimal, DiffersFromPrimal };

inline const char* tangent_relation_name(TangentRelation r) {
    switch (r) {
        case TangentRelation::MatchesPrimal:     return "matches_primal";
        case TangentRelation::DiffersFromPrimal: return "differs_from_primal";
        default:                                 return "unavailable";
    }
}

inline TangentRelation tangent_relation(const ProbeVerdict& verdict, double e_drop,
                                        double tol = 1.0e-8) {
    if (!verdict.valid) return TangentRelation::Unavailable;
    // NaN, +/-Inf and the -1 sentinel are all "not measured", and none of them is evidence
    // of agreement -- which is the reading `e_drop > 0` gave every one of them.
    if (!(e_drop == e_drop) || e_drop < 0.0 ||
        e_drop >= std::numeric_limits<double>::infinity()) {
        return TangentRelation::Unavailable;
    }
    return e_drop <= tol ? TangentRelation::MatchesPrimal
                         : TangentRelation::DiffersFromPrimal;
}

// ---------------------------------------------------------------------------------------
// Stage-reference certification
// ---------------------------------------------------------------------------------------
//
// A reference is an ASSUMPTION until it certifies itself, and R12 R4 is the worked example of
// why. Its first reading took a 120x20 solve as the truth and reported the shipped solve's
// error against it as an accuracy. The second arm then returned ref_agree=0 with a WORSE
// residual -- the signature of a warm start, not of a converged reference. With the warm start
// off, the two arms disagreed by 0.284 while the quantity they were measuring was 0.737: a
// reference 2.6x from its own sibling is not a reference, and rel_err was never an accuracy.
//
// Three arms, and the rule below. Two arms can only say whether they agree; three can say
// whether the sequence is CONVERGING, which is the question. The criteria are conjunctive
// because each rejects a different way of being wrong:
//   - all three converged      -- an arm the solver itself failed cannot anchor anything
//   - the residual falls       -- a tighter budget that does not reduce the residual is not
//                                 approaching a solution, it is wandering
//   - the states approach      -- the increments must be settling, not merely agreeing once
//   - and by a clear margin    -- a reference must be far closer to the truth than the
//                                 quantity it measures, or their difference is two errors
struct StageReferenceArms {
    // Indexed 0,1,2 = loosest..tightest. An array rather than three named members because
    // every criterion below is a statement about the SEQUENCE, and three scalars invite a
    // clause that silently reads only one of them -- which is how R13 came to print arm 1's
    // gap under the name K_ref while running three arms to obtain arm 3.
    bool   converged[3] = {false, false, false};
    // R13.1: isolation belongs IN the predicate. Three arms sharing a live solver's hopeless
    // streaks, trust radius and warm-start slots can agree numerically because they are three
    // points on one trajectory. R12 R4 is that exact case: ref_agree=0 was agreement between
    // a solve and its own warm start.
    bool   isolated[3] = {false, false, false};
    bool   fresh_solver_per_arm = false;
    double residual[3] = {-1.0, -1.0, -1.0};
    // ||Y3 - Y2|| / ||Y3||, and ||Y2 - Y1|| / ||Y2||: how far the sequence still moves.
    double state_gap_32 = -1.0, state_gap_21 = -1.0;
    // The same for F_E. Stage increments can agree while the explicit RHS they produce does
    // not, and it is F_E(Y_s) that forces the next stage -- so a reference certified on the
    // increment alone certifies the wrong quantity. R13's completion table required this and
    // its predicate had no field for it.
    double explicit_gap_32 = -1.0, explicit_gap_21 = -1.0;
    // ||Y_shipped - Y3|| / ||Y3||: the quantity the reference is being asked to measure.
    double shipped_gap = -1.0;
};

struct StageReferenceVerdict {
    bool certified = false;
    const char* reason = "";
};

struct StageReferenceTolerances {
    // A gap this small is converged outright, whatever the ratio does. Without an absolute
    // arm, 0.500 -> 0.499 -> 0.498 satisfies every contraction test ever written.
    double residual_abs = 1.0e-6;
    double state_abs    = 1.0e-6;
    double explicit_abs = 1.0e-6;
    // ...or it must be CONTRACTING at this rate. Either arm suffices; both being required
    // would reject a sequence that simply arrived.
    double contraction  = 0.5;
    // How much closer to the truth the reference must be than the thing it measures.
    double margin       = 10.0;
};

// A finite, non-negative number. Rejects NaN and +/-Inf, and admits an exact zero -- which
// R13's `residual > 0` did not, so a perfectly converged arm reported "residual_unavailable".
inline bool is_measured(double v) {
    return v == v && v > -std::numeric_limits<double>::infinity() &&
           v < std::numeric_limits<double>::infinity() && v >= 0.0;
}

// Converged, or contracting. Returns false if either value is not a measurement.
inline bool settled(double now, double before, double abs_tol, double contraction) {
    if (!is_measured(now) || !is_measured(before)) return false;
    if (now <= abs_tol) return true;                 // arrived
    if (before <= 0.0) return false;                 // cannot contract from zero to nonzero
    return now <= contraction * before;              // still shrinking, and fast enough
}

inline StageReferenceVerdict certify_stage_reference(
        const StageReferenceArms& a,
        const StageReferenceTolerances& tol = StageReferenceTolerances{}) {
    for (int i = 0; i < 3; ++i) {
        if (!a.converged[i]) return {false, "arm_not_converged"};
    }
    // Independence before numbers. Arms that shared state can agree for reasons that have
    // nothing to do with either being right.
    for (int i = 0; i < 3; ++i) {
        if (!a.isolated[i]) return {false, "arm_not_isolated"};
    }
    // R13.5: and the field is READ. R13.1 added fresh_solver_per_arm to this struct, the
    // caller sets it false (snapshot/restore is not a fresh solver), and the predicate never
    // looked at it -- so arms sharing a preconditioner and its caches could still certify.
    // That is the fifth time in this tree that a rule was computed and its consumer read
    // something else, and the first where both halves were written in one commit.
    if (!a.fresh_solver_per_arm) return {false, "not_fresh_solver_per_arm"};
    for (int i = 0; i < 3; ++i) {
        if (!is_measured(a.residual[i])) return {false, "nonfinite_residual"};
    }
    // Non-increasing, AND actually getting somewhere. Monotonicity alone admits
    // 1.000 -> 0.999 -> 0.998, which is not a converging sequence by any reading.
    if (!(a.residual[2] <= a.residual[1] && a.residual[1] <= a.residual[0])) {
        return {false, "residual_not_decreasing"};
    }
    if (!settled(a.residual[2], a.residual[1], tol.residual_abs, tol.contraction)) {
        return {false, "residual_not_settled"};
    }
    if (!is_measured(a.state_gap_32) || !is_measured(a.state_gap_21)) {
        return {false, "state_gap_unavailable"};
    }
    if (!settled(a.state_gap_32, a.state_gap_21, tol.state_abs, tol.contraction)) {
        return {false, "state_gap_not_settled"};
    }
    if (!is_measured(a.explicit_gap_32) || !is_measured(a.explicit_gap_21)) {
        return {false, "explicit_gap_unavailable"};
    }
    if (!settled(a.explicit_gap_32, a.explicit_gap_21, tol.explicit_abs, tol.contraction)) {
        return {false, "explicit_gap_not_settled"};
    }
    if (!is_measured(a.shipped_gap)) {
        return {false, "shipped_gap_unavailable"};
    }
    // The R12 R4 failure, as a rule: 0.284 against 0.737 is 2.6x, and 2.6x is two unconverged
    // solves being differenced.
    if (!(a.state_gap_32 * tol.margin <= a.shipped_gap)) {
        return {false, "insufficient_margin"};
    }
    return {true, "ok"};
}

// ---------------------------------------------------------------------------------------
// When an A/B comparison may be attributed to the variable it names
// ---------------------------------------------------------------------------------------
//
// WHY THIS EXISTS. R13.4 compared the preconditioner on against off by flipping one
// environment variable and called it single-variable. Production branches on that variable
// into a DIFFERENT KRYLOV IMPLEMENTATION (`gmres_M_inv ? solve_fgmres : solve_gmres`), and
// the two arms additionally ran different Newton counts, so the metric being compared
// minimised over different linear systems. The conclusion drawn from it was retracted.
//
// One environment variable is not one variable. What makes a comparison attributable is that
// everything the two arms shared is DEMONSTRATED to be shared -- inputs by digest, code path
// by construction, budget by equality, and the stopping rule by both arms terminating for the
// same reason. The last one is the easiest to forget and the most common way a "same budget"
// comparison silently becomes two different amounts of work.
struct AbComparison {
    bool same_operator = false;      // A digests agree
    bool same_rhs = false;           // b digests agree
    bool same_x0 = false;            // x0 digests agree
    bool same_solver_path = false;   // the SAME implementation, not an equivalent one
    bool same_budget = false;        // equal j / equal Arnoldi steps allowed
    bool early_stop_disabled = false;
    int  termination_a = -1;         // both arms must stop for the same reason, or "equal j"
    int  termination_b = -1;         // was not equal work
};

inline ProbeVerdict ab_attributable(const AbComparison& c) {
    if (!c.same_operator)      return {false, "different_operator"};
    if (!c.same_rhs)           return {false, "different_rhs"};
    if (!c.same_x0)            return {false, "different_x0"};
    // The one that caught R13.4. An equivalent algorithm is not the same implementation:
    // early-stop, restart, residual recomputation and breakdown handling are free to differ.
    if (!c.same_solver_path)   return {false, "different_solver_path"};
    if (!c.same_budget)        return {false, "different_budget"};
    if (!c.early_stop_disabled) return {false, "early_stop_enabled"};
    if (c.termination_a != c.termination_b) return {false, "different_termination"};
    return {true, "ok"};
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_PROBE_VALIDITY_H
