#include "tile_test_fixture.h"
#include "../wrf_sdirk3_coriolis.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void check_coriolis_coefficients();

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct CoefficientsTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*, const float*, const float*);
    friend type access(CoefficientsTag);
};
struct CoriolisTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*, const float*, const float*);
    friend type access(CoriolisTag);
};
template <typename Tag, typename Tag::type Member>
struct Accessor { friend typename Tag::type access(Tag) { return Member; } };
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoefficientsTag, &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;
template struct Accessor<CoriolisTag, &TileSDIRK3UnifiedSolver::setCoriolisParameters>;

constexpr double kMu = 80000.0;
constexpr double kC1H = 0.65;
constexpr double kC2H = 1200.0;
constexpr double kC1F = 0.55;
constexpr double kC2F = 700.0;

int wrap(int i, int n) {
    const int r = i % n;
    return r < 0 ? r + n : r;
}
size_t ui(int j, int k, int i) { return (static_cast<size_t>(j) * nz + k) * nu + i; }
size_t vi(int j, int k, int i) { return (static_cast<size_t>(j) * nz + k) * nx + i; }
size_t wi(int j, int k, int i) { return (static_cast<size_t>(j) * nw + k) * nx + i; }
size_t mi(int j, int i) { return static_cast<size_t>(j) * nx + i; }

struct Fields {
    bool packed;
    int m;
    int n;
    std::vector<float> f, e, sina, cosa;
};

double umap(int j, int i, bool varying) {
    if (!varying) return 1.0;
    return 1.10 + 0.023 * i + 0.011 * j;
}
double vmap(int j, int i, bool varying) {
    if (!varying) return 1.0;
    return 0.83 + 0.037 * i + 0.017 * j;
}

Fields configure(TileCase& tile, bool packed, bool varying_maps) {
    tile.solver.setWRFIndices(
        1, packed ? nx : nx + 1, 1, packed ? ny : ny + 1, 1, nz,
        1, packed ? nx : nx + 1, 1, packed ? ny : ny + 1, 1, nz,
        1, nu, 1, nv, 1, nw);
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    std::fill(tile.mass_map.begin(), tile.mass_map.end(), 1.0f);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nu; ++i) {
            const int ip = i == n ? 0 : (i == n + 1 ? 1 : i);
            tile.u_map[static_cast<size_t>(j) * nu + i] =
                static_cast<float>(umap(std::min(j, m - 1), ip, varying_maps));
        }
    }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nx; ++i)
            tile.v_map[static_cast<size_t>(j) * nx + i] =
                static_cast<float>(vmap(std::min(j, m), i < n ? i : 0, varying_maps));

    tile.step(1.0e-4f);
    std::vector<float> c1f(nw, static_cast<float>(kC1F));
    std::vector<float> c2f(nw, static_cast<float>(kC2F));
    std::vector<float> c1h(nw, static_cast<float>(kC1H));
    std::vector<float> c2h(nw, static_cast<float>(kC2H));
    (tile.solver.*access(CoefficientsTag{}))(
        c1f.data(), c2f.data(), c1h.data(), c2h.data());

    Fields fields{packed, m, n,
                  std::vector<float>(sm), std::vector<float>(sm),
                  std::vector<float>(sm), std::vector<float>(sm)};
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int ip = i < n ? i : 0;
            fields.f[mi(j, i)] = static_cast<float>(0.71 + 0.013 * ip + 0.021 * j);
            fields.e[mi(j, i)] = static_cast<float>(0.29 + 0.009 * ip + 0.017 * j);
            fields.sina[mi(j, i)] = static_cast<float>(0.14 + 0.005 * ip + 0.004 * j);
            fields.cosa[mi(j, i)] = static_cast<float>(0.93 - 0.003 * ip + 0.002 * j);
        }
    }
    (tile.solver.*access(CoriolisTag{}))(
        fields.f.data(), fields.e.data(), fields.sina.data(), fields.cosa.data());
    return fields;
}

void fill_state(TileCase& tile, bool packed, char component, bool varying_mass) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    for (int j = 0; j < m; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < n; ++i) {
                if (varying_mass)
                    tile.mu[mi(j, i)] = static_cast<float>(600.0 + 17.0 * j + 9.0 * i);
                if (component == 'u' && j > 0 && j < m)
                    tile.v[vi(j, k, i)] = static_cast<float>(0.37 + 0.07 * j + 0.019 * k + 0.033 * i);
                if (component == 'v')
                    tile.u[ui(j, k, i)] = static_cast<float>(0.63 + 0.05 * j + 0.023 * k + 0.027 * i);
                if (component == 'w') {
                    tile.u[ui(j, k, i)] = static_cast<float>(0.41 + 0.03 * j + 0.021 * k + 0.017 * i);
                    tile.v[vi(j, k, i)] = static_cast<float>(-0.28 + 0.04 * j + 0.018 * k + 0.031 * i);
                }
                if (component == 'z')
                    tile.w[wi(j, k, i)] = static_cast<float>(0.41 + 0.03 * j + 0.021 * k + 0.017 * i);
            }
        }
    }
    // V is a normal velocity: both physical symmetric wall rows are zero.
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            tile.v[vi(0, k, i)] = 0.0f;
            tile.v[vi(m, k, i)] = 0.0f;
        }
    if (packed) {
        if (varying_mass) {
            for (int j = 0; j < m; ++j)
                tile.mu[mi(j, n)] = tile.mu[mi(j, 0)];
            for (int i = 0; i < nx; ++i)
                tile.mu[mi(m, i)] = tile.mu[mi(m - 1, i < n ? i : 0)];
        }
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < nz; ++k) {
                tile.u[ui(j, k, n)] = tile.u[ui(j, k, 0)];
                tile.u[ui(j, k, n + 1)] = tile.u[ui(j, k, 1)];
                tile.v[vi(j, k, n)] = tile.v[vi(j, k, 0)];
            }
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i)
                tile.v[vi(m + 1, k, i)] = -tile.v[vi(m - 1, k, i)];
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < nx; ++i)
                tile.w[wi(m, k, i)] = tile.w[wi(m - 1, k, i)];
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < nw; ++k)
                tile.w[wi(j, k, n)] = tile.w[wi(j, k, 0)];
    } else {
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                tile.u[ui(j, k, n)] = tile.u[ui(j, k, 0)];
    }
}

std::vector<double> rhs_case(bool packed, bool varying_maps, bool varying_mass,
                             char component, bool enabled, RhsMode mode) {
    TileCase tile;
    const Fields fields = configure(tile, packed, varying_maps);
    fill_state(tile, packed, component, varying_mass);
    if (!enabled) {
        const std::vector<float> zero(sm, 0.0f), one(sm, 1.0f);
        (tile.solver.*access(CoriolisTag{}))(
            zero.data(), zero.data(), zero.data(), one.data());
    }
    const auto out = (tile.solver.*access(RhsTag{}))(tile.state(), mode)
        .detach().to(torch::kCPU, torch::kFloat64).contiguous();
    TORCH_CHECK(torch::isfinite(out).all().item<bool>(), "nonfinite actual RHS");
    const auto* p = out.data_ptr<double>();
    return std::vector<double>(p, p + total);
}

double velocity_u(int j, int k, int i, int n, char component) {
    i = i < n ? i : 0;
    if (component != 'v' && component != 'w') return 0.0;
    return component == 'v' ? 0.63 + 0.05 * j + 0.023 * k + 0.027 * i
                            : 0.41 + 0.03 * j + 0.021 * k + 0.017 * i;
}
double velocity_v(int j, int k, int i, int n, char component) {
    i = wrap(i, n);
    if (component != 'u' && component != 'w') return 0.0;
    return component == 'u' ? 0.37 + 0.07 * j + 0.019 * k + 0.033 * i
                            : -0.28 + 0.04 * j + 0.018 * k + 0.031 * i;
}
double mass_value(int j, int i, int m, int n, bool varying_mass) {
    const int jj = std::min(std::max(j, 0), m - 1);
    const int ii = i < n ? i : 0;
    return kMu + (varying_mass ? 600.0 + 17.0 * jj + 9.0 * ii : 0.0);
}
double mass_u(int j, int i, int m, int n, bool varying_mass) {
    const int ii = i < n ? i : 0;
    const int west = ii == 0 ? n - 1 : ii - 1;
    return 0.5 * (mass_value(j, west, m, n, varying_mass) +
                  mass_value(j, ii, m, n, varying_mass));
}
double mass_v(int j, int i, int m, int n, bool varying_mass) {
    const int ii = i < n ? i : 0;
    if (j <= 0) return mass_value(0, ii, m, n, varying_mass);
    if (j >= m) return mass_value(m - 1, ii, m, n, varying_mass);
    return 0.5 * (mass_value(j - 1, ii, m, n, varying_mass) +
                  mass_value(j, ii, m, n, varying_mass));
}
double alpha_u(int j, int i, int m, int n, bool varying_maps, bool varying_mass) {
    return (kC1H * mass_u(j, i, m, n, varying_mass) + kC2H) /
           umap(j, i < n ? i : 0, varying_maps);
}
double alpha_v(int j, int i, int m, int n, bool varying_maps, bool varying_mass) {
    return (kC1H * mass_v(j, i, m, n, varying_mass) + kC2H) /
           vmap(j, i < n ? i : 0, varying_maps);
}
double alpha_w(int j, int i, int m, int n, bool varying_maps, bool varying_mass) {
    return (kC1F * mass_value(j, i, m, n, varying_mass) + kC2F);
}
double ff(int j, int i) { return 0.71 + 0.013 * i + 0.021 * j; }
double ee(int j, int i) { return 0.29 + 0.009 * i + 0.017 * j; }
double ss(int j, int i) { return 0.14 + 0.005 * i + 0.004 * j; }
double cc(int j, int i) { return 0.93 - 0.003 * i + 0.002 * j; }
double f_at(int j, int i, int n) { return ff(j, i < n ? i : 0); }
double e_at(int j, int i, int n) { return ee(j, i < n ? i : 0); }
double s_at(int j, int i, int n) { return ss(j, i < n ? i : 0); }
double c_at(int j, int i, int n) { return cc(j, i < n ? i : 0); }

double expected_u(bool packed, bool varying_maps, bool varying_mass,
                   char component, int j, int k, int i) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    const int sj = std::min(j, m - 1);
    const int si = i < n ? i : (packed ? (i == n ? 0 : 1) : 0);
    const int west = wrap(si - 1, n), east = wrap(si, n);
    auto rv = [&](int jj, int ii) {
        if (jj <= 0 || jj >= m) return 0.0;
        const int sjj = std::min(std::max(jj, 0), m);
        return alpha_v(sjj, ii, m, n, varying_maps, varying_mass) *
               velocity_v(sjj, k, ii, n, component);
    };
    const double rv_u = 0.25 *
        (rv(sj, west) + rv(sj, east) + rv(sj + 1, west) + rv(sj + 1, east));
    const double c = f_at(sj, west, n) * 0.5 * rv_u +
                     f_at(sj, east, n) * 0.5 * rv_u;
    return c / alpha_u(sj, si, m, n, varying_maps, varying_mass);
}

double w_only_value(int j, int k, int i, int n) {
    if (k >= nz) return 0.0;
    i = i < n ? i : 0;
    return 0.41 + 0.03 * j + 0.021 * k + 0.017 * i;
}

double expected_u_w_only(bool packed, bool varying_maps, bool varying_mass,
                         int j, int k, int i) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    const int sj = std::min(j, m - 1);
    const int si = i < n ? i : (packed ? (i == n ? 0 : 1) : 0);
    const int west = wrap(si - 1, n), east = si;
    auto rw = [&](int kk, int ii) {
        return alpha_w(sj, ii, m, n, varying_maps, varying_mass) *
               w_only_value(sj, kk, ii, n);
    };
    const double rw_at_u = 0.25 *
        (rw(k, west) + rw(k, east) + rw(k + 1, west) + rw(k + 1, east));
    const double eu = 0.5 * (e_at(sj, west, n) + e_at(sj, east, n));
    const double cosa_u = 0.5 * (c_at(sj, west, n) + c_at(sj, east, n));
    return -eu * cosa_u * rw_at_u /
           alpha_u(sj, si, m, n, varying_maps, varying_mass);
}

double expected_v_w_only(bool packed, bool varying_maps, bool varying_mass,
                         int j, int k, int i) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    if (j == 0 || j == m) return 0.0;
    if (packed && j == m + 1)
        return -expected_v_w_only(packed, varying_maps, varying_mass, m - 1, k, i);
    i = i < n ? i : 0;
    auto rw = [&](int jj, int kk) {
        return alpha_w(jj, i, m, n, varying_maps, varying_mass) *
               w_only_value(jj, kk, i, n);
    };
    const double rw_at_v = 0.25 *
        (rw(j - 1, k) + rw(j, k) + rw(j - 1, k + 1) + rw(j, k + 1));
    const double ev = 0.5 * (e_at(j - 1, i, n) + e_at(j, i, n));
    const double sina_v = 0.5 * (s_at(j - 1, i, n) + s_at(j, i, n));
    return ev * sina_v * rw_at_v /
           alpha_v(j, i, m, n, varying_maps, varying_mass);
}

double expected_v(bool packed, bool varying_maps, bool varying_mass,
                   char component, int j, int k, int i) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    if (j == 0 || j == m) return 0.0;
    if (packed && j == m + 1)
        return -expected_v(packed, varying_maps, varying_mass, component, m - 1, k, i);
    i = i < n ? i : 0;
    const double ru = 0.25 *
        (alpha_u(j, i, m, n, varying_maps, varying_mass) * velocity_u(j, k, i, n, component) +
         alpha_u(j, i + 1, m, n, varying_maps, varying_mass) * velocity_u(j, k, i + 1, n, component) +
         alpha_u(j - 1, i, m, n, varying_maps, varying_mass) * velocity_u(j - 1, k, i, n, component) +
         alpha_u(j - 1, i + 1, m, n, varying_maps, varying_mass) * velocity_u(j - 1, k, i + 1, n, component));
    const double fv = 0.5 * (f_at(j - 1, i, n) + f_at(j, i, n));
    return -fv * ru / alpha_v(j, i, m, n, varying_maps, varying_mass);
}

double expected_w(bool packed, bool varying_maps, bool varying_mass,
                   char component, int j, int k, int i) {
    const int n = packed ? nx - 1 : nx;
    const int m = packed ? ny - 1 : ny;
    if (k == 0 || k == nz) return 0.0;
    const int sj = std::min(j, m - 1), si = i < n ? i : 0;
    const double ru = 0.5 *
        (alpha_u(sj, si, m, n, varying_maps, varying_mass) * velocity_u(sj, k, si, n, component) +
         alpha_u(sj, si + 1, m, n, varying_maps, varying_mass) * velocity_u(sj, k, si + 1, n, component));
    const auto v_mass = [&](int jj, int kk) {
        if (jj <= 0 || jj >= m) return 0.0;
        return alpha_v(jj, si, m, n, varying_maps, varying_mass) *
               velocity_v(jj, kk, si, n, component);
    };
    const double rv = 0.5 * (v_mass(sj, k) + v_mass(sj + 1, k));
    const double lower_u = 0.5 *
        (alpha_u(sj, si, m, n, varying_maps, varying_mass) * velocity_u(sj, k - 1, si, n, component) +
         alpha_u(sj, si + 1, m, n, varying_maps, varying_mass) * velocity_u(sj, k - 1, si + 1, n, component));
    const double lower_v = 0.5 * (v_mass(sj, k - 1) + v_mass(sj + 1, k - 1));
    const double ru_w = 0.5 * ru + 0.5 * lower_u;
    const double rv_w = 0.5 * rv + 0.5 * lower_v;
    const double c = e_at(sj, si, n) *
        (c_at(sj, si, n) * ru_w - s_at(sj, si, n) * rv_w);
    return c / alpha_w(sj, si, m, n, varying_maps, varying_mass);
}

double max_error(const std::vector<double>& got, const std::vector<double>& off,
                 bool packed, bool varying, bool varying_mass, char component, int mode) {
    double err = 0.0;
    int best_j = -1, best_k = -1, best_i = -1;
    double best_obs = 0.0, best_exp = 0.0;
    auto track = [&](double obs, double exp, int j, int k, int i) {
        const double e = std::abs(obs - exp);
        if (e > err) { err = e; best_j = j; best_k = k; best_i = i; best_obs = obs; best_exp = exp; }
    };
    if (component == 'u') {
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nz; ++k) for (int i = 0; i < nu; ++i) {
            const double expected = expected_u(packed, varying, varying_mass, component, j, k, i);
            track(got[ui(j,k,i)] - off[ui(j,k,i)], expected, j, k, i);
        }
    } else if (component == 'v') {
        for (int j = 0; j < nv; ++j) for (int k = 0; k < nz; ++k) for (int i = 0; i < nx; ++i) {
            const double expected = expected_v(packed, varying, varying_mass, component, j, k, i);
            track(got[su + vi(j,k,i)] - off[su + vi(j,k,i)], expected, j, k, i);
        }
    } else if (component == 'z') {
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nz; ++k) for (int i = 0; i < nu; ++i)
            track(got[ui(j,k,i)] - off[ui(j,k,i)],
                  expected_u_w_only(packed, varying, varying_mass, j, k, i), j, k, i);
        for (int j = 0; j < nv; ++j) for (int k = 0; k < nz; ++k) for (int i = 0; i < nx; ++i)
            track(got[su + vi(j,k,i)] - off[su + vi(j,k,i)],
                  expected_v_w_only(packed, varying, varying_mass, j, k, i), j, k, i);
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nw; ++k) for (int i = 0; i < nx; ++i)
            track(got[su + sv + wi(j,k,i)] - off[su + sv + wi(j,k,i)], 0.0, j, k, i);
    } else {
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nw; ++k) for (int i = 0; i < nx; ++i) {
            const double expected = expected_w(packed, varying, varying_mass, component, j, k, i);
            track(got[su + sv + wi(j,k,i)] - off[su + sv + wi(j,k,i)], expected, j, k, i);
        }
    }
    std::cout << std::setprecision(12) << "ACTUAL component=" << component
              << " packed=" << packed << " varying_maps=" << varying
              << " mode=" << mode << " max_error=" << err
              << " at=" << best_j << "," << best_k << "," << best_i
              << " observed=" << best_obs << " expected=" << best_exp << '\n';
    return err;
}

}  // namespace

int main() {
    torch::NoGradGuard no_grad;
    check_coriolis_coefficients();
    // Exercise the header-only core at the smallest supported native shape.
    // This covers the m=1 empty-V-interior and z=1 no-interior-W branches.
    {
        const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
        const auto tiny_u = torch::ones({1, 1, 3}, opts);
        const auto tiny_v = torch::ones({2, 1, 2}, opts);
        const auto tiny_w = torch::ones({1, 2, 2}, opts);
        const auto tiny_alpha_u = torch::ones_like(tiny_u);
        const auto tiny_alpha_v = torch::ones_like(tiny_v);
        const auto tiny_alpha_w = torch::ones_like(tiny_w);
        const auto tiny_u_map = torch::ones({1, 3}, opts);
        const auto tiny_v_map = torch::ones({2, 2}, opts);
        const auto tiny_w_map = torch::ones({1, 2}, opts);
        const auto tiny_coriolis = torch::ones({1, 2}, opts);
        const auto tiny_vertical = torch::ones({1}, opts);
        const auto tiny = wrf::sdirk3::wrf_coriolis_tendencies(
            tiny_u, tiny_v, tiny_w, tiny_alpha_u, tiny_alpha_v, tiny_alpha_w,
            tiny_u_map, tiny_u_map, tiny_v_map, tiny_v_map, tiny_w_map, tiny_w_map,
            tiny_coriolis, tiny_coriolis, tiny_coriolis, tiny_coriolis,
            tiny_vertical, tiny_vertical);
        TORCH_CHECK(tiny.u.sizes() == torch::IntArrayRef({1, 1, 3}) &&
                    tiny.v.sizes() == torch::IntArrayRef({2, 1, 2}) &&
                    tiny.w.sizes() == torch::IntArrayRef({1, 2, 2}) &&
                    tiny.w.abs().max().item<double>() == 0.0,
                    "smallest native Coriolis shape contract failed");
    }
    torch::set_num_threads(1);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1;
    cfg.mu_horizontal_div_only = false;
    cfg.hevi_split = false;
    cfg.wrf_omega_ww_cp = false;
    cfg.omega_w_blend = 1.0f;
    cfg.sign_smooth_delta = 0.0f;
    cfg.coriolis_f = 0.0f;
    int case_count = 0;
    for (const auto mode : {RhsMode::Full, RhsMode::ExplicitOnly}) {
        for (const bool packed : {true, false}) {
            for (const bool varying : {false, true}) {
                for (const bool varying_mass : {false, true}) {
                    for (const char component : {'u', 'v', 'w'}) {
                        ++case_count;
                        const auto on = rhs_case(packed, varying, varying_mass, component, true, mode);
                        const auto off = rhs_case(packed, varying, varying_mass, component, false, mode);
                        const double err = max_error(on, off, packed, varying, varying_mass, component, static_cast<int>(mode));
                        TORCH_CHECK(err < 1.0e-6, "actual canonical Coriolis contract mismatch");
                    }
                }
            }
        }
    }
    TORCH_CHECK(case_count == 48, "unexpected Coriolis contract case count");
    // Isolate the vertical W coupling into both horizontal equations.  The
    // 48 matrix above exercises mixed states; this witness keeps U and V zero
    // so a missing -e*cosa*Rw or +e*sina*Rw term cannot hide by cancellation.
    int w_coupling_cases = 0;
    for (const auto mode : {RhsMode::Full, RhsMode::ExplicitOnly}) {
        for (const bool packed : {true, false}) {
            ++w_coupling_cases;
            const auto on = rhs_case(packed, true, true, 'z', true, mode);
            const auto off = rhs_case(packed, true, true, 'z', false, mode);
            const double err = max_error(on, off, packed, true, true, 'z', static_cast<int>(mode));
            TORCH_CHECK(err < 1.0e-6, "vertical Coriolis coupling contract mismatch");
        }
    }
    TORCH_CHECK(w_coupling_cases == 4, "unexpected W coupling witness count");
    std::cout << "CORIOLIS_CONTRACT_CASES=" << case_count
              << " W_COUPLING_WITNESSES=" << w_coupling_cases << "\n";
    std::cout << "CORIOLIS_CONTRACT_EXIT=0\n";
    return 0;
}
