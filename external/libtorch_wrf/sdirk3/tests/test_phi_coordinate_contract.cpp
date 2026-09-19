// The complete coupled PH equation must give physical dPhi/dt=g*w at rest.
#include "tile_test_fixture.h"
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(RhsTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(
        const float*, const float*, const float*, const float*);
    friend type access(CoeffTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

double check_uniform_w(float msfty, bool varying_mass, bool hybrid,
                       wrf::sdirk3::RhsMode mode) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.imex_enabled = true;
    cfg.mass_coordinate_mode = 1;
    cfg.hevi_split = false;
    cfg.buoyancy_use_current_w = true;

    TileCase tile;
    std::fill(tile.mass_map.begin(), tile.mass_map.end(), msfty);
    tile.step(1.0e-4f);
    // Synchronize setup/runtime map sources for the physical conversion.
    tile.solver.getGridInfo()->msfty = torch::full({ny, nx}, msfty);

    std::vector<float> c1f = {0.65f, 0.71f, 0.83f, 0.97f, 1.11f};
    std::vector<float> c2f = {900.0f, 1030.0f, 1170.0f, 1310.0f, 1450.0f};
    std::vector<float> c1h = {0.61f, 0.73f, 0.86f, 0.99f};
    std::vector<float> c2h = {880.0f, 1020.0f, 1160.0f, 1300.0f};
    if (!hybrid) {
        std::fill(c1f.begin(), c1f.end(), 1.0f);
        std::fill(c1h.begin(), c1h.end(), 1.0f);
        std::fill(c2f.begin(), c2f.end(), 0.0f);
        std::fill(c2h.begin(), c2h.end(), 0.0f);
    }
    (tile.solver.*access(CoeffTag{}))(
        c1f.data(), c2f.data(), c1h.data(), c2h.data());

    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    if (varying_mass)
        for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i)
            tile.mu[j*nx+i] = 600.0f + 17.0f*j + 9.0f*i;
    const auto state_zero = tile.state();
    std::fill(tile.w.begin(), tile.w.end(), 2.0f);
    const auto state_w = tile.state();
    const auto zero_rhs = (tile.solver.*access(RhsTag{}))(
        state_zero, mode);
    const auto w_rhs = (tile.solver.*access(RhsTag{}))(
        state_w, mode);
    const auto zero_ph = zero_rhs.slice(0, su + sv + sw, su + sv + 2 * sw)
                                  .view({ny, nw, nx});
    const auto w_ph = w_rhs.slice(0, su + sv + sw, su + sv + 2 * sw)
                            .view({ny, nw, nx});
    const double expected = 9.81 * 2.0;
    const auto delta = (w_ph.to(torch::kFloat64)-zero_ph.to(torch::kFloat64))
        .slice(1,1,nw);
    TORCH_CHECK(torch::isfinite(delta).all().item<bool>(), "nonfinite PH/W rate");
    const double error = (delta-expected).abs().max().item<double>();
    std::cout << "PHI_COORD msfty=" << msfty << " variable_mass=" << varying_mass
              << " hybrid=" << hybrid << " mode=" << static_cast<int>(mode)
              << " expected=" << expected << " max_error=" << error << '\n';
    return error;
}
}  // namespace

int main() {
    torch::set_num_threads(1);
    for (const float map : {1.0f,2.5f})
        for (const bool mass : {false,true})
            for (const bool hybrid : {false,true})
                for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                                        wrf::sdirk3::RhsMode::ImplicitOnly}) {
                    const auto error=check_uniform_w(map,mass,hybrid,mode);
                    TORCH_CHECK(error < 2.0e-5, "Fortran physical PH/W contract mismatch: ",error);
                }
    return 0;
}
