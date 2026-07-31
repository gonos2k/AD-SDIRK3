// 9F.D52: FORWARD-mode tangents, and the forward/reverse duality (review section 6).
//
// WHY THIS FILE EXISTS: THE PREVIOUS TEST OVERCLAIMED. Base_EOS_Contract says it compares
// "analytic Jacobian <- AD JVP -> finite differences". It does not. It calls
//
//     torch::autograd::grad({a.sum()}, {theta})
//
// which is REVERSE mode with an all-ones cotangent -- a VJP. For a pointwise diagonal
// operator that recovers the diagonal, so the numbers were right, but three things were
// never exercised: LibTorch's forward-mode dual path, an ARBITRARY mixed direction rather
// than the implicit basis sweep, and the forward/reverse dot-product identity. The review
// caught the mislabel; the comments there are corrected and the missing coverage is here.
//
// THIS MATTERS MORE THAN A NAMING SLIP IN THIS PROJECT. The GMRES matvec is
// A.v = v - dt*gamma*(J.v), a genuine forward JVP. A test that only ever exercises reverse
// mode is testing the adjoint path and calling it the matvec path. They are different code
// in LibTorch and they fail differently.
//
// THE PRESSURE INTEGRATOR GETS THE SAME TREATMENT, and there it is sharper: p' = L mu' is
// LINEAR and homogeneous, so J.v == L.v exactly -- the operator applied to the tangent IS
// the tangent of the operator, with no tolerance to negotiate. That also makes this the
// test for whether the copy_()-based in-place assembly preserves the graph at all, which
// is a live hazard in this codebase (a sequential in-place recurrence is tracked forward
// and then dies in backward).

#include "../wrf_hydrostatic_pressure.h"

#include <torch/torch.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

std::string sci(double v) {
    std::ostringstream o; o << std::scientific << std::setprecision(3) << v; return o.str();
}

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

double max_rel(const torch::Tensor& got, const torch::Tensor& want) {
    return ((got - want).abs() / want.abs().clamp_min(1e-300)).max().item<double>();
}

constexpr float RD = 287.0f, CP = 1004.5f, CV = 717.5f, P0 = 1.0e5f, P1000 = 1.0e5f;

}  // namespace

int main() {
    using wrf::sdirk3::compute_inverse_density;
    using wrf::sdirk3::compute_pressure_hydrostatic;
    torch::manual_seed(20260731);

    const auto f64 = torch::TensorOptions().dtype(torch::kFloat64);
    const int n = 48;
    auto p64  = torch::linspace(9.5e4, 1.0e4, n, f64);
    auto th64 = torch::linspace(288.0, 430.0, n, f64);
    const double cvpm = -static_cast<double>(CV) / static_cast<double>(CP);

    // An ARBITRARY mixed direction. The old test's all-ones cotangent could not have
    // distinguished a Jacobian from its transpose, or caught a theta/pressure channel swap.
    auto v_th = torch::randn({n}, f64);
    auto v_p  = torch::randn({n}, f64) * 1.0e3;

    auto alpha    = compute_inverse_density(th64, p64, RD, CV, CP, P1000);
    auto analytic = alpha * (v_th / th64 + cvpm * v_p / p64);

    // --- FORWARD MODE, for real: dual numbers through the production helper ---
    {
        auto level = torch::autograd::forward_ad::enter_dual_level();
        auto th_d = torch::_make_dual(th64, v_th, level);
        auto p_d  = torch::_make_dual(p64,  v_p,  level);
        auto out  = compute_inverse_density(th_d, p_d, RD, CV, CP, P1000);
        auto unpacked = torch::_unpack_dual(out, level);
        auto tangent  = std::get<1>(unpacked);
        const bool has_tangent = tangent.defined() && tangent.numel() == n;
        torch::autograd::forward_ad::exit_dual_level(level);

        check(has_tangent, "the EOS propagates a FORWARD-mode dual (this is the path the "
                           "GMRES matvec uses; the old test never touched it)");
        if (has_tangent) {
            const double rel = max_rel(tangent, analytic);
            check(rel < 1e-12, "and the forward tangent equals the analytic Jv in an "
                               "ARBITRARY mixed direction (rel=" + sci(rel) + ")");
        } else {
            check(false, "forward tangent unavailable, so it cannot be compared");
        }
    }

    // --- CENTRAL DIRECTIONAL FD: the third, independent route ---
    // Not a per-channel sweep -- one step along the SAME mixed direction, which is what a
    // directional derivative actually means.
    {
        const double eps = 1e-7;
        auto hi = compute_inverse_density(th64 + eps * v_th, p64 + eps * v_p, RD, CV, CP, P1000);
        auto lo = compute_inverse_density(th64 - eps * v_th, p64 - eps * v_p, RD, CV, CP, P1000);
        auto fd = (hi - lo) / (2.0 * eps);
        const double rel = max_rel(fd, analytic);
        check(rel < 1e-6, "central DIRECTIONAL FD agrees along the same mixed direction "
                          "(rel=" + sci(rel) + ")");
    }

    // --- THE DUALITY: <Jv, lambda> == <v, J^T lambda> ---
    // The one identity that ties the matvec to the adjoint. 4D-Var needs both, and nothing
    // in this repo compared them until now: a transposed or channel-swapped Jacobian passes
    // every per-channel check above and fails here.
    {
        auto lam = torch::randn({n}, f64);
        auto th_r = th64.clone().requires_grad_(true);
        auto p_r  = p64.clone().requires_grad_(true);
        auto a    = compute_inverse_density(th_r, p_r, RD, CV, CP, P1000);
        auto grads = torch::autograd::grad({(a * lam).sum()}, {th_r, p_r});
        const double reverse = (grads[0] * v_th).sum().item<double>() +
                               (grads[1] * v_p ).sum().item<double>();
        const double forward = (analytic * lam).sum().item<double>();
        const double rel = std::abs(forward - reverse) / std::abs(forward);
        check(rel < 1e-12, "<Jv,lambda> == <v,J^T lambda> for the EOS (fwd=" + sci(forward) +
                           ", rev=" + sci(reverse) + ", rel=" + sci(rel) + ")");
    }

    // ================== the PRESSURE INTEGRATOR ==================
    // p' = L mu' is linear and homogeneous, so J.v == L.v EXACTLY. No tolerance to argue
    // about, and it is the strongest possible statement of "the tangent is the operator".
    const int NZ = 12;
    auto opts   = torch::TensorOptions().dtype(torch::kFloat64);
    auto theta3 = torch::full({1, NZ, 1}, 300.0, opts);
    auto pbase3 = torch::full({1, NZ, 1}, 5.0e4, opts);
    auto mu_b   = torch::full({1, 1}, 1.0e5, opts);
    auto c1h    = torch::ones({NZ}, opts);
    auto c2h    = torch::zeros({NZ}, opts);
    auto rdnw   = torch::full({NZ}, static_cast<double>(NZ), opts);   // magnitudes, per D50
    auto rdn    = torch::full({NZ}, static_cast<double>(NZ), opts);

    auto P = [&](const torch::Tensor& mu_full) {
        return compute_pressure_hydrostatic(theta3, mu_full, mu_b, pbase3, mu_b,
                                            c1h, c2h, rdnw, rdn, RD, CV, CP, P0, P1000);
    };

    auto v_mu = torch::randn({1, 1}, opts) * 1.0e3;

    // --- forward dual through copy_()-based in-place assembly ---
    {
        auto level = torch::autograd::forward_ad::enter_dual_level();
        auto mu_dual = torch::_make_dual(mu_b + 500.0, v_mu, level);
        auto out = P(mu_dual);
        auto tangent = std::get<1>(torch::_unpack_dual(out, level));
        const bool has_tangent = tangent.defined() && tangent.numel() == NZ;
        torch::Tensor t_copy;
        if (has_tangent) t_copy = tangent.clone();
        torch::autograd::forward_ad::exit_dual_level(level);

        check(has_tangent,
              "the pressure integrator propagates a forward dual THROUGH its copy_() "
              "in-place assembly -- the graph survives the level-by-level writes");
        if (has_tangent) {
            // L is linear and homogeneous: L.v is just the operator run on v as the anomaly.
            auto Lv = P(mu_b + v_mu);
            const double rel = max_rel(t_copy, Lv);
            check(rel < 1e-12, "and J.v == L.v EXACTLY, because the operator is linear in "
                               "mu' (rel=" + sci(rel) + ")");
        } else {
            check(false, "forward tangent unavailable, so J.v == L.v cannot be compared");
        }
    }

    // --- reverse: backward must RUN, not merely be tracked ---
    // "grad-enabled" is not "differentiable". An in-place recurrence is tracked forward and
    // then errors in backward; the only way to know is to call it.
    {
        auto mu_r = (mu_b + 500.0).clone().requires_grad_(true);
        bool ran = true;
        torch::Tensor g;
        try {
            auto loss = P(mu_r).pow(2).sum();
            loss.backward();
            g = mu_r.grad();
        } catch (const std::exception&) { ran = false; }
        check(ran && g.defined() && torch::isfinite(g).all().item<bool>() &&
                  g.abs().sum().item<double>() > 0.0,
              "backward() RUNS on the pressure integrator and yields a finite non-zero "
              "gradient -- tracked-forward is not the same as differentiable");
    }

    // --- and the duality holds for the integrator too ---
    {
        auto lam = torch::randn({1, NZ, 1}, opts);
        auto mu_r = (mu_b + 500.0).clone().requires_grad_(true);
        auto grads = torch::autograd::grad({(P(mu_r) * lam).sum()}, {mu_r});
        const double reverse = (grads[0] * v_mu).sum().item<double>();
        const double forward = (P(mu_b + v_mu) * lam).sum().item<double>();
        const double rel = std::abs(forward - reverse) / std::abs(forward);
        check(rel < 1e-12, "<Lv,lambda> == <v,L^T lambda> for the pressure integrator "
                           "(rel=" + sci(rel) + ")");
    }

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "AD_TANGENT_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "AD_TANGENT_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
