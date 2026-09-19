#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
struct CoefficientsTag {
    using type = void (TileSDIRK3UnifiedSolver::*)
        (const float*, const float*, const float*, const float*);
    friend type access(CoefficientsTag);
};
template struct Accessor<CoefficientsTag,
    &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;


void check_constant_theta(int direction, bool hevi) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.hevi_split = hevi;
    TileCase tile;
    tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                              1, nx, 1, ny, 1, nz,
                              1, nu, 1, nv, 1, nw);
    tile.step(1.0e-4f);
    auto state = torch::zeros({total}, torch::kFloat32);
    constexpr double pi = 3.14159265358979323846;
    // The fixture has theta_base=300 K. theta'=0 is therefore constant full
    // potential temperature. Horizontal mass divergence is nonzero in the first
    // case; pure vertical motion has zero diagnosed mass-coordinate Omega.
    if (direction == 2) {
        auto w = state.slice(0, su + sv, su + sv + sw).view({ny, nw, nx});
        for (int k = 1; k < nz; ++k)
            w.select(1, k).fill_(0.1 * std::sin(pi * k / nz));
    } else if (direction == 1) {
        auto v = state.slice(0, su, su + sv).view({nv, nz, nx});
        for (int j = 1; j < ny - 1; ++j)
            v.select(0, j).fill_(4.0 * std::sin(pi * j / (ny - 1)));
    } else {
        auto u = state.slice(0, 0, su).view({ny, nz, nu});
        for (int i = 0; i < nu; ++i)
            u.select(2, i).fill_(4.0 * std::sin(2.0 * pi * i / (nx - 1)));
    }
    double mu_norm = 0.0;
    for (const auto mode : {RhsMode::Full, RhsMode::ExplicitOnly, RhsMode::ImplicitOnly}) {
        const auto rhs = (tile.solver.*access(RhsTag{}))(state, mode);
        TORCH_CHECK(torch::isfinite(rhs).all().item<bool>(), "non-finite dry RHS");
        const auto theta = rhs.slice(0, su + sv + 2 * sw, total - sm);
        const double maximum = theta.abs().max().item<double>();
        std::cout << "CONSTANT_THETA direction=" << direction << " hevi=" << hevi
                  << " mode=" << static_cast<int>(mode) << " theta_max=" << maximum << '\n';
        TORCH_CHECK(maximum == 0.0,
                    "dry constant potential temperature gained a spurious source: ", maximum);
        if (mode == RhsMode::Full)
            mu_norm = rhs.slice(0, total - sm, total).norm().item<double>();
    }
    TORCH_CHECK(direction == 2 ? mu_norm == 0.0 : mu_norm > 1.0e-6,
                "mass divergence control was not informative: ", mu_norm);
    std::cout << "CONSTANT_THETA full_mu_norm=" << mu_norm << '\n';

    // A nonzero constant perturbation also exercises cancellation between its
    // conservative flux and the mass product rule. The split parts may be
    // nonzero individually, so this additional invariant is on the full RHS.
    constexpr double perturbation = 4.0;
    state.slice(0, su + sv + 2 * sw, total - sm).fill_(perturbation);
    const auto full = (tile.solver.*access(RhsTag{}))(state, RhsMode::Full);
    const double theta_max = full.slice(0, su + sv + 2 * sw, total - sm)
                                 .abs().max().item<double>();
    const double rate = perturbation * full.slice(0, total - sm, total)
                            .abs().max().item<double>() / 80000.0;
    const double tolerance = 32.0 * std::numeric_limits<float>::epsilon() * rate;
    TORCH_CHECK(theta_max <= tolerance,
                "constant theta transport/mass cancellation failed: ", theta_max,
                " tolerance=", tolerance);
    std::cout << "CONSTANT_THETA perturbation=" << perturbation
              << " theta_max=" << theta_max << " tolerance=" << tolerance << '\n';

}

void check_parameterized_constant_theta(bool packed, bool hevi, int order) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.hevi_split = hevi;
    cfg.advection_order = order;
    TileCase tile;
    if (packed)
        tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                                  1, nx, 1, ny, 1, nz,
                                  1, nu, 1, nv, 1, nw);
    const int n = nx - (packed ? 1 : 0);
    const int m = ny - (packed ? 1 : 0);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            tile.mass_map[j * nx + i] = 1.1f + 0.03f *
                std::cos(0.4f * (i % n) + 0.2f * std::min(j, m - 1));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nu; ++i)
            tile.u_map[j * nu + i] = 0.9f + 0.02f *
                std::sin(0.5f * (i % n) + 0.3f * std::min(j, m - 1));
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nx; ++i)
            tile.v_map[j * nx + i] = 1.2f + 0.02f *
                std::cos(0.3f * (i % n) + 0.4f * std::min(j, m));
    tile.step(1.0e-4f);  // installs map arrays
    // Apply hybrid coefficients after the fixture's setup step, which installs
    // its own sigma-coordinate coefficients. The setter reads nw entries.
    const std::vector<float> c1h{0.5f, 0.75f, 1.25f, 1.5f, 1.0f};
    const std::vector<float> c2h(nw, 1000.0f);
    (tile.solver.*access(CoefficientsTag{}))(
        tile.one.data(), tile.zero.data(), c1h.data(), c2h.data());

    auto state = torch::zeros({total}, torch::kFloat32);
    auto u = state.slice(0, 0, su).view({ny, nz, nu});
    auto v = state.slice(0, su, su + sv).view({nv, nz, nx});
    auto mu = state.slice(0, total - sm, total).view({ny, nx});
    auto ua = u.accessor<float, 3>();
    auto va = v.accessor<float, 3>();
    auto ma = mu.accessor<float, 2>();
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i)
            ma[j][i] = 900.0f * std::sin(0.6f * (i % n) + 0.4f * std::min(j, m - 1));
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nu; ++i)
                ua[j][k][i] = 3.0f * std::sin(0.6f * (i % n) +
                                              0.2f * k + 0.3f * std::min(j, m - 1));
    }
    for (int j = 1; j < m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i)
                va[j][k][i] = 2.0f * std::cos(0.4f * j + 0.3f * k + 0.2f * (i % n));
    if (packed) v.select(0, m + 1).copy_(-v.select(0, m - 1));
    state.slice(0, su + sv + 2 * sw, total - sm).fill_(4.0f);

    // Independent scalar face-divergence and Omega recurrence. The physical
    // cells exclude copied endpoints; no production averaging/helper is used.
    auto expected = torch::zeros({m, n}, torch::kFloat64);
    auto ea = expected.accessor<double, 2>();
    double omega_max = 0.0, top_omega_max = 0.0;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            auto mass = [&](int jj, int ii) {
                return 80000.0 + ma[std::clamp(jj, 0, m - 1)][(ii + n) % n];
            };
            double divv[nz];
            double dmdt = 0.0;
            const double map = tile.mass_map[j * nx + i];
            for (int k = 0; k < nz; ++k) {
                auto xflux = [&](int face) {
                    const double face_mass = 0.5 * (mass(j, face - 1) + mass(j, face));
                    return (c1h[k] * face_mass + c2h[k]) * ua[j][k][face] /
                           tile.u_map[j * nu + face];
                };
                auto yflux = [&](int face) {
                    const double face_mass = 0.5 * (mass(face - 1, i) + mass(face, i));
                    return (c1h[k] * face_mass + c2h[k]) * va[face][k][i] /
                           tile.v_map[face * nx + i];
                };
                divv[k] = -0.25 * map *
                    ((xflux(i + 1) - xflux(i)) + (yflux(j + 1) - yflux(j))) / tile.spacing;
                dmdt += divv[k];
            }
            ea[j][i] = map * dmdt;  // both mass-point maps equal `map`
            double omega = 0.0;
            for (int k = 0; k < nz - 1; ++k) {
                omega += 0.25 * c1h[k] * dmdt - divv[k];
                omega_max = std::max(omega_max, std::abs(omega));
            }
            top_omega_max = std::max(top_omega_max, std::abs(omega));
        }
    }
    const auto rhs = (tile.solver.*access(RhsTag{}))(state, RhsMode::Full);
    const auto actual_mu = rhs.slice(0, total - sm, total).view({ny, nx})
                               .slice(0, 0, m).slice(1, 0, n).to(torch::kFloat64);
    const double mass_scale = expected.abs().max().item<double>();
    const double mass_error = (actual_mu - expected).abs().max().item<double>();
    TORCH_CHECK(mass_scale > 1.0e-6 && omega_max > 1.0e-6 && top_omega_max > 1.0e-6,
                "mass/Omega controls are uninformative");
    TORCH_CHECK(mass_error < 3.0e-6 * mass_scale,
                "physical column continuity differs from face oracle: ", mass_error,
                " scale=", mass_scale);
    const double theta = rhs.slice(0, su + sv + 2 * sw, total - sm)
                             .view({ny, nz, nx}).slice(0, 0, m).slice(2, 0, n)
                             .abs().max().item<double>();
    // Roundoff relative to the separate transport/product-rule terms. The
    // minimum level mass is >40000 Pa for this fixture; no absolute error floor.
    const double tolerance = 128.0 * std::numeric_limits<float>::epsilon() *
                             (4.0 * 1.5 * mass_scale / 40000.0);
    std::cout << "PARAM_CONSTANT_THETA packed=" << packed << " hevi=" << hevi
              << " order=" << order << " mass_error=" << mass_error
              << " omega_max=" << omega_max << " top_omega=" << top_omega_max
              << " theta_max=" << theta << " tolerance=" << tolerance << std::endl;
    TORCH_CHECK(theta <= tolerance, "hybrid constant theta mismatch: ", theta,
                " tolerance=", tolerance);
}

}

int main() {
    torch::set_num_threads(1);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.split_explicit = false;
    cfg.mu_tend_fortran_parity = true;
    for (bool hevi : {false, true})
        for (int direction : {0, 1, 2})
            check_constant_theta(direction, hevi);
    for (bool packed : {false, true})
        for (bool hevi : {false, true})
            for (int order : {2, 3, 5})
                check_parameterized_constant_theta(packed, hevi, order);
}
