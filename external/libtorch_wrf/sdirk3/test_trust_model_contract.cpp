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
#include <limits>
#include <type_traits>

using wrf::sdirk3::TrustPredictionError;
using wrf::sdirk3::TrustPrediction;

// PR 9F.9.4 (P1-2): the invalid state {reduction=NaN, error=None} is no longer
// REPRESENTABLE -- there is no default constructor, so `TrustPrediction p;` (ok()==true
// with a NaN reduction) cannot even be written. This is a compile-time guarantee, not a
// runtime check.
static_assert(!std::is_default_constructible<TrustPrediction>::value,
              "TrustPrediction must not be default-constructible (no representable "
              "invalid state)");

static int g_fail = 0;
static int g_cases = 0;
static void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

// Unwrap a valid prediction to its reduction (NaN if not ok -- the value cases below all
// expect ok()).
static double pval(const torch::Tensor& R, const torch::Tensor& g, double a,
                   const torch::Tensor& m) {
    const auto p = wrf::sdirk3::sdirk3_trust_predicted_reduction(R, g, a, m);
    return p.ok() ? p.reduction() : std::numeric_limits<double>::quiet_NaN();
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
        const double p = pval(R_s, r_g, 0.0, mask);
        check(std::abs(p) < 1e-9, "alpha=0 -> predicted reduction is 0");
    }
    // ---- alpha = 1 : ||mask R_s||^2 - ||mask r_g||^2 -----------------------------
    {
        const double p = pval(R_s, r_g, 1.0, mask);
        auto mR = mask * R_s; auto mg = mask * r_g;
        const double want = mR.norm().item<double>() * mR.norm().item<double>()
                          - mg.norm().item<double>() * mg.norm().item<double>();
        check(std::abs(p - want) < 1e-9, "alpha=1 -> ||mask R_s||^2 - ||mask r_g||^2");
    }
    // ---- alpha = 0.5 : matches the independent reference (cross term included) -----
    {
        const double p = pval(R_s, r_g, 0.5, mask);
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
        const double p = pval(
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
        const double helper = pval(R_s, r_g, a, torch::Tensor());
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

    // ---- CONTRACT VIOLATIONS -> TYPED error (PR 9F.9.3) --------------------------
    // The error is now in the TYPE, not encoded as NaN (which the caller's `abs(x)>eps`
    // guard silently laundered back into a finite rho). Each case asserts the exact
    // TrustPredictionError, and that the result is not ok().
    {
        using wrf::sdirk3::sdirk3_trust_predicted_reduction;
        auto ok = torch::randn({10}, torch::kFloat64);
        auto badn = torch::randn({11}, torch::kFloat64);
        auto p_numel = sdirk3_trust_predicted_reduction(ok, badn, 0.5, torch::Tensor());
        check(!p_numel.ok() && p_numel.error() == TrustPredictionError::ShapeMismatch,
              "contract: mismatched numel -> ShapeMismatch (not ok)");
        auto p_2d = sdirk3_trust_predicted_reduction(
            ok, torch::randn({10, 1}, torch::kFloat64), 0.5, torch::Tensor());
        check(!p_2d.ok() && p_2d.error() == TrustPredictionError::NonOneDimInput,
              "contract: non-1-D input -> NonOneDimInput");
        auto p_alpha = sdirk3_trust_predicted_reduction(ok, ok, 1.5, torch::Tensor());
        check(!p_alpha.ok() && p_alpha.error() == TrustPredictionError::InvalidAlpha,
              "contract: alpha out of [0,1] -> InvalidAlpha");
        auto nonfinite = ok.clone();
        nonfinite[0] = std::numeric_limits<double>::infinity();
        auto p_nf = sdirk3_trust_predicted_reduction(ok, nonfinite, 0.5, torch::Tensor());
        check(!p_nf.ok() && p_nf.error() == TrustPredictionError::NonFiniteInput,
              "contract: non-finite input -> NonFiniteInput");
        // dtype mismatch
        auto p_dt = sdirk3_trust_predicted_reduction(
            ok, torch::randn({10}, torch::kFloat32), 0.5, torch::Tensor());
        check(!p_dt.ok() && p_dt.error() == TrustPredictionError::DtypeMismatch,
              "contract: dtype mismatch -> DtypeMismatch");
        // NON-BINARY mask is rejected (a weight != {0,1} is not a halo mask)
        auto nonbinary = torch::ones({10}, torch::kFloat64);
        nonbinary[3] = 2.0;
        auto p_mask = sdirk3_trust_predicted_reduction(ok, ok, 0.5, nonbinary);
        check(!p_mask.ok() && p_mask.error() == TrustPredictionError::InvalidMask,
              "contract: non-binary mask -> InvalidMask");
        // a BINARY mask is accepted
        auto binmask = torch::ones({10}, torch::kFloat64);
        binmask.slice(0, 0, 4).zero_();
        check(sdirk3_trust_predicted_reduction(ok, ok, 0.5, binmask).ok(),
              "contract: binary 0/1 mask is accepted");

        // PR 9F.9.4 (P1-4): a zero-length residual is a wiring/layout failure, NOT a
        // valid reduction=0. Two empty 1-D tensors -> EmptyInput, not success.
        auto empty = torch::empty({0}, torch::kFloat64);
        auto p_empty = sdirk3_trust_predicted_reduction(empty, empty, 0.5, torch::Tensor());
        check(!p_empty.ok() && p_empty.error() == TrustPredictionError::EmptyInput,
              "contract: empty residual -> EmptyInput (not a valid reduction=0)");

        // PR 9F.9.4 (P1-3): FINITE FP64 inputs whose squared sum OVERFLOWS to Inf must
        // fail as NonFiniteResult -- ok() must not promise a finite reduction the
        // arithmetic did not produce. 1e200^2 = 1e400 = +Inf in FP64.
        auto huge = torch::full({4}, 1.0e200, torch::kFloat64);
        check(std::isfinite(huge[0].item<double>()),
              "fixture: 1e200 is a FINITE input (the overflow is in the SQUARED sum)");
        auto p_ovf = sdirk3_trust_predicted_reduction(huge, huge, 1.0, torch::Tensor());
        check(!p_ovf.ok() && p_ovf.error() == TrustPredictionError::NonFiniteResult,
              "contract: finite inputs, overflowing squared sum -> NonFiniteResult");
    }

    // ---- TYPED RESULT: only success()/failure() build one (PR 9F.9.4 P1-2) -----------
    // Complements the compile-time static_assert (no default ctor) with the runtime
    // invariant: success() is ok() and carries the value; failure() is !ok() and names
    // the error. There is no third state.
    {
        const auto s = TrustPrediction::success(1.5);
        check(s.ok() && s.reduction() == 1.5,
              "typed result: success(1.5) -> ok() with reduction()==1.5");
        const auto f = TrustPrediction::failure(TrustPredictionError::EmptyInput);
        check(!f.ok() && f.error() == TrustPredictionError::EmptyInput,
              "typed result: failure(EmptyInput) -> !ok() with error()==EmptyInput");
    }

    // ---- SHARED MERIT AUTHORITY (PR 9F.9.4 P1-1) -------------------------------------
    // sdirk3_scaled_merit_sq is the ONE FP64 metric both predicted and actual go through.
    // Verify it equals a hand-computed masked FP64 sum-of-squares.
    {
        using wrf::sdirk3::sdirk3_scaled_merit_sq;
        auto v    = torch::randn({64}, torch::kFloat64);
        auto m    = torch::ones({64}, torch::kFloat64);
        m.slice(0, 0, 20).zero_();
        const double got  = sdirk3_scaled_merit_sq(v, m);
        const double want = (v * m).square().sum().item<double>();
        check(std::abs(got - want) < 1e-12,
              "merit authority: masked FP64 sum-of-squares matches hand calc");
    }

    // ---- HALO NaN/Inf is EXCLUDED by the mask (Gemini PR #62) -------------------------
    // Masked-out (halo) cells can hold NaN/Inf garbage. Because the merit and the finiteness
    // check mask via torch::where (not x*mask, where NaN*0==NaN), a non-finite HALO cell must
    // NOT fail the contract or poison the reduction -- only the ACTIVE domain matters. A
    // non-finite cell in the ACTIVE domain must still fail as NonFiniteInput.
    {
        using wrf::sdirk3::sdirk3_trust_predicted_reduction;
        using wrf::sdirk3::sdirk3_scaled_merit_sq;
        const int64_t n = 32;
        auto R    = torch::randn({n}, torch::kFloat64);
        auto g    = torch::randn({n}, torch::kFloat64);
        auto mask = torch::ones({n}, torch::kFloat64);
        mask.slice(0, 0, 8).zero_();                       // first 8 = halo
        // Poison the HALO of both residuals with NaN and Inf.
        R[0] = std::numeric_limits<double>::quiet_NaN();
        g[1] = std::numeric_limits<double>::infinity();
        auto p_halo = sdirk3_trust_predicted_reduction(R, g, 0.5, mask);
        check(p_halo.ok() && std::isfinite(p_halo.reduction()),
              "halo NaN/Inf is EXCLUDED: contract ok() and reduction() is finite");
        // Value equals the reduction computed on a CLEAN residual (halo zeroed by hand).
        auto Rc = R.clone(); auto gc = g.clone();
        Rc.slice(0, 0, 8).zero_(); gc.slice(0, 0, 8).zero_();
        auto want_p = sdirk3_trust_predicted_reduction(Rc, gc, 0.5, mask);
        check(want_p.ok() &&
              std::abs(p_halo.reduction() - want_p.reduction()) < 1e-12,
              "halo NaN/Inf: reduction matches the halo-zeroed residual (no leak)");
        // The merit authority alone also excludes the halo NaN.
        check(std::isfinite(sdirk3_scaled_merit_sq(R, mask)),
              "merit authority: halo NaN excluded (finite result)");
        // But a non-finite cell in the ACTIVE domain still fails.
        auto R_act_bad = torch::randn({n}, torch::kFloat64);
        R_act_bad[20] = std::numeric_limits<double>::quiet_NaN();   // active cell
        auto p_bad = sdirk3_trust_predicted_reduction(R_act_bad, g, 0.5, mask);
        check(!p_bad.ok() && p_bad.error() == TrustPredictionError::NonFiniteInput,
              "active-domain NaN still fails -> NonFiniteInput");
    }

    // ---- FP32 cancellation: FP64 accumulation preserves a tiny reduction ----------
    // A - B of two nearly-equal LARGE sums-of-squares. In FP32 the accumulation loses
    // the small true reduction; the helper accumulates in FP64 and must match an FP64
    // reference. This is exactly the small-reduction region where accept/reject matters.
    {
        const int64_t n = 4000;
        // big common part + tiny distinct part so ||R_s||^2 ~ ||R_lin||^2 >> reduction.
        auto big = 1.0e3 * torch::ones({n}, torch::kFloat64);
        auto R_s64 = big + 1.0e-2 * torch::randn({n}, torch::kFloat64);
        // choose r_g so that at alpha=1 the reduction is small: r_g ~ R_s + tiny
        auto r_g64 = R_s64 + 1.0e-1 * torch::randn({n}, torch::kFloat64);
        auto R_s32 = R_s64.to(torch::kFloat32);
        auto r_g32 = r_g64.to(torch::kFloat32);
        const double a = 1.0;
        const double got = pval(R_s32, r_g32, a, torch::Tensor());
        // FP64 reference from the FP32 VALUES (same inputs, exact accumulation).
        auto R_lin = (1.0 - a) * R_s32.to(torch::kFloat64) - a * r_g32.to(torch::kFloat64);
        auto e0 = R_s32.to(torch::kFloat64);
        const double ref = (e0 * e0).sum().item<double>()
                         - (R_lin * R_lin).sum().item<double>();
        // Tolerance relative to the reduction magnitude; a naive FP32 accumulation would
        // be off by O(||R||^2 * eps_fp32) ~ 1e3^2*4000*1e-7 ~ 1e5, far above this bound.
        check(std::abs(got - ref) < 1e-3 * (std::abs(ref) + 1.0),
              "FP32 inputs: FP64-accumulated reduction matches the FP64 reference "
              "(no cancellation loss)");
    }

    const int kExpected = 26;
    if (g_cases != kExpected) {
        std::printf("FAIL: case-count %d expected %d\n", g_cases, kExpected);
        ++g_fail;
    }
    if (g_fail == 0) { std::printf("Trust model contract: ALL PASS (%d/%d)\n", g_cases, kExpected); return 0; }
    std::printf("Trust model contract: %d FAILURE(S)\n", g_fail);
    return 1;
}
