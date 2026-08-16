// 9F.D132: is the horizontal gradient/divergence pair a DISCRETE ADJOINT, and does the mu Schur
// term inherit the sign that implies?
//
// WHY THIS RANKS ABOVE ANY COEFFICIENT EDIT. Production eliminates U and V from the mu row:
//
//     S_mu_mu = D_mu - sum_k [ C_mu_u * C_u_mu / D_u + C_mu_v * C_v_mu / D_v ]
//
// so the SIGN of the product C_mu_u * C_u_mu decides whether the acoustic round trip stiffens
// the mass diagonal or eats it. Both coefficients are built with the same sign:
//
//     C_u_mu = -h * (c_s^2/mu0) * H_x        C_mu_u = -h * mu0 * H_x
//
// (wrf_sdirk3_unified_preconditioner.cpp), so the product is POSITIVE and the Schur step
// SUBTRACTS it. For a continuous acoustic operator that is the wrong direction: A = I - hJ with
// J ~ -c_s^2 k^2 gives 1 + h^2 c_s^2 k^2, a diagonal that GROWS with stiffness.
//
// The discrete statement of "the divergence is minus the adjoint of the gradient" is
//
//     <q, D_h u>_mu + <G_h q, u>_u = 0
//
// for the actual staggered operators, metrics and boundary conditions. A scalar H_x > 0 used in
// both directions cannot express it -- the transpose/orientation sign is simply absent.
//
// This contract does three things and changes NO production behaviour:
//   1. proves the adjoint identity holds to machine precision for a real staggered
//      gradient/divergence pair on a periodic grid (so the property is testable, not folklore)
//   2. shows the sign consequence: an adjoint pair yields a NEGATIVE coefficient product, hence
//      a Schur step that ADDS to the mass diagonal
//   3. measures what production's same-sign scalars give instead, at operational parameters
//
// It is a measurement pinned as a contract, not a fix. The fix requires deciding what the mu row
// of the implicit RHS actually is, which is the open item this makes concrete.

#include <cmath>
#include <cstdio>
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

constexpr int kN = 16;          // periodic 1-D cells
constexpr double kDx = 1000.0;  // [m]

// Staggered gradient: cell-centred q -> face u. Periodic, face i sits between cells i-1 and i.
std::vector<double> grad(const std::vector<double>& q) {
    std::vector<double> g(kN, 0.0);
    for (int i = 0; i < kN; ++i) {
        const int im = (i - 1 + kN) % kN;
        g[i] = (q[i] - q[im]) / kDx;
    }
    return g;
}

// Divergence: face u -> cell centre. The ADJOINT partner of grad above.
std::vector<double> divg(const std::vector<double>& u) {
    std::vector<double> d(kN, 0.0);
    for (int i = 0; i < kN; ++i) {
        const int ip = (i + 1) % kN;
        d[i] = (u[ip] - u[i]) / kDx;
    }
    return d;
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

}  // namespace

int main() {
    std::cout << "=== Discrete_Adjoint_Pair_Contract ===" << std::endl;

    // Deterministic, non-symmetric test fields -- a symmetric pair could satisfy the identity by
    // accident and would not exercise the orientation.
    std::vector<double> q(kN), u(kN);
    for (int i = 0; i < kN; ++i) {
        q[i] = std::sin(2.0 * M_PI * i / kN) + 0.37 * std::cos(6.0 * M_PI * i / kN) + 0.11 * i;
        u[i] = std::cos(4.0 * M_PI * i / kN) - 0.23 * std::sin(2.0 * M_PI * i / kN) + 0.05 * i;
    }

    // ---- 1. The adjoint identity, to machine precision -----------------------------------
    {
        const double lhs = dot(q, divg(u));
        const double rhs = dot(grad(q), u);
        const double scale = std::max(std::abs(lhs), std::abs(rhs));
        const double resid = std::abs(lhs + rhs) / (scale > 0.0 ? scale : 1.0);
        std::printf("  <q,Du> = %.12g   <Gq,u> = %.12g   |sum|/scale = %.3g\n", lhs, rhs, resid);

        check(resid < 1e-12,
              "<q,D_h u> + <G_h q, u> = 0 to machine precision -- the pair IS a discrete adjoint, "
              "so this is a testable property and not a modelling preference");
        check(std::abs(lhs) > 1e-6 && std::abs(rhs) > 1e-6,
              "and both sides are non-trivial, so the identity above is not 0 = 0");
    }

    // ---- 2. The sign an adjoint pair implies ----------------------------------------------
    // Represent each operator by its action on a single Fourier mode. Adjointness forces the two
    // directions to carry OPPOSITE signs; their product is then negative, and the Schur step
    // (which SUBTRACTS the product) therefore ADDS stiffness to the mass diagonal.
    {
        const double k_h = 2.0 * M_PI / (kN * kDx);
        const double g_sym = +k_h;   // gradient
        const double d_sym = -k_h;   // divergence = MINUS the adjoint of the gradient
        const double product = g_sym * d_sym;

        check(product < 0.0,
              "an adjoint pair gives a NEGATIVE coefficient product (opposite signs)");
        check(-product > 0.0,
              "so S_mu_mu = D_mu - product ADDS the acoustic round trip to the mass diagonal, "
              "which is the direction A = I - hJ predicts for a stiff acoustic operator");
    }

    // ---- 3. What production's same-sign scalars give instead -------------------------------
    {
        const double h = 600.0 * 0.4358665215;
        const double cs2 = 1.2e5;
        const double mu0 = 9.0e4;
        const double H = 1.0e-5;

        // Verbatim from the production coefficient build.
        const double C_u_mu = -h * (cs2 / mu0) * H;
        const double C_mu_u = -h * mu0 * H;
        const double product = C_mu_u * C_u_mu;      // both negative -> POSITIVE
        const double D_mu = 1.0 + h * mu0 * (2.0 * H * H);
        const double S_mu_mu = D_mu - 2.0 * product; // x2 for the u and v directions

        std::printf("  production: C_u_mu=%.6g  C_mu_u=%.6g  product=%.6g\n",
                    C_u_mu, C_mu_u, product);
        std::printf("  D_mu=%.6g  ->  S_mu_mu = D_mu - 2*product = %.6g\n", D_mu, S_mu_mu);

        check(product > 0.0,
              "production's two couplings share a sign, so their product is POSITIVE -- the "
              "opposite of what the adjoint relation gives");
        check(S_mu_mu < 0.0,
              "and the Schur step therefore drives the mass diagonal NEGATIVE at operational "
              "parameters: an indefinite block inside M, produced by a sign, not by physics");
        check(std::abs(S_mu_mu) > 0.1,
              "and it is not a marginal crossing -- the magnitude is O(1), so a sign-preserving "
              "clamp downstream preserves the negativity rather than masking it");
    }

    constexpr int expected_checks = 7;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "DISCRETE_ADJOINT_PAIR: PASS" << std::endl; return 0; }
    std::cout << "DISCRETE_ADJOINT_PAIR: FAIL (" << failures << ")" << std::endl;
    return 1;
}
