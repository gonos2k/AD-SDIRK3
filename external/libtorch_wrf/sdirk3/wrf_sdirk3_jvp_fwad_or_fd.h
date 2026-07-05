#ifndef WRF_SDIRK3_JVP_FWAD_OR_FD_H
#define WRF_SDIRK3_JVP_FWAD_OR_FD_H

#include <torch/torch.h>
#include <torch/csrc/autograd/forward_grad.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "wrf_sdirk3_jvp_autograd.h"

namespace wrf {
namespace sdirk3 {

namespace jvp_detail {

// RAII dual-level guard for forward-mode AD.
class DualLevelGuard {
public:
    DualLevelGuard() : level_(torch::autograd::forward_ad::enter_dual_level()) {}
    ~DualLevelGuard() { torch::autograd::forward_ad::exit_dual_level(level_); }

    DualLevelGuard(const DualLevelGuard&) = delete;
    DualLevelGuard& operator=(const DualLevelGuard&) = delete;

    uint64_t level() const { return level_; }

private:
    uint64_t level_;
};

}  // namespace jvp_detail

// Try forward-mode AD JVP first, fallback to FD-JVP if fwAD path fails.
inline torch::Tensor compute_jvp_fwad_or_fd(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    int halo_width = 0,
    float fd_epsilon_override = 0.0f,
    bool* used_fd_fallback = nullptr) {

#ifdef DNWP_DISABLE_FWAD
    if (used_fd_fallback) {
        *used_fd_fallback = true;
    }
    float eps = fd_epsilon_override;
    if (!(eps > 0.0f)) {
        const double eps_m = static_cast<double>(std::numeric_limits<float>::epsilon());
        const double u_norm = u.norm().to(torch::kCPU).item<double>();
        const double v_norm = v.norm().to(torch::kCPU).item<double>();
        eps = static_cast<float>(std::clamp(
            std::sqrt(eps_m) * (1.0 + u_norm) / std::max(v_norm, 1e-30),
            1e-6, 1e-2));
    }
    return compute_jvp_finite_diff(F, u, v, eps, halo_width);
#else
    try {
        torch::NoGradGuard no_grad;
        jvp_detail::DualLevelGuard dual_guard;

        auto u_dual = torch::_make_dual(u, v, static_cast<int64_t>(dual_guard.level()));
        auto F_dual = F(u_dual);
        auto parts = torch::_unpack_dual(F_dual, static_cast<int64_t>(dual_guard.level()));
        auto jvp = std::get<1>(parts);

        if (!jvp.defined()) {
            throw std::runtime_error("fwAD tangent is undefined");
        }
        if (used_fd_fallback) {
            *used_fd_fallback = false;
        }
        return jvp;
    } catch (...) {
        if (used_fd_fallback) {
            *used_fd_fallback = true;
        }
        float eps = fd_epsilon_override;
        if (!(eps > 0.0f)) {
            const double eps_m = static_cast<double>(std::numeric_limits<float>::epsilon());
            const double u_norm = u.norm().to(torch::kCPU).item<double>();
            const double v_norm = v.norm().to(torch::kCPU).item<double>();
            eps = static_cast<float>(std::clamp(
                std::sqrt(eps_m) * (1.0 + u_norm) / std::max(v_norm, 1e-30),
                1e-6, 1e-2));
        }
        return compute_jvp_finite_diff(F, u, v, eps, halo_width);
    }
#endif
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_JVP_FWAD_OR_FD_H
