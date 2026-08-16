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
#include <memory>
#include <string>
#include <vector>

#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_types.h"
#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_unified_rhs.h"

#include <torch/torch.h>

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
              "REFERENCE PROPERTY (a staggered pair built here, not production's): "
              "<q,D_h u> + <G_h q,u> = 0 to machine precision -- this establishes what adjointness "
              "looks like and what sign it forces; it is NOT evidence about the production "
              "operators, which case 3 reads directly");
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

    // ---- 3. What PRODUCTION actually built -- read, not re-typed ---------------------------
    // The previous version of this case hard-coded `-h*(c_s^2/mu0)*H_x` as literals copied out of
    // the preconditioner. That asserts a property of the literals: it would keep passing if the
    // production formula changed, which is precisely when this finding would need revisiting.
    // Now the coefficients come from a real UnifiedPreconditioner.
    {
        auto grid = tiny_grid();
        auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
        wrf::sdirk3::UnifiedPreconditioner P(grid, physics, 600.0f, 0.4358665215f);

        const auto c = P.horizontal_coupling_snapshot();
        check(c.c_u_mu.defined() && c.c_mu_u.defined() &&
              c.c_u_mu.numel() > 0 && c.c_mu_u.numel() > 0,
              "the production mu<->u couplings are readable, so the sign check below has real "
              "operands rather than transcribed constants");

        const auto prod = (c.c_mu_u * c.c_u_mu);
        const double prod_max = prod.max().item<double>();
        const double prod_min = prod.min().item<double>();
        std::printf("  production C_mu_u*C_u_mu over levels: min=%.6g max=%.6g\n",
                    prod_min, prod_max);

        // Same-sign factors -> non-negative product. This is the measured claim.
        check(prod_min >= 0.0,
              "PRODUCTION SIGN: every level's C_mu_u * C_u_mu is non-negative -- the two "
              "directions share a sign, the opposite of what the adjoint relation gives");
        check(prod_max > 0.0,
              "and the product is genuinely nonzero, so the Schur step SUBTRACTS a positive "
              "quantity from the mass diagonal rather than adding stiffness to it");

        // The snapshot must not alias live state (the lesson from the phi accessor). Compare
        // against the value captured BEFORE scribbling -- my first attempt asserted "> -999",
        // which these coefficients (~ -2.3e4 at this grid spacing) never satisfied, so the check
        // failed for its own arithmetic rather than for aliasing. A sentinel is only a probe if
        // it lies outside the data's actual range.
        const auto before = P.horizontal_coupling_snapshot().c_mu_u.clone();
        auto scribble = P.horizontal_coupling_snapshot().c_mu_u;
        scribble.fill_(-999.0f);
        const auto after = P.horizontal_coupling_snapshot().c_mu_u;
        check(torch::equal(before, after),
              "and the coupling snapshot is DECOUPLED from the object: scribbling the returned "
              "tensor leaves the preconditioner's own coefficients bit-identical");
    }

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "DISCRETE_ADJOINT_PAIR: PASS" << std::endl; return 0; }
    std::cout << "DISCRETE_ADJOINT_PAIR: FAIL (" << failures << ")" << std::endl;
    return 1;
}
