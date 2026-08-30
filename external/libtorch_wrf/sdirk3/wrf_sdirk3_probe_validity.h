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

// R13.20 (numerics referee, claim 7.1): the EXCITATION FLOOR, promoted from a bare literal.
//
// A block carrying less than this share of ||A s|| is not being exercised by the step, and its
// ratio is noise over noise -- so it is excluded from `tau_excited_block_max`. The campaign fixed
// the floor-NORMALISATION trap (R13.18 P1-1: an unexcited block's tau is divided by the floor, so
// a small value means "barely touched", not "accurate"). It did not fix the floor-VALUE trap: this
// constant SELECTS WHICH BLOCK THE VERDICT NAMES. Measured on the dt=600 record, `share_rw` is
// 2.16e-04, 4.6x below 1e-3, so `rw` is excluded and the verdict reads 0.2008 (`ru`) -- while
// `tauraw_rw = 0.207042` is LARGER. At a floor of 1e-4 the headline number and the block it names
// both change.
//
// Same treatment as kKrylovNoProgressVsR0, which had the identical shape: an override, the value
// on the record, and the limits written at the constant. The raw max is now emitted beside the
// excited max, so the counterfactual is on every row rather than reachable only by re-running.
// Overridable per run via WRF_SDIRK3_TAU_EXCITATION_SHARE.
inline constexpr double kTauExcitationShare = 1.0e-3;


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

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_PROBE_VALIDITY_H
