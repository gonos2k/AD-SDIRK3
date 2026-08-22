// R13 A2: a JVP that fell back to finite differences is not a forward-mode tangent, and every
// verdict resting on it is void.
//
// WHY A COUNTER IS NOT ENOUGH. R12 C7 added g_jvp_fd_fallback_count so a degraded operator
// becomes visible without debug_level>=1, and measured 0 on em_b_wave. That is the right
// instrument and it does not close the hole: nothing CONSUMES the counter. A Ritz spectrum, a
// Taylor remainder or a transpose identity computed on an FD quotient prints in exactly the
// same format as one computed on a dual, and at float32 the FD noise floor sits well above the
// tolerances those records report. The fallback is silent by construction -- it exists to keep
// the solve running -- so the only thing that can stop it from laundering into a conclusion is
// a rule that marks the conclusion invalid.
//
// THE OTHER THREE DISQUALIFIERS are in the same rule for the same reason: each has already
// produced a published number in this campaign that had to be retracted.
//   - tile_local_operator: R12 C1 -- a spectrum from one tile of a patch is not the patch's.
//   - noncomplete_arm:     R12 R1 -- see Failed_Step_Map_Is_Invalid_Contract.
//   - reference_mismatch:  R12 P1a -- the probes linearized F(U, U_ref=U_0) while production
//                          integrates F(U, U_ref=U); the Jacobians differ by dF/dU_ref.

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

using wrf::sdirk3::TangentInputs;
using wrf::sdirk3::TangentSemantics;
using wrf::sdirk3::tangent_semantics_name;
using wrf::sdirk3::tangent_verdict;

}  // namespace

int main() {
    // The admissible case, and it must be admissible -- a rule that voids everything is not a
    // gate, it is an off switch.
    check(tangent_verdict(TangentInputs{}).valid,
          "a clean forward-mode tangent on a whole-patch single-rank operator is admissible");

    {
        TangentInputs in;
        in.fd_fallback = true;
        const auto v = tangent_verdict(in);
        check(!v.valid && std::string(v.reason) == "fd_fallback",
              "fd_fallback VOIDS the verdict (an FD quotient is not the operator's tangent)");
    }
    {
        TangentInputs in;
        in.topology_ok = false;
        const auto v = tangent_verdict(in);
        check(!v.valid && std::string(v.reason) == "tile_local_operator",
              "a tile-local operator voids the verdict");
    }
    {
        TangentInputs in;
        in.arms_complete = false;
        const auto v = tangent_verdict(in);
        check(!v.valid && std::string(v.reason) == "noncomplete_arm",
              "a non-complete step arm voids the verdict");
    }
    {
        TangentInputs in;
        in.reference_matches = false;
        const auto v = tangent_verdict(in);
        check(!v.valid && std::string(v.reason) == "reference_mismatch",
              "a linearization point production would not have used voids the verdict");
    }

    // Determinism when several disqualifiers hold at once. Two runs that are both invalid for
    // two reasons must report the SAME reason, or a diff of their records reports a difference
    // that is an artifact of evaluation order.
    {
        TangentInputs in;
        in.fd_fallback = true;
        in.topology_ok = false;
        in.arms_complete = false;
        in.reference_matches = false;
        const auto a = tangent_verdict(in);
        const auto b = tangent_verdict(in);
        check(std::string(a.reason) == "tile_local_operator" &&
                  std::string(a.reason) == std::string(b.reason),
              "with every disqualifier set, the reported reason is deterministic and is the "
              "outermost one (topology)");
    }

    // The semantics tag is the other half: WHICH function was linearized is not recoverable
    // from a config echo after the fact, and the three are different models, not settings.
    check(std::string(tangent_semantics_name(TangentSemantics::ExactPrimal)) == "exact_primal",
          "ExactPrimal names dF/dU of the function the forward integrates");
    check(std::string(tangent_semantics_name(TangentSemantics::OperationalDetachedSlow)) ==
              "operational_detached_slow",
          "OperationalDetachedSlow names the production graph with the slow channel detached");
    check(std::string(tangent_semantics_name(TangentSemantics::DiagnosticFrozenReference)) ==
              "diagnostic_frozen_reference",
          "DiagnosticFrozenReference names the frozen-U_ref probe mapping");

    constexpr int expected_checks = 9;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "JVP_FALLBACK_INVALIDATES_VERDICT_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "JVP_FALLBACK_INVALIDATES_VERDICT_CONTRACT: FAIL (" << failures << ")"
              << std::endl;
    return 1;
}
