#ifndef WRF_SDIRK3_IMEX_JFNK_STAGE_SOLVER_H
#define WRF_SDIRK3_IMEX_JFNK_STAGE_SOLVER_H

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <functional>

#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_newton_solver.h"

namespace wrf {
namespace sdirk3 {

struct IMEXNewtonKrylovOptions {
    int max_newton_iterations = 6;
    int gmres_restart = 15;
    int gmres_max_iterations = 80;

    float newton_relative_tolerance = 1e-6f;
    float newton_absolute_tolerance = 1e-10f;
    float gmres_relative_tolerance = 1e-4f;

    float initial_damping = 1.0f;
    int max_backtracking_steps = 6;
};

template <typename PreconditionerT>
torch::Tensor solve_imex_implicit_stage_jfnk(
    const torch::Tensor& rhs,
    const torch::Tensor& y_n,
    double alpha,
    bool strong_acoustic,
    const std::function<torch::Tensor(const torch::Tensor&)>& fast_operator,
    PreconditionerT& preconditioner,
    const IMEXNewtonKrylovOptions& options = IMEXNewtonKrylovOptions()) {
    if (std::abs(alpha) < 1e-14) {
        return rhs;
    }

    torch::NoGradGuard no_grad;
    preconditioner.set_alpha(alpha);

    torch::Tensor y = strong_acoustic ? y_n.detach().clone() : rhs.detach().clone();

    // One cheap preconditioned correction for robust initialization.
    {
        auto r0 = y - rhs - alpha * fast_operator(y);
        y = y - preconditioner.apply(r0);
    }

    const double rhs_norm = std::max(1e-30, rhs.norm().to(torch::kCPU).item<double>());

    for (int newton_iter = 0; newton_iter < options.max_newton_iterations; ++newton_iter) {
        auto residual = y - rhs - alpha * fast_operator(y);
        double residual_norm = residual.norm().to(torch::kCPU).item<double>();
        if (residual_norm <= options.newton_absolute_tolerance +
                                 options.newton_relative_tolerance * rhs_norm) {
            return y;
        }

        auto operator_A = [&](const torch::Tensor& v) -> torch::Tensor {
            auto jv = compute_jvp_fwad_or_fd(fast_operator, y, v);
            return v - alpha * jv;
        };

        auto preconditioned_inverse = [&](const torch::Tensor& v) -> torch::Tensor {
            return preconditioner.apply(v);
        };

        auto gmres = krylov_methods::solve_gmres(
            operator_A,
            -residual,
            torch::zeros_like(y),
            options.gmres_restart,
            options.gmres_relative_tolerance,
            options.gmres_max_iterations,
            preconditioned_inverse,
            nullptr,
            nullptr,
            false,
            false);

        auto delta = gmres.x;

        // Backtracking on nonlinear residual.
        double damping = options.initial_damping;
        torch::Tensor y_trial = y;
        bool accepted = false;
        for (int bt = 0; bt <= options.max_backtracking_steps; ++bt) {
            y_trial = y + damping * delta;
            auto residual_trial = y_trial - rhs - alpha * fast_operator(y_trial);
            double trial_norm = residual_trial.norm().to(torch::kCPU).item<double>();
            if (trial_norm < residual_norm) {
                accepted = true;
                break;
            }
            damping *= 0.5;
        }
        y = y_trial;
        if (!accepted && !gmres.success) {
            // Keep progressing with best effort even if linear and line search are weak.
            continue;
        }
    }

    return y;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_JFNK_STAGE_SOLVER_H
