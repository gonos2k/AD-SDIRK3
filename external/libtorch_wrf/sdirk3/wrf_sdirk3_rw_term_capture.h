#pragma once
// PR 9B commit 2: opt-in capture of the rw (w-momentum) tendency terms inside
// one RHS evaluation, for the term-level tangent bisection. Single global
// slot (single-tile, baseline-thread diagnostic — armed only by the
// directional checker around individual compute_rhs calls). Default cost at
// every site is ONE bool test; nothing is stored unless armed.
//
// Tensors are stored AS-IS (not detached): during a forward-mode dual
// evaluation the caller unpacks the tangents while its DualLevelGuard is
// still alive, then resets the slot.
#include <torch/torch.h>
#include <string>
#include <utility>
#include <vector>

namespace wrf {
namespace sdirk3 {

struct RwTermCapture {
    bool armed = false;
    std::vector<std::pair<std::string, torch::Tensor>> terms;
    void add(const char* name, const torch::Tensor& t) {
        if (armed) terms.emplace_back(name, t);
    }
    void reset() { terms.clear(); }
};

inline RwTermCapture& rw_term_capture_slot() {
    static RwTermCapture g_slot;
    return g_slot;
}

}  // namespace sdirk3
}  // namespace wrf
