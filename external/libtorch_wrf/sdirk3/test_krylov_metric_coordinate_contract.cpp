// R9 P0-A/B: the coordinate system a Krylov metric is measured in is part of the metric.
//
// Two published measurements were invalidated by conflating three coordinate systems:
//
//   physical    R              what the stage acceptance gate judges
//   Krylov (S)  r~ = S^-1 R    what FGMRES iterates
//   objective   L r~           what a left weighting L minimises
//
// Defect 1: a "relative residual" whose numerator was unweighted and whose denominator was
//           D-weighted. Its ratio against rho_D reduces to ||D^-1 r~||/||r~|| -- the RHS
//           cancels entirely -- so it measured a directional amplification of D^-1 and was
//           read as a disagreement between two objectives.
// Defect 2: a "stage-WRMS objective" applied as E^-1 when the vectors are r~ = S^-1 R, so the
//           weight that reproduces the gate's ||E^-1 R|| is E^-1 S. Omitting S is not a scale
//           error a tolerance absorbs: it can INVERT which block dominates.
// Defect 3: a "physical residual share" computed from r~, which is the (S) share.
//
// These three contracts make each defect a failing test rather than a review finding.
#include "wrf_sdirk3_krylov_metrics.h"

#include <torch/torch.h>
#include <cmath>
#include <cstdio>

using wrf::sdirk3::RelativeResidual;
using wrf::sdirk3::relative_residual;
using wrf::sdirk3::wrms_left_weight;
using wrf::sdirk3::block_energy_shares;
using wrf::sdirk3::StateLayout;

static int g_fail = 0;
static int g_cases = 0;
static void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static void check_close(double a, double b, double tol, const char* what) {
    ++g_cases;
    if (!(std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)))) {
        std::printf("FAIL: %s (got %.17g, want %.17g)\n", what, a, b);
        ++g_fail;
    }
}

// Two blocks of one element each -- the smallest fixture in which "which block dominates"
// is a yes/no question rather than a matter of degree.
static StateLayout two_block_layout() {
    StateLayout L;
    L.blocks.push_back({"blk0", 0, 1});
    L.blocks.push_back({"blk1", 1, 1});
    L.total_size = 2;
    return L;
}

static torch::Tensor vec(double a, double b) {
    return torch::tensor({a, b}, torch::kFloat64);
}

// ---------------------------------------------------------------------------
// B1  Krylov_Metric_Coordinate_Contract
// ---------------------------------------------------------------------------
// S = diag(100, 0.01), E = diag(10, 0.1).
//   E^-1   = diag(0.1, 10)  -> emphasises block 1 by 100x  (WRONG weighting)
//   E^-1 S = diag(10,  0.1) -> emphasises block 0 by 100x  (the physical stage-WRMS)
// The two weightings do not merely differ in magnitude; they reverse the dominant block.
static void contract_metric_coordinate() {
    const auto layout = two_block_layout();
    const auto S     = vec(100.0, 0.01);
    const auto E_inv = vec(0.1, 10.0);          // = 1/diag(10, 0.1)
    const auto r     = vec(1.0, 1.0);           // equal in Krylov coordinates

    const auto L_E = wrms_left_weight(E_inv, S);
    check(L_E.defined(), "wrms_left_weight defined for matching E^-1 and S");
    check_close(L_E[0].item<double>(), 10.0,  1e-12, "E^-1 S block0");
    check_close(L_E[1].item<double>(), 0.1, 1e-12, "E^-1 S block1");

    const auto share_wrong   = block_energy_shares(layout, r, E_inv);  // the shipped defect
    const auto share_correct = block_energy_shares(layout, r, L_E);

    check(share_wrong[1] > share_wrong[0],
          "E^-1 alone makes block1 dominant (this is what the defect measured)");
    check(share_correct[0] > share_correct[1],
          "E^-1 S makes block0 dominant (the physical stage-WRMS)");
    // The decisive statement: dropping S inverts the attribution.
    check((share_wrong[0] < share_wrong[1]) != (share_correct[0] < share_correct[1]),
          "omitting S REVERSES which block the objective attributes the residual to");

    // No silent fallback: a missing or mismatched S must yield no weight at all, because the
    // available fallback (E^-1) is the third, meaningless objective above.
    check(!wrms_left_weight(E_inv, torch::Tensor{}).defined(),
          "wrms_left_weight returns undefined when S is absent");
    check(!wrms_left_weight(E_inv, torch::ones({3}, torch::kFloat64)).defined(),
          "wrms_left_weight returns undefined when S has the wrong length");
}

// ---------------------------------------------------------------------------
// B2  Relative_Residual_Denominator_Contract
// ---------------------------------------------------------------------------
// b != r throughout, so a denominator taken from the wrong vector cannot hide.
static void contract_relative_residual_denominator() {
    const auto r = vec(3.0, 4.0);
    const auto b = vec(1.0, 8.0);              // deliberately not parallel to r
    const auto L = vec(2.0, 0.5);

    const auto rho = relative_residual(r, b, L);
    check(rho.valid, "relative_residual valid on a well-posed fixture");
    // Independent reference, not sharing code with the helper.
    const double num = std::sqrt(std::pow(2.0 * 3.0, 2) + std::pow(0.5 * 4.0, 2));
    const double den = std::sqrt(std::pow(2.0 * 1.0, 2) + std::pow(0.5 * 8.0, 2));
    check_close(rho.numerator, num, 1e-12, "weighted numerator");
    check_close(rho.denominator, den, 1e-12, "weighted denominator");
    check_close(rho.value, num / den, 1e-12, "weighted ratio");

    // An undefined weight is exactly the identity -- no ones-vector to drift from I.
    const auto rho_id  = relative_residual(r, b, torch::Tensor{});
    const auto rho_one = relative_residual(r, b, torch::ones_like(r));
    check_close(rho_id.value, rho_one.value, 1e-15, "undefined weight == identity weight");

    // THE RETRACTED DEFECT, stated as an identity.
    //
    // The shipped "rho_unscaled" was ||r||/||L b||: numerator unweighted, denominator
    // weighted. Its ratio against the properly weighted rho is
    //
    //     rho_L / rho_mixed  =  ||L r|| / ||r||
    //
    // with b absent from both sides. A relative residual whose ratio to another does not
    // depend on the right-hand side is not comparing two normalisations of one system.
    const double rho_mixed = r.norm().item<double>() / rho.denominator;
    const double ratio     = rho.value / rho_mixed;
    check_close(ratio, (L * r).norm().item<double>() / r.norm().item<double>(), 1e-12,
                "mixed-denominator ratio cancels the RHS (this is why 70-291x was retracted)");
    // ... and it is genuinely a different number, so the mutant is detectable.
    check(std::fabs(rho_mixed - rho.value) > 1e-9, "mixed denominator is not the same value");

    // A weight of the wrong length is a coordinate bug; it must not broadcast.
    check(!relative_residual(r, b, torch::ones({3}, torch::kFloat64)).valid,
          "wrong-length weight is rejected, not broadcast");
    // A zero right-hand side has no relative residual.
    check(!relative_residual(r, torch::zeros_like(b), L).valid,
          "zero RHS yields no relative residual");
}

// ---------------------------------------------------------------------------
// B3  Objective_Share_Coordinate_Contract
// ---------------------------------------------------------------------------
// The four shares must be four DIFFERENT numbers on a fixture designed to separate them,
// so a reader cannot take one for another. Also pins the by-construction identity that
// PR #161 reported as a finding: with D_q = 1/||r~_q||, every block's D-share is 1/n.
static void contract_objective_share_coordinates() {
    const auto layout = two_block_layout();
    const auto r     = vec(1.0, 50.0);      // Krylov-coordinate residual
    const auto S     = vec(100.0, 0.01);
    const auto E_inv = vec(0.1, 10.0);
    const auto L_E   = wrms_left_weight(E_inv, S);
    // D^-1_q = 1/||r~_q||, exactly as the solver builds it.
    const auto D_inv = vec(1.0 / 1.0, 1.0 / 50.0);

    const auto s_krylov = block_energy_shares(layout, r, torch::Tensor{});
    const auto s_phys   = block_energy_shares(layout, r, S);
    const auto s_D      = block_energy_shares(layout, r, D_inv);
    const auto s_wrms   = block_energy_shares(layout, r, L_E);

    for (const auto* p : {&s_krylov, &s_phys, &s_D, &s_wrms}) {
        check_close((*p)[0] + (*p)[1], 1.0, 1e-12, "shares sum to 1");
    }

    // (S) share vs physical share disagree about where the residual lives -- which is the
    // exact substitution the retracted "physical residual share" table made.
    check(s_krylov[1] > s_krylov[0], "in Krylov coordinates block1 dominates");
    check(s_phys[0] > s_phys[1],     "in PHYSICAL coordinates block0 dominates");

    // The D-share is 1/n by construction, so a table showing 1/6 per block is an identity of
    // the weighting, not a measurement of the residual.
    check_close(s_D[0], 0.5, 1e-12, "D-share is 1/n by construction (block0)");
    check_close(s_D[1], 0.5, 1e-12, "D-share is 1/n by construction (block1)");

    // All four genuinely distinct on this fixture.
    check(std::fabs(s_krylov[0] - s_phys[0]) > 1e-6, "krylov share != physical share");
    check(std::fabs(s_krylov[0] - s_D[0])    > 1e-6, "krylov share != D share");
    check(std::fabs(s_phys[0]   - s_wrms[0]) > 1e-6, "physical share != stage-WRMS share");
    check(std::fabs(s_D[0]      - s_wrms[0]) > 1e-6, "D share != stage-WRMS share");
}

int main() {
    torch::NoGradGuard ng;
    contract_metric_coordinate();
    contract_relative_residual_denominator();
    contract_objective_share_coordinates();
    std::printf("%s: %d cases, %d failures\n",
                g_fail == 0 ? "PASS" : "FAIL", g_cases, g_fail);
    return g_fail == 0 ? 0 : 1;
}
