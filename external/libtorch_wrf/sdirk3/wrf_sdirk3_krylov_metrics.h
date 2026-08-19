#pragma once

// ============================================================================
// Krylov metrics: relative residuals and objective shares, in NAMED coordinates.
// ============================================================================
// Three coordinate systems coexist in the Newton-Krylov solve, and conflating them
// invalidated two published measurements:
//
//   physical    R              what the stage acceptance gate judges
//   Krylov (S)  r~ = S^-1 R    what FGMRES iterates (gmres_rhs = -(S_inv_diag_ * R))
//   objective   L r~           what a given left weighting L minimises
//
// The corresponding left weights, applied to the vectors FGMRES actually holds:
//
//   L_S    = I         unweighted, in Krylov coordinates
//   L_phys = S         physical
//   L_D    = D^-1      FGMRES's own block-scaled objective
//   L_E    = E^-1 S    the stage gate's WRMS  <- note the S; E^-1 alone is a third,
//                                                meaningless weighting
//
// This header exists so there is ONE implementation of each quantity, reachable from
// the contracts. The failure it is built against is not arithmetic error but a
// combination that should never have been expressible: a numerator carrying one weight
// divided by a denominator carrying another.

#include <torch/torch.h>

#include <cmath>
#include <limits>
#include <vector>

#include "wrf_sdirk3_operator_contract.h"

namespace wrf {
namespace sdirk3 {

struct RelativeResidual {
    double numerator   = std::numeric_limits<double>::quiet_NaN();
    double denominator = std::numeric_limits<double>::quiet_NaN();
    double value       = std::numeric_limits<double>::quiet_NaN();
    bool   valid       = false;
};

// ||L r|| / ||L b||. Taking r, b and L together and dividing inside is the whole point:
// a caller cannot weight one side and not the other. An undefined weight means the
// identity, so the unweighted ratio needs no ones-vector that could drift from I.
inline RelativeResidual relative_residual(const torch::Tensor& r,
                                          const torch::Tensor& b,
                                          const torch::Tensor& left_weight) {
    torch::NoGradGuard no_grad;
    RelativeResidual out;
    if (!r.defined() || !b.defined() || r.numel() != b.numel()) return out;
    // A weight of the wrong length is a coordinate bug, not something to broadcast around.
    if (left_weight.defined() && left_weight.numel() != r.numel()) return out;
    const auto r64 = r.detach().to(torch::kFloat64).reshape({-1});
    const auto b64 = b.detach().to(torch::kFloat64).reshape({-1});
    const auto w64 = left_weight.defined()
                         ? left_weight.detach().to(torch::kFloat64).reshape({-1})
                         : torch::ones_like(r64);
    out.numerator   = (w64 * r64).norm().item<double>();
    out.denominator = (w64 * b64).norm().item<double>();
    if (!std::isfinite(out.numerator) || !std::isfinite(out.denominator) ||
        out.denominator <= 0.0) {
        return out;
    }
    out.value = out.numerator / out.denominator;
    out.valid = true;
    return out;
}

// The stage-WRMS objective expressed on Krylov vectors. E^-1 is physical; the vectors are
// r~ = S^-1 R; so the weight is E^-1 S. Returns an undefined tensor rather than silently
// falling back, because the fallback (E^-1 alone) is a different objective that can rank
// the blocks in the opposite order.
inline torch::Tensor wrms_left_weight(const torch::Tensor& e_inv,
                                      const torch::Tensor& krylov_to_physical) {
    torch::NoGradGuard no_grad;
    if (!e_inv.defined() || !krylov_to_physical.defined()) return torch::Tensor{};
    if (e_inv.numel() != krylov_to_physical.numel()) return torch::Tensor{};
    return e_inv * krylov_to_physical.detach().to(e_inv.options()).reshape_as(e_inv);
}

// Per-block share of ||L r||^2. Shares sum to 1 when the total is positive.
inline std::vector<double> block_energy_shares(const StateLayout& layout,
                                               const torch::Tensor& r,
                                               const torch::Tensor& left_weight) {
    torch::NoGradGuard no_grad;
    std::vector<double> shares(layout.blocks.size(), 0.0);
    if (!r.defined()) return shares;
    if (left_weight.defined() && left_weight.numel() != r.numel()) return shares;
    const auto r64 = r.detach().to(torch::kFloat64).reshape({-1});
    const auto w64 = left_weight.defined()
                         ? left_weight.detach().to(torch::kFloat64).reshape({-1})
                         : torch::Tensor{};
    double total = 0.0;
    for (std::size_t i = 0; i < layout.blocks.size(); ++i) {
        const auto& blk = layout.blocks[i];
        if (blk.start + blk.size > r64.numel()) return std::vector<double>(layout.blocks.size(), 0.0);
        auto rb = r64.slice(0, blk.start, blk.start + blk.size);
        if (w64.defined()) rb = rb * w64.slice(0, blk.start, blk.start + blk.size);
        shares[i] = rb.pow(2).sum().item<double>();
        total += shares[i];
    }
    if (total > 0.0) {
        for (auto& v : shares) v /= total;
    }
    return shares;
}

}  // namespace sdirk3
}  // namespace wrf
