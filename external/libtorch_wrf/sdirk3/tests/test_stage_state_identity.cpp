// 9F.D126: stage_state_generation must stay a FAITHFUL identity across rollback.
//
// The adjoint replay snapshots the preconditioner's stage state, binds other checkpoints, then
// restores. Before this contract, restore_stage_state() put the STATE back and left the
// generation counter where the replay had driven it:
//
//     snapshot at G, state S
//     replay binds ...            -> counter G+k, state S'
//     restore(snapshot)           -> state S, counter STILL G+k
//
// So G+k denoted S' during the replay and S afterwards: one value, two different linearizations,
// comparing EQUAL. That is the one thing an identity must never do, and it is exactly the
// "changes and unwinds" shape the operator contract already guards against at a finer scale.
//
// Restoring the counter to G would be worse -- G+1..G+k were already issued, so they would be
// handed out again for unrelated binds. A restore is a BINDING EVENT and takes a fresh value.

#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_types.h"
#include "../wrf_sdirk3_unified_rhs.h"

#include <torch/torch.h>

#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

std::shared_ptr<wrf::sdirk3::WRFGridInfo> tiny_grid() {
    auto g = std::make_shared<wrf::sdirk3::WRFGridInfo>();
    g->nx = 4;  g->ny = 3;  g->nz = 5;
    g->nx_u = 5; g->ny_v = 4; g->nz_w = 6;
    g->its = 1; g->ite = 4; g->jts = 1; g->jte = 3; g->kts = 1; g->kte = 5;
    g->ims = 1; g->ime = 4; g->jms = 1; g->jme = 3; g->kms = 1; g->kme = 6;
    g->ids = 1; g->ide = 5; g->jds = 1; g->jde = 4; g->kds = 1; g->kde = 6;
    g->dx = 1000.0f; g->dy = 1000.0f;
    return g;
}

}  // namespace

int main() {
    std::cout << "=== Stage_State_Identity_Contract ===" << std::endl;

    auto grid = tiny_grid();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    wrf::sdirk3::UnifiedPreconditioner P(grid, physics, 600.0f, 0.4358665215f);

    const uint64_t g0 = P.stage_state_generation();
    const double digest0 = P.stage_state_digest();
    const auto snap = P.snapshot_stage_state();

    // 1. A restore takes a FRESH value.
    P.restore_stage_state(snap);
    const uint64_t g1 = P.stage_state_generation();
    check(g1 > g0, "a restore mints a FRESH generation, not the one the rollback landed on");

    // 2. Control: the STATE really did come back. Without this the generation change above could
    //    be reporting a state change rather than a rollback, and the assertion would be about
    //    the wrong event.
    check(P.stage_state_digest() == digest0,
          "and the state digest is UNCHANGED -- it is a rollback, not a new binding of new state");

    // 3. Monotonic and never reused. A counter that returned to an earlier value would let two
    //    different linearizations share one identity, which is the defect itself.
    std::set<uint64_t> seen{g0, g1};
    for (int i = 0; i < 5; ++i) {
        P.restore_stage_state(snap);
        const uint64_t g = P.stage_state_generation();
        check(seen.insert(g).second && g > *std::prev(seen.end(), 2),
              "restore " + std::to_string(i + 1) + " is strictly greater and never reused");
    }

    // 4. KNOWN GAP, kept VISIBLE on purpose. My first version of this case asserted that a
    //    restore leaves coefficient_generation alone and called that correct -- "rebinding is not
    //    rebuilding". That endorsed a defect.
    //
    //    The acoustic/gravity coefficients are DERIVED from mu_full_stage_
    //    (wrf_sdirk3_unified_preconditioner.cpp:2436 and :3966 build inv_mu0 from it) and are
    //    rebuilt by update() -> initialize_acoustic_gravity_solver(), which is on the adjoint
    //    replay path. StageStateSnapshot carries none of them, so a restore puts the stage fields
    //    back while the coefficients stay bound to the replay checkpoint.
    //
    //    So the assertion stands but its REASON inverts: restore must not touch this counter
    //    because resetting it would MASK the rebuild, and the replay guard needs the drift to
    //    remain visible in order to report it. Restoring the coefficients themselves is a
    //    separate design decision about what the replay contract promises.
    const uint64_t c_before = P.coefficient_generation();
    P.restore_stage_state(snap);
    check(P.coefficient_generation() == c_before,
          "a restore does not MASK coefficient drift -- the counter stays visible so the replay "
          "guard can report coefficients left bound to the wrong checkpoint");

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "STAGE_STATE_IDENTITY: PASS" << std::endl; return 0; }
    std::cout << "STAGE_STATE_IDENTITY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
