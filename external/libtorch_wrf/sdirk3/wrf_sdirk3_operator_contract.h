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
#include "wrf_sdirk3_wrms_norm.h"

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

    // Which RHS the operator was formed from (the RhsMode enum in wrf_sdirk3_tile_unified.h,
    // carried as an int so this header keeps its light dependencies -- the same way the two modes
    // above are), the time coefficient it was formed with, and WHICH PARTITION of the state:
    // two layouts can share a total size and cut it differently.
    int rhs_mode = -1;
    double h = 0.0;                    // dt * gamma
    uint64_t layout_digest = 0;

    // DELIBERATELY OPTIONAL, and the only field that is. With flexible preconditioning P_j varies
    // per FGMRES iteration, so an operator judged at iteration j is not the one at iteration k --
    // but a token can also identify a whole solve, and -1 says so. It is still compared, so the
    // two kinds never test equal.
    int krylov_iteration = -1;

    // NOT ADDED: OperatorKind and StageUnknownForm. The review asks for them and the argument is
    // sound, but no such enum exists in this codebase -- inventing the values here would shape an
    // identity by guesswork, which is the objection that kept the sampled-max aggregator and the
    // first ScalePolicy out. They belong with whatever first needs to distinguish those cases.

    // Zero and negative are what an unset field leaves behind, so they are refused -- the same
    // hole the generation receipt had before it required nonzero.
    bool is_valid() const {
        return solver_id != 0 && stage_state_generation != 0 && coefficient_generation != 0
            && rhs_generation != 0 && scale_generation != 0
            && physical_step >= 0 && ark_stage >= 0 && newton_iteration >= 0
            && mass_coordinate_mode >= 0 && imex_split_mode >= 0
            && rhs_mode >= 0 && layout_digest != 0
            && std::isfinite(h) && h > 0.0;
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
            && imex_split_mode == o.imex_split_mode && hevi_split == o.hevi_split
            && rhs_mode == o.rhs_mode && layout_digest == o.layout_digest
            && h == o.h && krylov_iteration == o.krylov_iteration;
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

// The active domain DERIVED from the mode, instead of a caller remembering which one to pick.
//
// hevi_vertical_fast() is a fixed answer that says nothing about when it applies, and the default
// domain is all-six-active -- so a HEVI caller who forgets to set it gets a confident full-domain
// verdict rather than an error. That is the fail-open shape this contract exists to remove.
//
// Only the combinations whose domain has actually been established are answered. Everything else
// THROWS: an unrecognised split is a question about a partition nobody has written down, and
// guessing all-active for it is exactly the wrong default.
inline ImplicitActiveDomain make_implicit_active_domain(int mass_coordinate_mode,
                                                        int imex_split_mode,
                                                        bool hevi_split)
{
    TORCH_CHECK(mass_coordinate_mode >= 0 && imex_split_mode >= 0,
                "make_implicit_active_domain: mode must be set (got mass=",
                mass_coordinate_mode, ", split=", imex_split_mode, ")");
    if (hevi_split) {
        // EXACT combinations, not ranges. `mass >= 1` admitted the partial diagnostic modes
        // (DiagnosticOmegaOnly = 2, DiagnosticMuOnly = 3), whose mu-row ownership is NOT
        // established -- each corrects only one half of the mass coordinate, so whether the mass
        // row leaves the implicit set is a question nobody has answered. And any imex_split_mode
        // was accepted; the vertical-fast partition has only ever been exercised under ARK324
        // (mode 3), the operational configuration HEVI gates on.
        //
        // A resolver exists to refuse the combinations it cannot answer; admitting them returns a
        // confident partition for an unestablished question, which is the fail-open this function
        // was written to remove.
        TORCH_CHECK(mass_coordinate_mode == 1,
                    "make_implicit_active_domain: HEVI's vertical-fast domain is established for "
                    "WRFParity (mass_coordinate_mode=1) only; mode ", mass_coordinate_mode,
                    mass_coordinate_mode >= 2
                        ? " is a PARTIAL diagnostic mode whose mu-row ownership is not established"
                        : " is the legacy Omega, where the mass row still couples through w");
        TORCH_CHECK(imex_split_mode == 3,
                    "make_implicit_active_domain: the vertical-fast partition is established "
                    "under ARK324 (imex_split_mode=3) only; got ", imex_split_mode);
        return ImplicitActiveDomain::hevi_vertical_fast();
    }
    return ImplicitActiveDomain{};   // no split: the implicit solve owns every block
}

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
// Which weighting this scale carries. Both values have a real producer, which is why this is an
// enum and not a hopeful abstraction: SyntheticUniform is what the contract's own unit-conversion
// algebra uses, and FrozenErrorWeights is the per-element ewt production's stage gate already
// computes.
enum class ScalePolicy {
    SyntheticUniform,     // one positive scalar per block
    FrozenErrorWeights,   // per-element ewt, frozen at this linearization
};

struct ResidualScale {
    ScalePolicy policy = ScalePolicy::SyntheticUniform;

    // SyntheticUniform. Default is ZEROS, which is_valid() rejects. That is the whole point: a
    // caller who forgets to set a scale is REFUSED, while a caller who means to measure in raw
    // storage units says so with unscaled(). An all-ones default made those two cases
    // indistinguishable, so the named constructor documented a decision the type did not require
    // anyone to make.
    std::array<double, 6> block_scale{};

    // FrozenErrorWeights. The SAME vector production weights residuals by --
    // error_weights_packed() in wrf_sdirk3_wrms_norm.h -- so this contract and the stage gate
    // cannot disagree about what "small" means. Six block scalars cannot express it: ewt is
    // rtol*|y_ref| + atol pointwise, so it varies within a block by orders across a profile.
    torch::Tensor weights;

    // Matched against the token's scale_generation, so a scale frozen at one stage cannot be used
    // to weight a defect measured at another. A scale is part of the linearization, not a
    // free-floating preference.
    uint64_t generation = 0;

    bool is_valid() const {
        if (generation == 0) return false;
        if (policy == ScalePolicy::FrozenErrorWeights) {
            torch::NoGradGuard no_grad;
            return weights.defined() && weights.dim() == 1 && weights.is_floating_point()
                && weights.numel() > 0
                && torch::isfinite(weights).all().item<bool>()
                && (weights > 0).all().item<bool>();
        }
        for (double s : block_scale) {
            if (!(s > 0.0) || !std::isfinite(s)) return false;
        }
        return true;
    }

    static ResidualScale unscaled(uint64_t generation) {
        ResidualScale s;
        s.policy = ScalePolicy::SyntheticUniform;
        s.block_scale.fill(1.0);
        s.generation = generation;
        return s;
    }

    static ResidualScale from_error_weights(const torch::Tensor& ewt, uint64_t generation) {
        ResidualScale s;
        s.policy = ScalePolicy::FrozenErrorWeights;
        // A PRIVATE copy, so "frozen" is a property and not a name. A Tensor is a handle: storing
        // the caller's handle leaves them holding the same storage, free to mutate it after the
        // freeze and between validation and use. detach() because a scale must carry no graph --
        // ewt is built from the state, and a weighting that stays graph-connected would keep that
        // graph alive and let a diagnostic contribute gradient.
        if (ewt.defined()) {
            torch::NoGradGuard no_grad;
            s.weights = ewt.detach().clone();
        }
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
    if (scale.policy == ScalePolicy::FrozenErrorWeights) {
        // Frozen at another grid is a mismatch, not something to broadcast around.
        if (scale.weights.numel() != like.numel()) return torch::Tensor{};
        auto w = 1.0 / scale.weights.to(like.options());
        if (!torch::isfinite(w).all().item<bool>()) return torch::Tensor{};
        return w;
    }
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

// Packed block sizes FROM the layout authority. StateLayout::from_grid_dims() already computes
// these six sizes with OVERFLOW-CHECKED multiplication; a second expression elsewhere is not just
// a duplicate, it silently loses that checking. One source, derived.
inline PackedBlockSizes to_packed_block_sizes(const StateLayout& layout) {
    TORCH_CHECK(layout.is_valid(), "to_packed_block_sizes: layout is not valid");
    PackedBlockSizes pb;
    int64_t* slot[6] = {&pb.u, &pb.v, &pb.w, &pb.ph, &pb.t, &pb.mu};
    TORCH_CHECK(layout.blocks.size() == 6,
                "to_packed_block_sizes: expected six packed blocks");
    for (std::size_t i = 0; i < 6; ++i) *slot[i] = layout.blocks[i].size;
    return pb;
}

// WHICH stage a weighting was frozen for.
//
// ark_stage alone is not enough: ARK stage 2 recurs at EVERY physical step, so a source frozen at
// step 1 would be accepted at step 100 -- a different linearization wearing the same label.
// stage_state_generation increments on every bind, so it separates them. A physical step number
// would say it more directly, but none is threaded to this layer (DiagnosticContext says so in
// its own comment), and inventing one to look thorough would be worse than using the counter that
// actually exists.
// WHERE a weighting was taken, because there are two different metrics here and they are not
// interchangeable.
//
//   StageEntry      -- the reference state the Newton solve STARTS from. This is what a probe
//                      inside the solve can see, and it is the metric for judging A*P^-1 at a
//                      Newton linearization.
//   StageAcceptance -- the reference state the stage ENDS at (U_new), which is what the
//                      convergence gate weights by.
//
// The error weight is e_i(Y) = max(rtol*|Y_i| + atol_q, floor), so Y1 != Y2 gives e(Y1) != e(Y2).
// "Same formula and config" is therefore NOT "same metric", and letting one object serve both
// points would silently equate them. Naming the point makes a caller say which it wants.
enum class WeightingPoint {
    StageEntry,           // the state the stage STARTS from
    NewtonLinearization,  // Y_n = B + h*K_n, where the operator FGMRES solves is formed
    StageAcceptance,      // U_new, what the convergence gate weights by
};

struct StageIdentity {
    uint64_t solver_id = 0;

    // A MONOTONIC capture sequence, not the preconditioner's bind generation.
    //
    // The bind generation was the obvious choice and it does not work: the caller stamps the
    // identity BEFORE solve_stage, and the stage bind inside the solve increments that counter --
    // so the stamped value never equalled the value at use, and the check could never pass.
    // Measured by running it: the probe produced zero output. A guard that cannot pass is as
    // useless as one that always passes.
    //
    // A capture sequence is what the check actually needs. It distinguishes a leftover from a
    // previous stage AND from the same stage of a previous step, and the consumer marks it used,
    // so one capture serves exactly one solve.
    uint64_t capture_seq = 0;

    int ark_stage = -1;
    WeightingPoint point = WeightingPoint::StageEntry;

    bool is_valid() const {
        return solver_id != 0 && capture_seq != 0 && ark_stage >= 0;
    }
    bool operator==(const StageIdentity& o) const {
        return solver_id == o.solver_id
            && capture_seq == o.capture_seq
            && ark_stage == o.ark_stage
            && point == o.point;
    }
    bool operator!=(const StageIdentity& o) const { return !(*this == o); }
};

// The stage gate's weighting, CAPTURED rather than referenced.
//
// An earlier version of this carried y_ref, blocks and cfg and built the weights later. A Tensor
// is a handle, so holding y_ref left the caller able to mutate the reference after the freeze --
// "frozen" as a name again. Capturing immediately into ResidualScale (whose from_error_weights
// takes a detached private copy) removes the handle, and with it the need to carry blocks and cfg
// at all. Fewer fields, and the immutability is structural rather than promised.
struct FrozenStageWeights {
    ResidualScale scale;
    StageIdentity stage;

    // Which Newton iteration this capture was taken at. -1 for a stage-entry capture; the
    // NewtonLinearization re-capture stamps the live index so the emitted record can attribute a
    // Krylov trajectory to its Newton step.
    int newton_iter = -1;

    // The config the weights were built with, kept so a consumer can re-capture at a DIFFERENT
    // reference state without inventing its own rules. The reference state is deliberately NOT
    // kept -- holding it was the freeze defect -- but the config is a small value and losing it
    // would force the only other option: a second copy of the gate's tolerances.
    WRMSNormConfig cfg;

    bool usable(const StageIdentity& now) const {
        return scale.is_valid() && stage.is_valid() && stage == now;
    }
};

// Build the weights the stage gate would build, at this state, and freeze them.
inline FrozenStageWeights capture_stage_weights(const torch::Tensor& y_ref,
                                                const StateLayout& layout,
                                                const WRMSNormConfig& cfg,
                                                const StageIdentity& stage)
{
    FrozenStageWeights w;
    if (!y_ref.defined() || !layout.is_valid() || !stage.is_valid()) return w;
    torch::NoGradGuard no_grad;
    const auto flat = y_ref.detach().reshape({-1});
    if (flat.numel() != layout.total_size) return w;
    w.scale = ResidualScale::from_error_weights(
        error_weights_packed(flat, to_packed_block_sizes(layout), cfg),
        stage.capture_seq);
    w.stage = stage;
    w.cfg = cfg;
    return w;
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

    // The identity defect DECOMPOSED, because eps alone cannot judge Krylov difficulty:
    // B = cI has global_error = |c-1| -- arbitrarily large -- and GMRES solves cI x = b in ONE
    // step, its minimal polynomial having degree 1. What actually costs Krylov directions is the
    // part no scalar removes.
    //
    //   gain_alpha   = <v,Bv>_W / <v,v>_W            the best scalar multiple of v
    //   shape_defect = ||W(Bv - alpha v)|| / ||Wv||  the remainder after removing it
    //   cosine       = <v,Bv>_W / (||Wv|| ||WBv||)
    //
    // A pure gain error (alpha != 1, shape ~ 0, cos ~ 1) is one Krylov direction; a large shape
    // defect is rotation into other blocks. global_error stays as the identity defect -- the
    // right TARGET for A P^-1 ~ I -- but not, by itself, the verdict.
    double gain_alpha = 0.0;
    double shape_defect = 0.0;
    double cosine = 0.0;

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

    // The weighting is built BEFORE any operator runs. Its size match against this direction was
    // previously discovered inside inverse_scale_vector, which is called after four applications
    // -- so a scale frozen for a different grid perturbed a live solver four times before the
    // query was judged unanswerable. Refusing an unanswerable query must cost nothing.
    const auto sinv = inverse_scale_vector(snapshot.layout, snapshot.scale, direction);
    if (!sinv.defined()) return rep;

    // ||S^-1 v|| too. It depends on the SCALE AND THE DIRECTION ONLY -- no operator is involved --
    // so its check never belonged after execution, and leaving one sibling behind is how the size
    // mismatch above hid in the first place. Everything computable without an operator is settled
    // here.
    //
    // Reachable, not theoretical: weights of 1e300 are finite and positive (is_valid passes) and
    // 1/w = 1e-300 is finite (the vector builds), yet the SQUARES underflow inside the norm, so
    // ||S^-1 v|| is exactly 0 and the defect has no denominator. That scale annihilates the
    // direction, and it used to cost four operator applications to find out.
    const auto v64 = direction.to(torch::kFloat64);
    const auto sinv64 = sinv.to(torch::kFloat64);
    const auto v_s = v64 * sinv64;
    const double vn = v_s.norm().to(torch::kCPU).item<double>();
    if (!std::isfinite(vn) || !(vn > 0.0)) return rep;

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
    // FP64 for the ARITHMETIC, not only the reduction. Casting the result of
    // `(az - direction) * sinv` would promote a number whose cancellation, overflow and
    // underflow had already happened in the source dtype -- the promotion has to come first to
    // mean anything. Element-wise finiteness also does not make a REDUCTION finite: an FP32 L2
    // norm overflows to inf, and inf/inf is a NaN this would have reported as a number.
    const auto err = (az.to(torch::kFloat64) - v64) * sinv64;

    rep.global_error = err.norm().to(torch::kCPU).item<double>() / vn;
    if (!std::isfinite(rep.global_error)) return rep;

    {
        // Weighted decomposition of B = A P^-1 along this direction (see the field comments).
        const auto bw = az.to(torch::kFloat64) * sinv64;
        const double vv = v_s.dot(v_s).to(torch::kCPU).item<double>();
        const double vb = v_s.dot(bw).to(torch::kCPU).item<double>();
        rep.gain_alpha = vb / vv;
        rep.shape_defect =
            (bw - rep.gain_alpha * v_s).norm().to(torch::kCPU).item<double>() / vn;
        const double bn = bw.norm().to(torch::kCPU).item<double>();
        rep.cosine = (bn > 0.0) ? vb / (vn * bn) : 0.0;
    }
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
