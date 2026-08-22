// R13.2: which gate refused FIRST, and therefore which layer the next fix belongs in.
//
// WHY THIS EXISTS. The campaign has swept dt at 600, 300, 120, 60 and 20 and got the same
// answer every time -- "the step did not complete". That sweep cannot converge on a cause,
// because the phrase is one bucket holding at least seven outcomes that point at different
// layers: the state and its EOS, the RHS operator, the linearization, the preconditioner,
// the trust-region policy, the gate threshold, the publish gate. A dt sweep is the right
// experiment for exactly one of them (NewtonDiverged), and there was no way to tell whether
// that was the one occurring.
//
// FIRST, NOT WORST, and that is the whole design. A run whose entry state is already
// non-finite will ALSO show a stagnating Krylov solve and a rejected step -- every later gate
// refuses too, because they are all looking at consequences. Reporting the loudest signal
// sends the next week of work to the wrong layer, which is the failure this classifier
// exists to prevent, so the ordering cases below matter more than the individual ones.

#include "../wrf_sdirk3_first_failure.h"

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

using wrf::sdirk3::first_failure_of;
using wrf::sdirk3::StageFailure;
using wrf::sdirk3::stage_failure_layer;
using wrf::sdirk3::stage_failure_name;
using wrf::sdirk3::StageFailureSignals;

// A clean, converged, published stage.
StageFailureSignals ok_stage() {
    StageFailureSignals s;
    s.entry_state_finite = true;
    s.initial_residual_finite = true;
    s.residual_first = 1.0e-2;
    s.residual_last = 1.0e-9;
    s.newton_iterations = 4;
    s.newton_iteration_budget = 12;
    s.newton_converged = true;
    s.best_krylov_rel_error = 1.0e-3;
    s.krylov_iterations = 60;
    s.gmres_total_failures = 0;
    s.accepted_steps = 4;
    s.rejected_steps = 0;
    s.gate_metric_ok = true;
    s.state_published = true;
    return s;
}

std::string name_of(const StageFailureSignals& s) {
    return stage_failure_name(first_failure_of(s));
}

}  // namespace

int main() {
    check(first_failure_of(ok_stage()) == StageFailure::None,
          "a converged, admissible, published stage reports NO failure (a classifier that "
          "always finds one is not a classifier)");

    // ---- each category on its own ----
    {
        auto s = ok_stage();
        s.entry_state_finite = false;
        check(name_of(s) == "entry_state_not_finite",
              "a non-finite stage ENTRY state: the equation G_s does not exist, and nothing "
              "measured after it is a cause");
    }
    {
        auto s = ok_stage();
        s.initial_residual_finite = false;
        s.newton_converged = false;
        check(name_of(s) == "initial_residual_not_finite",
              "a finite entry with a non-finite G(0): the RHS operator, not the state");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 1.0e-2;
        s.residual_last = 5.0e-1;      // grew 50x
        check(name_of(s) == "newton_diverged",
              "a residual that GREW is divergence -- and this is the ONE category a dt sweep "
              "is the right experiment for");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = std::numeric_limits<double>::infinity();
        check(name_of(s) == "newton_diverged",
              "a non-finite FINAL residual is divergence that overflowed, not a missing "
              "measurement");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;      // roughly flat
        s.best_krylov_rel_error = 1.0;  // GMRES ended where it started
        check(name_of(s) == "krylov_stagnated",
              "a LINEAR solve that made no progress: Newton cannot converge on top of a "
              "solve that does not solve, so the cause is upstream of the outer iteration");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.gmres_total_failures = 2;
        check(name_of(s) == "krylov_stagnated",
              "an outright GMRES failure is the same finding, reached from a different "
              "signal");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 1.0e-4;   // the linear solve WORKED
        s.accepted_steps = 0;
        s.rejected_steps = 6;
        check(name_of(s) == "all_steps_rejected",
              "a working linear solve whose every step was rejected is a trust-region POLICY "
              "statement, not a numerical one");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 1.0e-2;
        s.residual_last = 9.0e-3;           // flat
        s.best_krylov_rel_error = 1.0e-4;   // linear solve fine
        s.accepted_steps = 5;               // steps were taken
        s.rejected_steps = 1;
        s.newton_iterations = 4;            // budget 12: not exhausted
        check(name_of(s) == "newton_stagnated",
              "steps taken, linear solve working, residual flat: the residual has a FLOOR, "
              "which is a statement about the split");
    }
    {
        auto s = ok_stage();
        s.gate_metric_ok = false;
        check(name_of(s) == "admissibility_rejected",
              "Newton converged by its own test and the gate refused: the disagreement is "
              "between two measures of 'converged', not in the solve");
    }
    {
        auto s = ok_stage();
        s.state_published = false;
        check(name_of(s) == "publish_rejected",
              "everything converged and admissible, and the state was still not published");
    }

    // ---- THE CASE THE FIRST REAL RUN PRODUCED ----
    //
    // em_b_wave, dt=600, stage 2, verbatim. Written down as a fixture because it REFUTED the
    // taxonomy: it classified as newton_stagnated, which sends the work to "residual floor or
    // split", while the residual had fallen monotonically from 1 to 0.4855 with every step
    // accepted and the linear solve working. Newton did not stall -- it spent its third and
    // last iteration and was cut off short of newton_tol=0.2.
    //
    // Every other category was excluded by the same record, which is the point of measuring:
    // entry_finite=1 (not the state or the EOS), R0_finite=1 (not the RHS operator), the
    // residual FELL (not divergence, so the dt sweep was the wrong experiment),
    // best_krylov_rel=0.5526 with gmres_total_failures=0 (not the preconditioner or the
    // operator -- the linear solve made progress), steps_rejected=0 (not the trust region),
    // gate_metric_ok=1 (not the admissibility threshold).
    {
        StageFailureSignals s;
        s.entry_state_finite = true;
        s.initial_residual_finite = true;
        s.residual_first = 1.0;
        s.residual_last = 0.4855;
        s.newton_iterations = 3;
        s.newton_iteration_budget = 3;
        s.newton_converged = false;
        s.best_krylov_rel_error = 0.5526;
        s.krylov_iterations = 21;
        s.gmres_total_failures = 0;
        s.accepted_steps = 3;
        s.rejected_steps = 0;
        s.gate_metric_ok = true;
        s.state_published = false;
        check(name_of(s) == "newton_budget_exhausted",
              "em_b_wave dt=600 stage 2, as measured: a residual falling 1 -> 0.4855 over "
              "3 of 3 allowed iterations is BUDGET EXHAUSTION, not a stall");
        check(std::string(stage_failure_layer(first_failure_of(s))) ==
                  "newton_iteration_budget",
              "...and the layer it names is the budget, not the split -- the distinction the "
              "first real run existed to make");
    }
    {   // The same signals with the budget NOT exhausted: the iteration stopped early for
        // some other reason, and that IS a finding about the solve.
        StageFailureSignals s;
        s.entry_state_finite = true;
        s.initial_residual_finite = true;
        s.residual_first = 1.0;
        s.residual_last = 0.4855;
        s.newton_iterations = 3;
        s.newton_iteration_budget = 12;
        s.newton_converged = false;
        s.best_krylov_rel_error = 0.5526;
        s.accepted_steps = 3;
        s.gate_metric_ok = true;
        check(name_of(s) == "newton_stagnated",
              "the SAME residual history with iterations left in the budget is NOT budget "
              "exhaustion -- the two are separated by the budget, not by the residual");
    }
    {   // A flat residual that also exhausted the budget is a stall, not a budget statement.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 1.0;
        s.residual_last = 0.99;
        s.newton_iterations = 12;
        s.newton_iteration_budget = 12;
        s.best_krylov_rel_error = 1.0e-4;
        s.accepted_steps = 12;
        check(name_of(s) == "newton_stagnated",
              "a FLAT residual that also ran out of budget is a stall: spending every "
              "iteration on nothing is not the budget's fault");
    }

    // ---- ORDERING: the part that decides where the work goes ----
    {
        // Every later gate is refusing too, because they are all looking at consequences.
        auto s = ok_stage();
        s.entry_state_finite = false;
        s.initial_residual_finite = false;
        s.newton_converged = false;
        s.residual_last = std::numeric_limits<double>::infinity();
        s.best_krylov_rel_error = 1.0;
        s.gmres_total_failures = 3;
        s.accepted_steps = 0;
        s.rejected_steps = 9;
        s.gate_metric_ok = false;
        s.state_published = false;
        check(name_of(s) == "entry_state_not_finite",
              "with EVERY gate refusing, the report is the FIRST one -- reporting the "
              "Krylov stall here would send the work to the preconditioner when the state "
              "was already non-finite before the solve began");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = std::numeric_limits<double>::infinity();
        s.best_krylov_rel_error = 1.0;
        s.accepted_steps = 0;
        s.rejected_steps = 4;
        check(name_of(s) == "newton_diverged",
              "divergence outranks a Krylov stall and a rejected step: a diverging iteration "
              "produces both");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 1.0;
        s.accepted_steps = 0;
        s.rejected_steps = 4;
        check(name_of(s) == "krylov_stagnated",
              "a Krylov stall outranks the rejected steps it causes -- a solve that returns "
              "nothing has nothing to accept");
    }

    // ---- boundaries ----
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 1.0e-2;
        s.residual_last = 1.9e-2;           // grew, but under the divergence factor
        s.best_krylov_rel_error = 1.0e-4;
        s.accepted_steps = 3;
        check(name_of(s) == "newton_stagnated",
              "a residual that ends slightly ABOVE where it started is stagnation with noise, "
              "not divergence -- naming it divergence would send the work to dt");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 0.5;      // halved: real progress
        s.accepted_steps = 3;
        check(name_of(s) == "newton_stagnated",
              "a linear solve that halved its residual made progress, so the stall is the "
              "OUTER iteration's");
    }
    {
        auto s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = -1.0;     // never measured
        s.accepted_steps = 3;
        check(name_of(s) == "newton_stagnated",
              "an UNMEASURED Krylov error is not evidence of a stall -- absence of a "
              "measurement must not become a finding");
    }

    // The layer mapping is the point of the exercise: it says where to work next.
    check(std::string(stage_failure_layer(StageFailure::KrylovStagnated)) ==
              "preconditioner_or_operator" &&
          std::string(stage_failure_layer(StageFailure::NewtonDiverged)) ==
              "linearization_or_timestep" &&
          std::string(stage_failure_layer(StageFailure::EntryStateNotFinite)) ==
              "state_eos_metric_boundary",
          "each category names the LAYER to work on, as data rather than something the "
          "reader reconstructs");

    constexpr int expected_checks = 22;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "FIRST_FAILURE_CLASSIFICATION_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "FIRST_FAILURE_CLASSIFICATION_CONTRACT: FAIL (" << failures << ")"
              << std::endl;
    return 1;
}
