#ifndef WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
#define WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H

#include <torch/torch.h>

#include <functional>

#include "wrf_sdirk3_newton_solver.h"

namespace wrf {
namespace sdirk3 {

// Reverse-mode VJP: computes J(y)^T * vector.
inline torch::Tensor compute_vjp_reverse_mode(
    const std::function<torch::Tensor(const torch::Tensor&)>& function,
    const torch::Tensor& y,
    const torch::Tensor& vector) {
    auto y_req = y.detach().clone().requires_grad_(true);
    auto f_y = function(y_req);
    auto scalar = (f_y * vector).sum();

    auto gradients = torch::autograd::grad(
        {scalar},
        {y_req},
        {},
        /*retain_graph=*/false,
        /*create_graph=*/false,
        /*allow_unused=*/false);
    return gradients[0];
}

// Solves (I - alpha * J^T) x = rhs with left preconditioning by P^{-T}.
template <typename PreconditionerT>
torch::Tensor solve_transpose_linear_system_gmres(
    const std::function<torch::Tensor(const torch::Tensor&)>& fast_operator,
    const torch::Tensor& linearization_point,
    const torch::Tensor& rhs,
    double alpha,
    PreconditionerT& preconditioner,
    int gmres_restart = 15,
    int gmres_max_iterations = 80,
    float gmres_tolerance = 1e-5f) {
    preconditioner.set_alpha(alpha);

    auto operator_transpose = [&](const torch::Tensor& v) -> torch::Tensor {
        auto jtv = compute_vjp_reverse_mode(fast_operator, linearization_point, v);
        return v - alpha * jtv;
    };

    auto left_operator = [&](const torch::Tensor& v) -> torch::Tensor {
        return preconditioner.apply_transpose(operator_transpose(v));
    };

    auto rhs_left = preconditioner.apply_transpose(rhs);
    auto x0 = torch::zeros_like(rhs);

    auto gmres = krylov_methods::solve_gmres(
        left_operator,
        rhs_left,
        x0,
        /*stage_id=*/0,
        /*ru_share_hint=*/0.0f,
        gmres_restart,
        gmres_tolerance,
        gmres_max_iterations,
        nullptr,
        nullptr,
        nullptr,
        false,
        false);
    // ADJOINT FAIL-CLOSE (full-repo review P1-2): the forward solver fail-closes
    // on non-convergence, but this transpose solve previously returned gmres.x
    // UNCONDITIONALLY — a stalled/broken-down adjoint solve produced a
    // partially-converged lambda the caller could not distinguish from a real
    // gradient, silently corrupting any 4D-Var descent direction built on it.
    // A wrong gradient is strictly worse than no gradient: refuse instead. The
    // throw propagates through runAdjointReplay (catch/restore/rethrow) -> the
    // C wrapper (-1) -> Fortran (ierr stays 1), the already-verified error chain.
    if (!gmres.success || !(gmres.rel_error <= gmres_tolerance)) {
        throw std::runtime_error(
            std::string("sdirk3 adjoint transpose solve did not converge (") +
            "success=" + (gmres.success ? "true" : "false") +
            ", rel_error=" + std::to_string(gmres.rel_error) +
            ", tol=" + std::to_string(gmres_tolerance) +
            ", iterations=" + std::to_string(gmres.iterations) +
            ", msg=" + gmres.message +
            ") — refusing to return a partially-converged adjoint");
    }
    return gmres.x;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
