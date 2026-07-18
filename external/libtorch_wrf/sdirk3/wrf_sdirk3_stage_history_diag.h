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

// Emit SDIRK3_STAGE_HISTORY_DIAG (per-source ARK increment provenance) and
// SDIRK3_STAGE_HISTORY_SUMMARY (history closure + per-source defect table) for
// the record stage `target_stage`. Every line is stamped with the full-precision
// `dt` and the solver-local monotonic `step_seq` (the primary, always-advancing
// diagnostic identifier). `checkpoint_timestep` is the solver's 4D-Var
// trajectory-checkpoint counter (get_saved_global_timestep()); it advances ONLY
// under save_trajectory and is 0 in a default run, so it is emitted for
// reference but is NOT the WRF model timestep -- do not read it as such.
//
// HARD GATE (PR 9E Commit A): returns an EMPTY string when the evidence is
// sound; otherwise a non-empty reason after printing the specific failure marker
// (SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE for a broken inventory,
// SDIRK3_STAGE_OPERAND_CLOSURE_FAILED for a non-finite closure or one whose
// relative error exceeds 1e-6). The caller MUST controlled-fatal on a non-empty
// return -- printing broken evidence and continuing would launder a false
// closure.
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

    // Build the detached FP64 increment leaves that compose the history, using
    // the SAME float coefficient cast production uses, so the closure reproduces
    // the assembled U_stage to ~float32 eps. `leaves` is filled completely FIRST
    // (so no reallocation can dangle a pointer), then the closure pointer list is
    // taken from the settled `leaves` vector.
    std::vector<OperandLeaf> leaves;           // inventory records (name + tensor)
    leaves.reserve(sources.size() * 2 + 1);

    auto add_leaf = [&](const std::string& name, const torch::Tensor& t) {
        OperandLeaf l;
        l.name = name;
        l.split = OperandSplit::History;
        l.kind = OperandTermKind::Additive;
        l.tensor = t;  // detached FP64 or undefined
        leaves.push_back(std::move(l));
    };

    // Base state U_n is the first history leaf.
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
        std::cerr << os.str() << std::endl;
    }

    // `leaves` is now complete; take stable pointers for the closure.
    std::vector<const torch::Tensor*> closure_leaves;
    closure_leaves.reserve(leaves.size());
    for (const auto& l : leaves) closure_leaves.push_back(&l.tensor);

    // The EXPECTED inventory MUST be derived from an AUTHORITATIVE source that is
    // independent of the observed `sources` -- here the target stage itself. An
    // ESDIRK ARK history for target stage i is composed of exactly the lower
    // stages j = 1..i-1, each contributing one explicit and one implicit
    // increment (zero-coefficient stages included, as zero tensors). Building
    // `required` from `sources` (as an earlier fix did) would let an OMITTED
    // source stage look valid: the dropped leaf would simply not be in `required`
    // either, so the "missing" check would have nothing to fire on (Codex
    // stop-gate). With the set fixed by target_stage and passed as `required`
    // (validate gates missing/duplicate/undefined on `required`, unknown on
    // `required U optional`), a dropped, duplicated, undefined, or mislabeled
    // source stage -- or a stray leaf -- now fails closed.
    std::vector<std::string> required;
    required.reserve(static_cast<size_t>(target_stage > 0 ? target_stage : 1) * 2);
    required.push_back("U_n");
    for (int j = 1; j < target_stage; ++j) {
        required.push_back("expl_incr_s" + std::to_string(j));
        required.push_back("impl_incr_s" + std::to_string(j));
    }

    // FP64 closure: U_n + sum_j (expl_incr_j + impl_incr_j) == U_stage ?
    // Reuse the Commit-1 machinery so the standing contract and production share
    // one authority.
    ClosureResult closure = closure_of(closure_leaves, U_stage);
    const std::string inventory =
        validate_stage_operand_inventory(leaves, required, /*optional=*/{});

    double u_n_norm = U_n.defined()
                          ? U_n.detach().to(torch::kFloat64).norm().item<double>()
                          : 0.0;
    double u_stage_norm =
        U_stage.defined()
            ? U_stage.detach().to(torch::kFloat64).norm().item<double>()
            : 0.0;

    {
        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_SUMMARY] " << tag
           << " closure_rel_err=" << std::scientific << std::setprecision(6)
           << closure.rel_err
           << " closure_abs_err=" << closure.abs_err
           << " closure_max_abs=" << closure.max_abs_err
           << " closure_finite=" << (closure.finite ? 1 : 0)
           << " U_n_norm=" << u_n_norm
           << " U_stage_norm=" << u_stage_norm
           << " inventory=" << (inventory.empty() ? std::string("OK") : inventory);
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

    // HARD GATE (PR 9E Commit A): the diagnostic evidence must itself be sound.
    // A broken inventory (missing/duplicate/undefined/unknown/non-finite leaf) or
    // a history that does not reconstruct the production U_stage means the
    // measurement is wrong; continuing would launder a false closure. Print the
    // specific failure marker and return a non-empty reason for the caller to
    // controlled-fatal on.
    if (!inventory.empty()) {
        std::cerr << "[SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE] " << tag
                  << " inventory=" << inventory << std::endl;
        return "SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE: " + inventory;
    }
    if (!closure.finite || closure.rel_err > 1e-6) {
        char rb[96];
        std::snprintf(rb, sizeof(rb), "rel_err=%.6e finite=%d",
                      closure.rel_err, closure.finite ? 1 : 0);
        std::cerr << "[SDIRK3_STAGE_OPERAND_CLOSURE_FAILED] " << tag << " " << rb
                  << std::endl;
        return std::string("SDIRK3_STAGE_OPERAND_CLOSURE_FAILED: ") + rb;
    }
    return std::string();
}

}  // namespace sdirk3
}  // namespace wrf
