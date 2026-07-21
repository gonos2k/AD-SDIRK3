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

namespace wrf {
namespace sdirk3 {

// Both inputs are in the SCALED space; `mask` is optional (undefined = no masking) and
// must broadcast against the residual. Returns the predicted reduction in L2^2 units.
inline double sdirk3_trust_predicted_reduction(const torch::Tensor& R_scaled,
                                               const torch::Tensor& r_g_scaled,
                                               double alpha,
                                               const torch::Tensor& mask) {
    torch::NoGradGuard no_grad;
    const auto apply_mask = [&](const torch::Tensor& x) {
        return mask.defined() ? (x * mask) : x;
    };
    const auto R_lin_s =
        (1.0 - alpha) * R_scaled - alpha * r_g_scaled;
    const auto a = apply_mask(R_scaled);
    const auto b = apply_mask(R_lin_s);
    const double na = a.norm().item<double>();
    const double nb = b.norm().item<double>();
    return na * na - nb * nb;
}

}  // namespace sdirk3
}  // namespace wrf
