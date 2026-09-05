// Actual production horizontal-momentum kernel versus an independent scalar
// WRF-biased stencil. Validation-only: no solver or production state changes.
#include "wrf_sdirk3_horizontal_momentum.h"

#include <torch/torch.h>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {

constexpr int M = 5;
constexpr int N = 8;
constexpr int Z = 4;
constexpr double RDX = 0.01;
constexpr double RDY = 0.02;
constexpr double PI = 3.14159265358979323846;

int imod(int x, int n) {
    const int r = x % n;
    return r < 0 ? r + n : r;
}

int even_row(int j) {
    const int r = imod(j, 2 * M);
    return r < M ? r : 2 * M - 1 - r;
}

std::pair<int, int> odd_row(int j) {
    const int r = imod(j, 2 * M);
    return r <= M ? std::make_pair(r, 1) : std::make_pair(2 * M - r, -1);
}

double qsample(const torch::TensorAccessor<double, 3> &q, int j, int k, int i, bool odd) {
    if (odd) {
        const auto [row, sign] = odd_row(j);
        return sign * q[row][k][imod(i, N)];
    }
    return q[even_row(j)][k][imod(i, N)];
}

double flux(const std::function<double(int)> &q, double transport, int order) {
    if (order == 2)
        return transport * (q(-1) + q(0)) / 2.0;
    if (order == 3) {
        return transport > 0.0 ? transport * (-q(-2) + 5.0 * q(-1) + 2.0 * q(0)) / 6.0
                               : transport * (2.0 * q(-1) + 5.0 * q(0) - q(1)) / 6.0;
    }
    return transport > 0.0
               ? transport *
                     (2.0 * q(-3) - 13.0 * q(-2) + 47.0 * q(-1) + 27.0 * q(0) - 3.0 * q(1)) / 60.0
               : transport *
                     (-3.0 * q(-2) + 27.0 * q(-1) + 47.0 * q(0) - 13.0 * q(1) + 2.0 * q(2)) / 60.0;
}

struct Fields {
    torch::Tensor u, v, w, ru, rv, msfux, msfvy, msftx, fnm, fnp;
};

struct ScalarDirections {
    torch::Tensor ux, uy, vx, vy, wx, wy;
};

ScalarDirections scalar_oracle(const Fields &f, int order) {
    auto u = f.u.accessor<double, 3>();
    auto v = f.v.accessor<double, 3>();
    auto w = f.w.accessor<double, 3>();
    auto ru = f.ru.accessor<double, 3>();
    auto rv = f.rv.accessor<double, 3>();
    auto ux = f.msfux.accessor<double, 2>();
    auto vy = f.msfvy.accessor<double, 2>();
    auto mx = f.msftx.accessor<double, 2>();
    auto fnm = f.fnm.accessor<double, 1>();
    auto fnp = f.fnp.accessor<double, 1>();
    auto ux_out = torch::zeros_like(f.u), uy_out = torch::zeros_like(f.u);
    auto vx_out = torch::zeros_like(f.v), vy_out = torch::zeros_like(f.v);
    auto wx_out = torch::zeros_like(f.w), wy_out = torch::zeros_like(f.w);
    auto ux_a = ux_out.accessor<double, 3>(), uy_a = uy_out.accessor<double, 3>();
    auto vx_a = vx_out.accessor<double, 3>(), vy_a = vy_out.accessor<double, 3>();
    auto wx_a = wx_out.accessor<double, 3>(), wy_a = wy_out.accessor<double, 3>();

    auto ru_at = [&](int j, int k, int i) { return ru[j][k][imod(i, N)]; };
    auto rv_at = [&](int j, int k, int i) { return rv[j][k][imod(i, N)]; };
    auto u_at = [&](int j, int k, int i) { return u[j][k][imod(i, N)]; };
    auto v_at = [&](int j, int k, int i) { return v[j][k][imod(i, N)]; };
    auto w_at = [&](int j, int k, int i) { return w[j][k][imod(i, N)]; };

    for (int j = 0; j < M; ++j)
        for (int k = 0; k < Z; ++k) {
            for (int i = 0; i <= N; ++i) {
                const double tx = 0.5 * (ru_at(j, k, i) + ru_at(j, k, i - 1));
                const double fx = flux([&](int d) { return u_at(j, k, i + d); }, tx, order);
                const int jf = j + 1;
                const double ty = 0.5 * (rv_at(jf, k, i) + rv_at(jf, k, i - 1));
                const double fy = flux(
                    [&](int d) { return qsample(f.u.accessor<double, 3>(), jf + d, k, i, false); },
                    ty, order);
                const double dx = flux([&](int d) { return u_at(j, k, i + 1 + d); },
                                       0.5 * (ru_at(j, k, i + 1) + ru_at(j, k, i)), order) -
                                  fx;
                const double dy =
                    fy - flux(
                             [&](int d) {
                                 return qsample(f.u.accessor<double, 3>(), j + d, k, i, false);
                             },
                             0.5 * (rv_at(j, k, i) + rv_at(j, k, i - 1)), order);
                ux_a[j][k][i] = -ux[j][i] * RDX * dx;
                uy_a[j][k][i] = -ux[j][i] * RDY * dy;
            }
        }

    for (int j = 1; j < M; ++j)
        for (int k = 0; k < Z; ++k) {
            for (int i = 0; i < N; ++i) {
                const double tx = 0.5 * (ru_at(j, k, i) + ru_at(j - 1, k, i));
                const double fx = flux([&](int d) { return v_at(j, k, i + d); }, tx, order);
                const double tx1 = 0.5 * (ru_at(j, k, i + 1) + ru_at(j - 1, k, i + 1));
                const double dx =
                    flux([&](int d) { return v_at(j, k, i + 1 + d); }, tx1, order) - fx;
                auto fy_at = [&](int face, int ii) {
                    const double ty = 0.5 * (rv_at(face - 1, k, ii) + rv_at(face, k, ii));
                    return flux(
                        [&](int d) {
                            return qsample(f.v.accessor<double, 3>(), face + d, k, ii, true);
                        },
                        ty, order);
                };
                const double dy = fy_at(j + 1, i) - fy_at(j, i);
                vx_a[j][k][i] = -vy[j][i] * RDX * dx;
                vy_a[j][k][i] = -vy[j][i] * RDY * dy;
            }
        }

    auto at_w = [&](int j, int k, int i, bool y) {
        const int level = k == Z ? Z - 1 : k;
        const double a = y ? rv_at(j, level, i) : ru_at(j, level, i);
        const double b = y ? rv_at(j, level - 1, i) : ru_at(j, level - 1, i);
        return k == Z ? (2.0 - fnm[Z - 1]) * a - fnp[Z - 1] * b : fnm[k] * a + fnp[k] * b;
    };
    for (int j = 0; j < M; ++j)
        for (int k = 1; k <= Z; ++k)
            for (int i = 0; i < N; ++i) {
                const auto wx = [&](int face) {
                    const double t = at_w(j, k, face, false);
                    return flux([&](int d) { return w_at(j, k, face + d); }, t, order);
                };
                const auto wy = [&](int face) {
                    const double t = at_w(face, k, i, true);
                    return flux(
                        [&](int d) {
                            return qsample(f.w.accessor<double, 3>(), face + d, k, i, false);
                        },
                        t, order);
                };
                wx_a[j][k][i] = -mx[j][i] * RDX * (wx(i + 1) - wx(i));
                wy_a[j][k][i] = -mx[j][i] * RDY * (wy(j + 1) - wy(j));
            }
    return {ux_out, uy_out, vx_out, vy_out, wx_out, wy_out};
}

Fields make_fields(int variant) {
    auto opts = torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    auto u = torch::empty({M, Z, N + 1}, opts), v = torch::empty({M + 1, Z, N}, opts);
    auto w = torch::empty({M, Z + 1, N}, opts), ru = torch::empty_like(u),
         rv = torch::empty_like(v);
    auto ux = torch::empty({M, N + 1}, opts), vy = torch::empty({M + 1, N}, opts),
         mx = torch::empty({M, N}, opts);
    auto fnm = torch::tensor({.3, .4, .6, .8}, opts), fnp = 1.0 - fnm;
    auto ua = u.accessor<double, 3>(), va = v.accessor<double, 3>(), wa = w.accessor<double, 3>();
    auto uxa = ux.accessor<double, 2>(), vya = vy.accessor<double, 2>(),
         mxa = mx.accessor<double, 2>();
    auto mass = torch::empty({M, N}, opts);
    auto ma = mass.accessor<double, 2>();
    constexpr double c1v[Z] = {.7, .9, 1.1, 1.3};
    for (int j = 0; j < M; ++j)
        for (int k = 0; k < Z; ++k)
            for (int i = 0; i <= N; ++i) {
                const double base =
                    2.0 * std::sin(2.0 * PI * i / N) + .3 * std::cos(PI * (j + .5) / M) + .2 * k;
                const bool active = variant != 3;
                const double sign = variant == 1 ? -1.0 : 1.0;
                ua[j][k][i] = i == N ? ua[j][k][0] : base * sign * (active ? 1.0 : 0.0);
            }
    for (int j = 0; j < M; ++j)
        for (int i = 0; i < N; ++i)
            ma[j][i] =
                80000.0 + 1200.0 * std::sin(2.0 * PI * i / N) + 400.0 * std::cos(PI * (j + .5) / M);
    for (int j = 0; j <= M; ++j)
        for (int k = 0; k < Z; ++k)
            for (int i = 0; i < N; ++i) {
                const bool active = variant != 2;
                const double sign = variant == 1 ? -1.0 : 1.0;
                va[j][k][i] = (j == 0 || j == M ? 0.0
                                                : std::sin(PI * j / M) *
                                                      (1.3 * std::cos(2.0 * PI * i / N) - .1 * k)) *
                              sign * (active ? 1.0 : 0.0);
            }
    auto rua = ru.accessor<double, 3>(), rva = rv.accessor<double, 3>();
    for (int j = 0; j < M; ++j)
        for (int k = 0; k < Z; ++k)
            for (int i = 0; i <= N; ++i) {
                const int im = imod(i, N);
                const double mu = .5 * (ma[j][im] + ma[j][imod(i - 1, N)]);
                const double c1 = c1v[k];
                const double alpha = (c1 * mu + 100000.0 * (1.0 - c1)) /
                                     (.9 + .03 * std::cos(2.0 * PI * i / N) + .02 * j);
                TORCH_CHECK(alpha > 0.0, "nonpositive U coupled mass");
                rua[j][k][i] = alpha * ua[j][k][i];
            }
    for (int j = 0; j <= M; ++j)
        for (int k = 0; k < Z; ++k)
            for (int i = 0; i < N; ++i) {
                const int jm = std::max(0, j - 1), jp = std::min(M - 1, j);
                const double mu = (j == 0   ? ma[0][i]
                                   : j == M ? ma[M - 1][i]
                                            : .5 * (ma[jm][i] + ma[jp][i]));
                const double c1 = c1v[k];
                const double alpha = (c1 * mu + 100000.0 * (1.0 - c1)) /
                                     (.8 + .04 * std::sin(2.0 * PI * i / N) + .02 * j);
                TORCH_CHECK(alpha > 0.0, "nonpositive V coupled mass");
                rva[j][k][i] = alpha * va[j][k][i];
            }
    for (int j = 0; j < M; ++j)
        for (int k = 0; k <= Z; ++k)
            for (int i = 0; i < N; ++i)
                wa[j][k][i] = .02 * (k + 1) * (k + 1) * (k + 1) * std::sin(2.0 * PI * i / N) *
                              std::cos(PI * (j + .5) / M);
    for (int j = 0; j < M; ++j)
        for (int i = 0; i <= N; ++i)
            uxa[j][i] = 1.4 + .05 * std::sin(2.0 * PI * i / N) + .04 * j;
    for (int j = 0; j <= M; ++j)
        for (int i = 0; i < N; ++i)
            vya[j][i] = 1.2 + .03 * std::cos(2.0 * PI * i / N) + .03 * j;
    for (int j = 0; j < M; ++j)
        for (int i = 0; i < N; ++i)
            mxa[j][i] = 1.1 + .07 * std::sin(2.0 * PI * i / N) + .05 * j;
    return {u, v, w, ru, rv, ux, vy, mx, fnm, fnp};
}

} // namespace

int main() {
    torch::NoGradGuard no_grad;
    torch::set_num_threads(1);
    for (int variant = 0; variant < 4; ++variant)
        for (int order : {2, 3, 5}) {
            const auto f = make_fields(variant);
            TORCH_CHECK(torch::equal(f.ru.select(2, 0), f.ru.select(2, N)),
                        "canonical Ru seam must be exact");
            TORCH_CHECK(f.rv.select(0, 0).abs().max().item<double>() == 0.0 &&
                            f.rv.select(0, M).abs().max().item<double>() == 0.0,
                        "canonical Rv walls must be zero");
            const auto flux = [order](const std::array<torch::Tensor, 6> &q,
                                      const torch::Tensor &a) {
                if (order == 2)
                    return a * (q[2] + q[3]) / 2.0;
                if (order == 3)
                    return a * ((7.0 * (q[3] + q[2]) - (q[4] + q[1])) / 12.0 +
                                torch::sign(a) * ((q[4] - q[1]) - 3.0 * (q[3] - q[2])) / 12.0);
                return a *
                       ((37.0 * (q[3] + q[2]) - 8.0 * (q[4] + q[1]) + q[5] + q[0]) / 60.0 -
                        torch::sign(a) *
                            ((q[5] - q[0]) - 5.0 * (q[4] - q[1]) + 10.0 * (q[3] - q[2])) / 60.0);
            };
            const auto got = wrf::sdirk3::wrf_horizontal_momentum(
                f.u, f.v, f.w, f.ru, f.rv, f.msfux, f.msfvy, f.msftx, f.fnm, f.fnp, RDX, RDY, flux);
            const auto expected = scalar_oracle(f, order);
            const auto actual =
                std::array<torch::Tensor, 6>{got.ux, got.uy, got.vx, got.vy, got.wx, got.wy};
            const auto reference = std::array<torch::Tensor, 6>{
                expected.ux, expected.uy, expected.vx, expected.vy, expected.wx, expected.wy};
            for (int c = 0; c < 6; ++c) {
                const double err = (actual[c] - reference[c]).abs().max().item<double>();
                const double scale = reference[c].abs().max().item<double>();
                TORCH_CHECK(torch::isfinite(actual[c]).all().item<bool>() && std::isfinite(scale),
                            "nonfinite direction: ", variant, "/", order, "/", c);
                TORCH_CHECK(std::isfinite(err) && err < 1e-10,
                            "variant/order/channel mismatch: ", variant, "/", order, "/", c,
                            " error=", err);
            }
            TORCH_CHECK(
                actual[4].abs().max().item<double>() + actual[5].abs().max().item<double>() > 1.0,
                "native W transport must be nontrivial");
            TORCH_CHECK(f.v.select(0, 0).abs().sum().item<double>() == 0.0 &&
                            f.v.select(0, M).abs().sum().item<double>() == 0.0,
                        "V wall rows must be zero");
            TORCH_CHECK((f.u.select(2, 0) - f.u.select(2, N)).abs().max().item<double>() == 0.0,
                        "U periodic seam must be exact");
            TORCH_CHECK(
                (actual[0].abs().max().item<double>() + actual[1].abs().max().item<double>() >
                 1e-8) == (variant != 3) &&
                    (actual[2].abs().max().item<double>() + actual[3].abs().max().item<double>() >
                     1e-8) == (variant != 2),
                "directional zero/nonzero control mismatch");
        }
    std::cout << "horizontal momentum contract: PASS 12 production/scalar cases, six directions\n";
}
