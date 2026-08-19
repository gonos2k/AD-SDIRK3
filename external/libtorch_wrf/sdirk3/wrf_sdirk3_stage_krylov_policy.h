#pragma once

// ============================================================================
// StageKrylovPolicy: resolving a stage's Krylov budget as a PURE function.
// ============================================================================
// A stage-budget experiment is only an experiment if it varies one thing. The shipped
// resolution order does not have that property:
//
//     1. stage >= 2:  apply the stage-2 knobs
//     2.              compute budget_active = OR over the stage-2 AND stage-3 knobs
//     3.              if budget_active and EW forcing is on, SCALE the budget
//     4. stage >= 3:  overwrite with the stage-3 knobs
//
// So leaving stage3_gmres_restart unset does not mean "stage 3 runs the default": it means
// stage 3 runs the stage-2 value AFTER EW scaling (600 -> 510), while setting it explicitly
// replaces the already-scaled number and stands unscaled at 600. A sweep across that knob
// therefore varies the value AND whether EW scaling reached it -- which is how a budget table
// came to be published, and retracted.
//
// This header makes the resolution a pure function of its inputs so the ordering is (a) named,
// (b) testable, and (c) selectable. `ShippedOrder` reproduces the behaviour above exactly and
// stays the default; `StageKnobFirst` resolves the stage's OWN knob before scaling, which is
// what a single-variable stage-3 experiment needs.
//
// Scope: knob resolution and EW budget scaling. The hopeless-mode caps stay in the solver --
// they depend on streak counters, i.e. on state, and a pure function has none. They remain
// separable per stage: the stage-2 cap cannot fire at stage 3, and the stage-3 cap cannot fire
// at stage 2, so moving the stage-3 knob resolution in here reorders nothing.

#include <algorithm>
#include <cmath>

namespace wrf {
namespace sdirk3 {

enum class StageKrylovOrder {
    ShippedOrder = 0,   // stage-2 knobs -> EW scale -> stage-3 knobs   (default; unchanged)
    StageKnobFirst = 1  // the stage's own knobs -> EW scale
};

// Where the restart value came from, so a report can say what actually ran.
enum class StageRestartSource { GlobalDefault = 0, Stage2Knob = 2, Stage3Knob = 3 };

struct StageKrylovInputs {
    int   stage = 1;

    int   base_restart = 0;
    int   base_max_restarts = 0;
    float base_tol = 0.0f;

    int   s2_restart = 0;
    int   s2_max_restarts = 0;
    float s2_tol = 0.0f;

    int   s3_restart = 0;
    int   s3_max_restarts = 0;
    float s3_tol = 0.0f;

    bool  ew_enabled = false;
    float ew_eta = -1.0f;

    StageKrylovOrder order = StageKrylovOrder::ShippedOrder;
};

struct StageKrylovPolicy {
    int   restart = 0;
    int   max_restarts = 0;
    float tol = 0.0f;
    bool  tol_overridden = false;
    bool  budget_active = false;
    float ew_scale = 1.0f;      // 1.0 also means "not applied"; read ew_applied to tell them apart
    bool  ew_applied = false;
    float ew_eta_used = -1.0f;
    StageRestartSource restart_source = StageRestartSource::GlobalDefault;
};

// Eisenstat-Walker forcing -> budget multiplier. Tighter forcing buys more Krylov work.
inline float ew_budget_scale(float eta) {
    if (!(eta > 0.0f) || !std::isfinite(eta)) return 1.0f;
    eta = std::clamp(eta, 1.0e-3f, 1.0f);
    if (eta <= 0.08f) return 1.25f;
    if (eta <= 0.15f) return 1.10f;
    if (eta >= 0.40f) return 0.75f;
    if (eta >= 0.30f) return 0.85f;
    return 1.0f;
}

inline StageKrylovPolicy resolve_stage_krylov_policy(const StageKrylovInputs& in) {
    StageKrylovPolicy p;
    p.restart      = in.base_restart;
    p.max_restarts = in.base_max_restarts;
    p.tol          = in.base_tol;

    if (in.stage < 2) return p;   // stages below 2 have no stage-aware budget at all

    // budget_active is an OR over BOTH stages' knobs, and deliberately so: it is the switch
    // that couples EW forcing to the budget, and it must not flip merely because a stage-3
    // experiment set the stage-3 knob.
    p.budget_active = (in.s2_restart > 0 || in.s2_max_restarts > 0 || in.s2_tol > 0.0f ||
                       in.s3_restart > 0 || in.s3_max_restarts > 0 || in.s3_tol > 0.0f);

    auto apply_stage2 = [&] {
        if (in.s2_restart > 0)      { p.restart = in.s2_restart;
                                      p.restart_source = StageRestartSource::Stage2Knob; }
        if (in.s2_max_restarts > 0) { p.max_restarts = in.s2_max_restarts; }
        if (in.s2_tol > 0.0f)       { p.tol = in.s2_tol; p.tol_overridden = true; }
    };
    auto apply_stage3 = [&] {
        if (in.s3_restart > 0)      { p.restart = in.s3_restart;
                                      p.restart_source = StageRestartSource::Stage3Knob; }
        if (in.s3_max_restarts > 0) { p.max_restarts = in.s3_max_restarts; }
        if (in.s3_tol > 0.0f)       { p.tol = in.s3_tol; p.tol_overridden = true; }
    };
    auto apply_ew = [&] {
        if (!(p.budget_active && in.ew_enabled && !p.tol_overridden)) return;
        float eta = in.ew_eta;
        if (!(eta > 0.0f) || !std::isfinite(eta)) eta = p.tol;
        if (!(eta > 0.0f) || !std::isfinite(eta)) return;
        eta = std::clamp(eta, 1.0e-3f, 1.0f);
        p.ew_eta_used = eta;
        const float scale = ew_budget_scale(eta);
        p.ew_scale = scale;
        const int restart_before = p.restart;
        const int maxr_before    = p.max_restarts;
        p.restart = std::max(2, static_cast<int>(p.restart * scale + 0.5f));
        if (scale > 1.0f) {
            p.max_restarts = std::max(
                1, static_cast<int>(p.max_restarts * std::min(scale, 1.20f) + 0.5f));
        } else if (scale < 1.0f) {
            p.max_restarts = std::max(
                1, static_cast<int>(p.max_restarts * std::max(scale, 0.70f) + 0.5f));
        }
        p.ew_applied = (p.restart != restart_before || p.max_restarts != maxr_before);
    };

    if (in.order == StageKrylovOrder::StageKnobFirst) {
        // The stage's OWN knob decides the budget, and EW scales whatever that is. Setting or
        // not setting stage3_gmres_restart then changes the VALUE and nothing else.
        if (in.stage >= 3) { apply_stage2(); apply_stage3(); } else { apply_stage2(); }
        apply_ew();
    } else {
        // Shipped: stage-2, then scale, then stage-3 replaces the scaled number.
        apply_stage2();
        apply_ew();
        if (in.stage >= 3) apply_stage3();
    }
    return p;
}


// The aggressive early-stop gates read a stage-budget knob to decide whether a stage counts as
// "budgeted". At stage_id >= 2 they read the STAGE-2 knobs -- at stage 3 as well. So a stage-3
// experiment that sets only stage3_* leaves those gates OFF, while one that sets stage2_* turns
// them ON for stage 3. That is the same confound as the resolution ordering above, at a
// different site, and it is why a stage-3 sweep is not a single-variable experiment.
//
// ShippedOrder returns the stage-2 knobs, exactly as today. StageKnobFirst lets the stage's own
// knobs govern its own gates.
struct StageBudgetKnobs {
    int restart = 0;
    int max_restarts = 0;
};

inline StageBudgetKnobs early_stop_gate_knobs(int stage,
                                              int s2_restart, int s2_max_restarts,
                                              int s3_restart, int s3_max_restarts,
                                              StageKrylovOrder order) {
    StageBudgetKnobs k{s2_restart, s2_max_restarts};
    if (order == StageKrylovOrder::StageKnobFirst && stage >= 3) {
        if (s3_restart > 0)      k.restart = s3_restart;
        if (s3_max_restarts > 0) k.max_restarts = s3_max_restarts;
    }
    return k;
}

}  // namespace sdirk3
}  // namespace wrf
