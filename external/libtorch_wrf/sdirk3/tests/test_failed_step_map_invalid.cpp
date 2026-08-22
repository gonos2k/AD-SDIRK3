// R13 A1: a step that did not advance may not report a tangent.
//
// WHAT WENT WRONG. R12 R1 measured the one-step map and found D(Phi_h) = I with
// worst_agree=6.4e-05 across two epsilons, and the record said so in as many words: "the
// central difference converged and D(Phi_h).v IS a directional derivative". Every one of the
// four arms had hit ABORT_ON_NEWTON_FAIL. The fail-closed gate suppresses the state publish,
// so each arm returned its INPUT, and a central difference of the map U -> U is the identity
// for any dynamics whatsoever. The quotient was arithmetically perfect and measured the
// rollback sentinel.
//
// That is not a hypothetical: WRF's own driver refuses the same step. module_implicit_sdirk3.F
// fatals on HARD_STAGE_ABORT, SOFT_NO_PROGRESS and every FATAL_*, so the state the probe
// differenced never reaches the outer integrator.
//
// WHAT THIS TEST PINS. The rule that decides the verdict, over the outcome codes that actually
// occur -- including the two that are easy to read as benign (OK_SKIPPED is "ok", and
// SOFT_NO_PROGRESS says the solve was finite) and are not, because neither publishes a state.

#include "../wrf_sdirk3_probe_validity.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

using wrf::sdirk3::StepOutcomeCode;
using wrf::sdirk3::StepStatus;
using wrf::sdirk3::step_map_verdict;
using wrf::sdirk3::step_status_of;

StepStatus st(StepOutcomeCode c) { return step_status_of(static_cast<int>(c)); }

}  // namespace

int main() {
    // --- the taxonomy, on the codes the solver really emits ---
    check(st(StepOutcomeCode::OK_ADVANCED) == StepStatus::Complete,
          "OK_ADVANCED is the ONLY code that means a state was published");
    check(st(StepOutcomeCode::OK_SKIPPED) == StepStatus::Rejected,
          "OK_SKIPPED is REJECTED, not Complete -- 'OK' names the call, not an advance");
    check(st(StepOutcomeCode::SOFT_NO_PROGRESS) == StepStatus::Rejected,
          "SOFT_NO_PROGRESS is REJECTED -- a finite solve that moved nothing is still no map");
    check(st(StepOutcomeCode::HARD_STAGE_ABORT) == StepStatus::Aborted,
          "HARD_STAGE_ABORT is ABORTED (this is outcome=20, the code em_b_wave produces)");
    check(st(StepOutcomeCode::FATAL_INPUT) == StepStatus::Aborted,
          "FATAL_INPUT is ABORTED");
    check(st(StepOutcomeCode::FATAL_INTERNAL) == StepStatus::Aborted,
          "FATAL_INTERNAL is ABORTED");
    // An unrecognised code must fail CLOSED. A new outcome added later defaults to
    // "not a map" rather than silently becoming admissible.
    check(step_status_of(9999) == StepStatus::Aborted,
          "an UNKNOWN outcome code is ABORTED (a new code cannot become admissible by "
          "default)");

    // --- the verdict rule ---
    const std::vector<StepStatus> all_complete{
        StepStatus::Complete, StepStatus::Complete, StepStatus::Complete, StepStatus::Complete};
    check(step_map_verdict(all_complete).valid,
          "four Complete arms: the verdict is admissible");

    // The exact configuration R12 R1 ran under.
    const std::vector<StepStatus> r12_run{
        StepStatus::Aborted, StepStatus::Aborted, StepStatus::Aborted, StepStatus::Aborted};
    const auto v_r12 = step_map_verdict(r12_run);
    check(!v_r12.valid && std::string(v_r12.reason) == "noncomplete_arm",
          "four ABORTED arms (the R12 configuration): VOID, reason=noncomplete_arm");

    // One bad arm out of four is enough -- the quotient mixes a real evaluation with a
    // returned input, which is worse than four rollbacks, not better.
    std::vector<StepStatus> one_bad = all_complete;
    one_bad[2] = StepStatus::Rejected;
    const auto v_one = step_map_verdict(one_bad);
    check(!v_one.valid && std::string(v_one.reason) == "noncomplete_arm",
          "ONE non-Complete arm voids the record (a mixed quotient is not a derivative)");

    // "All arms complete" over an empty set is vacuously true, and that is the reading that
    // turns 'never ran' into 'passed'. It must not.
    const auto v_empty = step_map_verdict({});
    check(!v_empty.valid && std::string(v_empty.reason) == "no_arm_ran",
          "NO arms is not 'all arms succeeded' -- vacuous truth fails closed");

    constexpr int expected_checks = 11;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "FAILED_STEP_MAP_IS_INVALID_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "FAILED_STEP_MAP_IS_INVALID_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
