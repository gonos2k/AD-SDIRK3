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
#include <cmath>
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

// An operator handed to this file is a std::function -- there is nothing stopping it from
// returning the wrong shape, dtype, device, or a NaN. And `A P^-1 v - v` BROADCASTS, so a
// wrong-shaped return does not throw: it produces a plausible number from a broken operator.
// Refuse instead.
inline bool operator_output_is_usable(const torch::Tensor& out, const torch::Tensor& like) {
    torch::NoGradGuard no_grad;
    return out.defined()
        && out.sizes() == like.sizes()
        && out.scalar_type() == like.scalar_type()
        && out.device() == like.device()
        && torch::isfinite(out).all().item<bool>();
}

// eps_q and where the error lands, FOR ONE DIRECTION. output_block_error[i] follows
// layout.blocks order.
//
// The name says "directional" and "defect" because both were overclaimed before: this is not a
// verdict on the preconditioner (one direction can be lucky), and larger is worse (a "quality"
// reads as larger-is-better).
struct DirectionalDefectReport {
    std::string input_block;
    double global_error = 0.0;                      // || A P^-1 v - v || / || v ||
    std::array<double, 6> output_block_error{};     // per-block share of that residual

    // Defect in the blocks the implicit solve OWNS, versus what leaked into blocks it does not.
    // These are different failures: the first is a wrong inverse, the second is M polluting a
    // channel the explicit side is integrating. The blocks partition the vector, so
    // active_error^2 + inactive_leakage^2 == global_error^2 -- if that identity breaks, a block
    // was dropped from the partition.
    double active_error = 0.0;
    double inactive_leakage = 0.0;

    bool ok = false;

    // Error concentrated OUTSIDE the input block means the cross-block model is wrong, which a
    // scalar per-block number cannot distinguish from a bad in-block inverse.
    //
    // THROWS when the direction was not supported on exactly one block. The first version
    // returned 0.0 in that case, and since the evaluator never set input_block,
    // it returned 0.0 ALWAYS -- so every attribution assertion written against it passed
    // vacuously. An unanswerable query must not produce a plausible number.
    double in_block_error(const StateLayout& layout) const {
        if (input_block.empty()) {
            throw std::logic_error(
                "in_block_error: the direction was not supported on exactly one block, so there "
                "is no input block to attribute to");
        }
        for (std::size_t i = 0; i < layout.blocks.size() && i < output_block_error.size(); ++i) {
            if (layout.blocks[i].name == input_block) return output_block_error[i];
        }
        throw std::logic_error("in_block_error: input_block '" + input_block +
                               "' is not in this layout");
    }
};

// THE judgement, in one direction. Pure: takes the operators as arguments, returns a value,
// touches nothing.
inline DirectionalDefectReport evaluate_directional_right_preconditioner_defect(
    const LinearizationSnapshot& snapshot,
    const LinearOperator& apply_A,
    const LinearOperator& apply_P_inverse,
    const torch::Tensor& direction)
{
    DirectionalDefectReport rep;
    if (!snapshot.is_valid() || !direction.defined() ||
        direction.numel() != snapshot.layout.total_size ||
        !operator_output_is_usable(direction, direction)) {
        return rep;   // ok stays false; callers must not read the numbers
    }

    const auto z = apply_P_inverse(direction);
    if (!operator_output_is_usable(z, direction)) return rep;
    const auto az = apply_A(z);
    if (!operator_output_is_usable(az, direction)) return rep;

    torch::NoGradGuard no_grad;
    const auto err = az - direction;
    const double vn = direction.norm().to(torch::kCPU).item<double>();
    if (!(vn > 0.0)) return rep;

    rep.global_error = err.norm().to(torch::kCPU).item<double>() / vn;
    double active_sq = 0.0, inactive_sq = 0.0;
    for (std::size_t i = 0; i < snapshot.layout.blocks.size() &&
                            i < rep.output_block_error.size(); ++i) {
        const auto& b = snapshot.layout.blocks[i];
        const double e =
            err.slice(0, b.start, b.start + b.size).norm().to(torch::kCPU).item<double>() / vn;
        rep.output_block_error[i] = e;
        (snapshot.active.by_name(b.name) ? active_sq : inactive_sq) += e * e;
    }
    rep.active_error = std::sqrt(active_sq);
    rep.inactive_leakage = std::sqrt(inactive_sq);

    // Which block the direction lives on, so the error can be attributed. Left empty when the
    // direction spans more than one block -- in_block_error then refuses rather than guessing.
    int n_support = 0;
    for (const auto& b : snapshot.layout.blocks) {
        const double bn =
            direction.slice(0, b.start, b.start + b.size).norm().to(torch::kCPU).item<double>();
        if (bn > 0.0) { ++n_support; rep.input_block = b.name; }
    }
    if (n_support != 1) rep.input_block.clear();

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
