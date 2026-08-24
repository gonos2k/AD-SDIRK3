#pragma once
// Per-block advection decomposition: horizontal vs vertical, as numbers on one stream.
//
// The u-terms trace measured adv_z at 21-28x adv_x/adv_y and named it the term that
// carries the norm -- but it only ever watched `ru`. Whether that is a property of the
// u-momentum equation or of the vertical operator in general is not answerable from one
// block, and the difference decides where a fix belongs. The same measurement is now
// emitted for rv, rw, t and ph from one implementation, so the blocks are comparable
// rather than each having its own ad-hoc record.
//
// Norms alone cannot see two large terms cancelling, so each part also reports its cosine
// with the total: a near -1 cosine on a large term is the signature that a norm-ranked
// decomposition would call "dominant" while it contributes almost nothing to the sum.

#include <cmath>
#include <iostream>
#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

// Reductions in FP64 -- an FP32 norm over a large tile loses digits the comparison needs.
inline double adv_norm64(const torch::Tensor& t) {
    return t.defined() && t.numel() > 0
        ? t.detach().to(torch::kFloat64).norm().item<double>() : -1.0;
}

inline double adv_absmax64(const torch::Tensor& t) {
    return t.defined() && t.numel() > 0
        ? t.detach().to(torch::kFloat64).abs().max().item<double>() : -1.0;
}

// -2.0 means "no cosine defined here" (an absent part, or a zero-norm one), which is
// outside the [-1, 1] a real cosine can take and so cannot be misread as a measurement.
inline double adv_cos64(const torch::Tensor& part, const torch::Tensor& total) {
    if (!part.defined() || !total.defined()) return -2.0;
    if (part.numel() == 0 || part.numel() != total.numel()) return -2.0;
    const auto a = part.detach().to(torch::kFloat64).reshape({-1});
    const auto b = total.detach().to(torch::kFloat64).reshape({-1});
    // R13.20 (adversarial loop, iteration 4): the operands ARE detached above, so these three
    // extractions were already graph-safe -- but the repo's rule is unconditional and a lint that
    // has to reason about which operands were detached four lines up is a lint nobody can trust.
    torch::NoGradGuard ng_obs;
    const double na = a.norm().item<double>();
    const double nb = b.norm().item<double>();
    if (!(na > 0.0) || !(nb > 0.0)) return -2.0;
    return (a * b).sum().item<double>() / (na * nb);
}

// Emit one SDIRK3_ADV_SPLIT record for a block. `x` and `y` are optional: some blocks
// apply map factors to the horizontal SUM and have no separable per-direction tendency
// at the point the parts are final, and reporting -1 for those is honest where
// reconstructing a split that the code does not compute would not be.
// `stage` and `mode` name the evaluation the record belongs to. Without them the first
// record is a stage-1 evaluation, where w is identically zero and every vertical term with
// it -- a row of zeros that says nothing about the operator but reads like a measurement.
inline void emit_adv_split(const char* block,
                           int stage,
                           const char* mode,
                           const torch::Tensor& horiz,
                           const torch::Tensor& vert,
                           const torch::Tensor& x = torch::Tensor{},
                           const torch::Tensor& y = torch::Tensor{}) {
    torch::NoGradGuard no_grad;   // every .item() below is inside this scope

    const torch::Tensor total = (horiz.defined() && vert.defined())
        ? (horiz + vert) : torch::Tensor{};
    const double n_h = adv_norm64(horiz);
    const double n_z = adv_norm64(vert);
    const double n_t = adv_norm64(total);
    const double n_x = adv_norm64(x);
    const double n_y = adv_norm64(y);

    std::cerr << "SDIRK3_ADV_SPLIT"
              << " block=" << block
              << " stage=" << stage
              << " mode=" << mode
              << " adv_x=" << n_x
              << " adv_y=" << n_y
              << " adv_z=" << n_z
              << " horiz_sum=" << n_h
              // The ratio is the cross-block comparable quantity: it cancels the index
              // extents, the coupled/decoupled convention and any common scaling.
              << " z_over_horiz=" << (n_h > 0.0 ? n_z / n_h : -1.0)
              << " max_x=" << adv_absmax64(x)
              << " max_y=" << adv_absmax64(y)
              << " max_z=" << adv_absmax64(vert)
              << " adv_total=" << n_t
              << " cos_x=" << adv_cos64(x, total)
              << " cos_y=" << adv_cos64(y, total)
              << " cos_z=" << adv_cos64(vert, total)
              << " cos_h=" << adv_cos64(horiz, total)
              // If the parts are large and the total is small, they cancel; this ratio
              // states that directly instead of leaving it to be inferred.
              << " sum_of_norms_over_total="
              << (n_t > 0.0 ? (n_h + n_z) / n_t : -1.0)
              << std::endl;
}

}  // namespace sdirk3
}  // namespace wrf
