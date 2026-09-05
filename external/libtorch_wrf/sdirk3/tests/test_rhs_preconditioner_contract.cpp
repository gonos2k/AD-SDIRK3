// Compare production FGMRES budgets and preconditioning with fixed A, b, and S=I.
#include "tile_test_fixture.h"

#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_newton_solver.h"
#include "../wrf_sdirk3_jvp_fwad_or_fd.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>

namespace {

using wrf::sdirk3::UnifiedPreconditioner;
using namespace wrf::sdirk3::test;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;

} // namespace

int main() {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.precond_acoustic_4x4 = 1;
    cfg.imex_split_mode = 3;

    TileCase tile;
    // A completed production step establishes the WRF geometry/omega contract.
    tile.step(1.0e-4f);
    const auto U = tile.state();
    const auto rhs = [&](const torch::Tensor& x) {
        return (tile.solver.*access(RhsTag{}))(x, wrf::sdirk3::RhsMode::ImplicitOnly);
    };
    const auto F0 = rhs(U);
    const float h = 0.1f * 0.4358665215f;
    const auto jvp = [&](const torch::Tensor& v) {
        bool used_fd = false;
        std::string reason;
        const auto result = wrf::sdirk3::compute_jvp_fwad_or_fd(
            rhs, U, v, 0, 0.0f, &used_fd, &reason);
        TORCH_CHECK(!used_fd, "production RHS fwAD fell back to FD: ", reason);
        return result;
    };
    auto A = [&](const torch::Tensor& v) {
        const auto Jv = jvp(v);
        return v - h*Jv;
    };
    torch::manual_seed(20260905);
    auto probe_v = torch::randn_like(U);
    const auto j1 = jvp(probe_v);
    TORCH_CHECK(torch::isfinite(j1).all().item<bool>(), "production fwAD JVP is non-finite");

    auto grid = tile.solver.getGridInfo();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    UnifiedPreconditioner P(grid, physics, 0.1f, 0.4358665215f);
    const auto mu_pert = U.slice(0, total-sm, total).reshape({ny,nx});
    P.bind_stage_state_or_throw(mu_pert, 1);
    P.update(U, 0.1f, 0.4358665215f);
    std::function<torch::Tensor(const torch::Tensor&)> M =
        [&](const torch::Tensor& v) { return P.apply(v); };
    const auto b = torch::randn_like(U) * 1.0e-3f;
    const auto x0 = torch::zeros_like(U);
    using wrf::sdirk3::krylov_methods::solve_fgmres;
    const float tol = 1.0e-5f;
    for (int budget : {8, 16}) {
        const int restarts = budget / 8;
        std::function<torch::Tensor(const torch::Tensor&)> no_pc;
        const auto none = solve_fgmres(A, b, x0, 0, 0.0f, 8, tol, restarts,
                                       no_pc, nullptr, nullptr, false, false);
        const auto actual = solve_fgmres(A, b, x0, 0, 0.0f, 8, tol, restarts,
                                         M, nullptr, nullptr, false, false);
        const auto true_rel = [&](const torch::Tensor& x) {
            return (b-A(x)).norm().item<double>() / b.norm().item<double>();
        };
        const double none_rel = true_rel(none.x);
        const double actual_rel = true_rel(actual.x);
        std::cout << "RHS_PRECOND_CONTRACT fixed_state=1 fixed_b=1 fixed_metric=1"
                  << " budget=" << budget
                  << " no_pc_iterations=" << none.iterations
                  << " no_pc_true_rel=" << none_rel
                  << " actual_pc_iterations=" << actual.iterations
                  << " actual_pc_true_rel=" << actual_rel
                  << " actual_pc_success=" << actual.success << '\n';
        TORCH_CHECK(std::isfinite(none_rel) && std::isfinite(actual_rel),
                    "production FGMRES returned a non-finite true residual");
        TORCH_CHECK(none.success && none_rel <= tol,
                    "unpreconditioned control failed its true-residual tolerance");
        if (actual.success)
            TORCH_CHECK(actual_rel <= tol, "preconditioned solver claimed success above true-residual tolerance");
    }
    TORCH_CHECK(torch::isfinite(F0).all().item<bool>(), "actual RHS produced non-finite values");
    std::cout << "RHS_PRECOND_CONTRACT NOTE: M is an approximate inverse; this fixture does not assert M=A or M=A^-1.\n";
    return 0;
}
