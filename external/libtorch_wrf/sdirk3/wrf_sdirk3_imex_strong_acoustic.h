#ifndef WRF_SDIRK3_IMEX_STRONG_ACOUSTIC_H
#define WRF_SDIRK3_IMEX_STRONG_ACOUSTIC_H

#include <torch/torch.h>

#include <algorithm>
#include <cmath>

namespace wrf {
namespace sdirk3 {

// Simple stiffness indicator chi = c_s,max * dt / min(dx, dz).
inline bool is_strong_acoustic_mode(const torch::Tensor& cs2_m_ref,
                                    double dt,
                                    double dx_min,
                                    double dz_min,
                                    double threshold = 0.8) {
    double cs2_max = cs2_m_ref.max().to(torch::kCPU).item<double>();
    double cs_max = std::sqrt(std::max(0.0, cs2_max));
    double denom = std::max(1e-12, std::min(dx_min, dz_min));
    double chi = cs_max * dt / denom;
    return chi > threshold;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_STRONG_ACOUSTIC_H
