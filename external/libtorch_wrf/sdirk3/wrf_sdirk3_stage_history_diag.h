#pragma once
// PR 9E Commit 2: opt-in, diagnosis-only emission of the ARK stage-HISTORY
// provenance and the per-source-stage Newton-defect summary for the record
// stage=2 (converging control) and stage=3 (failing target) operands.
//
// This closes the first two of PR 9E's four closures for the operand:
//   (1) source-stage derivative provenance  — ||k_slow[j]||, ||k_fast[j]|| as
//       they were BORN (captured by the caller at k_slow/k_fast creation; here
//       we only OBSERVE those stored tensors, never re-evaluate any RHS);
//   (2) ARK stage-history assembly — the exact increments
//         dt*aE[i][j]*k_slow[j] + dt*aI[i][j]*k_fast[j]
//       that compose U_stage = U_n + sum_j (...), verified by an FP64 closure
//       against the production-assembled U_stage (must be ~float32 eps).
//
// The per-source-stage defect record answers candidate C (prior-stage Newton
// defect inheritance): whether an un-converged prior stage's residual bleeds
// into stage i's history. newton_defect[j] = K_final[j] - F_fast(U_eval_final[j])
// is the value the Newton solver ALREADY computed on its last accepted
// iteration (R = K - F_fast(U_eval)); this file consumes the observed norms and
// never triggers an extra RHS evaluation.
//
// Non-interference contract (inherited from RwTermCapture / PR 9B and the
// generic StageOperandCapture of Commit 1):
//   - every reduction runs under NoGradGuard on DETACHED tensors, so no node is
//     added to the live autograd graph and no state tensor is mutated;
//   - nothing here touches k_slow / k_fast / U_stage / the residual — it reads
//     copies, so the production trajectory is bit-identical whether this is on
//     or off;
//   - gated by the caller on the cached stage_operand_diag config bool
//     (default off) and single-rank / single-tile topology; with the flag off
//     this header emits nothing and computes nothing.
//
// Spaces: K_norm, f_fast_norm, newton_defect_norm are RAW L2 (one consistent
// space). The solver's own scaled convergence metric (||S^-1 R||_rms) is
// carried alongside as a cross-reference column, NOT mixed into the raw defect.
// The per-block raw/scaled/halo decomposition is Commit 3's job.
#include "wrf_sdirk3_stage_operand_capture.h"
#include <torch/torch.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {

// One prior stage's converged-solve quality, snapshotted AT that stage's own
// completion (so it is that stage's value, not a later "last_stage_*" overwrite).
// All norms observe production tensors; nothing is recomputed.
struct StageDefectSnapshot {
    int stage = -1;                    // 1-based source stage id
    double k_norm = 0.0;               // ||k_fast[stage-1]||  (= ||K_final||), raw L2
    double f_fast_norm = -1.0;         // ||F_fast(U_eval_final)||, raw L2 (-1 = unobserved)
    double newton_defect_norm = 0.0;   // ||K_final - F_fast(U_eval_final)||, raw L2
    double defect_to_k_ratio = 0.0;    // newton_defect_norm / max(k_norm, tiny)
    double scaled_final_residual = -1.0; // solver ||S^-1 R||_rms cross-reference (-1 = n/a)
    bool converged = false;            // solver-reported convergence for this stage
    bool explicit_stage = false;       // ESDIRK first stage (no Newton solve; defect==0)
};

// One source stage's contribution to the target stage's ARK history.
struct StageHistorySource {
    int stage = -1;                    // 1-based source stage id (j+1)
    double a_explicit = 0.0;           // aE[target-1][j]
    double a_implicit = 0.0;           // aI[target-1][j]
    const torch::Tensor* k_slow = nullptr; // production tensor, observed (not owned)
    const torch::Tensor* k_fast = nullptr;
};

// A packed six-block boundary (ru/rv/rw/ph/t/mu), for per-block localization of
// an attribution failure.
struct StageOperandBlock {
    const char* name = "";
    int64_t start = 0;
    int64_t size = 0;
};

// Structural preflight (PR 9E history-authority P1-5): every packed operand must
// be a defined, non-empty, 1-D tensor of the SAME shape / device / dtype as the
// reference. This runs BEFORE any norm / multiply / subtract, which would
// otherwise raise a generic (non-Stage-operand) LibTorch exception on a
// dimension/dtype/device mismatch -- or, worse, SILENTLY BROADCAST a wrong shape.
// Returns "" when sound, else a stable marker+detail:
//   SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH / _DEVICE_MISMATCH / _DTYPE_MISMATCH.
inline std::string check_stage_operand_structure(
    const torch::Tensor& ref, const char* ref_name,
    const std::vector<std::pair<const char*, const torch::Tensor*>>& operands) {
    if (!ref.defined() || ref.numel() == 0 || ref.dim() != 1)
        return std::string("SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH: ") + ref_name +
               " (must be a defined non-empty 1-D packed tensor)";
    for (const auto& kv : operands) {
        const char* name = kv.first;
        const torch::Tensor* t = kv.second;
        // A null/undefined operand is a COMPLETENESS concern, left to the caller's
        // inventory / undefined-source check (which emits CAPTURE_INCOMPLETE or an
        // undefined-source fail). The structural preflight validates the
        // shape/device/dtype of the DEFINED operands, which is what would
        // otherwise raise a generic LibTorch throw or silently broadcast.
        if (!t || !t->defined()) continue;
        if (t->numel() == 0 || t->dim() != 1 || t->sizes() != ref.sizes())
            return std::string("SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH: ") + name;
        if (t->device() != ref.device())
            return std::string("SDIRK3_STAGE_OPERAND_DEVICE_MISMATCH: ") + name;
        if (t->scalar_type() != ref.scalar_type())
            return std::string("SDIRK3_STAGE_OPERAND_DTYPE_MISMATCH: ") + name;
    }
    return std::string();
}

// AUTHORITATIVE per-source ARK attribution check (PR 9E history-authority C1).
// The aggregate history closure proves only that the reconstructed TOTAL matches
// U_stage - U_n; it cannot catch a +delta/-delta compensation between two sources
// or a source-label swap, and reconstructing the history in FP64 does not verify
// the real FP32 production rounding. This check instead re-applies each source's
// intended increment with the IDENTICAL FP32 expression production used, on top of
// the captured running state `states[j-1]` (U_n for j=0), and asserts it lands on
// the captured production state `states[j]`:
//     states[j-1] + dt*aE*k_slow[j] + dt*aI*k_fast[j]  ==  states[j]   (per element)
// For a correct attribution this is BIT-EXACT (same inputs, same FP32 ops) even
// under huge-base ULP loss (both round identically), so the check requires
// residual == 0 exactly -- a magnitude-scaled tolerance like eps*|state| would
// allow ~477 at |state|=1e9 and hide a wrong small increment. A wrong k /
// coefficient / sign misses states[j] by a nonzero residual; per-block slicing
// localizes which block failed. The stage LABEL is enforced separately (below).
//
// `states` are the running FP32 states AFTER each source's production add
// (states.size() == sources.size(); states.back() must equal U_stage). Returns ""
// when sound, else a reason for the SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED marker.
// Emits SDIRK3_STAGE_APPLIED_DELTA success lines only after the gate passes.
inline std::string emit_stage_applied_delta_diag(
    int64_t step_seq,
    int checkpoint_timestep,
    int target_stage,
    float dt,
    const torch::Tensor& U_n,
    const torch::Tensor& U_stage,
    const std::vector<StageHistorySource>& sources,
    const std::vector<const torch::Tensor*>& states,
    const std::vector<StageOperandBlock>& blocks) {
    torch::NoGradGuard no_grad;
    char dtbuf[32];
    std::snprintf(dtbuf, sizeof(dtbuf), "%.9e", static_cast<double>(dt));
    const std::string tag = std::string("dt=") + dtbuf +
                            " step_seq=" + std::to_string(step_seq) +
                            " checkpoint_timestep=" +
                            std::to_string(checkpoint_timestep) +
                            " target_stage=" + std::to_string(target_stage) +
                            " iter=0";
    auto fail = [&](const std::string& detail) -> std::string {
        std::cerr << "[SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED] " << tag << " "
                  << detail << std::endl;
        return "SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED: " + detail;
    };

    if (states.size() != sources.size())
        return fail("state_count=" + std::to_string(states.size()) +
                    " != source_count=" + std::to_string(sources.size()));

    // STRUCTURAL PREFLIGHT (P1-5) before ANY tensor arithmetic below.
    {
        std::vector<std::pair<const char*, const torch::Tensor*>> ops;
        ops.reserve(1 + states.size() + sources.size() * 2);
        ops.push_back({"U_n", &U_n});
        for (std::size_t j = 0; j < states.size(); ++j)
            ops.push_back({"state", states[j]});
        for (const auto& s : sources) {
            ops.push_back({"k_slow", s.k_slow});
            ops.push_back({"k_fast", s.k_fast});
        }
        const std::string sr =
            check_stage_operand_structure(U_stage, "U_stage", ops);
        if (!sr.empty()) {
            const auto pos = sr.find(':');
            const std::string marker =
                (pos == std::string::npos) ? sr : sr.substr(0, pos);
            const std::string detail =
                (pos == std::string::npos) ? std::string() : sr.substr(pos + 2);
            std::cerr << "[" << marker << "] " << tag << " " << detail
                      << std::endl;
            return sr;
        }
    }

    // LABEL AUTHORITY (derived from the AUTHORITATIVE target_stage, not the
    // observed order): the history of target stage i is composed of source stages
    // 1..i-1 applied in order, so there must be exactly target_stage-1 sources and
    // the source at position j MUST carry the label j+1. The per-element reapply
    // check below ties each POSITION's k/coefficient to states[j]; it does NOT
    // constrain the stage LABEL, so a swapped label is a real misattribution the
    // reapply alone cannot catch. Enforce it here.
    if (static_cast<int>(sources.size()) != target_stage - 1)
        return fail("source_count=" + std::to_string(sources.size()) +
                    " != target_stage-1=" + std::to_string(target_stage - 1));
    for (std::size_t j = 0; j < sources.size(); ++j) {
        if (sources[j].stage != static_cast<int>(j) + 1)
            return fail("label_mismatch pos=" + std::to_string(j) + " stage=" +
                        std::to_string(sources[j].stage) +
                        " expected=" + std::to_string(j + 1));
    }

    std::vector<std::string> diag_lines;
    diag_lines.reserve(sources.size());
    double worst_residual = 0.0;
    std::string block_fail;

    for (std::size_t j = 0; j < sources.size(); ++j) {
        const auto& s = sources[j];
        if (!states[j] || !states[j]->defined() || !s.k_slow ||
            !s.k_slow->defined() || !s.k_fast || !s.k_fast->defined())
            return fail("undefined source/state at src_stage=" +
                        std::to_string(s.stage));
        const torch::Tensor& prev = (j == 0) ? U_n : *states[j - 1];
        if (!prev.defined())
            return fail("undefined prev state at src_stage=" +
                        std::to_string(s.stage));
        // IDENTICAL FP32 expression to production's compute_stage_rhs add.
        torch::Tensor reapply = prev +
                                dt * static_cast<float>(s.a_explicit) * (*s.k_slow) +
                                dt * static_cast<float>(s.a_implicit) * (*s.k_fast);
        // Non-finite inputs would make the residual NaN, and with the strict-zero
        // gate below std::max(0,NaN)==0 and NaN>0 is false -- so a NaN would FAIL
        // OPEN, emitting a "successful" attribution. Require finite prev / state /
        // reapply (which also covers non-finite k) BEFORE the zero comparison.
        if (!prev.isfinite().all().item<bool>() ||
            !states[j]->isfinite().all().item<bool>() ||
            !reapply.isfinite().all().item<bool>())
            return fail("nonfinite input/reapply at src_stage=" +
                        std::to_string(s.stage));
        // BIT-EXACT replay. Production and this re-application evaluate the same
        // FP32 expression, so a correct attribution lands on the captured state
        // EXACTLY (residual == 0), independent of |state| -- a huge-base ULP loss
        // is absorbed identically on both sides. A magnitude-scaled tolerance like
        // eps*|state| would allow ~477 at |state|=1e9 and hide a wrong small
        // increment; a strict zero cannot. Any nonzero residual is a real
        // misattribution.
        torch::Tensor residual = (reapply - *states[j]).abs();  // FP32, element-wise
        const double res_max = residual.max().item<double>();
        if (!std::isfinite(res_max))
            return fail("nonfinite residual at src_stage=" +
                        std::to_string(s.stage));
        worst_residual = std::max(worst_residual, res_max);
        const double applied_norm =
            (*states[j] - prev).to(torch::kFloat64).norm().item<double>();

        std::ostringstream os;
        os << "[SDIRK3_STAGE_APPLIED_DELTA] " << tag
           << " src_stage=" << s.stage << std::scientific << std::setprecision(6)
           << " applied_norm=" << applied_norm << " reapply_res_max=" << res_max;
        for (const auto& b : blocks) {
            if (b.start + b.size > residual.numel()) continue;
            const double bres =
                residual.slice(0, b.start, b.start + b.size).max().item<double>();
            os << " res_" << b.name << "=" << bres;
            if (bres > 0.0) {
                if (!block_fail.empty()) block_fail += ",";
                block_fail += std::string(b.name) + "@s" + std::to_string(s.stage);
            }
        }
        diag_lines.push_back(os.str());
    }

    if (worst_residual > 0.0) {
        char rb[128];
        std::snprintf(rb, sizeof(rb), "worst_reapply_residual=%.6e blocks=%s",
                      worst_residual,
                      block_fail.empty() ? "?" : block_fail.c_str());
        return fail(rb);
    }
    // Capture integrity: the last running state must be the assembled U_stage.
    if (states.back() && states.back()->defined() && U_stage.defined()) {
        const double integ =
            (*states.back() - U_stage).abs().max().item<double>();
        if (!std::isfinite(integ) || integ > 0.0) {  // NaN also fails closed
            char rb[96];
            std::snprintf(rb, sizeof(rb), "capture_integrity last_state_vs_U_stage=%.6e",
                          integ);
            return fail(rb);
        }
    }
    for (const auto& line : diag_lines) std::cerr << line << std::endl;
    return std::string();
}

// Validate the per-source-stage Newton-defect table against the AUTHORITATIVE
// expected set (the target stage), independent of the observed `defects` vector
// -- an omitted or duplicated source-stage defect must fail closed, not be
// silently under-reported (which would quietly gut PR 9E's candidate C, the
// prior-stage-defect-inheritance question). Returns "" when sound, else a
// comma-joined reason for the SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE marker.
//   expected stage ids = 1 .. target_stage-1, each exactly once
//   stage 1 (the ESDIRK explicit stage) MUST carry explicit=true; every later
//     stage MUST carry explicit=false
//   every norm/ratio finite AND non-negative -- k_norm, f_fast_norm,
//     newton_defect_norm, scaled_final_residual, defect_to_k_ratio -- checked
//     UNCONDITIONALLY (independent of role; the -1 "unobserved" sentinel fails)
//   explicit stage additionally: newton_defect == 0 and f_fast_norm ~= k_norm
inline std::string validate_stage_defect_inventory(
    const std::vector<StageDefectSnapshot>& defects, int target_stage) {
    std::string reason;
    auto append = [&](const std::string& r) {
        if (!reason.empty()) reason += ",";
        reason += r;
    };
    // presence: each expected source stage appears exactly once
    for (int j = 1; j < target_stage; ++j) {
        int count = 0;
        for (const auto& d : defects)
            if (d.stage == j) ++count;
        if (count == 0) append("missing:s" + std::to_string(j));
        if (count > 1) append("duplicate:s" + std::to_string(j));
    }
    for (const auto& d : defects) {
        const std::string sid = "s" + std::to_string(d.stage);
        if (d.stage < 1 || d.stage >= target_stage)
            append("unknown_stage:" + sid);
        if (!std::isfinite(d.k_norm) || !std::isfinite(d.f_fast_norm) ||
            !std::isfinite(d.newton_defect_norm) ||
            !std::isfinite(d.defect_to_k_ratio) ||
            !std::isfinite(d.scaled_final_residual))
            append("nonfinite:" + sid);
        // Every numeric field is a norm or a ratio of norms -> inherently
        // non-negative for BOTH roles; a negative value (including the -1
        // "unobserved" sentinel on a solved stage) is invalid data the finiteness
        // check alone would pass. These are checked UNCONDITIONALLY: putting a
        // non-negativity check inside a role branch (as an earlier fix did for
        // f_fast/defect/scaled) let the OTHER role skip it -- e.g. an explicit
        // stage 1 with a negative scaled_final_residual (Codex stop-gate). Only
        // the genuinely role-specific SEMANTIC checks live in the branch below.
        if (d.k_norm < 0.0) append("neg_k_norm:" + sid);
        if (d.f_fast_norm < 0.0) append("neg_f_fast:" + sid);
        if (d.newton_defect_norm < 0.0) append("neg_defect:" + sid);
        if (d.scaled_final_residual < 0.0) append("neg_scaled_resid:" + sid);
        if (d.defect_to_k_ratio < 0.0) append("neg_ratio:" + sid);
        // The explicit role is fully constrained BOTH ways: stage 1 IS the
        // ESDIRK explicit stage (a_implicit[0][0]==0, no Newton solve), so it
        // MUST carry explicit=true; every later stage MUST carry explicit=false.
        if (d.explicit_stage && d.stage != 1)
            append("explicit_flag_nonstage1:" + sid);
        if (d.stage == 1 && !d.explicit_stage)
            append("stage1_not_explicit:" + sid);
        // Role-specific SEMANTIC constraints only (non-negativity handled above).
        if (d.explicit_stage) {
            if (d.newton_defect_norm != 0.0)
                append("explicit_defect_nonzero:" + sid);
            const double denom = std::max(std::abs(d.k_norm), 1e-300);
            if (std::abs(d.f_fast_norm - d.k_norm) / denom > 1e-6)
                append("explicit_F_ne_K:" + sid);
        }
    }
    return reason;
}

// Emit SDIRK3_STAGE_HISTORY_DIAG (per-source ARK increment provenance) and
// SDIRK3_STAGE_HISTORY_SUMMARY (history closure + per-source defect table) for
// the record stage `target_stage`. Every line is stamped with the full-precision
// `dt` and the solver-local monotonic `step_seq` (the primary, always-advancing
// diagnostic identifier). `checkpoint_timestep` is the solver's 4D-Var
// trajectory-checkpoint counter (get_saved_global_timestep()); it advances ONLY
// under save_trajectory and is 0 in a default run, so it is emitted for
// reference but is NOT the WRF model timestep -- do not read it as such.
//
// HARD GATE (PR 9E evidence integrity): returns an EMPTY string when the
// evidence is sound; otherwise a non-empty reason after printing the specific
// failure marker -- SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE (history inventory),
// SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE (defect-table inventory), or
// SDIRK3_STAGE_OPERAND_CLOSURE_FAILED (the AUTHORITATIVE history-relative
// closure: Sum of increments vs U_stage - U_n, whose relative-error denominator
// EXCLUDES U_n so a base state far larger than the increments cannot launder a
// wrong coefficient or sign -- a full-state ||.||/||U_stage|| would). The caller
// MUST controlled-fatal on a non-empty return. On failure ONLY the failure
// marker is printed: no success-form STAGE_HISTORY_* record is emitted for a
// rejected capture, so a consumer that greps only the success markers can never
// mistake broken evidence for sound.
//
// `U_stage` MUST be the pristine production history (before any halo/sanitize
// mutation) so the FP64 closure reproduces it. All output goes to std::cerr
// (WRF routes cerr to rsl.error.0000).
inline std::string emit_stage_history_diag(
    int64_t step_seq,
    int checkpoint_timestep,
    int target_stage,
    float dt,
    const torch::Tensor& U_n,
    const torch::Tensor& U_stage,
    const std::vector<StageHistorySource>& sources,
    const std::vector<StageDefectSnapshot>& defects) {
    torch::NoGradGuard no_grad;

    char dtbuf[32];
    std::snprintf(dtbuf, sizeof(dtbuf), "%.9e", static_cast<double>(dt));
    const std::string tag = std::string("dt=") + dtbuf +
                            " step_seq=" + std::to_string(step_seq) +
                            " checkpoint_timestep=" +
                            std::to_string(checkpoint_timestep) +
                            " target_stage=" + std::to_string(target_stage) +
                            " iter=0";

    // ---- PHASE 0: STRUCTURAL PREFLIGHT (P1-5) before ANY tensor arithmetic. ----
    {
        std::vector<std::pair<const char*, const torch::Tensor*>> ops;
        ops.reserve(1 + sources.size() * 2);
        ops.push_back({"U_n", &U_n});
        for (const auto& s : sources) {
            ops.push_back({"k_slow", s.k_slow});
            ops.push_back({"k_fast", s.k_fast});
        }
        const std::string sr =
            check_stage_operand_structure(U_stage, "U_stage", ops);
        if (!sr.empty()) {
            const auto pos = sr.find(':');
            const std::string marker =
                (pos == std::string::npos) ? sr : sr.substr(0, pos);
            const std::string detail =
                (pos == std::string::npos) ? std::string() : sr.substr(pos + 2);
            std::cerr << "[" << marker << "] " << tag << " " << detail
                      << std::endl;
            return sr;
        }
    }

    // ---- PHASE 1: build the FP64 increment leaves and BUFFER the per-source
    // DIAG lines (do NOT print yet -- see PHASE 3/4 ordering). Increments use the
    // SAME float coefficient cast production uses. `leaves` is filled completely
    // first so no reallocation can dangle a pointer. ----
    std::vector<OperandLeaf> leaves;           // inventory records (name + tensor)
    leaves.reserve(sources.size() * 2 + 1);
    std::vector<std::string> diag_lines;
    diag_lines.reserve(sources.size());

    auto add_leaf = [&](const std::string& name, const torch::Tensor& t) {
        OperandLeaf l;
        l.name = name;
        l.split = OperandSplit::History;
        l.kind = OperandTermKind::Additive;
        l.tensor = t;  // detached FP64 or undefined
        leaves.push_back(std::move(l));
    };

    // Base state U_n is the first leaf (index 0); increments follow.
    add_leaf("U_n", U_n.defined() ? U_n.detach().to(torch::kFloat64)
                                  : torch::Tensor());

    for (const auto& s : sources) {
        torch::Tensor expl_incr;
        torch::Tensor impl_incr;
        double expl_norm = 0.0;
        double impl_norm = 0.0;
        double k_slow_norm = 0.0;
        double k_fast_norm = 0.0;
        if (s.k_slow && s.k_slow->defined()) {
            auto ks = s.k_slow->detach();
            k_slow_norm = ks.to(torch::kFloat64).norm().item<double>();
            expl_incr = (dt * static_cast<float>(s.a_explicit) * ks)
                            .to(torch::kFloat64);
            expl_norm = expl_incr.norm().item<double>();
        }
        if (s.k_fast && s.k_fast->defined()) {
            auto kf = s.k_fast->detach();
            k_fast_norm = kf.to(torch::kFloat64).norm().item<double>();
            impl_incr = (dt * static_cast<float>(s.a_implicit) * kf)
                            .to(torch::kFloat64);
            impl_norm = impl_incr.norm().item<double>();
        }
        add_leaf("expl_incr_s" + std::to_string(s.stage), expl_incr);
        add_leaf("impl_incr_s" + std::to_string(s.stage), impl_incr);

        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_DIAG] " << tag
           << " src_stage=" << s.stage
           << " aE=" << std::scientific << std::setprecision(9) << s.a_explicit
           << " aI=" << s.a_implicit
           << " k_slow_norm=" << k_slow_norm
           << " k_fast_norm=" << k_fast_norm
           << " expl_incr_norm=" << expl_norm
           << " impl_incr_norm=" << impl_norm;
        diag_lines.push_back(os.str());
    }

    // ---- PHASE 2: validate against INDEPENDENT authorities + compute closures ----
    // 2a. History inventory: expected leaf set derived from the AUTHORITATIVE
    // target stage (lower stages j = 1..i-1, zero-coefficient included), NOT from
    // the observed `sources` -- so an omitted source stage fails closed as a
    // missing leaf.
    std::vector<std::string> required;
    required.reserve(static_cast<size_t>(target_stage > 0 ? target_stage : 1) * 2);
    required.push_back("U_n");
    for (int j = 1; j < target_stage; ++j) {
        required.push_back("expl_incr_s" + std::to_string(j));
        required.push_back("impl_incr_s" + std::to_string(j));
    }
    const std::string inventory =
        validate_stage_operand_inventory(leaves, required, /*optional=*/{});

    // 2b. Defect-table inventory (independent authority: target stage).
    const std::string defect_reason =
        validate_stage_defect_inventory(defects, target_stage);

    // 2c. AUTHORITATIVE history-relative closure: reconstructed increments (leaves
    // 1..N, WITHOUT U_n) vs the actual history U_stage - U_n. The relative-error
    // denominator EXCLUDES U_n, so a base state far larger than the increments
    // cannot hide a wrong coefficient or sign. hist_max_rel additionally catches a
    // block-local spike the L2 metric would average out (per-element denom floored
    // by the history RMS to avoid a zero-crossing blow-up).
    torch::Tensor recon;
    for (std::size_t k = 1; k < leaves.size(); ++k) {
        if (!leaves[k].tensor.defined()) continue;
        recon = recon.defined() ? recon + leaves[k].tensor
                                : leaves[k].tensor.clone();
    }
    torch::Tensor U_n64 = U_n.defined() ? U_n.detach().to(torch::kFloat64)
                                        : torch::Tensor();
    torch::Tensor U_stage64 = U_stage.detach().to(torch::kFloat64);
    torch::Tensor actual_history =
        U_n64.defined() ? (U_stage64 - U_n64) : U_stage64.clone();
    if (!recon.defined()) recon = torch::zeros_like(actual_history);
    torch::Tensor hdiff = recon - actual_history;
    const double hist_abs = hdiff.norm().item<double>();
    const double recon_norm = recon.norm().item<double>();
    const double actual_norm = actual_history.norm().item<double>();
    const double hist_rel =
        hist_abs / std::max(std::max(recon_norm, actual_norm), 1e-300);
    const double hist_max_abs = hdiff.abs().max().item<double>();
    const double actual_rms =
        actual_norm /
        std::sqrt(static_cast<double>(std::max<int64_t>(actual_history.numel(), 1)));
    const double spike_floor = std::max(actual_rms * 1e-3, 1e-300);
    const double hist_max_rel =
        (hdiff.abs() / (actual_history.abs() + spike_floor)).max().item<double>();
    const bool hist_finite = hdiff.isfinite().all().item<bool>() &&
                             recon.isfinite().all().item<bool>();

    // Full-state closure is TELEMETRY ONLY (never the gate): with ||U_n|| >>
    // ||increments|| it stays ~eps even for a wrong increment, which is exactly
    // why it must not be the authority.
    std::vector<const torch::Tensor*> closure_leaves;
    closure_leaves.reserve(leaves.size());
    for (const auto& l : leaves) closure_leaves.push_back(&l.tensor);
    const ClosureResult full_closure = closure_of(closure_leaves, U_stage);

    const double u_n_norm = U_n64.defined() ? U_n64.norm().item<double>() : 0.0;
    const double u_stage_norm = U_stage64.norm().item<double>();

    // ---- PHASE 3: GATE. On ANY failure print ONLY the failure marker and return;
    // NO success-form STAGE_HISTORY_* record is emitted for a rejected capture. ----
    auto fail = [&](const char* marker, const std::string& detail) -> std::string {
        std::cerr << "[" << marker << "] " << tag << " " << detail << std::endl;
        return std::string(marker) + ": " + detail;
    };
    if (!inventory.empty())
        return fail("SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE",
                    "inventory=" + inventory);
    if (!defect_reason.empty())
        return fail("SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE",
                    "defect=" + defect_reason);
    if (!hist_finite || hist_rel > 1e-6 || hist_max_rel > 1e-1) {
        char rb[224];
        std::snprintf(rb, sizeof(rb),
                      "hist_rel=%.6e hist_max_abs=%.6e hist_max_rel=%.6e "
                      "finite=%d recon_norm=%.6e actual_norm=%.6e",
                      hist_rel, hist_max_abs, hist_max_rel, hist_finite ? 1 : 0,
                      recon_norm, actual_norm);
        return fail("SDIRK3_STAGE_OPERAND_CLOSURE_FAILED", rb);
    }

    // ---- PHASE 4: all authorities passed -> emit the SUCCESS records ----
    for (const auto& line : diag_lines) std::cerr << line << std::endl;
    {
        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_SUMMARY] " << tag << std::scientific
           << std::setprecision(6)
           << " hist_rel=" << hist_rel
           << " hist_abs=" << hist_abs
           << " hist_max_abs=" << hist_max_abs
           << " hist_max_rel=" << hist_max_rel
           << " recon_norm=" << recon_norm
           << " actual_norm=" << actual_norm
           << " full_state_rel=" << full_closure.rel_err
           << " U_n_norm=" << u_n_norm
           << " U_stage_norm=" << u_stage_norm
           << " inventory=OK defect=OK";
        std::cerr << os.str() << std::endl;
    }
    for (const auto& d : defects) {
        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_SUMMARY] " << tag
           << " src_stage=" << d.stage
           << " k_norm=" << std::scientific << std::setprecision(6) << d.k_norm
           << " f_fast_norm=" << d.f_fast_norm
           << " newton_defect_norm=" << d.newton_defect_norm
           << " defect_to_k_ratio=" << d.defect_to_k_ratio
           << " scaled_final_residual=" << d.scaled_final_residual
           << " converged=" << (d.converged ? 1 : 0)
           << " explicit=" << (d.explicit_stage ? 1 : 0);
        std::cerr << os.str() << std::endl;
    }
    return std::string();
}

}  // namespace sdirk3
}  // namespace wrf
