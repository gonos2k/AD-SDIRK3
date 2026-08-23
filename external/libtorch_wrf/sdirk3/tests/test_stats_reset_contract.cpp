// R13.10 (red team P0-1): the per-solve reset must clear EVERY per-solve field.
//
// reset_stats() cleared the nine fields it had in 2025 and none of the ten added since, so
// best_krylov_rel_error, the GMRES counters, krylov_diverged, accepted/rejected steps and
// initial_residual_measured accumulated for the LIFE OF THE SOLVER -- one object per run --
// and every first-failure classification after the first failure in a run read the run's
// history as the stage's. The fix value-initialises the struct. What THIS contract pins is
// narrower than "the next field cannot fall behind": the poison list below is hand-enumerated,
// so a future field is reset only because the implementation is value-init, and a future
// field-by-field rewrite that forgot one would pass here. The guarantee lives in
// reset_per_solve() being `s = ConvergenceStats{}`; this file pins that the fields known TODAY
// -- in particular the ten that were missed -- come back to their defaults.

#include "../wrf_sdirk3_newton_solver.h"

#include <iostream>
#include <string>

namespace {
int failures = 0, check_count = 0;
void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using Stats = wrf::sdirk3::WRFNewtonKrylovSolver::ConvergenceStats;
    Stats s;
    s.newton_iterations = 7; s.total_krylov_iterations = 99;
    s.newton_residuals = {1.0f, 0.5f};
    s.final_residual = 0.5f; s.final_residual_measured = true;
    s.initial_unscaled_residual = 1.0f; s.initial_residual_measured = true;
    s.initial_residual_finite = true; s.converged = true;
    s.best_krylov_rel_error = 0.3f; s.gmres_total_failures = 2;
    s.gmres_non_total_failures = 3; s.gmres_tolerance_reached = 1;
    s.krylov_diverged = true; s.accepted_steps = 4; s.rejected_steps = 1;
    s.newton_iteration_budget = 12;

    wrf::sdirk3::WRFNewtonKrylovSolver::reset_per_solve(s);
    const Stats d{};

    check(s.newton_iterations == d.newton_iterations &&
          s.total_krylov_iterations == d.total_krylov_iterations &&
          s.newton_residuals.empty() && s.converged == d.converged,
          "the 2025 fields reset (the ones reset_stats always cleared)");
    check(s.best_krylov_rel_error == d.best_krylov_rel_error,
          "best_krylov_rel_error resets -- it was the process-wide MINIMUM, so after one good "
          "solve anywhere the stall rule could never fire again");
    check(s.gmres_total_failures == d.gmres_total_failures &&
          s.gmres_non_total_failures == d.gmres_non_total_failures &&
          s.gmres_tolerance_reached == d.gmres_tolerance_reached,
          "the GMRES counters reset -- a total failure at timestep 1 made every later stage "
          "krylov_stagnated");
    check(s.krylov_diverged == d.krylov_diverged,
          "krylov_diverged resets -- it was sticky for the life of the run");
    check(s.accepted_steps == d.accepted_steps && s.rejected_steps == d.rejected_steps,
          "the step counters reset -- all_steps_rejected was unreachable after the first "
          "accepted step of the process");
    check(s.initial_residual_measured == d.initial_residual_measured &&
          s.initial_residual_finite == d.initial_residual_finite,
          "initial_residual_measured resets -- otherwise the R13.5 fix held for the first "
          "solve only");
    check(s.final_residual_measured == d.final_residual_measured && !s.final_residual_measured,
          "final_residual_measured resets to FALSE -- reset_stats used to assert it true with "
          "final_residual = 0.0, which is the unmeasured-reads-as-measured defect again");
    check(s.newton_iteration_budget == d.newton_iteration_budget,
          "the recorded budget resets, so a stale budget cannot classify the next solve");

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ") << "case-count ratchet ("
              << check_count << "/" << expected_checks << ")" << std::endl;
    if (!count_ok) ++failures;
    if (failures == 0) { std::cout << "STATS_RESET_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "STATS_RESET_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
