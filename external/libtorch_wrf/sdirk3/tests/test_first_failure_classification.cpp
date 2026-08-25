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
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// R13.20 (adversarial loop, iteration 3): a pinned classification whose SIGNALS NO PRODUCTION
// CALL SITE CAN EMIT. It still tests something real -- the classifier's precedence -- but it is
// NOT coverage of production behaviour, and nothing here used to say so.
//
// That distinction is not pedantry: it is how a dead branch survived two increments. The
// `krylov_budget_exhausted` fixture reached its clause from `all_near_worst_met_tolerance = true`
// with `worst_krylov_met_tolerance = false`, a pair the solver cannot produce, so the category
// read as covered in CI while never firing -- and it was even RENAMED, with a new fixture, on a
// branch production never reached. Marking these in the OUTPUT, not just in a comment, means the
// next reader sees the difference without re-deriving it.
int reserved_count = 0;
void check_reserved(bool ok, const std::string& what) {
    ++check_count;
    ++reserved_count;
    std::cout << (ok ? "  ok(RESERVED) " : "  FAIL(RESERVED) ") << what << std::endl;
    if (!ok) ++failures;
}

using wrf::sdirk3::first_failure_of;
using wrf::sdirk3::StageFailure;
using wrf::sdirk3::stage_failure_layer;
using wrf::sdirk3::stage_failure_name;
using wrf::sdirk3::StageFailureSignals;

// R13.23 (deep review P0-4): a COMPLETE exit receipt. The attribution is gated on completeness
// now, so a fixture that wants a subtype must supply a receipt that could have produced one --
// which is the contract, not a chore. Three fixtures previously asserted an attribution from a
// receipt carrying only the two reached flags.
static void give_complete_exit_receipt(StageFailureSignals& s, double rho_D, double rho_S,
                                       double tol, wrf::sdirk3::KrylovStoppingMetric metric) {
    s.exit_rho_stop_final = rho_D;
    s.exit_rho_S_final = rho_S;
    s.exit_stopping_metric = metric;
    s.exit_tolerance_applied = tol;
    s.exit_D_reached = (rho_D < tol);
    s.exit_S_reached = (rho_S < tol);
    s.exit_arnoldi_spent = 7;
    s.exit_arnoldi_allowed = 8;
}


// A clean, converged, published stage.
StageFailureSignals ok_stage() {
    StageFailureSignals s;
    // R13.17 (external review P0-4): a properly STAMPED record. The provenance gate now refuses a
    // missing stamp as well as a mismatched one, so every fixture must say which stage owns its
    // signals -- which is the point: an unstamped record used to run the whole classifier.
    s.signals_from_stage = 2;
    s.classifying_stage = 2;
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
        check_reserved(name_of(s) == "publish_rejected",
              "everything converged and admissible, and the state was still not published -- "
              "RESERVED: the one production call site (handle_stage_gate) sets state_published "
              "false unconditionally because it is upstream of any publish, AND is entered only "
              "when the gate is already unhappy, so a converged stage there always answers "
              "admissibility_rejected first. Kept for a publish-site classifier call that does "
              "not exist yet");
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
        s.signals_from_stage = 2; s.classifying_stage = 2;  // R13.17: stamped
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
        s.signals_from_stage = 2; s.classifying_stage = 2;  // R13.17: stamped
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
        s.best_krylov_rel_error = 1.02;              // ||r||/||b||  -- above 1
        s.best_krylov_rel_error_vs_r0 = 0.80;
        s.worst_krylov_rel_error_vs_r0 = 0.868;      // even the WORST solve moved 13%
        s.krylov_solves_measured_vs_r0 = 4;
        s.total_failure_vs_b_count = 1;
        s.total_failure_vs_r0_count = 0;
        s.gmres_total_failures = 1;              // by the ||b|| rule, in force by default
        s.accepted_steps = 0;
        s.rejected_steps = 4;
        check(name_of(s) == "all_steps_rejected",
              "a solve that REDUCED its residual is not a stalled solve, however it compares "
              "to ||b||: with r0-relative progress measured, the category is what refused the "
              "step, not the linear solve that produced it");
    }
    {
        // Same solve, nothing rejected, budget ran out while the residual was still falling.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 4.8e8;
        s.best_krylov_rel_error = 1.02;
        s.best_krylov_rel_error_vs_r0 = 0.55;
        s.worst_krylov_rel_error_vs_r0 = 0.868;
        s.krylov_solves_measured_vs_r0 = 12;
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
        s.best_krylov_rel_error = 0.94;           // looks like progress against ||b||
        s.best_krylov_rel_error_vs_r0 = 0.9995;   // went nowhere from where it started
        s.worst_krylov_rel_error_vs_r0 = 0.9995;
        s.krylov_solves_measured_vs_r0 = 1;
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
        s.best_krylov_rel_error_vs_r0 = -1.0;     // not measured
        s.worst_krylov_rel_error_vs_r0 = -1.0;    // not measured
        s.gmres_total_failures = 1;
        s.accepted_steps = 1;
        check(name_of(s) == "krylov_stagnated",
              "a record without the r0 measurement keeps the old precedence -- the fix must "
              "not make the classifier weaker on the records it already had");
    }

    // R13.13 (red team round 4): the MAX over solves, and a threshold calibrated for the
    // coordinate it is applied in. Reusing the ||b||-coordinate 0.99 in r0 coordinates is what
    // made the case below classify as newton_stagnated -- routing twelve consecutive 2% solves
    // to "the split", the most expensive wrong answer this campaign has available.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;                     // the outer iteration fell 1.1%
        s.best_krylov_rel_error_vs_r0 = 0.98;        // every solve removed 2%...
        s.worst_krylov_rel_error_vs_r0 = 0.98;       // ...and none did better
        s.krylov_solves_measured_vs_r0 = 12;
        s.gmres_total_failures = 12;
        s.accepted_steps = 12;
        s.newton_iterations = 12;
        s.newton_iteration_budget = 12;
        check(name_of(s) == "krylov_stagnated",
              "twelve consecutive solves each removing 2% of their own residual is a Krylov "
              "stall; a 0.99 threshold inherited from ||b|| coordinates calls it healthy and "
              "sends the work to the split");
    }
    {
        // The aggregate, not the threshold: one early success must not clear a late stall.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.5e8;
        s.best_krylov_rel_error_vs_r0 = 0.5526;      // iteration 0 solved well
        s.worst_krylov_rel_error_vs_r0 = 0.997;      // a later one went nowhere
        s.krylov_solves_measured_vs_r0 = 4;
        s.worst_krylov_iter = 3;
        s.accepted_steps = 3;
        check(name_of(s) == "krylov_stagnated",
              "the stage-best is a MIN and answers 'did any solve work'; the max is what "
              "answers 'did the linear solve stop working', and the record names the "
              "iteration it happened at");
    }
    {
        // Boundary, in the coordinate the constant lives in.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 3;
        s.worst_krylov_rel_error_vs_r0 = 0.90;
        const bool at_boundary = (name_of(s) == "krylov_stagnated");
        s.worst_krylov_rel_error_vs_r0 = 0.8999;
        // R13.14 (round 5, R5-18a): NAME the category below the boundary. `!= "krylov_stagnated"`
        // would pass if a future change made it `insufficient_evidence`.
        const bool below = (name_of(s) == "newton_stagnated");
        s.worst_krylov_rel_error_vs_r0 = 0.9001;
        const bool above = (name_of(s) == "krylov_stagnated");
        check(at_boundary && below && above,
              "the r0 no-progress boundary is pinned at 0.90 from both sides and AT the "
              "constant, so a later coordinate change cannot move it unnoticed");
    }
    {
        // Divergence still outranks the progress clause.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;
        s.krylov_diverged = true;
        s.worst_krylov_rel_error_vs_r0 = 0.20;       // by the progress test, healthy
        s.krylov_solves_measured_vs_r0 = 5;
        s.accepted_steps = 2;
        check(name_of(s) == "krylov_diverged",
              "a solve whose residual GREW is diverged even when the stage's worst progress "
              "ratio looks healthy -- divergence is upstream of stagnation, and the new "
              "progress clause must not preempt it");
    }
    {
        // The ||b||-coordinate second clause, in the branch where it still applies.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.worst_krylov_rel_error_vs_r0 = -1.0;       // r0 never measured
        s.gmres_total_failures = 0;                  // and nothing tripped the predicate
        s.best_krylov_rel_error = 0.995;             // so only the ||b|| reading is left
        s.accepted_steps = 2;
        check(name_of(s) == "krylov_stagnated",
              "with no r0 measurement and no total failure, the ||b|| reading is the only "
              "progress statement available and still fires -- the fallback branch is not "
              "dead code");
    }

    // R13.14 (red team round 5): the max must be over solves that DID WORK AND DID NOT
    // FINISH. A solve that converged on entry does zero work and returns rel_error equal to
    // its own r0 ratio, so its progress is EXACTLY 1.0 -- under a max, the best possible
    // outcome scored as a total stall. The solver excludes those; these fixtures pin what the
    // classifier must do with the record that results.
    {
        // Every solve either converged on entry or reached tolerance: NO stagnation evidence.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 4.8e8;                     // and the residual is still falling
        s.worst_krylov_rel_error_vs_r0 = -1.0;       // nothing survived the exclusion
        s.krylov_solves_measured_vs_r0 = 0;
        s.krylov_solves_trivial = 3;
        s.accepted_steps = 3;
        s.newton_iterations = 3;
        s.newton_iteration_budget = 3;
        check(name_of(s) == "newton_budget_exhausted",
              "a stage whose every solve converged on entry or reached tolerance has NO Krylov "
              "evidence, and a falling residual at the budget is a budget statement -- the "
              "category that exists to stop exactly this being called a stall");
    }
    {
        // The boundary is movable per run, and the record says which one was used.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 3;
        s.worst_krylov_rel_error_vs_r0 = 0.8795;     // the tree's measured default-budget value
        const bool at_default = (name_of(s) != "krylov_stagnated");
        // R13.16 (round 6, R6-4): the classifier applies a threshold only when the site that
        // sets it was REACHED -- the same flag the emitter prints. Two predicates on two
        // different fields let a row say `not_reached` while 0.90 silently decided the category.
        s.krylov_threshold_observed = true;
        s.krylov_no_progress_threshold = 0.85;       // a stricter reading of the same record
        const bool at_override = (name_of(s) == "krylov_stagnated");
        s.krylov_no_progress_threshold = -1.0;
        const bool back_to_default = (name_of(s) != "krylov_stagnated");
        check(at_default && at_override && back_to_default,
              "the measured 0.8795 sits 2.3% below the default boundary and flips under a "
              "stricter one, so the constant is reachable per run -- and an unset threshold "
              "classifies exactly as it did before the knob existed");
    }
    {
        // An out-of-range threshold must not become the rule.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8;
        s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 3;
        s.worst_krylov_rel_error_vs_r0 = 0.95;
        s.krylov_no_progress_threshold = 0.0;        // nonsense: would call everything a stall
        const bool zero_ignored = (name_of(s) == "krylov_stagnated");   // via the DEFAULT 0.90
        s.krylov_no_progress_threshold = 2.0;        // nonsense: would call nothing a stall
        const bool big_ignored = (name_of(s) == "krylov_stagnated");
        check(zero_ignored && big_ignored,
              "an out-of-range threshold falls back to the header constant rather than becoming "
              "the rule -- a knob that can silently disable a category is worse than no knob");
    }

    // R13.15 (external review P1-4): PROVENANCE is consumed, not printed beside the answer.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_last = 9.0e-3;
        s.worst_krylov_rel_error_vs_r0 = 0.999;      // would be a confident krylov_stagnated
        s.krylov_solves_measured_vs_r0 = 3;
        s.signals_from_stage = 2;
        s.classifying_stage = 3;                     // ...from the WRONG stage's evidence
        check(name_of(s) == "stage_signal_mismatch",
              "classifying stage 3 from stage 2's signals produces a confident WRONG layer; a "
              "provenance mismatch is not weak evidence, it is the wrong evidence");
        s.classifying_stage = 2;                     // matched
        check(name_of(s) == "krylov_stagnated",
              "and with provenance matched the same record classifies normally -- the gate is "
              "a refusal, not a new failure mode");
    }
    {
        // An explicit (ARK) stage runs no Newton, so every implicit signal is at its default.
        StageFailureSignals s;
        s.signals_from_stage = 2; s.classifying_stage = 2;  // R13.17: stamped
        s.is_explicit_stage = true;
        s.explicit_rhs_measured = true;
        s.explicit_rhs_finite = false;
        check(name_of(s) == "explicit_rhs_not_finite",
              "an explicit stage whose RHS blew up says so, instead of collapsing into "
              "insufficient_evidence because it has no Newton residual to report");
        s.explicit_rhs_finite = true;
        s.gate_metric_ok = false;
        check_reserved(name_of(s) == "explicit_admissibility_rejected",
              "and its gate refusal is its own category, on its own layer -- RESERVED: reaching "
              "it needs a FINITE explicit tendency with a bad gate metric, but a finite tendency "
              "sets all three gate metrics to 0 and the gate early-returns");
        s.gate_metric_ok = true;
        s.state_published = false;
        check_reserved(name_of(s) == "explicit_publish_rejected",
              "as is its publish refusal -- RESERVED, same reason as publish_rejected above");
        s.state_published = true;
        check_reserved(name_of(s) == "none",
              "a clean explicit stage is clean -- the branch must not manufacture a failure. "
              "RESERVED: the gate is not entered at all when nothing is wrong");
    }
    {
        StageFailureSignals s;
        s.signals_from_stage = 2; s.classifying_stage = 2;  // R13.17: stamped
        s.is_explicit_stage = true;
        s.explicit_rhs_measured = false;
        check(name_of(s) == "insufficient_evidence",
              "an explicit stage whose RHS was never measured is unmeasured, not finite -- "
              "absence of a measurement must not become a finding here either");
        // R13.20 (adversarial loop, iteration 2): the PRODUCER-REACHABLE explicit failure. The
        // gate fires on a non-finite explicit tendency by setting converged=false AND all three
        // metrics to infinity, so the classifier sees `gate_metric_ok = false` alongside
        // `explicit_rhs_finite = false` -- and the RHS answer must win, because it is the earlier
        // cause. Until R13.20 nothing produced these signals and this returned
        // `insufficient_evidence` on every run.
        {
            StageFailureSignals e;
            e.signals_from_stage = 1; e.classifying_stage = 1;
            e.is_explicit_stage = true;
            e.explicit_rhs_measured = true;
            e.explicit_rhs_finite = false;
            e.gate_metric_ok = false;      // what the infinite metrics produce at the gate
            e.state_published = false;     // the gate is upstream of any publish
            check(name_of(e) == "explicit_rhs_not_finite",
                  "a non-finite explicit tendency is reported AS THAT, not as the admissibility "
                  "or publish rejection that the same signals also satisfy -- the RHS is the "
                  "earlier cause, and before R13.20 this stage was never gated at all");
        }
        // R13.20: and the explicit stage's own gates are their OWN evidence class, not the
        // implicit machinery's preconditions -- the record must not label them `precondition`.
        const auto dx = wrf::sdirk3::stage_diagnosis_of(s);
        check(dx.primary_event_basis == wrf::sdirk3::StageDecisionBasis::ExplicitStageGate &&
              std::string(wrf::sdirk3::stage_decision_basis_name(
                  wrf::sdirk3::StageDecisionBasis::ExplicitStageGate)) == "explicit_stage_gate",
              "an explicit-stage verdict names the explicit-stage gate as its basis");
        check(!dx.attribution_from_metric && !dx.decided_by_exit_receipt,
              "...and claims neither r0 metric evidence nor an exit receipt, neither of which "
              "an explicit stage has");
    }

    {
        // R13.16 (round 6, R6-4): a threshold value with no observation flag is this struct's
        // DEFAULT, not a measurement, and must not decide the category.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 3;
        s.worst_krylov_rel_error_vs_r0 = 0.8795;
        s.krylov_threshold_observed = false;
        s.krylov_no_progress_threshold = 0.10;       // would call everything a stall...
        check(name_of(s) != "krylov_stagnated",
              "...but the site was never reached, so the header constant applies and the "
              "emitter's `not_reached` and the classifier's answer agree");
    }
    {
        // R13.16 (round 6, R6-2): "reached tolerance" is not evidence the solve worked. The
        // tolerance is the Eisenstat-Walker forcing term, CAPPED AT 0.9 and saturating there
        // exactly when the Newton residual stops falling -- so a solve can meet it having
        // removed 10%. Excluding those from the max was the inverse of the round-5 P0: that one
        // scored a no-op as a stall, this one discounted a stall as a success.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 5;
        s.accepted_steps = 5;
        s.worst_krylov_rel_error_vs_r0 = 0.926;      // removed 7%
        s.worst_krylov_met_tolerance = true;         // ...and was told that was enough
        s.worst_krylov_eta = 0.9;
        check(name_of(s) == "krylov_forcing_term_limited",
              "a solve that made no progress because its own forcing term did not ask for more "
              "is not the operator's failure -- naming it krylov_stagnated sends the work to the "
              "preconditioner, and the layer actually responsible had no category at all");
        // R13.18 (deep review P0-2): the CATEGORY layer is source-neutral, and the specific
        // source turns into a layer through `krylov_forcing_layer_for`. It used to name
        // Eisenstat-Walker unconditionally while the tolerance could have come from a stage
        // override or a ramp -- the source was produced, emitted, and read by nothing.
        check(std::string(stage_failure_layer(StageFailure::KrylovForcingTermLimited)) ==
                  "krylov_tolerance_policy_or_inner_budget",
              "the category's own layer names the tolerance policy without asserting WHICH one");
        check(std::string(wrf::sdirk3::krylov_forcing_layer_for(
                  wrf::sdirk3::KrylovToleranceSource::StageOverride)) ==
                  "stage_tolerance_override" &&
              std::string(wrf::sdirk3::krylov_forcing_layer_for(
                  wrf::sdirk3::KrylovToleranceSource::EisenstatWalker)) ==
                  "eisenstat_walker_forcing" &&
              std::string(wrf::sdirk3::krylov_forcing_layer_for(
                  wrf::sdirk3::KrylovToleranceSource::Unknown)) ==
                  "inner_tolerance_source_unrecorded",
              "...and the recorded source selects the specific layer, with an UNRECORDED source "
              "saying so rather than defaulting to a named mechanism");
        s.worst_krylov_met_tolerance = false;        // it stopped because it could not progress
        check(name_of(s) == "krylov_stagnated",
              "while the same ratio from a solve that did NOT meet its tolerance is a stall");
    }
    {
        // R13.16 (round 6, R6-3): `worst == -1` had four causes collapsed into the ||b|| rule.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 4.8e8;   // still falling
        s.worst_krylov_rel_error_vs_r0 = -1.0;
        s.krylov_solves_trivial = 4;                 // every solve did zero work
        s.gmres_total_failures = 1;                  // the ||b|| rule would fire on this
        s.accepted_steps = 4;
        s.newton_iterations = 12; s.newton_iteration_budget = 12;
        check(name_of(s) == "newton_budget_exhausted",
              "a stage whose every solve did zero work has NO Krylov evidence, so the ||b|| "
              "rule -- the coordinate this line of work exists to leave -- must not be applied "
              "to it by default");
        s.krylov_solves_trivial = 0;
        s.krylov_r0_unmeasured_solves = 4;           // the other separable cause
        check(name_of(s) == "newton_budget_exhausted",
              "and the same for a stage where no solve measured r0");
        s.krylov_r0_unmeasured_solves = 0;           // neither: an old record
        check(name_of(s) == "krylov_stagnated",
              "while a record that simply predates the field keeps the old precedence, so the "
              "classifier does not get weaker on evidence it already had");
    }

    // R13.17 (external review P0-4): fail-closed on ABSENCE, not only on disagreement.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.worst_krylov_rel_error_vs_r0 = 0.999;
        s.krylov_solves_measured_vs_r0 = 3;
        s.signals_from_stage = -1;                   // the documented "not stamped" sentinel
        check(name_of(s) == "stage_signal_missing",
              "an UNSTAMPED record cannot be classified: the gate required both stamps >= 0, so "
              "the sentinel skipped it and the whole classifier ran on signals of unknown owner");
        s.signals_from_stage = 2; s.classifying_stage = -1;
        check(name_of(s) == "stage_signal_missing",
              "and the same when the classifying stage is the one that was never stamped");
    }
    // R13.17 (external review P0-2): FOUR reasons a solve can show no progress, four layers.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 4;
        s.accepted_steps = 4;
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        // The decisive synthetic case the review names: rho_D = 0.85 < eta = 0.9 while
        // rho_S = 0.99 > eta. The solve minimised WHAT IT WAS ASKED TO and the result is still
        // useless to the Newton merit.
        s.worst_krylov_D_reached = true;
        s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = true;         // InternalConvergenceStop is a tolerance
        s.worst_krylov_tolerance_source = wrf::sdirk3::KrylovToleranceSource::EisenstatWalker;
        check(name_of(s) == "krylov_objective_mismatch",
              "D reached and S not is neither a stall (the solve worked) nor a forcing-term "
              "problem (tightening eta does not align two objectives) -- and it is exactly the "
              "InternalConvergenceStop state the round-6 rule sent back to krylov_stagnated");
        // R13.19 (P0-4): the category's layer is metric-NEUTRAL, and the specific one is derived
        // from the metric the solve actually stopped on -- it used to say "D" while the WRMS
        // experiment makes the satisfied metric E^-1 S at the Newton linearization point.
        check(std::string(stage_failure_layer(StageFailure::KrylovObjectiveMismatch)) ==
                  "krylov_stop_metric_vs_newton_merit",
              "the category's layer names the stop metric against the Newton merit without "
              "asserting WHICH metric");
        check(std::string(wrf::sdirk3::krylov_stopping_layer_for(
                  wrf::sdirk3::KrylovStoppingMetric::StageWRMS)) ==
                  "newton_WRMS_E_vs_newton_merit" &&
              std::string(wrf::sdirk3::krylov_stopping_layer_for(
                  wrf::sdirk3::KrylovStoppingMetric::BlockD)) ==
                  "block_D_vs_newton_merit" &&
              std::string(wrf::sdirk3::krylov_stopping_layer_for(
                  wrf::sdirk3::KrylovStoppingMetric::Unknown)) ==
                  "stop_metric_unrecorded",
              "...and the RECORDED metric selects the specific one, with an unrecorded metric "
              "saying so rather than defaulting to block D");

        s.worst_krylov_D_reached = true; s.worst_krylov_S_reached = true;
        check(name_of(s) == "krylov_forcing_term_limited",
              "with BOTH metrics satisfied and progress still poor, the forcing term asked for "
              "little -- that is the policy layer");

        s.worst_krylov_D_reached = false; s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = false;
        s.all_near_worst_met_tolerance = false;   // R13.20: what the producer emits alongside
        s.worst_krylov_budget_exhausted = true;
        // R13.19 (P1-2): the category states the FACT (the budget ran out), not the CAUSE
        // ("still descending when cut off"), which nothing measures -- a solve flat from its
        // first restart reaches this branch identically.
        check(name_of(s) == "krylov_budget_exhausted",
              "neither tolerance met and the budget gone is a solve cut off while still "
              "descending -- the inner budget, not the operator");

        s.worst_krylov_budget_exhausted = false;
        check(name_of(s) == "krylov_stagnated",
              "and only when it met nothing and was not cut off is the operator implicated");
    }
    {
        // R13.17 (external review P0-2): a TIE must not be resolved by arrival order.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 4;
        s.accepted_steps = 4;
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        s.worst_krylov_D_reached = true;
        s.worst_krylov_met_tolerance = true;
        // R13.20 (round 9, R9-6 self-review): the refusal is expressed by the signal that means
        // TIE DISAGREEMENT. A tied solve that met nothing implies `krylov_stagnated` while this
        // worst receipt implies `krylov_objective_mismatch`, so the producer sets this flag.
        s.near_worst_mechanism_ambiguous = true;
        check(name_of(s) == "krylov_stagnated",
              "when a solve tied at the worst ratio implies a DIFFERENT category, the "
              "forcing-term and objective-mismatch readings are refused -- with eta saturated at "
              "its cap a tie is not a remote case, and a strict `>` let whichever arrived first "
              "name the layer");
        // ...and the aggregate `all_near_worst_met_tolerance` no longer decides anything. It is
        // false on EVERY stage whose worst solve met nothing -- including stages where the tied
        // solves agree perfectly -- and while it gated this clause it made two of the four
        // answers below unreachable in production.
        s.near_worst_mechanism_ambiguous = false;
        s.all_near_worst_met_tolerance = false;
        check(name_of(s) == "krylov_objective_mismatch",
              "an UNMET tie that agrees on the mechanism is not ambiguous, and the specific "
              "category is named rather than refused");
    }
    {
        // R13.20 (round 9, R9-6 self-review): `krylov_budget_exhausted` from a state the SOLVER
        // CAN EMIT. The fixture that used to pin it set `worst_krylov_met_tolerance = false`
        // while leaving `all_near_worst_met_tolerance` at its `true` default -- and the producer
        // cannot emit that pair, because `near_worst_all_met` compares `worst_unmet` against
        // `worst` and those are the same number when the strictly-worst solve is the unmet one.
        // So the old guard returned `krylov_stagnated` before this clause on every real record,
        // and the category read as covered while never firing.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.krylov_solves_measured_vs_r0 = 4;
        s.accepted_steps = 4;
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        s.worst_krylov_D_reached = false;
        s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = false;
        s.all_near_worst_met_tolerance = false;   // what the producer ACTUALLY emits here
        s.worst_krylov_budget_exhausted = true;
        check(name_of(s) == "krylov_budget_exhausted",
              "neither tolerance met and the budget gone is the inner budget, not the operator -- "
              "and it is now reachable from the signal combination the solver produces");
        s.worst_krylov_budget_exhausted = false;
        check(name_of(s) == "krylov_stagnated",
              "and only when it met nothing and was not cut off is the operator implicated");
    }

    // R13.17 (external review P0-3): the loop's OWN exit reason outranks the reconstruction.
    {
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;   // flat: reconstruction says "stall"
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 3;
        s.newton_iterations = 12; s.newton_iteration_budget = 12;
        s.newton_termination = wrf::sdirk3::NewtonTerminationReason::BudgetExhausted;
        check(name_of(s) == "newton_budget_exhausted",
              "a flat residual at the bound is 'stopped moving' to the aggregate reconstruction "
              "and 'ran out of budget' to the loop itself -- and the loop is the one that knows; "
              "the campaign's reading of the 12x-budget run rests on exactly this distinction");
        s.newton_termination = wrf::sdirk3::NewtonTerminationReason::ResidualStall;
        check(name_of(s) == "newton_stagnated",
              "and the same aggregates with a RECORDED stall are a stall");
        s.newton_termination = wrf::sdirk3::NewtonTerminationReason::NotRecorded;
        check(name_of(s) == "newton_stagnated",
              "an unrecorded reason falls back to the old precedence, so records taken before "
              "the field classify exactly as they did");
    }

    {
        // R13.17, MEASURED: the em_b_wave 12x-budget run. The ratio fell below the boundary and
        // the category became `newton_stagnated`, layer `residual_floor_or_split` -- the
        // split-explicit rebuild -- while the loop's own exit was LinearSolveFailure ("[Newton]
        // GMRES total failure + zero update"), the SAME exit as the default-budget run. Only the
        // ratio moved. The campaign read that flip as "the failure moved outward to Newton".
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.worst_krylov_rel_error_vs_r0 = 0.8622;     // BELOW the 0.90 boundary
        s.krylov_solves_measured_vs_r0 = 3;
        s.accepted_steps = 2; s.rejected_steps = 1;
        s.newton_iterations = 3; s.newton_iteration_budget = 12;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        check(name_of(s) == "zero_update_after_total_failure",
              "the exit names WHAT IT OBSERVED -- a zero update after a total-failure flag -- and "
              "not a mechanism. Calling it a Krylov stall claimed the linear solve produced "
              "nothing, while the same run's worst solve removed 13.8% of its own residual, and "
              "the flag gating it is the ||b|| predicate this classifier spent four rounds "
              "moving off");
        s.newton_termination = wrf::sdirk3::NewtonTerminationReason::ResidualStall;
        check(name_of(s) == "newton_stagnated",
              "while the same ratio with a RECORDED residual stall is the outer iteration -- the "
              "two are separated by the exit reason, not by the threshold");
    }

    {
        // R13.17 SELF-REVIEW: the two holes the first version of the linear-failure fix left.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.worst_krylov_rel_error_vs_r0 = -1.0;       // NO solve measured r0
        s.krylov_r0_unmeasured_solves = 3;
        s.accepted_steps = 0; s.rejected_steps = 3;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        check(name_of(s) == "zero_update_after_total_failure",
              "the exit is honoured with NO Krylov evidence too, and still only claims what it "
              "observed; its layer names both candidates (the ||b|| rule and the step recovery) "
              "rather than picking one");
        s.newton_termination = wrf::sdirk3::NewtonTerminationReason::Exception;
        check(name_of(s) == "krylov_solve_threw",
              "and an exception thrown by the linear solve is not the outer iteration's failure "
              "-- nor is it DIVERGENCE, which is a measured behaviour an exception does not "
              "establish; the Exception enum value had ZERO producers until this review");
    }

    // R13.18 (deep review P0-3): the near-worst fold must be ORDER-INDEPENDENT. The reviewer's
    // counterexample, both permutations, plus the properties the fold has to have.
    {
        using wrf::sdirk3::NearWorstFold;
        using wrf::sdirk3::near_worst_accumulate;
        struct Solve { double p; bool met; };
        auto fold = [](std::vector<Solve> v) {
            NearWorstFold st;
            for (const auto& x : v) st = near_worst_accumulate(st, x.p, x.met);
            return st;
        };
        auto all_met_of = [&](std::vector<Solve> v) {
            return wrf::sdirk3::near_worst_all_met(fold(std::move(v)));
        };
        const Solve A{0.90, false}, B{0.99, true};
        const auto ab = fold({A, B});
        const auto ba = fold({B, A});
        check(all_met_of({A, B}) == all_met_of({B, A}) && ab.worst == ba.worst &&
              all_met_of({A, B}),
              "A(0.90,not-met) then B(0.99,met) must equal B then A -- the streaming version gave "
              "false one way and true the other, and that verdict decides whether the "
              "forcing-term and objective-mismatch categories may be read at all");

        // A strictly worse solve outside the band REPLACES the set; it does not inherit it.
        check(all_met_of({{0.50, false}, {0.99, true}}),
              "a clearly-better earlier solve is not in the final tie set and must not poison it");
        // ...and a solve inside the band JOINS it.
        check(!all_met_of({{0.9895, false}, {0.99, true}}),
              "a solve within the tie band of the final worst IS in the set, so one that met no "
              "tolerance makes the set ambiguous");
        // Order-independence of the joining case too.
        check(all_met_of({{0.9895, false}, {0.99, true}}) ==
                  all_met_of({{0.99, true}, {0.9895, false}}),
              "and that holds in either arrival order");
        // R13.19 (precision review P0-3): the THREE-ELEMENT CHAIN, all six permutations.
        // "Near" is NON-TRANSITIVE -- A~B and B~C with A!~C -- so a running boolean cannot
        // retract A once C arrives. The R13.18 fold gave `false` on two of these six and `true`
        // on four; the correct answer is `true`, since the final worst is 1.0 and A at 0.9985
        // sits outside the band (>= 0.999).
        {
            const Solve X{0.9985, false}, Y{0.9994, true}, Z{1.0000, true};
            const std::vector<std::vector<Solve>> perms = {
                {X, Y, Z}, {X, Z, Y}, {Y, X, Z}, {Y, Z, X}, {Z, X, Y}, {Z, Y, X}};
            bool all_true = true, agree = true;
            const bool first = all_met_of(perms[0]);
            for (const auto& v : perms) {
                const bool r = all_met_of(v);
                agree = agree && (r == first);
                all_true = all_true && r;
            }
            check(agree && all_true,
                  "all six permutations of a three-element chain agree, and agree on TRUE -- the "
                  "solve that met nothing is outside the final tie band, and a running boolean "
                  "could not retract it because near-ness is not transitive");
        }
        // A single solve is its own set.
        check(!all_met_of({{0.99, false}}) && all_met_of({{0.99, true}}),
              "the first solve starts the set rather than inheriting the `true` default");
    }

    {
        // R13.18 (deep review P0-4): a terminal event is subtyped by the solve that ENDED the
        // loop, not by the stage's largest-ratio solve -- they need not be the same iteration.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        // Stage-worst says iteration 0: D met, S not -- an objective mismatch THERE.
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        s.worst_krylov_D_reached = true; s.worst_krylov_S_reached = false;
        s.krylov_solves_measured_vs_r0 = 4;
        // ...but the solve that ENDED the loop, at iteration 3, met neither.
        s.exit_krylov_iter = 3;
        s.exit_D_reached = false; s.exit_S_reached = false;
        check(name_of(s) == "zero_update_after_total_failure",
              "the exit solve met no tolerance, so the terminal event keeps its own name -- "
              "reading the stage-worst receipt here would report an objective mismatch from an "
              "iteration that ended nothing");
        // R13.23: a complete receipt -- D met, S not, judged against one tolerance.
        give_complete_exit_receipt(s, /*rho_D=*/0.85, /*rho_S=*/1.05, /*tol=*/0.9,
                                   wrf::sdirk3::KrylovStoppingMetric::BlockD);
        // R13.21 (external review P0-2): the event does not change; the ATTRIBUTION does. This
        // check used to read the subtype out of `name_of`, i.e. out of the event.
        check(name_of(s) == "zero_update_after_total_failure",
              "the event is still the zero-update break -- a subtype earned by the exit solve is "
              "a statement about WHY, not about what ended the stage");
        check(wrf::sdirk3::stage_diagnosis_of(s).exit_attribution ==
                  StageFailure::KrylovObjectiveMismatch,
              "and when the exit solve itself met D and not S, the subtype is earned by the "
              "solve the event belongs to -- carried as the exit attribution");
        s.exit_krylov_iter = -1;   // no exit receipt at all
        check(name_of(s) == "zero_update_after_total_failure",
              "with no exit receipt the event may not borrow the stage-worst one");
    }

    {
        // R13.18 (deep review P0-1 remainder): the THIRD metric. The stage gate accepts on
        // ||E^-1 R||, so a solve can satisfy both recorded metrics and still be refused -- a state
        // the receipt could not express, and therefore a state with no category.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        s.exit_krylov_iter = 2;
        give_complete_exit_receipt(s, /*rho_D=*/0.5, /*rho_S=*/0.6, /*tol=*/0.9,
                                   wrf::sdirk3::KrylovStoppingMetric::BlockD);
        s.exit_rho_E_final = 0.7; s.exit_E_reached = false;
        // R13.19 (precision review P0-2): this fixture used to assert the mismatch on a record
        // inheriting `gate_metric_ok = true` from ok_stage() -- CI pinning a contract that
        // reported the gate refusing while the record said it passed. The gate must actually
        // have refused, and the category is named for the ENTRY metric it measures, not the gate.
        s.gate_metric_ok = false;
        // R13.21 (external review P0-2): the EVENT stays the event. This fixture used to pin the
        // subtype as `name_of(s)`, i.e. as the thing that ended the stage -- which is how a field
        // documented "what actually ended the stage" came to report a cause. The subtype is now
        // `exit_attribution`, checked below.
        check(name_of(s) == "zero_update_after_total_failure",
              "the loop ended at the zero-update break, and that is what the EVENT says -- the "
              "metric subtype is a separate answer to a separate question");
        {
            const auto d_em = wrf::sdirk3::stage_diagnosis_of(s);
            check(d_em.exit_attribution == StageFailure::KrylovEntryMetricMismatch,
                  "both metrics the solver was steered by are satisfied and the GATE's metric is "
                  "not -- not the operator, not the forcing term, not the budget, but the gate's "
                  "norm disagreeing with the solver's, recorded as the exit attribution");
        }
        check(std::string(stage_failure_layer(StageFailure::KrylovEntryMetricMismatch)) ==
                  "krylov_entry_E_metric_vs_solver_metrics",
              "and the layer names that disagreement rather than a component");
        s.exit_E_reached = true;
        check(name_of(s) != "krylov_entry_metric_mismatch",
              "with the gate's metric also satisfied there is no mismatch to report");
        s.exit_E_reached = false; s.exit_rho_E_final = -1.0;
        check(name_of(s) != "krylov_entry_metric_mismatch",
              "and an UNMEASURED rho_E may not produce the finding -- absence of a measurement "
              "must not become one, here either");
    }

    {
        // R13.19 (P0-2): with the gate recorded as PASSING, the entry-metric seam may not be
        // reported as a failure -- the record would be claiming a refusal that did not happen.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        s.exit_krylov_iter = 2;
        give_complete_exit_receipt(s, /*rho_D=*/0.5, /*rho_S=*/0.6, /*tol=*/0.9,
                                   wrf::sdirk3::KrylovStoppingMetric::BlockD);
        s.exit_rho_E_final = 0.7; s.exit_E_reached = false;
        s.gate_metric_ok = true;                 // the gate did NOT refuse
        check(name_of(s) != "krylov_entry_metric_mismatch",
              "a seam between the solver's metrics is not a refusal, and the classifier may not "
              "report one while the gate is recorded as having passed");
    }

    {
        // R13.19 (precision review P1-4): EVENT and CAUSE are two questions. The branch comment
        // said a recorded event "must not override the r0 evidence" while the code returned,
        // which overrides it. Both answers are now reported.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        s.worst_krylov_rel_error_vs_r0 = 0.999;     // the metric evidence says STALL
        s.krylov_solves_measured_vs_r0 = 4;
        s.accepted_steps = 3;
        const auto d = wrf::sdirk3::stage_diagnosis_of(s);
        check(std::string(wrf::sdirk3::stage_failure_name(d.primary_event)) ==
                  "zero_update_after_total_failure",
              "the primary slot is what actually ended the stage");
        check(std::string(wrf::sdirk3::stage_failure_name(d.attribution)) ==
                  "krylov_stagnated" && d.attribution_measured &&
                  d.attribution_from_metric,
              "...and the r0 evidence is PRESERVED beside it rather than discarded by it -- the "
              "event outranking the reconstruction no longer means throwing it away");
        check(wrf::sdirk3::first_failure_of(s) == d.primary_event,
              "and first_failure_of still returns the primary event, so every fixture built on "
              "it keeps its meaning");
    }
    {
        // With no metric evidence at all, the attribution says so instead of agreeing.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.final_residual_measured = false;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        const auto d = wrf::sdirk3::stage_diagnosis_of(s);
        check(!d.attribution_measured,
              "absence of metric evidence is reported as absence, not as agreement with the "
              "event -- the rule this campaign has applied to every other field");
    }

    {
        // R13.19 SELF-REVIEW: `attribution` is the classification with the EXIT REMOVED, and with
        // the termination cleared the clauses that consume it fall back to the AGGREGATE
        // reconstruction. So on a stage with no r0 evidence the field is the old precedence, not a
        // measurement -- and the first version of `attribution_measured` (excluding two enum
        // values) would have called that "measured".
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        s.worst_krylov_rel_error_vs_r0 = -1.0;   // no r0 evidence at all
        s.best_krylov_rel_error_vs_r0 = -1.0;
        s.krylov_solves_measured_vs_r0 = 0;
        s.accepted_steps = 3;
        const auto d = wrf::sdirk3::stage_diagnosis_of(s);
        check(d.attribution_measured && !d.attribution_from_metric,
              "an attribution reached by the aggregate fallback is NOT metric evidence, and the "
              "record says which -- a reconstruction naming an operator layer must not read as a "
              "measurement");
    }

    {
        // R13.19 SELF-REVIEW: the MECHANISM attribution was order-dependent even after the
        // all_met predicate became order-independent, because the D/S/source/budget receipt rides
        // on worst_*, which updates on strict `>`. Two solves at the SAME worst ratio let
        // whichever arrived first name the layer.
        using wrf::sdirk3::KrylovSolveMechanism;
        using wrf::sdirk3::near_worst_mechanism_ambiguous;
        KrylovSolveMechanism A; A.progress = 0.99; A.met_tolerance = true;
        A.D_reached = true;  A.S_reached = false;
        KrylovSolveMechanism B; B.progress = 0.99; B.met_tolerance = true;
        B.D_reached = true;  B.S_reached = true;
        check(near_worst_mechanism_ambiguous({A, B}) &&
              near_worst_mechanism_ambiguous({B, A}),
              "two solves tied at the worst ratio with DIFFERENT mechanisms are ambiguous in "
              "either order -- one of them is an objective mismatch and the other a forcing-term "
              "limit, and reporting the first arrival's was order-dependent");
        check(!near_worst_mechanism_ambiguous({A, A}) &&
              !near_worst_mechanism_ambiguous({A}),
              "agreeing solves, and a single solve, are not ambiguous");
        KrylovSolveMechanism C = A; C.progress = 0.5;   // clearly better: outside the band
        check(!near_worst_mechanism_ambiguous({B, C}) &&
              !near_worst_mechanism_ambiguous({C, B}),
              "and a solve outside the tie band is not in the set, so it cannot create ambiguity");
    }
    {
        // ...and the classifier refuses to name a mechanism when they disagree.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        s.krylov_solves_measured_vs_r0 = 2;
        s.accepted_steps = 2;
        s.exit_krylov_iter = 1;
        s.worst_krylov_D_reached = true; s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = true;
        check(name_of(s) == "krylov_objective_mismatch",
              "with the near-worst solves agreeing, the specific mechanism is named");
        s.near_worst_mechanism_ambiguous = true;
        check(name_of(s) == "krylov_stagnated",
              "and when they disagree the classifier reports the general category rather than "
              "whichever solve arrived first");
    }

    {
        // R13.19 SELF-REVIEW (round 8, P0-B): the D-satisfied zero-work solve must REACH the
        // objective-mismatch clause. Admitting it to the aggregate without counting its D
        // tolerance as "met" made it an unmet solve at the maximum, which trips the tie refusal --
        // the FIRST clause of the four-way -- so the category the fix exists to name became
        // unreachable and the layer emitted was the operator/split.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.worst_krylov_rel_error_vs_r0 = 1.0;     // zero-work solve: progress ~ 1
        s.krylov_solves_measured_vs_r0 = 1;
        s.accepted_steps = 1;
        s.exit_krylov_iter = 0;
        s.worst_krylov_D_reached = true;          // it met the D objective on entry
        s.worst_krylov_S_reached = false;         // and not the S one
        s.worst_krylov_met_tolerance = true;      // ...which IS meeting a tolerance
        s.all_near_worst_met_tolerance = true;
        check(name_of(s) == "krylov_objective_mismatch",
              "a solve that converged on entry in the D objective and not in S is the objective "
              "mismatch, and must reach that clause rather than being refused as an unmet tie");
        // R13.20: the historical misrouting is reproduced through the flag that now carries it.
        // (`all_near_worst_met_tolerance` no longer gates the clause -- see the R9-6 block above.)
        s.near_worst_mechanism_ambiguous = true;  // as the tie read before the fix
        check(name_of(s) == "krylov_stagnated",
              "...which is exactly what the record said before: krylov_stagnated, on the "
              "operator/split layer, beside a receipt reading D_reached=1 S_reached=0");
    }

    {
        // R13.19 SELF-REVIEW (round 8, P0-C): the layer must come from the receipt that DECIDED
        // the category. `specific_layer` read the EXIT solve's source/metric unconditionally,
        // while the four-way clauses can be reached from the STAGE-WORST receipt.
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;
        s.worst_krylov_rel_error_vs_r0 = 0.99;
        s.krylov_solves_measured_vs_r0 = 2;
        s.accepted_steps = 2;
        s.worst_krylov_D_reached = true; s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = true;
        // No exit receipt and no zero-update exit: this reached the clause via stage-worst.
        s.exit_krylov_iter = -1;
        const auto d = wrf::sdirk3::stage_diagnosis_of(s);
        check(name_of(s) == "krylov_objective_mismatch" && !d.decided_by_exit_receipt,
              "a category reached through the STAGE-WORST receipt must not be annotated with the "
              "exit solve's source or metric -- that is a layer describing one solve attached to "
              "a verdict decided by another");
        // ...and when the exit receipt IS what decided it, the flag says so.
        StageFailureSignals e = ok_stage();
        e.newton_converged = false;
        e.residual_first = 8.7e8; e.residual_last = 8.6e8;
        e.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        e.exit_krylov_iter = 3;
        give_complete_exit_receipt(e, /*rho_D=*/0.85, /*rho_S=*/1.05, /*tol=*/0.9,
                                   wrf::sdirk3::KrylovStoppingMetric::BlockD);
        const auto de = wrf::sdirk3::stage_diagnosis_of(e);
        // R13.21 (external review P0-2): event and cause, separately.
        check(de.primary_event == StageFailure::ZeroUpdateAfterTotalFailure &&
              de.decided_by_exit_receipt,
              "the event is the zero-update break and the exit receipt is what decided it, so the "
              "emitted layer and the verdict describe the same solve");
        check(de.exit_attribution == StageFailure::KrylovObjectiveMismatch,
              "...and the subtype the exit solve earned -- D met, S not -- is carried BESIDE the "
              "event rather than replacing it, which is what `primary_event` used to report");
    }

    {
        // R13.20 (round 9, R9-6): the tie refusal must fire on a difference that can change the
        // ANSWER. Tolerance PROVENANCE cannot: the classifier never branches on it.
        using wrf::sdirk3::KrylovSolveMechanism;
        using wrf::sdirk3::near_worst_mechanism_ambiguous;
        using wrf::sdirk3::krylov_mechanism_category;
        KrylovSolveMechanism P; P.progress = 0.99; P.met_tolerance = true;
        P.D_reached = true; P.S_reached = false;
        P.tolerance_source = static_cast<int>(wrf::sdirk3::KrylovToleranceSource::EisenstatWalker);
        KrylovSolveMechanism Q = P;
        Q.tolerance_source = static_cast<int>(wrf::sdirk3::KrylovToleranceSource::InnRamp);
        check(!near_worst_mechanism_ambiguous({P, Q}) &&
              !near_worst_mechanism_ambiguous({Q, P}),
              "two tied solves that differ ONLY in where their tolerance came from imply the "
              "same category and are not ambiguous -- the refusal used to fire here and fall "
              "through to krylov_stagnated, i.e. toward the operator/split layer");
        check(krylov_mechanism_category(P) == StageFailure::KrylovObjectiveMismatch &&
              krylov_mechanism_category(Q) == StageFailure::KrylovObjectiveMismatch,
              "...and both of them say objective mismatch, which is why");
        KrylovSolveMechanism R; R.progress = 0.99;   // met nothing, no budget signal
        check(krylov_mechanism_category(R) == StageFailure::KrylovStagnated &&
              near_worst_mechanism_ambiguous({P, R}),
              "a tied solve that implies a DIFFERENT category is still ambiguous");
        KrylovSolveMechanism T; T.progress = 0.99; T.budget_exhausted = true;
        check(krylov_mechanism_category(T) == StageFailure::KrylovBudgetExhausted,
              "and the budget answer is reachable from one receipt, so the four-way's third "
              "arm has a producer rather than only a fixture");
    }
    {
        // R13.20 (round 9, R9-2): `attribution_from_metric` must be produced by the clause that
        // RETURNED. The case it was written for -- r0 measured, worst ratio BELOW the
        // no-progress threshold, so the classifier falls through to the aggregate
        // reconstruction -- used to read 1, because the branch it fell through was entered on
        // `measured(worst_..._vs_r0)`. That is 1 by construction where the comment defines 0.
        using wrf::sdirk3::StageDecisionBasis;
        StageFailureSignals s = ok_stage();
        s.newton_converged = false;
        s.residual_first = 8.7e8; s.residual_last = 8.6e8;   // flat: reconstruction says "stall"
        s.krylov_solves_measured_vs_r0 = 4;
        s.accepted_steps = 4;
        s.worst_krylov_rel_error_vs_r0 = 0.20;   // every solve MOVED: below the threshold
        const auto d = wrf::sdirk3::stage_diagnosis_of(s);
        check(d.attribution == StageFailure::NewtonStagnated &&
              d.attribution_basis == StageDecisionBasis::AggregateReconstruction,
              "with every solve making progress the attribution comes from the residual trend "
              "and the iteration counts -- the aggregate reconstruction, by name");
        check(!d.attribution_from_metric,
              "...so it is NOT metric-decided, however many r0 readings exist on the record");
        s.worst_krylov_rel_error_vs_r0 = 0.99;   // now no solve moved
        s.worst_krylov_D_reached = true; s.worst_krylov_S_reached = false;
        s.worst_krylov_met_tolerance = true;
        const auto d2 = wrf::sdirk3::stage_diagnosis_of(s);
        check(d2.attribution_basis == StageDecisionBasis::KrylovR0Receipt &&
              d2.attribution_from_metric,
              "and when the r0 four-way is the clause that answered, it says so");
        check(std::string(wrf::sdirk3::stage_decision_basis_name(
                  StageDecisionBasis::LegacyKrylovAggregate)) == "legacy_krylov_aggregate",
              "the ||b||-coordinate fallback has its own name and is deliberately NOT counted "
              "as metric evidence");
    }
    {
        // R13.20 (round 9, R9-5): the E-W arm used to collapse three levers into one enum value,
        // routing a tolerance-limited verdict to ew_gamma/ew_alpha on solves that read neither.
        using wrf::sdirk3::KrylovToleranceSource;
        using wrf::sdirk3::krylov_forcing_layer_for;
        check(std::string(krylov_forcing_layer_for(KrylovToleranceSource::EisenstatWalker)) ==
                  "eisenstat_walker_forcing" &&
              std::string(krylov_forcing_layer_for(KrylovToleranceSource::EwEtaClamp)) ==
                  "ew_eta_min_max_clamp" &&
              std::string(krylov_forcing_layer_for(KrylovToleranceSource::EwInitialFloor)) ==
                  "ew_initial_eta_floor" &&
              std::string(krylov_forcing_layer_for(KrylovToleranceSource::Base)) ==
                  "base_inner_tolerance",
              "the eta clamp and the iteration-0 seed constant name their OWN knobs: the "
              "forcing term is not what bound when max(krylov_tol, EW_ETA_INITIAL) or the "
              "[eta_min, eta_max] clamp produced the number");
        check(std::string(wrf::sdirk3::krylov_tolerance_source_name(
                  KrylovToleranceSource::EwEtaClamp)) == "ew_eta_clamp" &&
              std::string(wrf::sdirk3::krylov_tolerance_source_name(
                  KrylovToleranceSource::EwInitialFloor)) == "ew_initial_floor",
              "...and the record prints them distinctly from eisenstat_walker");
    }

    {
        // R13.21 (external review P0-1): the entry verdict. A Krylov loop that returns before any
        // Arnoldi step because the inner stopping objective was already met is NOT necessarily a
        // convergence -- rho_S can be anything, including > 1. One flag used to carry both states,
        // derived from the termination LABEL, and it exempted both from the total-failure rules.
        using wrf::sdirk3::krylov_entry_verdict;
        using wrf::sdirk3::entry_exempts_total_failure;
        using wrf::sdirk3::entry_requires_nonlinear_decrease;

        const auto converged = krylov_entry_verdict(/*stop_metric=*/true, /*S=*/true);
        check(converged.S_reached && !converged.objective_mismatch &&
              entry_exempts_total_failure(converged) &&
              !entry_requires_nonlinear_decrease(converged),
              "an entry that met BOTH the stopping objective and S is converged: it exempts the "
              "total-failure rules and needs no extra nonlinear check");

        const auto mismatch = krylov_entry_verdict(/*stop_metric=*/true, /*S=*/false);
        check(mismatch.objective_mismatch && !mismatch.S_reached,
              "an entry that met the stopping objective and NOT S is an objective mismatch, not a "
              "convergence -- InitialStopMetricReached is named for the half it measures");
        check(!entry_exempts_total_failure(mismatch),
              "...and it does NOT bypass the total-failure policy: an identical rho_S returned "
              "after Arnoldi iterations is judged by that policy, and a control-flow label must "
              "not buy a different one");
        check(entry_requires_nonlinear_decrease(mismatch),
              "...and its step is taken only against a measured nonlinear decrease, which is what "
              "the recovery and trust acceptance sites already require of every other step");

        const auto not_on_entry = krylov_entry_verdict(/*stop_metric=*/false, /*S=*/true);
        check(!not_on_entry.S_reached && !not_on_entry.objective_mismatch &&
              !entry_exempts_total_failure(not_on_entry),
              "a solve that did NOT return on entry is neither, whatever its S flag says -- the "
              "verdict is about the entry return, not about S alone");
    }

    {
        // R13.21 (external review P0-3): two solves are interchangeable only if they send you to
        // THE SAME PLACE. R13.20 narrowed the tie check to CATEGORY equality, which was right
        // about the category and wrong about the consequence -- `specific_layer` is derived from
        // the tolerance source (forcing) or the stopping metric (objective mismatch), and the
        // stage-worst receipt updates on a strict `>`, so on an exact tie the FIRST ARRIVAL's
        // provenance is what the emitter reads. Category was order-invariant; the next place to
        // work was not.
        using wrf::sdirk3::KrylovSolveMechanism;
        using wrf::sdirk3::near_worst_mechanism_ambiguous;
        using wrf::sdirk3::krylov_specific_layer_for;
        using wrf::sdirk3::KrylovToleranceSource;
        using wrf::sdirk3::KrylovStoppingMetric;

        KrylovSolveMechanism F1; F1.progress = 0.99; F1.S_reached = true; F1.met_tolerance = true;
        F1.tolerance_source = static_cast<int>(KrylovToleranceSource::EwEtaClamp);
        KrylovSolveMechanism F2 = F1;
        F2.tolerance_source = static_cast<int>(KrylovToleranceSource::InnRamp);
        check(krylov_mechanism_category(F1) == krylov_mechanism_category(F2) &&
              near_worst_mechanism_ambiguous({F1, F2}) &&
              near_worst_mechanism_ambiguous({F2, F1}),
              "two tied forcing-limited solves whose tolerance came from DIFFERENT knobs agree on "
              "the category and disagree on the layer, so they are action-ambiguous in either "
              "order -- R13.20 called them interchangeable and the emitted layer depended on "
              "arrival order");
        check(std::string(krylov_specific_layer_for(F1)) == "ew_eta_min_max_clamp" &&
              std::string(krylov_specific_layer_for(F2)) == "inn_tolerance_ramp",
              "...and those are the two different places the record would have sent the work");

        KrylovSolveMechanism M1; M1.progress = 0.99; M1.D_reached = true; M1.S_reached = false;
        M1.met_tolerance = true;
        M1.stopping_metric = static_cast<int>(KrylovStoppingMetric::BlockD);
        KrylovSolveMechanism M2 = M1;
        M2.stopping_metric = static_cast<int>(KrylovStoppingMetric::StageWRMS);
        check(near_worst_mechanism_ambiguous({M1, M2}) && near_worst_mechanism_ambiguous({M2, M1}),
              "and two tied objective mismatches that stopped on DIFFERENT metrics are ambiguous "
              "too -- the layer is derived from the stopping metric there");

        // The gain R13.20 bought must survive: a category that derives no specific layer is not
        // made ambiguous by provenance that cannot change the answer.
        KrylovSolveMechanism S1; S1.progress = 0.99;   // met nothing -> KrylovStagnated
        S1.tolerance_source = static_cast<int>(KrylovToleranceSource::EwEtaClamp);
        KrylovSolveMechanism S2 = S1;
        S2.tolerance_source = static_cast<int>(KrylovToleranceSource::Base);
        S2.stopping_metric = static_cast<int>(KrylovStoppingMetric::IdentityS);
        check(!near_worst_mechanism_ambiguous({S1, S2}) && !near_worst_mechanism_ambiguous({S2, S1}),
              "but two tied stagnations derive no specific layer, so differing provenance does NOT "
              "make them ambiguous -- the refusal must not fire toward the operator/split layer "
              "for a difference that cannot change where the work goes");
        check(std::string(krylov_specific_layer_for(S1)) == "n/a",
              "...which is exactly because that category names its layer from the category alone");
    }

    {
        // R13.21 (external review P1-2): a Krylov return is complete or it is not. Six sites
        // return a result; four filled the metric/budget receipt and the two NanRetryExhausted
        // early returns filled only the constructor fields, so a terminal solve ending there gave
        // the classifier an exit receipt it could not subtype -- the record reporting ABSENCE
        // where the solve knew the answer.
        using wrf::sdirk3::KrylovReceiptView;
        using wrf::sdirk3::krylov_receipt_complete;

        KrylovReceiptView full;
        full.rho_D_final = 1.0; full.rho_S_final = 1.0;
        full.stopping_metric = static_cast<int>(wrf::sdirk3::KrylovStoppingMetric::BlockD);
        full.arnoldi_spent = 7; full.arnoldi_allowed = 8;
        check(krylov_receipt_complete(full),
              "a return that stamps both ratios, the metric it stopped on, and the budget it "
              "spent against what it was allowed is a complete receipt");

        // Each field alone must be able to fail it -- a rule that cannot fire is not a rule.
        { auto r = full; r.rho_D_final = -1.0;
          check(!krylov_receipt_complete(r), "...an unstamped rho_D alone makes it incomplete"); }
        { auto r = full; r.rho_S_final = -1.0;
          check(!krylov_receipt_complete(r), "...an unstamped rho_S alone makes it incomplete"); }
        { auto r = full; r.stopping_metric = -1;
          check(!krylov_receipt_complete(r),
                "...and without the metric it stopped on, nothing downstream can name the layer"); }
        { auto r = full; r.arnoldi_spent = -1;
          check(!krylov_receipt_complete(r), "...nor without the work actually spent"); }
        { auto r = full; r.arnoldi_allowed = 0;
          check(!krylov_receipt_complete(r),
                "...nor with a zero budget, which cannot establish exhaustion"); }

        // The pre-R13.21 NanRetryExhausted receipt, reconstructed: only the ctor fields.
        KrylovReceiptView nan_retry_before;
        nan_retry_before.rho_S_final = 1.0;   // rel_error was set; nothing else was
        check(!krylov_receipt_complete(nan_retry_before),
              "the NaN-retry return as it stood before R13.21 is incomplete by this rule, which "
              "is why a terminal solve ending there fell back to the generic event");
    }

    {
        // R13.21 (external review section 8): the no-progress boundary is a DECISION boundary, not
        // a mechanism classifier. Measured on this campaign's own stage-3 budget sweep: vs_r0
        // 0.907 at one setting and 0.8993 at the next -- 0.8 % apart -- flipped the attribution
        // krylov_stagnated <-> newton_stagnated and the layer with it, while the primary event was
        // stage-3 zero_update_after_total_failure in both.
        using wrf::sdirk3::threshold_proximity;
        using wrf::sdirk3::threshold_permits_specific_layer;

        const auto near_above = threshold_proximity(0.907, 0.90);
        const auto near_below = threshold_proximity(0.8993, 0.90);
        check(near_above.measured_ && near_below.measured_ &&
              near_above.distance > 0.0 && near_below.distance < 0.0,
              "the distance is SIGNED, so the record says which side of the boundary the row "
              "landed on as well as how far");
        check(near_above.sensitive && near_below.sensitive,
              "and both readings from the real sweep are inside the sensitivity band -- the flip "
              "between them is a boundary crossing, not a change of mechanism");
        check(!threshold_permits_specific_layer(near_above) &&
              !threshold_permits_specific_layer(near_below),
              "so neither may issue a specific layer: a work order that a 1 % change in the ratio "
              "would have reversed is not a work order");

        const auto far = threshold_proximity(0.9941, 0.90);
        check(far.measured_ && !far.sensitive && threshold_permits_specific_layer(far),
              "the dt=600 central record at 0.9941 is far from the boundary and keeps its layer");

        const auto unmeasured = threshold_proximity(-1.0, 0.90);
        check(!unmeasured.measured_ && threshold_permits_specific_layer(unmeasured),
              "and a row with no r0 progress at all is not 'threshold sensitive' -- absence of a "
              "measurement must not be reported as a near-boundary verdict");

        const auto custom = threshold_proximity(0.86, 0.85);
        check(custom.threshold == 0.85 && custom.sensitive,
              "the band is applied to the threshold ACTUALLY used, not to the header constant -- "
              "the boundary is overridable per run and the distance must follow it");
    }

    {
        // R13.23 (deep review P0-4): THE RECEIPT MUST EARN THE ATTRIBUTION.
        // `exit_receipt_complete` was computed and emitted but never consulted, so a row could
        // read `exit_receipt_complete=0` beside a specific `exit_attribution` derived from that
        // same receipt -- producer, emitter, no consumer, the shape this repository keeps fixing.
        using wrf::sdirk3::KrylovReceiptView;
        using wrf::sdirk3::krylov_receipt_complete;
        using wrf::sdirk3::KrylovStoppingMetric;

        KrylovReceiptView ok;
        ok.rho_D_final = 0.85; ok.rho_S_final = 1.05; ok.tolerance_applied = 0.9;
        ok.D_reached = true;   ok.S_reached = false;
        ok.stopping_metric = static_cast<int>(KrylovStoppingMetric::BlockD);
        ok.arnoldi_spent = 7;  ok.arnoldi_allowed = 8;
        check(krylov_receipt_complete(ok),
              "a receipt whose ratios, metric, budget and reached flags all agree is complete");

        { auto r = ok; r.stopping_metric = static_cast<int>(KrylovStoppingMetric::Unknown);
          check(!krylov_receipt_complete(r),
                "an UNKNOWN stopping metric fails: the old test was `>= 0` and Unknown is 0, so an "
                "unstamped metric passed as complete and an attribution named a layer the receipt "
                "could not support"); }
        { auto r = ok; r.arnoldi_spent = 9;
          check(!krylov_receipt_complete(r),
                "work spent cannot exceed work allowed -- a receipt that says so is not describing "
                "one solve"); }
        { auto r = ok; r.D_reached = false;
          check(!krylov_receipt_complete(r),
                "a reached flag that contradicts rho against the tolerance fails: the flags are "
                "re-derived, not trusted"); }
        { auto r = ok; r.tolerance_applied = -1.0;
          check(!krylov_receipt_complete(r),
                "and a tolerance that was never applied cannot have been reached -- claiming it is "
                "the fabrication this rule exists to catch"); }
        { auto r = ok; r.tolerance_applied = -1.0; r.D_reached = false; r.S_reached = false;
          check(krylov_receipt_complete(r),
                "...while a path that applied no tolerance and claims neither flag is complete, "
                "because it reports exactly what it measured"); }
        { auto r = ok; r.receipt_iter = 2; r.exit_iter = 3;
          check(!krylov_receipt_complete(r),
                "and a receipt stamped for another iteration is not this solve's"); }

        // The gate itself: an incomplete receipt yields NO subtype, not a plausible one.
        StageFailureSignals inc = ok_stage();
        inc.newton_converged = false;
        inc.residual_first = 8.7e8; inc.residual_last = 8.6e8;
        inc.newton_termination =
            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure;
        inc.exit_krylov_iter = 3;
        inc.exit_D_reached = true; inc.exit_S_reached = false;   // the flags alone
        const auto d_inc = wrf::sdirk3::stage_diagnosis_of(inc);
        check(d_inc.primary_event == StageFailure::ZeroUpdateAfterTotalFailure &&
              d_inc.exit_attribution == StageFailure::None,
              "with only the reached flags set the receipt is incomplete, so the event stands and "
              "NO subtype is attributed -- before this gate the same signals produced "
              "krylov_objective_mismatch from a receipt that could not support it");
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

    constexpr int expected_checks = 153;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    // R13.20: ratcheted like the case count, so converting a RESERVED pin into a live one -- or
    // silently adding another unreachable branch -- has to be an explicit edit here.
    constexpr int expected_reserved = 4;
    const bool reserved_ok = (reserved_count == expected_reserved);
    std::cout << (reserved_ok ? "  ok   " : "  FAIL ")
              << "reserved-branch ratchet (" << reserved_count << "/" << expected_reserved
              << " pinned classifications that NO production call site can produce)" << std::endl;
    if (!reserved_ok) ++failures;

    if (failures == 0) {
        std::cout << "FIRST_FAILURE_CLASSIFICATION_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "FIRST_FAILURE_CLASSIFICATION_CONTRACT: FAIL (" << failures << ")"
              << std::endl;
    return 1;
}
