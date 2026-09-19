#ifndef WRF_SDIRK3_ARK324_COMPOSITION_H
#define WRF_SDIRK3_ARK324_COMPOSITION_H

#include "wrf_sdirk3_imex_ark324_coeffs.h"
#include <torch/torch.h>
#include <vector>

namespace wrf::sdirk3 {

// Use double scalars in mode 3 so FP64 states retain the tableau precision.
// Keep stage history and diagnostic observations in the same operation order.
template <typename Scalar, typename Observer>
torch::Tensor ark324_stage_base(
    const torch::Tensor& initial, Scalar dt, int stage,
    const std::vector<torch::Tensor>& slow,
    const std::vector<torch::Tensor>& fast, Observer observe) {
    using Ark = ARK324L2SACoefficients;
    TORCH_CHECK(stage >= 0 && stage < Ark::stages,
                "ark324_stage_base: invalid stage");
    TORCH_CHECK(slow.size() >= static_cast<size_t>(stage) &&
                fast.size() >= static_cast<size_t>(stage),
                "ark324_stage_base: missing stage history");
    auto state = initial;
    for (int j = 0; j < stage; ++j) {
        state = state
            + dt * static_cast<Scalar>(Ark::a_explicit[stage][j]) * slow[j]
            + dt * static_cast<Scalar>(Ark::a_implicit[stage][j]) * fast[j];
        observe(j, state);
    }
    return state;
}

template <typename Scalar>
torch::Tensor ark324_final_state(
    const torch::Tensor& initial, Scalar dt,
    const std::vector<torch::Tensor>& full) {
    using Ark = ARK324L2SACoefficients;
    TORCH_CHECK(full.size() == Ark::stages,
                "ark324_final_state: all four completed stages are required");
    auto state = initial;
    for (int i = 0; i < Ark::stages; ++i) {
        state = state + dt * static_cast<Scalar>(Ark::b[i]) * full[i];
    }
    return state;
}

}  // namespace wrf::sdirk3
#endif
