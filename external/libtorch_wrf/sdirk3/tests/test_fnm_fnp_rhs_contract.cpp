// Exercise derived vertical weights in the assembled production geopotential RHS.
#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
}

void check_fnm_fnp_rhs() {
    auto& config = wrf::sdirk3::g_sdirk3_config;
    config = wrf::sdirk3::SDIRK3Config{};
    config.debug_level = 0;
    config.imex_split_mode = 3;
    TileCase tile;
    // Establish the supported WRF topology through the actual step entry point.
    tile.step(1.0e-4f);
    tile.solver.getGridInfo()->rdnw = torch::tensor({1.0f, 2.0f, 4.0f, 8.0f});
    tile.solver.setVerticalInterpolationCoefficients(nullptr, nullptr, 0.5f, 0, 0);
    // calc_ww_cp obtains Omega from horizontal mass divergence, not from w.
    // Vertical variation in u and curvature in phi activate weighted advection.
    for (int j=0; j<ny; ++j) {
        for (int k=0; k<nz; ++k)
            for (int i=0; i<nu; ++i)
                tile.u[(j*nz+k)*nu+i] = 2.0f*i*(k+1);
        for (int k=0; k<nw; ++k)
            for (int i=0; i<nx; ++i)
                tile.ph[(j*nw+k)*nx+i] = 0.05f*k*k;
    }
    const auto state = tile.state();
    const auto phi_rhs = [&]() {
        const auto rhs = (tile.solver.*access(RhsTag{}))(state, wrf::sdirk3::RhsMode::ImplicitOnly);
        return rhs.slice(0, su+sv+sw, su+sv+2*sw).detach().clone();
    };
    const auto derived = phi_rhs();
    const float fnm_wrf[nz] = {0, 2.0f/3, 2.0f/3, 2.0f/3};
    const float fnp_wrf[nz] = {1, 1.0f/3, 1.0f/3, 1.0f/3};
    tile.solver.setVerticalInterpolationCoefficients(fnm_wrf, fnp_wrf, 0.5f, 0, 0);
    const auto supplied = phi_rhs();
    const float fnm_half[nz] = {0, 0.5f, 0.5f, 0.5f};
    const float fnp_half[nz] = {1, 0.5f, 0.5f, 0.5f};
    tile.solver.setVerticalInterpolationCoefficients(fnm_half, fnp_half, 0.5f, 0, 0);
    const auto half = phi_rhs();
    const double derived_error = (derived-supplied).abs().max().item<double>();
    const double half_error = (half-supplied).abs().max().item<double>();
    std::cout << "FNM_FNP_RHS derived_error=" << derived_error
              << " half_error=" << half_error << '\n';
    TORCH_CHECK(std::isfinite(derived_error) && derived_error < 1e-7,
                "derived weights disagree with WRF in the geopotential RHS");
    TORCH_CHECK(std::isfinite(half_error) && half_error > 1e-9,
                "geopotential RHS did not distinguish the simple-average counterexample");

    // Independent signed PH regression on the physical-dimension fixture.  The
    // perturbation is horizontal-uniform, so its RHS isolates vertical PH
    // advection while the U/V fields provide a nonzero calc_ww_cp Omega.
    {
        constexpr double mu0 = 80000.0;
        constexpr double dnw = -0.25;
        constexpr double rdnw = 4.0;
        constexpr double pi = 3.14159265358979323846;
        const double rdx = 1.0 / 100000.0;

        auto& cfg = wrf::sdirk3::g_sdirk3_config;
        cfg.imex_split_mode = 3;
        cfg.imex_enabled = false;
        cfg.mass_coordinate_mode = 1;  // WRF Omega, with horizontal-only mu.
        cfg.wrf_omega_ww_cp = false;   // raw legacy twin is ignored by mode 1.
        cfg.mu_horizontal_div_only = false;
        cfg.omega_w_blend = 1.0f;
        cfg.ph_use_rdnw_not_rdzw = true;

        TileCase tile;
        tile.step(1.0e-4f);
        // Keep dnw and rdnw mutually consistent in the test geometry.
        tile.solver.getGridInfo()->rdnw = torch::full({nz}, static_cast<float>(rdnw));
        tile.solver.getGridInfo()->dnw = torch::full({nz}, static_cast<float>(dnw));
        const float fnm[nz] = {0.0f, 0.5f, 0.5f, 0.5f};
        const float fnp[nz] = {1.0f, 0.5f, 0.5f, 0.5f};
        tile.solver.setVerticalInterpolationCoefficients(fnm, fnp, 0.5f, 0.0f, 0.0f);

        std::fill(tile.w.begin(), tile.w.end(), 0.0f);
        std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                for (int i = 0; i < nu; ++i)
                    tile.u[(j*nz + k)*nu + i] =
                        static_cast<float>(std::sin(2.0*pi*i/nx) * (1.0 + 0.2*k));
            }
        }
        for (int j = 0; j < nv; ++j) {
            for (int k = 0; k < nz; ++k) {
                for (int i = 0; i < nx; ++i) {
                    const double y = (j == 0 || j == ny) ? 0.0 : std::sin(pi*j/ny);
                    tile.v[(j*nz + k)*nx + i] =
                        static_cast<float>(y * (1.0 + 0.3*k));
                }
            }
        }

        // Independent scalar calc_ww_cp Omega.
        std::vector<double> omega(static_cast<size_t>(ny*nw*nx), 0.0);
        auto om = [&](int j, int k, int i) -> double& {
            return omega[(j*nw + k)*nx + i];
        };
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                std::vector<double> dh(nz, 0.0);
                double sum_dh = 0.0;
                for (int k = 0; k < nz; ++k) {
                    const double du = tile.u[(j*nz + k)*nu + i + 1] -
                                      tile.u[(j*nz + k)*nu + i];
                    const double dv = tile.v[((j + 1)*nz + k)*nx + i] -
                                      tile.v[(j*nz + k)*nx + i];
                    dh[k] = rdx*mu0*(du + dv);
                    sum_dh += dh[k];
                }
                const double dmdt = dnw*sum_dh;
                om(j, 0, i) = 0.0;
                for (int k = 0; k < nz - 1; ++k)
                    om(j, k + 1, i) = om(j, k, i) - dnw*dmdt - dnw*dh[k];
                om(j, nz, i) = 0.0;
            }
        }

        const auto base_state = tile.state();
        auto pert_state = base_state.clone();
        auto pert_ph = pert_state.slice(0, su + sv + sw, su + sv + 2*sw)
                                  .view({ny, nw, nx});
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k <= nz; ++k)
                for (int i = 0; i < nx; ++i)
                    pert_ph[j][k][i] = 1000.0f * k * k;

        const auto ph_block = [](const torch::Tensor& rhs) {
            return rhs.slice(0, su + sv + sw, su + sv + 2*sw)
                      .view({ny, nw, nx});
        };
        const auto delta_for = [&](bool hevi, wrf::sdirk3::RhsMode mode) {
            cfg.hevi_split = hevi;
            const auto base = ph_block((tile.solver.*access(RhsTag{}))(
                base_state, mode));
            const auto pert = ph_block((tile.solver.*access(RhsTag{}))(
                pert_state, mode));
            return pert - base;
        };

        auto expected = torch::zeros({ny, nw, nx}, torch::kFloat32);
        for (int j = 0; j < ny; ++j) {
            for (int k = 1; k < nz; ++k) {
                for (int i = 0; i < nx; ++i) {
                    const double upper = rdnw *
                        (1000.0*(k + 1)*(k + 1) - 1000.0*k*k);
                    const double lower = rdnw *
                        (1000.0*k*k - 1000.0*(k - 1)*(k - 1));
                    expected[j][k][i] = static_cast<float>(
                        om(j, k, i) * 0.5 * (upper + lower) / mu0);
                }
            }
        }

        cfg.hevi_split = false;
        const auto full = delta_for(false, wrf::sdirk3::RhsMode::Full);
        const auto full_error = (full - expected).abs().max().item<double>();
        const auto full_opposite_error = (full + expected).abs().max().item<double>();
        const auto expected_norm = expected.norm().item<double>();

        const auto hevi_full = delta_for(true, wrf::sdirk3::RhsMode::Full);
        const auto hevi_imp = delta_for(true, wrf::sdirk3::RhsMode::ImplicitOnly);
        const auto hevi_exp = delta_for(true, wrf::sdirk3::RhsMode::ExplicitOnly);
        const auto hevi_sum_error =
            (hevi_full - hevi_imp - hevi_exp).abs().max().item<double>();
        const auto hevi_full_error =
            (hevi_full - expected).abs().max().item<double>();
        const auto hevi_exp_norm = hevi_exp.abs().max().item<double>();
        const auto lower_boundary = full.select(1, 0).abs().max().item<double>();
        const auto upper_boundary = full.select(1, nz).abs().max().item<double>();
        const double tol = 1.0e-6;

        std::cout << "PH_SIGN layout=physical"
                  << " expected_norm=" << expected_norm
                  << " full_error=" << full_error
                  << " full_opposite_error=" << full_opposite_error
                  << " hevi_full_error=" << hevi_full_error
                  << " hevi_sum_error=" << hevi_sum_error
                  << " hevi_exp_norm=" << hevi_exp_norm
                  << " bottom=" << lower_boundary
                  << " top=" << upper_boundary << '\n';
        TORCH_CHECK(std::isfinite(full_error) && expected_norm > 1.0e-5,
                    "PH signed probe did not produce an informative nonzero flux");
        TORCH_CHECK(full_error <= tol,
                    "PH vertical term has the wrong sign or scale: error=", full_error,
                    " opposite-sign error=", full_opposite_error);
        TORCH_CHECK(hevi_full_error <= tol && hevi_sum_error <= tol,
                    "HEVI PH split does not reproduce the signed full term: full=",
                    hevi_full_error, " sum=", hevi_sum_error);
        TORCH_CHECK(hevi_exp_norm <= tol && lower_boundary <= tol && upper_boundary <= tol,
                    "HEVI explicit PH or vertical boundary is nonzero: explicit=",
                    hevi_exp_norm, " bottom=", lower_boundary, " top=", upper_boundary);
    }
}
