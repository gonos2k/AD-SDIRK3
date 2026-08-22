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

// A converging sequence: residuals fall, the increments settle, and the shipped solve is far
// further away than the arms are from each other.
StageReferenceArms good() {
    StageReferenceArms a;
    a.converged_1 = a.converged_2 = a.converged_3 = true;
    a.residual_1 = 1.0e-4; a.residual_2 = 1.0e-6; a.residual_3 = 1.0e-8;
    a.state_gap_21 = 1.0e-3;
    a.state_gap_32 = 1.0e-5;
    a.shipped_gap  = 0.5;
    return a;
}

}  // namespace

int main() {
    check(certify_stage_reference(good()).certified,
          "a converging three-arm sequence certifies (a rule that certifies nothing is not a "
          "rule, it is a refusal)");

    {   // R12 R4, exactly as measured.
        StageReferenceArms a;
        a.converged_1 = true;          // ref  (120x20) converged=1  final_res=0.1196
        a.converged_2 = false;         // ref2 (200x30) converged=0  final_res=0.4612
        a.converged_3 = false;
        a.residual_1 = 0.1196; a.residual_2 = 0.4612; a.residual_3 = 0.4612;
        a.state_gap_21 = 0.2842;       // ref_agree
        a.state_gap_32 = 0.2842;
        a.shipped_gap  = 0.7371;       // rel_err
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "arm_not_converged",
              "the R12 R4 measurement does NOT certify -- and it is caught at the first "
              "criterion, because an arm the solver itself failed cannot anchor anything");
    }

    {   // The 2.6x separation on its own, with every arm converged: still not a reference.
        StageReferenceArms a = good();
        a.state_gap_21 = 0.30;
        a.state_gap_32 = 0.2842;
        a.shipped_gap  = 0.7371;
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "insufficient_margin",
              "0.2842 against 0.7371 is 2.6x and FAILS the margin: a reference must be far "
              "closer to the truth than the quantity it measures");
    }

    {   // A tighter budget whose residual grows is not approaching a solution.
        StageReferenceArms a = good();
        a.residual_3 = 1.0e-5;
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "residual_not_decreasing",
              "a tighter arm with a WORSE residual fails -- the exact 0.1196 -> 0.4612 shape");
    }

    {   // Increments that stop settling: the sequence agrees once and then wanders.
        StageReferenceArms a = good();
        a.state_gap_32 = 2.0e-3;   // larger than state_gap_21
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "state_gap_not_shrinking",
              "increments that stop settling fail, even with falling residuals");
    }

    {   // The warm-start signature: arm 3 returns arm 2 unchanged. gap_32 = 0 passes the
        // shrinking test trivially, so the margin test is what has to catch it -- and it does
        // NOT, because 0 * margin <= shipped_gap holds. This is the one hole a numeric rule
        // cannot close, so it is closed at the source instead (the probe restores the carried
        // state between arms) and recorded here so the limit is not forgotten.
        StageReferenceArms a = good();
        a.state_gap_32 = 0.0;
        const auto v = certify_stage_reference(a);
        check(v.certified,
              "KNOWN LIMIT: a bit-identical arm 3 (the warm-start signature) certifies under "
              "the numeric rule -- which is why arm independence is enforced by resetting the "
              "carried solver state, not by this predicate");
    }

    {
        StageReferenceArms a = good();
        a.residual_2 = -1.0;
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "residual_unavailable",
              "a missing residual is not a passing one");
    }
    {
        StageReferenceArms a = good();
        a.shipped_gap = -1.0;
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "shipped_gap_unavailable",
              "a missing shipped gap is not a passing one");
    }
    {
        StageReferenceArms a = good();
        a.state_gap_21 = -1.0;
        const auto v = certify_stage_reference(a);
        check(!v.certified && std::string(v.reason) == "state_gap_unavailable",
              "a missing state gap is not a passing one");
    }
    {   // The default of a freshly constructed record must be "not certified".
        const auto v = certify_stage_reference(StageReferenceArms{});
        check(!v.certified,
              "a default-constructed arm set does not certify -- an unpopulated probe reports "
              "no reference, not a perfect one");
    }

    constexpr int expected_checks = 10;
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
