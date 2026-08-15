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
    const int64_t P_state_numel =
        static_cast<int64_t>(grid->nx_u) * grid->ny * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny_v * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny;

    const uint64_t g0 = P.stage_state_generation();
    const uint64_t digest0 = P.stage_state_fingerprint();
    const auto snap = P.snapshot_stage_state();

    // 1. A restore takes a FRESH value.
    P.restore_stage_state(snap);
    const uint64_t g1 = P.stage_state_generation();
    check(g1 > g0, "a restore mints a FRESH generation, not the one the rollback landed on");

    // 2. Control: the STATE really did come back. Without this the generation change above could
    //    be reporting a state change rather than a rollback, and the assertion would be about
    //    the wrong event.
    check(P.stage_state_fingerprint() == digest0,
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

    // 5. FAIL CLOSED. The coefficients are derived from mu_full_stage_ and are NOT in the
    //    snapshot, so after a rollback the ones in memory came from a linearization point that is
    //    no longer bound. Reporting that is not enough -- the replay used to RETURN and let the
    //    next apply() use them. Now apply() refuses until a genuine rebuild.
    check(P.coefficients_stale(),
          "a rollback marks the derived coefficients STALE");
    {
        auto r = torch::zeros({16}, torch::kFloat32);
        bool threw = false;
        try { (void)P.apply(r); } catch (const std::exception&) { threw = true; }
        check(threw, "apply() REFUSES while stale -- a silent wrong answer becomes a loud stop");
    }

    // 6. And the refusal is not permanent: a real rebuild answers it. Without this the check
    //    above would be satisfied by a preconditioner that is simply broken forever.
    {
        auto U = torch::zeros({P_state_numel}, torch::kFloat32);
        bool rebuilt = false;
        try { P.update(U, 600.0f, 0.4358665215f); rebuilt = true; }
        catch (const std::exception&) {}
        if (rebuilt) {
            check(!P.coefficients_stale(),
                  "a genuine rebuild CLEARS the stale mark -- the fail-close is recoverable");
        } else {
            check(false, "update() could not run in this fixture, so recovery is unproven");
        }
    }

    // 7. THE FINGERPRINT CATCHES WHAT THE SUMMARY DIGEST MISSED.
    //
    // The old digest was 1e6*stage + 1e3*scale + |mu_full|_1. Both cases below tie under it and
    // separate under the fingerprint, which is the whole reason for the change. Built through the
    // public snapshot type, so no private state is reached around.
    {
        using Snap = wrf::sdirk3::UnifiedPreconditioner::StageStateSnapshot;
        const auto opts = torch::TensorOptions().dtype(torch::kFloat32);

        Snap a;
        a.mu_full_stage      = torch::tensor({1.0f, 2.0f, 3.0f}, opts);
        a.mu_pert_last_bound = torch::tensor({0.5f, 0.5f, 0.5f}, opts);
        a.current_stage = 2;
        a.mu_scale_correction = 1.0f;

        // Same L1 sum, different field: identical under the old digest.
        Snap b = a;
        b.mu_full_stage = torch::tensor({3.0f, 2.0f, 1.0f}, opts);
        check(std::abs(a.mu_full_stage.abs().sum().item<double>() -
                       b.mu_full_stage.abs().sum().item<double>()) < 1e-12,
              "the two mu_full fields have the SAME L1 sum, so the old digest tied them");

        P.restore_stage_state(a);
        const uint64_t fa = P.stage_state_fingerprint();
        P.restore_stage_state(b);
        const uint64_t fb = P.stage_state_fingerprint();
        check(fa != fb,
              "a REARRANGED mu_full fingerprints differently -- pointwise, not a summary sum");

        // The field the old digest omitted entirely.
        Snap c = a;
        c.mu_pert_last_bound = torch::tensor({0.5f, 0.5f, 0.6f}, opts);
        P.restore_stage_state(c);
        const uint64_t fc = P.stage_state_fingerprint();
        check(fc != fa,
              "changing mu_pert_last_bound moves the fingerprint -- the old digest never saw it, "
              "so dropping its restore would have kept passing");

        // Determinism, so the two inequalities above are about content and not about noise.
        P.restore_stage_state(a);
        check(P.stage_state_fingerprint() == fa,
              "restoring the same snapshot reproduces the same fingerprint");
    }

    // 8. ROLLBACK EQUIVALENCE: is the OPERATOR restored, or only the flag?
    //
    // Case 6 shows stale -> update -> not stale. That proves apply() is callable again; it does
    // NOT prove the coefficients came back. The failure this guards is a rollback that restores
    // the stage fields, rebuilds from them, and still yields a DIFFERENT P^-1 -- which would be
    // invisible to every check above.
    {
        using Snap = wrf::sdirk3::UnifiedPreconditioner::StageStateSnapshot;
        const auto opts32 = torch::TensorOptions().dtype(torch::kFloat32);
        auto U = torch::zeros({P_state_numel}, opts32);
        auto v = torch::arange(1, P_state_numel + 1, opts32) * 1e-3f;   // deterministic

        Snap S;
        S.mu_full_stage      = torch::full({grid->nx * grid->ny}, 90000.0f, opts32);
        S.mu_pert_last_bound = torch::zeros({grid->nx * grid->ny}, opts32);
        S.current_stage = 2;
        S.mu_scale_correction = 1.0f;

        Snap S_other = S;
        S_other.mu_full_stage = torch::full({grid->nx * grid->ny}, 70000.0f, opts32);

        bool usable = true;
        torch::Tensor p_before, p_after;
        try {
            P.restore_stage_state(S);
            P.update(U, 600.0f, 0.4358665215f);
            p_before = P.apply(v).detach().clone();

            P.restore_stage_state(S_other);      // the replay binds something else
            P.update(U, 600.0f, 0.4358665215f);  // and rebuilds from it
            (void)P.apply(v);

            P.restore_stage_state(S);            // roll back
            P.update(U, 600.0f, 0.4358665215f);  // rebuild from the restored state
            p_after = P.apply(v).detach().clone();
        } catch (const std::exception&) {
            usable = false;
        }

        if (usable && p_before.defined() && p_after.defined()) {
            const double nb = p_before.norm().item<double>();
            check(nb > 0.0, "P^-1 v is nonzero, so the comparison below is not a comparison of 0");
            const double eps_P =
                (p_after - p_before).norm().item<double>() / std::max(nb, 1e-300);
            check(eps_P < 1e-6,
                  "ROLLBACK EQUIVALENCE: after restore+rebuild, P^-1 v matches the pre-replay "
                  "operator (eps_P < 1e-6)");
        } else {
            // Stated rather than skipped silently: a contract that quietly does nothing when its
            // fixture cannot run is the witness-that-cannot-fail shape.
            check(false,
                  "apply() could not run in this fixture, so rollback equivalence is UNPROVEN");
        }
    }

    constexpr int expected_checks = 17;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "STAGE_STATE_IDENTITY: PASS" << std::endl; return 0; }
    std::cout << "STAGE_STATE_IDENTITY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
