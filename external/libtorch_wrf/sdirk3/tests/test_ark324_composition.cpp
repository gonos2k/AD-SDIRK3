#include "../wrf_sdirk3_ark324_composition.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_newton_solver.h"

#include <cmath>
#include <iostream>
#include <sstream>

using namespace wrf::sdirk3;
using Ark = ARK324L2SACoefficients;

namespace {
constexpr int n = 10;  // Exact 1x1x1 staggered StateLayout; last entry is the clock.
int checks = 0;
int failures = 0;

void check(bool ok, const std::string& label) {
    ++checks;
    failures += !ok;
    std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
}

struct Problem {
    torch::Tensor explicit_matrix = torch::zeros({n, n}, torch::kFloat64);
    torch::Tensor implicit_matrix = torch::zeros({n, n}, torch::kFloat64);
    torch::Tensor phase = torch::linspace(0.2, 1.0, n - 1, torch::kFloat64);
    bool forced;

    explicit Problem(bool forcing) : forced(forcing) {
        for (int j = 0; j < n - 1; ++j) {
            implicit_matrix[j][j] = -0.3 - 0.07 * j;
            if (j + 1 < n - 1) explicit_matrix[j][j + 1] = 0.7;
            if (j > 0) explicit_matrix[j][j - 1] = -0.2;
            if (j > 1) implicit_matrix[j][j - 2] = 0.15;
        }
    }

    torch::Tensor manufactured(const torch::Tensor& time) const {
        return torch::cat({torch::sin(time + phase), time.reshape({1})});
    }

    torch::Tensor slow(const torch::Tensor& state) const {
        auto rhs = explicit_matrix.mv(state);
        if (forced) {
            auto time = state[n - 1];
            auto exact = manufactured(time);
            auto rate = torch::cat({torch::cos(time + phase), torch::ones({1}, state.options())});
            rhs = rhs + rate - (explicit_matrix + implicit_matrix).mv(exact);
        }
        return rhs;
    }

    torch::Tensor initial() const {
        return forced ? manufactured(torch::zeros({}, torch::kFloat64))
                      : torch::cat({torch::linspace(1.0, 2.0, n - 1, torch::kFloat64),
                                    torch::zeros({1}, torch::kFloat64)});
    }

    torch::Tensor exact(double time) const {
        return forced ? manufactured(torch::tensor(time, torch::kFloat64))
                      : torch::matrix_exp(time * (explicit_matrix + implicit_matrix)).mv(initial());
    }
};

torch::Tensor integrate(const Problem& problem, int steps, float tolerance,
                        const torch::Tensor& initial = {}, bool dense_reference = false) {
    WRFNewtonKrylovOptions options;
    options.nx = options.ny = options.nz = 1;
    options.nx_u = options.ny_v = options.nz_w = 2;
    options.use_preconditioner = false;
    options.use_adaptive_tolerances = false;
    options.max_newton_iter = 24;
    options.gmres_restart = n;
    options.max_krylov_iter = 20;
    options.newton_tol = tolerance;
    options.newton_rtol = 0.0f;
    options.krylov_tol = 1e-7f;
    options.retain_graph_for_adjoint = initial.defined() && initial.requires_grad();
    WRFNewtonKrylovSolver solver(options);
    auto state = initial.defined() ? initial : problem.initial();
    solver.set_physics_scaling(torch::ones_like(state));
    const double dt = 1.0 / steps;
    for (int step = 0; step < steps; ++step) {
        std::vector<torch::Tensor> fast(Ark::stages), slow(Ark::stages), full(Ark::stages);
        for (int stage = 0; stage < Ark::stages; ++stage) {
            const auto base = ark324_stage_base(state, dt, stage, slow, fast,
                                                [](int, const torch::Tensor&) {});
            const double gamma = Ark::a_implicit[stage][stage];
            if (gamma == 0) {
                fast[stage] = problem.implicit_matrix.mv(base);
            } else if (dense_reference) {
                const auto matrix = torch::eye(n, base.options())
                    - (dt * gamma) * problem.implicit_matrix;
                fast[stage] = torch::linalg_solve(matrix, problem.implicit_matrix.mv(base));
            } else {
                const auto rhs = [&](const torch::Tensor& y) {
                    return problem.implicit_matrix.mv(y);
                };
                const auto result = solver.solve_stage_with_status(
                    base, torch::Tensor(), rhs, dt, gamma, stage + 1);
                TORCH_CHECK(result.converged, "manufactured stage did not converge: ", result.message);
                fast[stage] = result.K;
            }
            const auto stage_state = base + dt * gamma * fast[stage];
            slow[stage] = problem.slow(stage_state);
            full[stage] = fast[stage] + slow[stage];
        }
        state = ark324_final_state(state, dt, full);
    }
    return state;
}
}  // namespace

int main() {
    const auto saved = g_sdirk3_config;
    g_sdirk3_config.debug_level = 0;
    g_sdirk3_config.use_autograd = false;
    g_sdirk3_config.jvp_method = decltype(g_sdirk3_config)::JVP_DUAL_NUMBER;
    g_sdirk3_config.direct_u_solve_thresh = 0.0f;
    g_sdirk3_config.precond_type = 0;
    std::ostringstream solver_log;
    auto* previous_stream = std::cerr.rdbuf(solver_log.rdbuf());
    try {
        // A constant RHS isolates consistency from nonlinear/stage solve error.
        // Rounding the weights to float before an FP64 update introduces a bias.
        const auto initial_constant = torch::tensor({1.25, -0.75}, torch::kFloat64);
        const auto rate_constant = torch::tensor({0.7, -1.1}, torch::kFloat64);
        const std::vector<torch::Tensor> constant_stages(Ark::stages, rate_constant);
        const auto exact_constant = initial_constant + 0.25 * rate_constant;
        check((ark324_final_state(initial_constant, 0.25, constant_stages) -
               exact_constant).abs().max().item<double>() < 1e-15,
              "FP64 constant RHS preserves exact tableau consistency");
        check((ark324_final_state(initial_constant, 0.25f, constant_stages) -
               exact_constant).norm().item<double>() > 1e-8,
              "float-scalar negative control detects rounded-weight bias");
        {
            WRFNewtonKrylovOptions options;
            options.nx = options.ny = options.nz = 1;
            options.nx_u = options.ny_v = options.nz_w = 2;
            options.use_preconditioner = options.use_adaptive_tolerances = false;
            options.newton_tol = options.krylov_tol = 1e-12f;
            options.newton_rtol = 0.0f;
            options.max_newton_iter = 8;
            options.gmres_restart = n;
            options.max_krylov_iter = 20;
            WRFNewtonKrylovSolver solver(options);
            const auto base = torch::linspace(0.1, 0.6, n, torch::kFloat64);
            solver.set_physics_scaling(torch::ones_like(base));
            const auto rhs = [](const torch::Tensor& state) { return 2.0 * state; };
            const double dt = 0.125;
            const double gamma = Ark::a_implicit[1][1];
            const auto result = solver.solve_stage_with_status(base, {}, rhs, dt, gamma, 2);
            const auto exact = 2.0 * base / (1.0 - 2.0 * dt * gamma);
            check(result.converged && (result.K - exact).norm().item<double>() < 1e-11,
                  "FP64 Newton residual and JVP preserve the double implicit diagonal");
        }
        for (bool forced : {false, true}) {
            const Problem problem(forced);
            check((problem.explicit_matrix.matmul(problem.implicit_matrix) -
                   problem.implicit_matrix.matmul(problem.explicit_matrix)).norm().item<double>() > 0.1,
                  "split operators do not commute");
            double previous_error = 0;
            for (int steps : {4, 8, 16, 32}) {
                const auto state = integrate(problem, steps, 1e-8f);
                const double error = (state - problem.exact(1.0)).norm().item<double>();
                const double order = previous_error > 0 ? std::log2(previous_error / error) : 0;
                std::cout << "ORDER forced=" << forced << " steps=" << steps
                          << " error=" << error << " order=" << order << '\n';
                check(std::isfinite(error) && error > 0, "finite, nonzero temporal error");
                if (steps >= 16) check(order > 2.7 && order < 3.3, "observed third order");
                previous_error = error;
            }
            const auto tight = integrate(problem, 16, 1e-8f);
            const auto tighter = integrate(problem, 16, 1e-9f);
            const double solve_spread = (tighter - tight).norm().item<double>();
            const double temporal_error = (tight - problem.exact(1.0)).norm().item<double>();
            check(solve_spread < 0.01 * temporal_error,
                  "tightening Newton tolerance leaves temporal error dominant");
        }
        // The same ARK composition and production Newton returns now carry the
        // implicit-equation pullback. Compare to differentiable dense stage roots,
        // then to central differences of the actual primal, including the clock.
        g_sdirk3_config.use_autograd = true;
        const Problem forced(true);
        const auto direction = torch::linspace(-0.4, 0.7, n, torch::kFloat64);
        const auto terminal = torch::linspace(0.3, 1.2, n, torch::kFloat64);
        for (int steps : {1, 3}) {
            auto initial = forced.initial().requires_grad_(true);
            auto final = integrate(forced, steps, 1e-9f, initial);
            auto pulled = torch::autograd::grad({final}, {initial}, {terminal}, true)[0];
            auto reference_initial = initial.detach().clone().requires_grad_(true);
            auto reference_final = integrate(forced, steps, 1e-9f, reference_initial, true);
            auto reference_pulled = torch::autograd::grad(
                {reference_final}, {reference_initial}, {terminal})[0];
            const double adjoint_error = (pulled - reference_pulled).norm().item<double>()
                                        / reference_pulled.norm().item<double>();
            check(adjoint_error < 2e-6, "full ARK pullback matches dense implicit roots");
            const double epsilon = 1e-4;
            const auto plus = integrate(forced, steps, 1e-9f, initial.detach() + epsilon * direction);
            const auto minus = integrate(forced, steps, 1e-9f, initial.detach() - epsilon * direction);
            const auto tangent_fd = (plus - minus) / (2 * epsilon);
            const double lhs = tangent_fd.dot(terminal).item<double>();
            const double rhs = direction.dot(pulled).item<double>();
            const double dot_error = std::abs(lhs - rhs) / std::max(std::abs(lhs), std::abs(rhs));
            std::cout << "ADJOINT steps=" << steps << " dense_error=" << adjoint_error
                      << " dot_error=" << dot_error << '\n';
            check(dot_error < 2e-5, "full-step finite-difference tangent / adjoint dot identity");
            const auto objective = 0.5 * final.square().sum();
            const auto gradient = torch::autograd::grad({objective}, {initial})[0];
            const double derivative = gradient.dot(direction).item<double>();
            const double value = objective.item<double>();
            double previous = 0;
            for (double h : {0.02, 0.01, 0.005}) {
                const auto shifted = integrate(forced, steps, 1e-9f, initial.detach() + h * direction);
                const double remainder = std::abs(0.5 * shifted.square().sum().item<double>()
                                                  - value - h * derivative);
                if (previous > 0) {
                    const double rate = std::log2(previous / remainder);
                    check(rate > 1.9 && rate < 2.1, "objective Taylor remainder is second order");
                }
                previous = remainder;
            }
        }
        {
            WRFNewtonKrylovOptions options;
            options.nx = options.ny = options.nz = 1;
            options.nx_u = options.ny_v = options.nz_w = 2;
            options.use_preconditioner = options.use_adaptive_tolerances = false;
            options.retain_graph_for_adjoint = true;
            options.newton_tol = 1e-8f;
            options.krylov_tol = 1e-7f;
            options.gmres_restart = n;
            options.max_krylov_iter = 20;
            const auto matrix = Problem(false).implicit_matrix;
            auto initial = torch::linspace(0.1, 0.6, n, torch::kFloat64).requires_grad_(true);
            auto forcing = torch::linspace(-0.05, 0.03, n, torch::kFloat64).requires_grad_(true);
            const auto rhs = [&](const torch::Tensor& y) {
                return matrix.mv(y) + 0.03 * y.square() + forcing;
            };
            // Destroy the solver before backward: the saved graph must own all
            // required data and must not invoke a dangling Newton/tile callback.
            torch::Tensor root;
            {
                WRFNewtonKrylovSolver solver(options);
                solver.set_physics_scaling(torch::ones_like(initial));
                const auto result = solver.solve_stage_with_status(initial, {}, rhs, 0.5f, 0.5f, 2);
                TORCH_CHECK(result.converged, "nonlinear stage did not converge");
                root = result.K;
            }
            const auto point = initial.detach() + 0.25 * root.detach();
            const auto jacobian = matrix + torch::diag(0.06 * point);
            const auto a_transpose = (torch::eye(n, point.options()) - 0.25 * jacobian).t();
            const auto cotangent = torch::linspace(0.3, 1.2, n, torch::kFloat64);
            const auto lambda = torch::linalg_solve(a_transpose, cotangent);
            const auto expected = jacobian.t().mv(lambda);
            for (double scale : {1.0, 1e-20, 0.0}) {
                const auto gradients = torch::autograd::grad(
                    {root}, {initial, forcing}, {scale * cotangent}, true);
                if (scale == 0) {
                    check(gradients[0].norm().item<double>() == 0 &&
                          gradients[1].norm().item<double>() == 0, "zero cotangent gives zero pullback");
                } else {
                    check((gradients[0] / scale - expected).norm().item<double>() < 2e-6,
                          "nonlinear pullback uses converged point, independently of objective scale");
                    check((gradients[1] / scale - lambda).norm().item<double>() < 2e-6,
                          "external forcing cotangent is preserved");
                }
            }
            bool higher_order_rejected = false;
            try {
                torch::autograd::grad({root}, {initial}, {cotangent},
                                      /*retain_graph=*/true, /*create_graph=*/true);
            } catch (const c10::Error&) { higher_order_rejected = true; }
            check(higher_order_rejected, "unsupported higher-order pullback is rejected");

            options.max_newton_iter = 1;
            options.newton_tol = 1e-12f;
            WRFNewtonKrylovSolver insufficient(options);
            insufficient.set_physics_scaling(torch::ones_like(initial));
            bool unconverged_rejected = false;
            try { insufficient.solve_stage(initial, {}, rhs, 0.5f, 0.5f, 2); }
            catch (const c10::Error&) { unconverged_rejected = true; }
            check(unconverged_rejected, "production API rejects an unconverged differentiable stage");
        }
    } catch (const std::exception& e) {
        check(false, e.what());
        std::cout << solver_log.str().substr(solver_log.str().size() > 6000
                                           ? solver_log.str().size() - 6000 : 0);
    }
    std::cerr.rdbuf(previous_stream);
    g_sdirk3_config = saved;
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
