#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"

#include <torch/torch.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// Test-only member pointers exercise the compiled production helpers without
// changing their production visibility or adding a public test API.
struct AdvectXTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float, torch::Tensor*);
    friend type access(AdvectXTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<AdvectXTag, &TileSDIRK3UnifiedSolver::advect_scalar_x>;

struct AdvectYTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float, torch::Tensor*);
    friend type access(AdvectYTag);
};
template struct Accessor<AdvectYTag, &TileSDIRK3UnifiedSolver::advect_scalar_y>;

namespace {

constexpr double kPi = 3.14159265358979323846;

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
        ? (tile.*access(AdvectYTag{}))(f, vel, 1.0f / h, nullptr)
        : (tile.*access(AdvectXTag{}))(f, vel, 1.0f / h, nullptr);
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

void configure_packed_solver(TileSDIRK3UnifiedSolver& tile, int n) {
    // The production whole-domain packed convention has one endpoint in each
    // mass dimension: nx_=ide-ids+1 and ny_=jde-jds+1.  Keep the test in the
    // ordinary ARK contract; the private coupled-export guard is never entered
    // by these direct helper calls.
    tile.setWRFIndices(1, n, 1, n, 1, 1,
                       1, n, 1, n, 1, 1,
                       1, n, 1, n, 1, 1);
    tile.setBoundaryConditions(true, false, false, false, true, true,
                               false, false, false, false, false, false);
}

double periodic_value(const std::vector<double>& q, int i) {
    const int n = static_cast<int>(q.size());
    i %= n;
    if (i < 0) i += n;
    return q[static_cast<size_t>(i)];
}

double reflected_value(const std::vector<double>& q, int i) {
    const int m = static_cast<int>(q.size());
    if (i < 0) return q[static_cast<size_t>(-i - 1)];
    if (i >= m) return q[static_cast<size_t>(2 * m - 1 - i)];
    return q[static_cast<size_t>(i)];
}

double signed_velocity(double velocity) {
    return velocity > 0.0 ? 1.0 : (velocity < 0.0 ? -1.0 : 0.0);
}

double oracle_flux(int order, double q_im3, double q_im2, double q_im1,
                   double q_i, double q_ip1, double q_ip2, double velocity) {
    const double s = signed_velocity(velocity);
    if (order <= 2)
        return 0.5 * velocity * (q_i + q_im1);
    if (order < 5) {
        const double flux4 = (7.0 * (q_i + q_im1) - (q_ip1 + q_im2)) / 12.0;
        return velocity * (flux4 + s * ((q_ip1 - q_im2) -
                                         3.0 * (q_i - q_im1)) / 12.0);
    }
    const double flux6 = (37.0 * (q_i + q_im1) -
                          8.0 * (q_ip1 + q_im2) +
                          (q_ip2 + q_im3)) / 60.0;
    return velocity * (flux6 - s * ((q_ip2 - q_im3) -
                                    5.0 * (q_ip1 - q_im2) +
                                    10.0 * (q_i - q_im1)) / 60.0);
}

std::vector<double> packed_x_oracle(const std::vector<double>& q,
                                    const std::vector<double>& velocity,
                                    int order) {
    const int n = static_cast<int>(q.size());
    std::vector<double> flux(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        flux[static_cast<size_t>(i)] = oracle_flux(
            order, periodic_value(q, i - 3), periodic_value(q, i - 2),
            periodic_value(q, i - 1), periodic_value(q, i),
            periodic_value(q, i + 1), periodic_value(q, i + 2),
            velocity[static_cast<size_t>(i)]);
    }
    std::vector<double> tendency(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        tendency[static_cast<size_t>(i)] =
            -(flux[static_cast<size_t>((i + 1) % n)] - flux[static_cast<size_t>(i)]);
    return tendency;
}

std::vector<double> packed_y_oracle(const std::vector<double>& q,
                                    const std::vector<double>& velocity,
                                    int order) {
    const int m = static_cast<int>(q.size());
    std::vector<double> flux(static_cast<size_t>(m + 1));
    for (int j = 0; j <= m; ++j) {
        flux[static_cast<size_t>(j)] = oracle_flux(
            order, reflected_value(q, j - 3), reflected_value(q, j - 2),
            reflected_value(q, j - 1), reflected_value(q, j),
            reflected_value(q, j + 1), reflected_value(q, j + 2),
            velocity[static_cast<size_t>(j)]);
    }
    std::vector<double> tendency(static_cast<size_t>(m));
    for (int j = 0; j < m; ++j)
        tendency[static_cast<size_t>(j)] =
            -(flux[static_cast<size_t>(j + 1)] - flux[static_cast<size_t>(j)]);
    return tendency;
}

void check_packed_x_oracle(int order, double velocity_sign) {
    constexpr int n = 13;
    constexpr int nz = 1;
    const int period = n - 1;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.advection_order = order;
    cfg.sign_smooth_delta = 0.0f;
    cfg.enable_ad_halo_exchange = false;

    TileSDIRK3UnifiedSolver tile(n, n, nz, 1.0f, 1.0f,
                                 {1.0f}, {1.0f}, {1.0f}, 0);
    configure_packed_solver(tile, n);
    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto f = torch::zeros({n, nz, n}, opts);
    auto u = torch::zeros({n, nz, n + 1}, opts);
    std::vector<double> q(static_cast<size_t>(period));
    std::vector<double> velocity(static_cast<size_t>(period));
    for (int i = 0; i < period; ++i) {
        const double x = static_cast<double>(i) / period;
        q[static_cast<size_t>(i)] = 0.25 + 0.07 * x +
            0.013 * x * x + 0.002 * x * x * x;
        velocity[static_cast<size_t>(i)] = velocity_sign *
            (0.8 + 0.1 * std::cos(2.0 * kPi * x));
    }
    auto f_acc = f.accessor<double, 3>();
    auto u_acc = u.accessor<double, 3>();
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < period; ++i) {
            f_acc[j][0][i] = q[static_cast<size_t>(i)];
            u_acc[j][0][i] = velocity[static_cast<size_t>(i)];
        }
        // Q's packed U aliases are present in the input, although the true
        // periodic helper must compute only the independent period columns.
        f_acc[j][0][period] = f_acc[j][0][0];
        u_acc[j][0][period] = u_acc[j][0][0];
        u_acc[j][0][period + 1] = u_acc[j][0][1];
    }

    const auto got = (tile.*access(AdvectXTag{}))(f, u, 1.0f, nullptr)
                         .to(torch::kCPU).contiguous();
    const auto out = got.accessor<double, 3>();
    const auto expected = packed_x_oracle(q, velocity, order);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < period; ++i)
            TORCH_CHECK(std::abs(out[j][0][i] - expected[static_cast<size_t>(i)]) < 2e-12,
                        "packed X oracle mismatch order=", order,
                        " sign=", velocity_sign, " j=", j, " i=", i,
                        " got=", out[j][0][i], " expected=", expected[static_cast<size_t>(i)]);

    // The mass endpoint and the two U seam aliases are adapter rows.  The
    // independent periodic core must not read them when forming true faces.
    auto altered_f = f.clone();
    altered_f.select(2, period).fill_(17.0);
    auto altered_u = u.clone();
    altered_u.select(2, period).fill_(19.0);
    altered_u.select(2, period + 1).fill_(-23.0);
    const auto altered = (tile.*access(AdvectXTag{}))(altered_f, altered_u, 1.0f, nullptr)
                             .to(torch::kCPU).contiguous();
    const auto altered_out = altered.accessor<double, 3>();
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < period; ++i)
            TORCH_CHECK(std::abs(altered_out[j][0][i] - out[j][0][i]) < 2e-12,
                        "packed X true divergence used an adapter alias");
}

void check_packed_y_oracle(int order, double velocity_sign) {
    constexpr int n = 13;
    constexpr int nz = 1;
    const int m = n - 1;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.advection_order = order;
    cfg.sign_smooth_delta = 0.0f;
    cfg.enable_ad_halo_exchange = false;

    TileSDIRK3UnifiedSolver tile(n, n, nz, 1.0f, 1.0f,
                                 {1.0f}, {1.0f}, {1.0f}, 0);
    configure_packed_solver(tile, n);
    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto f = torch::zeros({n, nz, n}, opts);
    auto v = torch::zeros({n + 1, nz, n}, opts);
    std::vector<double> q(static_cast<size_t>(m));
    std::vector<double> velocity(static_cast<size_t>(m + 1), 0.0);
    for (int j = 0; j < m; ++j) {
        const double y = static_cast<double>(j) / m;
        q[static_cast<size_t>(j)] = 0.25 + 0.06 * y +
            0.017 * y * y + 0.003 * y * y * y;
    }
    for (int j = 1; j < m; ++j) {
        const double y = static_cast<double>(j) / m;
        velocity[static_cast<size_t>(j)] = velocity_sign *
            (0.8 + 0.1 * std::cos(2.0 * kPi * y));
    }
    auto f_acc = f.accessor<double, 3>();
    auto v_acc = v.accessor<double, 3>();
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            f_acc[j][0][i] = q[static_cast<size_t>(j)];
    for (int i = 0; i < n; ++i) {
        f_acc[m][0][i] = f_acc[m - 1][0][i];
        for (int j = 0; j <= m; ++j)
            v_acc[j][0][i] = velocity[static_cast<size_t>(j)];
        // This odd row is part of the packed V input contract, but it must not
        // be used for the true scalar divergence rows.
        v_acc[m + 1][0][i] = -v_acc[m - 1][0][i];
    }

    const auto got = (tile.*access(AdvectYTag{}))(f, v, 1.0f, nullptr)
                         .to(torch::kCPU).contiguous();
    const auto out = got.accessor<double, 3>();
    const auto expected = packed_y_oracle(q, velocity, order);
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            TORCH_CHECK(std::abs(out[j][0][i] - expected[static_cast<size_t>(j)]) < 2e-12,
                        "packed Y oracle mismatch order=", order,
                        " sign=", velocity_sign, " j=", j, " i=", i,
                        " got=", out[j][0][i], " expected=", expected[static_cast<size_t>(j)]);
    for (int i = 0; i < n; ++i)
        TORCH_CHECK(std::abs(out[m][0][i]) < 2e-12,
                    "packed Y scalar endpoint was not the retained zero-flux alias");

    // The odd V row is a state-extension row, not an additional physical face.
    // Changing it must not change any true scalar divergence row.
    auto altered_ghost = v.clone();
    altered_ghost.select(0, m + 1).fill_(17.0);
    const auto ghost_tensor = (tile.*access(AdvectYTag{}))(f, altered_ghost, 1.0f, nullptr)
                                  .to(torch::kCPU).contiguous();
    const auto ghost_out = ghost_tensor.accessor<double, 3>();
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            TORCH_CHECK(std::abs(ghost_out[j][0][i] - out[j][0][i]) < 2e-12,
                        "packed Y true divergence used the extra V ghost row");
}

torch::Tensor packed_x_field_from_core(const torch::Tensor& core) {
    return torch::cat({core, core.slice(2, 0, 1)}, 2);
}

torch::Tensor packed_y_field_from_core(const torch::Tensor& core) {
    const int64_t m = core.size(0);
    return torch::cat({core, core.slice(0, m - 1, m)}, 0);
}

void check_packed_ad_fd(bool y_direction) {
    constexpr int n = 13;
    constexpr int nz = 1;
    const int true_size = n - 1;
    constexpr double fd_step = 1.0e-6;
    constexpr int order = 5;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.advection_order = order;
    cfg.sign_smooth_delta = 0.0f;
    cfg.enable_ad_halo_exchange = false;

    TileSDIRK3UnifiedSolver tile(n, n, nz, 1.0f, 1.0f,
                                 {1.0f}, {1.0f}, {1.0f}, 0);
    configure_packed_solver(tile, n);
    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    const auto core_shape = y_direction
        ? std::vector<int64_t>{true_size, nz, n}
        : std::vector<int64_t>{n, nz, true_size};
    auto core = torch::zeros(core_shape, opts);
    auto direction = torch::zeros(core_shape, opts);
    if (y_direction) {
        auto c = core.accessor<double, 3>();
        auto d = direction.accessor<double, 3>();
        for (int j = 0; j < true_size; ++j)
            for (int i = 0; i < n; ++i) {
                c[j][0][i] = 0.2 + 0.04 * j + 0.003 * j * j + 0.01 * i;
                d[j][0][i] = 0.01 + 0.002 * j - 0.0003 * i;
            }
    } else {
        auto c = core.accessor<double, 3>();
        auto d = direction.accessor<double, 3>();
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < true_size; ++i) {
                c[j][0][i] = 0.2 + 0.04 * i + 0.003 * i * i + 0.01 * j;
                d[j][0][i] = 0.01 + 0.002 * i - 0.0003 * j;
            }
    }
    core = core.requires_grad_(true);
    auto f = y_direction ? packed_y_field_from_core(core)
                         : packed_x_field_from_core(core);

    torch::Tensor transport;
    if (y_direction) {
        transport = torch::zeros({n + 1, nz, n}, opts);
        auto a = transport.accessor<double, 3>();
        for (int i = 0; i < n; ++i) {
            a[0][0][i] = 0.0;
            a[true_size][0][i] = 0.0;
            for (int j = 1; j < true_size; ++j)
                a[j][0][i] = 0.8 + 0.1 * std::cos(2.0 * kPi * j / true_size);
            a[true_size + 1][0][i] = -a[true_size - 1][0][i];
        }
    } else {
        transport = torch::zeros({n, nz, n + 1}, opts);
        auto a = transport.accessor<double, 3>();
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < true_size; ++i)
                a[j][0][i] = 0.8 + 0.1 * std::cos(2.0 * kPi * i / true_size);
            a[j][0][true_size] = a[j][0][0];
            a[j][0][true_size + 1] = a[j][0][1];
        }
    }

    const auto apply = [&](const torch::Tensor& field) {
        return y_direction ? (tile.*access(AdvectYTag{}))(field, transport, 1.0f, nullptr)
                            : (tile.*access(AdvectXTag{}))(field, transport, 1.0f, nullptr);
    };
    const auto base = apply(f);
    const auto output_core = y_direction ? base.slice(0, 0, true_size)
                                         : base.slice(2, 0, true_size);
    auto seed = torch::zeros_like(output_core);
    if (y_direction) {
        auto s = seed.accessor<double, 3>();
        for (int j = 0; j < true_size; ++j)
            for (int i = 0; i < n; ++i)
                s[j][0][i] = 0.7 + 0.01 * j + 0.003 * i;
    } else {
        auto s = seed.accessor<double, 3>();
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < true_size; ++i)
                s[j][0][i] = 0.7 + 0.01 * j + 0.003 * i;
    }
    const auto ad = torch::autograd::grad({(output_core * seed).sum()}, {core})[0];
    const double ad_dot = (ad * direction).sum().item<double>();

    const auto plus_core = core.detach() + fd_step * direction;
    const auto minus_core = core.detach() - fd_step * direction;
    const auto plus = apply(y_direction ? packed_y_field_from_core(plus_core)
                                        : packed_x_field_from_core(plus_core));
    const auto minus = apply(y_direction ? packed_y_field_from_core(minus_core)
                                         : packed_x_field_from_core(minus_core));
    const auto fd_dot = (((y_direction ? plus.slice(0, 0, true_size)
                                      : plus.slice(2, 0, true_size)) * seed).sum() -
                         ((y_direction ? minus.slice(0, 0, true_size)
                                       : minus.slice(2, 0, true_size)) * seed).sum())
                            .item<double>() / (2.0 * fd_step);
    const double scale = std::max({1.0, std::abs(ad_dot), std::abs(fd_dot)});
    TORCH_CHECK(std::abs(ad_dot - fd_dot) < 2.0e-7 * scale,
                "packed ", y_direction ? "Y" : "X",
                " AD/FD mismatch: AD=", ad_dot, " FD=", fd_dot);
}

void check_zero_velocity_ad_fd() {
    constexpr int n = 13;
    constexpr int nz = 1;
    constexpr int order = 5;
    constexpr double fd_step = 1.0e-6;
    const int m = n - 1;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.advection_order = order;
    // At exactly zero transport the hard sign has a kink.  This focused
    // directional test uses the production smooth-sign option so the
    // velocity derivative has a well-defined centered-FD comparison.
    cfg.sign_smooth_delta = 0.1f;
    cfg.enable_ad_halo_exchange = false;

    TileSDIRK3UnifiedSolver tile(n, n, nz, 1.0f, 1.0f,
                                 {1.0f}, {1.0f}, {1.0f}, 0);
    configure_packed_solver(tile, n);
    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto core = torch::zeros({m, nz, n}, opts);
    auto c = core.accessor<double, 3>();
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            c[j][0][i] = 0.2 + 0.04 * j + 0.003 * j * j + 0.01 * i;
    const auto f = packed_y_field_from_core(core);
    auto velocity = torch::zeros({n + 1, nz, n}, opts).requires_grad_(true);
    auto direction = torch::zeros_like(velocity);
    auto d = direction.accessor<double, 3>();
    for (int j = 1; j < m; ++j)
        for (int i = 0; i < n; ++i)
            d[j][0][i] = 0.7 + 0.01 * j + 0.003 * i;

    const auto apply = [&](const torch::Tensor& speed) {
        return (tile.*access(AdvectYTag{}))(f, speed, 1.0f, nullptr);
    };
    auto seed = torch::zeros({m, nz, n}, opts);
    auto seed_acc = seed.accessor<double, 3>();
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
            seed_acc[j][0][i] = 0.4 + 0.03 * j + 0.007 * i;
    const auto base = apply(velocity).slice(0, 0, m);
    const auto ad = torch::autograd::grad({(base * seed).sum()}, {velocity})[0];
    const double ad_dot = (ad * direction).sum().item<double>();

    const auto plus = apply(velocity.detach() + fd_step * direction).slice(0, 0, m);
    const auto minus = apply(velocity.detach() - fd_step * direction).slice(0, 0, m);
    const double fd_dot = ((plus * seed).sum() - (minus * seed).sum())
                              .item<double>() / (2.0 * fd_step);
    TORCH_CHECK(std::abs(ad_dot) > 1.0e-8,
                "zero-velocity Y directional derivative was erased");
    const double scale = std::max({1.0, std::abs(ad_dot), std::abs(fd_dot)});
    TORCH_CHECK(std::abs(ad_dot - fd_dot) < 2.0e-7 * scale,
                "zero-velocity Y AD/FD mismatch: AD=", ad_dot,
                " FD=", fd_dot);
}

void check_packed_scalar_contract() {
    for (const int order : {2, 3, 5})
        for (const double velocity_sign : {-1.0, 1.0}) {
            check_packed_x_oracle(order, velocity_sign);
            check_packed_y_oracle(order, velocity_sign);
    }
    check_packed_ad_fd(false);
    check_packed_ad_fd(true);
    check_zero_velocity_ad_fd();
}

}  // namespace

int main() {
    torch::set_num_threads(1);
    wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    check_advection_order();
    check_packed_scalar_contract();
    std::cout << "scalar advection contract: PASS\n";
}
