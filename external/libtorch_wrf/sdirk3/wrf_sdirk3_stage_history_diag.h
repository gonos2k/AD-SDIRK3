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
#include <cstdio>
#include <iomanip>
#include <iostream>
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

// Validate the per-source-stage Newton-defect table against the AUTHORITATIVE
// expected set (the target stage), independent of the observed `defects` vector
// -- an omitted or duplicated source-stage defect must fail closed, not be
// silently under-reported (which would quietly gut PR 9E's candidate C, the
// prior-stage-defect-inheritance question). Returns "" when sound, else a
// comma-joined reason for the SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE marker.
//   expected stage ids = 1 .. target_stage-1, each exactly once
//   only stage 1 (the ESDIRK explicit stage) may carry explicit=true
//   explicit stage:     newton_defect == 0 and f_fast_norm ~= k_norm
//   non-explicit stage: f_fast_norm, newton_defect_norm, scaled_final_residual
//                       all observed and >= 0 (the -1 "unobserved" sentinel fails)
//   every norm/ratio finite
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
        if (d.explicit_stage && d.stage != 1)
            append("explicit_flag_nonstage1:" + sid);
        if (d.explicit_stage) {
            if (d.newton_defect_norm != 0.0)
                append("explicit_defect_nonzero:" + sid);
            const double denom = std::max(std::abs(d.k_norm), 1e-300);
            if (std::abs(d.f_fast_norm - d.k_norm) / denom > 1e-6)
                append("explicit_F_ne_K:" + sid);
        } else {
            if (d.f_fast_norm < 0.0) append("unobserved_F_fast:" + sid);
            if (d.newton_defect_norm < 0.0) append("neg_defect:" + sid);
            if (d.scaled_final_residual < 0.0) append("neg_scaled_resid:" + sid);
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
