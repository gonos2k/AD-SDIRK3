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

    // ---- R13.1: the verdict and its CONSUMER, tested as a pair ----
    //
    // R13 computed tangent_verdict and then selected its conclusion sentence with
    // `e_drop > 0.0`. Under an FD fallback that is not a near miss but an inversion: FD
    // cannot see a detach, so it returns the primal tangent, e_drop comes out ~0, and the
    // record asserted the STRONGEST claim available from the one measurement guaranteed to
    // be incapable of supporting it. Testing the rule alone could never have caught that --
    // tangent_verdict was correct. Only the pair fails.
    {
        using wrf::sdirk3::tangent_relation;
        using wrf::sdirk3::TangentRelation;

        TangentInputs fd;
        fd.fd_fallback = true;
        const auto v_fd = tangent_verdict(fd);
        check(tangent_relation(v_fd, 0.0) == TangentRelation::Unavailable,
              "an FD fallback with e_drop=0 reports UNAVAILABLE, not 'matches primal' -- the "
              "exact record R13 could emit, and the most confident possible reading of the "
              "least capable possible measurement");

        const auto v_ok = tangent_verdict(TangentInputs{});
        check(tangent_relation(v_ok, -1.0) == TangentRelation::Unavailable,
              "e_drop = -1 (never measured) is UNAVAILABLE -- it is not > 0, and R13's test "
              "sent it to 'IS the primal derivative'");
        check(tangent_relation(v_ok, std::numeric_limits<double>::quiet_NaN()) ==
                  TangentRelation::Unavailable,
              "NaN is UNAVAILABLE: every comparison against it is false, including the one "
              "that would have rejected it");
        check(tangent_relation(v_ok, 0.0) == TangentRelation::MatchesPrimal,
              "a VALID tangent with e_drop=0 does report matching the primal derivative");
        check(tangent_relation(v_ok, 1.0e-12) == TangentRelation::MatchesPrimal,
              "and a tolerance, not exact zero -- these are float32 tangents");
        check(tangent_relation(v_ok, 0.28) == TangentRelation::DiffersFromPrimal,
              "a valid, measured, nonzero e_drop reports differing from the primal derivative "
              "(0.28 is the top of the range R12 measured for the detached slow channel)");
        check(std::string(wrf::sdirk3::tangent_relation_name(TangentRelation::Unavailable)) ==
                  "unavailable",
              "and the record names the relation rather than implying it in prose");
    }

    // R1: a vanished finite-difference reference cannot certify a Jacobian.
    {
        torch::NoGradGuard no_grad;
        using wrf::sdirk3::central_fd_perturbation_verdict;
        using wrf::sdirk3::compare_jvp_reference;
        const auto k = torch::tensor({1.0e4f});
        const auto v = torch::ones_like(k);
        const auto plus = k + 1.0e-6f * v;
        const auto minus = k - 1.0e-6f * v;
        const auto lost = central_fd_perturbation_verdict(k, v, plus, minus);
        check(!lost.valid && std::string(lost.reason) == "unresolved_perturbation",
              "FP32 K=1e4, epsilon=1e-6 loses both perturbations");
        const auto invalid = compare_jvp_reference(v, (plus-minus)/2.0e-6f, lost);
        check(!invalid.verdict.valid && std::isnan(invalid.relative_error),
              "vanished FD reference cannot produce a passing zero error");
        const auto zero = torch::zeros_like(v);
        const wrf::sdirk3::ProbeVerdict resolved{true, "ok"};
        check(!compare_jvp_reference(v, zero, resolved).verdict.valid,
              "nonzero candidate against zero reference is not a pass");
        check(!compare_jvp_reference(zero, zero, resolved).verdict.valid,
              "two zero derivatives are uninformative, not independent confirmation");
        const auto match = compare_jvp_reference(v, v, resolved);
        check(match.verdict.valid && match.relative_error == 0.0,
              "informative equal derivatives pass");
        const auto mismatch = compare_jvp_reference(2*v, v, resolved);
        check(mismatch.verdict.valid && mismatch.relative_error == 1.0,
              "informative unequal derivatives retain their actual error");
        const auto nan = torch::full_like(v, std::numeric_limits<float>::quiet_NaN());
        const auto inf = torch::full_like(v, std::numeric_limits<float>::infinity());
        check(!compare_jvp_reference(nan, v, resolved).verdict.valid,
              "NaN candidate cannot pass");
        check(!compare_jvp_reference(v, inf, resolved).verdict.valid,
              "infinite reference cannot pass");
        const auto mixed = torch::tensor({1.0e4f, 0.0f});
        const auto mv = torch::ones_like(mixed);
        check(!central_fd_perturbation_verdict(mixed, mv, mixed+1.0e-6f*mv,
                                              mixed-1.0e-6f*mv).valid,
              "one resolved coordinate does not hide another lost coordinate");
        check(!central_fd_perturbation_verdict(k, v, k+1.0e-8f*v, k-1.0e-8f*v).valid,
              "stage-state perturbation loss is checked independently of K");
        check(!central_fd_perturbation_verdict(k, zero, k, k).valid,
              "zero direction is uninformative");
        check(!central_fd_perturbation_verdict(inf, v, inf, inf).valid,
              "nonfinite evaluation points are invalid");
        const auto small = torch::tensor({1.0e-20}, torch::kFloat64);
        const auto small_cmp = compare_jvp_reference(1.5*small, small, resolved);
        check(small_cmp.verdict.valid && std::abs(small_cmp.relative_error-0.5) < 1e-14,
              "small nonzero FP64 reference retains scale-relative error");
        const auto x = torch::tensor({2.0}, torch::kFloat64);
        const auto dx = torch::ones_like(x);
        const auto xp = x+1.0e-3*dx, xm = x-1.0e-3*dx;
        const auto finite_cmp = compare_jvp_reference(2*x*dx, (xp.square()-xm.square())/2.0e-3,
            central_fd_perturbation_verdict(x, dx, xp, xm));
        check(finite_cmp.verdict.valid && finite_cmp.relative_error < 1e-12,
              "central FD of x squared agrees with the analytic derivative");
    }

    constexpr int expected_checks = 30;
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
