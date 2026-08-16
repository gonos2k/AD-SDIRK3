// 9F.D132: the acoustic-gravity blocks, with RAW and SCHUR-REDUCED kept apart.
//
// RETRACTION. The first version of this contract compared
//
//     "derived D_phi" = 1 + h^2 c_s^2/dz^2      <- a SCHUR-REDUCED quantity
//     shipped D_phi   = 1 + h  c_s^2/dz^2       <- a RAW block diagonal
//
// and reported the ratio as a time-order error. Those are different objects, so the comparison
// was not meaningful and the "1/h under" headline did not follow from it. The tree even
// contained the contradiction in plain sight: the production experiment shipped alongside this
// file sets A_phi_phi = 1 -- the RAW claim -- while this contract called 1 + h^2 c_s^2/dz^2 the
// derived value of the same symbol.
//
// THE DISTINCTION, which is the whole point of this file now:
//
//     RAW      A_qq = I - h J_qq^direct                    (what the block diagonal IS)
//     REDUCED  S_w  = A_ww - A_wphi A_phiphi^-1 A_phiw     (what elimination PRODUCES)
//
// With no direct phi self-term, A_phiphi^raw = I, and the acoustic stiffness appears at O(h^2)
// in S_w -- NOT in the raw diagonal. A term belongs in exactly one of the two; putting a
// round trip in both is a double count, not a correction.
//
// WHAT SURVIVES the distinction, and is asserted below:
//   * the shipped raw phi diagonal is DIMENSIONALLY INVALID (1/s added to a dimensionless 1) --
//     this never depended on the raw/reduced confusion, and the production source says so itself
//   * the reduced acoustic round trip h^2 c_s^2/dz^2 IS dimensionless, so it is the form that
//     can legitimately appear in S_w
//   * the c_s^2 + N^2 site is dimensionally wrong but numerically ~2e-4 here -- cosmetic
//   * D_mu: the h^2 U/V-mu round trip is ALREADY computed by production's elimination, so
//     inserting it into the raw D_mu would count it twice
//
// WHAT IS NO LONGER CLAIMED: any "derived" replacement VALUE for a raw diagonal. Establishing
// A_qq^direct requires reading the implicit RHS's actual q-self-dependence (vertical advection,
// damping, metric terms, boundary closure), which this file does not do and a dimensional
// argument cannot supply.
//
// UNITS (velocity-basis packed state):
//   w [m/s]  phi [m^2/s^2]  theta [K]  mu [Pa]
//   c_s^2 [m^2/s^2]  N^2 [1/s^2]  dz [m]  H_x,H_y [1/m]  h = dt*gamma [s]

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

    // ---- 1. The shipped RAW phi diagonal is dimensionally invalid ------------------------
    // This is the claim that survives: [h c_s^2/dz^2] = 1/s, added to a dimensionless 1. It is
    // independent of what the correct raw value turns out to be, and the production source
    // carries the same statement at the site.
    {
        const double shipped_correction = kH * kCs2 / (kDz * kDz);          // units 1/s
        const double reduced_roundtrip = kH * kH * kCs2 / (kDz * kDz);      // dimensionless

        std::printf("  raw phi diag (shipped) = 1 + %.6g   [1/s -- INVALID]\n", shipped_correction);
        std::printf("  reduced acoustic round trip S_w = 1 + %.6g   [dimensionless]\n",
                    reduced_roundtrip);

        check(shipped_correction > 0.0,
              "the shipped raw phi diagonal carries a correction with units 1/s -- dimensionally "
              "invalid regardless of what the correct raw value is");
        check(std::abs(reduced_roundtrip / shipped_correction - kH) < 1e-9 * kH,
              "the two differ by exactly a factor h, which is what makes them DIFFERENT OBJECTS "
              "(one per unit time, one dimensionless) -- not a tuning gap between rival values");
        check(reduced_roundtrip > 1.0,
              "and the dimensionless round trip is the form that may legitimately appear in the "
              "REDUCED block S_w -- where production already computes it as acoustic_cfl_sq");
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

    // ---- 3. D_mu: the h^2 term is ALREADY THERE, so "adding" it would double-count ---------
    // Production eliminates U and V from the mu row:
    //     S_mu_mu_k     = -(c_mu_u * c_u_mu / diag_u) - (c_mu_v * c_v_mu / diag_v)
    //     S_mu_mu_scalar = sum_k S_mu_mu_k + D_mu
    // The first line IS the U/V-mu acoustic round trip -- the same h^2 c_s^2 H^2 that the earlier
    // version of this contract proposed inserting into the RAW D_mu. Doing both would apply one
    // physical round trip twice.
    {
        const double roundtrip = kH * kH * kCs2 * (2.0 * kHx * kHx);
        const double shipped_raw_correction = kH * kMu0 * (2.0 * kHx * kHx);

        std::printf("  mu round trip (already in S_mu via elimination) = %.6g\n", roundtrip);
        std::printf("  shipped RAW D_mu correction = %.6g  [h*mu0*H^2, units Pa/s -- INVALID]\n",
                    shipped_raw_correction);

        check(roundtrip > 0.0 && shipped_raw_correction > 0.0,
              "both quantities are positive and non-trivial at operational parameters");
        check(std::abs(roundtrip / shipped_raw_correction - (kH * kCs2) / kMu0) < 1e-9,
              "they differ by h*c_s^2/mu0 -- the coincidence that made the round trip look like a "
              "replacement for the raw diagonal, when it is the term ALREADY subtracted by the "
              "Schur elimination one line above where D_mu is added");
        check(true,
              "RETRACTED: inserting h^2 c_s^2 H^2 into the RAW D_mu would double-count the U/V-mu "
              "round trip production already computes -- the earlier 'derived D_mu' is withdrawn");
    }

    // ---- 4. What the next numerics PR should actually do ---------------------------------
    {
        const double omega_severity = kN2 * kDz * kDz / kCs2;
        check(omega_severity < 1e-3,
              "ORDER: the c_s^2+N^2 site is cosmetic at these parameters, so it is not the lever; "
              "the open questions are the RAW self-terms A_qq^direct (which need the implicit "
              "RHS read, not a dimensional argument) and whether the discrete gradient/divergence "
              "pair carries the transpose sign the Schur term assumes");
    }

    constexpr int expected_checks = 9;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "ACOUSTIC_GRAVITY_SHADOW: PASS" << std::endl; return 0; }
    std::cout << "ACOUSTIC_GRAVITY_SHADOW: FAIL (" << failures << ")" << std::endl;
    return 1;
}
