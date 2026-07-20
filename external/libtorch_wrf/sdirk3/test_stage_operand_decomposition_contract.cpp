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
#include <cstring>
#include <cstdlib>
#include "wrf_sdirk3_stage_operand_capture.h"
#include "wrf_sdirk3_stage_history_diag.h"

using namespace wrf::sdirk3;

extern "C" int sdirk3_rhs_run_close_checked(int exit_kind, int reason) noexcept;

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

// ---------------------------------------------------------------------------
// PR 9F.3: child modes. The cached WRF_SDIRK3_RHS_COUNT flag is latched false the
// first time it is read, so the ENABLED emitter path cannot be exercised in the
// parent process at all -- every prior "test" of it drove pure predicates or the
// storage accessor directly, never the constructor, the tick, the destructor, or
// the record text. A child process started with the flag SET is the only way to
// run the real thing, and its stderr is the real emitter output that the real
// shell parser then judges.
static int run_child_mode(const char* mode) {
    if (std::strcmp(mode, "one-sweep") == 0) {
        wrf::sdirk3::Sdirk3RhsSweepScope scope;
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        return 0;   // scope destructor emits `end` with delta=3
    }
    if (std::strcmp(mode, "nested") == 0) {
        wrf::sdirk3::Sdirk3RhsSweepScope outer;
        wrf::sdirk3::sdirk3_rhs_count_tick();
        {
            wrf::sdirk3::Sdirk3RhsSweepScope inner;   // inner begins AND ends inside
            wrf::sdirk3::sdirk3_rhs_count_tick();
        }
        wrf::sdirk3::sdirk3_rhs_count_tick();
        return 0;   // BOTH records must carry concurrent=1
    }
    if (std::strcmp(mode, "authority-frozen") == 0) {
        // The authority closes first (as Fortran does), then a FALLBACK fires. The
        // fallback must NOT produce a second, differently-attributed record: the
        // frozen {exit,total,authority} is re-emitted verbatim or nothing is.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME);
        // now the C++ fallbacks, in both flavours
        wrf::sdirk3::sdirk3_rhs_run_emit_end_once(wrf::sdirk3::Sdirk3RunExit::Fatal);
        wrf::sdirk3::sdirk3_rhs_run_emit_end_once(wrf::sdirk3::Sdirk3RunExit::Clean);
        return 0;
    }
    if (std::strcmp(mode, "close-conflict") == 0) {
        // A second close asking for a DIFFERENT outcome is a wiring bug, and must be
        // reported rather than silently answered with the first verdict.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        const int a = sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME);
        const int b = sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_FINALIZE);
        std::fprintf(stderr, "CLOSE_RC first=%d second=%d\n", a, b);
        return 0;
    }
    if (std::strcmp(mode, "close-clean") == 0) {
        // The finalizer authority. close_checked(CLEAN) must emit the run total
        // attributed to fortran_finalize -- the ABI-mapping half of the normal-exit
        // path. (The other half, the Fortran finalizer actually calling this, is
        // compile-verified; its runtime emission needs a COMPLETING SDIRK3 run,
        // which em_b_wave cannot yet produce at any dt -- see the scope note.)
        wrf::sdirk3::sdirk3_rhs_count_tick();
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_FINALIZE);
        return 0;
    }
    if (std::strcmp(mode, "lifecycle-concurrent-close") == 0) {
        // The property the OLD spin could not guarantee: under many workers racing to
        // close, NO closing record is lost and every emitted record is identical.
        // The single CAS RUNNING->CLOSED freezes {count,exit,auth}; losers re-decode
        // the same word. There is no winner-publication window to be preempted in.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        std::vector<std::thread> ts;
        std::atomic<int> ones{0};
        for (int i = 0; i < 8; ++i)
            ts.emplace_back([&] {
                if (sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME) == 1)
                    ones.fetch_add(1, std::memory_order_relaxed);
            });
        for (auto& t : ts) t.join();
        std::fprintf(stderr, "CONCURRENT_CLOSE_ONES=%d\n", ones.load());
        return 0;
    }
    if (std::strcmp(mode, "lifecycle-post-close-tick") == 0) {
        // tick -> close -> tick: the trailing tick must be REJECTED. On the linearized
        // word a tick after CLOSED returns 0 and flags post_close_tick, so a control
        // flow that evaluates the RHS past the fatal decision cannot hide.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME);
        const int rc = wrf::sdirk3::sdirk3_run_tick();
        std::fprintf(stderr, "POST_CLOSE_TICK_RC=%d\n", rc);
        // a second close re-emits, now carrying the post_close_tick flag
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME);
        return 0;
    }
    if (std::strcmp(mode, "lifecycle-reinit") == 0) {
        // clean finalize -> re-init -> a NEW generation. The old process-once static
        // machinery would have re-emitted generation 1's frozen record; the word
        // opens generation 2 with its own count.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_FINALIZE);
        wrf::sdirk3::sdirk3_rhs_run_begin_checked_impl();   // explicit re-init
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_FINALIZE);
        return 0;
    }
    if (std::strcmp(mode, "close-disabled") == 0) {
        // Contract: with counting disabled the close is a total no-op returning 1.
        const int rc = sdirk3_rhs_run_close_checked(wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL, wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME);
        std::fprintf(stderr, "CLOSE_RC disabled=%d\n", rc);
        return 0;
    }
    if (std::strcmp(mode, "abort") == 0) {
        // The exit path the acceptance run actually takes. A controlled fatal ends
        // in MPI_Abort/std::abort, which run NO static destructors -- so if the
        // closing run total is only emitted from one, it is structurally
        // unreachable here and the harness gate could only ever fail.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::mpi_safety::abort_c_abi_exception("child_abort_mode",
                                                       "deliberate controlled fatal");
    }
    if (std::strcmp(mode, "no-sweep") == 0) {
        // RHS calls with no sweep open at all: invisible to the sweep table, which
        // is precisely why the whole-run total exists.
        wrf::sdirk3::sdirk3_rhs_count_tick();
        wrf::sdirk3::sdirk3_rhs_count_tick();
        return 0;
    }
    std::fprintf(stderr, "unknown child mode: %s\n", mode);
    return 2;
}

// Run this same binary as a child with counting ENABLED and capture its stderr.
static std::string capture_child(const char* self, const char* mode) {
    std::string cmd = "WRF_SDIRK3_RHS_COUNT=1 '";
    cmd += self;
    cmd += "' --child ";
    cmd += mode;
    cmd += " 2>&1";
    std::string out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    return out;
}
static int count_occurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t i = hay.find(needle); i != std::string::npos;
         i = hay.find(needle, i + needle.size())) ++n;
    return n;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0)
        return run_child_mode(argv[2]);
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
        auto U_n = rnd({24}, 5.0f, 0.2f);
        auto k_slow = rnd({24}, 2.0f, 0.4f);
        auto k_fast = rnd({24}, 1.0f, 0.6f);
        auto U_stage = U_n + k_slow + k_fast;
        std::vector<StageHistorySource> src(1);
        src[0].stage = 1;
        src[0].a_explicit = 1.0;
        src[0].a_implicit = 1.0;
        src[0].k_slow = &k_slow;
        src[0].k_fast = &k_fast;
        // target stage 2 expects source stage 1's defect (explicit ESDIRK stage:
        // convergence NOT APPLICABLE -> every Newton field is the kDefectNA
        // sentinel and no converged claim). A valid defect table is now required
        // to pass the defect-inventory hard gate.
        const double k1n = k_fast.to(torch::kFloat64).norm().item<double>();
        StageDefectSnapshot d1;
        d1.stage = 1;
        d1.explicit_stage = true;
        d1.converged = false;
        d1.k_norm = k1n;
        d1.f_fast_norm = kDefectNA;
        d1.newton_defect_norm = kDefectNA;
        d1.defect_to_k_ratio = kDefectNA;
        d1.scaled_final_residual = kDefectNA;
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
        auto U_n = rnd({24}, 1e10f, 0.2f).to(torch::kFloat64);
        auto k_slow = rnd({24}, 1e3f, 0.4f).to(torch::kFloat64);
        auto k_fast = rnd({24}, 1e3f, 0.6f).to(torch::kFloat64);
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
        d1.converged = false;
        d1.k_norm = k1n;
        d1.f_fast_norm = kDefectNA;
        d1.newton_defect_norm = kDefectNA;
        d1.defect_to_k_ratio = kDefectNA;
        d1.scaled_final_residual = kDefectNA;
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
            d.k_norm = 10.0;
            if (expl) {
                // convergence NOT APPLICABLE -> kDefectNA + no converged claim
                d.converged = false;
                d.f_fast_norm = kDefectNA;
                d.newton_defect_norm = kDefectNA;
                d.defect_to_k_ratio = kDefectNA;
                d.scaled_final_residual = kDefectNA;
            } else {
                d.converged = true;
                d.f_fast_norm = 5.0;
                d.newton_defect_norm = 2.0;
                d.defect_to_k_ratio = 0.2;
                d.scaled_final_residual = 0.1;
            }
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
        std::vector<StageDefectSnapshot> badexplv{good(1, true), badexpl};
        check(validate_stage_defect_inventory(badexplv, 3).find(
                  "explicit_flag_nonstage1:s2") != std::string::npos,
              "case18: explicit flag on stage 2 -> explicit_flag_nonstage1:s2");
        // stage 1 misflagged as NON-explicit must also fail (the converse) -- it
        // would otherwise be judged by the implicit contract and dodge the
        // n/a-sentinel explicit constraints entirely.
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
        // The implicit role keeps unconditional non-negativity: a non-explicit
        // stage with a negative scaled_final_residual fails.
        auto negsc = good(2, false);
        negsc.scaled_final_residual = -0.5;
        std::vector<StageDefectSnapshot> negscv{good(1, true), negsc};
        check(validate_stage_defect_inventory(negscv, 3).find(
                  "neg_scaled_resid:s2") != std::string::npos,
              "case18: implicit negative scaled_final_residual -> "
              "neg_scaled_resid:s2");
    }

    // (19) ordering (P2): a REJECTED capture emits ONLY the failure marker, never
    //      a success-form STAGE_HISTORY_* record, so a consumer that greps the
    //      success markers can never mistake broken evidence for sound.
    {
        const float dt = 1.0f;
        auto U_n = rnd({24}, 5.0f, 0.2f);
        auto k_fast = rnd({24}, 1.0f, 0.6f);
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

    // (20) AUTHORITATIVE per-source ARK attribution (C1). States are built with
    //      the IDENTICAL FP32 expression production/emitter use, so a correct
    //      attribution re-applies bit-exactly; a source label swap or a +delta
    //      coefficient error misses the captured state and fails.
    {
        const float dt = 1.0f;
        auto U_n = rnd({18}, 5.0f, 0.1f);
        auto ks0 = rnd({18}, 2.0f, 0.2f), kf0 = rnd({18}, 1.0f, 0.3f);
        auto ks1 = rnd({18}, 1.5f, 0.4f), kf1 = rnd({18}, 0.8f, 0.5f);
        auto state0 = U_n + dt * 1.0f * ks0 + dt * 1.0f * kf0;
        auto state1 = state0 + dt * 1.0f * ks1 + dt * 1.0f * kf1;
        auto U_stage = state1;
        std::vector<StageHistorySource> src(2);
        src[0] = {1, 1.0, 1.0, &ks0, &kf0};
        src[1] = {2, 1.0, 1.0, &ks1, &kf1};
        std::vector<const torch::Tensor*> states{&state0, &state1};
        std::vector<StageOperandBlock> blocks = {{"ru", 0, 6}, {"rv", 6, 3},
                                                 {"rw", 9, 2}, {"ph", 11, 2},
                                                 {"t", 13, 3}, {"mu", 16, 2}};
        check(emit_stage_applied_delta_diag(0, 0, 3, dt, U_n, U_stage, src,
                                            states, blocks)
                  .empty(),
              "case20: correct attribution re-applies bit-exactly (passes)");
        // k-pointer swap (labels correct, k wrong) -> caught by the reapply.
        std::vector<StageHistorySource> kswap = src;
        kswap[0].k_slow = &ks1; kswap[0].k_fast = &kf1;
        kswap[1].k_slow = &ks0; kswap[1].k_fast = &kf0;
        check(emit_stage_applied_delta_diag(0, 0, 3, dt, U_n, U_stage, kswap,
                                            states, blocks)
                  .find("ATTRIBUTION_FAILED") != std::string::npos,
              "case20: source k-pointer swap -> ATTRIBUTION_FAILED (reapply)");
        // pure LABEL swap (k/coeff correct at each position, stage labels
        // swapped) -> caught by the label authority, which the reapply alone
        // cannot enforce.
        std::vector<StageHistorySource> labelswap = src;
        labelswap[0].stage = 2; labelswap[1].stage = 1;
        check(emit_stage_applied_delta_diag(0, 0, 3, dt, U_n, U_stage, labelswap,
                                            states, blocks)
                  .find("label_mismatch") != std::string::npos,
              "case20: pure source-label swap -> label_mismatch");
        std::vector<StageHistorySource> perturbed = src;
        perturbed[0].a_explicit = 1.5;  // +delta coefficient error
        check(emit_stage_applied_delta_diag(0, 0, 3, dt, U_n, U_stage, perturbed,
                                            states, blocks)
                  .find("ATTRIBUTION_FAILED") != std::string::npos,
              "case20: +delta coefficient error -> ATTRIBUTION_FAILED");
    }

    // (21) huge-base + ULP increment: a CORRECT production assembly (where FP32
    //      rounding partially absorbs the increment into the base) must PASS,
    //      because the emitter re-applies with the identical FP32 op and rounds
    //      identically -- validating the real production rounding regime.
    {
        const float dt = 1.0f;
        // Constant 1e9 base -> uniform FP32 ULP (~119).
        auto U_n = torch::full({8}, 1.0e9f, torch::kFloat32);
        auto ks0 = rnd({8}, 1.0e2f, 0.2f), kf0 = rnd({8}, 1.0e2f, 0.3f);
        auto state0 = U_n + dt * 1.0f * ks0 + dt * 1.0f * kf0;  // real FP32 (ULP loss)
        std::vector<StageHistorySource> src(1);
        src[0] = {1, 1.0, 1.0, &ks0, &kf0};
        std::vector<const torch::Tensor*> states{&state0};
        std::vector<StageOperandBlock> blocks = {{"ru", 0, 8}};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, src, states,
                                            blocks)
                  .empty(),
              "case21: huge-base ULP correct production passes (FP32 regime)");
        // Huge-base MISATTRIBUTION: a wrong increment offset by ~2.5 ULP (300 at
        // 1e9). Its residual (~238) is BELOW the old 4*eps*|state| (~476)
        // tolerance -- so a magnitude-scaled tolerance would have hidden it -- but
        // strict bit-exact replay catches it.
        auto ks_wrong = ks0 + 300.0f;
        std::vector<StageHistorySource> bad(1);
        bad[0] = {1, 1.0, 1.0, &ks_wrong, &kf0};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, bad, states,
                                            blocks)
                  .find("ATTRIBUTION_FAILED") != std::string::npos,
              "case21: huge-base sub-tolerance misattribution -> ATTRIBUTION_FAILED "
              "(bit-exact catches what eps*|state| would mask)");
    }

    // (22) huge ru block, tiny mu block, mu sign error: a global-RMS floor would
    //      bury the mu error under the ru magnitude; per-block localization must
    //      fail specifically on mu.
    {
        const float dt = 1.0f;
        auto U_n = rnd({10}, 1.0f, 0.1f);
        auto ks0 = torch::cat({rnd({8}, 1.0e6f, 0.2f), rnd({2}, 1.0f, 0.3f)});
        auto kf0 = torch::zeros({10}, torch::kFloat32);
        auto state0 = U_n + dt * 1.0f * ks0 + dt * 1.0f * kf0;  // correct production
        auto ks0_wrong = ks0.clone();
        ks0_wrong.index_put_({torch::indexing::Slice(8, 10)},
                             -ks0.slice(0, 8, 10));  // flip mu sign only
        std::vector<StageHistorySource> src(1);
        src[0] = {1, 1.0, 1.0, &ks0_wrong, &kf0};
        std::vector<const torch::Tensor*> states{&state0};
        std::vector<StageOperandBlock> blocks = {{"ru", 0, 8}, {"mu", 8, 2}};
        std::string r = emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0,
                                                      src, states, blocks);
        check(r.find("ATTRIBUTION_FAILED") != std::string::npos &&
                  r.find("mu@s1") != std::string::npos,
              "case22: huge-ru small-mu sign error fails on the mu block");
    }

    // (23) non-finite input must FAIL CLOSED. With strict residual==0,
    //      std::max(0,NaN)==0 and NaN>0 is false, so a NaN residual would fail
    //      OPEN (emit a "successful" attribution) without an explicit finite gate.
    {
        const float dt = 1.0f;
        auto U_n = rnd({6}, 5.0f, 0.1f);
        auto ks0 = rnd({6}, 2.0f, 0.2f), kf0 = rnd({6}, 1.0f, 0.3f);
        auto state0 = U_n + dt * 1.0f * ks0 + dt * 1.0f * kf0;
        auto ks_nan = ks0.clone();
        ks_nan.index_put_({0}, std::nanf(""));  // NaN in the source
        std::vector<StageHistorySource> src(1);
        src[0] = {1, 1.0, 1.0, &ks_nan, &kf0};
        std::vector<const torch::Tensor*> states{&state0};
        std::vector<StageOperandBlock> blocks = {{"ru", 0, 6}};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, src, states,
                                            blocks)
                  .find("nonfinite") != std::string::npos,
              "case23: non-finite source input -> nonfinite (fails closed, not open)");
    }

    // (24) structural preflight (P1-5): a shape/dtype/dimensionality mismatch
    //      fails with a STABLE marker before any tensor arithmetic, not a generic
    //      LibTorch throw or a silent broadcast.
    {
        const float dt = 1.0f;
        auto U_n = rnd({6}, 5.0f, 0.1f);
        auto ks0 = rnd({6}, 2.0f, 0.2f), kf0 = rnd({6}, 1.0f, 0.3f);
        auto state0 = U_n + dt * 1.0f * ks0 + dt * 1.0f * kf0;
        std::vector<StageOperandBlock> blocks = {{"ru", 0, 6}};
        std::vector<const torch::Tensor*> states{&state0};
        auto ks_short = rnd({5}, 2.0f, 0.2f);  // N-1 length
        std::vector<StageHistorySource> src_shape(1);
        src_shape[0] = {1, 1.0, 1.0, &ks_short, &kf0};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, src_shape,
                                            states, blocks)
                  .find("SHAPE_MISMATCH") != std::string::npos,
              "case24: N-1 operand -> SHAPE_MISMATCH");
        auto ks_2d = ks0.reshape({6, 1});  // 2-D
        std::vector<StageHistorySource> src_2d(1);
        src_2d[0] = {1, 1.0, 1.0, &ks_2d, &kf0};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, src_2d,
                                            states, blocks)
                  .find("SHAPE_MISMATCH") != std::string::npos,
              "case24: 2-D operand -> SHAPE_MISMATCH");
        auto ks_f64 = ks0.to(torch::kFloat64);  // wrong dtype
        std::vector<StageHistorySource> src_dtype(1);
        src_dtype[0] = {1, 1.0, 1.0, &ks_f64, &kf0};
        check(emit_stage_applied_delta_diag(0, 0, 2, dt, U_n, state0, src_dtype,
                                            states, blocks)
                  .find("DTYPE_MISMATCH") != std::string::npos,
              "case24: FP64 operand -> DTYPE_MISMATCH");
    }

    // (25) explicit-stage N/A semantics (P1-1): an explicit ESDIRK stage runs NO
    //      Newton solve, so convergence is NOT APPLICABLE. Its record must carry
    //      the kDefectNA sentinel for every Newton field and MUST NOT claim
    //      converged. The old synthetic record (f_fast forced == k_norm, defect
    //      == 0, converged copied from the solver's last_stage_converged_) made
    //      the validator self-fulfilling -- each of those manufactured shapes
    //      must now FAIL, and the expected value (the sentinel) is INDEPENDENT of
    //      k_norm, so no norm coincidence can pass it.
    {
        auto expl_na = []() {
            StageDefectSnapshot d;
            d.stage = 1;
            d.explicit_stage = true;
            d.converged = false;
            d.k_norm = 10.0;
            d.f_fast_norm = kDefectNA;
            d.newton_defect_norm = kDefectNA;
            d.defect_to_k_ratio = kDefectNA;
            d.scaled_final_residual = kDefectNA;
            return d;
        };
        auto impl2 = []() {
            StageDefectSnapshot d;
            d.stage = 2;
            d.explicit_stage = false;
            d.converged = true;
            d.k_norm = 8.0;
            d.f_fast_norm = 3.0;
            d.newton_defect_norm = 1.0;
            d.defect_to_k_ratio = 0.125;
            d.scaled_final_residual = 0.05;
            return d;
        };
        std::vector<StageDefectSnapshot> okv{expl_na(), impl2()};
        check(validate_stage_defect_inventory(okv, 3).empty(),
              "case25: canonical explicit N/A defect record passes");
        // manufactured f_fast == k_norm (the OLD self-fulfilling value) FAILS:
        // "F_fast ~= K" is no longer a pass path.
        auto manuF = expl_na();
        manuF.f_fast_norm = manuF.k_norm;  // 10.0, not kDefectNA
        std::vector<StageDefectSnapshot> manuFv{manuF, impl2()};
        check(validate_stage_defect_inventory(manuFv, 3).find(
                  "explicit_f_fast_not_na:s1") != std::string::npos,
              "case25: manufactured f_fast==k_norm -> explicit_f_fast_not_na "
              "(not self-fulfilling)");
        // F == -K yields ||F|| == ||K|| just as F == K does; because the contract
        // is the n/a sentinel (not a norm comparison), it is rejected identically
        // -- the norm-only trap the reviewer named cannot pass.
        auto negF = expl_na();
        negF.f_fast_norm = negF.k_norm;  // ||F||==||K|| regardless of sign
        check(!validate_stage_defect_inventory({negF, impl2()}, 3).empty(),
              "case25: ||F||==||K|| explicit record rejected (no norm-only pass)");
        // synthesized defect == 0 (a real observed 0, not n/a) also fails
        auto zeroDef = expl_na();
        zeroDef.newton_defect_norm = 0.0;
        std::vector<StageDefectSnapshot> zeroDefv{zeroDef, impl2()};
        check(validate_stage_defect_inventory(zeroDefv, 3).find(
                  "explicit_defect_not_na:s1") != std::string::npos,
              "case25: synthesized defect==0 -> explicit_defect_not_na");
        // converged claim on an explicit record (last_stage_converged_ pollution)
        // fails -- an explicit stage carries NO convergence verdict.
        auto polluted = expl_na();
        polluted.converged = true;
        std::vector<StageDefectSnapshot> pollv{polluted, impl2()};
        check(validate_stage_defect_inventory(pollv, 3).find(
                  "explicit_converged_claim:s1") != std::string::npos,
              "case25: explicit converged claim -> explicit_converged_claim "
              "(no last_stage_converged_ pollution)");
    }

    // (26) coherent Newton-defect tensor authority (P1-4): the {K,F,R} triple must
    //      be internally coherent (||K-F-R||==0) AND belong to the stage value the
    //      solve returned (K==returned_K) -- both derived by the emitter FROM THE
    //      TENSORS, never a caller scalar. A stale/foreign or incoherent triple
    //      fails closed.
    {
        auto K = rnd({6}, 4.0f, 0.3f);
        auto F = rnd({6}, 1.5f, 0.9f);
        auto R = K - F;  // coherent, bit-exact in float32
        auto make = [&](DefectEvaluationPoint p) {
            StageDefectTensorSnapshot s;
            s.stage = 2;
            s.retry_generation = 1;
            s.newton_iter = 0;
            s.point = p;
            s.K = K;
            s.F = F;
            s.R = R;
            s.returned_K = K;
            return s;
        };
        double defect = -1, closure = -1, ratio = -1;
        check(validate_stage_defect_tensor(make(DefectEvaluationPoint::ResidualEval),
                                           &defect, &closure, &ratio)
                  .empty(),
              "case26: coherent {K,F,R} + K==returned_K passes");
        check(closure == 0.0 && defect > 0.0 && ratio > 0.0,
              "case26: emitter derives ||K-F-R||==0 and ||R||>0 from the tensors");
        check(validate_stage_defect_tensor(make(DefectEvaluationPoint::Unobserved))
                  .find("DEFECT_UNOBSERVED") != std::string::npos,
              "case26: unobserved point -> DEFECT_UNOBSERVED");
        auto mism = make(DefectEvaluationPoint::ResidualEval);
        mism.returned_K = K + rnd({6}, 0.01f, 0.1f);  // F/R from a different point
        check(validate_stage_defect_tensor(mism).find("DEFECT_UNOBSERVED") !=
                  std::string::npos,
              "case26: captured K != returned K -> DEFECT_UNOBSERVED");
        auto inc = make(DefectEvaluationPoint::ResidualEval);
        inc.R = R + rnd({6}, 0.5f, 0.2f);  // ||K-F-R|| != 0
        check(validate_stage_defect_tensor(inc).find("DEFECT_INCOHERENT") !=
                  std::string::npos,
              "case26: ||K-F-R|| != 0 -> DEFECT_INCOHERENT");
        auto badshape = make(DefectEvaluationPoint::ResidualEval);
        badshape.F = rnd({5}, 1.5f, 0.9f);  // wrong length
        check(validate_stage_defect_tensor(badshape).find("SHAPE_MISMATCH") !=
                  std::string::npos,
              "case26: mismatched-length F -> SHAPE_MISMATCH");
        auto nanr = make(DefectEvaluationPoint::ResidualEval);
        auto rnan = R.clone();
        rnan.index_put_({0}, std::nan(""));
        nanr.R = rnan;  // non-finite closure/defect
        check(!validate_stage_defect_tensor(nanr).empty(),
              "case26: non-finite R rejected (NONFINITE)");
    }

    // (27) birth-time immutable source snapshot (P1-3): a source that references a
    //      DETACHED BIRTH CLONE keeps the value the derivative had at birth even
    //      after the live derivative is mutated in place, so the provenance closure
    //      reflects birth -- not the later mutation.
    {
        const float dt = 1.0f;
        auto U_n = rnd({24}, 5.0f, 0.2f);
        auto k_slow_live = rnd({24}, 2.0f, 0.4f);
        auto k_fast_live = rnd({24}, 1.0f, 0.6f);
        auto k_slow_birth = k_slow_live.detach().clone();  // what the tile stores
        auto k_fast_birth = k_fast_live.detach().clone();
        auto U_stage = U_n + k_slow_birth + k_fast_birth;  // history from BIRTH values
        const double k1n = k_fast_birth.to(torch::kFloat64).norm().item<double>();
        StageDefectSnapshot d1;
        d1.stage = 1;
        d1.explicit_stage = true;
        d1.converged = false;
        d1.k_norm = k1n;
        d1.f_fast_norm = kDefectNA;
        d1.newton_defect_norm = kDefectNA;
        d1.defect_to_k_ratio = kDefectNA;
        d1.scaled_final_residual = kDefectNA;
        std::vector<StageDefectSnapshot> defs{d1};
        auto mk_src = [&](const torch::Tensor* ks, const torch::Tensor* kf) {
            std::vector<StageHistorySource> src(1);
            src[0].stage = 1;
            src[0].birth_generation = 7;
            src[0].a_explicit = 1.0;
            src[0].a_implicit = 1.0;
            src[0].k_slow = ks;
            src[0].k_fast = kf;
            return src;
        };
        // CORRUPT the live derivatives in place AFTER birth.
        {
            torch::NoGradGuard g;
            k_slow_live.mul_(3.0f);
            k_fast_live.add_(7.0f);
        }
        // the birth-clone source still closes against U_stage (birth values)
        check(emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage,
                                      mk_src(&k_slow_birth, &k_fast_birth), defs)
                  .empty(),
              "case27: birth-clone source survives live-derivative mutation");
        // the SAME source on the MUTATED live tensors fails -- proving the closure
        // depends on the derivative values, so consuming the clone mattered.
        check(!emit_stage_history_diag(0, 0, 2, dt, U_n, U_stage,
                                       mk_src(&k_slow_live, &k_fast_live), defs)
                   .empty(),
              "case27: same source on mutated live tensors fails (clone mattered)");
    }

    // (28) line-atomic output helper (P2): emit_sdirk3_diag_line writes the exact
    //      line, and concurrent emitters never interleave characters within a line
    //      (mirrors the solver's NEWTON/FGMRES diag serialization).
    {
        std::ostringstream cap;
        auto* old = std::cerr.rdbuf(cap.rdbuf());
        emit_sdirk3_diag_line("[SDIRK3_TEST] a=1 b=2\n");
        std::cerr.rdbuf(old);
        check(cap.str() == "[SDIRK3_TEST] a=1 b=2\n",
              "case28: emit_sdirk3_diag_line writes the exact line");

        std::ostringstream cap2;
        auto* old2 = std::cerr.rdbuf(cap2.rdbuf());
        {
            std::vector<std::thread> ts;
            for (int t = 0; t < 8; ++t)
                ts.emplace_back([] {
                    for (int i = 0; i < 50; ++i)
                        emit_sdirk3_diag_line("[SDIRK3_TEST] LINE_MARK end\n");
                });
            for (auto& th : ts) th.join();
        }
        std::cerr.rdbuf(old2);
        std::istringstream iss(cap2.str());
        std::string ln;
        int n = 0;
        bool all_intact = true;
        while (std::getline(iss, ln)) {
            ++n;
            if (ln != "[SDIRK3_TEST] LINE_MARK end") all_intact = false;
        }
        check(n == 400 && all_intact,
              "case28: 8x50 concurrent lines all intact (no interleaving)");
    }

    // (29) Gemini review hardening: the four Newton fields default to kDefectNA,
    //      so a partially-populated IMPLICIT snapshot (e.g. the defect was never
    //      written) FAILS the inventory instead of passing silently on a 0.0 that
    //      reads as a legitimate zero defect. A fully-defaulted EXPLICIT record
    //      still passes (all-kDefectNA is exactly its contract).
    {
        StageDefectSnapshot expl;  // default-constructed
        expl.stage = 1;
        expl.explicit_stage = true;
        expl.k_norm = 10.0;  // the four Newton fields keep their kDefectNA defaults
        StageDefectSnapshot partial;
        partial.stage = 2;
        partial.explicit_stage = false;
        partial.k_norm = 10.0;
        partial.f_fast_norm = 5.0;
        partial.scaled_final_residual = 0.1;
        // newton_defect_norm and defect_to_k_ratio deliberately left at default
        std::string r = validate_stage_defect_inventory({expl, partial}, 3);
        check(r.find("neg_defect:s2") != std::string::npos,
              "case29: unset implicit defect defaults to kDefectNA -> neg_defect "
              "(no silent 0.0 pass); default explicit record still valid");
    }

    // (30) PR 9F.1 — evidence TRANSACTION + tensor-derived defect authority.
    //      (a) success lines must be STAGED, never written, until every authority
    //          passed; a later-gate failure must leave ZERO success-form evidence.
    //      (b) the published convergence numbers must be TENSOR-derived, and the
    //          caller's scalars must be cross-checked, not trusted.
    //      (c) the tensor evidence itself needs an inventory (a MISSING tensor
    //          record was previously invisible, since only existing ones were judged).
    {
        const float dt = 1.0f;
        auto U_n = rnd({24}, 5.0f, 0.2f);
        auto ks1 = rnd({24}, 2.0f, 0.4f), kf1 = rnd({24}, 1.0f, 0.6f);
        auto ks2 = rnd({24}, 1.5f, 0.8f), kf2 = rnd({24}, 0.9f, 1.1f);
        auto U_stage = U_n + ks1 + kf1 + ks2 + kf2;   // aE=aI=1, dt=1
        std::vector<StageHistorySource> src(2);
        src[0] = {1, 1.0, 1.0, &ks1, &kf1}; src[0].birth_generation = 1;
        src[1] = {2, 1.0, 1.0, &ks2, &kf2}; src[1].birth_generation = 2;

        // coherent tensors for the IMPLICIT source (stage 2)
        auto K = rnd({24}, 4.0f, 0.3f), F = rnd({24}, 1.5f, 0.9f);
        auto R = K - F;
        StageDefectTensorSnapshot ts;
        ts.stage = 2; ts.retry_generation = 1; ts.newton_iter = 0;
        ts.point = DefectEvaluationPoint::ResidualEval;
        ts.K = K; ts.F = F; ts.R = R; ts.returned_K = K;
        DerivedDefectRecord rec;
        check(validate_stage_defect_tensor(ts, nullptr, nullptr, nullptr, &rec).empty() &&
                  rec.observed,
              "case30: coherent triple yields an OBSERVED derived record");

        // scalar telemetry that AGREES with the tensor (float32-vs-float64 gap only)
        StageDefectSnapshot d1;
        d1.stage = 1; d1.explicit_stage = true; d1.converged = false;
        d1.k_norm = kf1.to(torch::kFloat64).norm().item<double>();
        d1.f_fast_norm = kDefectNA; d1.newton_defect_norm = kDefectNA;
        d1.defect_to_k_ratio = kDefectNA; d1.scaled_final_residual = kDefectNA;
        StageDefectSnapshot d2;
        d2.stage = 2; d2.explicit_stage = false; d2.converged = true;
        d2.k_norm = rec.k_norm;
        d2.f_fast_norm = rec.f_norm;
        d2.newton_defect_norm = rec.defect_norm;
        d2.defect_to_k_ratio = rec.ratio;
        d2.scaled_final_residual = 0.05;
        std::vector<StageDefectSnapshot> defs{d1, d2};
        std::vector<DerivedDefectRecord> derived{rec};

        // --- (a) TRANSACTION: nothing written while staged ---
        StageOperandEvidenceBundle bundle;
        std::ostringstream cap;
        auto* old = std::cerr.rdbuf(cap.rdbuf());
        std::string r = emit_stage_history_diag(0, 0, 3, dt, U_n, U_stage, src, defs,
                                                &derived, &bundle);
        std::cerr.rdbuf(old);
        check(r.empty(), "case30: sound history + tensor authority passes");
        check(cap.str().find("SDIRK3_STAGE_HISTORY") == std::string::npos,
              "case30: NO success line written while staged (transaction holds)");
        check(bundle.pending() > 0, "case30: success lines buffered in the bundle");

        // a later gate fails -> discard -> the evidence never reaches the log
        std::ostringstream cap2;
        old = std::cerr.rdbuf(cap2.rdbuf());
        bundle.discard();
        std::cerr.rdbuf(old);
        check(cap2.str().empty() && bundle.pending() == 0,
              "case30: discard drops staged evidence without writing any of it");

        // commit DOES write, and publishes TENSOR-derived numbers
        StageOperandEvidenceBundle b2;
        std::ostringstream cap3;
        old = std::cerr.rdbuf(cap3.rdbuf());
        emit_stage_history_diag(0, 0, 3, dt, U_n, U_stage, src, defs, &derived, &b2);
        b2.commit();
        std::cerr.rdbuf(old);
        check(cap3.str().find("defect_eval=tensor") != std::string::npos,
              "case30: committed summary publishes tensor-derived defect (defect_eval=tensor)");
        check(cap3.str().find("telemetry_newton_defect_norm=") != std::string::npos,
              "case30: caller scalars demoted to labelled telemetry_* columns");

        // --- (c) tensor INVENTORY: a missing implicit tensor record fails closed ---
        std::vector<DerivedDefectRecord> empty_derived;
        StageOperandEvidenceBundle b3;
        check(emit_stage_history_diag(0, 0, 3, dt, U_n, U_stage, src, defs,
                                      &empty_derived, &b3)
                  .find("DEFECT_INCOMPLETE") != std::string::npos,
              "case30: missing tensor record -> DEFECT_INCOMPLETE (was invisible before)");

        // --- (b) scalar telemetry disagreeing with the tensor fails closed ---
        auto bad = defs;
        bad[1].newton_defect_norm = rec.defect_norm * 2.0;   // plausible, finite, wrong
        StageOperandEvidenceBundle b4;
        check(emit_stage_history_diag(0, 0, 3, dt, U_n, U_stage, src, bad, &derived, &b4)
                  .find("DEFECT_INCOHERENT") != std::string::npos,
              "case30: scalar defect disagreeing with tensor -> DEFECT_INCOHERENT");
    }

    // (31) PR 9F.1 — pre/post-postprocess K semantics are MEASURED, not mixed.
    //      The Newton defect is evaluated at the solver-returned K; the ARK history
    //      consumes k_fast AFTER the post-solve damping transforms. At dt=600 the
    //      solve does not converge, so that damping DOES fire and the two differ.
    //      The record must report the measured gap, not silently blend them.
    {
        auto K = rnd({12}, 3.0f, 0.25f), F = rnd({12}, 1.1f, 0.75f);
        auto R = K - F;
        auto mk = [&](const torch::Tensor& final_k) {
            StageDefectTensorSnapshot s;
            s.stage = 2; s.retry_generation = 1; s.newton_iter = 0;
            s.point = DefectEvaluationPoint::ResidualEval;
            s.K = K; s.F = F; s.R = R; s.returned_K = K; s.final_k = final_k;
            return s;
        };
        // (a) no post-solve transform: final_k IS the returned K
        DerivedDefectRecord r0;
        check(validate_stage_defect_tensor(mk(K), nullptr, nullptr, nullptr, &r0).empty(),
              "case31: coherent triple with final_k == returned_K passes");
        check(r0.final_k_observed && !r0.postprocess_applied &&
                  r0.postprocess_delta_max == 0.0,
              "case31: no transform -> postprocess_applied=0, delta==0");
        // (b) a damping transform fired: final_k = 0.5 * returned_K
        DerivedDefectRecord r1;
        auto damped = K * 0.5f;
        check(validate_stage_defect_tensor(mk(damped), nullptr, nullptr, nullptr, &r1).empty(),
              "case31: damped final_k still passes the coherence gates (defect is "
              "evaluated at returned_K, which is unchanged)");
        check(r1.postprocess_applied && r1.postprocess_delta_max > 0.0,
              "case31: damping fired -> postprocess_applied=1 and delta>0 (MEASURED, "
              "not silently mixed with the pre-transform defect)");
        // (c) absent final_k is reported as unobserved rather than assumed equal
        DerivedDefectRecord r2;
        check(validate_stage_defect_tensor(mk(torch::Tensor()), nullptr, nullptr,
                                           nullptr, &r2).empty() &&
                  !r2.final_k_observed && !r2.postprocess_applied,
              "case31: absent final_k -> final_k_observed=0 (never assumed identical)");
    }

    // ---- case32: PR 9F.2 P1-2 -- the DISABLED counter must not move -------------
    // The default production path must pay no atomic read-modify-write. The env var
    // is unread in this unit-test process, so sdirk3_rhs_count_enabled() is false and
    // the tick must be a no-op. This pins the property that regressed once already:
    // gating only the PRINTING while leaving the atomic unconditional.
    {
        const long long before = wrf::sdirk3::sdirk3_rhs_count_value();
        for (int i = 0; i < 1000; ++i) wrf::sdirk3::sdirk3_rhs_count_tick();
        const long long after = wrf::sdirk3::sdirk3_rhs_count_value();
        check(!wrf::sdirk3::sdirk3_rhs_count_enabled(),
              "case32: counting is DISABLED in this process (no WRF_SDIRK3_RHS_COUNT)");
        check(after == before,
              "case32: 1000 ticks with counting disabled leave the counter UNCHANGED "
              "(default path performs no atomic RMW)");
    }

    // ---- case33: the sweep scope emits nothing when counting is disabled ---------
    // RAII pairing must not become an unconditional I/O cost on the default path.
    {
        const long long before = wrf::sdirk3::sdirk3_rhs_count_value();
        { wrf::sdirk3::Sdirk3RhsSweepScope s; wrf::sdirk3::sdirk3_rhs_count_tick(); }
        check(wrf::sdirk3::sdirk3_rhs_count_value() == before,
              "case33: sweep scope + tick with counting disabled is a total no-op");
        // The DEPTH counter is a second shared atomic, and the first version of the
        // concurrency guard incremented it unconditionally in the member-init list --
        // re-introducing per-sweep exactly the default-path atomic RMW that case32
        // removes per-RHS-call. Pin it: disabled construction must not touch depth.
        check(wrf::sdirk3::sdirk3_rhs_sweep_depth() == 0,
              "case33: sweep depth is UNTOUCHED when counting is disabled (no atomic "
              "RMW on the default path, entry or exit)");
        {
            wrf::sdirk3::Sdirk3RhsSweepScope outer;
            check(wrf::sdirk3::sdirk3_rhs_sweep_depth() == 0,
                  "case33: depth stays 0 even DURING a disabled scope lifetime");
        }
    }

    // ---- case34: nesting detection, including the COMPLETED inner sweep --------
    // The counter is thread_local, so cross-thread contamination is impossible by
    // construction; the predicate only has to catch same-thread nesting. Kept pure
    // so it is testable here at all -- the enabled cache is latched false in this
    // process, so a predicate reachable only through the live scope could not run.
    //
    // The third case is the one a depth-only predicate gets WRONG: an inner sweep
    // that begins and ends inside our lifetime drives depth 1 -> 2 -> 1, so both of
    // our depth samples read 1 and the outer row would claim to be clean while
    // containing the inner sweep's RHS calls. The epoch term is what catches it.
    {
        using wrf::sdirk3::sdirk3_sweep_overlapped;
        const long long e = 5;
        check(!sdirk3_sweep_overlapped(false, 1, e, e),
              "case34: solitary sweep (depth 1, epoch unmoved) -> clean");
        check(sdirk3_sweep_overlapped(true, 1, e, e),
              "case34: a sweep already open on this thread at entry -> tainted");
        check(sdirk3_sweep_overlapped(false, 1, e, e + 1),
              "case34: COMPLETED inner sweep is detected by the epoch (both depth "
              "samples read 1, so depth alone would call this clean)");
        check(sdirk3_sweep_overlapped(false, 2, e, e + 1),
              "case34: an inner sweep still open at our exit -> tainted");
        check(sdirk3_sweep_overlapped(false, 0, e, e),
              "case34: impossible depth 0 at exit is treated as tainted (fail-closed)");
    }

    // ---- case35: the counter is thread-private ---------------------------------
    // This is the property that replaced three failed attempts to make a global
    // counter attributable, so it must be tested NON-VACUOUSLY.
    //
    // The obvious test -- call sdirk3_rhs_count_tick() on another thread and check
    // this thread is unmoved -- is worthless here: counting is DISABLED in this
    // process (case32/33 force the cached flag false), so tick() returns before
    // incrementing and NOTHING moves on either thread. A process-global counter
    // would pass that test identically. It proves only that the gate holds.
    //
    // The counter accessor itself is not gated, so drive it directly. If the storage
    // were shared, the main thread would observe the other thread's writes.
    {
        // Seed THIS thread first, so "the other thread sees 0" is itself
        // non-vacuous: with shared storage the other thread would observe this
        // seed rather than zero.
        for (int i = 0; i < 41; ++i) ++wrf::sdirk3::sdirk3_rhs_call_counter();
        const long long before = wrf::sdirk3::sdirk3_rhs_count_value();
        check(before >= 41,
              "case35: this thread's own direct increments ARE visible to itself "
              "(the accessor is not a no-op, so the checks below have teeth)");
        long long other_thread_saw = -1;
        std::thread t([&] {
            // A fresh thread's private counter must start at zero...
            other_thread_saw = wrf::sdirk3::sdirk3_rhs_count_value();
            // ...and writes here must be invisible to the spawning thread.
            for (int i = 0; i < 128; ++i) ++wrf::sdirk3::sdirk3_rhs_call_counter();
        });
        t.join();
        check(wrf::sdirk3::sdirk3_rhs_count_value() == before,
              "case35: 128 DIRECT increments on another thread leave this thread's "
              "counter unchanged (storage is thread-private, not merely gated)");
        check(other_thread_saw == 0,
              "case35: a fresh thread starts from its own zeroed counter, not from "
              "this thread's accumulated value");
    }

    // ---- case36: the disabled gate is evaluated per thread ----------------------
    // Separate from case35 on purpose: this one IS about the gate, and stating it
    // separately keeps case35 from being read as covering both.
    {
        long long moved = -1;
        std::thread t([&] {
            const long long b = wrf::sdirk3::sdirk3_rhs_count_value();
            for (int i = 0; i < 128; ++i) wrf::sdirk3::sdirk3_rhs_count_tick();
            moved = wrf::sdirk3::sdirk3_rhs_count_value() - b;
        });
        t.join();
        check(moved == 0,
              "case36: with counting disabled, 128 ticks on a NEW thread move that "
              "thread's counter by 0 (the gate is not inherited as enabled)");
    }

    // ---- case37: the ENABLED emitter path actually runs ------------------------
    // Everything before this drives predicates and accessors. The constructor, the
    // enabled tick, the destructor, the delta arithmetic and the record TEXT were
    // never executed by any standing test, because the cached flag latches false in
    // this process. A child with the flag set is the only way to reach them, and its
    // stderr is the real emitter output rather than a hand-written fixture.
    {
        const std::string out = capture_child(argv[0], "one-sweep");
        check(count_occurrences(out, "SDIRK3_RHS_CALLS phase=begin") == 1,
              "case37: enabled child emits exactly one sweep begin record");
        check(count_occurrences(out, "SDIRK3_RHS_CALLS phase=end") == 1,
              "case37: enabled child emits exactly one sweep end record");
        check(out.find("phase=end sweep_seq=0 total=3 delta=3") != std::string::npos,
              "case37: the REAL delta is computed and printed (3 ticks -> delta=3)");
        check(out.find("concurrent=1") == std::string::npos,
              "case37: a solitary sweep is not marked concurrent");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=begin generation=1 total=0") != std::string::npos,
              "case37: whole-run total is opened");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=end generation=1 total=3 exit=clean") !=
                  std::string::npos,
              "case37: whole-run total is closed from the exit handler, agrees, and "
              "is labelled exit=clean");
    }

    // ---- case38: nested child -- BOTH records must be tainted ------------------
    // The inner sweep begins and ends inside the outer one, so the outer delta
    // contains the inner calls. A depth-only predicate marks only the inner record.
    {
        const std::string out = capture_child(argv[0], "nested");
        check(count_occurrences(out, "SDIRK3_RHS_CALLS phase=begin") == 2,
              "case38: nested child emits two begin records");
        check(out.find("phase=end sweep_seq=1") != std::string::npos &&
                  out.find("phase=end sweep_seq=1 total=2 delta=1 concurrent=1") !=
                      std::string::npos,
              "case38: the INNER end record is tainted");
        check(out.find("phase=end sweep_seq=0 total=3 delta=3 concurrent=1") !=
                  std::string::npos,
              "case38: the OUTER end record is ALSO tainted -- it contained a "
              "completed inner sweep that both depth samples miss");
    }

    // ---- case39: RHS calls with no sweep at all --------------------------------
    // This is the gap the whole-run total exists to close: the sweep table sees
    // nothing, so an extra call here is invisible to every per-sweep gate.
    {
        const std::string out = capture_child(argv[0], "no-sweep");
        check(count_occurrences(out, "SDIRK3_RHS_CALLS ") == 0,
              "case39: calls outside any sweep leave NO sweep records (the gap)");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=end generation=1 total=2 exit=clean") !=
                  std::string::npos,
              "case39: ...but the whole-run total still counts them");
    }

    // ---- case40: the closing run total survives a CONTROLLED FATAL -------------
    // This is the exit the dt=600 acceptance run actually takes. MPI_Abort and
    // std::abort run no static destructors and no atexit handlers, so an
    // exit-handler-only emitter makes the closing record structurally unreachable
    // on precisely the runs being measured -- and the harness gate that requires it
    // could then only ever fail. The pre-abort hook is what closes that.
    {
        const std::string out = capture_child(argv[0], "abort");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=begin generation=1 total=0") != std::string::npos,
              "case40: aborting child still opens the whole-run total");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=end generation=1 total=2 exit=fatal") !=
                  std::string::npos,
              "case40: aborting child EMITS the closing run total before dying "
              "(static destructors never run through MPI_Abort/std::abort) AND "
              "labels it exit=fatal so it cannot pose as a completed run");
        check(out.find("SDIRK3_C_ABI_EXCEPTION") != std::string::npos,
              "case40: ...and the controlled-fatal marker is still emitted");
        check(count_occurrences(out, "SDIRK3_RHS_RUN_TOTAL phase=end") == 1,
              "case40: exactly one closing record -- the emit is idempotent across "
              "the fatal hook and the exit handler");
    }

    // ---- case41: the authority record is FROZEN against fallbacks ---------------
    // Three earlier rounds mistook "a closing record exists" for "the production
    // path works". The authority field makes that checkable, but only if a later
    // fallback cannot overwrite or contradict it.
    {
        const std::string out = capture_child(argv[0], "authority-frozen");
        check(count_occurrences(out, "SDIRK3_RHS_RUN_TOTAL phase=end") >= 1,
              "case41: the authority close emits a closing record");
        check(out.find("exit=fatal authority=fortran_outcome reason=step_outcome") != std::string::npos,
              "case41: it is attributed to the FORTRAN authority");
        check(out.find("authority=cpp_preabort") == std::string::npos &&
                  out.find("authority=cpp_destructor") == std::string::npos,
              "case41: later C++ fallbacks do NOT re-attribute the record to "
              "themselves (frozen authority)");
        // Every end line must be byte-identical: duplication is tolerated, drift is
        // not, and a fallback emitting a DIFFERENT total would be drift.
        check(count_occurrences(out, "phase=end generation=1 total=2 exit=fatal "
                                     "authority=fortran_outcome") ==
                  count_occurrences(out, "SDIRK3_RHS_RUN_TOTAL phase=end"),
              "case41: every closing record is the same frozen line");
    }

    // ---- case42: a conflicting second close is reported, not absorbed -----------
    {
        const std::string out = capture_child(argv[0], "close-conflict");
        check(out.find("CLOSE_RC first=1 second=0") != std::string::npos,
              "case42: closing again with a DIFFERENT exit kind returns 0 (a wiring "
              "bug is surfaced rather than answered with the first verdict)");
    }

    // ---- case43: disabled close is a total no-op -------------------------------
    {
        const std::string out = capture_child(argv[0], "close-disabled");
        check(out.find("CLOSE_RC disabled=1") != std::string::npos,
              "case43: with counting disabled the close returns 1 and emits nothing");
        check(out.find("SDIRK3_RHS_RUN_TOTAL") == std::string::npos,
              "case43: ...and no run record is produced at all");
    }

    // ---- case44: close(CLEAN) is attributed to fortran_finalize ----------------
    // The FATAL path is proven end-to-end (case41 + the real dt=600 run). The CLEAN
    // path had no dedicated positive test: only close-conflict touched it, as a
    // rejected second call. This pins that a first CLEAN close emits exit=clean
    // authority=fortran_finalize, so a future wiring change cannot silently
    // mis-attribute the normal-exit record.
    {
        const std::string out = capture_child(argv[0], "close-clean");
        check(out.find("SDIRK3_RHS_RUN_TOTAL phase=end") != std::string::npos,
              "case44: close(CLEAN) emits a closing record");
        check(out.find("generation=1 total=1 exit=clean authority=fortran_finalize reason=finalize") != std::string::npos,
              "case44: it is attributed to the FORTRAN FINALIZER authority");
        check(out.find("exit=fatal") == std::string::npos &&
                  out.find("fortran_outcome") == std::string::npos,
              "case44: a clean close is not mislabelled fatal/outcome");
    }

    // ---- case45: concurrent close loses NO record and all are identical ---------
    // The regression the reviewer named: the old bounded spin could return 0 (record
    // lost) if the winner was preempted. The CAS lifecycle cannot -- prove it under 8
    // racing closers.
    {
        const std::string out = capture_child(argv[0], "lifecycle-concurrent-close");
        check(out.find("CONCURRENT_CLOSE_ONES=8") != std::string::npos,
              "case45: all 8 concurrent closers return 1 (none lost)");
        check(count_occurrences(out, "SDIRK3_RHS_RUN_TOTAL phase=end") == 8,
              "case45: exactly 8 closing records emitted (one per caller)");
        check(count_occurrences(
                  out, "phase=end generation=1 total=3 exit=fatal "
                       "authority=fortran_outcome") == 8,
              "case45: all 8 records are the SAME frozen line (count frozen at 3)");
    }

    // ---- case46: a tick after close is rejected and flagged --------------------
    {
        const std::string out = capture_child(argv[0], "lifecycle-post-close-tick");
        check(out.find("POST_CLOSE_TICK_RC=0") != std::string::npos,
              "case46: tick after close returns 0 (the RHS-past-fatal violation)");
        check(out.find("post_close_tick=1") != std::string::npos,
              "case46: the violation is recorded in the re-emitted closing record");
    }

    // ---- case47: re-init opens a new generation --------------------------------
    {
        const std::string out = capture_child(argv[0], "lifecycle-reinit");
        check(out.find("phase=begin generation=1 total=0") != std::string::npos &&
                  out.find("phase=end generation=1 total=3 exit=clean") !=
                      std::string::npos,
              "case47: first lifecycle is generation 1 (count 3)");
        check(out.find("phase=begin generation=2 total=0") != std::string::npos &&
                  out.find("phase=end generation=2 total=2 exit=clean") !=
                      std::string::npos,
              "case47: re-init opens generation 2 with its OWN count (2), not a "
              "replay of generation 1");
    }

    const int kExpected = 131;  // ratchet: update deliberately with the cases
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
