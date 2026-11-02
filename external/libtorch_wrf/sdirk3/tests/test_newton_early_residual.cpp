#include "../wrf_sdirk3_newton_solver.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_first_failure.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace wrf::sdirk3;

int main() {
    const auto saved = g_sdirk3_config;
    auto& cfg = g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.use_autograd = false;
    WRFNewtonKrylovOptions opt;
    opt.nx = opt.ny = opt.nz = 1;
    opt.nx_u = opt.ny_v = opt.nz_w = 2;
    opt.use_preconditioner = false;
    opt.use_adaptive_tolerances = false;
    opt.max_newton_iter = 2;
    opt.newton_tol = 1e-12f;
    opt.newton_rtol = 0.0f;
    auto u = torch::linspace(0.1, 1.0, 10, torch::kFloat64);
    auto scale = torch::linspace(1.0, 2.0, 10, torch::kFloat64);
    auto k = torch::zeros({1, u.numel()}, u.options());
    bool ok = true;
    for (bool production : {false, true}) {
        for (int kind = 0; kind < 3; ++kind) {
            WRFNewtonKrylovSolver solver(opt);
            solver.set_physics_scaling(scale);
            int calls = 0;
            auto rhs = [&](const torch::Tensor& x) -> torch::Tensor {
                ++calls;
                if (kind == 0 && calls > 1) throw std::runtime_error("forced JVP failure");
                if (kind == 1) return torch::full_like(x, std::numeric_limits<double>::quiet_NaN());
                if (kind == 2) return torch::full_like(x, std::numeric_limits<double>::infinity());
                return 2.0 * x;
            };
            WRFNewtonKrylovSolver::NewtonResult result{};
            torch::Tensor returned;
            if (production) returned = solver.solve_stage(u, k, rhs, 0.125, 0.25, 1);
            else { result = solver.solve_stage_with_status(u, k, rhs, 0.125, 0.25, 1); returned = result.K; }
            const auto stats = solver.get_stats();
            const float expected = (2.0 * u / scale).norm().item<float>() / std::sqrt(10.0f);
            bool pass = returned.defined() && torch::equal(returned, k.select(0, 0)) && !stats.converged &&
                stats.newton_iterations == 1 && stats.initial_residual_measured &&
                stats.final_residual_measured;
            if (kind == 0) {
                pass &= calls > 1 && std::abs(stats.final_residual - expected) < 1e-6f &&
                    stats.newton_termination == static_cast<int>(NewtonTerminationReason::Exception);
            } else {
                pass &= !stats.initial_residual_finite && !std::isfinite(stats.final_residual) &&
                    stats.newton_termination == static_cast<int>(NewtonTerminationReason::NonfiniteResidual);
            }
            if (!production) {
                pass &= !result.converged && result.iterations == 1 &&
                    ((std::isnan(result.final_residual) && std::isnan(stats.final_residual)) ||
                     result.final_residual == stats.final_residual);
            }
            std::cout << (pass ? "PASS" : "FAIL") << " production=" << production
                      << " kind=" << kind << " calls=" << calls
                      << " final=" << stats.final_residual << '\n';
            ok &= pass;
        }
    }
    // Direct API edge case only: zero Newton budget is not a model configuration claim.
    for (bool production : {false, true}) {
        auto zero_opt = opt;
        zero_opt.max_newton_iter = 0;
        WRFNewtonKrylovSolver solver(zero_opt);
        solver.set_physics_scaling(scale);
        const auto rhs = [](const torch::Tensor& x) { return 2.0 * x; };
        WRFNewtonKrylovSolver::NewtonResult result{};
        if (production) (void)solver.solve_stage(u, k, rhs, 0.125, 0.25, 1);
        else result = solver.solve_stage_with_status(u, k, rhs, 0.125, 0.25, 1);
        const auto stats = solver.get_stats();
        const bool pass = !stats.converged && !stats.final_residual_measured &&
            std::isnan(stats.final_residual) &&
            (production || (!result.converged && std::isnan(result.final_residual)));
        std::cout << (pass ? "PASS" : "FAIL") << " zero_budget production="
                  << production << '\n';
        ok &= pass;
    }
    // A checked FP64-to-float conversion can throw before a scalar receipt exists.
    // The status API must not invent a residual; the production API still propagates.
    for (bool production : {false, true}) {
      for (bool conversion_overflow : {false, true}) {
        WRFNewtonKrylovSolver solver(opt);
        solver.set_physics_scaling(torch::full_like(u, 1.0e40));
        int calls = 0;
        const auto rhs = [&](const torch::Tensor& x) {
            ++calls;
            if (!conversion_overflow) throw 17;
            return torch::full_like(x, 1.0e39);
        };
        WRFNewtonKrylovSolver::NewtonResult result{};
        torch::Tensor returned;
        bool threw = false;
        try {
            if (production) returned = solver.solve_stage(u, k, rhs, 0.125, 0.25, 1);
            else { result = solver.solve_stage_with_status(u, k, rhs, 0.125, 0.25, 1); returned = result.K; }
        } catch (...) { threw = true; }
        const auto stats = solver.get_stats();
        const bool pass = !returned.defined() && threw == production &&
            calls == 1 && !stats.converged && !stats.initial_residual_measured &&
            !stats.final_residual_measured && std::isnan(stats.final_residual) &&
            stats.newton_termination == static_cast<int>(NewtonTerminationReason::Exception) &&
            (production || (!result.converged && std::isnan(result.final_residual)));
        std::cout << (pass ? "PASS" : "FAIL") << " unobserved_exception conversion="
                  << conversion_overflow << " production="
                  << production << " calls=" << calls << " final=" << stats.final_residual << '\n';
        ok &= pass;
      }
    }
    g_sdirk3_config = saved;
    return ok ? 0 : 1;
}
