// R9 §10.1: a stage-budget experiment must vary ONE thing.
//
// The shipped resolution order is  stage-2 knobs -> EW budget scaling -> stage-3 knobs.
// Under it, leaving stage3_gmres_restart unset does not mean "stage 3 runs the default": it
// means stage 3 runs the stage-2 value AFTER scaling, while setting it explicitly replaces the
// scaled number and stands unscaled. So a sweep over that knob varies the value AND whether EW
// scaling reached it -- which is how a stage-3 budget table came to be published and retracted.
//
// These cases pin that the two orderings are genuinely different (so the confound is real, not
// a story), and that StageKnobFirst removes it.
#include "wrf_sdirk3_stage_krylov_policy.h"

#include <cstdio>
#include <cmath>

using wrf::sdirk3::StageKrylovInputs;
using wrf::sdirk3::StageKrylovOrder;
using wrf::sdirk3::StageRestartSource;
using wrf::sdirk3::resolve_stage_krylov_policy;
using wrf::sdirk3::ew_budget_scale;
using wrf::sdirk3::early_stop_gate_knobs;

static int g_fail = 0, g_cases = 0;
static void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static void check_eq(int a, int b, const char* what) {
    ++g_cases;
    if (a != b) { std::printf("FAIL: %s (got %d, want %d)\n", what, a, b); ++g_fail; }
}

// The configuration the retracted table was measured in: stage-2 set to 600, EW forcing on and
// loose enough to scale the budget DOWN (eta >= 0.40 -> 0.75, i.e. 600 -> 450).
static StageKrylovInputs base_inputs(int stage) {
    StageKrylovInputs in;
    in.stage = stage;
    in.base_restart = 30;
    in.base_max_restarts = 4;
    in.base_tol = 0.1f;
    in.s2_restart = 600;
    in.ew_enabled = true;
    in.ew_eta = 0.5f;              // -> scale 0.75
    return in;
}

static void contract_ordering_confound() {
    check(std::fabs(ew_budget_scale(0.5f) - 0.75f) < 1e-6f, "eta=0.5 scales the budget to 0.75");

    // Arm A: stage-3 knob UNSET. Stage 3 inherits stage 2's 600 and IS scaled -> 450.
    auto inherit = base_inputs(3);
    const auto p_inherit = resolve_stage_krylov_policy(inherit);
    check_eq(p_inherit.restart, 450, "unset stage-3 knob inherits stage 2 AND is EW-scaled");
    check(p_inherit.restart_source == StageRestartSource::Stage2Knob,
          "the inherited value's source is the stage-2 knob, not the global default");

    // Arm B: stage-3 knob SET to the same 600. It replaces the already-scaled number -> 600.
    auto explicit_same = base_inputs(3);
    explicit_same.s3_restart = 600;
    const auto p_explicit = resolve_stage_krylov_policy(explicit_same);
    check_eq(p_explicit.restart, 600, "an explicit stage-3 knob replaces the SCALED number");

    // THE CONFOUND: the two arms differ even though the knob was set to the value already in
    // effect. A table comparing them is not comparing budgets.
    check(p_inherit.restart != p_explicit.restart,
          "setting stage-3 to the value already inherited CHANGES the budget (the confound)");

    // StageKnobFirst removes it: the stage's own knob is resolved before scaling, so setting it
    // to the inherited value is a no-op and the sweep varies only the value.
    auto inherit_first = base_inputs(3);
    inherit_first.order = StageKrylovOrder::StageKnobFirst;
    auto explicit_first = explicit_same;
    explicit_first.order = StageKrylovOrder::StageKnobFirst;
    const auto q_inherit  = resolve_stage_krylov_policy(inherit_first);
    const auto q_explicit = resolve_stage_krylov_policy(explicit_first);
    check_eq(q_inherit.restart, q_explicit.restart,
             "StageKnobFirst: setting stage-3 to the inherited value changes nothing");
    check_eq(q_explicit.restart, 450, "StageKnobFirst scales the stage's own value");
    check(q_explicit.restart_source == StageRestartSource::Stage3Knob,
          "StageKnobFirst attributes the value to the stage-3 knob");

    // And a genuinely different stage-3 value still lands where the value says, scaled.
    auto smaller = explicit_first;
    smaller.s3_restart = 100;
    check_eq(resolve_stage_krylov_policy(smaller).restart, 75,
             "StageKnobFirst: 100 -> 75 under the same scaling as every other arm");
}

static void contract_budget_active_is_an_or() {
    // budget_active must NOT flip merely because a stage-3 experiment set the stage-3 knob --
    // otherwise the experiment also switches EW/budget coupling on. It is an OR over both.
    auto in = base_inputs(3);
    check(resolve_stage_krylov_policy(in).budget_active, "stage-2 knob alone activates");
    in.s2_restart = 0;
    in.s3_restart = 600;
    check(resolve_stage_krylov_policy(in).budget_active, "stage-3 knob alone activates");
    in.s3_restart = 0;
    check(!resolve_stage_krylov_policy(in).budget_active, "no stage knob set -> inactive");
    // With nothing active there is no scaling either, so the global default stands.
    check_eq(resolve_stage_krylov_policy(in).restart, 30, "inactive leaves the global default");
}

static void contract_tol_override_blocks_scaling() {
    // A stage tolerance override means the stage dictates the forcing, so EW must not also
    // scale the budget from an eta the stage overrode.
    auto in = base_inputs(2);
    in.s2_tol = 0.05f;
    const auto p = resolve_stage_krylov_policy(in);
    check(p.tol_overridden, "stage-2 tol override recorded");
    check(!p.ew_applied, "an overridden tolerance suppresses EW budget scaling");
    check_eq(p.restart, 600, "restart stays at the stage-2 value, unscaled");
}

static void contract_stage1_untouched() {
    auto in = base_inputs(1);
    const auto p = resolve_stage_krylov_policy(in);
    check_eq(p.restart, 30, "stage 1 keeps the global default");
    check(!p.budget_active, "stage 1 has no stage-aware budget");
    check(p.restart_source == StageRestartSource::GlobalDefault, "stage 1 source is the default");
}

// The SECOND site of the same confound: the aggressive early-stop gates read the stage-2 knobs
// at stage_id >= 2, stage 3 included. Setting only stage3_* leaves the gate OFF; setting
// stage2_* turns it ON for stage 3. So a stage-3 sweep changes the budget AND the early-stop
// policy -- two variables, one knob.
static void contract_early_stop_gate_knobs() {
    // Shipped: stage 3 reads stage 2's knobs, whatever stage 3's own are.
    auto shipped = early_stop_gate_knobs(3, /*s2*/ 600, 1, /*s3*/ 0, 0,
                                         StageKrylovOrder::ShippedOrder);
    check_eq(shipped.restart, 600, "shipped: stage 3 reads the stage-2 restart");
    check_eq(shipped.max_restarts, 1, "shipped: stage 3 reads the stage-2 max_restarts");

    auto shipped_s3only = early_stop_gate_knobs(3, /*s2*/ 0, 0, /*s3*/ 600, 1,
                                                StageKrylovOrder::ShippedOrder);
    check_eq(shipped_s3only.restart, 0,
             "shipped: setting ONLY stage3 leaves the stage-3 early-stop gate off (the confound)");

    // StageKnobFirst: the stage's own knobs govern its own gate.
    auto own = early_stop_gate_knobs(3, /*s2*/ 0, 0, /*s3*/ 600, 1,
                                     StageKrylovOrder::StageKnobFirst);
    check_eq(own.restart, 600, "StageKnobFirst: stage 3 reads its own restart");
    check_eq(own.max_restarts, 1, "StageKnobFirst: stage 3 reads its own max_restarts");

    // Stage 2 is unaffected by the ordering -- it has no later stage to inherit from.
    auto s2_shipped = early_stop_gate_knobs(2, 600, 1, 42, 7, StageKrylovOrder::ShippedOrder);
    auto s2_first   = early_stop_gate_knobs(2, 600, 1, 42, 7, StageKrylovOrder::StageKnobFirst);
    check_eq(s2_shipped.restart, s2_first.restart, "stage 2 gate is order-independent");
    check_eq(s2_shipped.restart, 600, "stage 2 gate reads the stage-2 knob under both orders");
}

int main() {
    contract_ordering_confound();
    contract_budget_active_is_an_or();
    contract_tol_override_blocks_scaling();
    contract_stage1_untouched();
    contract_early_stop_gate_knobs();
    std::printf("%s: %d cases, %d failures\n",
                g_fail == 0 ? "PASS" : "FAIL", g_cases, g_fail);
    return g_fail == 0 ? 0 : 1;
}
