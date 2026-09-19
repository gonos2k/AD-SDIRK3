// Actual fixed-trajectory API: primal equality, nonzero/zero VJP, and FD/Taylor.
// Uses the same tolerance/perturbation protocol as the existing two-owner test.
#include "tile_test_fixture.h"
#include <limits>

namespace {
using wrf::sdirk3::test::TileCase;
constexpr int N = wrf::sdirk3::test::total;

struct ChainResult {
    torch::Tensor output;
    torch::Tensor initial_pullback;
};

struct MultiChainResult {
    torch::Tensor output;
    std::vector<torch::Tensor> pullbacks;
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

std::vector<int64_t> packed_block_sizes() {
    return {wrf::sdirk3::test::su, wrf::sdirk3::test::sv,
            wrf::sdirk3::test::sw, wrf::sdirk3::test::sw,
            wrf::sdirk3::test::st, wrf::sdirk3::test::sm};
}

torch::Tensor make_block_vector(int block) {
    const auto sizes = packed_block_sizes();
    TORCH_CHECK(block >= 0 && block < static_cast<int>(sizes.size()),
                "invalid packed block ", block);
    int64_t start = 0;
    for (int b = 0; b < block; ++b) start += sizes[b];
    const int64_t size = sizes[block];
    auto local = torch::arange(size, torch::kFloat32);
    local = torch::sin((0.031f + 0.002f * block) * local + 0.17f * block) +
            0.3f * torch::cos((0.011f + 0.001f * block) * local);
    local = local / local.norm();
    auto packed = torch::zeros({N}, torch::kFloat32);
    packed.slice(0, start, start + size).copy_(local);
    return packed;
}

int trajectory_length = 2;
std::vector<float> trajectory_dt_schedule() {
    if (trajectory_length == 2) return {0.1f, 0.075f};
    return {0.1f, 0.075f, 0.05f};
}
ChainResult run_chain(const torch::Tensor& x0, const torch::Tensor& terminal, bool retain_graph) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.retain_graph_for_adjoint = retain_graph;
    TileCase tile(100000.0f, 0.0f, true);
    tile.set(x0);
    const auto dt_schedule = trajectory_dt_schedule();
    if (retain_graph) tile.solver.beginFixedTrajectory(trajectory_length, dt_schedule);
    for (int step = 0; step < trajectory_length; ++step) tile.step(dt_schedule[step]);
    const auto out = tile.state();
    if (!retain_graph) return {out, {}};
    const auto lambda = tile.solver.pullbackFixedTrajectory(terminal);
    tile.solver.closeFixedTrajectory();
    return {out, lambda};
}

MultiChainResult run_chain_many(const torch::Tensor& x0,
                                const std::vector<torch::Tensor>& terminals) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.retain_graph_for_adjoint = true;
    TileCase tile(100000.0f, 0.0f, true);
    tile.set(x0);
    const auto dt_schedule = trajectory_dt_schedule();
    tile.solver.beginFixedTrajectory(trajectory_length, dt_schedule);
    for (int step = 0; step < trajectory_length; ++step) tile.step(dt_schedule[step]);
    const auto out = tile.state();
    std::vector<torch::Tensor> pullbacks;
    pullbacks.reserve(terminals.size());
    for (const auto& terminal : terminals)
        pullbacks.push_back(tile.solver.pullbackFixedTrajectory(terminal));
    tile.solver.closeFixedTrajectory();
    return {out, std::move(pullbacks)};
}

void configure(bool top_lid) {
    auto &cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.n_threads = 1;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1;
    cfg.hevi_split = false;
    cfg.khdif = cfg.kvdif = 0.0f;
    cfg.wrf_w_damping = 0;
    cfg.non_hydrostatic = true;
    cfg.do_curvature = true;
    cfg.split_explicit_top_lid = top_lid;
    cfg.buoyancy_use_current_w = true;
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
        for (const bool top_lid : {false, true}) {
        configure(top_lid);
        for (int length : {2, 3}) {
        trajectory_length = length;
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
            // Keep the linear objective diagnostic separate while preserving the original
            // quadratic Taylor remainder and ratio assertions below.
            const auto plus_delta = plus.to(torch::kFloat64) - baseline.output.to(torch::kFloat64);
            const double objective = plus_delta.dot(terminal.to(torch::kFloat64)).item<double>() +
                                     0.5 * plus_delta.square().sum().item<double>();
            const double remainder = std::abs(objective - eps * ad);
            const double linear_remainder = std::abs(
                plus_delta.dot(terminal.to(torch::kFloat64)).item<double>() - eps * ad);
            std::cout << "FIXED_TRAJECTORY_ADJOINT N=" << length << " eps=" << eps << " ad=" << ad << " fd=" << fd
                      << " relative_error=" << rel << " linear_remainder=" << linear_remainder
                      << " quadratic_remainder=" << remainder;
            if (previous_remainder > 0.0)
                std::cout << " quadratic_remainder_ratio=" << previous_remainder / remainder;
            std::cout << '\n';
            TORCH_CHECK(rel < 5e-4 && std::isfinite(linear_remainder) &&
                            std::isfinite(remainder) && remainder > 0.0,
                        "two-step tile VJP finite-difference/Taylor check failed");
            if (previous_remainder > 0.0) {
                const double ratio = previous_remainder / remainder;
                TORCH_CHECK(ratio > 3.5 && ratio < 4.5,
                            "two-step objective Taylor remainder is not second order");
            }
            previous_remainder = remainder;
        }
        std::cout << "fixed C++ trajectory NH=1 curvature=1 top_lid=" << top_lid
                  << " N=" << length << " passed\n";

        // One retained trajectory supplies independent block cotangents without repeating the
        // nonlinear forward solve.  The six vectors are supported on the exact packed blocks
        // (ru, rv, rw, ph, t, mu), so this exercises coordinate ownership rather than only one
        // global random direction.  Keep this contract probe to one representative case to
        // bound runtime; the original four cases above remain unchanged.
        if (!top_lid && length == 2) {
            std::vector<torch::Tensor> block_vectors;
            for (int block = 0; block < 6; ++block)
                block_vectors.push_back(make_block_vector(block));
            const auto c1 = block_vectors[0];
            const auto c2 = block_vectors[5];
            const auto c12 = c1 + 2.0f * c2;
            auto terminals = block_vectors;
            terminals.push_back(c12);
            terminals.push_back(torch::zeros({N}, torch::kFloat32));
            const auto batch = run_chain_many(x0, terminals);
            TORCH_CHECK(batch.pullbacks.size() == 8,
                        "block/cotangent contract returned the wrong pullback count");
            for (int block = 0; block < 6; ++block) {
                const auto& pb = batch.pullbacks[block];
                TORCH_CHECK(torch::isfinite(pb).all().item<bool>() &&
                                pb.norm().item<double>() > 1e-8,
                            "block cotangent ", block, " produced an uninformative pullback");

                // Independent block direction: compare one central FD to the corresponding
                // block terminal objective, using the existing eps=0.01 and rel<5e-4 budget.
                const float eps = 0.01f;
                const auto& block_cotangent = block_vectors[block];
                const auto plus = run_chain(x0 + eps * block_vectors[block],
                                            block_cotangent, false).output;
                const auto minus = run_chain(x0 - eps * block_vectors[block],
                                             block_cotangent, false).output;
                const auto delta = (plus.to(torch::kFloat64) - minus.to(torch::kFloat64)) /
                                   (2.0 * eps);
                const double fd = delta.dot(block_cotangent.to(torch::kFloat64)).item<double>();
                const double ad = block_vectors[block].to(torch::kFloat64)
                                      .dot(batch.pullbacks[block].to(torch::kFloat64)).item<double>();
                const double denom = 0.5 * (std::abs(fd) + std::abs(ad));
                const double rel = std::abs(fd - ad) / denom;
                std::cout << "FIXED_TRAJECTORY_BLOCK block=" << block
                          << " fd=" << fd << " ad=" << ad << " relative_error=" << rel << '\n';
                TORCH_CHECK(std::isfinite(fd) && denom > 1e-12 &&
                                rel < 5e-4,
                            "block direction ", block,
                            " failed fixed-trajectory FD/VJP contract");

                // Reuse this FD and the six retained VJPs: no additional trajectory solves.
                // Four FP32 unit roundoffs bound a conservative output/pullback quantization
                // allowance for this projection, not the full integration/truncation error.
                const double roundoff = 2.0 * std::numeric_limits<float>::epsilon();
                const auto direction64 = block_vectors[block].to(torch::kFloat64);
                for (int output_block = 0; output_block < 6; ++output_block) {
                    const auto terminal64 = block_vectors[output_block].to(torch::kFloat64);
                    const auto pullback64 = batch.pullbacks[output_block].to(torch::kFloat64);
                    const double cross_fd = delta.dot(terminal64).item<double>();
                    const double cross_ad = direction64.dot(pullback64).item<double>();
                    const double atol = roundoff * (
                        terminal64.abs().dot(plus.to(torch::kFloat64).abs() +
                                             minus.to(torch::kFloat64).abs()).item<double>() /
                            (2.0 * eps) +
                        direction64.abs().dot(pullback64.abs()).item<double>());
                    const double signal = std::max(std::abs(cross_fd), std::abs(cross_ad));
                    const double budget = atol + 5e-4 * signal;
                    const char* status = cross_fd == 0.0 && cross_ad == 0.0 ? "exact-zero" :
                                         signal <= atol ? "unresolved" : "resolved";
                    std::cout << "FIXED_TRAJECTORY_CROSS input=" << block
                              << " output=" << output_block << " fd=" << cross_fd
                              << " ad=" << cross_ad << " atol=" << atol
                              << " status=" << status << '\n';
                    TORCH_CHECK(std::isfinite(cross_fd) && std::isfinite(cross_ad) &&
                                    std::isfinite(atol) && std::abs(cross_fd - cross_ad) <= budget,
                                "cross-block FD/VJP mismatch: input=", block,
                                " output=", output_block);
                    if (block == 2 && output_block == 3) {
                        // W -> PH must be observable; a zeroed coupling must fail this budget.
                        TORCH_CHECK(std::abs(cross_fd) > atol + 5e-4 * std::abs(cross_fd),
                                    "W -> PH coupling is not resolved above the error budget");
                    }
                }
            }
            const auto& pb1 = batch.pullbacks[0];
            const auto& pb2 = batch.pullbacks[5];
            const auto& pb12 = batch.pullbacks[6];
            const double lin_den = std::max(
                (pb12.abs().max().item<double>()),
                (pb1 + 2.0f * pb2).abs().max().item<double>());
            const double lin_err = (pb12 - pb1 - 2.0f * pb2).abs().max().item<double>() /
                                   std::max(lin_den, 1e-30);
            TORCH_CHECK(lin_err < 5e-4,
                        "fixed-trajectory pullback is not linear in terminal cotangent");
            TORCH_CHECK(batch.pullbacks[7].norm().item<double>() == 0.0,
                        "zero terminal cotangent did not remain exactly zero in batch pullback");
            std::cout << "fixed trajectory block/cotangent contract passed"
                      << " max_linear_error=" << lin_err << '\n';
        }
        }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
