#include "tile_test_fixture.h"
#include "../wrf_sdirk3_newton_solver.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

namespace {

using namespace wrf::sdirk3;
using Solver = WRFNewtonKrylovSolver;
constexpr int kStateSize = 10;  // Existing 1x1x1 staggered solver fixture layout.

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& label) {
    ++checks;
    failures += !ok;
    std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
}

struct ConfigGuard {
    SDIRK3Config saved = g_sdirk3_config;
    ~ConfigGuard() { g_sdirk3_config = saved; }
};

struct CapturedOutput {
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());

    ~CapturedOutput() {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
    }

    std::string text() const { return out.str() + err.str(); }
};

enum class JvpMode { Autograd, FixedForwardFd };

void configure_stage_case(JvpMode mode, int debug_level, bool telemetry) {
    auto& cfg = g_sdirk3_config;
    cfg.debug_level = debug_level;
    cfg.solver_telemetry = telemetry;
    cfg.use_autograd = mode == JvpMode::Autograd;
    cfg.jvp_method = mode == JvpMode::Autograd
        ? SDIRK3Config::JVP_AUTOGRAD : SDIRK3Config::JVP_FINITE_DIFF;
    cfg.jvp_use_forward_diff = mode == JvpMode::FixedForwardFd;
    cfg.jvp_mixed_fd_newton_switch = 0;
    cfg.jvp_auto_bench_calls = 0;
    cfg.retain_graph_for_adjoint = false;
    cfg.save_trajectory = false;
}

WRFNewtonKrylovOptions stage_options() {
    WRFNewtonKrylovOptions options;
    options.nx = options.ny = options.nz = 1;
    options.nx_u = options.ny_v = options.nz_w = 2;
    options.use_preconditioner = false;
    options.use_adaptive_tolerances = false;
    options.max_newton_iter = 8;
    options.gmres_restart = kStateSize;
    options.max_krylov_iter = 20;
    options.newton_tol = 1.0e-10f;
    options.newton_rtol = 0.0f;
    options.krylov_tol = 1.0e-8f;
    // The standalone options intentionally keep the production default: no saved
    // trajectory and no retained adjoint graph.
    return options;
}

struct StageOutcome {
    torch::Tensor K;
    bool converged = false;
    int iterations = -1;
    float final_residual = std::numeric_limits<float>::quiet_NaN();
    std::string log;
    std::string error;
};

StageOutcome solve_linear_stage(Solver* shared_solver, JvpMode mode,
                                int debug_level, bool telemetry,
                                bool nonfinite_rhs = false) {
    configure_stage_case(mode, debug_level, telemetry);
    auto options = stage_options();
    StageOutcome outcome;
    CapturedOutput captured;
    try {
        std::unique_ptr<Solver> owned_solver;
        if (!shared_solver) owned_solver = std::make_unique<Solver>(options);
        Solver& solver = shared_solver ? *shared_solver : *owned_solver;
        const auto initial = torch::linspace(0.1, 0.6, kStateSize, torch::kFloat64);
        solver.set_physics_scaling(torch::ones_like(initial));
        // This generic callback makes option parity deterministic; the real WRF
        // RHS is exercised separately by run_real_tile_stage below.
        const auto rhs = [nonfinite_rhs](const torch::Tensor& state) {
            return nonfinite_rhs
                ? torch::full_like(state, std::numeric_limits<double>::quiet_NaN())
                : 2.0 * state;
        };
        const auto result = solver.solve_stage_with_status(
            initial, torch::Tensor(), rhs, 0.125, 0.25, 2);
        outcome.K = result.K.detach().clone();
        outcome.converged = result.converged;
        outcome.iterations = result.iterations;
        outcome.final_residual = result.final_residual;
    } catch (const std::exception& error) {
        outcome.error = error.what();
    }
    outcome.log = captured.text();
    return outcome;
}

bool same_stage_result(const StageOutcome& left, const StageOutcome& right) {
    return left.converged == right.converged &&
           left.iterations == right.iterations &&
           left.final_residual == right.final_residual &&
           left.K.defined() && right.K.defined() && torch::equal(left.K, right.K);
}

int counter_total(const std::string& log, const std::string& key) {
    const std::regex pattern(key + R"(=(\d+))");
    int total = 0;
    for (std::sregex_iterator it(log.begin(), log.end(), pattern), end; it != end; ++it)
        total += std::stoi((*it)[1].str());
    return total;
}

void check_stage_diagnostic_noninterference() {
    const auto quiet_ad = solve_linear_stage(nullptr, JvpMode::Autograd, 0, false);
    const auto telemetry_ad = solve_linear_stage(nullptr, JvpMode::Autograd, 0, true);
    const auto debug_ad = solve_linear_stage(nullptr, JvpMode::Autograd, 2, false);
    const auto quiet_fd = solve_linear_stage(nullptr, JvpMode::FixedForwardFd, 0, false);
    const auto telemetry_fd = solve_linear_stage(nullptr, JvpMode::FixedForwardFd, 0, true);
    const auto debug_fd = solve_linear_stage(nullptr, JvpMode::FixedForwardFd, 2, false);

    const auto valid = [](const StageOutcome& result) {
        return result.error.empty() && result.converged && result.iterations > 0 &&
               result.K.defined() && torch::isfinite(result.K).all().item<bool>();
    };
    check(valid(quiet_ad) && valid(telemetry_ad) && valid(debug_ad),
          "AD stage converges with quiet, telemetry, and debug diagnostics");
    check(valid(quiet_fd) && valid(telemetry_fd) && valid(debug_fd),
          "fixed forward-FD stage converges with quiet, telemetry, and debug diagnostics");
    check(same_stage_result(quiet_ad, telemetry_ad) && same_stage_result(quiet_ad, debug_ad),
          "AD K, convergence, iteration count, and residual are unchanged by diagnostics");
    check(same_stage_result(quiet_fd, telemetry_fd) && same_stage_result(quiet_fd, debug_fd),
          "fixed-FD K, convergence, iteration count, and residual are unchanged by diagnostics");
    check((quiet_ad.K - quiet_fd.K).abs().max().item<double>() < 1.0e-9,
          "AD and fixed forward-FD solve the same linear implicit stage");

    const auto quiet_has_no_solver_diagnostics = [](const std::string& log) {
        return log.find("[Newton] JVP calls:") == std::string::npos &&
               log.find("[SOLVER TELEMETRY]") == std::string::npos &&
               log.find("[Newton] CONVERGED stage=") == std::string::npos &&
               log.find("[JVP DIAG]") == std::string::npos &&
               log.find("[FD DIAG]") == std::string::npos;
    };
    check(quiet_has_no_solver_diagnostics(quiet_ad.log) &&
          quiet_has_no_solver_diagnostics(quiet_fd.log),
          "debug=0 with telemetry off omits successful per-solve diagnostic labels");
    check(telemetry_ad.log.find("[SOLVER TELEMETRY]") != std::string::npos &&
          telemetry_ad.log.find("[Newton] JVP calls:") != std::string::npos &&
          telemetry_ad.log.find("total JVP time:") != std::string::npos &&
          telemetry_ad.log.find("[Newton] CONVERGED stage=") != std::string::npos,
          "telemetry=on at debug=0 emits the aggregate JVP and successful-stage records");
    check(telemetry_fd.log.find("[SOLVER TELEMETRY]") != std::string::npos &&
          telemetry_fd.log.find("[Newton] JVP calls:") != std::string::npos &&
          telemetry_fd.log.find("jvp_fd_mode=forward") != std::string::npos,
          "fixed-FD telemetry identifies the selected forward-difference mode");
    check(telemetry_ad.log.find("[JVP DIAG]") == std::string::npos &&
          telemetry_ad.log.find("[FD DIAG]") == std::string::npos &&
          telemetry_fd.log.find("[JVP DIAG]") == std::string::npos &&
          telemetry_fd.log.find("[FD DIAG]") == std::string::npos,
          "telemetry at debug=0 emits aggregates without per-call norm diagnostics");
    check(counter_total(telemetry_ad.log, "jvp_ad_calls") > 0 &&
          counter_total(telemetry_ad.log, "jvp_fd_calls") == 0,
          "AD telemetry counts AD JVPs and no FD fallbacks");
    check(counter_total(telemetry_fd.log, "jvp_ad_calls") == 0 &&
          counter_total(telemetry_fd.log, "jvp_fd_calls") > 0,
          "fixed-FD telemetry counts FD JVPs and no AD calls");
    check(debug_ad.log.find("[JVP DIAG]") != std::string::npos &&
          debug_fd.log.find("[JVP DIAG]") != std::string::npos &&
          debug_fd.log.find("[FD DIAG]") != std::string::npos,
          "debug=2 enables JVP and fixed-FD norm diagnostics");

    // Compare matched histories: the solver carries stage/predictor caches, so
    // comparing call 1 directly to call 2 confounds the telemetry flag with
    // legitimate solver-state evolution.
    Solver toggled_solver(stage_options());
    Solver quiet_solver(stage_options());
    const auto toggled_first = solve_linear_stage(
        &toggled_solver, JvpMode::FixedForwardFd, 0, true);
    const auto toggled_second = solve_linear_stage(
        &toggled_solver, JvpMode::FixedForwardFd, 0, false);
    const auto quiet_first = solve_linear_stage(
        &quiet_solver, JvpMode::FixedForwardFd, 0, false);
    const auto quiet_second = solve_linear_stage(
        &quiet_solver, JvpMode::FixedForwardFd, 0, false);
    check(valid(toggled_first) && valid(toggled_second) &&
          valid(quiet_first) && valid(quiet_second),
          "both telemetry histories converge on both solves");
    check(same_stage_result(toggled_first, quiet_first),
          "telemetry-on and quiet solvers agree for matched first-call histories");
    check(same_stage_result(toggled_second, quiet_second),
          "telemetry-on-then-off and off-only solvers agree for matched second-call histories");
    check(toggled_first.log.find("[SOLVER TELEMETRY]") != std::string::npos &&
          quiet_has_no_solver_diagnostics(toggled_second.log),
          "telemetry off is quiet again on the same solver instance");
    std::cout << "TOGGLE_HISTORY on_first_iterations=" << toggled_first.iterations
              << " on_second_iterations=" << toggled_second.iterations
              << " quiet_first_iterations=" << quiet_first.iterations
              << " quiet_second_iterations=" << quiet_second.iterations << '\n';

    const auto nonfinite = solve_linear_stage(
        nullptr, JvpMode::FixedForwardFd, 0, false, true);
    const bool failed_closed = !nonfinite.error.empty() || !nonfinite.converged;
    const bool error_visible = nonfinite.log.find("EXPLOSION GUARD") != std::string::npos ||
        nonfinite.log.find("non-finite") != std::string::npos ||
        nonfinite.log.find("NaN/Inf") != std::string::npos;
    check(failed_closed, "quiet non-finite RHS does not produce a converged stage");
    check(error_visible, "quiet mode still emits the actual non-finite/error diagnostic");
}

struct TileOutcome {
    torch::Tensor state;
    std::string log;
};

TileOutcome run_real_tile_stage(int debug_level) {
    configure_stage_case(JvpMode::Autograd, debug_level, false);
    auto& cfg = g_sdirk3_config;
    cfg.imex_split_mode = 3;
    cfg.hevi_split = false;
    cfg.split_explicit = false;
    cfg.mass_coordinate_mode = 1;
    cfg.diffusion_option = 2;
    cfg.khdif = 1000.0f;
    cfg.kvdif = 0.0f;
    cfg.precond_type = 0;
    cfg.max_newton_iter = 8;
    cfg.newton_tol = 1.0e-9f;
    cfg.newton_rtol = 0.0f;
    cfg.max_krylov_iter = 20;
    cfg.gmres_restart = 15;
    cfg.krylov_tol = 1.0e-7f;
    cfg.nk_adaptive_tol = false;
    constexpr float spacing = 1000.0f;

    TileOutcome outcome;
    CapturedOutput captured;
    wrf::sdirk3::test::TileCase tile(spacing);
    tile.solver.setTerrainSlopes(
        torch::zeros({wrf::sdirk3::test::ny, wrf::sdirk3::test::nw, wrf::sdirk3::test::nx}),
        torch::zeros({wrf::sdirk3::test::ny, wrf::sdirk3::test::nw, wrf::sdirk3::test::nx}));
    for (int j = 0; j < wrf::sdirk3::test::ny; ++j) {
        for (int k = 0; k < wrf::sdirk3::test::nz; ++k) {
            for (int i = 0; i < wrf::sdirk3::test::nu; ++i) {
                tile.u[(j * wrf::sdirk3::test::nz + k) * wrf::sdirk3::test::nu + i] =
                    i == wrf::sdirk3::test::nx ? 0.0f :
                    std::sin(2.0 * std::acos(-1.0) * i / wrf::sdirk3::test::nx);
            }
            for (int i = 0; i < wrf::sdirk3::test::nx; ++i) {
                tile.theta[(j * wrf::sdirk3::test::nz + k) * wrf::sdirk3::test::nx + i] =
                    0.5f * std::cos(2.0 * std::acos(-1.0) * i / wrf::sdirk3::test::nx) *
                    std::cos(2.0 * std::acos(-1.0) * j / (wrf::sdirk3::test::ny - 1));
            }
        }
    }
    tile.step(0.01f);
    outcome.state = tile.state();
    outcome.log = captured.text();
    return outcome;
}

void check_real_fwad_diagnostic_gates() {
    const auto quiet = run_real_tile_stage(0);
    const auto diagnostic = run_real_tile_stage(2);
    check(torch::equal(quiet.state, diagnostic.state),
          "debug=2 FWAD checks leave the real WRF tile result unchanged");
    check(quiet.log.find("[FWAD_CHECK]") == std::string::npos,
          "debug=0 suppresses the real-RHS FWAD diagnostics");
    check(quiet.log.find("[SDIRK3] U west halo max:") == std::string::npos &&
          quiet.log.find("[SDIRK3] V west halo max:") == std::string::npos &&
          quiet.log.find("[FP64 STATE CARRY]") == std::string::npos,
          "quiet TileCase omits U/V halo norms and the disabled FP64 carry report");
    check(diagnostic.log.find("Forward-mode AD active, checking W-PGF tangent propagation") != std::string::npos &&
          diagnostic.log.find("w_pgf_buoy_all (stacked PGF)") != std::string::npos &&
          diagnostic.log.find("Checking tangent at coupled-to-velocity conversion") != std::string::npos,
          "debug=2 exposes all three real W-PGF / conversion FWAD diagnostic blocks");
    check(diagnostic.log.find("[Newton] JVP calls:") != std::string::npos &&
          diagnostic.log.find("[JVP DIAG]") != std::string::npos,
          "strict tile tolerances force a real implicit AD-JVP solve for the FWAD checks");
}

}  // namespace

int main() {
    ConfigGuard restore_config;
    const WRFNewtonKrylovOptions defaults;
    check(!defaults.retain_graph_for_adjoint && !g_sdirk3_config.retain_graph_for_adjoint &&
          !g_sdirk3_config.save_trajectory,
          "ordinary solver and tile defaults do not retain an adjoint trajectory");
    try {
        check_stage_diagnostic_noninterference();
        check_real_fwad_diagnostic_gates();
    } catch (const std::exception& error) {
        check(false, std::string("unexpected exception: ") + error.what());
    }
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
