// R13 B2: a reference is an assumption until it certifies itself.
//
// THE WORKED FAILURE THIS ENCODES. R12 R4 solved stage 2 at an enlarged budget and reported
// the shipped solve's error against it as an accuracy. A second arm at a LARGER budget then
// returned ref_agree=0 -- bit-identical increments -- together with a WORSE residual (0.9989)
// and converged=0. That is the signature of the second solve warm-starting from the first and
// returning it unchanged, not of a converged reference; the manifest had already measured
// warmstart_enabled=1. Self-certification that shares mutable state with the thing it
// certifies is not independent.
//
// With the warm start off the arms came back at ref_agree=0.2842 against rel_err=0.7371. A
// reference 2.6x from its own sibling is not a reference: those are two unconverged solves
// being differenced, and rel_err was never an accuracy. Note also that the LARGER budget did
// WORSE (0.4612, converged=0) than the smaller (0.1196, converged=1) -- GMRES(m) is not
// monotone in m, so that alone does not prove non-convergence, but it does mean the smaller
// answer is not certified as the solution.
//
// Each criterion below rejects one of those ways of being wrong, and the R12 numbers appear
// verbatim as a case so a regression to that reading fails here.

#include "../wrf_sdirk3_probe_validity.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

using wrf::sdirk3::certify_stage_reference;
using wrf::sdirk3::StageReferenceArms;

// A converging sequence: isolated arms, residuals contracting, increments and F_E settling,
// and the shipped solve far further away than the arms are from each other.
StageReferenceArms good() {
    StageReferenceArms a;
    for (int i = 0; i < 3; ++i) { a.converged[i] = true; a.isolated[i] = true; }
    a.residual[0] = 1.0e-4; a.residual[1] = 1.0e-6; a.residual[2] = 1.0e-8;
    a.state_gap_21 = 1.0e-3;
    a.state_gap_32 = 1.0e-5;
    a.explicit_gap_21 = 1.0e-3;
    a.explicit_gap_32 = 1.0e-5;
    a.shipped_gap  = 0.5;
    return a;
}

std::string why(const StageReferenceArms& a) {
    return certify_stage_reference(a).reason;
}

}  // namespace

int main() {
    check(certify_stage_reference(good()).certified,
          "a converging, isolated three-arm sequence certifies (a rule that certifies "
          "nothing is not a rule, it is a refusal)");

    {   // R12 R4, exactly as measured.
        StageReferenceArms a = good();
        a.converged[1] = false;        // ref2 (200x30) converged=0  final_res=0.4612
        a.converged[2] = false;
        a.residual[0] = 0.1196; a.residual[1] = 0.4612; a.residual[2] = 0.4612;
        a.state_gap_21 = 0.2842;       // ref_agree
        a.state_gap_32 = 0.2842;
        a.shipped_gap  = 0.7371;       // rel_err
        check(!certify_stage_reference(a).certified && why(a) == "arm_not_converged",
              "the R12 R4 measurement does NOT certify -- caught at the first criterion, "
              "because an arm the solver itself failed cannot anchor anything");
    }

    {   // The 2.6x separation alone, with everything else clean.
        StageReferenceArms a = good();
        a.state_gap_21 = 0.30;
        a.state_gap_32 = 0.1;          // contracting, so it reaches the margin clause
        a.shipped_gap  = 0.7371;
        check(!certify_stage_reference(a).certified && why(a) == "insufficient_margin",
              "0.1 against 0.7371 is 7.4x and FAILS the margin: a reference must be far "
              "closer to the truth than the quantity it measures");
    }

    {   // A tighter budget whose residual grows is not approaching a solution.
        StageReferenceArms a = good();
        a.residual[2] = 1.0e-5;
        check(!certify_stage_reference(a).certified && why(a) == "residual_not_decreasing",
              "a tighter arm with a WORSE residual fails -- the 0.1196 -> 0.4612 shape");
    }

    // ---- R13.1: the five holes the R13 predicate had ----

    {   // (a) NaN and Inf. Three +Inf residuals satisfy `>0` and `<=`, so the R13 predicate
        // accepted an entirely non-finite arm set.
        StageReferenceArms a = good();
        const double inf = std::numeric_limits<double>::infinity();
        a.residual[0] = a.residual[1] = a.residual[2] = inf;
        check(!certify_stage_reference(a).certified && why(a) == "nonfinite_residual",
              "three +Inf residuals FAIL -- they satisfy `>0` and `<=` and the R13 rule "
              "certified them");
        StageReferenceArms b = good();
        b.state_gap_32 = std::numeric_limits<double>::quiet_NaN();
        check(!certify_stage_reference(b).certified && why(b) == "state_gap_unavailable",
              "a NaN gap FAILS rather than passing every comparison it appears in");
    }

    {   // (b) An exactly converged arm. `residual > 0` rejected the one case the predicate
        // most wants to accept.
        StageReferenceArms a = good();
        a.residual[2] = 0.0;
        check(certify_stage_reference(a).certified,
              "an EXACTLY converged tightest arm (residual 0) certifies -- R13's `> 0` test "
              "reported it as residual_unavailable");
    }

    {   // (c) Monotone but going nowhere.
        StageReferenceArms a = good();
        a.residual[0] = 1.000; a.residual[1] = 0.999; a.residual[2] = 0.998;
        check(!certify_stage_reference(a).certified && why(a) == "residual_not_settled",
              "1.000 -> 0.999 -> 0.998 FAILS: monotone is not converging, and R13 required "
              "only monotone");
    }

    {   // (d) The same for the state gap.
        StageReferenceArms a = good();
        a.state_gap_21 = 0.500;
        a.state_gap_32 = 0.499;
        check(!certify_stage_reference(a).certified && why(a) == "state_gap_not_settled",
              "0.500 -> 0.499 FAILS for the increment too");
    }

    {   // (e) F_E. R13's completion table required it and the predicate had no field for it.
        StageReferenceArms a = good();
        a.explicit_gap_21 = 0.4;
        a.explicit_gap_32 = 0.39;
        check(!certify_stage_reference(a).certified && why(a) == "explicit_gap_not_settled",
              "increments that settle while F_E does NOT fail -- F_E(Y_s) forces the next "
              "stage, so certifying on the increment alone certifies the wrong quantity");
        StageReferenceArms b = good();
        b.explicit_gap_32 = -1.0;
        check(!certify_stage_reference(b).certified && why(b) == "explicit_gap_unavailable",
              "and an F_E gap that was never measured is not a passing one");
    }

    {   // (f) Isolation is part of the predicate now. This is what closes the hole R13's own
        // test documented and left open: a bit-identical arm 3 satisfies every numeric
        // clause, and the reason it can be bit-identical is shared state.
        StageReferenceArms a = good();
        a.isolated[1] = false;
        check(!certify_stage_reference(a).certified && why(a) == "arm_not_isolated",
              "an arm that did not start from the entry state FAILS before any number is "
              "read -- three points on one trajectory can agree for reasons unrelated to "
              "either being right");
        StageReferenceArms b = good();
        b.state_gap_32 = 0.0;          // the warm-start signature
        check(certify_stage_reference(b).certified,
              "a bit-identical arm 3 still satisfies the NUMERIC clauses (gap 0) -- which is "
              "why isolation had to become a clause rather than a caveat");
    }

    {
        StageReferenceArms a = good();
        a.shipped_gap = -1.0;
        check(!certify_stage_reference(a).certified && why(a) == "shipped_gap_unavailable",
              "a missing shipped gap is not a passing one");
    }
    {   // The default of a freshly constructed record must be "not certified".
        check(!certify_stage_reference(StageReferenceArms{}).certified,
              "a default-constructed arm set does not certify -- an unpopulated probe "
              "reports no reference, not a perfect one");
    }

    constexpr int expected_checks = 15;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "STAGE_REFERENCE_CERTIFICATION_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "STAGE_REFERENCE_CERTIFICATION_CONTRACT: FAIL (" << failures << ")"
              << std::endl;
    return 1;
}
