#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

using wrf::sdirk3::RhsMode;
using wrf::sdirk3::SDIRK3Config;
using wrf::sdirk3::g_sdirk3_config;
using wrf::sdirk3::test::TileCase;
using wrf::sdirk3::test::nx;
using wrf::sdirk3::test::ny;
using wrf::sdirk3::test::nz;
using wrf::sdirk3::test::nu;
using wrf::sdirk3::test::nv;
using wrf::sdirk3::test::nw;
using wrf::sdirk3::test::su;
using wrf::sdirk3::test::sv;
using wrf::sdirk3::test::sw;
using wrf::sdirk3::test::st;
using wrf::sdirk3::test::sm;
using wrf::sdirk3::test::total;

struct ProjectTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&);
    friend type access(ProjectTag);
};
struct PackedTag {
    using type = bool (TileSDIRK3UnifiedSolver::*)() const;
    friend type access(PackedTag);
};
struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<ProjectTag, &TileSDIRK3UnifiedSolver::projectStateBoundaries>;
template struct Accessor<PackedTag, &TileSDIRK3UnifiedSolver::isPackedPeriodicDomain>;
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;

constexpr int n = nx - 1;
constexpr int m = ny - 1;
constexpr double pi = 3.14159265358979323846;

torch::Tensor project(TileCase& tile, const torch::Tensor& x) {
    return (tile.solver.*access(ProjectTag{}))(x);
}

// Deliberately scalar-indexed oracle. It does not share the production cat/slice
// construction, so a dimension or sign error in Q cannot be self-consistent.
torch::Tensor oracle(const torch::Tensor& input, bool packed, int normal_v_upper = nv - 1) {
    auto out = input.clone();
    auto u = out.slice(0, 0, su).view({ny, nz, nu});
    auto v = out.slice(0, su, su + sv).view({nv, nz, nx});
    auto copy3 = [](torch::Tensor q, int rows, int aliases) {
        for (int j = 0; j < rows; ++j)
            for (int k = 0; k < q.size(1); ++k) {
                q.index_put_({j, k, n}, q.index({j, k, 0}));
                if (aliases == 2)
                    q.index_put_({j, k, n + 1}, q.index({j, k, 1}));
            }
    };
    auto copy2 = [](torch::Tensor q) {
        for (int j = 0; j < m; ++j)
            q.index_put_({j, n}, q.index({j, 0}));
    };
    if (!packed) {
        // TileCase's constructor indices describe a physical symmetric wall at
        // both V endpoints. X is periodic but is not an endpoint projection here.
        v.select(0, 0).zero_();
        v.select(0, normal_v_upper).zero_();
        return out;
    }

    v.select(0, 0).zero_();
    v.select(0, m).zero_();
    copy3(u, m, 2);
    u.index_put_({m}, u.index({m - 1}));
    copy3(v, m + 1, 1);

    // Even Y rows for mass-like fields and the actual WRF odd V north ghost.
    auto w = out.slice(0, su + sv, su + sv + sw).view({ny, nw, nx});
    auto ph = out.slice(0, su + sv + sw, su + sv + 2 * sw).view({ny, nw, nx});
    auto t = out.slice(0, su + sv + 2 * sw, su + sv + 2 * sw + st).view({ny, nz, nx});
    copy3(w, m, 1);
    copy3(ph, m, 1);
    copy3(t, m, 1);
    copy2(out.slice(0, total - sm, total).view({ny, nx}));
    w.index_put_({m}, w.index({m - 1}));
    ph.index_put_({m}, ph.index({m - 1}));
    t.index_put_({m}, t.index({m - 1}));
    auto mu = out.slice(0, total - sm, total).view({ny, nx});
    mu.index_put_({m}, mu.index({m - 1}));
    v.index_put_({m + 1}, -v.index({m - 1}));
    return out;
}

void configure_packed(TileCase& tile) {
    tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                              1, nx, 1, ny, 1, nz,
                              1, nu, 1, nv, 1, nw);
}

void check_projection_contract(torch::ScalarType dtype) {
    TileCase tile;
    configure_packed(tile);
    TORCH_CHECK((tile.solver.*access(PackedTag{}))(), "packed fixture was not recognized");
    auto opts = torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    auto x = torch::randn({total}, opts);
    const auto got = project(tile, x);
    const auto want = oracle(x, true);
    const double oracle_err = (got - want).abs().max().item<double>();
    const double idem_err = (project(tile, got) - got).abs().max().item<double>();
    const double tol = dtype == torch::kFloat64 ? 1.0e-12 : 2.0e-6;
    TORCH_CHECK(oracle_err <= tol, "packed scalar oracle mismatch: ", oracle_err);
    TORCH_CHECK(idem_err <= tol, "Q^2 != Q: ", idem_err);

    auto leaf = torch::randn({total}, opts).requires_grad_(true);
    auto seed = torch::randn({total}, opts);  // includes arbitrary ghost cotangents
    const auto projected = project(tile, leaf);
    const auto grad = torch::autograd::grad({(projected * seed).sum()}, {leaf})[0];
    const auto direction = torch::randn({total}, opts);
    const double lhs = (seed * project(tile, direction)).sum().item<double>();
    const double rhs = (grad * direction).sum().item<double>();
    const double dot_err = std::abs(lhs - rhs);
    TORCH_CHECK(dot_err <= 2.0e-11 * std::max({1.0, std::abs(lhs), std::abs(rhs)}) ||
                    (dtype == torch::kFloat32 && dot_err <= 2.0e-5),
                "Q^T dot identity failed: ", dot_err);

    // Negative control: Q is oblique in packed Euclidean coordinates. A unit at
    // a mass ghost is pulled back to its representative, so Q and Q^T differ.
    auto ghost = torch::zeros({total}, opts);
    auto representative_seed = torch::zeros({total}, opts);
    ghost.index_put_({total - sm + n}, 1.0);
    representative_seed.index_put_({total - sm}, 1.0);
    const double wrong_lhs = (representative_seed * project(tile, ghost)).sum().item<double>();
    const double wrong_rhs = (project(tile, representative_seed) * ghost).sum().item<double>();
    TORCH_CHECK(std::abs(wrong_lhs - wrong_rhs) > 0.5,
                "negative control failed to distinguish Q from Q^T");
    std::cout << "PACKED_BOUNDARY dtype=" << (dtype == torch::kFloat64 ? "float64" : "float32")
              << " oracle_max=" << oracle_err << " idempotence_max=" << idem_err
              << " dot_error=" << dot_err << " q_ne_qt_delta="
              << std::abs(wrong_lhs - wrong_rhs) << '\n';
}

void check_exclusions() {
    auto& cfg = g_sdirk3_config;
    TileCase packed;
    configure_packed(packed);
    auto x = torch::randn({total}, torch::kFloat32);
    cfg.enable_ad_halo_exchange = true;
    TORCH_CHECK(!(packed.solver.*access(PackedTag{}))(), "AD halo enabled packed gate");
    const auto halo_expected = oracle(x, false, m);
    const double halo_err = (project(packed, x) - halo_expected).abs().max().item<double>();
    TORCH_CHECK(halo_err == 0.0, "AD halo path changed normal-wall projection: ", halo_err);
    cfg.enable_ad_halo_exchange = false;

    auto physical = TileCase();
    TORCH_CHECK(!(physical.solver.*access(PackedTag{}))(), "physical-dimension fixture packed gate");
    const auto physical_expected = oracle(x, false, nv - 1);
    const double physical_err = (project(physical, x) - physical_expected).abs().max().item<double>();
    TORCH_CHECK(physical_err == 0.0,
                "physical-dimension fixture changed normal-wall projection: ", physical_err);
    std::cout << "PACKED_BOUNDARY exclusions halo_max=" << halo_err
              << " physical_dimension_max=" << physical_err << '\n';
}

void check_rhs_closure() {
    auto& cfg = g_sdirk3_config;
    cfg.enable_ad_halo_exchange = false;
    TileCase tile;
    configure_packed(tile);
    tile.step(1.0e-4f);
    auto state = torch::zeros({total}, torch::kFloat32);
    auto u = state.slice(0, 0, su).view({ny, nz, nu});
    for (int j = 0; j < m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < n; ++i)
                u[j][k][i] = std::sin(2.0 * pi * i / static_cast<double>(n));
    state = project(tile, state).contiguous();
    const auto rhs = [&](RhsMode mode) {
        return (tile.solver.*access(RhsTag{}))(state, mode);
    };
    double full_mu_core = 0.0;
    for (const auto mode : {RhsMode::Full, RhsMode::ExplicitOnly, RhsMode::ImplicitOnly}) {
        const auto F = rhs(mode);
        const double closure = (F - project(tile, F)).abs().max().item<double>();
        TORCH_CHECK(torch::isfinite(F).all().item<bool>(), "RHS has NaN/Inf");
        TORCH_CHECK(closure <= 2.0e-6, "RHS is outside ran(Q), mode=", static_cast<int>(mode),
                    " residual=", closure);
        if (mode == RhsMode::Full) {
            const auto mu = F.slice(0, total - sm, total).view({ny, nx});
            full_mu_core = mu.slice(0, 0, m).slice(1, 0, n).norm().item<double>();
        }
        std::cout << "PACKED_BOUNDARY rhs_mode=" << static_cast<int>(mode)
                  << " closure_max=" << closure << '\n';
    }
    TORCH_CHECK(full_mu_core > 1.0e-8, "full RHS physical mu control was zero");
    std::cout << "PACKED_BOUNDARY rhs_full_mu_core=" << full_mu_core << '\n';
}

} // namespace

int main() {
    torch::set_num_threads(1);
    torch::manual_seed(20260905);
    g_sdirk3_config = SDIRK3Config{};
    g_sdirk3_config.debug_level = 0;
    g_sdirk3_config.imex_split_mode = 3;
    g_sdirk3_config.split_explicit = false;
    g_sdirk3_config.rhs_bc_parity = false;
    g_sdirk3_config.mass_pgf_bc_guard = false;
    g_sdirk3_config.mu_tend_fortran_parity = true;
    check_projection_contract(torch::kFloat32);
    check_projection_contract(torch::kFloat64);
    check_exclusions();
    check_rhs_closure();
    return 0;
}
