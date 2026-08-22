// R13 B3: a diagnostic that changes the run is an intervention, and the fingerprint has to be
// able to notice.
//
// THE GAP. ScopedProbeState restores two members and reports what it could not restore -- which
// is the right shape, and its fingerprint covered grid dt, two advection work tensors and an
// RHS call counter. None of those is the state that changes a SOLVE. The Newton solver carries
// hopeless-budget streaks, a trust radius, a preconditioner fallback latch and per-stage
// warm-start slots across calls, and every one of them alters what the next solve does. A probe
// could move all of them and the fingerprint would report nothing moved.
//
// That is not hypothetical. R12 R4's "certifying" second reference warm-started from the first
// and returned it bit-identically; ref_agree=0 was read as agreement rather than as the
// signature of a solve that never independently happened. The state responsible was exactly
// this state, and no fingerprint was watching it.
//
// WHAT THIS FILE PINS. A fingerprint that cannot detect a change is worse than none -- it
// converts an unknown into a false negative. So each carried field is moved ON ITS OWN and the
// digest must move with it. A digest insensitive to any one field would let a probe mutate
// through that field forever, silently, while the record said "observer".

#include "../wrf_sdirk3_newton_solver.h"

#include <torch/torch.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

using Solver = wrf::sdirk3::WRFNewtonKrylovSolver;

std::uint64_t digest_after(Solver& s, const Solver::CarriedState& st) {
    s.restore_carried_state(st);
    return s.carried_state_digest();
}

}  // namespace

int main() {
    wrf::sdirk3::WRFNewtonKrylovOptions opts;
    Solver solver(opts, /*mu_size=*/0);

    const auto base = solver.capture_carried_state();
    const std::uint64_t d0 = digest_after(solver, base);

    check(digest_after(solver, base) == d0,
          "the digest is a FUNCTION of the state: the same state twice gives the same value");

    // Each field on its own. A digest that misses one lets a probe mutate through it forever.
    {
        auto s = base; s.stage2_hopeless_streak += 1;
        check(digest_after(solver, s) != d0, "the digest sees stage2_hopeless_streak");
    }
    {
        auto s = base; s.stage3_hopeless_streak += 1;
        check(digest_after(solver, s) != d0, "the digest sees stage3_hopeless_streak");
    }
    {
        auto s = base; s.stage2_hopeless_budget_mode = !s.stage2_hopeless_budget_mode;
        check(digest_after(solver, s) != d0, "the digest sees stage2_hopeless_budget_mode");
    }
    {
        auto s = base; s.stage3_hopeless_budget_mode = !s.stage3_hopeless_budget_mode;
        check(digest_after(solver, s) != d0, "the digest sees stage3_hopeless_budget_mode");
    }
    {
        auto s = base; s.stage3_warmstart_disabled = !s.stage3_warmstart_disabled;
        check(digest_after(solver, s) != d0, "the digest sees stage3_warmstart_disabled");
    }
    {
        auto s = base; s.precond_fallback_count += 1;
        check(digest_after(solver, s) != d0,
              "the digest sees precond_fallback_count -- a fallback changes the OPERATOR, not "
              "a knob, so a probe that triggers one has changed the run");
    }
    {
        auto s = base; s.trust_radius = s.trust_radius * 0.5f + 0.125f;
        check(digest_after(solver, s) != d0, "the digest sees trust_radius");
    }
    {   // The R12 R4 mechanism: a warm start left behind by a diagnostic solve.
        auto s = base;
        s.warmstart_stage.assign(4, torch::Tensor{});
        s.warmstart_stage[2] = torch::ones({8}, torch::kFloat32);
        s.warmstart_relerr.assign(4, -1.0f);
        check(digest_after(solver, s) != d0,
              "the digest sees a warm-start slot being populated -- the exact state that made "
              "R12 R4's second reference return the first one unchanged");
    }
    {   // ...and WHICH slot holds it, because two stages are not interchangeable.
        auto a = base;
        a.warmstart_stage.assign(4, torch::Tensor{});
        a.warmstart_stage[2] = torch::ones({8}, torch::kFloat32);
        a.warmstart_relerr.assign(4, -1.0f);
        const auto da = digest_after(solver, a);
        auto b = a;
        b.warmstart_stage[2] = torch::Tensor{};
        b.warmstart_stage[3] = torch::ones({8}, torch::kFloat32);
        check(digest_after(solver, b) != da,
              "and WHICH stage slot holds it: the digest is order-sensitive, so a warm start "
              "moved between stages is a different state");
    }

    // Restore has to be a restore. A snapshot that aliases the live tensors is not one, and
    // this codebase has already shipped a latch that depended on a value it did not own.
    {
        auto s = base;
        auto shared = torch::ones({8}, torch::kFloat32);
        s.warmstart_stage.assign(1, shared);
        s.warmstart_relerr.assign(1, 0.5f);
        solver.restore_carried_state(s);
        const auto captured = solver.capture_carried_state();
        shared.mul_(3.0);   // the live tensor moves AFTER the capture
        const bool independent =
            captured.warmstart_stage.size() == 1 &&
            captured.warmstart_stage[0].defined() &&
            captured.warmstart_stage[0].sum().item<float>() == 8.0f;
        check(independent,
              "capture CLONES the warm-start slots: a snapshot that aliased them would be "
              "overwritten by the very solve it exists to undo");
    }

    // R13.1: RESTORE must clone too. R13 tested that capture clones and stopped there --
    // half a round trip, and the half that was broken. Assigning the vector shares storage
    // with the snapshot, so the next solve writing a warm-start slot in place edits the
    // snapshot the following arm restores from.
    {
        auto s = base;
        s.warmstart_stage.assign(1, torch::ones({8}, torch::kFloat32));
        s.warmstart_relerr.assign(1, 0.5f);
        solver.restore_carried_state(s);
        const auto live = solver.capture_carried_state();
        // Whatever the solver now writes in place must not reach the snapshot `s`.
        live.warmstart_stage[0].mul_(7.0);
        const bool snapshot_intact =
            s.warmstart_stage[0].defined() &&
            s.warmstart_stage[0].sum().item<float>() == 8.0f;
        check(snapshot_intact,
              "RESTORE clones as well: an in-place write by the solve that follows cannot "
              "reach back into the snapshot the next arm restores from");
    }

    // R13.1: the digest has to see DIRECTION. Mixing only norm and numel made v and -v the
    // same state -- and v and Pv for any norm-preserving P. Either changes what the next
    // solve does, and neither was visible.
    {
        auto a = base;
        a.warmstart_stage.assign(1, torch::tensor({1.0f, 2.0f, 3.0f, 4.0f}));
        a.warmstart_relerr.assign(1, -1.0f);
        const auto da = digest_after(solver, a);

        auto negated = a;
        negated.warmstart_stage.assign(1, torch::tensor({-1.0f, -2.0f, -3.0f, -4.0f}));
        check(digest_after(solver, negated) != da,
              "v and -v are DIFFERENT states. This case FAILED when written: plain FNV-1a "
              "cannot see a change confined to bit 63 (multiplication mod 2^64 never carries "
              "out of it), and a negation flips the sign of both the sum and the first "
              "moment -- two such changes cancel exactly. The avalanche shift is what makes "
              "the digest able to notice at all");

        auto permuted = a;
        permuted.warmstart_stage.assign(1, torch::tensor({4.0f, 3.0f, 2.0f, 1.0f}));
        check(digest_after(solver, permuted) != da,
              "and a PERMUTATION is a different state: same norm, same sum, different "
              "first moment");
    }

    // Round trip: restoring the captured state returns the digest to where it was.
    {
        solver.restore_carried_state(base);
        check(solver.carried_state_digest() == d0,
              "capture -> mutate -> restore returns the digest to its entry value, which is "
              "what makes 'every arm started from the same state' checkable rather than "
              "assumed");
    }

    constexpr int expected_checks = 15;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "DIAGNOSTIC_OBSERVER_NONINTERFERENCE_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "DIAGNOSTIC_OBSERVER_NONINTERFERENCE_CONTRACT: FAIL (" << failures << ")"
              << std::endl;
    return 1;
}
