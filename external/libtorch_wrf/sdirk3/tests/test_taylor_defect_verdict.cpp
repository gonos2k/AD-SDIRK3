// R13.13: the Taylor-defect probe's preconditions, as a rule with fixtures.
//
// WHY THIS EXISTS. The probe measures tau = ||G(K+s) - G(K) - A s|| / ||A s|| and the same at
// alpha = 1/2, and prints a three-way causal conclusion in its own row: tau << 1 means the
// inner solve binds, tau = O(1) with ratio ~1/2 means the nonlinearity over the step, ratio ~1
// means a Jacobian defect. That conclusion rests on two things the arithmetic cannot see:
//
//   1. A is the real JVP. On the finite-difference fallback A is a difference quotient whose
//      epsilon depends on ||v|| and on per-block state norms -- not linear in its argument, and
//      at float32 noise-limited at roughly the magnitude tau itself reports. "tau = 0.018, the
//      linearization is faithful to 2%" and "tau = 0.018, we measured the noise floor" are the
//      same row without a receipt.
//   2. The alpha arm MEASURES A(alpha*s). Scaling alpha*A(s) instead makes the ratio partly
//      true by construction -- only the numerator gets re-measured -- and imports (1) again.
//
// The probe was written with neither, printing the conclusion unconditionally. This file is
// what a rule spelled out at the emit site cannot have.

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

using wrf::sdirk3::TaylorDefectInputs;
using wrf::sdirk3::TaylorVerdict;
using wrf::sdirk3::taylor_defect_verdict;
using wrf::sdirk3::taylor_verdict_name;

// A probe run whose preconditions all hold: forward-mode JVP throughout, the alpha arm
// computed with its own matvec, and that matvec agreeing with the scaled one.
TaylorDefectInputs sound() {
    TaylorDefectInputs in;
    in.fd_fallback_free = true;
    in.alpha_arm_measured = true;
    in.tau = 0.1192;          // the measured em_b_wave stage-2 iteration-0 value
    in.tau_alpha = 0.03972;   // alpha = 1/3, so tau_alpha/tau = 0.3333 for a quadratic remainder
    in.alpha = 1.0 / 3.0;
    in.linearity_residual = 1.791e-7;   // measured: about 1.5 float32 ulps
    return in;
}

std::string name_of(const TaylorDefectInputs& in) {
    return taylor_verdict_name(taylor_defect_verdict(in));
}

}  // namespace

int main() {
    check(name_of(sound()) == "measured",
          "preconditions met: forward-mode JVP, the alpha arm measured, and A(alpha*s) "
          "agreeing with alpha*A(s) -- the conclusion may be printed (a rule that rejects "
          "everything is not a rule)");

    {
        auto in = sound();
        in.fd_fallback_free = false;
        check(name_of(in) == "fd_fallback",
              "an FD matvec is not a Jacobian: its epsilon moves when the step is halved, so "
              "BOTH tau and the ratio pick up the epsilon's response and the three-way "
              "conclusion is about the difference scheme, not the linearization");
    }
    {
        auto in = sound();
        in.alpha_arm_measured = false;
        check(name_of(in) == "alpha_arm_assumed",
              "taking A(alpha*s) as alpha*A(s) makes the ratio partly true by construction -- "
              "only the numerator is re-measured -- so a ratio of exactly 1/2 stops being "
              "evidence");
    }
    {
        auto in = sound();
        in.linearity_residual = 1.0e-3;
        check(name_of(in) == "operator_nonlinear",
              "and when the measured A(alpha*s) does NOT match alpha*A(s), the operator is not "
              "linear on this step whatever produced it -- the receipt is what makes that "
              "visible instead of assumed");
    }
    {
        auto in = sound();
        in.linearity_residual = -1.0;
        check(name_of(in) == "operator_nonlinear",
              "an UNMEASURED linearity residual is not a passing one: the sentinel must fail "
              "the range test, not slip through it");
    }
    {
        auto in = sound();
        in.tau = -1.0;
        check(name_of(in) == "unmeasured",
              "no tau, no verdict -- and 'unmeasured' is its own answer, not folded into a "
              "failure that names a mechanism");
        in = sound();
        in.tau_alpha = -1.0;
        check(name_of(in) == "unmeasured", "the same for the alpha arm");
        in = sound();
        in.alpha = 0.0;
        check(name_of(in) == "unmeasured",
              "and for alpha itself: a zero step has no defect ratio to report");
    }
    {
        // R13.14 (red team round 5, P0): a DYADIC alpha makes the linearity receipt a tautology.
        // A forward-AD tangent scales by a power of two with identical significands, so
        // A(s/2) == 0.5*A(s) bit for bit for ANY operator -- and the FD path cancels the same
        // way, since halving ||v|| exactly doubles its epsilon exactly and the perturbed vector
        // is the same vector. The probe shipped with alpha = 1/2 and reported
        // linearity_residual = 0, which was read as "the JVP agrees to the last bit".
        for (double a : {0.5, 0.25, 2.0, 1.0, 0.125}) {
            auto in = sound();
            in.alpha = a;
            in.linearity_residual = 0.0;      // what a dyadic alpha always produces
            if (name_of(in) != "alpha_dyadic") {
                check(false, std::string("dyadic alpha ") + std::to_string(a) +
                             " must be refused");
            }
        }
        auto in = sound();
        in.alpha = 1.0 / 3.0;
        check(name_of(in) == "measured",
              "a receipt that cannot fail is not a receipt: every power of two is refused, and "
              "a non-dyadic alpha is what makes the linearity residual a real measurement "
              "(0 by construction at 1/2; 1.7e-07 measured at 1/3)");
    }
    {
        // Ordering: the FD receipt is checked before the linearity residual, because on the FD
        // path the residual is a measurement OF the FD scheme and reporting "operator_nonlinear"
        // would name the wrong cause.
        auto in = sound();
        in.fd_fallback_free = false;
        in.linearity_residual = 1.0e-2;
        check(name_of(in) == "fd_fallback",
              "with both wrong, the refusal names the UPSTREAM cause: on the FD path the "
              "linearity residual measures the difference scheme, so reporting it as an "
              "operator defect would send the work to the Jacobian");
    }
    {
        // tau being large is NOT a precondition failure -- that is the measurement talking.
        auto in = sound();
        in.tau = 3.7;
        in.tau_alpha = 1.85;
        check(name_of(in) == "measured",
              "a LARGE tau is a finding, not an invalid probe: the rule gates on preconditions "
              "only, so it can never quietly suppress the case the probe exists to catch");
    }

    constexpr int expected_checks = 11;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "TAYLOR_DEFECT_VERDICT_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "TAYLOR_DEFECT_VERDICT_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
