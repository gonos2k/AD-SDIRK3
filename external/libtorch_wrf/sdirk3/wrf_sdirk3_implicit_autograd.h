#ifndef WRF_SDIRK3_IMPLICIT_AUTOGRAD_H
#define WRF_SDIRK3_IMPLICIT_AUTOGRAD_H

#include "wrf_sdirk3_imex_adjoint_linear_solve.h"
#include <torch/csrc/autograd/custom_function.h>

namespace wrf::sdirk3::implicit_diff {

// First-order pullback of a converged K = F(base + alpha*K).
// Save the RHS graph now: no callback into a later, mutable tile state.
class ConvergedStage final : public torch::autograd::Function<ConvergedStage> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx, const torch::Tensor& base,
        const torch::Tensor& root, const torch::Tensor& point,
        const torch::Tensor& rhs, double alpha, const WRFNewtonKrylovOptions& options) {
        ctx->save_for_backward({point, rhs});
        ctx->saved_data["alpha"] = alpha;
        ctx->saved_data["dims"] = std::vector<int64_t>{options.nx, options.ny, options.nz,
            options.nx_u, options.ny_v, options.nz_w};
        ctx->saved_data["restart"] = options.gmres_restart;
        ctx->saved_data["cycles"] = options.max_krylov_iter;
        ctx->saved_data["tolerance"] = static_cast<double>(options.krylov_tol);
        return root.detach().clone();
    }

    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext* ctx,
        torch::autograd::variable_list gradients) {
        TORCH_CHECK(!torch::GradMode::is_enabled(),
                    "ConvergedStage: higher-order derivatives are not implemented");
        auto saved = ctx->get_saved_variables();
        const auto& point = saved[0];
        const auto& rhs = saved[1];
        const auto cotangent = gradients[0].detach();
        const double norm = cotangent.to(torch::kFloat64).norm().item<double>();
        TORCH_CHECK(std::isfinite(norm), "ConvergedStage: non-finite cotangent");
        if (norm == 0) {
            return {torch::zeros_like(point), {}, {}, torch::zeros_like(rhs), {}, {}};
        }
        const double alpha = ctx->saved_data["alpha"].toDouble();
        const auto dims = ctx->saved_data["dims"].toIntVector();
        const auto layout = StateLayout::from_grid_dims(
            dims[0], dims[1], dims[2], dims[3], dims[4], dims[5]);
        const float tolerance = static_cast<float>(ctx->saved_data["tolerance"].toDouble());
        const auto transpose = [&](const torch::Tensor& vector) -> torch::Tensor {
            if (!rhs.requires_grad()) return torch::zeros_like(point);
            auto result = torch::autograd::grad({rhs}, {point}, {vector},
                /*retain_graph=*/true, /*create_graph=*/false, /*allow_unused=*/true);
            return result[0].defined() ? result[0] : torch::zeros_like(point);
        };
        const auto apply = [&](const torch::Tensor& vector) {
            return vector - alpha * transpose(vector);
        };
        // Normalize the cotangent, so Krylov's absolute small-vector safeguards
        // do not make the derivative depend on the objective's overall scale.
        const auto normalized = cotangent / norm;
        const auto active = torch::ones_like(normalized);
        const auto solved = krylov_methods::solve_fgmres(
            apply, normalized, torch::zeros_like(normalized), 0, 0.0f,
            static_cast<int>(ctx->saved_data["restart"].toInt()), tolerance,
            static_cast<int>(ctx->saved_data["cycles"].toInt()), nullptr,
            &layout, &active, true, true);
        const auto residual = normalized - apply(solved.x);
        const double residual_norm = residual.detach().to(torch::kFloat64).norm().item<double>();
        const double rhs_norm = normalized.detach().to(torch::kFloat64).norm().item<double>();
        TORCH_CHECK(assess_adjoint_solve(residual_norm, rhs_norm, solved.breakdown,
                                       tolerance) == SolveVerdict::Converged,
                    "ConvergedStage: transpose solve failed its physical residual contract; ",
                    "relative residual=", residual_norm / rhs_norm);
        const auto lambda = norm * solved.x;
        // rhs also carries any dependence on external forcing/reference tensors.
        // Returning lambda there preserves those edges; point is an independent
        // leaf, so its J^T*lambda does not duplicate the direct base pullback.
        return {transpose(lambda), {}, {}, lambda, {}, {}};
    }
};

inline torch::Tensor attach_converged_stage_pullback(
    const torch::Tensor& base, const torch::Tensor& root,
    const std::function<torch::Tensor(const torch::Tensor&)>& rhs_function,
    double dt, double gamma, const WRFNewtonKrylovOptions& options) {
    TORCH_CHECK(!options.is_multi_tile,
                "ConvergedStage: multi-tile boundary pullback is not implemented");
    torch::AutoGradMode enable_grad(true);
    auto point = (base.detach() + (dt * gamma) * root.detach()).clone().requires_grad_(true);
    auto rhs = rhs_function(point);
    TORCH_CHECK(rhs.sizes() == root.sizes() && torch::isfinite(rhs).all().item<bool>(),
                "ConvergedStage: invalid converged RHS graph");
    return ConvergedStage::apply(base, root.detach(), point, rhs,
                                dt * gamma, options);
}

}  // namespace wrf::sdirk3::implicit_diff
#endif
