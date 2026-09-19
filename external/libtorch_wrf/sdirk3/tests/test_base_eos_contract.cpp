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
// Structure: analytic Jacobian <- REVERSE-mode AD -> finite differences, in float32 and
// float64, plus a negative control that the OLD formula fails. Three independent routes to
// the same derivative, because agreement between two of them proves less than it looks.
//
// 9F.D52 CORRECTION (review section 6). This header used to say "AD JVP". It is not one.
// torch::autograd::grad({a.sum()}, {x}) is REVERSE mode with an all-ones cotangent -- a
// VJP. For a pointwise diagonal operator that recovers the diagonal, so the numbers below
// are right and the checks are real, but the label was wrong and it hid three gaps:
// LibTorch's forward-mode dual path was never entered, the direction was always the
// implicit all-ones one rather than an arbitrary mixed (dtheta, dp), and the
// forward/reverse dot-product identity was never checked. In a project whose GMRES matvec
// IS a forward JVP, calling the adjoint path "JVP" is not a naming slip.
//
// AD_Tangent_Contract now covers all three, for this helper and for the pressure
// integrator. What remains here is the reverse-mode and FD evidence, correctly named.

#include "../wrf_hydrostatic_pressure.h"

#include <torch/torch.h>

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

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

void check_pressure_perturbations(torch::ScalarType dtype) {
    const auto options = torch::TensorOptions().dtype(dtype);
    auto column = [&](std::initializer_list<double> values) {
        return torch::tensor(std::vector<double>(values), options).view({1, 2, 1});
    };
    auto pb = column({95000, 80000});
    auto alb = wrf::sdirk3::compute_inverse_density(torch::full_like(pb, 300), pb,
                                                  RD, CV, CP, P1000);
    auto t = column({4e-6, -2e-6});
    auto mu = torch::full({1, 1}, 5e-4, options);
    auto mub = torch::full_like(mu, 80000);
    auto rdnw = torch::tensor({2., 4.}, options);
    auto c1 = torch::tensor({0.75, 0.5}, options);
    auto c2 = torch::tensor({1000., 3000.}, options);
    auto direction = torch::tensor({0., 1., -0.5}, options).view({1, 3, 1});
    auto evaluate = [&](const torch::Tensor& ph) {
        return wrf::sdirk3::calc_p_rho_wrf(ph, t, mu, mub, alb, pb, rdnw, c1, c2,
                                         RD, CV, CP, P1000, 300);
    };
    auto ph = 3e-4 * direction;
    auto got = evaluate(ph);
    const double kappa = static_cast<double>(CP) / CV;
    std::vector<double> al_ref, alt_ref, p_ref, da_ref, dp_ref;
    // Scalar reference starts from the SAME stored inputs, including rounded alb.
    // It deliberately does not assume that this quantized base is exactly balanced.
    for (int k = 0; k < 2; ++k) {
        const double a = alb.flatten()[k].item<double>();
        const double c = c1[k].item<double>();
        const double denom = c * (mub.item<double>() + mu.item<double>()) + c2[k].item<double>();
        const double dph = ph.flatten()[k+1].item<double>() - ph.flatten()[k].item<double>();
        const double al = (rdnw[k].item<double>() * dph - a * c * mu.item<double>()) / denom;
        const double alt = a + al;
        const double p = P1000 * std::pow(RD * (300. + t.flatten()[k].item<double>()) /
                                          (P1000 * alt), kappa);
        const double da = rdnw[k].item<double>() *
            (direction.flatten()[k+1].item<double>() - direction.flatten()[k].item<double>()) / denom;
        al_ref.push_back(al); alt_ref.push_back(alt); p_ref.push_back(p - pb.flatten()[k].item<double>());
        da_ref.push_back(da); dp_ref.push_back(-kappa * p * da / alt);
    }
    auto reference = [&](const std::vector<double>& values) {
        return torch::tensor(values, torch::kFloat64).view({1, 2, 1});
    };
    auto close = [](const torch::Tensor& x, const torch::Tensor& y, double rtol, double atol) {
        return torch::allclose(x.to(torch::kFloat64), y.to(torch::kFloat64), rtol, atol);
    };
    bool outputs_ok = true;
    for (const auto& pair : {std::make_pair(got.al_pert, reference(al_ref)),
                             std::make_pair(got.alt, reference(alt_ref)),
                             std::make_pair(got.p_pert, reference(p_ref))}) {
        outputs_ok &= pair.first.scalar_type() == dtype && pair.first.device() == ph.device() &&
                      pair.first.sizes() == t.sizes() && pair.first.is_contiguous() &&
                      close(pair.first, pair.second.to(dtype),
                            dtype == torch::kFloat64 ? 1e-12 : 2e-7, 1e-10);
    }
    const std::string label = dtype == torch::kFloat32 ? "FP32 " : "FP64 ";
    check(outputs_ok, label + "pressure/alpha outputs preserve ABI and agree with scalar reference");

    auto q = torch::full({}, 3e-4, options).requires_grad_(true);
    auto reverse = torch::autograd::grad({evaluate(q * direction).p_pert.sum()}, {q})[0];
    auto dp = reference(dp_ref);
    const double ad_rtol = dtype == torch::kFloat64 ? 1e-12 : 2e-6;
    check(close(reverse, dp.sum(), ad_rtol, 1e-10), label + "pressure reverse derivative matches analytic EOS");

    auto level = torch::autograd::forward_ad::enter_dual_level();
    auto dual = evaluate(torch::_make_dual(ph, direction, level));
    bool tangent_ok = true;
    for (const auto& pair : {std::make_pair(dual.al_pert, reference(da_ref)),
                             std::make_pair(dual.alt, reference(da_ref)),
                             std::make_pair(dual.p_pert, dp)}) {
        auto tangent = std::get<1>(torch::_unpack_dual(pair.first, level));
        tangent_ok &= tangent.defined() && close(tangent, pair.second, ad_rtol, 1e-10);
    }
    torch::autograd::forward_ad::exit_dual_level(level);
    check(tangent_ok, label + "all three pressure/alpha forward tangents match analytic EOS");

    constexpr double epsilon = 1e-4;
    auto plus = evaluate(ph + epsilon * direction).p_pert.to(torch::kFloat64);
    auto minus = evaluate(ph - epsilon * direction).p_pert.to(torch::kFloat64);
    check(close((plus - minus) / (2 * epsilon), dp, 2e-6, 1e-8),
          label + "small represented geopotential perturbations survive the pressure calculation");
}

void check_state_perturbations(torch::ScalarType dtype) {
    const auto options = torch::TensorOptions().dtype(dtype);
    auto column = [&](std::initializer_list<double> values) {
        return torch::tensor(std::vector<double>(values), options).view({1, 2, 1});
    };
    auto pb = column({95000, 80000});
    auto alb = wrf::sdirk3::compute_inverse_density(torch::full_like(pb, 300), pb,
                                                    RD, CV, CP, P1000);
    auto t = column({4e-6, -2e-6});
    auto mu = torch::full({1, 1}, 5e-4, options);
    auto mub = torch::full_like(mu, 80000);
    auto rdnw = torch::tensor({2., 4.}, options);
    auto c1 = torch::tensor({0.75, 0.5}, options);
    auto c2 = torch::tensor({1000., 3000.}, options);
    auto ph = torch::zeros({1, 3, 1}, options);
    auto dt_dir = column({0.75, -1.25});
    auto dmu_dir = torch::full_like(mu, 50.0);
    auto evaluate = [&](const torch::Tensor& t_arg, const torch::Tensor& mu_arg) {
        return wrf::sdirk3::calc_p_rho_wrf(ph, t_arg, mu_arg, mub, alb, pb, rdnw, c1, c2,
                                           RD, CV, CP, P1000, 300);
    };

    const double kappa = static_cast<double>(CP) / CV;
    std::vector<double> al_t_ref, alt_t_ref, p_t_ref;
    std::vector<double> al_mu_ref, alt_mu_ref, p_mu_ref;
    std::vector<double> al_mix_ref, alt_mix_ref, p_mix_ref;
    for (int k = 0; k < 2; ++k) {
        const double a = alb.flatten()[k].item<double>();
        const double c = c1[k].item<double>();
        const double m = mu.item<double>();
        const double muts = mub.item<double>() + m;
        const double denom = c * muts + c2[k].item<double>();
        const double al = (-a * c * m) / denom;
        const double alt = a + al;
        const double theta = 300.0 + t.flatten()[k].item<double>();
        const double p = P1000 * std::pow(RD * theta / (P1000 * alt), kappa);
        const double dal_dmu = -c * (a + al) / denom;
        const double dt = dt_dir.flatten()[k].item<double>();
        const double dmu = dmu_dir.item<double>();
        const double dal_mu = dal_dmu * dmu;

        // Independent analytic directions: da/dt=0 and
        // da/dmu=-c1*(alb+al)/(c1*muts+c2), with dp=kappa*p*(dt/theta-da/alt).
        al_t_ref.push_back(0.0);
        alt_t_ref.push_back(0.0);
        p_t_ref.push_back(kappa * p * dt / theta);
        al_mu_ref.push_back(dal_mu);
        alt_mu_ref.push_back(dal_mu);
        p_mu_ref.push_back(-kappa * p * dal_mu / alt);
        al_mix_ref.push_back(dal_mu);
        alt_mix_ref.push_back(dal_mu);
        p_mix_ref.push_back(kappa * p * (dt / theta - dal_mu / alt));
    }
    auto reference = [&](const std::vector<double>& values) {
        return torch::tensor(values, torch::kFloat64).view({1, 2, 1});
    };
    auto close = [dtype](const torch::Tensor& x, const torch::Tensor& y) {
        return torch::allclose(x.to(torch::kFloat64), y.to(torch::kFloat64),
                               dtype == torch::kFloat64 ? 1e-12 : 2e-6, 1e-10);
    };
    const std::string label = dtype == torch::kFloat32 ? "FP32 " : "FP64 ";

    auto check_dual = [&](const torch::Tensor& t_tangent, const torch::Tensor& mu_tangent,
                          const torch::Tensor& al_ref, const torch::Tensor& alt_ref,
                          const torch::Tensor& p_ref) {
        auto level = torch::autograd::forward_ad::enter_dual_level();
        auto t_dual = torch::_make_dual(t, t_tangent, level);
        auto mu_dual = torch::_make_dual(mu, mu_tangent, level);
        auto out = evaluate(t_dual, mu_dual);
        bool ok = true;
        for (const auto& pair : {std::make_pair(out.al_pert, al_ref),
                                 std::make_pair(out.alt, alt_ref),
                                 std::make_pair(out.p_pert, p_ref)}) {
            auto tangent = std::get<1>(torch::_unpack_dual(pair.first, level));
            ok &= tangent.defined() && close(tangent, pair.second);
        }
        torch::autograd::forward_ad::exit_dual_level(level);
        return ok;
    };

    check(check_dual(dt_dir, torch::zeros_like(mu), reference(al_t_ref), reference(alt_t_ref),
                     reference(p_t_ref)),
          label + "independent t_pert FWAD matches da/dt=0 and analytic dp");
    check(check_dual(torch::zeros_like(t), dmu_dir, reference(al_mu_ref), reference(alt_mu_ref),
                     reference(p_mu_ref)),
          label + "independent mu_pert FWAD matches analytic da/dmu and dp");

    // A weighted reverse dot identity checks the same mixed direction through all outputs.
    auto t_leaf = t.clone().requires_grad_(true);
    auto mu_leaf = mu.clone().requires_grad_(true);
    auto mixed = evaluate(t_leaf, mu_leaf);
    auto lambda_al = column({0.7, -1.1});
    auto lambda_alt = column({-0.4, 0.9});
    auto lambda_p = column({1.3, -0.6});
    auto objective = (mixed.al_pert * lambda_al + mixed.alt * lambda_alt +
                      mixed.p_pert * lambda_p).sum();
    auto reverse = torch::autograd::grad({objective}, {t_leaf, mu_leaf});
    const auto mixed_dot =
        (reference(al_mix_ref) * lambda_al.to(torch::kFloat64) +
         reference(alt_mix_ref) * lambda_alt.to(torch::kFloat64) +
         reference(p_mix_ref) * lambda_p.to(torch::kFloat64)).sum();
    const auto reverse_dot =
        (reverse[0].to(torch::kFloat64) * dt_dir.to(torch::kFloat64)).sum() +
        (reverse[1].to(torch::kFloat64) * dmu_dir.to(torch::kFloat64)).sum();
    check(close(reverse_dot, mixed_dot),
          label + "reverse/forward dot identity holds for mixed t_pert+mu_pert EOS direction");
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

    // --- ANALYTIC vs REVERSE-mode AD: d(alpha)/d(theta) = alpha/theta ---
    {
        auto th = th64.clone().requires_grad_(true);
        auto a  = compute_inverse_density(th, p64, RD, CV, CP, P1000);
        auto g  = torch::autograd::grad({a.sum()}, {th})[0];
        auto analytic = compute_inverse_density(th64, p64, RD, CV, CP, P1000) / th64;
        const double rel = ((g - analytic).abs() / analytic.abs()).max().item<double>();
        check(rel < 1e-12, "reverse-mode d(alpha)/d(theta) == alpha/theta (rel=" +
                           sci(rel) + ")");
    }

    // --- ANALYTIC vs REVERSE-mode AD: d(alpha)/d(p) = cvpm * alpha/p ---
    // This is the one the old formula gets wrong, so it carries the weight here.
    {
        auto p = p64.clone().requires_grad_(true);
        auto a = compute_inverse_density(th64, p, RD, CV, CP, P1000);
        auto g = torch::autograd::grad({a.sum()}, {p})[0];
        const double cvpm = -static_cast<double>(CV) / static_cast<double>(CP);
        auto analytic = cvpm * compute_inverse_density(th64, p64, RD, CV, CP, P1000) / p64;
        const double rel = ((g - analytic).abs() / analytic.abs()).max().item<double>();
        check(rel < 1e-12, "reverse-mode d(alpha)/d(p) == cvpm*alpha/p (rel=" +
                           sci(rel) + ")");
        check((g < 0).all().item<bool>(),
              "and it is NEGATIVE: alpha falls as pressure rises");
    }

    // --- FINITE DIFFERENCES: a third, independent route ---
    // Central differences in float64 with a relative step. Agreement between analytic and
    // reverse-mode AD alone would not distinguish "both right" from "both wrong in the
    // same way", since AD differentiates the same expression the analytic form came from.
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

    check_pressure_perturbations(torch::kFloat32);
    check_pressure_perturbations(torch::kFloat64);
    check_state_perturbations(torch::kFloat32);
    check_state_perturbations(torch::kFloat64);

    constexpr int expected_checks = 24;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_EOS_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "BASE_EOS_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
