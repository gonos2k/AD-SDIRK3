// 9F.D49: the base-state EOS, forward AND tangent (review section 1).
//
// WHY THE TANGENT MATTERS AS MUCH AS THE VALUE. This is a 4D-Var project: the EOS enters
// the Newton Jacobian, the JVP and the adjoint, not only the forward pressure gradient.
// The two formulas differ in their DERIVATIVES by more than they differ in value, and in
// a different direction:
//
//     correct:  alpha = (rd/p0)*theta*(p/p0)^cvpm,  cvpm = -cv/cp
//               d(alpha)/d(theta) =  alpha/theta
//               d(alpha)/d(p)     =  cvpm * alpha/p        =  -(cv/cp) * alpha/p
//     old:      alpha_old = rd*theta/p
//               d(alpha_old)/d(p) = -alpha_old/p
//
// So even at EQUAL alpha the old form overstates the pressure-direction tangent by
// cp/cv = 1.4 exactly. A forward-only check cannot see that, and D47 shipped with only a
// forward measurement. Verified symbolically before this test was written.
//
// Structure: analytic Jacobian <- AD JVP -> finite differences, in float32 and float64,
// plus a negative control that the OLD formula fails. Three independent routes to the
// same derivative, because agreement between two of them proves less than it looks.

#include "../wrf_hydrostatic_pressure.h"

#include <torch/torch.h>

#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

// 9F.D49: scientific notation. std::to_string gives 6 DECIMALS, so 1.2e-08 prints as
// "0.000000" -- three of this file's failures first appeared as "rel=0.000000 FAIL",
// which reads like a passing value and a broken comparison. It was neither.
std::string sci(double v) {
    std::ostringstream o; o << std::scientific << std::setprecision(3) << v; return o.str();
}

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

constexpr float RD = 287.0f, CP = 1004.5f, CV = 717.5f, P1000 = 1.0e5f;

// The formula D47 replaced. Kept ONLY as a negative control: it must fail the contract.
torch::Tensor legacy_inverse_density(const torch::Tensor& theta, const torch::Tensor& p) {
    return RD * theta / p;
}

}  // namespace

int main() {
    using wrf::sdirk3::compute_inverse_density;
    torch::manual_seed(0);

    // A realistic column: 950 hPa down to 100 hPa, theta increasing with height.
    const int n = 64;
    auto p64  = torch::linspace(9.5e4, 1.0e4, n, torch::kFloat64);
    auto th64 = torch::linspace(288.0, 430.0, n, torch::kFloat64);

    // --- forward: matches the closed form, in both precisions ---
    {
        const double cvpm = -static_cast<double>(CV) / static_cast<double>(CP);
        auto expect = (static_cast<double>(RD) / P1000) * th64 *
                      torch::pow(p64 / static_cast<double>(P1000), cvpm);
        auto got = compute_inverse_density(th64, p64, RD, CV, CP, P1000);
        const double rel = ((got - expect).abs() / expect.abs()).max().item<double>();
        check(rel < 1e-12, "float64 forward matches the closed form (rel=" +
                           sci(rel) + ")");

        auto got32 = compute_inverse_density(th64.to(torch::kFloat32),
                                             p64.to(torch::kFloat32), RD, CV, CP, P1000);
        const double rel32 = ((got32.to(torch::kFloat64) - expect).abs() / expect.abs())
                                 .max().item<double>();
        check(rel32 < 1e-6, "float32 forward agrees to float eps (rel=" +
                            sci(rel32) + ")");
    }

    // --- the SECOND form of the same law, as an independent route ---
    // The docs and the memory both state the WRF expression "IS R_d*theta*Pi/p", with
    // Pi = (p/p0)^(R_d/cp). That equality is not a restatement -- it holds only if
    //     R_d/cp - 1 == -cv/cp   <=>   cv == cp - R_d   (Mayer)
    // so this simultaneously pins the SIGN convention of the exponent and the mutual
    // consistency of rd/cv/cp. A cv<->cp swap, or constants that do not satisfy Mayer,
    // fails here while passing every check above (they all use the same cvpm).
    {
        auto exner = torch::pow(p64 / static_cast<double>(P1000),
                                static_cast<double>(RD) / static_cast<double>(CP));
        auto via_exner = static_cast<double>(RD) * th64 * exner / p64;
        auto got = compute_inverse_density(th64, p64, RD, CV, CP, P1000);
        const double rel = ((got - via_exner).abs() / via_exner.abs()).max().item<double>();
        check(rel < 1e-12, "equals R_d*theta*Pi/p, so the exponent sign and Mayer's "
                           "cv=cp-R_d both hold (rel=" + sci(rel) + ")");
    }

    // --- ANALYTIC vs AD: d(alpha)/d(theta) = alpha/theta ---
    {
        auto th = th64.clone().requires_grad_(true);
        auto a  = compute_inverse_density(th, p64, RD, CV, CP, P1000);
        auto g  = torch::autograd::grad({a.sum()}, {th})[0];
        auto analytic = compute_inverse_density(th64, p64, RD, CV, CP, P1000) / th64;
        const double rel = ((g - analytic).abs() / analytic.abs()).max().item<double>();
        check(rel < 1e-12, "AD d(alpha)/d(theta) == alpha/theta (rel=" +
                           sci(rel) + ")");
    }

    // --- ANALYTIC vs AD: d(alpha)/d(p) = cvpm * alpha/p ---
    // This is the one the old formula gets wrong, so it carries the weight here.
    {
        auto p = p64.clone().requires_grad_(true);
        auto a = compute_inverse_density(th64, p, RD, CV, CP, P1000);
        auto g = torch::autograd::grad({a.sum()}, {p})[0];
        const double cvpm = -static_cast<double>(CV) / static_cast<double>(CP);
        auto analytic = cvpm * compute_inverse_density(th64, p64, RD, CV, CP, P1000) / p64;
        const double rel = ((g - analytic).abs() / analytic.abs()).max().item<double>();
        check(rel < 1e-12, "AD d(alpha)/d(p) == cvpm*alpha/p (rel=" +
                           sci(rel) + ")");
        check((g < 0).all().item<bool>(),
              "and it is NEGATIVE: alpha falls as pressure rises");
    }

    // --- FINITE DIFFERENCES: a third, independent route ---
    // Central differences in float64 with a relative step. Agreement between analytic and
    // AD alone would not distinguish "both right" from "both wrong in the same way",
    // since AD differentiates the same expression the analytic form was derived from.
    {
        const double eps = 1e-6;
        auto p_hi = p64 * (1.0 + eps);
        auto p_lo = p64 * (1.0 - eps);
        auto fd = (compute_inverse_density(th64, p_hi, RD, CV, CP, P1000) -
                   compute_inverse_density(th64, p_lo, RD, CV, CP, P1000)) / (2.0 * eps * p64);
        const double cvpm = -static_cast<double>(CV) / static_cast<double>(CP);
        auto analytic = cvpm * compute_inverse_density(th64, p64, RD, CV, CP, P1000) / p64;
        const double rel = ((fd - analytic).abs() / analytic.abs()).max().item<double>();
        check(rel < 1e-8, "central FD agrees with the analytic d/dp (rel=" +
                          sci(rel) + ")");
    }

    // --- NEGATIVE CONTROL: the OLD formula fails both value and tangent ---
    {
        auto a_new = compute_inverse_density(th64, p64, RD, CV, CP, P1000);
        auto a_old = legacy_inverse_density(th64, p64);
        const double ratio_max = (a_old / a_new).max().item<double>();
        check(ratio_max > 1.5,
              "legacy rd*theta/p is HIGH by up to " + sci(ratio_max) +
              "x on this column (1/Pi), so the forward check rejects it");

        // The tangent discrepancy is the sharper one: at EQUAL alpha the old form
        // overstates |d/dp| by exactly cp/cv = 1.4, independent of the state.
        auto p = p64.clone().requires_grad_(true);
        auto go = torch::autograd::grad({legacy_inverse_density(th64, p).sum()}, {p})[0];
        auto tangent_ratio = (go.abs() / a_old) / ((-static_cast<double>(CV) /
                              static_cast<double>(CP)) * -1.0 / p64);
        const double r = tangent_ratio.mean().item<double>();
        check(std::abs(r - static_cast<double>(CP) / static_cast<double>(CV)) < 1e-9,
              "and its normalised pressure tangent is exactly cp/cv = " +
              std::to_string(static_cast<double>(CP) / static_cast<double>(CV)) +
              "x too large (measured " + sci(r) + ")");
    }

    // --- theta must be ABSOLUTE, not the t_init perturbation ---
    // A canary for the confusion this campaign already hit once: a review claimed the
    // +t0 was missing when tile_unified_impl.cpp:24917 already applies it. Feeding
    // theta-t0 here is a ~300 K error and must be nowhere near correct.
    {
        auto a_abs  = compute_inverse_density(th64, p64, RD, CV, CP, P1000);
        auto a_pert = compute_inverse_density(th64 - 300.0, p64, RD, CV, CP, P1000);
        const double rel = ((a_pert - a_abs).abs() / a_abs.abs()).max().item<double>();
        check(rel > 0.5, "passing theta-t0 instead of theta is grossly wrong (rel=" +
                         sci(rel) + "), so the absolute-theta contract is testable");
    }

    constexpr int expected_checks = 10;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_EOS_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "BASE_EOS_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
