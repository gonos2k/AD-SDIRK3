#pragma once

// WRF Coriolis tendencies on the physical periodic-X, symmetric-Y core.
// Inputs alpha_u/alpha_v/alpha_w are the native coupled-momentum factors:
// Ru=alpha_u*u, Rv=alpha_v*v, Rw=alpha_w*w.  The returned tensors are
// canonical coupled-momentum rates; the caller owns ordinary/export conversion
// and applies the existing Q aliases after this physical core.

#include <torch/torch.h>

namespace wrf::sdirk3 {

struct CoriolisTendencies {
    // WRF canonical coupled-momentum rates.  The caller divides each component
    // by its native alpha to obtain the corresponding physical velocity rate.
    torch::Tensor u;
    torch::Tensor v;
    torch::Tensor w;
};

inline CoriolisTendencies wrf_coriolis_tendencies(
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& alpha_u, const torch::Tensor& alpha_v,
    const torch::Tensor& alpha_w, const torch::Tensor& msfux,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx,
    const torch::Tensor& msfvy, const torch::Tensor& msftx,
    const torch::Tensor& msfty, const torch::Tensor& f, const torch::Tensor& e,
    const torch::Tensor& sina, const torch::Tensor& cosa,
    const torch::Tensor& fzm, const torch::Tensor& fzp) {
    const int64_t m = u.size(0);
    const int64_t z = u.size(1);
    const int64_t n = u.size(2) - 1;
    TORCH_CHECK(m >= 1 && n >= 2 && z >= 1,
                "canonical Coriolis core requires at least 1x2x1 cells");
    TORCH_CHECK(u.sizes() == torch::IntArrayRef({m, z, n + 1}) &&
                    v.sizes() == torch::IntArrayRef({m + 1, z, n}) &&
                    w.sizes() == torch::IntArrayRef({m, z + 1, n}),
                "invalid native velocity layout");
    TORCH_CHECK(alpha_u.sizes() == u.sizes() && alpha_v.sizes() == v.sizes() &&
                    alpha_w.sizes() == w.sizes(),
                "alpha must be native-staggered");
    TORCH_CHECK(msfux.sizes() == torch::IntArrayRef({m, n + 1}) &&
                    msfuy.sizes() == msfux.sizes() &&
                    msfvx.sizes() == torch::IntArrayRef({m + 1, n}) &&
                    msfvy.sizes() == msfvx.sizes() &&
                    msftx.sizes() == torch::IntArrayRef({m, n}) &&
                    msfty.sizes() == msftx.sizes() &&
                    f.sizes() == msftx.sizes() && e.sizes() == msftx.sizes() &&
                    sina.sizes() == msftx.sizes() && cosa.sizes() == msftx.sizes(),
                "invalid map or Coriolis field layout");
    TORCH_CHECK(fzm.dim() == 1 && fzp.dim() == 1 && fzm.numel() >= z &&
                    fzp.numel() >= z,
                "invalid vertical interpolation weights");

    // Fortran couple_momentum, with the map factors already included in alpha:
    //   Ru = alpha_u*u, Rv = alpha_v*v, Rw = alpha_w*w.
    const auto ru = alpha_u * u;
    const auto rv = alpha_v * v;
    const auto rw = alpha_w * w;

    // U equation, Fortran lines 3727-3733.  The x=0 face uses the wrapped
    // mass cell n-1, and the x=n face is its physical periodic alias.
    const auto rv_west = torch::roll(rv, {1}, {2});
    const auto rv_at_u = 0.25 *
        (rv_west.slice(0, 0, m) + rv.slice(0, 0, m) +
         rv_west.slice(0, 1, m + 1) + rv.slice(0, 1, m + 1));
    const auto rw_x = 0.5 * (rw + torch::roll(rw, {1}, {2}));
    const auto rw_at_u = 0.5 *
        (rw_x.slice(1, 0, z) + rw_x.slice(1, 1, z + 1));
    const auto fu = 0.5 * (f + torch::roll(f, {1}, {1}));
    const auto eu = 0.5 * (e + torch::roll(e, {1}, {1}));
    const auto cosa_u = 0.5 * (cosa + torch::roll(cosa, {1}, {1}));
    const auto ratio_u = (msfux / msfuy).slice(1, 0, n).unsqueeze(1);
    const auto cu_core = ratio_u * fu.unsqueeze(1) * rv_at_u -
                         eu.unsqueeze(1) * cosa_u.unsqueeze(1) * rw_at_u;
    const auto cu = torch::cat({cu_core, cu_core.slice(2, 0, 1)}, 2);

    // V equation, Fortran lines 3799-3808.  The two physical wall rows are
    // projected out by the staggered boundary contract; only j=1..m-1 is a
    // physical normal-velocity equation.
    const auto ru_at_v = 0.25 *
        (ru.slice(0, 0, m - 1).slice(2, 0, n) +
         ru.slice(0, 0, m - 1).slice(2, 1, n + 1) +
         ru.slice(0, 1, m).slice(2, 0, n) +
         ru.slice(0, 1, m).slice(2, 1, n + 1));
    const auto rw_y = 0.5 * (rw.slice(0, 0, m - 1) + rw.slice(0, 1, m));
    const auto rw_at_v = 0.5 *
        (rw_y.slice(1, 0, z) + rw_y.slice(1, 1, z + 1));
    const auto fv = 0.5 * (f.slice(0, 0, m - 1) + f.slice(0, 1, m));
    const auto ev = 0.5 * (e.slice(0, 0, m - 1) + e.slice(0, 1, m));
    const auto sina_v = 0.5 *
        (sina.slice(0, 0, m - 1) + sina.slice(0, 1, m));
    const auto ratio_v = (msfvy / msfvx).slice(0, 1, m).unsqueeze(1);
    const auto cv_inner = -ratio_v * fv.unsqueeze(1) * ru_at_v +
                          ratio_v * ev.unsqueeze(1) * sina_v.unsqueeze(1) * rw_at_v;
    const auto cv_wall = torch::zeros({1, z, n}, cv_inner.options());
    const auto cv = torch::cat({cv_wall, cv_inner, cv_wall}, 0);

    // W equation, Fortran lines 3843-3848.  Unlike U/V, this is the only
    // Coriolis average that uses the supplied fzm/fzp vertical weights.
    // With one mass level there are no interior W levels; preserve the native
    // [m,z+1,n] shape explicitly rather than concatenating empty tensors.
    if (z == 1)
        return {cu, cv, torch::zeros_like(w)};
    const auto ru_at_mass = 0.5 *
        (ru.slice(2, 0, n) + ru.slice(2, 1, n + 1));
    const auto rv_at_mass = 0.5 *
        (rv.slice(0, 0, m) + rv.slice(0, 1, m + 1));
    const auto fzm_inner = fzm.slice(0, 1, z).view({1, z - 1, 1});
    const auto fzp_inner = fzp.slice(0, 1, z).view({1, z - 1, 1});
    const auto ru_at_w_inner = fzm_inner * ru_at_mass.slice(1, 1, z) +
                               fzp_inner * ru_at_mass.slice(1, 0, z - 1);
    const auto rv_at_w_inner = fzm_inner * rv_at_mass.slice(1, 1, z) +
                               fzp_inner * rv_at_mass.slice(1, 0, z - 1);
    const auto ratio_w = (msftx / msfty).unsqueeze(1);
    const auto cw_inner = e.unsqueeze(1) *
        (cosa.unsqueeze(1) * ru_at_w_inner -
         ratio_w * sina.unsqueeze(1) * rv_at_w_inner);
    const auto cw_wall = torch::zeros_like(cw_inner.slice(1, 0, 1));
    const auto cw = torch::cat({cw_wall, cw_inner, cw_wall}, 1);

    return {cu, cv, cw};
}

}  // namespace wrf::sdirk3
