// Exercise derived vertical weights in the assembled production geopotential RHS.
#include "tile_test_fixture.h"

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
}
