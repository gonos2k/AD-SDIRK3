#ifndef WRF_SDIRK3_COEFFICIENTS_H
#define WRF_SDIRK3_COEFFICIENTS_H

#include <cmath>

namespace wrf {
namespace sdirk3 {

/**
 * Hairer-Wanner L-stable SDIRK-3 Coefficients (3rd order, stiffly accurate)
 *
 * Butcher tableau:
 *   γ       | γ        0        0
 *   (1+γ)/2 | a21      γ        0
 *   1       | b1       b2       γ
 *   --------|---------------------------
 *           | b1       b2       γ       (stiffly accurate: bi = a3i)
 *
 * Where γ is the real root of 6γ³ - 18γ² + 9γ - 1 = 0.
 * b2 = (1 - 4γ + 2γ²)/(1-γ), b1 = 1 - γ - b2, b3 = γ.
 *
 * FIX 2026-01-29: Previous coefficients (a21=1-2γ, b3=0) were only 1st order
 * and caused numerical instability. These correct H-W coefficients are 3rd order
 * and L-stable with R(∞) = 0.
 */

struct WRFCoefficients {
    // gamma: root of 6x³ - 18x² + 9x - 1 = 0
    static constexpr double gamma = 0.43586652150845899942;

    // Time levels (c_i = sum_j a_{ij})
    static constexpr double c1 = gamma;                   // ≈ 0.4359
    static constexpr double c2 = (1.0 + gamma) / 2.0;     // ≈ 0.7179
    static constexpr double c3 = 1.0;                     // = 1.0 (stiffly accurate)

    // Butcher tableau A matrix
    static constexpr double a11 = gamma;
    static constexpr double a21 = 0.28206673924577050029;   // (1-γ)/2
    static constexpr double a22 = gamma;
    static constexpr double a31 = 1.20849664917310119469;   // = b1 (stiffly accurate)
    static constexpr double a32 = -0.64436317068156019411;  // = b2 (stiffly accurate)
    static constexpr double a33 = gamma;

    // Weights: stiffly accurate (bi = a_{3,i})
    static constexpr double b1 = 1.20849664917310119469;
    static constexpr double b2 = -0.64436317068156019411;
    static constexpr double b3 = gamma;  // = γ for L-stability and stiff accuracy

    // Metadata
    static constexpr bool is_L_stable = true;
    static constexpr double R_at_infinity = 0.0;
};

// Number of stages
static constexpr int num_stages = 3;

// Precision-related constants for float (4-byte)
static constexpr float FLOAT_EPSILON = 1.192093e-07f;
static constexpr double COEFF_TOL = 1e-14;
static constexpr float NEWTON_TOL = 1e-5f;
static constexpr float GMRES_TOL = 1e-4f;

/**
 * Verify coefficients (used in debug builds)
 */
[[maybe_unused]] static bool verify_coefficients() {
    const double tol = COEFF_TOL;

    // 1. Verify row sums equal c values
    double row1_sum = WRFCoefficients::a11;
    double row2_sum = WRFCoefficients::a21 + WRFCoefficients::a22;
    double row3_sum = WRFCoefficients::a31 + WRFCoefficients::a32 + WRFCoefficients::a33;

    if (std::abs(row1_sum - WRFCoefficients::c1) > tol) return false;
    if (std::abs(row2_sum - WRFCoefficients::c2) > tol) return false;
    if (std::abs(row3_sum - WRFCoefficients::c3) > tol) return false;

    // 2. Verify weight sum equals 1
    double weight_sum = WRFCoefficients::b1 + WRFCoefficients::b2 + WRFCoefficients::b3;
    if (std::abs(weight_sum - 1.0) > tol) return false;

    // 3. Verify stiff accuracy: bi = a_{3,i}
    if (std::abs(WRFCoefficients::b1 - WRFCoefficients::a31) > tol) return false;
    if (std::abs(WRFCoefficients::b2 - WRFCoefficients::a32) > tol) return false;
    if (std::abs(WRFCoefficients::b3 - WRFCoefficients::a33) > tol) return false;

    // 4. Verify order 2 condition: sum(bi*ci) = 1/2
    double order2 = WRFCoefficients::b1 * WRFCoefficients::c1 +
                    WRFCoefficients::b2 * WRFCoefficients::c2 +
                    WRFCoefficients::b3 * WRFCoefficients::c3;
    if (std::abs(order2 - 0.5) > tol) return false;

    // 5. Verify order 3 condition: sum(bi*ci^2) = 1/3
    double order3 = WRFCoefficients::b1 * WRFCoefficients::c1 * WRFCoefficients::c1 +
                    WRFCoefficients::b2 * WRFCoefficients::c2 * WRFCoefficients::c2 +
                    WRFCoefficients::b3 * WRFCoefficients::c3 * WRFCoefficients::c3;
    if (std::abs(order3 - 1.0/3.0) > tol) return false;

    return true;
}

/**
 * Butcher tableau accessor functions
 */
static constexpr double a(int i, int j) {
    if (i == 1 && j == 1) return WRFCoefficients::a11;
    if (i == 2 && j == 1) return WRFCoefficients::a21;
    if (i == 2 && j == 2) return WRFCoefficients::a22;
    if (i == 3 && j == 1) return WRFCoefficients::a31;
    if (i == 3 && j == 2) return WRFCoefficients::a32;
    if (i == 3 && j == 3) return WRFCoefficients::a33;
    return 0.0;
}

static constexpr double c(int i) {
    if (i == 1) return WRFCoefficients::c1;
    if (i == 2) return WRFCoefficients::c2;
    if (i == 3) return WRFCoefficients::c3;
    return 0.0;
}

static constexpr double b(int i) {
    if (i == 1) return WRFCoefficients::b1;
    if (i == 2) return WRFCoefficients::b2;
    if (i == 3) return WRFCoefficients::b3;
    return 0.0;
}

/**
 * Compute stability function R(z) for SDIRK3
 * For L-stable stiffly accurate scheme: R(∞) = 0
 */
inline double compute_stability_function(double z) {
    double g = WRFCoefficients::gamma;
    // For stiffly accurate L-stable SDIRK3, R(z) = P(z)/(1-gz)³
    // where P is a degree-3 polynomial matching the exponential to 3rd order
    double gz = g * z;
    double denom = (1.0 - gz) * (1.0 - gz) * (1.0 - gz);
    if (std::abs(denom) < 1e-30) return 0.0;

    // Compute numerator from Butcher tableau
    // R(z) = 1 + z*b^T*(I-zA)^{-1}*e
    // For stiffly accurate: R(z) = e_3^T*(I-zA)^{-1}*e (last row of resolvent)
    // At z→∞: R(∞) = 0 (L-stable)
    double z2 = z * z;
    double p1 = WRFCoefficients::b1 + WRFCoefficients::b2 + WRFCoefficients::b3 - 3.0*g;
    double p2 = 3.0*g*g - (WRFCoefficients::b1*WRFCoefficients::c1 +
                WRFCoefficients::b2*WRFCoefficients::c2 + WRFCoefficients::b3*WRFCoefficients::c3)*2.0;

    double numerator = 1.0 + p1*z + p2*z2;
    return numerator / denom;
}

/**
 * Compute acoustic CFL condition
 */
inline float compute_acoustic_cfl(
    float dx, float dy, float dz,
    float c_sound, float dt) {

    float dx_min = dx;
    if (dy < dx_min) dx_min = dy;
    if (dz < dx_min) dx_min = dz;
    float cfl = c_sound * dt / dx_min;
    return cfl;
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_COEFFICIENTS_H
