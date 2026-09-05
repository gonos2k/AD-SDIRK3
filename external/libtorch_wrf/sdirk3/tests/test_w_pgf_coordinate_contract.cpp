// Independent WRF pressure-force oracle, including the W top boundary.
#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;
struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct PressureTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(PressureTag);
};
struct CoefficientsTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                 const float*, const float*);
    friend type access(CoefficientsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<PressureTag, &TileSDIRK3UnifiedSolver::p_pert_>;
template struct Accessor<CoefficientsTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

void check_w(float map, bool hybrid, bool hevi, RhsMode mode) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.hevi_split = hevi;
    TileCase tile;
    std::fill(tile.mass_map.begin(), tile.mass_map.end(), map);
    tile.step(1.0e-4f);
    tile.solver.getGridInfo()->rdn = torch::full({nz}, 4.0f);
    tile.solver.getGridInfo()->rdnw = torch::full({nz}, 4.0f);
    tile.solver.getGridInfo()->dnw = torch::full({nz}, -0.25f);

    std::vector<float> c1h(nz, 1), c2h(nz, 0), c1f(nw, 1), c2f(nw, 0);
    if (hybrid) {
        // Derivative of B(eta)=1.4*eta-0.4*eta^2: positive layer masses,
        // sum(-dnw*c1h)=1 and sum(-dnw*c2h)=0.
        for (int k = 0; k < nz; ++k) {
            c1h[k] = 0.7f + 0.2f*k;
            c2h[k] = 100000.0f*(1.0f-c1h[k]);
        }
        for (int k = 0; k < nw; ++k) {
            c1f[k] = 0.6f + 0.2f*k;
            c2f[k] = 100000.0f*(1.0f-c1f[k]);
        }
    }
    (tile.solver.*access(CoefficientsTag{}))(
        c1f.data(), c2f.data(), c1h.data(), c2h.data());
    for (auto* field : tile.fields()) std::fill(field->begin(), field->end(), 0.0f);
    const auto base = tile.state();
    auto perturbed = base.clone();
    auto theta = perturbed.slice(0, su+sv+2*sw, su+sv+2*sw+st).view({ny,nz,nx});
    for (int k = 0; k < nz; ++k) theta.select(1,k).fill_(4.0f*(k+1));

    const auto evaluate = [&](const torch::Tensor& state, torch::Tensor& pressure) {
        const auto rhs = (tile.solver.*access(RhsTag{}))(state, mode);
        TORCH_CHECK(torch::isfinite(rhs).all().item<bool>(), "nonfinite W PGF RHS");
        TORCH_CHECK(rhs.slice(0, su+sv+2*sw+st).abs().max().item<double>() == 0,
                    "pressure-only W fixture has a mass tendency");
        pressure = (tile.solver.*access(PressureTag{})).to(torch::kFloat64).clone();
        return rhs.slice(0, su+sv, su+sv+sw).view({ny,nw,nx})
            .to(torch::kFloat64).clone();
    };
    torch::Tensor p0, p1;
    const auto w0 = evaluate(base, p0);
    const auto w1 = evaluate(perturbed, p1);
    const auto dp = p1-p0;
    TORCH_CHECK(torch::isfinite(dp).all().item<bool>(), "nonfinite pressure operand");
    auto expected = torch::zeros({ny,nw,nx}, torch::kFloat64);
    constexpr double gravity = double(9.81f);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            for (int k = 1; k <= nz; ++k) {
                const double alpha = (double(c1f[k])*80000.0+c2f[k])/map;
                TORCH_CHECK(alpha > 0 && std::isfinite(alpha), "invalid coupled mass");
                // module_big_step_utilities_em.F:2484-2494, subtract two
                // rest states: fixed buoyancy cancels, only pressure changes.
                const double raw = k == nz
                    ? (gravity/map)*8.0*dp[j][nz-1][i].item<double>()
                    : -(gravity/map)*4.0*(dp[j][k][i].item<double>()-
                                           dp[j][k-1][i].item<double>());
                expected[j][k][i] = raw/alpha;
            }
        }
    }
    const double signal = expected.abs().max().item<double>();
    const double error = (w1-w0-expected).abs().max().item<double>();
    TORCH_CHECK(signal > 1e-4 && std::isfinite(error), "uninformative W PGF oracle");
    const double tolerance = 128*std::numeric_limits<float>::epsilon()*signal;
    std::cout << "W PGF map=" << map << " hybrid=" << hybrid << " hevi=" << hevi
              << " mode=" << static_cast<int>(mode) << " signal=" << signal
              << " max_abs_error=" << error << '\n';
    TORCH_CHECK(error <= tolerance, "W PGF coordinate contract error=", error,
                " tolerance=", tolerance);
}
} // namespace

void check_w_pgf_coordinates() {
    for (float map : {1.0f, 2.5f})
        for (bool hybrid : {false, true})
            for (bool hevi : {false, true})
                for (auto mode : {RhsMode::Full, RhsMode::ImplicitOnly})
                    check_w(map, hybrid, hevi, mode);
}
