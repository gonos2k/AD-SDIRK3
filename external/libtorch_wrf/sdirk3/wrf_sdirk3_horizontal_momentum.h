#pragma once
#include <torch/torch.h>
#include <array>

namespace wrf::sdirk3 {

struct HorizontalMomentumFluxes {
    torch::Tensor ux, uy, vx, vy, wx, wy;
};

// Independent physical cells: X periodic, Y even except normal V (odd).
// The caller supplies canonical Ru/Rv, native maps, and its existing flux rule.
template <class Flux>
HorizontalMomentumFluxes
wrf_horizontal_momentum(const torch::Tensor &u, const torch::Tensor &v, const torch::Tensor &w,
                        const torch::Tensor &ru, const torch::Tensor &rv,
                        const torch::Tensor &msfux, const torch::Tensor &msfvy,
                        const torch::Tensor &msftx, const torch::Tensor &fnm,
                        const torch::Tensor &fnp, double rdx, double rdy, Flux flux) {
    const int64_t m = u.size(0), z = u.size(1), n = u.size(2) - 1;
    TORCH_CHECK(m >= 2 && n >= 2 && z >= 2 && u.sizes() == ru.sizes() && v.sizes() == rv.sizes() &&
                    v.sizes() == torch::IntArrayRef({m + 1, z, n}) &&
                    w.sizes() == torch::IntArrayRef({m, z + 1, n}),
                "invalid native momentum layout");
    TORCH_CHECK(msfux.sizes() == torch::IntArrayRef({m, n + 1}) &&
                    msfvy.sizes() == torch::IntArrayRef({m + 1, n}) &&
                    msftx.sizes() == torch::IntArrayRef({m, n}) && fnm.dim() == 1 &&
                    fnp.dim() == 1 && fnm.numel() >= z && fnp.numel() >= z,
                "invalid native momentum maps or interpolation coefficients");
    const auto q_u = u.slice(2, 0, n), r_u = ru.slice(2, 0, n);
    const auto x_flux = [&](const torch::Tensor &q, const torch::Tensor &transport) {
        std::array<torch::Tensor, 6> samples;
        for (int offset = -3; offset <= 2; ++offset)
            samples[offset + 3] = torch::roll(q, -offset, 2);
        return flux(samples, transport);
    };
    const auto x_div = [&](const torch::Tensor &f) { return (torch::roll(f, -1, 2) - f) * rdx; };
    const auto y_flux = [&](const torch::Tensor &q, const torch::Tensor &transport, int first_face,
                            bool odd) {
        auto index = torch::arange(transport.size(0), q.options().dtype(torch::kLong)) + first_face;
        std::array<torch::Tensor, 6> samples;
        for (int offset = -3; offset <= 2; ++offset) {
            const auto folded = torch::remainder(index + offset, 2 * m);
            const auto reflected = odd ? folded > m : folded >= m;
            const auto source = torch::where(reflected, (odd ? 2 * m : 2 * m - 1) - folded, folded);
            auto sample = q.index_select(0, source);
            if (odd)
                sample = sample * torch::where(reflected, -1, 1).view({-1, 1, 1});
            samples[offset + 3] = sample;
        }
        return flux(samples, transport);
    };

    const auto ux = x_flux(q_u, 0.5 * (r_u + torch::roll(r_u, 1, 2)));
    const auto uy = y_flux(q_u, 0.5 * (rv + torch::roll(rv, 1, 2)), 0, false);
    const auto map_u = msfux.slice(1, 0, n).unsqueeze(1);
    const auto du_x = -x_div(ux) * map_u;
    const auto du_y = -(uy.slice(0, 1, m + 1) - uy.slice(0, 0, m)) * rdy * map_u;

    const auto vx = x_flux(v.slice(0, 1, m), 0.5 * (r_u.slice(0, 0, m - 1) + r_u.slice(0, 1, m)));
    const auto vy = y_flux(v, 0.5 * (rv.slice(0, 0, m) + rv.slice(0, 1, m + 1)), 1, true);
    const auto map_v = msfvy.slice(0, 1, m).unsqueeze(1);
    const auto dv_x = -x_div(vx) * map_v;
    const auto dv_y = -(vy.slice(0, 1, m) - vy.slice(0, 0, m - 1)) * rdy * map_v;

    const auto at_w = [&](const torch::Tensor &transport) {
        const auto middle = fnm.slice(0, 1, z).view({1, -1, 1}) * transport.slice(1, 1, z) +
                            fnp.slice(0, 1, z).view({1, -1, 1}) * transport.slice(1, 0, z - 1);
        const auto lid = (2 - fnm.select(0, z - 1)) * transport.slice(1, z - 1, z) -
                         fnp.select(0, z - 1) * transport.slice(1, z - 2, z - 1);
        return torch::cat({torch::zeros_like(transport.slice(1, 0, 1)), middle, lid}, 1);
    };
    const auto wx = x_flux(w, at_w(r_u));
    const auto wy = y_flux(w, at_w(rv), 0, false);
    const auto map_w = msftx.unsqueeze(1);
    const auto dw_x = -x_div(wx) * map_w;
    const auto dw_y = -(wy.slice(0, 1, m + 1) - wy.slice(0, 0, m)) * rdy * map_w;
    const auto u_seam = [](const torch::Tensor &q) { return torch::cat({q, q.slice(2, 0, 1)}, 2); };
    const auto v_walls = [](const torch::Tensor &q) {
        const auto wall = torch::zeros_like(q.slice(0, 0, 1));
        return torch::cat({wall, q, wall}, 0);
    };
    return {u_seam(du_x), u_seam(du_y), v_walls(dv_x), v_walls(dv_y), dw_x, dw_y};
}
} // namespace wrf::sdirk3
