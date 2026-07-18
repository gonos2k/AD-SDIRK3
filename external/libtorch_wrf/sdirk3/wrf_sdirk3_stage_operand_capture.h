#pragma once
// PR 9E: opt-in, diagnosis-only capture of the STAGE OPERAND provenance chain
//   source derivative -> ARK history -> fast RHS -> Newton residual -> FGMRES
// for the record stage=3 / Newton iter=0 operand (the second implicit solve,
// post-W-damping-parity / post-preconditioner-consistency). This is the
// generic capture + closure machinery; the production hooks and the reports
// land in later commits of this PR.
//
// Safety contract — inherited verbatim from the proven RwTermCapture pattern
// (PR 9B/9B.1/9B.2):
//  - the slot is THREAD-LOCAL, so a capture armed on one thread can never
//    observe or race with evaluations on other threads (cross-thread
//    contamination is impossible by construction — this is the supported
//    single-tile diagnostic path);
//  - arming is RAII (StageOperandCaptureScope): an exception mid-capture
//    unwinds through the destructor, which disarms AND clears the slot, so no
//    tensors leak past the scope and later plain evaluations never append to a
//    stale armed slot;
//  - nested arming FAILS CLOSED (armed_ok() == false); the caller emits a
//    stable marker instead of mixing two captures;
//  - take() relinquishes ownership FIRST, so a later scope armed on the same
//    thread while this object is still alive is never disarmed/cleared by it;
//  - the inventory validator is the single authority for missing / duplicate /
//    undefined / unknown / non-finite detection, shared by the production
//    checker and the standing contract.
//
// Every captured leaf carries an OperandLeaf record (identity + tensor), so
// the closures can be computed WITHOUT re-evaluating any production RHS: the
// capture only OBSERVES the tensors production already built.
#include <torch/torch.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wrf {
namespace sdirk3 {

// The six tendency blocks of the SDIRK3 state, in packed order.
enum class OperandBlock : int8_t {
    Ru = 0,  // u-momentum
    Rv = 1,  // v-momentum
    Rw = 2,  // w-momentum
    Ph = 3,  // geopotential (phi)
    T = 4,   // potential temperature
    Mu = 5,  // column mass
    Unknown = -1,
};

// Which split/role a captured leaf belongs to.
enum class OperandSplit : int8_t {
    Slow = 0,      // explicit derivative leaf (k_slow provenance)
    Fast = 1,      // implicit fast-RHS leaf (F_fast provenance)
    Residual = 2,  // Newton residual / FGMRES-RHS leaf
    History = 3,   // ARK stage-history increment leaf
    Unknown = -1,
};

// How a leaf contributes to its block sum.
enum class OperandTermKind : int8_t {
    Additive = 0,       // a physical term added into the tendency
    Transform = 1,      // a mask/overwrite/padding/BC step (before/after/delta)
    NewtonDefect = 2,   // K_final - F_fast(U_eval_final) of a source stage
    ResidualOffset = 3, // any production residual offset beyond K - F(U)
    Unknown = -1,
};

// Context stamped on a capture scope (which operand it is provenance for).
struct OperandCaptureContext {
    int timestep = -1;
    int target_stage = -1;    // e.g. 3 for the second implicit solve
    int source_stage = -1;    // which prior stage produced this derivative
    int newton_iter = -1;     // 0 for the initial operand
    OperandSplit split = OperandSplit::Unknown;
    const char* evaluation_role = "";  // "history"/"fast_rhs"/"residual"/...
};

struct OperandLeaf {
    std::string name;             // stable per-call-site leaf id
    OperandBlock block = OperandBlock::Unknown;
    OperandSplit split = OperandSplit::Unknown;
    OperandTermKind kind = OperandTermKind::Additive;
    bool enabled = true;          // false = family exists but inactive here
    torch::Tensor tensor;         // the exact production tensor (observed)
    // For Transform leaves: before/after so delta = after - before is exact.
    torch::Tensor before;
    torch::Tensor after;
};

struct StageOperandCapture {
    bool armed = false;
    OperandCaptureContext ctx;
    std::vector<OperandLeaf> leaves;
    void add(const OperandLeaf& leaf) {
        if (armed) leaves.push_back(leaf);
    }
    void add_additive(const char* name, OperandBlock block, OperandSplit split,
                      const torch::Tensor& t, bool enabled = true) {
        if (!armed) return;
        OperandLeaf l;
        l.name = name;
        l.block = block;
        l.split = split;
        l.kind = OperandTermKind::Additive;
        l.enabled = enabled;
        l.tensor = t;
        leaves.push_back(std::move(l));
    }
    void add_transform(const char* name, OperandBlock block, OperandSplit split,
                       const torch::Tensor& before, const torch::Tensor& after) {
        if (!armed) return;
        OperandLeaf l;
        l.name = name;
        l.block = block;
        l.split = split;
        l.kind = OperandTermKind::Transform;
        l.before = before;
        l.after = after;
        l.tensor = after - before;  // delta
        leaves.push_back(std::move(l));
    }
    void reset() {
        leaves.clear();
        ctx = OperandCaptureContext{};
    }
};

inline StageOperandCapture& stage_operand_capture_slot() {
    static thread_local StageOperandCapture t_slot;
    return t_slot;
}

// RAII arm/disarm mirroring RwTermCaptureScope. Nested arm fails closed.
class StageOperandCaptureScope {
  public:
    explicit StageOperandCaptureScope(const OperandCaptureContext& ctx)
        : slot_(stage_operand_capture_slot()) {
        if (slot_.armed) return;  // nested arm: fail closed, caller checks armed_ok()
        slot_.reset();
        slot_.ctx = ctx;
        slot_.armed = true;
        owned_ = true;
    }
    ~StageOperandCaptureScope() {
        if (owned_) {
            slot_.armed = false;
            slot_.reset();
        }
    }
    StageOperandCaptureScope(const StageOperandCaptureScope&) = delete;
    StageOperandCaptureScope& operator=(const StageOperandCaptureScope&) = delete;

    bool armed_ok() const { return owned_; }

    // Disarm and move the captured leaves out. Ownership is relinquished FIRST
    // (as in RwTermCaptureScope::take), so a later scope on the same thread is
    // never disarmed/cleared by this one.
    std::vector<OperandLeaf> take() {
        if (!owned_) return {};
        owned_ = false;
        slot_.armed = false;
        auto out = std::move(slot_.leaves);
        slot_.reset();
        return out;
    }

  private:
    StageOperandCapture& slot_;
    bool owned_ = false;
};

// Inventory validation: the single authority. `required` is the set of leaf
// ids that MUST be present (defined, unique) for this capture role; every
// captured leaf must be either in `required` or in `optional`, and every
// tensor (or transform before/after) must be DEFINED and FINITE. Returns an
// empty string when clean; otherwise a comma-joined reason string for the
// SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE marker.
//
// Callers MUST run this on the RAW captured leaves BEFORE any reduction that
// would throw on an undefined tensor.
inline std::string validate_stage_operand_inventory(
    const std::vector<OperandLeaf>& leaves,
    const std::vector<std::string>& required,
    const std::vector<std::string>& optional) {
    std::string reason;
    auto append = [&](const std::string& r) {
        if (!reason.empty()) reason += ",";
        reason += r;
    };
    auto is_known = [&](const std::string& n) {
        for (const auto& r : required)
            if (r == n) return true;
        for (const auto& o : optional)
            if (o == n) return true;
        return false;
    };
    struct Counts {
        int total = 0;
        int defined = 0;
    };
    auto stats_of = [&](const std::string& name) {
        Counts c;
        for (const auto& l : leaves) {
            if (l.name != name) continue;
            ++c.total;
            const bool tdef =
                (l.kind == OperandTermKind::Transform)
                    ? (l.before.defined() && l.after.defined())
                    : l.tensor.defined();
            if (tdef) ++c.defined;
        }
        return c;
    };
    for (const auto& name : required) {
        const Counts c = stats_of(name);
        if (c.total == 0) append("missing:" + name);
        if (c.total > 1) append("duplicate:" + name);
        if (c.total == 1 && c.defined == 0) append("undefined:" + name);
    }
    for (const auto& l : leaves) {
        if (!is_known(l.name)) append("unknown:" + l.name);
    }
    // Non-finite fail-close (a diagnostic reduction; the caller runs this on
    // the enabled path only).
    {
        torch::NoGradGuard no_grad;
        for (const auto& l : leaves) {
            if (!l.tensor.defined()) continue;
            if (!l.tensor.isfinite().all().item<bool>())
                append("nonfinite:" + l.name);
        }
    }
    return reason;
}

// FP64 closure helper: ||sum(leaves of `block`) - target|| relative error,
// accumulated in double. Additive and Transform(delta) leaves both contribute
// their `tensor`. Returns {abs_err, rel_err, max_abs_err}. All reductions are
// diagnostic (NoGradGuard); operate on detached float64 clones.
struct ClosureResult {
    double abs_err = 0.0;
    double rel_err = 0.0;
    double max_abs_err = 0.0;
    bool finite = true;
};

inline ClosureResult closure_of(
    const std::vector<const torch::Tensor*>& leaves,
    const torch::Tensor& target) {
    torch::NoGradGuard no_grad;
    ClosureResult r;
    torch::Tensor acc;
    for (const auto* t : leaves) {
        if (!t || !t->defined()) continue;
        auto d = t->detach().to(torch::kFloat64);
        acc = acc.defined() ? acc + d : d;
    }
    auto tgt = target.detach().to(torch::kFloat64);
    if (!acc.defined()) acc = torch::zeros_like(tgt);
    auto diff = acc - tgt;
    r.abs_err = diff.norm().item<double>();
    r.max_abs_err = diff.abs().max().item<double>();
    double tn = tgt.norm().item<double>();
    r.rel_err = r.abs_err / std::max(tn, 1e-300);
    r.finite = diff.isfinite().all().item<bool>() &&
               acc.isfinite().all().item<bool>();
    return r;
}

}  // namespace sdirk3
}  // namespace wrf
