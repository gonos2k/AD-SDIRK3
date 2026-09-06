// Compare production FGMRES budgets and preconditioning with fixed A, b, and S=I.
#include "tile_test_fixture.h"

#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_newton_solver.h"
#include "../wrf_sdirk3_jvp_fwad_or_fd.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

int main(int argc, char** argv) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.precond_acoustic_4x4 = 1;
    cfg.imex_split_mode = 3;
    const bool phi_unity = argc > 1 && std::string(argv[1]) == "--phi-unity";
    if (phi_unity) {
        ::setenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY", "1", /*overwrite=*/1);
    } else {
        ::unsetenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY");
    }

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

    // Isolate the raw Phi -> U/V units from the production RHS.  The direction is
    // horizontally varying but constant in k, so it exercises only the horizontal
    // geopotential gradient and does not introduce an EOS/thermodynamic perturbation.
    // TileCase has a scalar 80,000 Pa base column, so the weighted PGF mass cancels
    // the coupled-momentum -> velocity conversion exactly on this fixture:
    //   A_u,phi = h * dPhi/dx,   A_v,phi = h * dPhi/dy.
    constexpr float phi_step_x = 1.0e4f;  // m^2/s^2 per x cell
    constexpr float phi_step_y = 7.0e3f;  // m^2/s^2 per y cell
    constexpr float fixture_mu0 = 8.0e4f; // Pa, TileCase::mass
    std::vector<float> phi_gradient(sw, 0.0f);
    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nw; ++k) {
            for (int i = 0; i < nx; ++i) {
                phi_gradient[(j*nw + k)*nx + i] =
                    phi_step_x * static_cast<float>(i) +
                    phi_step_y * static_cast<float>(j);
            }
        }
    }
    auto v_phi_pgf = torch::zeros_like(U);
    v_phi_pgf.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::from_blob(phi_gradient.data(), {sw}, U.options()).clone());
    const auto A_phi_pgf = A(v_phi_pgf);
    const auto A_u_phi_pgf = A_phi_pgf.slice(0, 0, su).reshape({ny, nz, nu});
    const auto A_v_phi_pgf = A_phi_pgf.slice(0, su, su + sv).reshape({nv, nz, nx});
    // Exclude the symmetric/wrap boundaries.  Check two mass levels explicitly;
    // vertical constancy means both levels must carry the same scalar response.
    const auto A_u_phi_interior = A_u_phi_pgf.slice(1, 1, 3).slice(2, 1, nx - 1);
    const auto A_v_phi_interior = A_v_phi_pgf.slice(0, 1, ny).slice(1, 1, 3);
    const float expected_A_u_phi = h * phi_step_x / tile.spacing;
    const float expected_A_v_phi = h * phi_step_y / tile.spacing;
    const double u_phi_err =
        (A_u_phi_interior - expected_A_u_phi).abs().max().item<double>() /
        std::max(std::abs(static_cast<double>(expected_A_u_phi)), 1.0e-30);
    const double v_phi_err =
        (A_v_phi_interior - expected_A_v_phi).abs().max().item<double>() /
        std::max(std::abs(static_cast<double>(expected_A_v_phi)), 1.0e-30);
    std::cout << "RHS_PRECOND_PHI_PGF fixed_mu0=" << fixture_mu0
              << " expected_A_u_phi=" << expected_A_u_phi
              << " expected_A_v_phi=" << expected_A_v_phi
              << " A_u_phi_rel_err=" << u_phi_err
              << " A_v_phi_rel_err=" << v_phi_err
              << " levels_checked=2 interior=1\n";
    TORCH_CHECK(torch::isfinite(A_u_phi_interior).all().item<bool>() &&
                torch::isfinite(A_v_phi_interior).all().item<bool>(),
                "Phi-only PGF probe produced a non-finite U/V response");
    TORCH_CHECK(u_phi_err < 5.0e-3 && v_phi_err < 5.0e-3,
                "Phi-only PGF probe disagrees with the raw weighted-PGF contract: U rel=",
                u_phi_err, " V rel=", v_phi_err);

    torch::manual_seed(20260905);
    auto probe_v = torch::randn_like(U);
    const auto j1 = jvp(probe_v);
    TORCH_CHECK(torch::isfinite(j1).all().item<bool>(), "production fwAD JVP is non-finite");

    // Directional block readout from the actual ImplicitOnly JVP.  These are
    // measurements of A=I-hJ in raw packed coordinates, not assertions that
    // the approximate preconditioner must equal A.
    auto v_phi = torch::zeros_like(U);
    v_phi.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::randn({sw}, U.options()));
    auto v_mu = torch::zeros_like(U);
    v_mu.slice(0, total-sm, total).copy_(torch::randn({sm}, U.options()));
    const auto A_phi = v_phi - h*jvp(v_phi);
    const auto A_mu = v_mu - h*jvp(v_mu);
    const double a_mu_phi = A_phi.slice(0, total-sm, total).norm().item<double>() /
                            std::max(v_phi.slice(0, su + sv + sw, su + sv + 2*sw).norm().item<double>(), 1e-30);
    const double a_phi_mu = A_mu.slice(0, su + sv + sw, su + sv + 2*sw).norm().item<double>() /
                            std::max(v_mu.slice(0, total-sm, total).norm().item<double>(), 1e-30);
    std::cout << "RHS_PRECOND_BLOCKS mode=" << (phi_unity ? "phi_unity" : "shipped")
              << " rel_A_mu_phi=" << a_mu_phi
              << " rel_A_phi_mu=" << a_phi_mu << '\n';

    auto grid = tile.solver.getGridInfo();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    UnifiedPreconditioner P(grid, physics, 0.1f, 0.4358665215f);
    const auto mu_pert = U.slice(0, total-sm, total).reshape({ny,nx});
    if (P.raw_principal_enabled())
        P.bind_raw_principal_state_or_throw(U, 1, "fixed RHS contract state");
    else
        P.bind_stage_state_or_throw(mu_pert, 1);
    P.update(U, 0.1f, 0.4358665215f);
    std::function<torch::Tensor(const torch::Tensor&)> M =
        [&](const torch::Tensor& v) { return P.apply(v); };

    // Observe one independent M column.  This is deliberately a unit residual in a
    // single interior Phi mass cell, rather than the PGF gradient direction above:
    // it reports the current inverse-preconditioner U/V response without folding a
    // spatial stencil into the input.  The controlled comparison uses the source's
    // existing MU-PHI Schur switch; no production coefficient is changed here.
    constexpr int probe_j = 2;
    constexpr int probe_k = 1;
    constexpr int probe_i = 3;
    std::vector<float> phi_column_data(sw, 0.0f);
    phi_column_data[(probe_j*nw + probe_k)*nx + probe_i] = 1.0f;
    auto v_phi_column = torch::zeros_like(U);
    v_phi_column.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::from_blob(phi_column_data.data(), {sw}, U.options()).clone());
    const char* prior_mu_phi_zero = std::getenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const bool had_prior_mu_phi_zero = prior_mu_phi_zero != nullptr;
    const std::string prior_mu_phi_zero_value = had_prior_mu_phi_zero ? prior_mu_phi_zero : "";
    ::unsetenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const auto M_phi_column = M(v_phi_column);
    ::setenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO", "1", /*overwrite=*/1);
    const auto M_phi_column_mu_schur_zero = M(v_phi_column);
    if (had_prior_mu_phi_zero)
        ::setenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO", prior_mu_phi_zero_value.c_str(), /*overwrite=*/1);
    else
        ::unsetenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const auto M_u_phi_column = M_phi_column.slice(0, 0, su).reshape({ny, nz, nu});
    const auto M_v_phi_column = M_phi_column.slice(0, su, su + sv).reshape({nv, nz, nx});
    const auto M_u_phi_column_zero =
        M_phi_column_mu_schur_zero.slice(0, 0, su).reshape({ny, nz, nu});
    const auto M_v_phi_column_zero =
        M_phi_column_mu_schur_zero.slice(0, su, su + sv).reshape({nv, nz, nx});
    const float m_u_phi = M_u_phi_column[probe_j][probe_k][probe_i].item<float>();
    const float m_v_phi = M_v_phi_column[probe_j][probe_k][probe_i].item<float>();
    const float m_u_phi_zero = M_u_phi_column_zero[probe_j][probe_k][probe_i].item<float>();
    const float m_v_phi_zero = M_v_phi_column_zero[probe_j][probe_k][probe_i].item<float>();
    const float current_M_u_phi_model = -h / (fixture_mu0 * tile.spacing);
    const float current_M_v_phi_model = -h / (fixture_mu0 * tile.spacing);
    std::cout << "RHS_PRECOND_PHI_COLUMN model="
              << (P.raw_principal_enabled() ? "raw_principal" : "legacy")
              << " legacy_mu_phi_switch_active=" << !P.raw_principal_enabled()
              << " input_j=" << probe_j
              << " input_k=" << probe_k << " input_i=" << probe_i
              << " M_u_phi=" << m_u_phi << " M_v_phi=" << m_v_phi
              << " M_u_phi_mu_schur_zero=" << m_u_phi_zero
              << " M_v_phi_mu_schur_zero=" << m_v_phi_zero
              << " legacy_local_model_u=" << current_M_u_phi_model
              << " legacy_local_model_v=" << current_M_v_phi_model << '\n';
    TORCH_CHECK(std::isfinite(m_u_phi) && std::isfinite(m_v_phi) &&
                std::isfinite(m_u_phi_zero) && std::isfinite(m_v_phi_zero),
                "Phi-column preconditioner response is non-finite");

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
        std::cout << "RHS_PRECOND_CONTRACT mode=" << (phi_unity ? "phi_unity" : "shipped")
                  << " fixed_state=1 fixed_b=1 fixed_metric=1"
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
