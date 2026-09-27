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
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace wrf {
namespace sdirk3 {

struct RwTermCaptureIdentity {
    bool valid = false;
    int timestep = -1;
    int stage = -1;
    int newton_iter = -1;
    std::uint64_t solve_generation = 0;
    std::uint64_t solver_id = 0;
    std::uint64_t input_digest = 0;
};

struct RwWConversionCapture {
    bool observed = false;
    bool canonical_horizontal = false;
    bool coupled_slow_export = false;
    torch::Tensor w_input;
    torch::Tensor velocity_mass_w;
    torch::Tensor mu_tend_at_w;
    torch::Tensor conversion_rate;
    torch::Tensor w_tend_after_conversion;
};

// Shared RHS input identity digest. This is the same FNV-1a over packed FP32
// bytes used by the existing optional RHS_COUNT operand record.
inline std::uint64_t sdirk3_rhs_operand_digest(const torch::Tensor& tensor) {
    torch::NoGradGuard no_grad;
    const auto cpu = tensor.detach().to(torch::kCPU, torch::kFloat32).contiguous();
    const auto* bytes = reinterpret_cast<const unsigned char*>(cpu.data_ptr<float>());
    const std::size_t size = static_cast<std::size_t>(cpu.numel()) * sizeof(float);
    std::uint64_t digest = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) {
        digest ^= bytes[i];
        digest *= 1099511628211ULL;
    }
    return digest;
}

struct RwTermCapture {
    bool armed = false;
    bool capture_final_w = false;
    int rhs_mode = -1;
    RwTermCaptureIdentity identity;
    RwWConversionCapture w_conversion;
    std::vector<std::pair<std::string, torch::Tensor>> terms;
    torch::Tensor final_w_packed;
    void add(const char* name, const torch::Tensor& t) {
        if (armed) terms.emplace_back(name, t);
    }
    void observe_final_w(const torch::Tensor& packed_w) {
        if (armed && capture_final_w && packed_w.defined())
            final_w_packed = packed_w.detach().clone();
    }
    void observe_w_conversion(const torch::Tensor& w_input,
                              const torch::Tensor& velocity_mass_w,
                              const torch::Tensor& mu_tend_at_w,
                              const torch::Tensor& conversion_rate,
                              const torch::Tensor& w_tend_after_conversion,
                              bool canonical_horizontal,
                              bool coupled_slow_export) {
        if (!armed || !capture_final_w) return;
        w_conversion.observed = true;
        w_conversion.canonical_horizontal = canonical_horizontal;
        w_conversion.coupled_slow_export = coupled_slow_export;
        w_conversion.w_input = w_input;
        w_conversion.velocity_mass_w = velocity_mass_w;
        w_conversion.mu_tend_at_w = mu_tend_at_w;
        w_conversion.conversion_rate = conversion_rate;
        w_conversion.w_tend_after_conversion = w_tend_after_conversion;
    }
    void reset() {
        terms.clear();
        final_w_packed = torch::Tensor();
        capture_final_w = false;
        rhs_mode = -1;
        identity = RwTermCaptureIdentity{};
        w_conversion = RwWConversionCapture{};
    }
};

inline RwTermCapture& rw_term_capture_slot() {
    static thread_local RwTermCapture t_slot;
    return t_slot;
}

inline bool rw_term_capture_expects_wdamp(
    bool wrf_w_damping, bool implicit_wdamp, float w_damp_alpha,
    float wrf_w_crit_cfl) {
    return wrf_w_damping && implicit_wdamp && w_damp_alpha > 0.0f &&
           wrf_w_crit_cfl > 0.0f;
}



// RAII arm/disarm. Constructor arms THIS THREAD's slot (refusing if it is
// already armed — nested capture); destructor disarms and clears whatever
// was not taken, so an exception during the captured evaluation cannot leak
// an armed slot or retained tensors.
class RwTermCaptureScope {
  public:
    explicit RwTermCaptureScope(
        bool capture_final_w = false,
        RwTermCaptureIdentity identity = RwTermCaptureIdentity{})
        : slot_(rw_term_capture_slot()) {
        if (slot_.armed) return;  // nested arm: fail closed, caller checks armed_ok()
        slot_.reset();
        slot_.armed = true;
        slot_.capture_final_w = capture_final_w;
        slot_.identity = identity;
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
    bool capture_final_w() const { return owned_ && slot_.capture_final_w; }
    const RwTermCaptureIdentity& identity() const { return slot_.identity; }
    int rhs_mode() const { return owned_ ? slot_.rhs_mode : -1; }
    torch::Tensor final_w_observation() const {
        return owned_ && slot_.capture_final_w ? slot_.final_w_packed : torch::Tensor();
    }
    const RwWConversionCapture& w_conversion_observation() const {
        return slot_.w_conversion;
    }

    void observe_w_conversion(const torch::Tensor& w_input,
                              const torch::Tensor& velocity_mass_w,
                              const torch::Tensor& mu_tend_at_w,
                              const torch::Tensor& conversion_rate,
                              const torch::Tensor& w_tend_after_conversion,
                              bool canonical_horizontal,
                              bool coupled_slow_export) {
        if (!owned_ || !slot_.capture_final_w) return;
        auto& observed = slot_.w_conversion;
        observed.observed = true;
        observed.canonical_horizontal = canonical_horizontal;
        observed.coupled_slow_export = coupled_slow_export;
        observed.w_input = w_input;
        observed.velocity_mass_w = velocity_mass_w;
        observed.mu_tend_at_w = mu_tend_at_w;
        observed.conversion_rate = conversion_rate;
        observed.w_tend_after_conversion = w_tend_after_conversion;
    }

    void observe_final_w(const torch::Tensor& packed_w) {
        if (owned_ && slot_.capture_final_w && packed_w.defined())
            slot_.final_w_packed = packed_w.detach().clone();
    }

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
    // PR 9C: the W-damping family (inputs, chain factors, and the term) is
    // present only when the parity-gated damping is ACTIVE (expect_wdamp);
    // with WRF's default w_damping=0 the whole family is absent.
    static const char* kRequired[] = {
        "pg",           "buoy_mu1",     "buoy_mu2",      "rw_pre_pgf",
        "w_pgf_buoy_all", "w_top_contrib", "rw_pre_mask", "rw_post_mask",
        "rw_tend_final",
    };
    static const char* kWdampFamily[] = {
        "w_input",       "mu_input",     "wd_vert_cfl",  "wd_cfl_excess",
        "wd_w_sign",     "wd_mass_factor", "w_damp_padded",
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
    for (const char* name : kWdampFamily) {
        const Counts c = stats_of(name);
        if (expect_wdamp && c.total == 0) append(std::string("missing:") + name);
        if (!expect_wdamp && c.total > 0)
            append(std::string("unexpected:") + name);
        if (c.total > 1) append(std::string("duplicate:") + name);
        if (c.total == 1 && c.defined == 0)
            append(std::string("undefined:") + name);
    }
    for (const auto& kv : terms) {
        bool known = false;
        for (const char* name : kRequired)
            if (kv.first == name) known = true;
        for (const char* name : kWdampFamily)
            if (kv.first == name) known = true;
        if (!known) append(std::string("unknown:") + kv.first);
    }
    return reason;
}

}  // namespace sdirk3
}  // namespace wrf
