// 9F.D140 (review 13.3): the mu<->phi coupling has ONE source, and the asymmetry is REACHABLE.
//
// Before D139 the coupling was built independently in three places -- packed 1D 4x4, the 4D
// enhanced solve, and the scalar reference -- and all three assigned the same scalar to both
// directions, the scalar path saying so outright:
//
//     float A_mu_phi = A_phi_mu;  // SYMMETRIC per design (A_μΦ = A_Φμ)
//
// So the measured S_mu_phi == S_phi_mu was guaranteed by construction, and the asymmetry the
// corrected mass equation implies could not be expressed at all.
//
// WHAT THIS FILE DOES NOT DO: assert that the two directions are equal today. They are, but
// pinning that would lock in the defect -- a contract that ratifies the current value is worse
// than no contract, because it makes the wrong answer a requirement. What it pins is the
// STRUCTURE that makes the fix possible and keeps the three paths from diverging again:
//
//   * one decision function, so a change lands everywhere at once
//   * two independently-addressable fields, so asymmetry is representable
//   * a_phi_mu survives when a_mu_phi is zeroed -- the fix must not delete BOTH directions
//
// The last one is the trap worth guarding. "mu does not depend on phi" is about the mu ROW;
// phi <- mu (vertical hydrostatic) is a different entry and must remain.

#include "../wrf_sdirk3_unified_preconditioner.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// Representative of the operational configuration: dt*gamma ~ 261.5 at dt=600, and c2 the
// coefficient the preconditioner pairs with it.
constexpr float kDtGamma = 261.52f;
constexpr float kC2 = 1.2977f;

}  // namespace

int main() {
    std::cout << "=== MuPhi_Coupling_Structure_Contract ===" << std::endl;

    using wrf::sdirk3::mu_phi_direct_coupling;
    using wrf::sdirk3::mu_phi_from_phi_mu;

    // ------------------------------------------------- 1. the hydrostatic direction is real
    {
        const auto c = mu_phi_direct_coupling(kDtGamma, kC2);
        check(std::isfinite(c.a_phi_mu) && c.a_phi_mu != 0.0f,
              "a_phi_mu (vertical hydrostatic phi <- mu) is finite and nonzero");
        check(std::abs(c.a_phi_mu - kDtGamma * kC2) < 1e-4f,
              "a_phi_mu is dt_gamma * c2, the hydrostatic coefficient");
    }

    // ------------------------------------------- 2. ONE decision drives the mu <- phi entry
    // If a future edit reintroduces a second, independent formula in some path, the builder
    // and the decision function stop agreeing and this fails.
    {
        const auto c = mu_phi_direct_coupling(kDtGamma, kC2);
        check(c.a_mu_phi == mu_phi_from_phi_mu(c.a_phi_mu),
              "the builder's a_mu_phi is exactly what the decision function returns");
    }

    // ---------------------------------- 3. determinism: same inputs, same coupling, always
    {
        const auto a = mu_phi_direct_coupling(kDtGamma, kC2);
        const auto b = mu_phi_direct_coupling(kDtGamma, kC2);
        check(a.a_mu_phi == b.a_mu_phi && a.a_phi_mu == b.a_phi_mu,
              "the coupling is a pure function of (dt_gamma, c2)");
    }

    // ------------------------------------------- 4. THE POINT: asymmetry is REPRESENTABLE
    // A struct with two named fields can hold a_mu_phi = 0 while a_phi_mu keeps the
    // hydrostatic value. The single-scalar form this replaced could not express that state at
    // all -- writing zero to "the" coefficient necessarily erased both directions.
    {
        auto c = mu_phi_direct_coupling(kDtGamma, kC2);
        const float hydrostatic = c.a_phi_mu;

        c.a_mu_phi = 0.0f;   // what the corrected mass equation implies for the mu ROW

        check(c.a_mu_phi == 0.0f, "a_mu_phi can be zeroed independently");
        check(c.a_phi_mu == hydrostatic,
              "zeroing a_mu_phi leaves a_phi_mu INTACT -- the fix must not delete both directions");
        check(c.a_phi_mu != c.a_mu_phi,
              "the two directions can differ (the state the single-scalar form could not hold)");
    }

    // ------------------------------- 5. the reverse asymmetry is representable too
    // Not a physical claim; it demonstrates the fields are genuinely independent rather than
    // one being derived from the other in a way that happens to survive case 4.
    {
        auto c = mu_phi_direct_coupling(kDtGamma, kC2);
        const float mu_phi_before = c.a_mu_phi;
        c.a_phi_mu = 0.0f;
        check(c.a_mu_phi == mu_phi_before,
              "zeroing a_phi_mu leaves a_mu_phi intact (the fields are independent both ways)");
    }

    // ----------------------------- 6. scaling behaviour, so a unit change cannot pass silently
    // Both entries carry dt*gamma linearly today. If a re-derivation moves either to a
    // different order in h (the review's I - hJ vs h^2 Schur point), this ratio moves and the
    // change becomes visible here rather than only in a convergence run.
    {
        const auto c1 = mu_phi_direct_coupling(kDtGamma, kC2);
        const auto c2x = mu_phi_direct_coupling(2.0f * kDtGamma, kC2);
        check(std::abs(c2x.a_phi_mu - 2.0f * c1.a_phi_mu) < 1e-3f,
              "a_phi_mu is FIRST order in dt_gamma (doubling h doubles it)");
        check(std::abs(c2x.a_mu_phi - mu_phi_from_phi_mu(c2x.a_phi_mu)) < 1e-6f,
              "a_mu_phi tracks the decision at the doubled step too");
    }

    // ------------------------------------------------- 7. zero input yields zero coupling
    {
        const auto c = mu_phi_direct_coupling(0.0f, kC2);
        check(c.a_phi_mu == 0.0f && c.a_mu_phi == 0.0f,
              "dt_gamma = 0 gives no coupling in either direction");
    }

    constexpr int expected_checks = 11;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "MUPHI_COUPLING: PASS" << std::endl; return 0; }
    std::cout << "MUPHI_COUPLING: FAIL (" << failures << ")" << std::endl;
    return 1;
}
