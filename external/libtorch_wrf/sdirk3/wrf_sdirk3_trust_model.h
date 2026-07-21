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
//
// PR 9F.9.4: EmptyInput (a zero-length packed residual is a wiring/layout failure, not a
// valid reduction=0) and NonFiniteResult (finite FP64 inputs can still overflow the
// squared sum to Inf/NaN -- ok() must not promise a finite reduction the math didn't
// deliver) complete the contract.
// PR 9F.9.5: EmptyActiveDomain (a mask that zeros EVERY cell -- a non-empty packed residual
// with no active DOF is a halo/layout wiring failure, not a valid reduction=0) and
// UnsupportedDtype (the math assumes a real FP32/FP64 residual; int/bool/complex would be
// silently mis-promoted) close the last two ways a wiring error could pass as a number.
enum class TrustPredictionError {
    None,
    UndefinedInput,
    EmptyInput,
    EmptyActiveDomain,
    NonOneDimInput,
    ShapeMismatch,
    DeviceMismatch,
    DtypeMismatch,
    UnsupportedDtype,
    NonFiniteInput,
    InvalidMask,
    InvalidAlpha,
    NonFiniteResult
};
inline const char* trust_prediction_error_name(TrustPredictionError e) {
    switch (e) {
        case TrustPredictionError::None:              return "none";
        case TrustPredictionError::UndefinedInput:    return "undefined_input";
        case TrustPredictionError::EmptyInput:        return "empty_input";
        case TrustPredictionError::EmptyActiveDomain: return "empty_active_domain";
        case TrustPredictionError::NonOneDimInput:    return "non_1d_input";
        case TrustPredictionError::ShapeMismatch:     return "shape_mismatch";
        case TrustPredictionError::DeviceMismatch:    return "device_mismatch";
        case TrustPredictionError::DtypeMismatch:     return "dtype_mismatch";
        case TrustPredictionError::UnsupportedDtype:  return "unsupported_dtype";
        case TrustPredictionError::NonFiniteInput:    return "non_finite_input";
        case TrustPredictionError::InvalidMask:       return "invalid_mask";
        case TrustPredictionError::InvalidAlpha:      return "invalid_alpha";
        case TrustPredictionError::NonFiniteResult:   return "non_finite_result";
    }
    return "unknown";  // unreachable; keeps -Wswitch honest AND satisfies -Wreturn-type
}

// PR 9F.9.4: a result type that CANNOT represent an invalid state. The previous struct had
// public fields and a default of {reduction=NaN, error=None}, so `TrustPrediction p;` gave
// ok()==true with a NaN reduction -- exactly the "failure is representable" flaw the typed
// result was meant to close. No default constructor, no public fields.
//
// PR 9F.9.5: the factories themselves now enforce the invariant `ok() => finite reduction`
// and `!ok() => a real error`. Previously `success(NaN)` / `success(Inf)` built an ok()
// object carrying a non-finite value, and `failure(None)` built an ok() object with no
// value -- both re-opened the very hole the type was meant to close. Now success() demotes a
// non-finite value to failure(NonFiniteResult), and failure(None) is remapped to
// UndefinedInput, so no factory call can produce ok() with a bad reduction.
class TrustPrediction {
public:
    static TrustPrediction success(double reduction) {
        if (!std::isfinite(reduction))
            return TrustPrediction(std::numeric_limits<double>::quiet_NaN(),
                                   TrustPredictionError::NonFiniteResult);
        return TrustPrediction(reduction, TrustPredictionError::None);
    }
    static TrustPrediction failure(TrustPredictionError error) {
        // None is the success discriminant; failure(None) is a caller bug -- collapse it to a
        // defined error so a failure() call can NEVER yield ok().
        if (error == TrustPredictionError::None)
            error = TrustPredictionError::UndefinedInput;
        return TrustPrediction(std::numeric_limits<double>::quiet_NaN(), error);
    }
    bool ok() const noexcept { return error_ == TrustPredictionError::None; }
    // Meaningful only when ok(); NaN otherwise (a failure() carries no reduction).
    double reduction() const noexcept { return reduction_; }
    TrustPredictionError error() const noexcept { return error_; }

private:
    TrustPrediction(double reduction, TrustPredictionError error)
        : reduction_(reduction), error_(error) {}
    double reduction_;
    TrustPredictionError error_;
};

// PR 9F.9.4: the SINGLE FP64 merit authority. Both the predicted reduction (A, B below) and
// the caller's ACTUAL reduction go through this one function, so actual and predicted are
// computed in the SAME norm at the SAME precision -- previously the actual reduction was
// res_old_val^2 - res_new_val^2 with res_* materialized as FP32, so rho_exact mixed an FP32
// numerator with an FP64 denominator. Masked (halo) then squared-summed in FP64. Inputs MUST
// already be validated compatible (the predicted helper validates; the caller shares the
// production halo mask); this is an unchecked kernel by design. NoGradGuard: diagnostic-only.
inline double sdirk3_scaled_merit_sq(const torch::Tensor& residual_scaled,
                                     const torch::Tensor& mask) {
    torch::NoGradGuard no_grad;
    // Mask via torch::where, NOT `residual_scaled * mask`: masked-out (halo) cells can hold
    // NaN/Inf garbage, and NaN*0 == NaN in IEEE-754 would poison the whole sum even when the
    // active domain is clean. `where` REPLACES those cells with 0, so the halo cannot leak
    // into the reduction. For a finite halo this is identical to x*mask (binary mask), so it
    // stays consistent with the active-domain case.
    const auto x = mask.defined()
        ? torch::where(mask.to(torch::kBool), residual_scaled,
                       torch::zeros_like(residual_scaled))
        : residual_scaled;
    return x.to(torch::kFloat64).square().sum().item<double>();
}

// Both inputs are in the SCALED space; `mask` is optional (undefined = no masking) and,
// if defined, must EXACTLY match (1-D, same shape/device/dtype, finite, binary 0/1) --
// no implicit conversion or broadcasting, which would turn a wiring error into a
// plausible-but-wrong number. Returns TrustPrediction::success(reduction) or ::failure(e);
// reduction is only meaningful when ok().
//
// STRICT input contract: both residuals defined, NON-EMPTY, 1-D, identical shape/device/
// dtype and finite; alpha finite in [0,1]. The RESULT is also checked finite (finite inputs
// can overflow the squared sum). On any violation error() names the cause -- the caller MUST
// branch on ok(), never read reduction() blindly.
inline TrustPrediction sdirk3_trust_predicted_reduction(const torch::Tensor& R_scaled,
                                                        const torch::Tensor& r_g_scaled,
                                                        double alpha,
                                                        const torch::Tensor& mask) {
    using E = TrustPredictionError;
    torch::NoGradGuard no_grad;

    if (!R_scaled.defined() || !r_g_scaled.defined())
        return TrustPrediction::failure(E::UndefinedInput);
    if (R_scaled.numel() == 0 || r_g_scaled.numel() == 0)
        return TrustPrediction::failure(E::EmptyInput);
    if (R_scaled.dim() != 1 || r_g_scaled.dim() != 1)
        return TrustPrediction::failure(E::NonOneDimInput);
    if (R_scaled.sizes() != r_g_scaled.sizes())
        return TrustPrediction::failure(E::ShapeMismatch);
    if (R_scaled.device() != r_g_scaled.device())
        return TrustPrediction::failure(E::DeviceMismatch);
    if (R_scaled.scalar_type() != r_g_scaled.scalar_type())
        return TrustPrediction::failure(E::DtypeMismatch);
    // The math assumes a REAL residual; int/bool/complex would be silently mis-promoted by
    // the arithmetic and the to(kFloat64) cast. Restrict to the FP types WRF actually uses.
    if (R_scaled.scalar_type() != torch::kFloat32 &&
        R_scaled.scalar_type() != torch::kFloat64)
        return TrustPrediction::failure(E::UnsupportedDtype);
    if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0)
        return TrustPrediction::failure(E::InvalidAlpha);
    if (mask.defined()) {
        // EXACT match, no auto-convert; and mask must be finite and binary {0,1}.
        if (mask.dim() != 1 || mask.sizes() != R_scaled.sizes() ||
            mask.device() != R_scaled.device() ||
            mask.scalar_type() != R_scaled.scalar_type())
            return TrustPrediction::failure(E::InvalidMask);
        // ONE sync for finite AND binary: mask*(1-mask) == 0 iff mask in {0,1}, and any
        // NaN/Inf makes the product non-zero (so a non-finite mask also fails here).
        if (!((mask * (1.0 - mask)) == 0.0).all().item<bool>())
            return TrustPrediction::failure(E::InvalidMask);
        // A mask that zeros EVERY cell leaves no active DOF: a non-empty residual with an
        // empty active domain is a halo/layout wiring failure, NOT a valid reduction=0.
        if (!mask.to(torch::kBool).any().item<bool>())
            return TrustPrediction::failure(E::EmptyActiveDomain);
    }
    // Exclude masked-out (halo) cells BEFORE the finiteness check and any reduction. The
    // mask defines the valid domain; halo cells can hold NaN/Inf garbage, so validating
    // finiteness over the FULL tensor would spuriously fail on cells the computation does
    // not use. torch::where replaces them with 0 (NaN*0==NaN rules out plain multiply). For
    // a finite halo this is identical to the unmasked tensor on the active domain.
    torch::Tensor R_active = R_scaled;
    torch::Tensor r_g_active = r_g_scaled;
    if (mask.defined()) {
        const auto mb = mask.to(torch::kBool);
        R_active   = torch::where(mb, R_scaled,   torch::zeros_like(R_scaled));
        r_g_active = torch::where(mb, r_g_scaled, torch::zeros_like(r_g_scaled));
    }
    // ONE sync for both residuals' finiteness, on the ACTIVE domain only.
    if (!torch::isfinite(R_active).logical_and(torch::isfinite(r_g_active))
             .all().item<bool>())
        return TrustPrediction::failure(E::NonFiniteInput);

    const auto R_lin_s = (1.0 - alpha) * R_active - alpha * r_g_active;
    // Accumulate in FP64 through the shared merit authority. The predicted reduction is
    // A - B of two nearly-equal large sums-of-squares; FP32 accumulation would lose it to
    // cancellation exactly in the small-reduction region where accept/reject is most
    // sensitive. Diagnostic-only, so the FP64 reduction (a GPU->CPU sync + a widened
    // reduce) is an accepted cost. R_active/R_lin_s already have the halo zeroed, so merit
    // needs no further mask.
    const double A = sdirk3_scaled_merit_sq(R_active, torch::Tensor());
    const double B = sdirk3_scaled_merit_sq(R_lin_s, torch::Tensor());
    // Finite inputs can still overflow the squared sum to Inf, or A-B to NaN (Inf-Inf).
    // ok() must never promise a finite reduction the arithmetic did not produce.
    if (!std::isfinite(A) || !std::isfinite(B) || !std::isfinite(A - B))
        return TrustPrediction::failure(E::NonFiniteResult);
    return TrustPrediction::success(A - B);
}

}  // namespace sdirk3
}  // namespace wrf
