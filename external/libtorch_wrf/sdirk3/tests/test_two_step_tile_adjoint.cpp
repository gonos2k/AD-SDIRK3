// Two C++ tile maps with separate graph ownership; excludes the Fortran caller.
#include "tile_test_fixture.h"

namespace {
using wrf::sdirk3::test::TileCase;
constexpr int N = wrf::sdirk3::test::total;

struct ChainResult {
    torch::Tensor output;
    torch::Tensor initial_pullback;
};

torch::Tensor make_initial() {
    auto x = torch::zeros({N}, torch::kFloat32);
    auto idx = torch::arange(N, torch::kFloat32);
    // Excite several packed blocks while staying small enough for the dry fixture.
    x += 1.0e-3f * torch::sin(0.017f * idx);
    return x;
}

torch::Tensor make_direction() {
    auto idx = torch::arange(N, torch::kFloat32);
    auto v = torch::sin(0.031f * idx) + 0.3f * torch::cos(0.011f * idx);
    return v / v.norm();
}

torch::Tensor make_terminal() {
    auto idx = torch::arange(N, torch::kFloat32);
    auto lambda = torch::cos(0.023f * idx) - 0.2f * torch::sin(0.041f * idx);
    return lambda / lambda.norm();
}

ChainResult run_chain(const torch::Tensor &x0, const torch::Tensor &terminal, bool retain_graph) {
    auto &cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.retain_graph_for_adjoint = retain_graph;
    TileCase first;
    first.set(x0);
    first.step(0.1f);
    const auto x1 = first.state();

    // A separate solver owns the second step's graph. This avoids overwriting
    // the first step's retained graph while composing the two VJPs.
    TileCase second;
    second.set(x1);
    second.step(0.1f);
    const auto x2 = second.state();
    if (!retain_graph)
        return {x2, {}};

    const auto lambda1 = second.solver.pullbackLastStep(terminal);
    const auto lambda0 = first.solver.pullbackLastStep(lambda1);
    return {x2, lambda0};
}

void configure() {
    auto &cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.n_threads = 1;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1;
    cfg.hevi_split = false;
    cfg.use_autograd = true;
    cfg.imex_slow_in_tangent = true;
    cfg.precond_type = 0;
    cfg.max_newton_iter = 40;
    cfg.newton_tol = 1e-7f;
    cfg.krylov_tol = 1e-6f;
    cfg.gmres_restart = 30;
    cfg.max_krylov_iter = 20;
    cfg.stage_fail_action = 1;
    cfg.gmres_warmstart = false;
    cfg.inn_warmstart_enable = false;
}
} // namespace

int main() {
    try {
        torch::set_num_threads(1);
        configure();
        const auto x0 = make_initial();
        const auto direction = make_direction();
        const auto terminal = make_terminal();
        const auto baseline = run_chain(x0, terminal, true);
        TORCH_CHECK(torch::isfinite(baseline.output).all().item<bool>(),
                    "two-step output is non-finite");
        TORCH_CHECK(torch::isfinite(baseline.initial_pullback).all().item<bool>(),
                    "two-step pullback is non-finite");
        TORCH_CHECK(baseline.initial_pullback.norm().item<double>() > 1e-8,
                    "two-step pullback is uninformative");
        TORCH_CHECK((baseline.initial_pullback - terminal).norm().item<double>() > 1e-3,
                    "two-step pullback reduced to the identity");

        const auto forward_control = run_chain(x0, terminal, false);
        TORCH_CHECK(torch::equal(baseline.output, forward_control.output),
                    "retained graph changed the two-step forward result");

        const auto zero = run_chain(x0, torch::zeros_like(terminal), true);
        TORCH_CHECK(zero.initial_pullback.norm().item<double>() == 0.0,
                    "zero terminal cotangent did not pull back to zero");

        const auto ad = direction.to(torch::kFloat64)
                            .dot(baseline.initial_pullback.to(torch::kFloat64))
                            .item<double>();
        TORCH_CHECK(std::isfinite(ad) && std::abs(ad) > 1e-12,
                    "two-step directional adjoint signal is zero/uninformative");
        double previous_remainder = 0.0;
        for (const float eps : {0.04f, 0.02f, 0.01f}) {
            const auto plus = run_chain(x0 + eps * direction, terminal, false).output;
            const auto minus = run_chain(x0 - eps * direction, terminal, false).output;
            const auto delta = (plus.to(torch::kFloat64) - minus.to(torch::kFloat64)) / (2.0 * eps);
            const double fd = delta.dot(terminal.to(torch::kFloat64)).item<double>();
            const double denom = 0.5 * (std::abs(fd) + std::abs(ad));
            TORCH_CHECK(std::isfinite(fd) && denom > 1e-12,
                        "two-step FD/adjoint signal is zero or non-finite");
            const double rel = std::abs(fd - ad) / denom;
            // Objective Taylor check on the plus branch, with the quadratic
            // term making the expected remainder ratio approximately four.
            const auto plus_delta = plus.to(torch::kFloat64) - baseline.output.to(torch::kFloat64);
            const double objective = plus_delta.dot(terminal.to(torch::kFloat64)).item<double>() +
                                     0.5 * plus_delta.square().sum().item<double>();
            const double remainder = std::abs(objective - eps * ad);
            std::cout << "TWO_STEP_TILE_ADJOINT eps=" << eps << " ad=" << ad << " fd=" << fd
                      << " relative_error=" << rel << " taylor_remainder=" << remainder;
            if (previous_remainder > 0.0)
                std::cout << " remainder_ratio=" << previous_remainder / remainder;
            std::cout << '\n';
            TORCH_CHECK(rel < 5e-4 && std::isfinite(remainder) && remainder > 0.0,
                        "two-step tile VJP finite-difference/Taylor check failed");
            if (previous_remainder > 0.0) {
                const double ratio = previous_remainder / remainder;
                TORCH_CHECK(ratio > 3.5 && ratio < 4.5,
                            "two-step objective Taylor remainder is not second order");
            }
            previous_remainder = remainder;
        }
        std::cout << "two-step C++ packed tile composition passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
