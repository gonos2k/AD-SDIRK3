#include "tile_test_fixture.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

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



void check_native_hard_v3_full() {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.imex_enabled = false;
    cfg.mass_coordinate_mode = 1;
    cfg.non_hydrostatic = true;
    cfg.advection_order = 5;
    cfg.hevi_split = false;
    cfg.mu_tend_fortran_parity = true;
    cfg.internal_fp64 = true;
    TORCH_CHECK(cfg.sign_smooth_delta == 1.0e-3f,
                "native hard-v3 test must preserve the global smooth-sign default");

    TileCase tile(100000.0f, 0.0f);
    tile.solver.setNonHydrostatic(true);
    tile.step(1.0e-4f);  // publish the actual Full-RHS map/metric context
    auto grid = tile.solver.getGridInfo();
    TORCH_CHECK(grid, "native THM fixture has no GridInfo");

    constexpr double pi = 3.14159265358979323846;
    const std::vector<double> dnw_mag{0.08, 0.17, 0.29, 0.46};
    std::vector<float> dnw_signed(nz), rdnw_pos(nz), rdn_pos(nz, 0.0f),
                      c1h(nz, 1.0f), c2h(nz, 0.0f);
    std::vector<float> c1f(nw, 1.0f), c2f(nw, 0.0f);
    for (int k = 0; k < nz; ++k) {
        dnw_signed[k] = static_cast<float>(-dnw_mag[k]);
        rdnw_pos[k] = static_cast<float>(1.0 / dnw_mag[k]);
    }
    for (int k = 1; k < nz; ++k) {
        const double dn = 0.5 * (dnw_mag[k - 1] + dnw_mag[k]);
        rdn_pos[k] = static_cast<float>(1.0 / dn);
    }
    grid->rdnw = torch::from_blob(rdnw_pos.data(), {nz}, torch::kFloat32).clone();
    grid->dnw = torch::from_blob(dnw_signed.data(), {nz}, torch::kFloat32).clone();
    grid->rdn = torch::from_blob(rdn_pos.data(), {nz}, torch::kFloat32).clone();
    (tile.solver.*access(CoefficientsTag{}))(
        c1f.data(), c2f.data(), c1h.data(), c2h.data());
    std::vector<float> fnm(nz, 0.0f), fnp(nz, 0.0f);
    for (int k = 1; k < nz; ++k) {
        const double dnw_lo = 1.0 / static_cast<double>(rdnw_pos[k - 1]);
        const double dnw_hi = 1.0 / static_cast<double>(rdnw_pos[k]);
        const double dnv = 1.0 / static_cast<double>(rdn_pos[k]);
        fnm[k] = static_cast<float>(0.5 * dnw_lo / dnv);
        fnp[k] = static_cast<float>(0.5 * dnw_hi / dnv);
        TORCH_CHECK(std::abs(fnm[k] + fnp[k] - 1.0f) < 2e-7f,
                    "native THM nonuniform face weights do not sum to one");
    }
    tile.solver.setVerticalInterpolationCoefficients(fnm.data(), fnp.data(),
                                                       0.5f, 0.0f, 0.0f);

    // Positive periodic U creates a well-resolved nonzero Omega; its vertical
    // profile varies with k so each interior
    // recurrence level is active. Full-RHS comparisons below use only the
    // legacy-minus-hard difference, so common horizontal/mass terms cancel.
    for (int j = 0; j < ny; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i <= nx; ++i) {
                const int ii = i % nx;
                tile.u[(j*nz + k)*nu + i] = static_cast<float>(
                    5.0 + 2.0 * std::sin(2.0*pi*(ii + 0.25)/nx) * (1.0 + 0.2*k));
            }
    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i)
                tile.theta[(j*nz + k)*nx + i] = static_cast<float>(
                    6.0 + 1.2*k + 0.35*k*k + 0.5*k*k*k);
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < nx; ++i)
                tile.ph[(j*nw + k)*nx + i] = static_cast<float>(0.2*k*k*k);
    }
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    const auto x = tile.projectControlState(tile.state().to(torch::kFloat64)).contiguous();

    const double rdx = static_cast<double>(1.0f / tile.spacing);
    const double rdy = static_cast<double>(1.0f / tile.spacing);
    const auto omega_for = [&](const torch::Tensor& input) {
        const auto q = input.contiguous();
        const auto* a = q.data_ptr<double>();
        const auto u_at = [&](int j, int k, int i) {
            return a[(j*nz + k)*nu + i];
        };
        const auto v_at = [&](int j, int k, int i) {
            return a[su + (j*nz + k)*nx + i];
        };
        const auto mu_at = [&](int j, int i) {
            return a[total - sm + j*nx + i];
        };
        const auto mass = [&](int j, int i) { return 80000.0 + mu_at(j, i); };
        std::vector<double> om(static_cast<size_t>(ny*nw*nx), 0.0);
        const auto at = [&](int j, int k, int i) -> double& {
            return om[(j*nw + k)*nx + i];
        };
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                std::vector<double> divv(nz, 0.0);
                double dmdt = 0.0;
                for (int k = 0; k < nz; ++k) {
                    const auto cu = [&](int face) {
                        const int left = (face + nx - 1) % nx;
                        const int right = face % nx;
                        const double muu = 0.5 * (mass(j, left) + mass(j, right));
                        return (c1h[k]*muu + c2h[k]) * u_at(j, k, face);
                    };
                    const auto cv = [&](int face) {
                        const int south = std::max(0, face - 1);
                        const int north = std::min(ny - 1, face);
                        const double muv = 0.5 * (mass(south, i) + mass(north, i));
                        return (c1h[k]*muv + c2h[k]) * v_at(face, k, i);
                    };
                    const double dnw_omega = -1.0 / static_cast<double>(rdnw_pos[k]);
                    divv[k] = dnw_omega *
                        (rdx*(cu(i + 1) - cu(i)) + rdy*(cv(j + 1) - cv(j)));
                    dmdt += divv[k];
                }
                at(j, 0, i) = 0.0;
                for (int k = 0; k < nz - 1; ++k)
                    at(j, k + 1, i) = at(j, k, i) -
                        (-1.0/static_cast<double>(rdnw_pos[k]))*c1h[k]*dmdt - divv[k];
                at(j, nz, i) = 0.0;
            }
        }
        return om;
    };
    const auto omega = omega_for(x);
    const auto om = [&](int j, int k, int i) {
        return omega[(j*nw + k)*nx + i];
    };
    double omega_min = std::numeric_limits<double>::infinity();
    double omega_max = 0.0;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            for (int k = 1; k < nz; ++k) {
                omega_min = std::min(omega_min, std::abs(om(j, k, i)));
                omega_max = std::max(omega_max, std::abs(om(j, k, i)));
            }
    TORCH_CHECK(omega_min > 1.0e-3 && omega_max > 1.0e-2,
                "hard-v3 omega sign margin is uninformative: min=", omega_min,
                " max=", omega_max);

    // Independent weighted-linear legacy face flux and hard order-3 WRF flux.
    const auto* xa = x.data_ptr<double>();
    const auto t_at = [&](int j, int k, int i) {
        return xa[su + sv + 2*sw + (j*nz + k)*nx + i];
    };
    std::vector<double> expected_legacy(static_cast<size_t>(ny*nz*nx), 0.0);
    std::vector<double> expected_hard3(static_cast<size_t>(ny*nz*nx), 0.0);
    const auto out_at = [&](std::vector<double>& values, int j, int k, int i) -> double& {
        return values[(j*nz + k)*nx + i];
    };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            std::vector<double> f2(nw, 0.0), f3(nw, 0.0);
            for (int face = 1; face < nz; ++face) {
                const double q0 = t_at(j, face - 1, i);
                const double q1 = t_at(j, face, i);
                const double dnw_lo = 1.0 / static_cast<double>(rdnw_pos[face - 1]);
                const double dnw_hi = 1.0 / static_cast<double>(rdnw_pos[face]);
                const double dnv = 1.0 / static_cast<double>(rdn_pos[face]);
                const double fzm = 0.5 * dnw_lo / dnv;
                const double fzp = 0.5 * dnw_hi / dnv;
                const double interp = fzm*q1 + fzp*q0;
                f2[face] = om(j, face, i) *
                    (static_cast<double>(fnm[face])*q1 + static_cast<double>(fnp[face])*q0);
                double qface = interp;
                if (face >= 2 && face <= nz - 2) {
                    const double qm2 = t_at(j, face - 2, i);
                    const double qm1 = t_at(j, face - 1, i);
                    const double qi = t_at(j, face, i);
                    const double qp1 = t_at(j, face + 1, i);
                    const double flux4 = (7.0*(qi + qm1) - (qp1 + qm2)) / 12.0;
                    const double corr = ((qp1 - qm2) - 3.0*(qi - qm1)) / 12.0;
                    const double dir = om(j, face, i) > 0.0 ? -1.0 : 1.0;
                    qface = flux4 + dir*corr;
                }
                f3[face] = om(j, face, i) * qface;
            }
            for (int k = 0; k < nz; ++k) {
                const double m = 80000.0 + xa[total - sm + j*nx + i];
                const double denom = c1h[k]*m + c2h[k];
                const double r = rdnw_pos[k];
                out_at(expected_legacy, j, k, i) = r*(f2[k + 1] - f2[k]) / denom;
                out_at(expected_hard3, j, k, i) = r*(f3[k + 1] - f3[k]) / denom;
            }
        }
    }
    const auto t_block = [](const torch::Tensor& rhs) {
        return rhs.slice(0, su + sv + 2*sw, total - sm).view({ny, nz, nx});
    };
    const auto run_full_theta = [&](const torch::Tensor& input) {
        return t_block((tile.solver.*access(RhsTag{}))(input, RhsMode::Full));
    };
    // No setter means the standalone legacy weighted avg_z_to_w branch (THM=v0).
    const auto legacy = run_full_theta(x);
    tile.solver.setNativeTransportOptions(2, 3);
    const auto hard3 = run_full_theta(x);
    TORCH_CHECK(legacy.scalar_type() == torch::kFloat64 && hard3.scalar_type() == torch::kFloat64,
                "native transport oracle requires solver float64 arithmetic");
    TORCH_CHECK(std::abs(hard3.select(1, nz - 1).norm().item<double>()) > 1e-9,
                "hard-v3 top mass-cell tendency was dropped");
    const auto weighted_minus_hard = legacy - hard3;
    const auto expected_delta = torch::from_blob(expected_legacy.data(),
        {ny,nz,nx}, torch::kFloat64).clone() -
        torch::from_blob(expected_hard3.data(), {ny,nz,nx}, torch::kFloat64).clone();
    const double delta_err = (weighted_minus_hard - expected_delta).norm().item<double>() /
        std::max(expected_delta.norm().item<double>(), 1e-30);
    TORCH_CHECK(std::isfinite(delta_err) && delta_err < 2e-9 &&
                expected_delta.norm().item<double>() > 1e-9,
                "native hard-v3/legacy Full component delta differs from independent oracle: ",
                delta_err);

    // Mixed Full-RHS tangent projected onto physical state owners: the
    // periodic U closure follows x=0, V symmetry walls vanish, and fixed W/PH
    // bottoms vanish. The top remains dynamic for top_lid=false.
    auto index = torch::arange(total, x.options());
    auto direction = (torch::sin(0.013*index + 0.17) +
                      0.23*torch::cos(0.021*index + 0.31));
    direction = tile.projectControlState(direction).clone();
    auto du = direction.slice(0, 0, su).view({ny,nz,nu});
    auto dv = direction.slice(0, su, su+sv).view({nv,nz,nx});
    auto dw = direction.slice(0, su+sv, su+sv+sw).view({ny,nw,nx});
    auto dph = direction.slice(0, su+sv+sw, su+sv+2*sw).view({ny,nw,nx});
    // This fixture stores the physical periodic U closing face at i=nx.
    du.select(2,nx).copy_(du.select(2,0));
    dw.select(1,0).zero_();
    dph.select(1,0).zero_();
    if (cfg.split_explicit_top_lid) {
        dw.select(1,nz).zero_();
        dph.select(1,nz).zero_();
    }
    direction = direction / direction.norm();
    TORCH_CHECK((du.select(2,nx)-du.select(2,0)).abs().max().item<double>() == 0.0 &&
                dv.select(0,0).abs().max().item<double>() == 0.0 &&
                dv.select(0,ny).abs().max().item<double>() == 0.0 &&
                dw.select(1,0).abs().max().item<double>() == 0.0 &&
                dph.select(1,0).abs().max().item<double>() == 0.0 &&
                (!cfg.split_explicit_top_lid ||
                 (dw.select(1,nz).abs().max().item<double>() == 0.0 &&
                  dph.select(1,nz).abs().max().item<double>() == 0.0)),
                "projected tangent violates physical staggered boundary owners");
    TORCH_CHECK(direction.slice(0, 0, su).norm().item<double>() > 1e-5 &&
                direction.slice(0, su, su + sv).norm().item<double>() > 1e-5 &&
                direction.slice(0, total - sm, total).norm().item<double>() > 1e-5,
                "mixed tangent must move both velocities and total mass");
    auto lambda_ph = torch::zeros_like(x);
    auto lambda_thm = torch::zeros_like(x);
    const auto Ffull = [&](const torch::Tensor& input) {
        return (tile.solver.*access(RhsTag{}))(input, RhsMode::Full);
    };
    bool used_fd = true;
    const auto jv = wrf::sdirk3::compute_jvp_fwad_or_fd(
        Ffull, x, direction, 0, 0.0f, &used_fd);
    TORCH_CHECK(!used_fd, "Full-RHS JVP used finite-difference fallback");
    const double jv_ph = jv.slice(0, su + sv + sw, su + sv + 2*sw).norm().item<double>();
    const double jv_t = jv.slice(0, su + sv + 2*sw, total - sm).norm().item<double>();
    TORCH_CHECK(jv_ph > 1e-12 && jv_t > 1e-12,
                "mixed Full-RHS tangent did not excite both PH and THM: PH=", jv_ph,
                " THM=", jv_t);
    // Separate owner-masked cotangents align with each complete field JVP.
    // This gives a strong VJP pairing while the FD checks below compare the
    // entire PH and THM vectors independently, not only their scalar sums.
    auto ph_owner = torch::ones({ny,nw,nx}, x.options());
    ph_owner.select(1,0).zero_();
    if (cfg.split_explicit_top_lid) ph_owner.select(1,nz).zero_();
    const auto ph_jv = jv.slice(0, su+sv+sw, su+sv+2*sw).view({ny,nw,nx});
    const auto thm_jv = jv.slice(0, su+sv+2*sw, total-sm).view({ny,nz,nx});
    const auto ph_jv_owned = ph_jv * ph_owner;
    const auto thm_jv_owned = thm_jv;  // All physical mass cells are owners here.
    lambda_ph.slice(0, su+sv+sw, su+sv+2*sw).copy_(ph_jv_owned.detach().flatten());
    lambda_thm.slice(0, su+sv+2*sw, total-sm).copy_(thm_jv_owned.detach().flatten());
    lambda_ph = lambda_ph/lambda_ph.norm();
    lambda_thm = lambda_thm/lambda_thm.norm();
    const auto lambda_ph_block = lambda_ph.slice(0, su+sv+sw, su+sv+2*sw)
                                     .view({ny,nw,nx});
    TORCH_CHECK(lambda_ph.slice(0, su+sv, su+sv+sw).norm().item<double>() == 0.0 &&
                lambda_ph_block.select(1,0).norm().item<double>() == 0.0 &&
                lambda_thm.slice(0, su+sv, su+sv+sw).norm().item<double>() == 0.0,
                "PH/THM cotangents must mask W and fixed PH bottom output cells");
    const auto check_field_derivative = [&](const torch::Tensor& lambda,
                                            int begin, int end, const char* name) {
        const auto jv_field = (jv.slice(0, begin, end) *
            (end-begin == sw ? ph_owner.flatten() : torch::ones({end-begin}, x.options())));
        const auto lambda_field = lambda.slice(0, begin, end);
        auto x_rev = x.clone().set_requires_grad(true);
        const auto loss = (Ffull(x_rev)*lambda).sum();
        const auto vjp = torch::autograd::grad({loss}, {x_rev})[0];
        const double lhs = (jv*lambda).sum().item<double>();
        const double rhs = (direction*vjp).sum().item<double>();
        const double scale = jv_field.norm().item<double>() * lambda_field.norm().item<double>();
        const double dual_rel = std::abs(lhs-rhs)/std::max(scale,1e-30);
        TORCH_CHECK(std::isfinite(dual_rel) && std::abs(lhs)/std::max(scale,1e-30) > 1e-5 &&
                    dual_rel < 2e-8,
                    name, " Full-RHS JVP/VJP duality failed: lhs=", lhs,
                    " rhs=", rhs, " normalized_error=", dual_rel);

        double safe_epsilon = 1e-2;
        const double probe = 1e-4;
        const auto omega_probe_p = omega_for((x+probe*direction).contiguous());
        const auto omega_probe_m = omega_for((x-probe*direction).contiguous());
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                for (int k = 2; k <= nz-2; ++k) {
                    const size_t slot = static_cast<size_t>((j*nw+k)*nx+i);
                    const double slope = std::max(std::abs(omega_probe_p[slot]-omega[slot]),
                                                  std::abs(omega_probe_m[slot]-omega[slot]))/probe;
                    if (slope > 0.0)
                        safe_epsilon = std::min(safe_epsilon,
                            0.25*std::abs(omega[slot])/slope);
                }
        TORCH_CHECK(std::isfinite(safe_epsilon) && safe_epsilon >= 1e-8,
                    name, " no useful sign-preserving FD step from Omega margin");
        const std::array<double,9> epsilons{safe_epsilon, safe_epsilon/2,
            safe_epsilon/4, safe_epsilon/8, safe_epsilon/16,
            safe_epsilon/32, safe_epsilon/64, safe_epsilon/128,
            safe_epsilon/256};
        bool plateau = false, previous_valid = false;
        torch::Tensor previous_fd;
        for (const double eps : epsilons) {
            const auto xp = (x+eps*direction).contiguous();
            const auto xm = (x-eps*direction).contiguous();
            const auto op = omega_for(xp);
            const auto omm = omega_for(xm);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    for (int k = 2; k <= nz-2; ++k) {
                        const size_t slot = static_cast<size_t>((j*nw+k)*nx+i);
                        TORCH_CHECK(op[slot]*omega[slot] > 0.0 &&
                                    omm[slot]*omega[slot] > 0.0 &&
                                    std::abs(op[slot]) >= 0.5*std::abs(omega[slot]) &&
                                    std::abs(omm[slot]) >= 0.5*std::abs(omega[slot]),
                                    name, " FD perturbation crossed/approached hard-v3 Omega branch");
                    }
            const auto fp = Ffull(xp).slice(0, begin, end);
            const auto fm = Ffull(xm).slice(0, begin, end);
            const auto fd = ((fp-fm)/(2.0*eps)) *
                (end-begin == sw ? ph_owner.flatten() : torch::ones({end-begin}, x.options()));
            const double fd_err = (fd-jv_field).norm().item<double>() /
                std::max(jv_field.norm().item<double>(),1e-30);
            const bool valid = std::isfinite(fd_err) && fd_err < 2e-7;
            std::cout << "NATIVE_HARD_V3_FD field=" << name << " eps=" << eps
                      << " jvp_norm=" << jv_field.norm().item<double>()
                      << " fd_norm=" << fd.norm().item<double>()
                      << " rel=" << fd_err << " valid=" << valid << '\n';
            if (valid && previous_valid &&
                (fd-previous_fd).norm().item<double>() /
                    std::max(jv_field.norm().item<double>(),1e-30) < 2e-7)
                plateau = true;
            previous_fd = fd;
            previous_valid = valid;
        }
        TORCH_CHECK(plateau, name,
                    " central Full-RHS FD lacks adjacent individually valid levels");
        std::cout << "NATIVE_HARD_V3_FULL field=" << name
                  << " normalized_dual_error=" << dual_rel
                  << " fd_plateau=" << plateau << '\n';
    };
    check_field_derivative(lambda_ph, su+sv+sw, su+sv+2*sw, "PH");
    check_field_derivative(lambda_thm, su+sv+2*sw, total-sm, "THM");
    std::cout << "NATIVE_HARD_V3_FULL delta_rel=" << delta_err
              << " omega_min=" << omega_min << " jvp_fallback=" << used_fd << '\n';
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
    check_native_hard_v3_full();
}
