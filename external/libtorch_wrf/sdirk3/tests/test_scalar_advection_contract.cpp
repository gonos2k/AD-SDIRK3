#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"

#include <torch/torch.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

// Test-only member pointers exercise the compiled production helpers without
// changing their production visibility or adding a public test API.
struct AdvectXTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float);
    friend type access(AdvectXTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<AdvectXTag, &TileSDIRK3UnifiedSolver::advect_scalar_x>;

struct AdvectYTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float);
    friend type access(AdvectYTag);
};
template struct Accessor<AdvectYTag, &TileSDIRK3UnifiedSolver::advect_scalar_y>;

struct DivTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
         const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
         const torch::Tensor&, float, float, bool);
    friend type access(DivTag);
};
template struct Accessor<DivTag, &TileSDIRK3UnifiedSolver::compute_3d_divergence>;

namespace {

double advection_error(int n, double velocity, bool y_direction) {
    constexpr int nz = 1;
    const float h = 1.0f / n;
    TileSDIRK3UnifiedSolver tile(n, n, nz, h, h,
                                 {h}, {h}, {1.0f}, 0);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.advection_order = 3;
    cfg.sign_smooth_delta = 0.0f;
    cfg.debug_level = 0;

    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    const int64_t n_mass = n;
    const int64_t n_stag = n + 1;
    auto f = torch::zeros({n, nz, n_mass}, opts);
    auto vel = torch::full({n, nz, n_stag}, velocity, opts);
    if (y_direction) {
        f = torch::zeros({n_mass, nz, n}, opts);
        vel = torch::full({n_stag, nz, n}, velocity, opts);
    }
    auto f_acc = f.accessor<double, 3>();
    for (int j = 0; j < f.size(0); ++j)
        for (int i = 0; i < f.size(2); ++i) {
            const double x = static_cast<double>(y_direction ? j : i) / n;
            f_acc[j][0][i] = x * x * x * x;
        }

    const auto tendency = y_direction
        ? (tile.*access(AdvectYTag{}))(f, vel, 1.0f / h)
        : (tile.*access(AdvectXTag{}))(f, vel, 1.0f / h);
    const auto out = tendency.to(torch::kCPU).contiguous();
    double max_error = 0.0;
    const int begin = 8;
    const int end = n - 8;
    for (int i = begin; i < end; ++i) {
        const double x = static_cast<double>(i) / n;
        const double exact = -velocity * 4.0 * x * x * x;
        const double got = y_direction ? out[i][0][0].item<double>()
                                       : out[0][0][i].item<double>();
        max_error = std::max(max_error, std::abs(got - exact));
    }
    return max_error;
}

void check_advection_order() {
    for (bool y : {false, true}) {
        for (double velocity : {-1.0, 1.0}) {
            const double e32 = advection_error(32, velocity, y);
            const double e64 = advection_error(64, velocity, y);
            const double e128 = advection_error(128, velocity, y);
            TORCH_CHECK(e32 > 0.0 && e64 > 0.0 && e128 > 0.0,
                        "degenerate advection error");
            const double order1 = std::log(e32 / e64) / std::log(2.0);
            const double order2 = std::log(e64 / e128) / std::log(2.0);
            TORCH_CHECK(order1 > 2.7 && order2 > 2.7,
                        "order-3 scalar advection failed: y=", y,
                        " velocity=", velocity, " orders=", order1, ",", order2);
            std::cout << "scalar_advection y=" << y << " velocity=" << velocity
                      << " errors=" << e32 << "," << e64 << "," << e128
                      << " orders=" << order1 << "," << order2 << '\n';
        }
    }
}

void check_divergence_finite_contract() {
    constexpr int n = 16;
    constexpr int nz = 2;
    TileSDIRK3UnifiedSolver tile(n, n, nz, 1.0, 1.0,
                                 {1.0}, {1.0}, {1.0f, 1.0f}, 0);
    tile.setStaggeredDimensions(n + 1, n + 1, nz + 1);
    // Make index 0 a physical open boundary. computeInteriorBounds then
    // consumes only [1:n-1), so a NaN at u[...,0] is a true unused halo/boundary
    // control rather than an accidentally active periodic seam value.
    tile.setBoundaryConditions(false, false, false, false, false, false,
                               true, false, true, false, false, false);
    auto opts = torch::TensorOptions().dtype(torch::kFloat32);
    auto u = torch::ones({n, nz, n + 1}, opts);
    auto v = torch::ones({n + 1, nz, n}, opts);
    auto w = torch::ones({n, nz + 1, n}, opts);
    auto mass_x = torch::ones({n, n}, opts);
    auto mass_y = torch::ones({n, n}, opts);
    auto u_map = torch::ones({n, n + 1}, opts);
    auto v_map = torch::ones({n + 1, n}, opts);
    auto div = (tile.*access(DivTag{}))(u, v, w, mass_x, mass_y, u_map, v_map,
                                         1.0f, 1.0f, true);
    TORCH_CHECK(torch::isfinite(div).all().item<bool>(),
                "finite control divergence is not finite");

    auto unused_halo_nan = u.clone();
    unused_halo_nan.select(2, 0).fill_(std::numeric_limits<float>::quiet_NaN());
    auto halo_div = (tile.*access(DivTag{}))(unused_halo_nan, v, w, mass_x, mass_y,
                                              u_map, v_map, 1.0f, 1.0f, true);
    TORCH_CHECK(torch::isfinite(halo_div).all().item<bool>(),
                "unused halo NaN was over-checked");

    auto consumed_nan = u.clone();
    consumed_nan.index_put_({6, 0, 6}, std::numeric_limits<float>::quiet_NaN());
    auto bad_div = (tile.*access(DivTag{}))(consumed_nan, v, w, mass_x, mass_y,
                                             u_map, v_map, 1.0f, 1.0f, true);
    TORCH_CHECK(!torch::isfinite(bad_div).all().item<bool>(),
                "consumed NaN was hidden by divergence sanitization");
}

}  // namespace

int main() {
    torch::set_num_threads(1);
    wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    check_advection_order();
    check_divergence_finite_contract();
    std::cout << "scalar advection/divergence contract: PASS\n";
}
