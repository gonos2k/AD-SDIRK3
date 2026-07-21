// PR 9F.9.1: pure, testable exact trust-region predicted-reduction model.
//
// The first shadow (PR 9F.9) computed this INLINE and got the COORDINATE SPACE wrong
// -- it mixed the unscaled Newton residual R with the S-scaled GMRES residual r_g and
// re-applied S^-1 to r_g. It had no unit test, and it never fired at dt=600 (GMRES
// total-failure bypasses the trust trial), so the error shipped undetected. Extracting
// the math into a pure function makes it directly testable against a hand-computed
// reference, independent of whether the production path ever reaches it.
//
// CONTRACT. Everything is in the SCALED space the outer Newton hands to GMRES:
//   the GMRES system is  A_s x = b_s  with  A_s = S^-1 A S,  b_s = -S^-1 R,  x = S^-1 dK
//   (verified: gmres_rhs = -(S_inv * R); dK = S_diag * gmres_result.x), so the returned
//   true residual  r_g = b_s - A_s x = S^-1(-R - A dK)  is ALREADY S-scaled.
// Given the scaled residual  R_s = S^-1 R  and r_g, the linear model of the new residual
// at step fraction alpha is
//   R_lin_s(alpha) = R_s + alpha * A_s x = R_s + alpha*(b_s - r_g) = (1-alpha)R_s - alpha*r_g,
// and the predicted reduction in the trust region's own norm (L2 of the halo-masked
// scaled residual -- matching res_old_val/res_new_val, which are ||S^-1 R . mask||_2) is
//   ||mask . R_s||^2 - ||mask . R_lin_s(alpha)||^2.
// The cross term 2*alpha*(1-alpha)*<R_s, r_g> that the production heuristic drops is
// included here BY CONSTRUCTION. r_g must NOT be re-scaled by S^-1 -- it is already scaled.
#pragma once

#include <torch/torch.h>
#include <cmath>    // std::isfinite
#include <limits>   // std::numeric_limits

namespace wrf {
namespace sdirk3 {

// PR 9F.9.3: a TYPED result. The previous version encoded a contract violation as NaN;
// the caller then divided by it (rho = actual / pred) with a `abs(pred) > eps` guard,
// and abs(NaN) > eps is FALSE, so the guard clamped the denominator and turned the
// violation back into a plausible finite rho -- laundering the failure. Representing the
// error in the TYPE forces the caller to branch on it explicitly.
enum class TrustPredictionError {
    None,
    UndefinedInput,
    NonOneDimInput,
    ShapeMismatch,
    DeviceMismatch,
    DtypeMismatch,
    NonFiniteInput,
    InvalidMask,
    InvalidAlpha
};
inline const char* trust_prediction_error_name(TrustPredictionError e) {
    switch (e) {
        case TrustPredictionError::None:           return "none";
        case TrustPredictionError::UndefinedInput: return "undefined_input";
        case TrustPredictionError::NonOneDimInput: return "non_1d_input";
        case TrustPredictionError::ShapeMismatch:  return "shape_mismatch";
        case TrustPredictionError::DeviceMismatch: return "device_mismatch";
        case TrustPredictionError::DtypeMismatch:  return "dtype_mismatch";
        case TrustPredictionError::NonFiniteInput: return "non_finite_input";
        case TrustPredictionError::InvalidMask:    return "invalid_mask";
        case TrustPredictionError::InvalidAlpha:   return "invalid_alpha";
    }
    return "unknown";  // unreachable; keeps -Wswitch honest AND satisfies -Wreturn-type
}
struct TrustPrediction {
    double reduction = std::numeric_limits<double>::quiet_NaN();
    TrustPredictionError error = TrustPredictionError::None;
    bool ok() const noexcept { return error == TrustPredictionError::None; }
};

// Both inputs are in the SCALED space; `mask` is optional (undefined = no masking) and,
// if defined, must EXACTLY match (1-D, same shape/device/dtype, finite, binary 0/1) --
// no implicit conversion or broadcasting, which would turn a wiring error into a
// plausible-but-wrong number. Returns TrustPrediction{reduction, error}; reduction is
// only meaningful when ok().
//
// STRICT input contract: both residuals defined, 1-D, identical shape/device/dtype and
// finite; alpha finite in [0,1]. On any violation the reduction is NaN and error names
// the cause -- the caller MUST branch on ok(), never divide by reduction blindly.
inline TrustPrediction sdirk3_trust_predicted_reduction(const torch::Tensor& R_scaled,
                                                        const torch::Tensor& r_g_scaled,
                                                        double alpha,
                                                        const torch::Tensor& mask) {
    using E = TrustPredictionError;
    torch::NoGradGuard no_grad;
    const auto fail = [](E e) { return TrustPrediction{
        std::numeric_limits<double>::quiet_NaN(), e}; };

    if (!R_scaled.defined() || !r_g_scaled.defined()) return fail(E::UndefinedInput);
    if (R_scaled.dim() != 1 || r_g_scaled.dim() != 1)  return fail(E::NonOneDimInput);
    if (R_scaled.sizes() != r_g_scaled.sizes())        return fail(E::ShapeMismatch);
    if (R_scaled.device() != r_g_scaled.device())      return fail(E::DeviceMismatch);
    if (R_scaled.scalar_type() != r_g_scaled.scalar_type()) return fail(E::DtypeMismatch);
    if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0) return fail(E::InvalidAlpha);
    if (mask.defined()) {
        // EXACT match, no auto-convert; and mask must be finite and binary {0,1}.
        if (mask.dim() != 1 || mask.sizes() != R_scaled.sizes() ||
            mask.device() != R_scaled.device() ||
            mask.scalar_type() != R_scaled.scalar_type())
            return fail(E::InvalidMask);
        // ONE sync for finite AND binary: mask*(1-mask) == 0 iff mask in {0,1}, and any
        // NaN/Inf makes the product non-zero (so a non-finite mask also fails here).
        if (!((mask * (1.0 - mask)) == 0.0).all().item<bool>())
            return fail(E::InvalidMask);
    }
    // ONE sync for both residuals' finiteness.
    if (!torch::isfinite(R_scaled).logical_and(torch::isfinite(r_g_scaled))
             .all().item<bool>())
        return fail(E::NonFiniteInput);

    const auto apply_mask = [&](const torch::Tensor& x) {
        return mask.defined() ? (x * mask) : x;
    };
    const auto R_lin_s = (1.0 - alpha) * R_scaled - alpha * r_g_scaled;
    const auto a = apply_mask(R_scaled);
    const auto b = apply_mask(R_lin_s);
    // Accumulate in FP64. The predicted reduction is A - B of two nearly-equal large
    // sums-of-squares; FP32 accumulation would lose it to cancellation exactly in the
    // small-reduction region where accept/reject is most sensitive. Diagnostic-only, so
    // the FP64 reduction (a GPU->CPU sync + a widened reduce) is an accepted cost.
    const double A = a.to(torch::kFloat64).square().sum().item<double>();
    const double B = b.to(torch::kFloat64).square().sum().item<double>();
    return TrustPrediction{A - B, E::None};
}

}  // namespace sdirk3
}  // namespace wrf
