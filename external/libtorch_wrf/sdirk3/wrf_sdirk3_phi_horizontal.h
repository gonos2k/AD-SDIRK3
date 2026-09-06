#pragma once
#include <torch/torch.h>
#include <string>
#include <utility>

namespace wrf::sdirk3::phi_horizontal {

// Coupled PH tendencies before the caller applies msfty/(c1f*M+c2f).
struct Result { torch::Tensor x; torch::Tensor y; };

inline void require(bool ok, const std::string& msg) { TORCH_CHECK(ok, "PH horizontal: ", msg); }

inline torch::Tensor periodic_select(const torch::Tensor& q, int dim, int offset) {
    const auto n = q.size(dim);
    auto io = torch::TensorOptions().dtype(torch::kLong).device(q.device());
    auto idx = torch::remainder(torch::arange(n, io) + offset, n);
    return q.index_select(dim, idx);
}
inline torch::Tensor reflected_select(const torch::Tensor& q, int offset, int length) {
    // share/module_bc.f: scalar symmetric_ys/ye: q(-j)=q(j-1), q(ny+j)=q(ny-j-1).
    const auto m = q.size(0);
    auto io = torch::TensorOptions().dtype(torch::kLong).device(q.device());
    auto raw = torch::arange(length, io) + offset;
    auto period = 2 * m;
    auto folded = torch::remainder(raw, period);
    auto idx = torch::where(folded < m, folded, period - 1 - folded);
    return q.index_select(0, idx);
}
inline torch::Tensor centered(const torch::Tensor& q, int dim, int order) {
    auto sl = [&](int off) {
        if (dim == 2) return periodic_select(q, 2, off);
        return reflected_select(q, off, q.size(0));
    };
    if (order == 2) return sl(1) - sl(-1);
    if (order == 4) return (8.0 * (sl(1) - sl(-1)) - (sl(2) - sl(-2))) / 12.0;
    require(order == 6, "order must be one of 2, 4, 6");
    return (45.0 * (sl(1) - sl(-1)) - 9.0 * (sl(2) - sl(-2))
            + (sl(3) - sl(-3))) / 60.0;
}
inline torch::Tensor face_difference_x(const torch::Tensor& q) {
    const auto n = q.size(2);
    auto oriented = q - periodic_select(q, 2, -1);
    auto seam = oriented.select(2, 0);
    auto interior = oriented.slice(2, 1, n);
    return torch::cat({seam.unsqueeze(2), interior, seam.unsqueeze(2)}, 2);
}
inline torch::Tensor face_difference_y(const torch::Tensor& q) {
    const auto m = q.size(0);
    // v faces are j=0..ny: q(j)-q(j-1), including the two wall faces.
    return reflected_select(q, 0, m + 1) - reflected_select(q, -1, m + 1);
}
inline torch::Tensor mass_to_u(const torch::Tensor& mass) {
    const auto n = mass.size(1);
    auto seam = 0.5 * (mass.slice(1, 0, 1) + mass.slice(1, n - 1, n));
    auto interior = 0.5 * (mass.slice(1, 1, n) + mass.slice(1, 0, n - 1));
    return torch::cat({seam, interior, seam}, 1);
}
inline torch::Tensor mass_to_v(const torch::Tensor& mass) {
    const auto m = mass.size(0);
    auto interior = 0.5 * (mass.slice(0, 1, m) + mass.slice(0, 0, m - 1));
    return torch::cat({mass.slice(0, 0, 1), interior, mass.slice(0, m - 1, m)}, 0);
}

inline Result native_w_phi_horizontal_core(
    const torch::Tensor& phi_w, const torch::Tensor& mass,
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& msfux, const torch::Tensor& msfvy,
    const torch::Tensor& msfty, const torch::Tensor& c1f,
    const torch::Tensor& c2f, int order, float rdx, float rdy,
    double cfn, double cfn1) {
    require(phi_w.defined() && mass.defined() && u.defined() && v.defined(), "undefined state tensor");
    require(msfux.defined() && msfvy.defined() && msfty.defined() && c1f.defined() && c2f.defined(), "undefined metric/coefficient");
    require(phi_w.dim() == 3 && mass.dim() == 2 && u.dim() == 3 && v.dim() == 3,
            "rank contract is phi[ny,nz+1,nx], mass[ny,nx], u[ny,nz,nx+1], v[ny+1,nz,nx]");
    const auto ny = mass.size(0), nx = mass.size(1), nz = u.size(1);
    require(ny >= 1 && nx >= 1 && nz >= 2, "physical shape requires ny,nx >= 1 and nz >= 2");
    require(phi_w.sizes() == torch::IntArrayRef({ny, nz + 1, nx}), "phi shape mismatch");
    require(u.sizes() == torch::IntArrayRef({ny, nz, nx + 1}), "u shape mismatch");
    require(v.sizes() == torch::IntArrayRef({ny + 1, nz, nx}), "v shape mismatch");
    require(msfux.sizes() == torch::IntArrayRef({ny, nx + 1}), "msfux shape mismatch");
    require(msfvy.sizes() == torch::IntArrayRef({ny + 1, nx}), "msfvy shape mismatch");
    require(msfty.sizes() == torch::IntArrayRef({ny, nx}), "msfty shape mismatch");
    require(c1f.numel() == nz + 1 && c2f.numel() == nz + 1, "c1f/c2f length mismatch");
    require(order == 2 || order == 4 || order == 6, "order must be one of 2, 4, 6");
    const auto dev = phi_w.device(); const auto dt = phi_w.scalar_type();
    for (const auto* t : {&mass, &u, &v, &msfux, &msfvy, &msfty, &c1f, &c2f})
        require(t->device() == dev && t->scalar_type() == dt, "all operands must share device and dtype");
    auto muu = mass_to_u(mass);
    auto muv = mass_to_v(mass);
    auto gx = (order == 2) ? face_difference_x(phi_w) : centered(phi_w, 2, order);
    auto gy = (order == 2) ? face_difference_y(phi_w) : centered(phi_w, 0, order);
    auto xvel = torch::cat({torch::zeros_like(u.slice(1, 0, 1)),
                            u.slice(1, 1, nz) + u.slice(1, 0, nz - 1),
                            cfn * u.slice(1, nz - 1, nz) + cfn1 * u.slice(1, nz - 2, nz - 1)}, 1);
    auto yvel = torch::cat({torch::zeros_like(v.slice(1, 0, 1)),
                            v.slice(1, 1, nz) + v.slice(1, 0, nz - 1),
                            cfn * v.slice(1, nz - 1, nz) + cfn1 * v.slice(1, nz - 2, nz - 1)}, 1);
    auto c1 = c1f.view({1, nz + 1, 1}); auto c2 = c2f.view({1, nz + 1, 1});
    auto xf = (c1 * muu.unsqueeze(1) + c2) * xvel * msfux.unsqueeze(1);
    auto yf = (c1 * muv.unsqueeze(1) + c2) * yvel * msfvy.unsqueeze(1);
    torch::Tensor x, y;
    if (order == 2) {
        auto fx = 0.25 * xf * gx;
        auto fy = 0.25 * yf * gy;
        // Fortran top uses .5 rather than .25 for extrapolated velocity.
        fx = torch::cat({fx.slice(1, 0, nz), 2.0 * fx.slice(1, nz, nz + 1)}, 1);
        fy = torch::cat({fy.slice(1, 0, nz), 2.0 * fy.slice(1, nz, nz + 1)}, 1);
        // WRF rhs_ph is advective here: positive-oriented west/east and
        // south/north gradients are added, then multiplied by -rd{ x,y}.
        x = -rdx * (fx.slice(2, 1, nx + 1) + fx.slice(2, 0, nx)) / msfty.unsqueeze(1);
        y = -rdy * (fy.slice(0, 1, ny + 1) + fy.slice(0, 0, ny)) / msfty.unsqueeze(1);
    } else {
        // WRF's periodic/symmetric order-4/6 branch is advective form:
        // the east+west velocity factors multiply a centered mass-point derivative.
        auto xfac = xf.slice(2, 1, nx + 1) + xf.slice(2, 0, nx);
        auto yfac = yf.slice(0, 1, ny + 1) + yf.slice(0, 0, ny);
        x = -0.25 * rdx * xfac.slice(1, 0, nz) * gx.slice(1, 0, nz) / msfty.unsqueeze(1);
        y = -0.25 * rdy * yfac.slice(1, 0, nz) * gy.slice(1, 0, nz) / msfty.unsqueeze(1);
        auto xtop = -0.5 * rdx * xfac.select(1, nz).unsqueeze(1) * gx.select(1, nz).unsqueeze(1) / msfty.unsqueeze(1);
        auto ytop = -0.5 * rdy * yfac.select(1, nz).unsqueeze(1) * gy.select(1, nz).unsqueeze(1) / msfty.unsqueeze(1);
        x = torch::cat({x, xtop}, 1);
        y = torch::cat({y, ytop}, 1);
    }
    return {x, y};
}

inline torch::Tensor extend_packed_scalar(const torch::Tensor& core) {
    auto col = core.slice(2, 0, 1);
    auto with_col = torch::cat({core, col}, 2);
    auto last_row = with_col.slice(0, with_col.size(0) - 1, with_col.size(0));
    return torch::cat({with_col, last_row}, 0);
}
inline Result native_w_phi_horizontal(
    const torch::Tensor& phi_w, const torch::Tensor& mass,
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& msfux, const torch::Tensor& msfvy,
    const torch::Tensor& msfty, const torch::Tensor& c1f,
    const torch::Tensor& c2f, int order, float rdx, float rdy,
    double cfn, double cfn1, bool packed) {
    if (!packed) return native_w_phi_horizontal_core(phi_w, mass, u, v, msfux, msfvy, msfty,
                                                         c1f, c2f, order, rdx, rdy, cfn, cfn1);
    require(phi_w.dim() == 3 && mass.dim() == 2 && u.dim() == 3 && v.dim() == 3, "packed rank contract");
    const auto ny = mass.size(0) - 1, nx = mass.size(1) - 1, nz = u.size(1);
    require(ny >= 1 && nx >= 1, "packed shape requires at least one physical cell in each axis");
    auto core = native_w_phi_horizontal_core(
        phi_w.slice(0, 0, ny).slice(2, 0, nx), mass.slice(0, 0, ny).slice(1, 0, nx),
        u.slice(0, 0, ny).slice(2, 0, nx + 1), v.slice(0, 0, ny + 1).slice(2, 0, nx),
        msfux.slice(0, 0, ny).slice(1, 0, nx + 1), msfvy.slice(0, 0, ny + 1).slice(1, 0, nx),
        msfty.slice(0, 0, ny).slice(1, 0, nx), c1f, c2f, order, rdx, rdy, cfn, cfn1);
    return {extend_packed_scalar(core.x), extend_packed_scalar(core.y)};
}
}
