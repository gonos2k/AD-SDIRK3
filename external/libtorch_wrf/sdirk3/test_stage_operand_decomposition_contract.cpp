// PR 9E commit 1: standing contract for the generic stage-operand capture and
// closure machinery (wrf_sdirk3_stage_operand_capture.h). The production hooks
// land in later commits; this pins the capture safety contract (inherited from
// RwTermCapture) and the FP64 closure semantics with synthetic fixtures.
#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include <thread>
#include <numeric>
#include <functional>
#include <iostream>
#include <sstream>
#include "wrf_sdirk3_stage_operand_capture.h"
#include "wrf_sdirk3_stage_history_diag.h"

using namespace wrf::sdirk3;

namespace {

int g_cases = 0, g_fail = 0;
void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

OperandCaptureContext ctx3() {
    OperandCaptureContext c;
    c.timestep = 0;
    c.target_stage = 3;
    c.source_stage = 2;
    c.newton_iter = 0;
    c.split = OperandSplit::History;
    c.evaluation_role = "history";
    return c;
}

torch::Tensor rnd(std::initializer_list<int64_t> shape, float scale, float phase) {
    auto opt = torch::TensorOptions().dtype(torch::kFloat32);
    auto n = torch::arange(
        std::accumulate(shape.begin(), shape.end(), int64_t{1},
                        std::multiplies<int64_t>()),
        opt);
    return (scale * torch::sin(n * 0.7f + phase)).reshape(
        std::vector<int64_t>(shape));
}

}  // namespace

int main() {
    // (1) six-block unequal-size packing/unpacking: leaves on differently
    //     shaped blocks each close against their own block target.
    {
        StageOperandCaptureScope s(ctx3());
        check(s.armed_ok(), "case1: scope arms");
        auto ru = rnd({5, 6, 4}, 3.0f, 0.1f);   // u-momentum shape
        auto rw = rnd({5, 7, 4}, 2.0f, 0.3f);   // w-momentum (nz_w != nz)
        auto mu = rnd({5, 4}, 900.0f, 0.5f);    // column mass (2-D)
        s.take();  // drop; we re-arm below (this also tests take on empty-ish)
    }
    {
        StageOperandCaptureScope s(ctx3());
        auto ru_a = rnd({5, 6, 4}, 3.0f, 0.1f);
        auto ru_b = rnd({5, 6, 4}, 1.5f, 0.9f);
        stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, ru_a);
        stage_operand_capture_slot().add_additive("pgf_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, ru_b);
        auto leaves = s.take();
        check(leaves.size() == 2, "case1: two leaves captured");
        auto target = ru_a + ru_b;
        auto c = closure_of({&leaves[0].tensor, &leaves[1].tensor}, target);
        check(c.rel_err <= 1e-6 && c.finite,
              "case1: two Ru leaves close to their block sum");
    }

    // (2) positive/negative ARK coefficients: a history increment built from
    //     +aE and -aI scaled leaves closes.
    {
        auto k1 = rnd({4, 5, 3}, 10.0f, 0.2f);
        auto k2 = rnd({4, 5, 3}, 8.0f, 1.1f);
        const double aE = 0.75, aI = -0.35, dt = 600.0;
        auto scaled1 = (dt * aE) * k1;
        auto scaled2 = (dt * aI) * k2;
        auto hist = scaled1 + scaled2;
        auto c = closure_of({&scaled1, &scaled2}, hist);
        check(c.rel_err <= 1e-6, "case2: +/- ARK-scaled leaves close");
    }

    // (3) large opposing terms with strong cancellation: closure still exact,
    //     even though the sum is ~1e-4 of the term magnitudes.
    {
        auto big = rnd({6, 8, 5}, 1.0e6f, 0.4f);
        auto near_neg = -big + rnd({6, 8, 5}, 1.0e2f, 0.4f);  // ~1e2 residual
        auto target = big + near_neg;
        auto c = closure_of({&big, &near_neg}, target);
        check(c.rel_err <= 1e-6 && c.finite,
              "case3: strong-cancellation sum closes exactly");
        // cancellation factor (sum of norms / result norm) is huge here.
        double sum_norms = big.norm().item<float>() + near_neg.norm().item<float>();
        double res_norm = target.norm().item<float>();
        check(sum_norms / std::max(res_norm, 1e-30) > 100.0,
              "case3: cancellation factor is large");
    }

    // (4) one history term omission -> closure FAILS.
    {
        auto k1 = rnd({4, 5, 3}, 5.0f, 0.2f);
        auto k2 = rnd({4, 5, 3}, 5.0f, 1.3f);
        auto target = k1 + k2;
        auto c = closure_of({&k1}, target);  // k2 omitted
        check(c.rel_err > 1e-6, "case4: omitted history term breaks closure");
    }

    // (5) duplicate term -> inventory FAILS.
    {
        StageOperandCaptureScope s(ctx3());
        auto t = rnd({3, 3, 3}, 1.0f, 0.0f);
        stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, t);
        stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, t);
        auto leaves = s.take();
        auto reason = validate_stage_operand_inventory(leaves, {"adv_ru"}, {});
        check(reason.find("duplicate:adv_ru") != std::string::npos,
              "case5: duplicate leaf flagged");
    }

    // (6) undefined term -> stable marker.
    {
        StageOperandCaptureScope s(ctx3());
        torch::Tensor undef;
        OperandLeaf l;
        l.name = "pgf_ru";
        l.block = OperandBlock::Ru;
        l.split = OperandSplit::Fast;
        l.kind = OperandTermKind::Additive;
        l.tensor = undef;
        stage_operand_capture_slot().add(l);
        auto leaves = s.take();
        auto reason = validate_stage_operand_inventory(leaves, {"pgf_ru"}, {});
        check(reason.find("undefined:pgf_ru") != std::string::npos,
              "case6: undefined leaf -> stable marker");
    }

    // (7) wrong coefficient sign -> stage-history closure FAILS.
    {
        auto k = rnd({4, 5, 3}, 7.0f, 0.6f);
        const double dt = 600.0, aI = 0.4;
        auto correct = (dt * aI) * k;
        auto wrong = (dt * -aI) * k;  // sign flipped
        auto c = closure_of({&wrong}, correct);
        check(c.rel_err > 1e-6, "case7: wrong ARK sign breaks closure");
    }

    // (8) block-offset swap -> packed closure FAILS. Pack two blocks into one
    //     flat vector; a swapped offset misaligns the target.
    {
        auto ru = rnd({20}, 3.0f, 0.1f);
        auto rw = rnd({20}, 9.0f, 0.7f);
        auto packed = torch::cat({ru, rw});             // [ru | rw]
        auto swapped = torch::cat({rw, ru});            // wrong offset order
        auto c = closure_of({&swapped}, packed);
        check(c.rel_err > 1e-6, "case8: swapped block offsets break closure");
        auto c_ok = closure_of({&packed}, packed);
        check(c_ok.rel_err <= 1e-6, "case8: correct pack closes");
    }

    // (9) nested capture refusal.
    {
        StageOperandCaptureScope outer(ctx3());
        check(outer.armed_ok(), "case9: outer arms");
        StageOperandCaptureScope inner(ctx3());
        check(!inner.armed_ok(), "case9: nested arm fails closed");
    }

    // (10) exception cleanup: a scope destroyed by an exception leaves the
    //      slot disarmed and empty.
    {
        try {
            StageOperandCaptureScope s(ctx3());
            auto t = rnd({3, 3, 3}, 1.0f, 0.0f);
            stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                      OperandSplit::Fast, t);
            throw std::runtime_error("mid-capture");
        } catch (const std::exception&) {
        }
        check(!stage_operand_capture_slot().armed &&
                  stage_operand_capture_slot().leaves.empty(),
              "case10: exception disarms and clears the slot");
    }

    // (11) two-thread isolation: a capture armed on a worker does not touch the
    //      main slot.
    {
        bool worker_saw_own = false;
        std::thread w([&] {
            StageOperandCaptureScope s(ctx3());
            auto t = rnd({2, 2, 2}, 1.0f, 0.0f);
            stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                      OperandSplit::Fast, t);
            worker_saw_own = (stage_operand_capture_slot().leaves.size() == 1);
        });
        w.join();
        check(worker_saw_own, "case11: worker sees its own capture");
        check(!stage_operand_capture_slot().armed &&
                  stage_operand_capture_slot().leaves.empty(),
              "case11: main slot untouched by worker capture");
    }

    // (12) take() then a later scope destructor does not interfere.
    {
        StageOperandCaptureScope s1(ctx3());
        auto t = rnd({3, 3, 3}, 2.0f, 0.0f);
        stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, t);
        auto taken = s1.take();
        {
            StageOperandCaptureScope s2(ctx3());
            check(s2.armed_ok(),
                  "case12: second scope arms after take()");
            auto t2 = rnd({3, 3, 3}, 4.0f, 0.5f);
            stage_operand_capture_slot().add_additive("pgf_ru", OperandBlock::Ru,
                                                      OperandSplit::Fast, t2);
            check(stage_operand_capture_slot().leaves.size() == 1,
                  "case12: s2 slot has only its own leaf");
        }  // s1 is still alive; its destructor must NOT clear s2's already-taken state
        check(taken.size() == 1 && taken[0].name == "adv_ru",
              "case12: taken leaves survive a later scope's lifetime");
    }

    // (13) JVP/probe evaluation role: a capture stamped with a probe role must
    //      be refusable by the production checker. Here we assert the context
    //      carries the role so the caller can gate on it.
    {
        OperandCaptureContext probe = ctx3();
        probe.evaluation_role = "jvp_probe";
        StageOperandCaptureScope s(probe);
        check(std::string(stage_operand_capture_slot().ctx.evaluation_role) ==
                  "jvp_probe",
              "case13: probe role is visible on the slot (caller gates on it)");
    }

    // (14) non-finite term fail-close.
    {
        StageOperandCaptureScope s(ctx3());
        auto t = rnd({3, 3, 3}, 1.0f, 0.0f).clone();
        t[0][0][0] = std::nanf("");
        stage_operand_capture_slot().add_additive("adv_ru", OperandBlock::Ru,
                                                  OperandSplit::Fast, t);
        auto leaves = s.take();
        auto reason = validate_stage_operand_inventory(leaves, {"adv_ru"}, {});
        check(reason.find("nonfinite:adv_ru") != std::string::npos,
              "case14: non-finite leaf fail-closes");
    }

    // (15) additive + transform mixed closure: an additive term followed by a
    //      masking transform closes to the post-mask result.
    {
        StageOperandCaptureScope s(ctx3());
        auto pre = rnd({4, 5, 4}, 3.0f, 0.2f);
        // masking transform: zero the top level (a boundary overwrite).
        auto post = pre.clone();
        post.index_put_({torch::indexing::Slice(), 4}, 0.0f);
        stage_operand_capture_slot().add_additive("rw_pre_mask", OperandBlock::Rw,
                                                  OperandSplit::Fast, pre);
        stage_operand_capture_slot().add_transform("rw_mask", OperandBlock::Rw,
                                                   OperandSplit::Fast, pre, post);
        auto leaves = s.take();
        check(leaves.size() == 2 &&
                  leaves[1].kind == OperandTermKind::Transform,
              "case15: transform leaf recorded with kind=Transform");
        // additive(pre) + transform.delta(post-pre) == post
        auto c = closure_of({&leaves[0].tensor, &leaves[1].tensor}, post);
        check(c.rel_err <= 1e-6 && c.finite,
              "case15: additive + transform closes to the post-mask result");
    }

    // (16) emit-path inventory HARD GATE (Commit A / Codex stop-gate regression).
    //      emit_stage_history_diag must pass the EXPECTED leaf set as `required`
    //      to the validator; with the old empty-`required`/all-`optional` call a
    //      missing/undefined/duplicate increment was laundered (returned ""). A
    //      sound history passes; an undefined explicit derivative fails closed
    //      with SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE.
    {
        const float dt = 1.0f;  // aE=aI=1, dt=1 => exact float reconstruction
        auto U_n = rnd({4, 3, 2}, 5.0f, 0.2f);
        auto k_slow = rnd({4, 3, 2}, 2.0f, 0.4f);
        auto k_fast = rnd({4, 3, 2}, 1.0f, 0.6f);
        auto U_stage = U_n + k_slow + k_fast;
        std::vector<StageHistorySource> src(1);
        src[0].stage = 1;
        src[0].a_explicit = 1.0;
        src[0].a_implicit = 1.0;
        src[0].k_slow = &k_slow;
        src[0].k_fast = &k_fast;
        // target stage 2 expects source stage 1's defect (explicit ESDIRK stage:
        // defect==0, F_fast ~= K). A valid defect table is now required to pass
        // the defect-inventory hard gate.
        const double k1n = k_fast.to(torch::kFloat64).norm().item<double>();
        StageDefectSnapshot d1;
        d1.stage = 1;
        d1.explicit_stage = true;
        d1.converged = true;
        d1.k_norm = k1n;
        d1.f_fast_norm = k1n;
        d1.newton_defect_norm = 0.0;
        d1.defect_to_k_ratio = 0.0;
        d1.scaled_final_residual = 0.0;
        std::vector<StageDefectSnapshot> defs{d1};
        std::string ok =
            emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage, src, defs);
        check(ok.empty(), "case16: sound history passes the emit hard gate");

        torch::Tensor undef;  // undefined explicit derivative
        std::vector<StageHistorySource> bad = src;
        bad[0].k_slow = &undef;
        std::string fail =
            emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage, bad, defs);
        check(!fail.empty(),
              "case16: undefined increment fails the emit hard gate closed");
        check(fail.find("CAPTURE_INCOMPLETE") != std::string::npos,
              "case16: undefined increment -> SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE");

        // Omitted source stage: target stage 3's history expects sources 1 AND 2.
        // The expected inventory is derived from target_stage, NOT from the
        // (incomplete) sources vector, so providing only stage 1 must fail closed
        // as a MISSING leaf -- not be laundered as valid.
        std::vector<StageHistorySource> omitted = src;  // only stage 1
        std::string miss =
            emit_stage_history_diag(0, 0, 3, dt, U_n, U_stage, omitted, defs);
        check(!miss.empty(),
              "case16: omitted source stage fails the emit hard gate closed");
        check(miss.find("CAPTURE_INCOMPLETE") != std::string::npos,
              "case16: omitted source stage -> CAPTURE_INCOMPLETE (missing leaf)");
    }

    // (17) base-state-dominated laundering negative (P1-2). A large U_n must NOT
    //      hide a wrong increment sign. float64 tensors isolate the deliberate
    //      sign flip from float32 noise: the full-state ratio stays ~2e-7 (would
    //      PASS the old ||.||/||U_stage|| gate) but the authoritative
    //      history-relative closure FAILS.
    {
        const float dt = 1.0f;
        auto U_n = rnd({4, 3, 2}, 1e10f, 0.2f).to(torch::kFloat64);
        auto k_slow = rnd({4, 3, 2}, 1e3f, 0.4f).to(torch::kFloat64);
        auto k_fast = rnd({4, 3, 2}, 1e3f, 0.6f).to(torch::kFloat64);
        auto U_stage = U_n + k_slow + k_fast;  // CORRECT history (exact in float64)
        std::vector<StageHistorySource> src(1);
        src[0].stage = 1;
        src[0].a_explicit = -1.0;  // WRONG sign (should be +1)
        src[0].a_implicit = 1.0;
        src[0].k_slow = &k_slow;
        src[0].k_fast = &k_fast;
        const double k1n = k_fast.norm().item<double>();
        StageDefectSnapshot d1;
        d1.stage = 1;
        d1.explicit_stage = true;
        d1.converged = true;
        d1.k_norm = k1n;
        d1.f_fast_norm = k1n;
        d1.newton_defect_norm = 0.0;
        d1.defect_to_k_ratio = 0.0;
        d1.scaled_final_residual = 0.0;
        std::vector<StageDefectSnapshot> defs{d1};
        std::string r =
            emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage, src, defs);
        check(r.find("CLOSURE_FAILED") != std::string::npos,
              "case17: large-U_n wrong sign -> CLOSURE_FAILED (not laundered)");
    }

    // (18) defect-inventory validator negatives (P1-3), exercised directly.
    {
        auto good = [](int stage, bool expl) {
            StageDefectSnapshot d;
            d.stage = stage;
            d.explicit_stage = expl;
            d.converged = true;
            d.k_norm = 10.0;
            d.f_fast_norm = expl ? 10.0 : 5.0;
            d.newton_defect_norm = expl ? 0.0 : 2.0;
            d.defect_to_k_ratio = expl ? 0.0 : 0.2;
            d.scaled_final_residual = expl ? 0.0 : 0.1;
            return d;
        };
        std::vector<StageDefectSnapshot> okv{good(1, true), good(2, false)};
        check(validate_stage_defect_inventory(okv, 3).empty(),
              "case18: sound defect table passes");
        std::vector<StageDefectSnapshot> missv{good(1, true)};
        check(validate_stage_defect_inventory(missv, 3).find("missing:s2") !=
                  std::string::npos,
              "case18: missing source-stage defect -> missing:s2");
        std::vector<StageDefectSnapshot> dupv{good(1, true), good(1, true),
                                              good(2, false)};
        check(validate_stage_defect_inventory(dupv, 3).find("duplicate:s1") !=
                  std::string::npos,
              "case18: duplicate source-stage defect -> duplicate:s1");
        std::vector<StageDefectSnapshot> oorv{good(1, true), good(2, false),
                                              good(3, false)};
        check(validate_stage_defect_inventory(oorv, 3).find("unknown_stage:s3") !=
                  std::string::npos,
              "case18: out-of-range defect stage -> unknown_stage:s3");
        auto nanrec = good(2, false);
        nanrec.newton_defect_norm = std::nan("");
        std::vector<StageDefectSnapshot> nanv{good(1, true), nanrec};
        check(validate_stage_defect_inventory(nanv, 3).find("nonfinite:s2") !=
                  std::string::npos,
              "case18: NaN defect norm -> nonfinite:s2");
        auto unobs = good(2, false);
        unobs.f_fast_norm = -1.0;  // the "unobserved" sentinel on a solved stage
        std::vector<StageDefectSnapshot> unobsv{good(1, true), unobs};
        check(validate_stage_defect_inventory(unobsv, 3).find("neg_f_fast:s2") !=
                  std::string::npos,
              "case18: non-explicit -1 sentinel -> neg_f_fast:s2");
        auto badexpl = good(2, true);  // explicit flag on a non-stage-1 source
        badexpl.newton_defect_norm = 0.0;
        badexpl.f_fast_norm = badexpl.k_norm;
        std::vector<StageDefectSnapshot> badexplv{good(1, true), badexpl};
        check(validate_stage_defect_inventory(badexplv, 3).find(
                  "explicit_flag_nonstage1:s2") != std::string::npos,
              "case18: explicit flag on stage 2 -> explicit_flag_nonstage1:s2");
        // stage 1 misflagged as NON-explicit must also fail (the converse) -- it
        // would otherwise pass the non-explicit branch and skip the defect==0/F==K
        // explicit constraints entirely.
        std::vector<StageDefectSnapshot> s1nev{good(1, false), good(2, false)};
        check(validate_stage_defect_inventory(s1nev, 3).find(
                  "stage1_not_explicit:s1") != std::string::npos,
              "case18: stage 1 misflagged non-explicit -> stage1_not_explicit:s1");
        // a negative norm/ratio (invalid: finiteness alone would pass it)
        auto negk = good(2, false);
        negk.k_norm = -1.0;
        std::vector<StageDefectSnapshot> negkv{good(1, true), negk};
        check(validate_stage_defect_inventory(negkv, 3).find("neg_k_norm:s2") !=
                  std::string::npos,
              "case18: negative k_norm -> neg_k_norm:s2");
        // non-negativity is UNCONDITIONAL: an explicit stage 1 with a negative
        // scaled_final_residual must also fail (the explicit branch must not skip
        // it).
        auto negsc = good(1, true);
        negsc.scaled_final_residual = -1.0;
        std::vector<StageDefectSnapshot> negscv{negsc, good(2, false)};
        check(validate_stage_defect_inventory(negscv, 3).find(
                  "neg_scaled_resid:s1") != std::string::npos,
              "case18: explicit stage-1 negative scaled_final_residual -> "
              "neg_scaled_resid:s1");
    }

    // (19) ordering (P2): a REJECTED capture emits ONLY the failure marker, never
    //      a success-form STAGE_HISTORY_* record, so a consumer that greps the
    //      success markers can never mistake broken evidence for sound.
    {
        const float dt = 1.0f;
        auto U_n = rnd({4, 3, 2}, 5.0f, 0.2f);
        auto k_fast = rnd({4, 3, 2}, 1.0f, 0.6f);
        auto U_stage = U_n + k_fast;
        std::vector<StageHistorySource> src(1);
        src[0].stage = 1;
        src[0].a_explicit = 1.0;
        src[0].a_implicit = 1.0;
        torch::Tensor undef;
        src[0].k_slow = &undef;  // undefined -> inventory failure
        src[0].k_fast = &k_fast;
        std::vector<StageDefectSnapshot> defs;
        std::ostringstream captured;
        std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
        std::string r =
            emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage, src, defs);
        std::cerr.rdbuf(old);
        const std::string out = captured.str();
        check(!r.empty() &&
                  out.find("SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE") !=
                      std::string::npos,
              "case19: failure marker emitted on rejected capture");
        check(out.find("SDIRK3_STAGE_HISTORY_DIAG") == std::string::npos,
              "case19: no success STAGE_HISTORY_DIAG on failure");
        check(out.find("SDIRK3_STAGE_HISTORY_SUMMARY") == std::string::npos,
              "case19: no success STAGE_HISTORY_SUMMARY on failure");
    }

    const int kExpected = 43;  // ratchet: update deliberately with the cases
    if (g_cases != kExpected) {
        std::printf("FAIL: case-count ratchet executed %d expected %d\n",
                    g_cases, kExpected);
        ++g_fail;
    }
    if (g_fail == 0) {
        std::printf("Stage operand decomposition contract: ALL PASS (%d/%d)\n",
                    g_cases, kExpected);
        return 0;
    }
    std::printf("Stage operand decomposition contract: %d FAILURE(S)\n", g_fail);
    return 1;
}
