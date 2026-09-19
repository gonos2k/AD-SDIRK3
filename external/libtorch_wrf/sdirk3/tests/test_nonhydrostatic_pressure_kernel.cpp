#include "wrf_sdirk3_nonhydrostatic_pressure.h"

#include <torch/torch.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace wrf::sdirk3::nonhydrostatic_pressure;

namespace {

struct Fields {
    torch::Tensor p, php, msfux, msfuy, msfvx, msfvy, mu_pert, c1h, rdnw, fnm, fnp;
    int64_t ny, nz, nx;
};

int mod_index(int value, int period) {
    const int r = value % period;
    return r < 0 ? r + period : r;
}

int reflect_y(int value, int ny) {
    if (ny == 1) return 0;
    const int folded = mod_index(value, 2 * ny);
    return folded < ny ? folded : 2 * ny - 1 - folded;
}

torch::Tensor pattern(const std::vector<int64_t>& shape, double offset, double scale,
                      double frequency) {
    int64_t size = 1;
    for (const auto extent : shape) size *= extent;
    const auto options = torch::TensorOptions().dtype(torch::kFloat64);
    const auto x = torch::arange(size, options);
    return offset + scale * torch::sin(frequency * x + 0.17).reshape(shape);
}

Fields fixture(bool top_lid, int64_t ny = 4, int64_t nz = 4, int64_t nx = 5) {
    const auto options = torch::TensorOptions().dtype(torch::kFloat64);
    Fields f{
        pattern({ny, nz, nx}, 4.0, 1.4, 0.27),
        pattern({ny, nz, nx}, 9.0, 2.1, 0.19),
        pattern({ny, nx + 1}, 1.55, 0.22, 0.31),
        pattern({ny, nx + 1}, 1.15, 0.13, 0.23),
        pattern({ny + 1, nx}, 1.35, 0.19, 0.29),
        pattern({ny + 1, nx}, 1.72, 0.16, 0.37),
        pattern({ny, nx}, 7.5, 1.3, 0.21), // perturbation mass mu_2
        torch::tensor({0.73, 1.18, 0.91, 1.37}, options),
        torch::tensor({-1.45, -2.20, -0.83, -1.76}, options),
        torch::tensor({0.31, 0.68, 0.22}, options),
        torch::tensor({0.69, 0.32, 0.78}, options),
        ny, nz, nx};
    // Native staggered map inputs carry the same physical boundary aliases as
    // WRF set_physical_bc2d: periodic U faces in X and reflected V faces in Y.
    f.msfux.slice(1, nx, nx + 1).copy_(f.msfux.slice(1, 0, 1));
    f.msfuy.slice(1, nx, nx + 1).copy_(f.msfuy.slice(1, 0, 1));
    f.msfvx.slice(0, ny, ny + 1).copy_(f.msfvx.slice(0, ny - 1, ny));
    f.msfvy.slice(0, ny, ny + 1).copy_(f.msfvy.slice(0, ny - 1, ny));
    (void)top_lid;
    return f;
}

double p_at(const torch::TensorAccessor<double, 3>& p, int j, int k, int i) {
    return p[j][k][mod_index(i, static_cast<int>(p.size(2)))];
}

struct Oracle {
    torch::Tensor dpn_u, dpn_v, term4_u, term4_v;
};

Oracle scalar_oracle(const Fields& f, bool top_lid,
                    double cf1, double cf2, double cf3,
                    double cfn, double cfn1, double rdx, double rdy) {
    const int ny = static_cast<int>(f.ny);
    const int nz = static_cast<int>(f.nz);
    const int nx = static_cast<int>(f.nx);
    const auto options = f.p.options();
    auto dpn_u = torch::zeros({ny, nz + 1, nx + 1}, options);
    auto dpn_v = torch::zeros({ny + 1, nz + 1, nx}, options);
    auto term4_u = torch::zeros({ny, nz, nx + 1}, options);
    auto term4_v = torch::zeros({ny + 1, nz, nx}, options);

    const auto p_tensor = f.p.contiguous();
    const auto php_tensor = f.php.contiguous();
    const auto msfux_tensor = f.msfux.contiguous();
    const auto msfuy_tensor = f.msfuy.contiguous();
    const auto msfvx_tensor = f.msfvx.contiguous();
    const auto msfvy_tensor = f.msfvy.contiguous();
    const auto mu_tensor = f.mu_pert.contiguous();
    const auto c1h_tensor = f.c1h.contiguous();
    const auto rdnw_tensor = f.rdnw.contiguous();
    const auto fnm_tensor = f.fnm.contiguous();
    const auto fnp_tensor = f.fnp.contiguous();
    const auto p = p_tensor.accessor<double, 3>();
    const auto php = php_tensor.accessor<double, 3>();
    const auto msfux = msfux_tensor.accessor<double, 2>();
    const auto msfuy = msfuy_tensor.accessor<double, 2>();
    const auto msfvx = msfvx_tensor.accessor<double, 2>();
    const auto msfvy = msfvy_tensor.accessor<double, 2>();
    const auto mu_pert = mu_tensor.accessor<double, 2>();
    const auto c1h = c1h_tensor.accessor<double, 1>();
    const auto rdnw = rdnw_tensor.accessor<double, 1>();
    const auto fnm = fnm_tensor.accessor<double, 1>();
    const auto fnp = fnp_tensor.accessor<double, 1>();
    auto du = dpn_u.accessor<double, 3>();
    auto dv = dpn_v.accessor<double, 3>();
    auto xu = term4_u.accessor<double, 3>();
    auto yv = term4_v.accessor<double, 3>();

    auto p_u = [&](int j, int k, int i) {
        return 0.5 * (p_at(p, j, k, i - 1) + p_at(p, j, k, i));
    };
    auto p_v = [&](int j, int k, int i) {
        const int south = reflect_y(j - 1, ny);
        const int north = reflect_y(j, ny);
        return 0.5 * (p_at(p, south, k, i) + p_at(p, north, k, i));
    };
    auto mu_u = [&](int j, int i) {
        return 0.5 * (mu_pert[j][mod_index(i - 1, nx)] + mu_pert[j][mod_index(i, nx)]);
    };
    auto mu_v = [&](int j, int i) {
        return 0.5 * (mu_pert[reflect_y(j - 1, ny)][i] + mu_pert[reflect_y(j, ny)][i]);
    };

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i <= nx; ++i) {
            du[j][0][i] = cf1 * p_u(j, 0, i) + cf2 * p_u(j, 1, i)
                        + cf3 * p_u(j, 2, i);
            du[j][nz][i] = top_lid
                ? cfn * p_u(j, nz - 1, i) + cfn1 * p_u(j, nz - 2, i)
                : 0.0;
            for (int k = 1; k < nz; ++k)
                du[j][k][i] = fnm[k - 1] * p_u(j, k, i)
                            + fnp[k - 1] * p_u(j, k - 1, i);
        }
    }
    for (int j = 0; j <= ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            dv[j][0][i] = cf1 * p_v(j, 0, i) + cf2 * p_v(j, 1, i)
                        + cf3 * p_v(j, 2, i);
            dv[j][nz][i] = top_lid
                ? cfn * p_v(j, nz - 1, i) + cfn1 * p_v(j, nz - 2, i)
                : 0.0;
            for (int k = 1; k < nz; ++k)
                dv[j][k][i] = fnm[k - 1] * p_v(j, k, i)
                            + fnp[k - 1] * p_v(j, k - 1, i);
        }
    }

    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i <= nx; ++i) {
                const double dphi = php[j][k][mod_index(i, nx)]
                                  - php[j][k][mod_index(i - 1, nx)];
                const double vertical = rdnw[k] * (du[j][k + 1][i] - du[j][k][i])
                                      - c1h[k] * mu_u(j, i);
                xu[j][k][i] = (msfux[j][i] / msfuy[j][i]) * rdx * dphi * vertical;
            }
        }
    }
    for (int j = 0; j <= ny; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                const int south = reflect_y(j - 1, ny);
                const int north = reflect_y(j, ny);
                const double dphi = php[north][k][i] - php[south][k][i];
                const double vertical = rdnw[k] * (dv[j][k + 1][i] - dv[j][k][i])
                                      - c1h[k] * mu_v(j, i);
                yv[j][k][i] = (msfvy[j][i] / msfvx[j][i]) * rdy * dphi * vertical;
            }
        }
    }
    return {dpn_u, dpn_v, term4_u, term4_v};
}

double max_abs(const torch::Tensor& value) {
    return value.abs().max().item<double>();
}

bool close(const torch::Tensor& got, const torch::Tensor& expected, double tolerance,
           const std::string& label) {
    const double error = max_abs(got - expected);
    const bool ok = std::isfinite(error) && error <= tolerance;
    std::cout << label << " max_abs=" << error << " " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool run_scalar_matrix() {
    bool ok = true;
    constexpr double cf1 = 0.57;
    constexpr double cf2 = -0.23;
    constexpr double cf3 = 0.66;
    constexpr double cfn = 1.41;
    constexpr double cfn1 = -0.38;
    constexpr double rdx = 0.013;
    constexpr double rdy = 0.021;
    for (const bool top_lid : {false, true}) {
        const auto f = fixture(top_lid);
        const auto result = native_nonhydrostatic_pressure_core(
            f.p, f.php, f.msfux, f.msfuy, f.msfvx, f.msfvy, f.mu_pert, f.c1h, f.rdnw,
            rdx, rdy, cf1, cf2, cf3, f.fnm, f.fnp, top_lid, cfn, cfn1);
        const auto expected = scalar_oracle(f, top_lid, cf1, cf2, cf3, cfn, cfn1, rdx, rdy);
        ok &= close(result.dpn_u, expected.dpn_u, 2.0e-12,
                    std::string("scalar dpn_u top_lid=") + (top_lid ? "1" : "0"));
        ok &= close(result.dpn_v, expected.dpn_v, 2.0e-12,
                    std::string("scalar dpn_v top_lid=") + (top_lid ? "1" : "0"));
        ok &= close(result.term4_u, expected.term4_u, 2.0e-12,
                    std::string("scalar term4_u top_lid=") + (top_lid ? "1" : "0"));
        ok &= close(result.term4_v, expected.term4_v, 2.0e-12,
                    std::string("scalar term4_v top_lid=") + (top_lid ? "1" : "0"));
        ok &= close(result.tendency_u, -result.term4_u, 0.0,
                    std::string("outer-sign u top_lid=") + (top_lid ? "1" : "0"));
        ok &= close(result.tendency_v, -result.term4_v, 0.0,
                    std::string("outer-sign v top_lid=") + (top_lid ? "1" : "0"));

        const double u_seam = max_abs(result.dpn_u.slice(2, 0, 1) - result.dpn_u.slice(2, f.nx, f.nx + 1));
        const double term_u_seam = max_abs(result.term4_u.slice(2, 0, 1)
                                           - result.term4_u.slice(2, f.nx, f.nx + 1));
        const double v_south = max_abs(result.dpn_v.select(0, 0));
        const double v_north = max_abs(result.dpn_v.select(0, f.ny));
        const double phi_south = max_abs(result.term4_v.slice(0, 0, 1));
        const double phi_north = max_abs(result.term4_v.slice(0, f.ny, f.ny + 1));
        std::cout << "boundaries top_lid=" << top_lid
                  << " u_seam=" << u_seam
                  << " term4_u_seam=" << term_u_seam
                  << " v_south_dpn_signal=" << v_south
                  << " v_north_dpn_signal=" << v_north
                  << " v_south_term4=" << phi_south
                  << " v_north_term4=" << phi_north << "\n";
        ok &= u_seam == 0.0 && term_u_seam == 0.0
           && v_south > 1.0e-8 && v_north > 1.0e-8
           && phi_south == 0.0 && phi_north == 0.0;

        const double term_u_signal = max_abs(result.term4_u);
        const double term_v_signal = max_abs(result.term4_v);
        std::cout << "witness top_lid=" << top_lid
                  << " max_term4_u=" << term_u_signal
                  << " max_term4_v=" << term_v_signal << "\n";
        ok &= term_u_signal > 1.0e-8 && term_v_signal > 1.0e-8;
        if (top_lid) {
            const auto no_lid = native_nonhydrostatic_pressure_core(
                f.p, f.php, f.msfux, f.msfuy, f.msfvx, f.msfvy, f.mu_pert, f.c1h, f.rdnw,
                rdx, rdy, cf1, cf2, cf3, f.fnm, f.fnp, false, cfn, cfn1);
            const double top_difference = max_abs(result.dpn_u.select(1, f.nz)
                                                 - no_lid.dpn_u.select(1, f.nz));
            std::cout << "top_lid witness dpn_u_top_difference=" << top_difference
                      << "\n";
            ok &= top_difference > 1.0e-8;
        }
    }
    std::cout << "scalar_matrix " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool run_ad_fd() {
    constexpr double cf1 = 0.57;
    constexpr double cf2 = -0.23;
    constexpr double cf3 = 0.66;
    constexpr double cfn = 1.41;
    constexpr double cfn1 = -0.38;
    constexpr double rdx = 0.013;
    constexpr double rdy = 0.021;
    const auto base = fixture(true, 4, 4, 5);
    auto p = base.p.clone().set_requires_grad(true);
    auto php = base.php.clone().set_requires_grad(true);
    const auto direction_p = torch::cos(torch::arange(p.numel(), p.options()).reshape_as(p) * 0.37);
    const auto direction_php = torch::sin(torch::arange(php.numel(), php.options()).reshape_as(php) * 0.29);
    const auto weight_u = torch::sin(torch::arange((base.ny * base.nz * (base.nx + 1)), p.options())
                                         .reshape({base.ny, base.nz, base.nx + 1}) * 0.13);
    const auto weight_v = torch::cos(torch::arange(((base.ny + 1) * base.nz * base.nx), p.options())
                                         .reshape({base.ny + 1, base.nz, base.nx}) * 0.11);
    const auto result = native_nonhydrostatic_pressure_core(
        p, php, base.msfux, base.msfuy, base.msfvx, base.msfvy, base.mu_pert, base.c1h, base.rdnw,
        rdx, rdy, cf1, cf2, cf3, base.fnm, base.fnp, true, cfn, cfn1);
    const auto loss = (result.term4_u * weight_u).sum()
                    + (result.term4_v * weight_v).sum()
                    + 0.17 * result.dpn_u.sum() - 0.11 * result.dpn_v.sum();
    loss.backward();
    const double analytic = (p.grad() * direction_p).sum().item<double>()
                          + (php.grad() * direction_php).sum().item<double>();
    constexpr double eps = 1.0e-6;
    torch::NoGradGuard no_grad;
    const auto plus = native_nonhydrostatic_pressure_core(
        p + eps * direction_p, php + eps * direction_php,
        base.msfux, base.msfuy, base.msfvx, base.msfvy, base.mu_pert, base.c1h, base.rdnw,
        rdx, rdy, cf1, cf2, cf3, base.fnm, base.fnp, true, cfn, cfn1);
    const auto minus = native_nonhydrostatic_pressure_core(
        p - eps * direction_p, php - eps * direction_php,
        base.msfux, base.msfuy, base.msfvx, base.msfvy, base.mu_pert, base.c1h, base.rdnw,
        rdx, rdy, cf1, cf2, cf3, base.fnm, base.fnp, true, cfn, cfn1);
    const auto loss_of = [&](const Result& r) {
        return (r.term4_u * weight_u).sum() + (r.term4_v * weight_v).sum()
             + 0.17 * r.dpn_u.sum() - 0.11 * r.dpn_v.sum();
    };
    const double fd = ((loss_of(plus) - loss_of(minus)) / (2.0 * eps)).item<double>();
    const double relative = std::abs(analytic - fd) / (std::abs(fd) + 1.0e-30);
    const bool ok = p.grad().defined() && php.grad().defined()
                 && p.grad().isfinite().all().item<bool>()
                 && php.grad().isfinite().all().item<bool>()
                 && std::isfinite(analytic) && std::isfinite(fd)
                 && std::abs(fd) > 1.0e-8 && relative < 1.0e-8;
    std::cout << "AD/FD analytic=" << analytic << " fd=" << fd
              << " relative=" << relative << " " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

}  // namespace

int main() {
    const bool ok = run_scalar_matrix() && run_ad_fd();
    std::cout << "nonhydrostatic_pressure_kernel " << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
