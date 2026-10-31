#include "tile_test_fixture.h"
#include "wrf_sdirk3_jvp_autograd.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include <unordered_set>

namespace wrf::sdirk3::test {
struct PreparedRhsTest {
    static torch::Tensor rhs(TileCase& tile, const torch::Tensor& x, bool fail = false) {
        auto evaluate = [&]() -> torch::Tensor {
            auto y = tile.solver.computeUnifiedRHS(x, RhsMode::ImplicitOnly);
            if (fail) throw std::runtime_error("injected RHS failure");
            return y;
        };
        return x.requires_grad() ? tile.solver.withPreparedRhsCleanup(evaluate) : evaluate();
    }
    static void rejectMoistTile(TileCase& tile, const torch::Tensor& state) {
        auto& cfg = g_sdirk3_config;
        cfg.reuse_linearization = true;
        cfg.internal_fp64_state_carry = true;
        tile.solver.prepared_rhs_enabled_ = true;
        tile.solver.n_moist_ = 1;
        bool rejected = false;
        try {
            tile.solver.solveImplicitStage(state, {}, 0.1, 0.5, 2, {}, {});
        } catch (const c10::Error& e) {
            rejected = std::string(e.what()).find("forward-only dry single-tile") != std::string::npos;
        }
        tile.solver.n_moist_ = 0;
        tile.solver.prepared_rhs_enabled_ = false;
        cfg.reuse_linearization = false;
        cfg.internal_fp64_state_carry = false;
        TORCH_CHECK(rejected, "reuse accepted a Tile with moisture species");
    }
    static void throwAfterRhs(TileCase& tile, const torch::Tensor& x,
                              const std::function<void(const torch::Tensor&)>& inspect) {
        tile.solver.withPreparedRhsCleanup([&]() -> torch::Tensor {
            auto y = tile.solver.computeUnifiedRHS(x, RhsMode::ImplicitOnly);
            inspect(y);
            throw std::runtime_error("injected RHS failure");
        });
    }
    static std::vector<torch::Tensor*> owners(TileCase& t) {
        auto& s = t.solver;
        return {&s.p_pert_, &s.ru_tend_, &s.rv_tend_, &s.rw_tend_, &s.t_tend_,
                &s.mu_tend_, &s.ph_tend_, &s.qv_tend_, &s.dpx_work_, &s.ru_adv_z_work_,
                &s.rv_adv_z_work_, &s.rw_adv_z_work_, &s.t_adv_z_work_};
    }
    static void checkOwnerValues(TileCase& tile, const torch::Tensor& x, bool fail) {
        std::vector<torch::Tensor> before;
        auto snapshot = [&]() -> torch::Tensor {
            auto y = tile.solver.computeUnifiedRHS(x, RhsMode::ImplicitOnly);
            for (auto* owner : owners(tile))
                before.push_back(owner->defined() ? owner->detach().clone() : torch::Tensor());
            if (fail) throw std::runtime_error("injected RHS failure");
            return y;
        };
        try {
            tile.solver.withPreparedRhsCleanup(snapshot);
            TORCH_CHECK(!fail, "injected failure was not raised");
        } catch (const std::runtime_error&) {
            if (!fail) throw;
        }
        auto current = owners(tile);
        TORCH_CHECK(before.size() == current.size());
        for (size_t i = 0; i < before.size(); ++i) {
            TORCH_CHECK(before[i].defined() == current[i]->defined());
            if (before[i].defined()) TORCH_CHECK(torch::equal(before[i], *current[i]),
                                                "owner values changed during cleanup");
        }
    }
};
}

using namespace wrf::sdirk3;
using namespace wrf::sdirk3::test;
using Node = torch::autograd::Node;
static std::vector<std::weak_ptr<Node>> nodes(const std::vector<torch::Tensor>& roots) {
    std::vector<std::weak_ptr<Node>> result;
    std::vector<std::shared_ptr<Node>> pending;
    std::unordered_set<Node*> seen;
    for (const auto& root : roots) if (root.grad_fn()) pending.push_back(root.grad_fn());
    while (!pending.empty()) {
        auto node = std::move(pending.back()); pending.pop_back();
        if (!seen.insert(node.get()).second) continue;
        result.push_back(node);
        for (const auto& edge : node->next_edges()) if (edge.function) pending.push_back(edge.function);
    }
    return result;
}
static void checkNewtonReuse() {
    auto& cfg = g_sdirk3_config;
    cfg = SDIRK3Config{};
    cfg.use_autograd = true;
    cfg.reuse_linearization = false; // Eligibility is solver-local.
    cfg.direct_u_solve_thresh = 0.0f;
    cfg.precond_type = 0;
    const int n = 10;
    auto matrix = torch::zeros({n, n}, torch::kFloat64);
    for (int i = 0; i < n; ++i) {
        matrix[i][i] = -0.3 - 0.02 * i;
        if (i > 0) matrix[i][i - 1] = 0.17;
        if (i + 1 < n) matrix[i][i + 1] = -0.04;
    }
    const auto base = torch::linspace(0.1, 1.0, n, torch::kFloat64);
    const auto forcing = torch::linspace(-0.2, 0.3, n, torch::kFloat64);
    constexpr double dt = 0.3, gamma = 0.5;
    const auto system = torch::eye(n, base.options()) - dt * gamma * matrix;
    const auto right = matrix.mv(base) + forcing;
    const auto exact = torch::linalg_solve(system, right);
    struct LogCapture {
        std::ostringstream output;
        std::streambuf* previous;
        LogCapture() : previous(std::cerr.rdbuf(output.rdbuf())) {}
        ~LogCapture() { std::cerr.rdbuf(previous); }
    };
    auto run = [&](bool reuse, bool reject_preparation) {
        WRFNewtonKrylovOptions options;
        options.nx = options.ny = options.nz = 1;
        options.nx_u = options.ny_v = options.nz_w = 2;
        options.use_preconditioner = options.use_adaptive_tolerances = false;
        options.newton_tol = 1e-10f;
        options.newton_rtol = 0.0f;
        options.krylov_tol = 1e-10f;
        options.max_newton_iter = 16;
        options.gmres_restart = n;
        options.max_krylov_iter = 20;
        options.reuse_linearization = reuse;
        int preparations = 0;
        auto rhs = [&](const torch::Tensor& x) -> torch::Tensor {
            if (x.requires_grad()) {
                ++preparations;
                if (reject_preparation) throw std::runtime_error("injected preparation failure");
            }
            return matrix.mv(x) + forcing;
        };
        WRFNewtonKrylovSolver solver(options);
        solver.set_physics_scaling(torch::ones_like(base));
        LogCapture capture;
        const auto result = solver.solve_stage_with_status(base, {}, rhs, dt, gamma, 2);
        torch::NoGradGuard no_grad;
        TORCH_CHECK(result.converged, "linear Newton did not converge: ", result.message);
        TORCH_CHECK((result.K - exact).norm().item<double>() < 1e-8,
                    "linear Newton differs from dense solution");
        TORCH_CHECK((right - system.mv(result.K)).norm().item<double>() < 1e-8,
                    "linear Newton true residual is too large");
        const auto log = capture.output.str();
        if (reuse) {
            TORCH_CHECK(preparations > 0 && log.find("reference_calls=1") != std::string::npos &&
                        log.find("preparation_calls=1") != std::string::npos,
                        "reuse reference/preparation were not exercised");
            if (reject_preparation) {
                TORCH_CHECK(log.find("status=preparation_error") != std::string::npos,
                            "preparation fallback was not reported");
            } else {
                const auto enabled = log.find("status=enabled");
                TORCH_CHECK(enabled != std::string::npos &&
                            log.find("validation_apply_calls=1", enabled) != std::string::npos,
                            "reuse or its derivative guard was not exercised");
                const auto count = log.find("prepared_apply_calls=", enabled);
                TORCH_CHECK(count != std::string::npos &&
                            std::stoi(log.substr(count + std::string("prepared_apply_calls=").size())) > 0,
                            "prepared JVP was not applied");
            }
        } else {
            TORCH_CHECK(preparations == 0, "default-off path prepared a graph");
        }
        return result.K.detach().clone();
    };
    const auto baseline = run(false, false);
    const auto prepared = run(true, false);
    const auto fallback = run(true, true);
    TORCH_CHECK(torch::allclose(baseline, prepared, 1e-9, 1e-10) &&
                torch::allclose(baseline, fallback, 1e-9, 1e-10),
                "reuse or fallback changed linear solution");
}

int main() {
    try {
        torch::set_num_threads(1);
        torch::manual_seed(42);
        checkNewtonReuse();
        for (int mode : {3, 4}) {
            auto& cfg = g_sdirk3_config;
            cfg = SDIRK3Config{};
            cfg.internal_fp64 = true; cfg.imex_split_mode = mode;
            cfg.mass_coordinate_mode = 1; cfg.diffusion_option = 1;
            cfg.khdif = 0; cfg.kvdif = 0; cfg.use_autograd = true;
            cfg.precond_type = 0; cfg.n_threads = 1;
            cfg.max_newton_iter = 40; cfg.newton_tol = 1e-9;
            cfg.krylov_tol = 1e-7; cfg.stage_fail_action = 1;
            TileCase tile(5000);
            tile.step(1e-4f);
            auto state = tile.state().to(torch::kFloat64);
            if (mode == 3) PreparedRhsTest::rejectMoistTile(tile, state);
            auto rhs = [&](const torch::Tensor& x) { return PreparedRhsTest::rhs(tile, x); };
            auto direction = torch::randn_like(state);
            bool fallback = false;
            auto reference = compute_jvp_fwad_or_fd(rhs, state, direction, 0, 0, &fallback);
            TORCH_CHECK(!fallback, "fwAD reference fell back");
            auto baseline = rhs(state).detach().clone();
            std::vector<std::weak_ptr<Node>> weak;
            {
                torch::AutoGradMode enabled(true);
                auto x = state.detach().clone().set_requires_grad(true);
                auto y = rhs(x);
                auto seed = torch::zeros_like(y).set_requires_grad(true);
                auto g = torch::autograd::grad({y}, {x}, {seed}, true, true, false)[0];
                weak = nodes({y, g});
                TORCH_CHECK(!weak.empty());
                for (auto* owner : PreparedRhsTest::owners(tile))
                    if (owner->defined()) TORCH_CHECK(!owner->requires_grad());
            }
            for (const auto& node : weak) TORCH_CHECK(node.expired(), "RHS graph retained by Tile");
            {
                PreparedJvp prepared(rhs, state);
                TORCH_CHECK(torch::equal(prepared.primal(), baseline), "RHS primal mismatch");
                TORCH_CHECK(torch::allclose(prepared.apply(direction), reference, 1e-10, 1e-12),
                            "RHS JVP mismatch");
            }
            {
                auto x = state.detach().clone().set_requires_grad(true);
                PreparedRhsTest::checkOwnerValues(tile, x, false);
                PreparedRhsTest::checkOwnerValues(tile, x, true);
            }
            weak.clear();
            try {
                auto x = state.detach().clone().set_requires_grad(true);
                PreparedRhsTest::throwAfterRhs(tile, x, [&](const torch::Tensor& y) {
                    weak = nodes({y});
                });
                TORCH_CHECK(false, "injected exception was not raised");
            } catch (const std::runtime_error&) {
                for (auto* owner : PreparedRhsTest::owners(tile))
                    if (owner->defined()) TORCH_CHECK(!owner->requires_grad(), "exception retained graph");
            }
            TORCH_CHECK(!weak.empty());
            for (const auto& node : weak) TORCH_CHECK(node.expired(), "exception graph retained by Tile");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
