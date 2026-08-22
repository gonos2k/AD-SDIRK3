// R13.7: when may an A/B comparison be attributed to the variable it names?
//
// THE CASE THIS ENCODES IS MY OWN. R13.4 compared the preconditioner on against off by
// flipping one environment variable, called it single-variable, and concluded that the
// preconditioner was net-harmful. Production branches on that variable into a different
// Krylov implementation:
//
//     gmres_M_inv ? krylov_methods::solve_fgmres(...) : krylov_methods::solve_gmres(...)
//
// and the comment beside it says so outright. The two arms additionally ran different Newton
// counts, so the metric compared minimised over DIFFERENT linear systems. The conclusion was
// retracted.
//
// ONE ENVIRONMENT VARIABLE IS NOT ONE VARIABLE. Nothing in the failure was subtle once seen;
// what was missing was a rule that had to be satisfied before the number could be read. This
// is that rule, and the first case below is the retracted comparison.

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

using wrf::sdirk3::ab_attributable;
using wrf::sdirk3::AbComparison;

AbComparison sound() {
    AbComparison c;
    c.same_operator = c.same_rhs = c.same_x0 = true;
    c.same_solver_path = c.same_budget = c.early_stop_disabled = true;
    c.termination_a = c.termination_b = 5;
    return c;
}

std::string why(const AbComparison& c) { return ab_attributable(c).reason; }

}  // namespace

int main() {
    check(ab_attributable(sound()).valid,
          "a comparison with shared inputs, one code path, equal budget, early-stop off and "
          "matching termination IS attributable (a rule that rejects everything is not a rule)");

    {   // R13.4, as run.
        AbComparison c = sound();
        c.same_solver_path = false;   // FGMRES vs GMRES
        c.same_operator = false;      // different Newton counts -> different A_n
        c.same_rhs = false;
        const auto v = ab_attributable(c);
        check(!v.valid,
              "the RETRACTED R13.4 preconditioner comparison is NOT attributable -- it moved "
              "the Krylov implementation and the linear systems along with the preconditioner");
    }

    {   // The single defect that mattered most, isolated: same system, different code path.
        AbComparison c = sound();
        c.same_solver_path = false;
        check(!ab_attributable(c).valid && why(c) == "different_solver_path",
              "SAME (A,b,x0) but two implementations is still not attributable -- an "
              "equivalent algorithm is not the same code, and early-stop, restart, residual "
              "recomputation and breakdown handling are all free to differ");
    }

    {   AbComparison c = sound(); c.same_operator = false;
        check(!ab_attributable(c).valid && why(c) == "different_operator",
              "different A is not attributable"); }
    {   AbComparison c = sound(); c.same_rhs = false;
        check(!ab_attributable(c).valid && why(c) == "different_rhs",
              "different b is not attributable"); }
    {   AbComparison c = sound(); c.same_x0 = false;
        check(!ab_attributable(c).valid && why(c) == "different_x0",
              "different x0 is not attributable"); }
    {   AbComparison c = sound(); c.same_budget = false;
        check(!ab_attributable(c).valid && why(c) == "different_budget",
              "unequal Arnoldi budget is not attributable -- rho compared at different j is "
              "not a comparison"); }
    {   AbComparison c = sound(); c.early_stop_disabled = false;
        check(!ab_attributable(c).valid && why(c) == "early_stop_enabled",
              "with early stop ENABLED the two arms choose their own budgets, so a nominally "
              "equal budget is not equal work"); }
    {   // The easiest to forget: budgets equal, but one arm stopped for another reason.
        AbComparison c = sound();
        c.termination_b = 2;
        check(!ab_attributable(c).valid && why(c) == "different_termination",
              "equal budget with DIFFERENT termination reasons is not equal work -- this is "
              "how a 'same budget' comparison silently becomes two different amounts of it"); }

    {   // Order is deterministic, so two arms disqualified for several reasons report the same
        // one and a diff of their records is stable.
        AbComparison c;   // everything false
        const auto a = ab_attributable(c), b = ab_attributable(c);
        check(std::string(a.reason) == "different_operator" &&
                  std::string(a.reason) == std::string(b.reason),
              "with every precondition unmet the reported reason is deterministic and is the "
              "outermost one");
    }
    {   // A default-constructed comparison must not be attributable: unpopulated is not sound.
        check(!ab_attributable(AbComparison{}).valid,
              "a default-constructed comparison is NOT attributable -- an unpopulated record "
              "is not evidence of a controlled experiment");
    }

    constexpr int expected_checks = 11;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "AB_ATTRIBUTION_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "AB_ATTRIBUTION_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
