#ifndef WRF_SDIRK3_JVP_FWAD_OR_FD_H
#define WRF_SDIRK3_JVP_FWAD_OR_FD_H

#include <torch/torch.h>
#include <string>
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

// The FD step size, computed under ONE NoGradGuard.
//
// This is the repository's hardest constraint (`.item()` only inside a NoGradGuard), and the
// three call sites below each violated it in a way that reads as guarded: the guard in the
// `try` block is a scoped object, so by the time a `catch` body runs it has already been
// destroyed. The two fallback paths therefore synced the graph unguarded -- on exactly the
// path that runs when forward-mode AD has just failed, i.e. when the graph is already
// suspect. Hoisting the computation here makes the guard impossible to scope wrong, and
// makes the epsilon rule a single definition rather than three copies that can drift.
// The machine epsilon OF THE TENSOR, not of float.
//
// The step size sqrt(eps_m)*(1+||u||)/||v|| balances two errors that move in opposite
// directions: truncation (grows with eps) and round-off (grows as eps shrinks). Both depend
// on the working precision, so a float32 constant is not a conservative default for other
// dtypes -- it is simply the wrong balance point. On float64 the perturbation is ~2e4 times
// larger than it should be and truncation dominates, which reads as a genuine tangent
// discrepancy. On FP16/BF16 it can land below the representable step, so u + eps*v == u and
// the quotient is exactly zero -- a severed tangent that looks like a converged one.
inline double machine_epsilon_of(const torch::Tensor& t) {
    switch (t.scalar_type()) {
        case torch::kFloat64:  return std::numeric_limits<double>::epsilon();
        case torch::kFloat32:  return static_cast<double>(std::numeric_limits<float>::epsilon());
        case torch::kFloat16:  return 9.765625e-4;    // 2^-10
        case torch::kBFloat16: return 7.8125e-3;      // 2^-7
        default:
            throw std::invalid_argument(
                "compute_jvp_fwad_or_fd: no finite-difference step is defined for this dtype");
    }
}

inline float fd_epsilon_for(const torch::Tensor& u, const torch::Tensor& v,
                            float override_value) {
    if (override_value > 0.0f) {
        if (!std::isfinite(override_value)) {
            throw std::invalid_argument(
                "compute_jvp_fwad_or_fd: the epsilon override is not finite");
        }
        return override_value;
    }
    torch::NoGradGuard no_grad;
    const double eps_m = machine_epsilon_of(u);
    const double u_norm = u.detach().norm().to(torch::kCPU).item<double>();
    const double v_norm = v.detach().norm().to(torch::kCPU).item<double>();
    // A non-finite norm makes every downstream comparison false rather than failing, which is
    // how a blow-up becomes a passing measurement.
    if (!std::isfinite(u_norm) || !std::isfinite(v_norm)) {
        throw std::invalid_argument(
            "compute_jvp_fwad_or_fd: a non-finite state or direction norm has no step size");
    }
    return static_cast<float>(std::clamp(
        std::sqrt(eps_m) * (1.0 + u_norm) / std::max(v_norm, 1e-30),
        1e-6, 1e-2));
}

}  // namespace jvp_detail

// Try forward-mode AD JVP first, fallback to FD-JVP if fwAD path fails.
inline torch::Tensor compute_jvp_fwad_or_fd(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    int halo_width = 0,
    float fd_epsilon_override = 0.0f,
    bool* used_fd_fallback = nullptr,
    // WHY it fell back, not just THAT it did. `catch (...)` swallowed the distinction between
    // "F threw" and "the tangent came back undefined", and those have opposite causes: the
    // first is an error inside the evaluation, the second means the dual was SEVERED -- a
    // detach, or an output that does not depend on the dual input. A caller that only learns a
    // fallback happened cannot act on it, and a degraded operator then reads as a working one.
    // Optional and defaulted, so production call sites are unchanged.
    std::string* fallback_reason = nullptr) {

#ifdef DNWP_DISABLE_FWAD
    if (used_fd_fallback) {
        *used_fd_fallback = true;
    }
    const float eps = jvp_detail::fd_epsilon_for(u, v, fd_epsilon_override);
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
            throw std::runtime_error(
                "fwAD tangent is undefined (the dual was severed: a detach, or an output that "
                "does not depend on the dual input)");
        }
        if (used_fd_fallback) {
            *used_fd_fallback = false;
        }
        return jvp;
    } catch (const std::exception& e) {
        if (used_fd_fallback) {
            *used_fd_fallback = true;
        }
        if (fallback_reason) {
            *fallback_reason = e.what();
        }
        const float eps = jvp_detail::fd_epsilon_for(u, v, fd_epsilon_override);
        return compute_jvp_finite_diff(F, u, v, eps, halo_width);
    } catch (...) {
        if (used_fd_fallback) {
            *used_fd_fallback = true;
        }
        if (fallback_reason) {
            *fallback_reason = "non-std exception";
        }
        const float eps2 = jvp_detail::fd_epsilon_for(u, v, fd_epsilon_override);
        return compute_jvp_finite_diff(F, u, v, eps2, halo_width);
    }
#endif
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_JVP_FWAD_OR_FD_H
