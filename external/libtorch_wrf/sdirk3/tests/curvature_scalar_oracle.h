#pragma once

#include <torch/torch.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Independent scalar transcription of module_big_step_utilities_em.F's
// curvature operator in canonical (alpha*u, alpha*v, alpha*w) units.
namespace curvature_oracle {

struct Tendencies {
    torch::Tensor u, v, w; // coupled tendencies, same shapes as u/v/w
};

inline int wrap_or_clamp(int i, int n, bool periodic_x) {
    if (n <= 1) return 0;
    if (periodic_x) {
        i %= n;
        return i < 0 ? i + n : i;
    }
    return std::max(0, std::min(i, n - 1));
}

inline int row_or_clamp(int j, int n) {
    return n <= 1 ? 0 : std::max(0, std::min(j, n - 1));
}

inline torch::Tensor at2(const torch::Tensor& a, int j, int i, int ny, int nx,
                         bool periodic_x) {
    const int jj = row_or_clamp(j, std::min<int>(ny, a.size(0)));
    const int ii = wrap_or_clamp(i, std::min<int>(nx, a.size(1)), periodic_x);
    return a.index({jj, ii});
}

inline torch::Tensor at3(const torch::Tensor& a, int j, int k, int i,
                         int ny, int nx, bool periodic_x) {
    const int jj = row_or_clamp(j, std::min<int>(ny, a.size(0)));
    const int ii = wrap_or_clamp(i, std::min<int>(nx, a.size(2)), periodic_x);
    const int kk = std::max(0, std::min(k, static_cast<int>(a.size(1)) - 1));
    return a.index({jj, kk, ii});
}

inline torch::Tensor at1(const torch::Tensor& a, int k, float fallback,
                         const torch::TensorOptions& options) {
    if (!a.defined() || a.numel() == 0 || k < 0 || k >= a.size(0))
        return torch::full({}, fallback, options);
    return a.index({k});
}

inline torch::Tensor xavg2(const torch::Tensor& a, int j, int i, int ny, int nx,
                           bool periodic_x) {
    return 0.5f * (at2(a, j, i - 1, ny, nx, periodic_x) +
                   at2(a, j, i, ny, nx, periodic_x));
}

inline torch::Tensor xavg3(const torch::Tensor& a, int j, int k, int i,
                           int ny, int nx, bool periodic_x) {
    return 0.5f * (at3(a, j, k, i - 1, ny, nx, periodic_x) +
                   at3(a, j, k, i, ny, nx, periodic_x));
}

// `u/v/w` are [j,k,i]. `mass_m`/`mass_n` are the physical rows/columns; a
// packed periodic tile may carry one symmetric-y and one periodic-x alias.
inline Tendencies curvature_canonical(
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& Ru, const torch::Tensor& Rv, const torch::Tensor& Rw,
    const torch::Tensor& msfux, const torch::Tensor& msfuy,
    const torch::Tensor& msfvx, const torch::Tensor& msfvy,
    const torch::Tensor& msftx, const torch::Tensor& msfty,
    const torch::Tensor& xlat, const torch::Tensor& fzm,
    const torch::Tensor& fzp, int mass_m, int mass_n, bool packed,
    bool periodic_x, bool symmetric_y, int map_proj, bool polar,
    float rdx, float rdy, float reradius) {
    const auto opt = u.options();
    const int ny_u = static_cast<int>(u.size(0));
    const int nz = static_cast<int>(u.size(1));
    const int nx_u = static_cast<int>(u.size(2));
    const int ny_v = static_cast<int>(v.size(0));
    const int nz_w = static_cast<int>(w.size(1));
    const int nx = static_cast<int>(w.size(2));
    const int m = std::max(1, std::min(mass_m, ny_u));
    const int n = std::max(1, std::min(mass_n, nx));

    auto u_t = torch::zeros_like(u);
    auto v_t = torch::zeros_like(v);
    auto w_t = torch::zeros_like(w);

    // vxgm(i,k,j) = ubar*d(msfvx)/dy - vbar*d(msfuy)/dx.
    auto vxgm = torch::zeros({m, nz, n}, opt);
    for (int j = 0; j < m; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < n; ++i) {
                const auto ua = 0.5f * (at3(u, j, k, i, m, n, periodic_x) +
                                        at3(u, j, k, i + 1, m, n, periodic_x));
                const auto va = 0.5f * (at3(v, j, k, i, ny_v, n, periodic_x) +
                                        at3(v, j + 1, k, i, ny_v, n, periodic_x));
                const auto dmx = at2(msfvx, j + 1, i, ny_v, n, periodic_x) -
                                 at2(msfvx, j, i, ny_v, n, periodic_x);
                const auto dmy = at2(msfuy, j, i + 1, m, n, periodic_x) -
                                 at2(msfuy, j, i, m, n, periodic_x);
                vxgm.index_put_({j, k, i}, ua * dmx * rdy - va * dmy * rdx);
            }
        }
    }

    const bool tan_branch = map_proj == 6 || polar;
    auto tan_u = [&](int j, int i) {
        const auto lat = 0.5f * (at2(xlat, j, i - 1, m, n, periodic_x) +
                                 at2(xlat, j, i, m, n, periodic_x));
        return torch::tan(lat);
    };
    auto tan_v = [&](int j, int i) {
        const auto lat = 0.5f * (at2(xlat, j - 1, i, m, n, periodic_x) +
                                 at2(xlat, j, i, m, n, periodic_x));
        return torch::tan(lat);
    };

    // u equation, including both map-projection branches.
    for (int j = 0; j < ny_u; ++j) {
        const int jp = packed ? std::min(j, m - 1) : j;
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nx_u; ++i) {
                const int ip = packed ? (i % n) : i;
                const auto rw_u = 0.25f * (
                    at3(Rw, jp, k, ip - 1, m, n, periodic_x) +
                    at3(Rw, jp, k, ip,     m, n, periodic_x) +
                    at3(Rw, jp, k + 1, ip - 1, m, n, periodic_x) +
                    at3(Rw, jp, k + 1, ip,     m, n, periodic_x));
                const auto rv_u = 0.25f * (
                    at3(Rv, jp, k, ip - 1, ny_v, n, periodic_x) +
                    at3(Rv, jp, k, ip,     ny_v, n, periodic_x) +
                    at3(Rv, jp + 1, k, ip - 1, ny_v, n, periodic_x) +
                    at3(Rv, jp + 1, k, ip,     ny_v, n, periodic_x));
                const auto uu = at3(u, jp, k, ip, m, n, periodic_x);
                torch::Tensor value;
                if (tan_branch) {
                    const auto ratio = at2(msfux, jp, ip, m, n, periodic_x) /
                                       at2(msfuy, jp, ip, m, n, periodic_x);
                    value = uu * reradius * (ratio * rv_u * tan_u(jp, ip) - rw_u);
                } else {
                    const auto vg = 0.5f * (at3(vxgm, jp, k, ip - 1, m, n, periodic_x) +
                                           at3(vxgm, jp, k, ip, m, n, periodic_x));
                    value = vg * rv_u - uu * reradius * rw_u;
                }
                u_t.index_put_({j, k, i}, value);
            }
        }
    }

    // v equation. Symmetric-y normal velocity rows are exactly zero.
    for (int j = 0; j < ny_v; ++j) {
        const bool y_boundary = symmetric_y && (j == 0 || j >= m);
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                if (y_boundary) {
                    v_t.index_put_({j, k, i}, torch::zeros({}, opt));
                    continue;
                }
                const int jp = std::min(j, m - 1);
                const auto ru_v = 0.25f * (
                    at3(Ru, jp, k, i, m, n, periodic_x) +
                    at3(Ru, jp, k, i + 1, m, n, periodic_x) +
                    at3(Ru, jp - 1, k, i, m, n, periodic_x) +
                    at3(Ru, jp - 1, k, i + 1, m, n, periodic_x));
                const auto rw_v = 0.25f * (
                    at3(Rw, jp - 1, k + 1, i, m, n, periodic_x) +
                    at3(Rw, jp - 1, k, i, m, n, periodic_x) +
                    at3(Rw, jp, k + 1, i, m, n, periodic_x) +
                    at3(Rw, jp, k, i, m, n, periodic_x));
                const auto vv = at3(v, j, k, i, ny_v, n, periodic_x);
                const auto ratio = at2(msfvy, j, i, ny_v, n, periodic_x) /
                                   at2(msfvx, j, i, ny_v, n, periodic_x);
                torch::Tensor value;
                if (tan_branch) {
                    const auto ua = 0.25f * (
                        at3(u, jp, k, i, m, n, periodic_x) +
                        at3(u, jp, k, i + 1, m, n, periodic_x) +
                        at3(u, jp - 1, k, i, m, n, periodic_x) +
                        at3(u, jp - 1, k, i + 1, m, n, periodic_x));
                    value = -ratio * reradius * (ua * tan_v(jp, i) * ru_v + vv * rw_v);
                } else {
                    const auto vg = 0.5f * (at3(vxgm, jp, k, i, m, n, periodic_x) +
                                           at3(vxgm, jp - 1, k, i, m, n, periodic_x));
                    value = -vg * ru_v - ratio * vv * reradius * rw_v;
                }
                v_t.index_put_({j, k, i}, value);
            }
        }
    }

    // w equation: Fortran loops k=2:kte (1-based), i.e. internal w levels only.
    for (int j = 0; j < static_cast<int>(w.size(0)); ++j) {
        const int jp = packed ? std::min(j, m - 1) : j;
        for (int k = 1; k < nz_w - 1; ++k) {
            for (int i = 0; i < nx; ++i) {
                const auto fm = at1(fzm, k, 0.5f, opt);
                const auto fp = at1(fzp, k, 0.5f, opt);
                const auto ru_w = 0.5f * (
                    fm * (at3(Ru, jp, k, i, m, n, periodic_x) + at3(Ru, jp, k, i + 1, m, n, periodic_x)) +
                    fp * (at3(Ru, jp, k - 1, i, m, n, periodic_x) + at3(Ru, jp, k - 1, i + 1, m, n, periodic_x)));
                const auto uu_w = 0.5f * (
                    fm * (at3(u, jp, k, i, m, n, periodic_x) + at3(u, jp, k, i + 1, m, n, periodic_x)) +
                    fp * (at3(u, jp, k - 1, i, m, n, periodic_x) + at3(u, jp, k - 1, i + 1, m, n, periodic_x)));
                const auto rv_w = 0.5f * (
                    fm * (at3(Rv, jp, k, i, ny_v, n, periodic_x) + at3(Rv, jp + 1, k, i, ny_v, n, periodic_x)) +
                    fp * (at3(Rv, jp, k - 1, i, ny_v, n, periodic_x) + at3(Rv, jp + 1, k - 1, i, ny_v, n, periodic_x)));
                const auto vv_w = 0.5f * (
                    fm * (at3(v, jp, k, i, ny_v, n, periodic_x) + at3(v, jp + 1, k, i, ny_v, n, periodic_x)) +
                    fp * (at3(v, jp, k - 1, i, ny_v, n, periodic_x) + at3(v, jp + 1, k - 1, i, ny_v, n, periodic_x)));
                const auto ratio = at2(msftx, jp, i, m, n, periodic_x) /
                                   at2(msfty, jp, i, m, n, periodic_x);
                w_t.index_put_({j, k, i}, reradius * (ru_w * uu_w + ratio * rv_w * vv_w));
            }
        }
    }
    return {u_t, v_t, w_t};
}

} // namespace curvature_oracle
