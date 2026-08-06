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

// WHICH linearization this is. Not a counter -- an identity.
//
// Two independent counters (one for A, one for P^-1) cannot tell "the same linearization" from
// "two different solvers that happen to both be at 7". Every field below has a real producer in
// this codebase, which is why this is an identity and not a wish list:
//
//   stage_state_generation, coefficient_generation  <- UnifiedPreconditioner::StageBindingReceipt
//   mass_coordinate_mode, imex_split_mode, hevi_split <- SDIRK3Config
//   physical_step, ark_stage, newton_iteration      <- the Newton driver
//
// The two generations are DISTINCT on purpose, and production already learned why: a small state
// change updates mu_full_stage_ without triggering a coefficient rebuild, so coefficient_generation
// is not evidence that THIS state was bound. StageBindingReceipt says so in its own comment; the
// contract's earlier single "preconditioner_generation" had discarded that distinction.
//
// solver_id is a monotonic id, never a `this` pointer: addresses are recycled, and this project
// has already shipped one latch keyed on a recycled address.
struct LinearizationToken {
    uint64_t solver_id = 0;
    uint64_t stage_state_generation = 0;
    uint64_t coefficient_generation = 0;
    uint64_t rhs_generation = 0;
    uint64_t scale_generation = 0;

    int physical_step = -1;
    int ark_stage = -1;
    int newton_iteration = -1;

    int mass_coordinate_mode = -1;
    int imex_split_mode = -1;
    bool hevi_split = false;

    // Zero and negative are what an unset field leaves behind, so they are refused -- the same
    // hole the generation receipt had before it required nonzero.
    bool is_valid() const {
        return solver_id != 0 && stage_state_generation != 0 && coefficient_generation != 0
            && rhs_generation != 0 && scale_generation != 0
            && physical_step >= 0 && ark_stage >= 0 && newton_iteration >= 0
            && mass_coordinate_mode >= 0 && imex_split_mode >= 0;
    }

    bool operator==(const LinearizationToken& o) const {
        return solver_id == o.solver_id
            && stage_state_generation == o.stage_state_generation
            && coefficient_generation == o.coefficient_generation
            && rhs_generation == o.rhs_generation
            && scale_generation == o.scale_generation
            && physical_step == o.physical_step && ark_stage == o.ark_stage
            && newton_iteration == o.newton_iteration
            && mass_coordinate_mode == o.mass_coordinate_mode
            && imex_split_mode == o.imex_split_mode && hevi_split == o.hevi_split;
    }
    bool operator!=(const LinearizationToken& o) const { return !(*this == o); }
};

// An operator together with the linearization it was captured at. A bare std::function pair
// cannot detect that A came from one generation and M^-1 from another, and a report that mixes
// them measures nothing -- so the generation travels WITH the operator and the evaluator refuses
// a mismatch. The snapshot already declared rhs_generation and preconditioner_generation; until
// this, nothing compared them to anything.
struct BoundLinearOperator {
    LinearOperator apply;
    LinearizationToken token;

    // Repeatability proves the operator RETURNED the same thing twice. It does not prove the
    // operator did not CHANGE while doing so: a fallback latch, a refinement counter, a cache
    // generation or a lazy allocation can all advance while the output is bit-identical. A probe
    // that mutates the solver it measures is an observer effect, and this project has paid for
    // one before.
    //
    // So one of these must be supplied, and the report says which was used:
    //   state_digest set    -> the digest is SAMPLED around every call and must not move
    //   declared_stateless  -> purity is ASSERTED by the caller and nothing is sampled
    // Supplying neither is refused. "Nothing said" must not read as "checked".
    //
    // LIMIT, stated because the previous version overstated this. A digest is a CALLER'S CLAIM
    // about what it observes. A digest that ignores the operator entirely -- `[]{ return 0; }` --
    // never moves, so it passes, and from inside this function it is indistinguishable from a
    // faithful digest whose state simply did not change. The contract therefore reports that the
    // digest DID NOT MOVE; it cannot report that the operator is pure, and the field is named for
    // the former.
    std::function<uint64_t()> state_digest;
    bool declared_stateless = false;

    bool has_purity_evidence() const {
        return static_cast<bool>(state_digest) || declared_stateless;
    }

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

    // Matched against the token's scale_generation, so a scale frozen at one stage cannot be used
    // to weight a defect measured at another. A scale is part of the linearization, not a
    // free-floating preference.
    uint64_t generation = 0;

    bool is_valid() const {
        if (generation == 0) return false;
        for (double s : block_scale) {
            if (!(s > 0.0) || !std::isfinite(s)) return false;
        }
        return true;
    }

    static ResidualScale unscaled(uint64_t generation) {
        ResidualScale s;
        s.block_scale.fill(1.0);
        s.generation = generation;
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

    LinearizationToken token;

    double dt = 0.0;
    double gamma = 0.0;
    double h() const { return dt * gamma; }

    bool is_valid() const {
        // isfinite on dt/gamma because inf passes `> 0.0`. The token carries the rest: its
        // is_valid() refuses the unset-field values, and requiring the scale's generation to
        // match binds the weighting to the same linearization as the operators.
        return layout.is_valid() && scale.is_valid() && token.is_valid()
            && scale.generation == token.scale_generation
            && std::isfinite(dt) && dt > 0.0
            && std::isfinite(gamma) && gamma > 0.0;
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

    // True when BOTH operators supplied a state digest and none of the samples moved.
    //
    // NOT a proof of purity: a digest is the caller's claim about what it watches, and a constant
    // digest reads exactly like a faithful one here. The name says what was observed -- an earlier
    // version called this purity_verified, which asserted more than the check can deliver.
    bool state_digest_unchanged = false;

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
        !apply_A.has_purity_evidence() || !apply_P_inverse.has_purity_evidence() ||
        apply_A.token != snapshot.token || apply_P_inverse.token != snapshot.token ||
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

    // Purity, in three independent senses -- they fail differently and each needs its own check.
    //
    //   1. write-through : the operator mutated its ARGUMENT
    //   2. repeatability : the operator RETURNED something different the second time
    //   3. state purity  : the operator CHANGED ITSELF, returning the same thing either way
    //
    // (3) is invisible to (1) and (2). An operator that increments a counter and returns a clone
    // passes both and still moves the solver underneath the measurement.
    const auto digest_of = [](const BoundLinearOperator& op) -> uint64_t {
        return op.state_digest ? op.state_digest() : 0ULL;
    };
    const uint64_t a_digest_before = digest_of(apply_A);
    const uint64_t p_digest_before = digest_of(apply_P_inverse);
    bool digests_held = true;
    // Sampled after EVERY application, not once at the end: state that advances on the first call
    // and unwinds on the second returns to its initial value, and a before/after pair cannot see
    // that at all.
    const auto sample_digests = [&]() {
        digests_held = digests_held
            && digest_of(apply_A) == a_digest_before
            && digest_of(apply_P_inverse) == p_digest_before;
    };

    const auto direction_before = direction.clone();
    const auto z = apply_P_inverse(direction);
    sample_digests();
    if (!operator_output_is_usable(z, direction)) return rep;
    if (!direction.equal(direction_before)) return rep;   // P^-1 wrote through its input

    const auto z_before = z.clone();
    const auto az = apply_A(z);
    sample_digests();
    if (!operator_output_is_usable(az, direction)) return rep;
    if (!z.equal(z_before)) return rep;                   // A wrote through its input

    // Second application. Its outputs get the SAME validator as the first -- checking only
    // equality would let a second call return a wrong-dtype or non-finite tensor unremarked, so
    // long as it matched.
    //
    // Equality here is EXACT. That is the strongest contract and the right one while every caller
    // is a deterministic CPU operator; a live GPU adapter with atomic reductions is where this
    // must become a dtype-aware tolerance, and that belongs with the adapter that needs it.
    const auto z2 = apply_P_inverse(direction);
    sample_digests();
    if (!operator_output_is_usable(z2, direction) || !z2.equal(z)) return rep;
    const auto az2 = apply_A(z2);
    sample_digests();
    if (!operator_output_is_usable(az2, direction) || !az2.equal(az)) return rep;

    if (!digests_held) return rep;                        // an operator changed ITSELF
    rep.state_digest_unchanged =
        static_cast<bool>(apply_A.state_digest) && static_cast<bool>(apply_P_inverse.state_digest);

    // Everything below is measured in SCALED coordinates. With the default unscaled S this is
    // elementwise multiplication by ones, so the numbers are exactly what they were before.
    const auto sinv = inverse_scale_vector(snapshot.layout, snapshot.scale, direction);
    if (!sinv.defined()) return rep;

    // FP64 for the ARITHMETIC, not only the reduction. Casting the result of
    // `(az - direction) * sinv` would promote a number whose cancellation, overflow and
    // underflow had already happened in the source dtype -- the promotion has to come first to
    // mean anything. Element-wise finiteness also does not make a REDUCTION finite: an FP32 L2
    // norm overflows to inf, and inf/inf is a NaN this would have reported as a number.
    const auto v64 = direction.to(torch::kFloat64);
    const auto sinv64 = sinv.to(torch::kFloat64);
    const auto err = (az.to(torch::kFloat64) - v64) * sinv64;
    const auto v_s = v64 * sinv64;
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
