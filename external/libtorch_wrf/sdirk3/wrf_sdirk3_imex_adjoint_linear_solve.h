#ifndef WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
#define WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H

#include <torch/torch.h>

#include <functional>
#include <limits>

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

    // 9F.D91 (review P0-1): the PHYSICAL residual is the stopping authority.
    double rel_true = std::numeric_limits<double>::infinity();

    // 9F.D87 (review section 6): report the TRUE, UNPRECONDITIONED residual.
    //
    // solve_gmres is handed rhs_left = P^T b and left_operator = P^T A^T, so its rel_error
    // is ||P^T(b - A^T x)|| / ||P^T b|| -- the PRECONDITIONED norm. With the identity
    // preconditioner that coincides with the physical residual; with any real P it does
    // not. Comparing rel_error ACROSS preconditioners therefore compares different norms,
    // and the D84 dt-ladder did exactly that: its identity column was a true residual and
    // its M^-T column was not, so the two were never comparable.
    //
    // One extra operator application against the 400 in the solve. The verdict has to be
    // made on the same physical quantity for every preconditioner or it is not a comparison.
    {
        // operator_transpose forms a VJP, so it MUST NOT be inside a NoGradGuard. Only the
        // reductions below are guarded.
        //
        // I wrapped this whole block in a guard first, and it died with "element 0 of
        // tensors does not require grad" -- the FIFTH occurrence of that pattern in this
        // campaign (D59 power_iterate_sigma_max, D66 the adjoint driver, D80's misreading,
        // D85 the probe itself, now here), and the second within one session, minutes after
        // fixing it in the probe. The reflex to open a diagnostic with NoGradGuard is the
        // bug; the rule is "guard each .item(), and NOTHING that calls back into an operator
        // which may need a graph".
        auto r_phys = rhs - operator_transpose(gmres.x);
        torch::NoGradGuard no_grad;
        rel_true = (r_phys.norm() / rhs.norm().clamp_min(1e-30)).template item<double>();
        std::cerr << "SDIRK3_TRANSPOSE_TRUE_RESIDUAL rel_true=" << rel_true
                  << " rel_preconditioned=" << gmres.rel_error
                  << " |x|=" << gmres.x.norm().template item<double>()
                  << std::endl << std::flush;
    }

    // ADJOINT FAIL-CLOSE (full-repo review P1-2): the forward solver fail-closes
    // on non-convergence, but this transpose solve previously returned gmres.x
    // UNCONDITIONALLY — a stalled/broken-down adjoint solve produced a
    // partially-converged lambda the caller could not distinguish from a real
    // gradient, silently corrupting any 4D-Var descent direction built on it.
    // A wrong gradient is strictly worse than no gradient: refuse instead. The
    // throw propagates through runAdjointReplay (catch/restore/rethrow) -> the
    // C wrapper (-1) -> Fortran (ierr stays 1), the already-verified error chain.
    // 9F.D91 (review P0-1): JUDGE ON THE PHYSICAL RESIDUAL.
    //
    // D87 computed rel_true and only PRINTED it; the gate still read gmres.rel_error, which
    // is ||P^T(b - A^T x)|| / ||P^T b||. A preconditioner that shrinks the residual along
    // some direction makes that ratio small while the physical residual stays O(1):
    //
    //     A = I,  b = (1,1)^T,  x = (1,0)^T   =>   rel_true = 1/sqrt(2) ~ 0.707
    //     P^T = diag(1, 1e-6)                 =>   rel_precond ~ 1e-6   "converged"
    //
    // That is a FALSE SUCCESS returning a 70%-wrong gradient, and the comment directly below
    // is the reason it matters: a wrong gradient is strictly worse than no gradient. The
    // preconditioned residual describes the Krylov iteration; only the physical one
    // describes the equation the caller asked to solve.
    //
    // gmres.success is kept as a NECESSARY condition -- it also covers breakdown and NaN
    // paths -- but it is no longer sufficient, and gmres.rel_error is now diagnostic only.
    const bool physical_ok = std::isfinite(rel_true) && rel_true <= gmres_tolerance;
    if (!gmres.success || !physical_ok) {
        throw std::runtime_error(
            std::string("sdirk3 adjoint transpose solve did not converge (") +
            "success=" + (gmres.success ? "true" : "false") +
            ", rel_true=" + std::to_string(rel_true) +
            ", rel_preconditioned=" + std::to_string(gmres.rel_error) +
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
