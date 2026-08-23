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
    s.initial_residual_measured = true;
    s.initial_residual_finite = true;
    s.residual_first = 1.0e-2;
    s.residual_last = 1.0e-9;
    s.newton_iterations = 4;
    s.newton_iteration_budget = 12;
    s.final_residual_measured = true;
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
        s.initial_residual_measured = true;
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
        s.initial_residual_measured = true;
        s.initial_residual_finite = true;
        s.residual_first = 1.0;
        s.residual_last = 0.4855;
        s.newton_iterations = 3;
        s.newton_iteration_budget = 3;
        s.final_residual_measured = true;
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
        s.initial_residual_measured = true;
        s.initial_residual_finite = true;
        s.residual_first = 1.0;
        s.residual_last = 0.4855;
        s.newton_iterations = 3;
        s.newton_iteration_budget = 12;
        s.final_residual_measured = true;
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

    // ---- R13.5: the review's counter-examples ----
    {   // Absence of a measurement must not become a finding. R0 was derived from
        // isfinite(initial_unscaled_residual), and that member initialises to 0.0 -- finite --
        // so a solve that never evaluated R0 reported R0_finite=1.
        StageFailureSignals s = ok_stage();
        s.initial_residual_measured = false;
        check(name_of(s) == "insufficient_evidence",
              "an R0 that was never measured is INSUFFICIENT EVIDENCE, not a finite one -- "
              "0.0 is finite, which is how the unmeasured case used to read as positive");
        check(std::string(stage_failure_layer(first_failure_of(s))) == "unknown",
              "...and it names no layer, because it has no evidence to name one from");
    }
    {   // Divergence is not stagnation. The total-failure predicate folds raw_rel_error > 1
        // (the residual GREW) together with rel_error >= 0.999 (it did not move).
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.krylov_diverged = true;
        s.gmres_total_failures = 1;
        check(name_of(s) == "krylov_diverged",
              "a linear residual that GREW is divergence, not stagnation -- they arrived as "
              "the same category and point at different work");
    }
    {   // The layer must not claim to have excluded five candidates it never tested.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 1.0;
        const std::string layer = stage_failure_layer(first_failure_of(s));
        check(layer.find("timestep") != std::string::npos &&
                  layer.find("jvp") != std::string::npos &&
                  layer.find("policy") != std::string::npos,
              "a Krylov stall names the FULL disjunction including timestep, JVP and solver "
              "policy: A_h = I - h*gamma*J, so h is in the Krylov operator, and "
              "termination_reason (arnoldi_stagnation / mid_budget_hopeless / restart / nan "
              "retry / max budget) is not yet plumbed to separate policy from operator");
    }
    {   // A stage where a solve succeeded and a later one failed is not the same as one where
        // none succeeded -- best_krylov_rel and gmres_total_failures describe DIFFERENT solves.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 0.3853;   // some solve got here
        s.gmres_total_failures = 1;          // a DIFFERENT solve failed outright
        s.gmres_non_total_failures = 3;
        s.gmres_tolerance_reached = 0;
        check(name_of(s) == "krylov_stagnated" && s.gmres_non_total_failures > 0 &&
                  s.gmres_tolerance_reached == 0,
              "best_krylov_rel and gmres_total_failures describe different solves. The count "
              "is named gmres_non_total_failures because that is what it counts -- a solve "
              "that ended at rho=0.5 without meeting tolerance was being called a success; "
              "gmres_tolerance_reached is the quantity the old name implied, and it is 0 here");
    }

    {   // R13.8: the same defect R13.6 fixed for R0, left in place for the FINAL residual.
        // -1.0 is the not-measured sentinel and it was mapped to NewtonDiverged alongside
        // NaN/Inf -- absence of a measurement reported as the strongest finding available.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.final_residual_measured = false;
        s.residual_last = -1.0;
        check(name_of(s) == "insufficient_evidence",
              "a final residual that was never MEASURED is insufficient evidence, not "
              "divergence -- the -1 sentinel and NaN/Inf are different states");
        StageFailureSignals d = ok_stage();
        d.newton_converged = false;
        d.final_residual_measured = true;
        d.residual_last = std::numeric_limits<double>::infinity();
        check(name_of(d) == "newton_diverged",
              "...while a MEASURED non-finite final residual still IS divergence that "
              "overflowed");
    }

    // ---- R13.11 (referee C7): TIME order, where the record has it ----
    //
    // "First in causal order" was a fixed precedence over stage aggregates with no iteration
    // index -- a modelling choice, not a measurement. With first-event indices the one case
    // the precedence got backwards can be measured: a rejection at iteration 1 that shrank
    // the radius and changed the next solve's x0 and budget, followed by a Krylov failure
    // at iteration 3. Precedence said Krylov; time says the rejection came first.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.gmres_total_failures = 1;
        s.first_krylov_failure_iter = 3;
        s.accepted_steps = 0;
        s.rejected_steps = 2;
        s.first_rejection_iter = 1;
        check(name_of(s) == "all_steps_rejected",
              "a rejection at iteration 1 followed by a Krylov failure at iteration 3 is "
              "reported as the REJECTION -- time order, not precedence");
        s.first_rejection_iter = 5;   // now the Krylov failure came first
        check(name_of(s) == "krylov_stagnated",
              "...and with the Krylov failure first in time, it is reported first");
        s.first_rejection_iter = -1;  // no index recorded: fall back to precedence
        check(name_of(s) == "krylov_stagnated",
              "with no index recorded the old precedence stands, and the record says the "
              "index is -1 rather than implying an order it did not measure");
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

    // R13.12 (red team R3-2): the two Krylov coordinates are not interchangeable.
    // ||r||/||b|| answers "is this step predicted to reduce the NONLINEAR residual" -- on a
    // warm start it can exceed 1 while the linear solve is working fine. ||r||/||r0|| answers
    // "did the solve move". The classifier's no-progress clause read the first under a comment
    // claiming the second, so a solve that made progress was named KrylovStagnated and the
    // work would have gone to the operator/preconditioner instead of to the step policy.
    {
        // The measured em_b_wave failing iteration: r0/||b|| = 1.054, exit 1.02 -- a 3%
        // reduction that reads as "worse than b".
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 4.8e8;
        s.best_krylov_rel_error = 1.02;             // ||r||/||b||  -- above 1
        s.best_krylov_rel_error_vs_r0 = 0.968;      // ||r||/||r0|| -- it moved
        s.first_krylov_failure_rel_vs_r0 = 0.968;   // and that WAS the failing solve
        s.total_failure_vs_b_count = 1;
        s.total_failure_vs_r0_count = 0;
        s.gmres_total_failures = 1;              // by the ||b|| rule, in force by default
        s.accepted_steps = 0;
        s.rejected_steps = 4;
        check(name_of(s) == "all_steps_rejected",
              "a solve that REDUCED its residual is not a stalled solve, however it compares "
              "to ||b||: with r0-relative progress measured, the category is what refused the "
              "step, not the linear solve that produced it");
        check(s.total_failure_vs_b_count != s.total_failure_vs_r0_count,
              "the two readings of the production predicate are BOTH on the record, so a "
              "disagreement between the rules is visible rather than silent");
    }
    {
        // Same solve, nothing rejected, budget ran out while the residual was still falling.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 4.8e8;
        s.best_krylov_rel_error = 1.02;
        s.best_krylov_rel_error_vs_r0 = 0.968;
        s.gmres_total_failures = 1;
        s.accepted_steps = 3;
        s.newton_iterations = 12;
        s.newton_iteration_budget = 12;
        check(name_of(s) == "newton_budget_exhausted",
              "and when nothing refused the step either, a working solve plus a falling "
              "residual at the budget is a budget statement -- the em_b_wave dt=600 case");
    }
    {
        // Genuine stagnation, stated in the coordinate that can express it.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;
        s.best_krylov_rel_error = 0.94;          // looks like progress against ||b||
        s.best_krylov_rel_error_vs_r0 = 0.9995;  // went nowhere from where it started
        s.accepted_steps = 2;
        check(name_of(s) == "krylov_stagnated",
              "the converse case: a warm start whose ||r||/||b|| looks healthy while the "
              "solve made no progress at all -- caught only in r0 coordinates");
    }
    {
        // The fallback: no r0 measurement, old precedence unchanged.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.best_krylov_rel_error = 0.995;
        s.best_krylov_rel_error_vs_r0 = -1.0;    // not measured
        s.gmres_total_failures = 1;
        s.accepted_steps = 1;
        check(name_of(s) == "krylov_stagnated",
              "a record without the r0 measurement keeps the old precedence -- the fix must "
              "not make the classifier weaker on the records it already had");
    }

    // R13.12: WHICH solve's progress. The stage-best is a min over solves and answers "did
    // any solve work"; a first-failure classifier is asking about the solve that first
    // refused. On em_b_wave the cold-start solve reaches 0.55 and the failing one is at
    // iteration 3 -- reading the best would report the early success and clear the stall.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.5e8;
        s.best_krylov_rel_error_vs_r0 = 0.5526;      // iteration 0 solved well
        s.first_krylov_failure_rel_vs_r0 = 0.997;    // iteration 3 went nowhere
        s.gmres_total_failures = 1;
        s.first_krylov_failure_iter = 3;
        s.accepted_steps = 3;
        check(name_of(s) == "krylov_stagnated",
              "a stage whose FIRST failing solve went nowhere is a Krylov stall, however well "
              "an earlier solve did -- the stage-best must not clear a late stall");
    }
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.5e8;
        s.best_krylov_rel_error_vs_r0 = 0.997;       // every solve was poor...
        s.first_krylov_failure_rel_vs_r0 = 0.60;     // ...but the one that FAILED moved
        s.gmres_total_failures = 1;
        s.accepted_steps = 0;
        s.rejected_steps = 5;
        check(name_of(s) == "all_steps_rejected",
              "and the converse: the failing solve's own progress decides, so a poor "
              "stage-best cannot manufacture a stall the first refusal did not show");
    }

    // The layer mapping is the point of the exercise: it says where to work next.
    check(std::string(stage_failure_layer(StageFailure::KrylovStagnated)) ==
              "operator_or_timestep_or_jvp_or_scaling_or_preconditioner_or_policy" &&
          std::string(stage_failure_layer(StageFailure::NewtonDiverged)) ==
              "linearization_or_timestep" &&
          std::string(stage_failure_layer(StageFailure::EntryStateNotFinite)) ==
              "nonfinite_entry_state" &&
          std::string(stage_failure_layer(StageFailure::InitialResidualNotFinite)) ==
              "nonfinite_initial_residual",
          "each category names the LAYER to work on, as data rather than something the "
          "reader reconstructs -- and the entry/R0 layers name what they ACTUALLY exclude. "
          "entry_finite=1 excludes a NaN/Inf entry tensor, not a wrong EOS value, a stagger "
          "or metric sign error, a stale halo or a finite-but-unphysical state; R0_finite=1 "
          "excludes a NaN/Inf first residual, not a wrong RHS, a wrong Jacobian, a bad scale "
          "or a JVP inconsistency");

    constexpr int expected_checks = 39;
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
