// The linear-operator contract: what A is, which blocks it acts on, and how good P^-1 is.
//
// Everything here is a pure function of an explicit snapshot. No globals, no environment reads,
// no latches, no mutation of solver state -- the three defect classes this campaign kept hitting
// when diagnostics lived inside the solver.
//
// THE ONE JUDGEMENT. A preconditioner is judged by
//
//     eps_q = || A P^-1 v_q - v_q || / || v_q ||
//
// on the RIGHT-preconditioned operator FGMRES actually iterates, and by where that error lands.
// Directional summaries (Rayleigh quotients, gains, cosines) are telemetry: for a coupled Schur
// preconditioner A_qq = 1 does NOT imply (A^-1)_qq = 1, so a raw block summary is not a target
// for an inverse gain. Reading one as the other produced three retracted conclusions.

#pragma once

#include "wrf_sdirk3_state_layout.h"

#include <torch/torch.h>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

using LinearOperator = std::function<torch::Tensor(const torch::Tensor&)>;

// Which blocks the implicit solve actually owns. One authority, shared by the Newton unknown,
// the ImplicitOnly RHS, the residual gate, the JVP/VJP and the preconditioner -- when these
// disagree about a block, the stage residual contains a row nothing is solving.
struct ImplicitActiveDomain {
    bool ru = true, rv = true, rw = true, ph = true, theta = true, mu = true;

    bool by_name(const std::string& n) const {
        if (n == "ru") return ru;
        if (n == "rv") return rv;
        if (n == "rw") return rw;
        if (n == "ph") return ph;
        if (n == "t")  return theta;
        if (n == "mu") return mu;
        throw std::invalid_argument("ImplicitActiveDomain: unknown block '" + n + "'");
    }

    // HEVI with the corrected mass coordinate: horizontal momentum and the column mass are
    // explicit, so the implicit solve owns only the vertical fast subsystem.
    static ImplicitActiveDomain hevi_vertical_fast() {
        ImplicitActiveDomain d;
        d.ru = d.rv = d.mu = false;
        d.rw = d.ph = d.theta = true;
        return d;
    }
};

// Everything a probe needs to be reproducible, and to say which evaluation it describes.
struct LinearizationSnapshot {
    StateLayout layout;
    ImplicitActiveDomain active;

    double dt = 0.0;
    double gamma = 0.0;
    double h() const { return dt * gamma; }

    int physical_step = -1;
    int ark_stage = -1;
    int newton_iteration = -1;

    uint64_t rhs_generation = 0;
    uint64_t preconditioner_generation = 0;

    bool is_valid() const {
        return layout.is_valid() && dt > 0.0 && gamma > 0.0;
    }
};

// eps_q and where the error lands. output_block_error[i] follows layout.blocks order.
struct PreconditionerQualityReport {
    std::string input_block;
    double global_error = 0.0;                      // || A P^-1 v - v || / || v ||
    std::array<double, 6> output_block_error{};     // per-block share of that residual
    bool ok = false;

    // Error concentrated OUTSIDE the input block means the cross-block model is wrong, which a
    // scalar per-block number cannot distinguish from a bad in-block inverse.
    double in_block_error(const StateLayout& layout) const {
        for (std::size_t i = 0; i < layout.blocks.size() && i < output_block_error.size(); ++i) {
            if (layout.blocks[i].name == input_block) return output_block_error[i];
        }
        return 0.0;
    }
};

// THE judgement. Pure: takes the operators as arguments, returns a value, touches nothing.
inline PreconditionerQualityReport evaluate_right_preconditioner(
    const LinearizationSnapshot& snapshot,
    const LinearOperator& apply_A,
    const LinearOperator& apply_P_inverse,
    const torch::Tensor& direction)
{
    PreconditionerQualityReport rep;
    if (!snapshot.is_valid() || !direction.defined() ||
        direction.numel() != snapshot.layout.total_size) {
        return rep;   // ok stays false; callers must not read the numbers
    }

    const auto z = apply_P_inverse(direction);
    if (!z.defined()) return rep;
    const auto az = apply_A(z);
    if (!az.defined()) return rep;

    torch::NoGradGuard no_grad;
    const auto err = az - direction;
    const double vn = direction.norm().to(torch::kCPU).item<double>();
    if (!(vn > 0.0)) return rep;

    rep.global_error = err.norm().to(torch::kCPU).item<double>() / vn;
    for (std::size_t i = 0; i < snapshot.layout.blocks.size() &&
                            i < rep.output_block_error.size(); ++i) {
        const auto& b = snapshot.layout.blocks[i];
        rep.output_block_error[i] =
            err.slice(0, b.start, b.start + b.size).norm().to(torch::kCPU).item<double>() / vn;
    }
    rep.ok = true;
    return rep;
}

// A vector supported on one block, so eps_q attributes to a named input.
inline torch::Tensor block_direction(const StateLayout& layout,
                                     const std::string& block_name,
                                     const torch::Tensor& like,
                                     const torch::Tensor& block_values)
{
    for (const auto& b : layout.blocks) {
        if (b.name != block_name) continue;
        if (block_values.numel() != b.size) {
            throw std::invalid_argument("block_direction: size mismatch for '" + block_name + "'");
        }
        auto v = torch::zeros_like(like);
        v.slice(0, b.start, b.start + b.size).copy_(block_values.to(like.options()));
        return v;
    }
    throw std::invalid_argument("block_direction: unknown block '" + block_name + "'");
}

}  // namespace sdirk3
}  // namespace wrf
