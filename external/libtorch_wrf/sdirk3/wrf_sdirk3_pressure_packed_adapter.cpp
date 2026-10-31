#include "wrf_sdirk3_pressure_packed_adapter.h"
#include <limits>

namespace wrf::sdirk3 {
PressurePackedAdapter::PressurePackedAdapter(int n, int m, int nz)
    : n_(n), m_(m), nz_(nz) {
    TORCH_CHECK(n >= 2 && m >= 2 && nz >= 2 && n <= std::numeric_limits<int>::max() - 2 &&
                m <= std::numeric_limits<int>::max() - 2 && nz < std::numeric_limits<int>::max(),
                "packed pressure adapter dimensions are invalid");
    layout_ = StateLayout::from_grid_dims(n + 1, m + 1, nz, n + 2, m + 2, nz + 1);
    TORCH_CHECK(layout_.is_valid() && layout_.is_exact,
                "packed pressure adapter requires a valid physical domain");
}

ReferencePressureFields PressurePackedAdapter::extract(const torch::Tensor& x) const {
    TORCH_CHECK(x.defined() && x.dim() == 1 && x.numel() == size() && x.is_cpu() &&
                x.scalar_type() == torch::kFloat64,
                "packed pressure adapter requires a CPU FP64 packed vector");
    const auto view = [&](int b, torch::IntArrayRef shape) {
        const auto& block = layout_.blocks[b];
        return x.slice(0, block.start, block.start + block.size).reshape(shape);
    };
    auto u = view(0, {m_ + 1, nz_, n_ + 2});
    auto v = view(1, {m_ + 2, nz_, n_ + 1});
    auto w = view(2, {m_ + 1, nz_ + 1, n_ + 1});
    auto ph = view(3, {m_ + 1, nz_ + 1, n_ + 1});
    auto t = view(4, {m_ + 1, nz_, n_ + 1});
    auto mu = view(5, {m_ + 1, n_ + 1});
    return {u.slice(0, 0, m_).slice(2, 0, n_),
            v.slice(0, 1, m_).slice(2, 0, n_),
            w.slice(0, 0, m_).slice(1, 1, nz_ + 1).slice(2, 0, n_),
            ph.slice(0, 0, m_).slice(1, 1, nz_ + 1).slice(2, 0, n_),
            t.slice(0, 0, m_).slice(2, 0, n_),
            mu.slice(0, 0, m_).slice(1, 0, n_)};
}

torch::Tensor PressurePackedAdapter::extend(const ReferencePressureFields& f) const {
    const auto valid = [](const torch::Tensor& v, torch::IntArrayRef shape) {
        return v.defined() && v.is_cpu() && v.scalar_type() == torch::kFloat64 && v.sizes() == shape;
    };
    TORCH_CHECK(valid(f.u, {m_,nz_,n_}) && valid(f.v, {m_-1,nz_,n_}) &&
                valid(f.w, {m_,nz_,n_}) && valid(f.phi, {m_,nz_,n_}) &&
                valid(f.theta, {m_,nz_,n_}) && valid(f.mu, {m_,n_}),
                "independent pressure fields have invalid shape, device or dtype");
    const auto mass = [&](const torch::Tensor& x) {
        auto seam = torch::cat({x, x.slice(2, 0, 1)}, 2);
        return torch::cat({seam, seam.slice(0, m_ - 1, m_)}, 0);
    };
    auto ux = torch::cat({f.u, f.u.slice(2, 0, 2)}, 2);
    auto u = torch::cat({ux, ux.slice(0, m_ - 1, m_)}, 0);
    auto wall = torch::zeros({1, nz_, n_}, f.v.options());
    auto vf = torch::cat({wall, f.v, wall}, 0);
    auto vx = torch::cat({vf, vf.slice(2, 0, 1)}, 2);
    auto v = torch::cat({vx, -vx.slice(0, m_ - 1, m_)}, 0);
    const auto vertical = [&](const torch::Tensor& x) {
        return mass(torch::cat({torch::zeros({m_,1,n_}, x.options()), x}, 1));
    };
    auto mu = mass(f.mu.unsqueeze(1)).squeeze(1);
    return torch::cat({u.flatten(), v.flatten(), vertical(f.w).flatten(),
                       vertical(f.phi).flatten(), mass(f.theta).flatten(), mu.flatten()});
}

torch::Tensor PressurePackedAdapter::project(const torch::Tensor& x) const {
    return extend(extract(x));
}
torch::Tensor PressurePackedAdapter::apply(const ReferencePressureModel& model, const torch::Tensor& x) const {
    auto fields = extract(x);
    const auto& ph = layout_.blocks[3];
    const auto phi0 = x.slice(0, ph.start, ph.start + ph.size)
        .view({m_ + 1, nz_ + 1, n_ + 1}).slice(0, 0, m_).select(1, 0).slice(1, 0, n_);
    fields.phi = fields.phi - phi0.unsqueeze(1);
    auto result = model.apply(fields);
    result.phi = result.phi + phi0.unsqueeze(1);
    return extend(result) + (x - project(x));
}
torch::Tensor PressurePackedAdapter::solve(const ReferencePressureModel& model, const torch::Tensor& b) const {
    auto fields = extract(b);
    const auto& ph = layout_.blocks[3];
    const auto phi0 = b.slice(0, ph.start, ph.start + ph.size)
        .view({m_ + 1, nz_ + 1, n_ + 1}).slice(0, 0, m_).select(1, 0).slice(1, 0, n_);
    fields.phi = fields.phi - phi0.unsqueeze(1);
    auto result = model.solve(fields);
    result.phi = result.phi + phi0.unsqueeze(1);
    return extend(result) + (b - project(b));
}
torch::Tensor PressurePackedAdapter::solve_scaled(const ReferencePressureModel& model,
                                                   const torch::Tensor& rhs_scaled,
                                                   const torch::Tensor& scale) const {
    TORCH_CHECK(scale.defined() && scale.sizes() == rhs_scaled.sizes() && scale.is_cpu() &&
                scale.scalar_type() == torch::kFloat64 && torch::isfinite(scale).all().item<bool>() &&
                (scale > 0).all().item<bool>(), "scale must be finite, positive and packed");
    return solve(model, scale * rhs_scaled) / scale;
}
}
