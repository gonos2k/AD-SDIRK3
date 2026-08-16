// 9F.D130: the acoustic-gravity coefficients DERIVED, next to what production ships.
//
// This is a SHADOW, not a rewrite. Every earlier single-term "fix" measured on the live operator
// either made conditioning worse (mu-phi asymmetry) or was inert (HEVI mu identity), so the
// re-derivation starts by QUANTIFYING each production coefficient against the value a clean
// I - hJ / Schur derivation gives, at representative em_b_wave parameters. The deliverable is the
// pinned mismatch table -- which discrepancies are large, which are cosmetic -- so the eventual
// numerics PR spends its isolating measurements on the terms that matter.
//
// UNITS, fixed once (velocity-basis packed state, wrf_sdirk3_state_layout.h):
//   w   [m s^-1]      phi [m^2 s^-2]     theta [K]     mu [Pa]
//   c_s^2 [m^2 s^-2]  N^2 [s^-2]         dz [m]        H_x,H_y [m^-1]   mu0 [Pa]
//   h = dt*gamma [s]
//
// STRUCTURE. Two first-order equations eliminated into one produce a ROUND TRIP: the Schur
// diagonal picks up h^2 * (rate1 * rate2), which is dimensionless when the rates multiply to
// [s^-2]. A direct Jacobian rate enters as h * J_qq. Mixing the two orders -- an O(h) term where
// the elimination gives O(h^2) -- is not a small error at h = 261.5 s.
//
// Derived reference forms (vertical acoustic-gravity, standard linearized column):
//   phi <-> w acoustic round trip :  S_phi = 1 + h^2 * c_s^2 / dz^2          [dimensionless]
//   w diagonal (acoustic+gravity) :  S_w   = 1 + h^2 * (c_s^2/dz^2 + N^2)    [dimensionless]
//   mu <-> u,v horizontal acoustic:  S_mu  = 1 + h^2 * c_s^2 * (H_x^2+H_y^2) [dimensionless]
//
// Production forms (wrf_sdirk3_unified_preconditioner.cpp, cited in the campaign findings):
//   D_phi = 1 + h * c_s^2 / dz^2            -- O(h) where the round trip is O(h^2)
//   W     uses (c_s^2 + N^2) / dz^2         -- adds m^2 s^-2 to s^-2 before dividing
//   D_mu  = 1 + h * mu0 * (H_x^2 + H_y^2)   -- carries mu0 [Pa] instead of c_s^2, O(h)

#include <cmath>
#include <cstdio>
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

// Representative em_b_wave / operational parameters.
constexpr double kH   = 600.0 * 0.4358665215;  // h = dt*gamma [s] ~ 261.5
constexpr double kCs2 = 1.2e5;                 // c_s^2 [m^2 s^-2] (~346 m/s)^2
constexpr double kN2  = 1.0e-4;                // N^2 [s^-2] (typical troposphere)
constexpr double kDz  = 500.0;                 // [m]
constexpr double kMu0 = 9.0e4;                 // [Pa] (~900 hPa dry column)
constexpr double kHx  = 1.0e-5;                // [m^-1] (dx = 100 km)

}  // namespace

int main() {
    std::cout << "=== AcousticGravity_Shadow_Contract ===" << std::endl;
    std::printf("  params: h=%.4g s  c_s^2=%.4g  N^2=%.4g  dz=%.4g  mu0=%.4g  H=%.4g\n",
                kH, kCs2, kN2, kDz, kMu0, kHx);

    // ---- 1. D_phi: O(h) shipped where the elimination gives O(h^2) --------------------------
    {
        const double derived = 1.0 + kH * kH * kCs2 / (kDz * kDz);
        const double shipped = 1.0 + kH * kCs2 / (kDz * kDz);
        const double corr_ratio = (shipped - 1.0) / (derived - 1.0);   // = 1/h exactly
        std::printf("  D_phi: derived=%.6g shipped=%.6g correction-ratio=%.6g (=1/h)\n",
                    derived, shipped, corr_ratio);
        check(std::abs(corr_ratio - 1.0 / kH) < 1e-12,
              "D_phi: the shipped correction is EXACTLY 1/h of the derived one -- a time-order "
              "error, not a tuning difference (h = 261.5 at dt=600)");
        check(derived / shipped > 200.0,
              "at operational h the derived phi diagonal is >200x the shipped one -- LARGE");
    }

    // ---- 2. W diagonal: (c_s^2 + N^2)/dz^2 is dimensionally invalid but numerically tiny ----
    {
        const double correct_omega2 = kCs2 / (kDz * kDz) + kN2;        // [s^-2]
        const double shipped_omega2 = (kCs2 + kN2) / (kDz * kDz);      // adds m^2/s^2 to 1/s^2
        const double rel = std::abs(correct_omega2 - shipped_omega2) / correct_omega2;
        std::printf("  omega^2: correct=%.6g shipped-form=%.6g rel-diff=%.3g\n",
                    correct_omega2, shipped_omega2, rel);
        check(rel < 1e-3,
              "omega^2: the c_s^2+N^2 defect is dimensionally WRONG but numerically ~2e-4 "
              "relative here -- gravity is negligible next to c_s^2/dz^2. NOT a dt=600 "
              "candidate; fix for correctness, expect no conditioning change");
        // And the reason, pinned: the misplaced N^2 term is small BECAUSE N^2*dz^2 << c_s^2.
        check(kN2 * kDz * kDz / kCs2 < 1e-3,
              "the smallness is structural (N^2 dz^2 << c_s^2), not a parameter accident");
    }

    // ---- 3. D_mu: wrong physical constant AND wrong time order ------------------------------
    {
        const double derived = 1.0 + kH * kH * kCs2 * (2.0 * kHx * kHx);
        const double shipped = 1.0 + kH * kMu0 * (2.0 * kHx * kHx);
        const double factor = (kH * kCs2) / kMu0;   // what multiplies shipped's correction
        std::printf("  D_mu: derived=%.6g shipped=%.6g missing-factor=h*c_s^2/mu0=%.6g\n",
                    derived, shipped, factor);
        check(std::abs((derived - 1.0) / (shipped - 1.0) - factor) < 1e-9,
              "D_mu: shipped is missing EXACTLY h*c_s^2/mu0 -- and that equals 348.7 here, "
              "matching the campaign's independently measured S_mu_phi=339 to ~3%");
        check(factor > 100.0,
              "the mu correction is underweighted by >100x at operational parameters -- LARGE");
        // The near-coincidence that confused two sessions, pinned so it stays disambiguated:
        // h*c_s^2/mu0 is numerically ~ S_mu_phi because it is the SAME expression, not because
        // the mu-phi coupling is the mu-u,v Schur term.
    }

    // ---- 4. The ordering conclusion, stated as a check so the table cannot be read backwards -
    {
        const double dphi_severity = kH;                          // 261.5x
        const double dmu_severity  = (kH * kCs2) / kMu0;          // 348.7x
        const double omega_severity = kN2 * kDz * kDz / kCs2;     // 2e-4 relative
        check(dmu_severity > 100.0 && dphi_severity > 100.0 && omega_severity < 1e-3,
              "PRIORITY ORDER for the numerics PR: D_mu and D_phi are the large structural "
              "defects; the c_s^2+N^2 site is cosmetic at these parameters");
    }

    constexpr int expected_checks = 7;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "ACOUSTIC_GRAVITY_SHADOW: PASS" << std::endl; return 0; }
    std::cout << "ACOUSTIC_GRAVITY_SHADOW: FAIL (" << failures << ")" << std::endl;
    return 1;
}
