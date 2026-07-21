// PR 9F.9.1: pure contract test for the exact trust-region predicted reduction.
//
// The first shadow shipped with the coordinate space wrong and NO unit test; it never
// fired at dt=600 so CI could not catch it. This test drives the pure function directly
// with synthetic tensors -- non-identity scale (already folded into the scaled inputs),
// a residual and a GMRES residual that are NOT orthogonal (so the cross term matters),
// a mask with zeros (N_active < N_total), and alpha in {0, 0.5, 1} -- and checks it
// against an INDEPENDENT element-wise reference. The decisive case proves the cross
// term 2*alpha*(1-alpha)*<R_s,r_g> is INCLUDED (the exact model), unlike the production
// heuristic that drops it.
#include "wrf_sdirk3_trust_model.h"

#include <torch/torch.h>
#include <cmath>
#include <cstdio>

static int g_fail = 0;
static int g_cases = 0;
static void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

// Independent reference: ||mask*R_s||^2 - ||mask*((1-a)R_s - a*r_g)||^2, expanded so it
// does NOT share code with the function under test.
static double ref_pred(const torch::Tensor& R_s, const torch::Tensor& r_g,
                       double a, const torch::Tensor& mask) {
    torch::NoGradGuard ng;
    auto m = [&](const torch::Tensor& x) { return mask.defined() ? x * mask : x; };
    auto a0 = m(R_s);
    auto lin = (1.0 - a) * R_s - a * r_g;
    auto a1 = m(lin);
    double n0 = a0.norm().item<double>();
    double n1 = a1.norm().item<double>();
    return n0 * n0 - n1 * n1;
}

int main() {
    torch::manual_seed(20260721);
    const int64_t N = 500;
    // Scaled residual and GMRES residual; deliberately correlated so <R_s,r_g> != 0.
    auto R_s = torch::randn({N}, torch::kFloat64);
    auto r_g = 0.7 * R_s + 0.9 * torch::randn({N}, torch::kFloat64);
    // Mask: zero out 120 entries so N_active = 380 != N.
    auto mask = torch::ones({N}, torch::kFloat64);
    mask.slice(0, 0, 120).zero_();

    const double dot = (mask * R_s).dot(mask * r_g).item<double>();
    check(std::abs(dot) > 1e-6,
          "fixture: <mask*R_s, mask*r_g> is non-zero (cross term is exercised)");

    // ---- alpha = 0 : no step, predicted reduction is exactly 0 --------------------
    {
        const double p = wrf::sdirk3::sdirk3_trust_predicted_reduction(R_s, r_g, 0.0, mask);
        check(std::abs(p) < 1e-9, "alpha=0 -> predicted reduction is 0");
    }
    // ---- alpha = 1 : ||mask R_s||^2 - ||mask r_g||^2 -----------------------------
    {
        const double p = wrf::sdirk3::sdirk3_trust_predicted_reduction(R_s, r_g, 1.0, mask);
        auto mR = mask * R_s; auto mg = mask * r_g;
        const double want = mR.norm().item<double>() * mR.norm().item<double>()
                          - mg.norm().item<double>() * mg.norm().item<double>();
        check(std::abs(p - want) < 1e-9, "alpha=1 -> ||mask R_s||^2 - ||mask r_g||^2");
    }
    // ---- alpha = 0.5 : matches the independent reference (cross term included) -----
    {
        const double p = wrf::sdirk3::sdirk3_trust_predicted_reduction(R_s, r_g, 0.5, mask);
        const double want = ref_pred(R_s, r_g, 0.5, mask);
        check(std::abs(p - want) < 1e-9, "alpha=0.5 -> matches independent reference");

        // The cross-term-DROPPING model (what the production heuristic approximates):
        //   ||mask R_s||^2 * [1 - (1-a)^2] - a^2 ||mask r_g||^2   (no 2a(1-a)<R_s,r_g>)
        auto mR = mask * R_s; auto mg = mask * r_g;
        const double a = 0.5;
        const double nR2 = mR.norm().item<double>() * mR.norm().item<double>();
        const double ng2 = mg.norm().item<double>() * mg.norm().item<double>();
        const double dropped = nR2 * (1.0 - (1.0 - a) * (1.0 - a)) - a * a * ng2;
        check(std::abs(p - dropped) > 1e-6,
              "alpha=0.5 -> exact model DIFFERS from the cross-term-dropped heuristic "
              "(the cross term is genuinely present)");
        // And the difference is exactly the cross term 2a(1-a)<mask R_s, mask r_g>.
        const double cross = 2.0 * a * (1.0 - a) * dot;
        check(std::abs((p - dropped) - cross) < 1e-9,
              "alpha=0.5 -> exact - dropped == 2a(1-a)<R_s,r_g> (the omitted cross term)");
    }
    // ---- no mask: N_active = N ---------------------------------------------------
    {
        const double p = wrf::sdirk3::sdirk3_trust_predicted_reduction(
            R_s, r_g, 0.5, torch::Tensor());
        const double want = ref_pred(R_s, r_g, 0.5, torch::Tensor());
        check(std::abs(p - want) < 1e-9, "undefined mask -> no masking, matches reference");
    }

    // ---- CALLER-COORDINATE INTEGRATION (PR 9F.9.2) -------------------------------
    // The bug in PR #58 was in the CALLER's coordinate handling, not the formula. Build
    // a small system EXACTLY as the outer Newton does -- A_s = S^-1 A S, b_s = -S^-1 R,
    // r_g = b_s - A_s x -- feed the helper the scaled inputs (R_s = S^-1 R, r_g), and
    // compare to an INDEPENDENT computation in the ORIGINAL space of the new residual's
    // linear model R + alpha*A*dK (dK = S x). A caller that forgot to scale, or scaled
    // twice, would diverge here.
    {
        const int64_t n = 40;
        auto S    = torch::rand({n}, torch::kFloat64) + 0.5;   // diagonal scale != I
        auto Sinv = S.reciprocal();
        auto A    = torch::randn({n, n}, torch::kFloat64);     // dense operator
        auto R    = torch::randn({n}, torch::kFloat64);        // unscaled residual
        auto x    = torch::randn({n}, torch::kFloat64);        // scaled step (dK_tilde)
        auto dK   = S * x;                                     // unscaled step
        // scaled system, exactly as the solver builds it
        auto R_s  = Sinv * R;
        auto A_s_x = Sinv * torch::mv(A, S * x);               // A_s x = S^-1 A (S x)
        auto b_s  = -(Sinv * R);
        auto r_g  = b_s - A_s_x;                               // GMRES true residual (scaled)
        const double a = 0.5;
        const double helper = wrf::sdirk3::sdirk3_trust_predicted_reduction(
            R_s, r_g, a, torch::Tensor());
        // Original-space reference: linear model of the new residual is R + a*A*dK, and
        // the predicted reduction in the SAME scaled L2 norm the helper uses is
        //   ||S^-1 R||^2 - ||S^-1 (R + a A dK)||^2.
        auto R_new_lin = R + a * torch::mv(A, dK);
        auto e0 = Sinv * R;
        auto e1 = Sinv * R_new_lin;
        const double ref = (e0 * e0).sum().item<double>() - (e1 * e1).sum().item<double>();
        check(std::abs(helper - ref) < 1e-9,
              "caller integration: scaled-space helper == original-space direct calc "
              "(a mis-scaled or double-scaled caller would diverge)");
    }

    // ---- CONTRACT VIOLATIONS return NaN (PR 9F.9.2) ------------------------------
    {
        auto ok = torch::randn({10}, torch::kFloat64);
        auto shape2d = torch::randn({10, 1}, torch::kFloat64);
        auto badn = torch::randn({11}, torch::kFloat64);
        check(std::isnan(wrf::sdirk3::sdirk3_trust_predicted_reduction(
                  ok, badn, 0.5, torch::Tensor())),
              "contract: mismatched numel -> NaN");
        check(std::isnan(wrf::sdirk3::sdirk3_trust_predicted_reduction(
                  ok, shape2d.reshape({10}).unsqueeze(1), 0.5, torch::Tensor())),
              "contract: non-1-D input -> NaN");
        check(std::isnan(wrf::sdirk3::sdirk3_trust_predicted_reduction(
                  ok, ok, 1.5, torch::Tensor())),
              "contract: alpha out of [0,1] -> NaN");
        auto nonfinite = ok.clone(); nonfinite[0] = std::numeric_limits<double>::infinity();
        check(std::isnan(wrf::sdirk3::sdirk3_trust_predicted_reduction(
                  ok, nonfinite, 0.5, torch::Tensor())),
              "contract: non-finite input -> NaN");
    }

    const int kExpected = 12;
    if (g_cases != kExpected) {
        std::printf("FAIL: case-count %d expected %d\n", g_cases, kExpected);
        ++g_fail;
    }
    if (g_fail == 0) { std::printf("Trust model contract: ALL PASS (%d/%d)\n", g_cases, kExpected); return 0; }
    std::printf("Trust model contract: %d FAILURE(S)\n", g_fail);
    return 1;
}
