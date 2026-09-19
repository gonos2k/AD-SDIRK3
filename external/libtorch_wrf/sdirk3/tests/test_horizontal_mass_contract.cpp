// Actual-RHS regression for horizontal momentum/mass coupling.
//
// This deliberately uses the shared TileCase runtime setup.  The c2=10000 case is
// an algebraic fixture, not a normalized hybrid forecast: c1=1 and c2=0 is the
// ordinary dry-mass case, while the large c2 case tests cancellation of the
// product-rule mass correction in the actual Full RHS.
#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor &, RhsMode);
    friend type access(RhsTag);
};
struct CoefficientsTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float *, const float *, const float *,
                                                   const float *);
    friend type access(CoefficientsTag);
};
struct RuTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(RuTag);
};
struct BoundaryTag {
    using type = wrf::sdirk3::WdampRuntimeContract TileSDIRK3UnifiedSolver::*;
    friend type access(BoundaryTag);
};
template <typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoefficientsTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;
template struct Accessor<RuTag, &TileSDIRK3UnifiedSolver::ru_adv_z_work_>;
template struct Accessor<BoundaryTag, &TileSDIRK3UnifiedSolver::wdamp_contract_>;

constexpr double pi = 3.1415926535897932384626433832795;
constexpr double mu0 = 80000.0;

size_t u_index(int j, int k, int i) { return (j * nz + k) * nu + i; }

std::vector<double> values(const torch::Tensor &x) {
    const auto y = x.detach().to(torch::kCPU, torch::kFloat64).contiguous();
    const auto *p = y.data_ptr<double>();
    std::vector<double> result(p, p + y.numel());
    for (double value : result)
        TORCH_CHECK(std::isfinite(value), "nonfinite RHS/work value");
    return result;
}

struct EvenRhs {
    std::vector<double> u;
    double mass_dot_l2 = 0.0;
    double mass_oracle_error = 0.0;
    double vertical_work_max = 0.0;
};

void configure_indices(TileCase &tile, bool packed) {
    // TileCase's constructor has already run the shared zero-copy setup.  The
    // packed path leaves one x seam and one symmetric-y row as adapter storage;
    // the unpacked path uses all physical x/y rows.
    tile.solver.setWRFIndices(1, packed ? nx : nx + 1, 1, packed ? ny : ny + 1, 1, nz, 1,
                              packed ? nx : nx + 1, 1, packed ? ny : ny + 1, 1, nz, 1, nu, 1, nv, 1,
                              nw);
    std::fill(tile.mass_map.begin(), tile.mass_map.end(), 1.0f);
    std::fill(tile.u_map.begin(), tile.u_map.end(), 1.0f);
    std::fill(tile.v_map.begin(), tile.v_map.end(), 1.0f);
    tile.solver.getGridInfo()->rdnw = torch::full({nz}, 4.0f);
    tile.solver.getGridInfo()->dnw = torch::full({nz}, -0.25f);
    tile.solver.getGridInfo()->rdn = torch::full({nz}, 4.0f);
    tile.solver.*access(BoundaryTag{}) = wrf::sdirk3::resolve_wdamp_runtime_contract(
        true, 1, 1, true, true, false, false, false, false, false, true, true, false, false, false,
        false, false);
    std::vector<float> c1(nw, 1.0f), c2(nw, 0.0f);
    (tile.solver.*access(CoefficientsTag{}))(c1.data(), c2.data(), c1.data(), c2.data());
}

void fill_state(TileCase &tile, bool packed, double sign) {
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);

    const int ncell = packed ? nx - 1 : nx;
    const int mcell = packed ? ny - 1 : ny;
    for (int j = 0; j < mcell; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i <= ncell; ++i) {
                const int ip = i % ncell;
                tile.u[u_index(j, k, i)] = static_cast<float>(
                    sign * 10.0 * std::sin(2.0 * pi * static_cast<double>(ip) / ncell));
            }
        }
    }
    if (packed) {
        for (int j = 0; j < mcell; ++j)
            for (int k = 0; k < nz; ++k)
                tile.u[u_index(j, k, ncell + 1)] = tile.u[u_index(j, k, 1)];
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nu; ++i)
                tile.u[u_index(mcell, k, i)] = tile.u[u_index(mcell - 1, k, i)];
    }
}

EvenRhs run_even(TileCase &tile, bool packed, double c2_value) {
    std::vector<float> c1(nw, 1.0f), c2(nw, static_cast<float>(c2_value));
    (tile.solver.*access(CoefficientsTag{}))(c1.data(), c2.data(), c1.data(), c2.data());

    std::vector<std::vector<double>> full;
    for (double sign : {0.0, 1.0, -1.0}) {
        fill_state(tile, packed, sign);
        full.push_back(values((tile.solver.*access(RhsTag{}))(tile.state(), RhsMode::Full)));
    }
    TORCH_CHECK(full[0].size() >= static_cast<size_t>(su), "short Full RHS");
    EvenRhs out;
    out.u.resize(su);
    for (int q = 0; q < su; ++q)
        out.u[q] = 0.5 * (full[1][q] + full[2][q] - 2.0 * full[0][q]);

    // The last odd probe leaves the backing arrays at sign=-1; restore the
    // positive physical state before evaluating the independent mass oracle.
    fill_state(tile, packed, 1.0);
    const int ncell = packed ? nx - 1 : nx;
    const int mcell = packed ? ny - 1 : ny;
    const size_t mass_offset = su + sv + 2 * sw + st;
    for (int j = 0; j < mcell; ++j) {
        for (int i = 0; i < ncell; ++i) {
            const double div =
                (tile.u[u_index(j, 0, i + 1)] - tile.u[u_index(j, 0, i)]) / tile.spacing;
            const double expected = -(mu0 + c2_value) * div;
            const double actual = full[1][mass_offset + static_cast<size_t>(j * nx + i)];
            TORCH_CHECK(std::isfinite(actual), "nonfinite mass RHS");
            out.mass_oracle_error = std::max(out.mass_oracle_error, std::abs(actual - expected));
        }
    }
    out.vertical_work_max = 0.0;
    for (const auto &value : values((tile.solver.*access(RuTag{}))))
        out.vertical_work_max = std::max(out.vertical_work_max, std::abs(value));

    double mdot_sq = 0.0;
    for (int j = 0; j < mcell; ++j)
        for (int i = 0; i < ncell; ++i) {
            const double actual = full[1][mass_offset + static_cast<size_t>(j * nx + i)];
            mdot_sq += actual * actual;
        }
    out.mass_dot_l2 = std::sqrt(mdot_sq);
    return out;
}

void check_case(bool packed) {
    TileCase tile;
    configure_indices(tile, packed);
    // Publish the complete step metadata and persistent interpolation views via
    // the public production entry point before this direct-RHS witness.
    tile.step(1.0e-4f);
    const auto ordinary = run_even(tile, packed, 0.0);
    const auto hybrid = run_even(tile, packed, 10000.0);

    double delta_sq = 0.0;
    double ordinary_sq = 0.0;
    double delta_max = 0.0;
    const int ncell = packed ? nx - 1 : nx;
    const int mcell = packed ? ny - 1 : ny;
    for (int j = 1; j < mcell - 1; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 1; i < ncell; ++i) {
                const size_t q = u_index(j, k, i);
                const double d = hybrid.u[q] - ordinary.u[q];
                delta_sq += d * d;
                ordinary_sq += ordinary.u[q] * ordinary.u[q];
                delta_max = std::max(delta_max, std::abs(d));
            }
    const double delta_l2 = std::sqrt(delta_sq);
    const double ordinary_l2 = std::sqrt(ordinary_sq);
    TORCH_CHECK(ordinary.mass_dot_l2 > 1.0e-4, "fixture must have nontrivial horizontal Mdot");
    TORCH_CHECK(ordinary_l2 > 1.0e-6,
                "ordinary c2=0 even RHS must contain physical horizontal advection");
    TORCH_CHECK(delta_l2 <= 2.0e-9 * std::max(1.0, ordinary_l2),
                "c2 hybrid product-rule delta exceeds tolerance: delta_l2=", delta_l2,
                " ordinary_l2=", ordinary_l2);
    TORCH_CHECK(ordinary.mass_oracle_error < 2.0e-6 && hybrid.mass_oracle_error < 2.0e-6,
                "mass RHS disagrees with scalar oracle: c2=0 error=", ordinary.mass_oracle_error,
                " c2=10000 error=", hybrid.mass_oracle_error);
    TORCH_CHECK(ordinary.vertical_work_max < 1.0e-6 && hybrid.vertical_work_max < 1.0e-6,
                "unexpected vertical U advection work: c2=0 max=", ordinary.vertical_work_max,
                " c2=10000 max=", hybrid.vertical_work_max);
    std::cout << (packed ? "packed" : "unpacked") << " mass_dot_l2=" << ordinary.mass_dot_l2
              << " ordinary_even_u_l2=" << ordinary_l2 << " hybrid_delta_l2=" << delta_l2
              << " hybrid_delta_max=" << delta_max
              << " mass_oracle_error=" << ordinary.mass_oracle_error << "/"
              << hybrid.mass_oracle_error << " vertical_work_max=" << ordinary.vertical_work_max
              << "/" << hybrid.vertical_work_max << '\n';
}
} // namespace

int main() {
    torch::NoGradGuard no_grad;
    torch::set_num_threads(1);
    auto &cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1; // ordinary mass coordinate
    cfg.mu_horizontal_div_only = false;
    cfg.wrf_omega_ww_cp = true; // Omega vanishes for this vertically uniform flow
    cfg.hevi_split = false;
    cfg.omega_w_blend = 1.0f;
    cfg.sign_smooth_delta = 0.0f;

    check_case(true);
    check_case(false);
    return 0;
}
