#include "../wrf_sdirk3_coriolis.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using torch::indexing::Slice;
using wrf::sdirk3::CoriolisTendencies;

struct Inputs {
    torch::Tensor u, v, w, au, av, aw;
    torch::Tensor msfux, msfuy, msfvx, msfvy, msftx, msfty;
    torch::Tensor f, e, sina, cosa, fzm, fzp;
};

torch::Tensor make3(int64_t a, int64_t b, int64_t c, double bias,
                    double da, double db, double dc) {
    auto x = torch::empty({a, b, c}, torch::TensorOptions().dtype(torch::kFloat64));
    auto q = x.accessor<double, 3>();
    for (int64_t i = 0; i < a; ++i)
        for (int64_t k = 0; k < b; ++k)
            for (int64_t j = 0; j < c; ++j)
                q[i][k][j] = bias + da * i + db * k + dc * j +
                             0.003 * i * j - 0.002 * k * j;
    return x;
}

torch::Tensor make2(int64_t a, int64_t b, double bias, double da, double db) {
    auto x = torch::empty({a, b}, torch::TensorOptions().dtype(torch::kFloat64));
    auto q = x.accessor<double, 2>();
    for (int64_t i = 0; i < a; ++i)
        for (int64_t j = 0; j < b; ++j)
            q[i][j] = bias + da * i + db * j + 0.004 * i * j;
    return x;
}

Inputs make_inputs() {
    constexpr int64_t m = 5, z = 4, n = 7;
    Inputs x{
        make3(m, z, n + 1, 0.70, 0.11, 0.07, 0.023),
        make3(m + 1, z, n, -0.25, 0.09, 0.031, 0.041),
        make3(m, z + 1, n, 0.18, 0.05, 0.043, 0.017),
        make3(m, z, n + 1, 1.20, 0.013, 0.021, 0.017),
        make3(m + 1, z, n, 0.95, 0.011, 0.019, 0.015),
        make3(m, z + 1, n, 1.08, 0.009, 0.016, 0.013),
        make2(m, n + 1, 1.10, 0.006, 0.021),
        make2(m, n + 1, 1.04, 0.004, 0.014),
        make2(m + 1, n, 1.16, 0.008, 0.017),
        make2(m + 1, n, 1.02, 0.005, 0.012),
        make2(m, n, 0.98, 0.007, 0.011),
        make2(m, n, 1.06, 0.003, 0.009),
        make2(m, n, 0.17, 0.013, 0.019),
        make2(m, n, 0.23, 0.010, 0.015),
        make2(m, n, 0.31, 0.008, 0.011),
        make2(m, n, 0.87, 0.004, 0.007),
        torch::tensor({0.26, 0.31, 0.38, 0.47}, torch::kFloat64),
        torch::tensor({0.74, 0.69, 0.62, 0.53}, torch::kFloat64),
    };
    return x;
}

struct Oracle {
    torch::Tensor u, v, w;
};

Oracle scalar_oracle(const Inputs& x) {
    const int64_t m = x.u.size(0), z = x.u.size(1), n = x.u.size(2) - 1;
    auto opts = x.u.options();
    auto cu = torch::zeros({m, z, n + 1}, opts);
    auto cv = torch::zeros({m + 1, z, n}, opts);
    auto cw = torch::zeros({m, z + 1, n}, opts);

    const auto u = x.u.accessor<double, 3>();
    const auto v = x.v.accessor<double, 3>();
    const auto w = x.w.accessor<double, 3>();
    const auto au = x.au.accessor<double, 3>();
    const auto av = x.av.accessor<double, 3>();
    const auto aw = x.aw.accessor<double, 3>();
    const auto msfux = x.msfux.accessor<double, 2>();
    const auto msfuy = x.msfuy.accessor<double, 2>();
    const auto msfvx = x.msfvx.accessor<double, 2>();
    const auto msfvy = x.msfvy.accessor<double, 2>();
    const auto msftx = x.msftx.accessor<double, 2>();
    const auto msfty = x.msfty.accessor<double, 2>();
    const auto f = x.f.accessor<double, 2>();
    const auto e = x.e.accessor<double, 2>();
    const auto sina = x.sina.accessor<double, 2>();
    const auto cosa = x.cosa.accessor<double, 2>();
    const auto fzm = x.fzm.accessor<double, 1>();
    const auto fzp = x.fzp.accessor<double, 1>();
    auto out_u = cu.accessor<double, 3>();
    auto out_v = cv.accessor<double, 3>();
    auto out_w = cw.accessor<double, 3>();

    auto ru = [&](int64_t j, int64_t k, int64_t i) {
        return au[j][k][i] * u[j][k][i];
    };
    auto rv = [&](int64_t j, int64_t k, int64_t i) {
        return av[j][k][i] * v[j][k][i];
    };
    auto rw = [&](int64_t j, int64_t k, int64_t i) {
        return aw[j][k][i] * w[j][k][i];
    };

    for (int64_t j = 0; j < m; ++j) {
        for (int64_t k = 0; k < z; ++k) {
            for (int64_t i = 0; i < n; ++i) {
                const int64_t west = (i + n - 1) % n;
                const double rv_u = 0.25 *
                    (rv(j, k, west) + rv(j, k, i) + rv(j + 1, k, west) +
                     rv(j + 1, k, i));
                const double rw_u = 0.25 *
                    (rw(j, k, west) + rw(j, k, i) + rw(j, k + 1, west) +
                     rw(j, k + 1, i));
                const double fu = 0.5 * (f[j][west] + f[j][i]);
                const double eu = 0.5 * (e[j][west] + e[j][i]);
                const double cosa_u = 0.5 * (cosa[j][west] + cosa[j][i]);
                out_u[j][k][i] = (msfux[j][i] / msfuy[j][i]) * fu * rv_u -
                                 eu * cosa_u * rw_u;
            }
            out_u[j][k][n] = out_u[j][k][0];
        }
    }

    for (int64_t j = 1; j < m; ++j) {
        for (int64_t k = 0; k < z; ++k) {
            for (int64_t i = 0; i < n; ++i) {
                const double ru_v = 0.25 *
                    (ru(j, k, i) + ru(j, k, i + 1) + ru(j - 1, k, i) +
                     ru(j - 1, k, i + 1));
                const double rw_v = 0.25 *
                    (rw(j - 1, k, i) + rw(j - 1, k + 1, i) + rw(j, k, i) +
                     rw(j, k + 1, i));
                const double fv = 0.5 * (f[j - 1][i] + f[j][i]);
                const double ev = 0.5 * (e[j - 1][i] + e[j][i]);
                const double sina_v = 0.5 * (sina[j - 1][i] + sina[j][i]);
                out_v[j][k][i] = -(msfvy[j][i] / msfvx[j][i]) * fv * ru_v +
                                 (msfvy[j][i] / msfvx[j][i]) * ev * sina_v * rw_v;
            }
        }
    }

    for (int64_t j = 0; j < m; ++j) {
        for (int64_t k = 1; k < z; ++k) {
            for (int64_t i = 0; i < n; ++i) {
                const double ru_w = fzm[k] * 0.5 *
                        (ru(j, k, i) + ru(j, k, i + 1)) +
                    fzp[k] * 0.5 * (ru(j, k - 1, i) + ru(j, k - 1, i + 1));
                const double rv_w = fzm[k] * 0.5 *
                        (rv(j, k, i) + rv(j + 1, k, i)) +
                    fzp[k] * 0.5 * (rv(j, k - 1, i) + rv(j + 1, k - 1, i));
                out_w[j][k][i] = e[j][i] *
                    (cosa[j][i] * ru_w - (msftx[j][i] / msfty[j][i]) *
                     sina[j][i] * rv_w);
            }
        }
    }
    return {cu, cv, cw};
}

double max_abs(const torch::Tensor& x) { return x.abs().max().item<double>(); }

void require_close(const char* name, const torch::Tensor& got,
                   const torch::Tensor& expected) {
    const double err = max_abs(got - expected);
    if (err > 2.0e-12)
        throw std::runtime_error(std::string(name) + " max error=" +
                                 std::to_string(err));
    std::cout << name << " max_abs=" << err << "\n";
}

}  // namespace


void check_coriolis_coefficients() {
    const auto x = make_inputs();
    const CoriolisTendencies got = wrf::sdirk3::wrf_coriolis_tendencies(
        x.u, x.v, x.w, x.au, x.av, x.aw, x.msfux, x.msfuy, x.msfvx,
        x.msfvy, x.msftx, x.msfty, x.f, x.e, x.sina, x.cosa, x.fzm, x.fzp);
    const auto expected = scalar_oracle(x);
    require_close("U periodic wrapped canonical force", got.u, expected.u);
    require_close("V interior canonical force", got.v, expected.v);
    require_close("W fnm/fnp canonical force", got.w, expected.w);

    const int64_t m = x.u.size(0), z = x.u.size(1), n = x.u.size(2) - 1;
    if (max_abs(got.u.slice(2, n, n + 1) - got.u.slice(2, 0, 1)) > 0.0)
        throw std::runtime_error("U periodic endpoint is not an alias");
    if (max_abs(got.v.index({0}) ) > 0.0 || max_abs(got.v.index({m})) > 0.0)
        throw std::runtime_error("V wall force was not projected out");
    if (max_abs(got.w.index({Slice(), 0}) ) > 0.0 ||
        max_abs(got.w.index({Slice(), z})) > 0.0)
        throw std::runtime_error("W lid force was not projected out");

    // This is the ordinary RHS conversion contract: a raw coupled force C
    // becomes a velocity force C/alpha. Check nonunit, nonconstant alpha.
    const auto physical_u = got.u / x.au;
    const auto physical_v = got.v / x.av;
    const auto physical_w = got.w / x.aw;
    if (!torch::isfinite(physical_u).all().item<bool>() ||
        !torch::isfinite(physical_v).all().item<bool>() ||
        !torch::isfinite(physical_w).all().item<bool>())
        throw std::runtime_error("alpha conversion produced nonfinite force");
    std::cout << "physical_conversion_u_max=" << max_abs(physical_u)
              << " physical_conversion_v_max=" << max_abs(physical_v)
              << " physical_conversion_w_max=" << max_abs(physical_w) << "\n";
    std::cout << "production Coriolis coefficient oracle PASS m=" << m
              << " z=" << z << " n=" << n << "\n";
}
