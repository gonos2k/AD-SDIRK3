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

// An operator together with the linearization it was captured at. A bare std::function pair
// cannot detect that A came from one generation and M^-1 from another, and a report that mixes
// them measures nothing -- so the generation travels WITH the operator and the evaluator refuses
// a mismatch. The snapshot already declared rhs_generation and preconditioner_generation; until
// this, nothing compared them to anything.
struct BoundLinearOperator {
    LinearOperator apply;
    uint64_t generation = 0;

    torch::Tensor operator()(const torch::Tensor& v) const { return apply(v); }
    explicit operator bool() const { return static_cast<bool>(apply); }
};

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

// Per-block scale S for the RESIDUAL space.
//
// The blocks carry different units -- the packed state is the VELOCITY basis (see
// wrf_sdirk3_state_layout.h: "ru/rv/rw" is a legacy misnomer for u/v/w in m s^-1), while mu is Pa
// and ph is m^2 s^-2. A plain Euclidean defect is therefore dominated by whichever block carries
// the biggest numbers, and switching Pa to hPa would change the "error" with no physics changing.
//
// RESIDUAL, not state: with right preconditioning A maps unknowns to residuals and P^-1 maps back,
// so the composite A P^-1 acts on the residual space, and S_R^-1 (A P^-1) S_R is what the scaled
// defect measures. Naming it for the state would invite filling it with state units.
//
// A unit change is a conjugation: in new units Atilde = D^-1 A D, vtilde = D^-1 v. With
// Stilde = D^-1 S the D's cancel exactly, so the scaled defect is invariant -- not approximately,
// identically.
struct ResidualScale {
    // Default is ZEROS, which is_valid() rejects. That is the whole point: a caller who forgets
    // to set a scale is REFUSED, while a caller who means to measure in raw storage units says so
    // with unscaled(). An all-ones default made those two cases indistinguishable, so the named
    // constructor documented a decision the type did not require anyone to make.
    std::array<double, 6> block_scale{};

    bool is_valid() const {
        for (double s : block_scale) {
            if (!(s > 0.0) || !std::isfinite(s)) return false;
        }
        return true;
    }

    static ResidualScale unscaled() {
        ResidualScale s;
        s.block_scale.fill(1.0);
        return s;
    }
};

// S^-1 as a vector over the full state, for elementwise weighting.
inline torch::Tensor inverse_scale_vector(const StateLayout& layout,
                                          const ResidualScale& scale,
                                          const torch::Tensor& like)
{
    torch::NoGradGuard no_grad;
    auto sinv = torch::ones_like(like);
    for (std::size_t i = 0; i < layout.blocks.size() && i < scale.block_scale.size(); ++i) {
        const auto& b = layout.blocks[i];
        // 1/s is finite in double but need not be in the TENSOR's dtype: a scale below ~1e-38
        // overflows to inf in float32, and inf weights turn the defect into NaN downstream.
        // Returning an undefined tensor makes the caller refuse rather than report that NaN.
        const double inv = 1.0 / scale.block_scale[i];
        sinv.slice(0, b.start, b.start + b.size).fill_(inv);
    }
    if (!torch::isfinite(sinv).all().item<bool>()) return torch::Tensor{};
    return sinv;
}

// Everything a probe needs to be reproducible, and to say which evaluation it describes.
struct LinearizationSnapshot {
    StateLayout layout;
    ImplicitActiveDomain active;
    ResidualScale scale;          // frozen HERE, so the number carries the units it was taken in

    double dt = 0.0;
    double gamma = 0.0;
    double h() const { return dt * gamma; }

    int physical_step = -1;
    int ark_stage = -1;
    int newton_iteration = -1;

    uint64_t rhs_generation = 0;
    uint64_t preconditioner_generation = 0;

    bool is_valid() const {
        // isfinite on dt/gamma because inf passes `> 0.0`, and generations because a receipt of
        // zero is what a caller who never set one leaves behind -- 0 == 0 compared equal, so the
        // receipt check passed for exactly the callers who supplied no receipt.
        return layout.is_valid() && scale.is_valid()
            && std::isfinite(dt) && dt > 0.0
            && std::isfinite(gamma) && gamma > 0.0
            && rhs_generation != 0 && preconditioner_generation != 0;
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

    // Defect in the blocks the implicit solve OWNS, versus what landed in blocks it does not.
    //
    // The second is NOT attributable to the preconditioner alone, which is why it is no longer
    // called "leakage": it contains A's own split-ownership error, P^-1's cross-block leakage,
    // and their composition. Separating those needs an A-only probe alongside this one.
    //
    // The blocks partition the vector, so active_error^2 + inactive_output_defect^2 ==
    // global_error^2 -- if that identity breaks, a block was dropped from the partition.
    double active_error = 0.0;
    double inactive_output_defect = 0.0;

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
    const BoundLinearOperator& apply_A,
    const BoundLinearOperator& apply_P_inverse,
    const torch::Tensor& direction)
{
    DirectionalDefectReport rep;
    if (!snapshot.is_valid() || !apply_A || !apply_P_inverse ||
        apply_A.generation != snapshot.rhs_generation ||
        apply_P_inverse.generation != snapshot.preconditioner_generation ||
        !direction.defined() ||
        // numel alone admits [1, N], whose block slices along dim 0 would be empty and whose
        // scale weighting would land on the wrong axis. The packed state is 1-D by contract.
        direction.dim() != 1 || !direction.is_floating_point() ||
        direction.numel() != snapshot.layout.total_size ||
        !operator_output_is_usable(direction, direction)) {
        return rep;   // ok stays false; callers must not read the numbers
    }

    // The guard wraps the OPERATOR CALLS, not just the reductions below. This is a diagnostic:
    // it must not build a graph, and it must not free or pollute buffers a live adjoint owns.
    torch::NoGradGuard no_grad;

    // The direction must live in the subspace the implicit solve OWNS. Asking how well P^-1
    // inverts A along a block the explicit side integrates has no answer -- the solve never
    // visits that direction -- and scoring it anyway produces a number about nothing.
    for (const auto& b : snapshot.layout.blocks) {
        if (snapshot.active.by_name(b.name)) continue;
        const double bn =
            direction.slice(0, b.start, b.start + b.size).norm().to(torch::kCPU).item<double>();
        if (bn > 0.0) return rep;
    }

    // Purity. These arrive as std::function; nothing stops one from writing through its argument
    // or carrying state (a fallback latch, a refinement counter, a lazy allocation). Both would
    // make this probe an observer that changes the solve it is measuring.
    const auto direction_before = direction.clone();
    const auto z = apply_P_inverse(direction);
    if (!operator_output_is_usable(z, direction)) return rep;
    if (!direction.equal(direction_before)) return rep;   // P^-1 wrote through its input

    const auto z_before = z.clone();
    const auto az = apply_A(z);
    if (!operator_output_is_usable(az, direction)) return rep;
    if (!z.equal(z_before)) return rep;                   // A wrote through its input

    // Repeatability: a stateful operator answers differently the second time, and a number that
    // cannot be reproduced is not a measurement.
    const auto z2 = apply_P_inverse(direction);
    if (!z2.defined() || !z2.equal(z)) return rep;
    const auto az2 = apply_A(z2);
    if (!az2.defined() || !az2.equal(az)) return rep;

    // Everything below is measured in SCALED coordinates. With the default unscaled S this is
    // elementwise multiplication by ones, so the numbers are exactly what they were before.
    const auto sinv = inverse_scale_vector(snapshot.layout, snapshot.scale, direction);
    if (!sinv.defined()) return rep;

    // Reductions in FP64. Element-wise finiteness does NOT make the norm finite: an FP32 L2 norm
    // overflows to inf on a large vector, and inf/inf is a NaN this would have reported as a
    // number. Promoting costs one copy on a diagnostic path and removes the whole failure mode.
    const auto err = ((az - direction) * sinv).to(torch::kFloat64);
    const auto v_s = (direction * sinv).to(torch::kFloat64);
    const double vn = v_s.norm().to(torch::kCPU).item<double>();
    if (!std::isfinite(vn) || !(vn > 0.0)) return rep;

    rep.global_error = err.norm().to(torch::kCPU).item<double>() / vn;
    if (!std::isfinite(rep.global_error)) return rep;
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
    rep.inactive_output_defect = std::sqrt(inactive_sq);

    // Which block the direction lives on, so the error can be attributed. Left empty when the
    // direction spans more than one block -- in_block_error then refuses rather than guessing.
    // Support is a property of the direction itself; a positive diagonal scale cannot create or
    // destroy it, so this reads the physical vector.
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
