#pragma once
// PR 9B commit 2 / PR 9B.1: opt-in capture of the rw (w-momentum) tendency
// terms inside one RHS evaluation, for the term-level tangent bisection.
//
// PR 9B.1 safety contract (review follow-up):
//  - The slot is THREAD-LOCAL: a capture armed on one thread can never
//    observe or race with RHS evaluations on other threads (each thread has
//    its own slot; cross-thread contamination is impossible by
//    construction). The capture therefore only sees terms computed on the
//    arming thread — exactly the supported single-tile diagnostic path.
//  - Arming is RAII (RwTermCaptureScope): compute_rhs throwing mid-capture
//    unwinds through the scope destructor, which disarms AND clears the
//    slot — no tensors are retained past the scope and later plain RHS
//    calls never append to a stale armed slot.
//  - Nested arming FAILS CLOSED: an inner scope refuses to arm
//    (armed_ok() == false) and the caller emits a stable marker instead of
//    mixing two captures.
//  - validate_rw_term_inventory() is the single authority for the expected
//    term inventory (missing/duplicate/unknown detection) shared by the
//    checker and the standing contract test.
//
// Tensors are stored AS-IS (not detached): during a forward-mode dual
// evaluation the caller unpacks the tangents (via take()) while its
// DualLevelGuard is still alive, then the scope ends.
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
    static thread_local RwTermCapture t_slot;
    return t_slot;
}

// RAII arm/disarm. Constructor arms THIS THREAD's slot (refusing if it is
// already armed — nested capture); destructor disarms and clears whatever
// was not taken, so an exception during the captured evaluation cannot leak
// an armed slot or retained tensors.
class RwTermCaptureScope {
  public:
    RwTermCaptureScope() : slot_(rw_term_capture_slot()) {
        if (slot_.armed) return;  // nested arm: fail closed, caller checks armed_ok()
        slot_.reset();
        slot_.armed = true;
        owned_ = true;
    }
    ~RwTermCaptureScope() {
        if (owned_) {
            slot_.armed = false;
            slot_.reset();
        }
    }
    RwTermCaptureScope(const RwTermCaptureScope&) = delete;
    RwTermCaptureScope& operator=(const RwTermCaptureScope&) = delete;

    bool armed_ok() const { return owned_; }

    // Disarm and move the captured terms out (normal completion path).
    // PR 9B.2 (P1-1): ownership is relinquished FIRST — after take() this
    // scope's destructor is a no-op, so a later scope armed on the same
    // thread (while this object is still alive) can never be disarmed or
    // cleared by it.
    std::vector<std::pair<std::string, torch::Tensor>> take() {
        if (!owned_) return {};
        owned_ = false;
        slot_.armed = false;
        auto out = std::move(slot_.terms);
        slot_.reset();
        return out;
    }

  private:
    RwTermCapture& slot_;
    bool owned_ = false;
};

// Expected inventory of one ImplicitOnly rw capture. w_damp_padded is
// present only when the implicit W-damping gate is active. Returns an empty
// string when the inventory is complete, duplicate-free, and every captured
// tensor is DEFINED; otherwise a human-readable reason ("missing:<name>",
// "duplicate:<name>", "undefined:<name>", "unknown:<name>", comma-joined)
// for the fail-close SDIRK3_RW_TERM_CAPTURE_INCOMPLETE marker.
//
// PR 9B.2 (P1-2): total and defined counts are tracked separately —
// total==0 -> missing, total>1 -> duplicate (even when one copy is
// undefined), total==1 && defined==0 -> undefined. Callers MUST run this on
// the RAW captured terms BEFORE any detach().clone() or _unpack_dual(),
// which would throw on an undefined tensor instead of failing closed.
inline std::string validate_rw_term_inventory(
    const std::vector<std::pair<std::string, torch::Tensor>>& terms,
    bool expect_wdamp) {
    static const char* kRequired[] = {
        "w_input",      "mu_input",       "pg",           "buoy_mu1",
        "buoy_mu2",     "rw_pre_pgf",     "w_pgf_buoy_all",
        "w_top_contrib", "rw_pre_mask",   "rw_post_mask", "wd_vert_cfl",
        "wd_cfl_excess", "wd_w_sign",     "wd_mass_factor",
        "rw_tend_final",
    };
    std::string reason;
    auto append = [&](const std::string& r) {
        if (!reason.empty()) reason += ",";
        reason += r;
    };
    struct Counts {
        int total = 0;
        int defined = 0;
    };
    auto stats_of = [&](const char* name) {
        Counts c;
        for (const auto& kv : terms) {
            if (kv.first == name) {
                ++c.total;
                if (kv.second.defined()) ++c.defined;
            }
        }
        return c;
    };
    for (const char* name : kRequired) {
        const Counts c = stats_of(name);
        if (c.total == 0) append(std::string("missing:") + name);
        if (c.total > 1) append(std::string("duplicate:") + name);
        if (c.total == 1 && c.defined == 0)
            append(std::string("undefined:") + name);
    }
    {
        const Counts c = stats_of("w_damp_padded");
        if (expect_wdamp && c.total == 0) append("missing:w_damp_padded");
        if (!expect_wdamp && c.total > 0) append("unexpected:w_damp_padded");
        if (c.total > 1) append("duplicate:w_damp_padded");
        if (c.total == 1 && c.defined == 0) append("undefined:w_damp_padded");
    }
    for (const auto& kv : terms) {
        bool known = kv.first == "w_damp_padded";
        for (const char* name : kRequired)
            if (kv.first == name) known = true;
        if (!known) append(std::string("unknown:") + kv.first);
    }
    return reason;
}

}  // namespace sdirk3
}  // namespace wrf
