#include "../wrf_sdirk3_newton_solver.h"
#include "../wrf_sdirk3_config.h"
#include <cmath>
#include <iostream>

using namespace wrf::sdirk3;

int main() {
    const auto saved = g_sdirk3_config;
    bool ok = true;
    for (bool adaptive : {false, true}) {
        auto& cfg = g_sdirk3_config;
        cfg.debug_level = 0;
        cfg.use_autograd = true;
        cfg.jvp_method = SDIRK3Config::JVP_AUTOGRAD;
        WRFNewtonKrylovOptions opt;
        opt.nx = opt.ny = opt.nz = 1;
        opt.nx_u = opt.ny_v = opt.nz_w = 2;
        opt.use_preconditioner = false;
        opt.use_adaptive_tolerances = adaptive;
        opt.max_newton_iter = 1;
        opt.gmres_restart = 10;
        opt.max_krylov_iter = 20;
        opt.newton_tol = 1e-12f;
        opt.newton_rtol = 0.0f;
        opt.krylov_tol = 1e-8f;
        WRFNewtonKrylovSolver solver(opt);
        auto u = torch::linspace(0.1, 0.6, 10, torch::kFloat64);
        auto scale = torch::linspace(1.0, 2.0, 10, torch::kFloat64);
        solver.set_physics_scaling(scale);
        int calls = 0;
        auto rhs = [&calls](const torch::Tensor& x) { ++calls; return 2.0 * x + 0.5 * x * x; };
        auto initial_k = torch::zeros({1, u.numel()}, u.options());
        auto result = solver.solve_stage_with_status(u, initial_k, rhs, 0.125, 0.25, 1);
        auto stats = solver.get_stats();
        if (!result.K.defined()) { ok = false; continue; }
        auto eval = u + 0.125 * 0.25 * result.K;
        auto defect = result.K - (2.0 * eval + 0.5 * eval * eval);
        const double expected = (defect / scale).norm().item<double>() / std::sqrt(10.0);
        const double initial = ((2.0 * u + 0.5 * u * u) / scale).norm().item<double>() / std::sqrt(10.0);
        bool pass = !result.converged && result.iterations == 1 && stats.accepted_steps > 0 &&
            stats.final_residual_measured && std::isfinite(result.final_residual) && expected > opt.newton_tol &&
            std::abs(result.final_residual - expected) < 1e-7 &&
            result.final_residual == stats.final_residual && std::abs(expected - initial) > 1e-4 && calls > 0;
        std::cout << (pass ? "PASS" : "FAIL") << " adaptive=" << adaptive
                  << " returned=" << result.final_residual << " expected=" << expected
                  << " initial=" << initial << " accepted=" << stats.accepted_steps << '\n';
        ok &= pass;
    }
    g_sdirk3_config = saved;
    return ok ? 0 : 1;
}
