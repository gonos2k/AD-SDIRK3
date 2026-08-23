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
    c.same_frozen_operator = c.fresh_wrapper_per_arm = true;
    c.operator_state_unchanged = c.preconditioner_state_unchanged = true;
    c.d_consistent_across_arms = true;
    c.msel_engaged_measured = true;
    c.diagnostic_noninterfering = true;
    c.jvp_authoritative = true;
    c.identity_resolved = true;
    c.rho_a_finite = c.rho_b_finite = true;
    c.termination_a_admissible = c.termination_b_admissible = true;
    c.termination_a = c.termination_b = 5;
    c.order_invariant = true;
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

    // ---- R13.8: the clauses that caught R13.7 ----
    //
    // R13.7 froze (A, b, x0) and used one solve_fgmres for both arms -- it satisfied every
    // clause the rule had. It was still unattributable, because production's preconditioner
    // closures are `mutable`: a fallback latch, and a defect gate that evaluates only on its
    // first call. Running five arms through one closure gives only the FIRST a fresh
    // preconditioner, and hands the aged one to the production solve that follows.
    {
        // R13.15 (external review P0-1): the contract used to be "a fresh preconditioner per
        // arm", asserted true at the only production caller while every arm called into ONE
        // UnifiedPreconditioner instance. The honest question is not freshness but whether the
        // shared object MOVED, which is a measurement.
        AbComparison c = sound();
        c.preconditioner_state_unchanged = false;
        check(!ab_attributable(c).valid && why(c) == "preconditioner_state_moved",
              "arms sharing ONE stateful preconditioner are attributable only while that "
              "object's behaviour is identical before and after the ladder -- a fresh WRAPPER "
              "around a moving instance is what R13.7 had, and what its clean-looking numbers "
              "could not support");
    }
    {
        AbComparison c = sound();
        c.fresh_wrapper_per_arm = false;
        check(!ab_attributable(c).valid && why(c) == "stale_wrapper_per_arm",
              "and the wrapper itself must still be per-arm: its fallback latch is exactly the "
              "state that gave only the FIRST arm a clean preconditioner");
    }
    {
        AbComparison c = sound();
        c.diagnostic_noninterfering = false;
        check(!ab_attributable(c).valid && why(c) == "probe_interfered",
              "a probe that changed the run it observed is an INTERVENTION, and its numbers "
              "describe a trajectory that would not otherwise have existed");
    }
    {   // FGMRES presumes a linear operator; an FD matvec with a block-dependent epsilon is
        // not one, and A(alpha*v) != alpha*A(v) breaks the premise rather than adding noise.
        AbComparison c = sound();
        c.jvp_authoritative = false;
        check(!ab_attributable(c).valid && why(c) == "jvp_not_authoritative",
              "an operator that fell back to finite differences is not the linear operator "
              "FGMRES assumes, so nothing measured through it attributes to the variable");
    }
    {   // Same-wrongness is not attribution.
        AbComparison c = sound();
        c.termination_a = c.termination_b = 7;   // e.g. NanRetryExhausted, in BOTH arms
        c.termination_a_admissible = c.termination_b_admissible = false;
        check(!ab_attributable(c).valid && why(c) == "inadmissible_termination",
              "two arms that failed the SAME way satisfy termination_a == termination_b and "
              "are still not attributable -- the rule is an allow-list, not an equality");
    }
    {
        AbComparison c = sound();
        c.rho_b_finite = false;
        check(!ab_attributable(c).valid && why(c) == "nonfinite_rho",
              "a non-finite residual in either arm voids the comparison");
    }
    {
        // R13.15 (external review P0-2): sharing ONE operator closure is the correct A/B
        // design -- the arms must differ only in M -- so "fresh operator per arm" was both
        // hardcoded and, as a requirement, wrong. What must hold is that it is the SAME
        // operator and that it did not move.
        AbComparison c = sound();
        c.same_frozen_operator = false;
        check(!ab_attributable(c).valid && why(c) == "operator_not_shared_across_arms",
              "arms given DIFFERENT operators are not an A/B of the preconditioner; the "
              "requirement is sameness, which the old contract inverted into freshness");
    }
    {
        AbComparison c = sound();
        c.operator_state_unchanged = false;
        check(!ab_attributable(c).valid && why(c) == "operator_state_moved",
              "and a shared operator that behaves differently after the ladder than before it "
              "did not give the arms the same operator, whatever the closure identity says");
    }

    {   // R13.10 (red team P1-1): measured and then not read. worst_order_delta was on the
        // record and ab_valid=1 printed beside any value of it.
        AbComparison c = sound();
        c.order_invariant = false;
        check(!ab_attributable(c).valid && why(c) == "order_dependent",
              "arms whose numbers move with their ORDER are not attributable -- something "
              "survived between passes, and a measured delta the verdict ignores is the "
              "eighth instance of a rule without its consumer");
    }

    // R13.13 (red team round 4): A = I - h*gamma*J is invertible BECAUSE of the I. If float32
    // noise swamps the identity term on the directions the solver builds, every rho the probe
    // reports is about a different operator than the one named -- so the measurement was made
    // and then read by nothing, the tenth instance of that split in this tree. The rule lives
    // in the header now, and this fixture is what a rule spelled out at the emit site cannot
    // have.
    {
        auto c = sound();
        c.identity_resolved = false;
        const auto v = ab_attributable(c);
        check(!v.valid && std::string(v.reason) == "identity_below_noise_floor",
              "an operator whose identity term sits below its own noise floor is not the "
              "operator the comparison names, and the refusal says which precondition failed");
    }

    {
        // R13.15 (external review P1-2): rho_D is a headline number and is comparable across
        // arms only if every arm weighted by the SAME D -- which was printed per row and
        // enforced nowhere, while rho0_D from the FIRST row was reused as the baseline for all.
        auto c = sound();
        c.d_consistent_across_arms = false;
        check(!ab_attributable(c).valid && why(c) == "d_weight_inconsistent",
              "arms that weighted by different D are not comparable in rho_D, and an empty "
              "d_inv_used that the config REQUESTED is a transfer failure, not 'D = I'");
    }
    {
        // R13.15 (external review P1-1): the Msel conclusion is a SEPARATE claim. The M-vs-I
        // verdict must not depend on the projection having engaged, and the Msel one must.
        auto c = sound();
        c.msel_engaged_measured = false;
        const auto mi = ab_attributable(c);
        const auto ms = wrf::sdirk3::msel_attributable(c);
        check(mi.valid && !ms.valid && std::string(ms.reason) == "msel_not_engaged",
              "a layout mismatch that silently disables the row projection voids the Msel "
              "conclusion and leaves the M-vs-I one standing -- one verdict could not say that");
    }
    {
        // ...and the Msel verdict inherits every attribution precondition.
        auto c = sound();
        c.order_invariant = false;
        const auto ms = wrf::sdirk3::msel_attributable(c);
        check(!ms.valid && std::string(ms.reason) == "order_dependent",
              "the Msel verdict is attribution PLUS its own receipt, so it can never be the "
              "looser of the two");
    }

    constexpr int expected_checks = 24;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "AB_ATTRIBUTION_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "AB_ATTRIBUTION_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
