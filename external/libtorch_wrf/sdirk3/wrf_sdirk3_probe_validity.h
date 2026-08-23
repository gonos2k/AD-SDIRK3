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

#include <cmath>
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
// The Taylor-defect probe: tau = ||G(K+s) - G(K) - A s|| / ||A s||, plus the same at alpha=1/2,
// used to separate "the inner solve binds" (tau << 1) from "the nonlinearity over the step"
// (tau = O(1), ratio ~ 1/2) from "the Jacobian is wrong" (tau = O(1), ratio ~ 1).
//
// It has TWO preconditions that the arithmetic cannot see:
//  1. A must be the real Jacobian-vector product. On the FD fallback path A is a difference
//     quotient with a state-dependent, ||v||-dependent epsilon -- not linear in its argument,
//     and at float32 noise-limited at roughly the magnitude tau itself reports. "tau = 0.018,
//     so the linearization is faithful to 2%" and "tau = 0.018, so we measured the FD noise
//     floor" are then the same record.
//  2. The alpha arm must MEASURE A(alpha*s), not assume alpha*A(s). Assuming it makes the
//     ratio partly true by construction (only the numerator is re-measured) and silently
//     imports precondition 1 a second time.
//  3. alpha must NOT be a power of two. This one is subtle and cost a P0. A forward-AD tangent
//     is a float expression whose every term is (primal) x (tangent); scaling the input tangent
//     by a dyadic factor scales every intermediate by that factor with IDENTICAL significands,
//     so under IEEE-754 A(s/2) == 0.5*A(s) BIT FOR BIT, for any operator. The linearity receipt
//     is then identically zero and measures the exponent arithmetic, not the Jacobian. The FD
//     path cancels the same way: halving ||v|| exactly doubles its epsilon exactly, so the
//     perturbed vector is the SAME vector. A receipt that cannot fail is not a receipt --
//     measured with alpha = 1/3 the same probe reports 1.7e-07, about 1.5 float32 ulps.
// A probe that prints a three-way causal conclusion in its own row needs all three on record.
struct TaylorDefectInputs {
    bool   fd_fallback_free = false;   // the JVP was forward-mode for every matvec in the probe
    bool   alpha_arm_measured = false; // A(alpha*s) was computed, not scaled from A(s)
    double tau = -1.0;                 // -1 = not measured
    double tau_alpha = -1.0;
    double alpha = -1.0;
    // ||A(alpha*s) - alpha*A(s)|| / ||alpha*A(s)||: the operator's linearity ON THIS STEP,
    // measured rather than presumed. -1 = not measured.
    double linearity_residual = -1.0;
    // R13.17 (external review P1-1): the WORST per-block tau. The global packed L2 lets one
    // dominant block hide a large relative defect in a small one -- and rw/ph/mu, the blocks this
    // campaign is actually about, are the small ones. -1 = not measured.
    double tau_block_max = -1.0;
    // R13.17 (external review P1-2): was the step REALIZED? At float32 a small step can fail to
    // change the stored state at all, and G(K+s) - G(K) can be cancellation-dominated, while tau
    // and the ratio still print plausible values. Fraction of ||s|| actually present in the
    // stored K, and the signal-to-roundoff of the measured difference.
    double realized_step_fraction = -1.0;
    double signal_to_roundoff = -1.0;
};

enum class TaylorVerdict { Unmeasured, FdFallback, AlphaArmAssumed, AlphaDyadic,
                           LinearityUnmeasured, OperatorNonlinear, StepNotRealized,
                           RoundoffLimited, BlockDefect, Measured };

inline const char* taylor_verdict_name(TaylorVerdict v) {
    switch (v) {
        case TaylorVerdict::Unmeasured:      return "unmeasured";
        case TaylorVerdict::FdFallback:      return "fd_fallback";
        case TaylorVerdict::AlphaArmAssumed: return "alpha_arm_assumed";
        case TaylorVerdict::AlphaDyadic:     return "alpha_dyadic";
        case TaylorVerdict::LinearityUnmeasured: return "linearity_unmeasured";
        case TaylorVerdict::OperatorNonlinear: return "operator_nonlinear";
        case TaylorVerdict::StepNotRealized: return "step_not_realized";
        case TaylorVerdict::RoundoffLimited: return "roundoff_limited";
        case TaylorVerdict::BlockDefect:     return "block_defect";
        case TaylorVerdict::Measured:        return "measured";
    }
    return "unknown";
}

// How far A(alpha*s) may sit from alpha*A(s) before the ratio stops being about the Jacobian.
inline constexpr double kTaylorLinearityTol = 1.0e-4;
// The stored state must contain essentially all of the step that was requested. Below this the
// difference being measured is a different step from the one tau is normalised by.
inline constexpr double kTaylorStepRealized = 0.99;
// ||dR|| must stand this far above the float32 roundoff of the quantities differenced, or the
// numerator of tau is cancellation noise. 100x is ~7 significant bits of headroom.
inline constexpr double kTaylorSignalFloor = 100.0;
// A per-block defect this large is a finding even when the packed reading is small: one dominant
// block can carry the global norm while another is wrong. Set at 1 -- a remainder as large as the
// linear response itself in any EXCITED block.
inline constexpr double kTaylorBlockDefect = 1.0;

// A power of two, within the exactness that matters here: alpha == 2^k for integer k.
inline bool is_dyadic(double a) {
    // R13.16 (round 6, R6-10): +Inf passed `a > 0.0`, skipped the first loop, and then
    // `inf * 0.5 == inf` FOREVER -- an infinite loop in a header-inline predicate. Unreachable
    // from the current caller (taylor_defect_verdict rejects non-finite alpha first), but the
    // function is public, its comment about repeated halving reaching 1.0 is simply false for
    // infinities, and no fixture passed it one.
    if (!(a > 0.0) || !std::isfinite(a)) return false;
    // Repeated exact doubling/halving reaches 1.0 iff the significand is 1.
    while (a < 1.0) a *= 2.0;
    while (a > 1.0) a *= 0.5;
    return a == 1.0;
}

inline TaylorVerdict taylor_defect_verdict(const TaylorDefectInputs& in) {
    // R13.14 (round 5, R5-7): `is_measured`, not a bare `>= 0.0`. +Inf passes `>= 0.0`, so a
    // blown-up tau with sound preconditions returned Measured and the row printed the
    // three-way causal conclusion beside `tau=inf`. This header defines the rejecting
    // predicate a few lines up and this rule was not using it.
    if (!is_measured(in.tau) || !is_measured(in.tau_alpha) ||
        !is_measured(in.alpha) || in.alpha <= 0.0) {
        return TaylorVerdict::Unmeasured;
    }
    if (!in.fd_fallback_free)   return TaylorVerdict::FdFallback;
    if (!in.alpha_arm_measured) return TaylorVerdict::AlphaArmAssumed;
    // Checked BEFORE the residual, because with a dyadic alpha the residual is zero by
    // construction and reporting "linear" from it would be the tautology this rule exists for.
    if (is_dyadic(in.alpha))    return TaylorVerdict::AlphaDyadic;
    // R13.14 (round 5, R5-7): an ABSENT linearity measurement is not a nonlinear operator.
    // The sentinel is emitted when the scaled matvec has zero norm -- a degenerate matvec, not
    // a Jacobian defect -- and calling that `operator_nonlinear` names a mechanism from a
    // measurement that was never taken, which is the standard this file's sibling clause
    // already applies to tau ("unmeasured is its own answer, not folded into a failure that
    // names a mechanism"). `is_measured` is used rather than a bare >= 0 so that +/-Inf is
    // rejected here too.
    if (!is_measured(in.linearity_residual)) return TaylorVerdict::LinearityUnmeasured;
    if (in.linearity_residual > kTaylorLinearityTol) {
        return TaylorVerdict::OperatorNonlinear;
    }
    // R13.17 (external review P1-2): the step must have HAPPENED, and the difference it produced
    // must stand above the roundoff floor. Checked after the operator preconditions because an
    // unrealized step is a statement about this measurement, not about the operator.
    if (is_measured(in.realized_step_fraction) &&
        in.realized_step_fraction < kTaylorStepRealized) {
        return TaylorVerdict::StepNotRealized;
    }
    if (is_measured(in.signal_to_roundoff) &&
        in.signal_to_roundoff < kTaylorSignalFloor) {
        return TaylorVerdict::RoundoffLimited;
    }
    // R13.18 (deep review P1-2): the per-block maximum was written and never READ, so a record
    // with tau_global = 0.01 and tau_excited_block_max = 100 still returned Measured. The number
    // a conclusion may quote is the max of the two.
    if (is_measured(in.tau_block_max) && in.tau_block_max > kTaylorBlockDefect) {
        return TaylorVerdict::BlockDefect;
    }
    return TaylorVerdict::Measured;
}

struct AbComparison {
    // These three are "the arms were handed the same inputs". A caller may establish them by
    // COMPARING DIGESTS or BY CONSTRUCTION (one closure, one tensor, handed to every arm) --
    // both are valid evidence, but a caller that emits digests beside `ab_valid=1` is read as
    // having compared them, so a by-construction caller must say which it did. The frozen A/B
    // probe now digests b and x0 per arm and compares, so its `ab_evidence=` field reads
    // `digests_compared`.
    bool same_operator = false;
    bool same_rhs = false;
    bool same_x0 = false;
    bool same_solver_path = false;   // the SAME implementation, not an equivalent one
    bool same_budget = false;        // equal Arnoldi dimension allowed to both arms
    bool early_stop_disabled = false;
    // R13.8: each arm must have STARTED from the same state, not merely been handed the same
    // inputs. Production's preconditioner closures are `mutable` -- a fallback latch and a
    // defect gate that only evaluates on its first call -- so a probe that runs several arms
    // through one closure gives only the FIRST arm a fresh preconditioner, and leaves the
    // aged one to the production solve that follows.
    // R13.15 (external R13.8/R13.9 review, P0-1/P0-2): these two used to be hardcoded `true`
    // at the only production caller, and neither was true as named.
    //
    //   * every arm calls into the SAME `UnifiedPreconditioner` instance. `make_fresh_M()`
    //     copies the mutable WRAPPER (its fallback latch, its closure-local state); the object
    //     underneath -- with its caches, member diagnostics and stage-bound fields, some of
    //     which later branches read -- is shared. "fresh wrapper" is true; "fresh
    //     preconditioner" was not.
    //   * every arm shares one `gmres_op` closure. That is the CORRECT A/B design -- the arms
    //     must differ only in M -- so the defect was never the sharing, it was calling it
    //     freshness.
    //
    // What the comparison actually needs is that the shared objects did not MOVE between arms,
    // which is a measurement, not a naming choice. Each is a behavioural fingerprint: apply the
    // object to one fixed probe vector before and after the ladder and compare the outputs, so
    // any internal state change that could alter a result is caught, and one that could not is
    // correctly ignored.
    bool fresh_wrapper_per_arm = false;         // the mutable wrapper is per-arm (measured)
    bool shared_preconditioner_instance = true; // STATED, not hidden: the object is shared
    bool preconditioner_state_unchanged = false;// M(probe) identical before and after the arms
    bool same_frozen_operator = false;          // one operator closure for every arm
    bool operator_state_unchanged = false;      // A(probe) identical before and after the arms
    // ...and the probe must not have changed the run it observed.
    bool diagnostic_noninterfering = false;
    // R13.8: the operator FGMRES was handed must actually be linear. An FD matvec with a
    // block-dependent epsilon is not, and FGMRES presumes it is.
    bool jvp_authoritative = false;
    // R13.13 (red team round 4): is the IDENTITY term of A = I - h*gamma*J resolved above the
    // operator's own floating-point noise? A = I - h*gamma*J is only invertible BECAUSE of the
    // I; if float32 noise swamps it, every rho below is about a different operator than the one
    // named. Measured as (matvec noise) / (||v|| / ||A v||) on a Krylov direction. This was
    // computed inline at the emit site and read by nothing -- the tenth instance in this tree
    // of a rule whose consumer reads something else -- which is why it now lives here, where a
    // fixture can reject its negation.
    bool identity_resolved = false;
    // Each arm produced a finite, usable number.
    bool rho_a_finite = false;
    bool rho_b_finite = false;
    // R13.8: an ALLOW-LIST, not equality. Two arms that failed the same way -- both
    // NanRetryExhausted, both on a degraded operator -- satisfy termination_a ==
    // termination_b, and same-wrongness is not attribution.
    int  termination_a = -1;
    int  termination_b = -1;
    bool termination_a_admissible = false;   // MaxBudget or ToleranceReached
    bool termination_b_admissible = false;
    // R13.10 (red team P1-1): order invariance was measured (worst_order_delta) and then
    // NOT read by the verdict -- ab_valid=1 printed beside any delta. Measured means a clause.
    bool order_invariant = false;
    // R13.15 (external review P1-1): the Msel arm is an OUTPUT-ROW PROJECTION, and whether it
    // actually engaged was printed per row and read by nothing -- so a layout mismatch that
    // silently disabled the projection still produced ab_valid=1 over rows labelled Msel.
    // Consumed by `msel_attributable` only: the M-vs-I comparison does not depend on it.
    bool msel_engaged_measured = false;
    // R13.15 (external review P1-2): every arm weighted by the SAME D, and a D that the config
    // requested actually arrived. rho_D is a headline number; it is comparable across arms only
    // under both. `d_weighted` used to be printed per row and enforced nowhere, and an empty
    // d_inv_used could not be told apart from a transfer failure.
    bool d_consistent_across_arms = false;
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
    // The ones that caught R13.7.
    if (!c.same_frozen_operator)         return {false, "operator_not_shared_across_arms"};
    if (!c.fresh_wrapper_per_arm)        return {false, "stale_wrapper_per_arm"};
    // The shared instances must not have MOVED. This replaces two claims of freshness that were
    // asserted; it is weaker in name and stronger in evidence.
    if (!c.operator_state_unchanged)     return {false, "operator_state_moved"};
    if (!c.preconditioner_state_unchanged) return {false, "preconditioner_state_moved"};
    if (!c.diagnostic_noninterfering)    return {false, "probe_interfered"};
    if (!c.jvp_authoritative)            return {false, "jvp_not_authoritative"};
    if (!c.identity_resolved)            return {false, "identity_below_noise_floor"};
    if (!c.rho_a_finite || !c.rho_b_finite) return {false, "nonfinite_rho"};
    if (!c.termination_a_admissible || !c.termination_b_admissible) {
        return {false, "inadmissible_termination"};
    }
    if (c.termination_a != c.termination_b) return {false, "different_termination"};
    if (!c.d_consistent_across_arms)        return {false, "d_weight_inconsistent"};
    if (!c.order_invariant)                 return {false, "order_dependent"};
    return {true, "ok"};
}

// The Msel conclusion is a SEPARATE claim from the M-vs-I one and needs its own gate: everything
// attribution needs, plus evidence that the row projection actually engaged on every Msel row.
// Without this split a reader takes `ab_valid=1` -- earned by the M and I arms -- as licence to
// read the Msel rows too.
inline ProbeVerdict msel_attributable(const AbComparison& c) {
    const auto base = ab_attributable(c);
    if (!base.valid) return base;
    if (!c.msel_engaged_measured) return {false, "msel_not_engaged"};
    return {true, "ok"};
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_PROBE_VALIDITY_H
