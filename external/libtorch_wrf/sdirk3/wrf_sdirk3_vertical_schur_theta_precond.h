#ifndef WRF_SDIRK3_VERTICAL_SCHUR_THETA_PRECOND_H
#define WRF_SDIRK3_VERTICAL_SCHUR_THETA_PRECOND_H

#include <torch/torch.h>

#include <cmath>

#include "wrf_sdirk3_acoustic_coeff_builder.h"

namespace wrf {
namespace sdirk3 {

// Fast-state channel indices in packed tensor [nfast, nz_w, ny, nx]
struct FastChannelsWPhiTheta {
    int w = -1;
    int phi = -1;
    int theta = -1;
};

// Experimental P1 preconditioner:
// - W/phi Schur-TDMA backbone
// - theta contribution from WRF (3.6)-style term on RHS
// - apply_transpose() provided for adjoint use
class VerticalSchurThetaPreconditioner {
public:
    VerticalSchurThetaPreconditioner(FastChannelsWPhiTheta channels,
                                     torch::Tensor inv_deta_m_1d,
                                     torch::Tensor inv_deta_w_1d)
        : channels_(channels),
          inv_deta_m_(std::move(inv_deta_m_1d)),
          inv_deta_w_(std::move(inv_deta_w_1d)) {
        TORCH_CHECK(inv_deta_m_.dim() == 1 && inv_deta_w_.dim() == 1,
                    "inv_deta arrays must be 1D");
    }

    void update_coeffs(const AcousticLinearCoeffs& coeffs) {
        C_m_ = coeffs.C_m;
        D_m_ = coeffs.D_m;
        inv_theta_m_ = coeffs.inv_theta_m;
        S_w_ = coeffs.S_w;
        T_w_ = coeffs.T_w;

        TORCH_CHECK(C_m_.dim() == 3 && D_m_.dim() == 3 && inv_theta_m_.dim() == 3,
                    "mass coeff tensors must be [nz_m,ny,nx]");
        TORCH_CHECK(S_w_.dim() == 3 && T_w_.dim() == 3,
                    "w coeff tensors must be [nz,ny,nx]");

        const int64_t nz_m = C_m_.size(0);
        const int64_t nz = nz_m - 1;
        TORCH_CHECK(inv_deta_m_.size(0) == nz_m, "inv_deta_m length mismatch");
        TORCH_CHECK(inv_deta_w_.size(0) == nz, "inv_deta_w length mismatch");

        auto inv_dw = inv_deta_w_.view({nz, 1, 1});
        auto inv_dm0 = inv_deta_m_.narrow(0, 0, nz).view({nz, 1, 1});
        auto inv_dm1 = inv_deta_m_.narrow(0, 1, nz).view({nz, 1, 1});

        auto C0 = C_m_.narrow(0, 0, nz);
        auto C1 = C_m_.narrow(0, 1, nz);

        aL_ = C0 * inv_dw * inv_dm0;
        cL_ = C1 * inv_dw * inv_dm1;
        bL_ = -(aL_ + cL_);

        cp_ = torch::empty_like(aL_);
        dp_ = torch::empty_like(aL_);
        xphi_ = torch::empty_like(aL_);
    }

    void set_alpha(double alpha) { alpha_ = alpha; }

    torch::Tensor apply(const torch::Tensor& r_fast) {
        TORCH_CHECK(r_fast.dim() == 4, "r_fast must be [nfast,nz_w,ny,nx]");
        if (std::abs(alpha_) < 1e-20) {
            return r_fast;
        }

        auto out = r_fast.clone();
        auto rW_full = r_fast.select(0, channels_.w);
        auto rPhi_full = r_fast.select(0, channels_.phi);
        auto rTh_full = r_fast.select(0, channels_.theta);

        const int64_t nz_w = rW_full.size(0);
        const int64_t nz_m = nz_w - 1;
        const int64_t nz = nz_w - 2;
        TORCH_CHECK(C_m_.size(0) == nz_m, "coeff mismatch: C_m nz_m");
        TORCH_CHECK(aL_.size(0) == nz, "coeff mismatch: aL nz");

        auto rW = rW_full.narrow(0, 1, nz);
        auto rPhi = rPhi_full.narrow(0, 1, nz);
        auto rTh_m = rTh_full.narrow(0, 1, nz_m);

        auto q_m = D_m_ * (rTh_m * inv_theta_m_);
        auto G_theta = diff_mass_to_w(q_m, inv_deta_w_);

        const double beta = alpha_ * alpha_;
        auto K = S_w_ * T_w_;
        auto a = -beta * K * aL_;
        auto b = 1.0 - beta * K * bL_;
        auto c = -beta * K * cL_;
        auto rhs = (alpha_ * T_w_) * rW + rPhi + (beta * K) * G_theta;

        a.select(0, 0).zero_();
        c.select(0, nz - 1).zero_();

        auto dPhi = solve_tdma_(a, b, c, rhs);
        auto denom = (alpha_ * T_w_).clamp_min(1e-12);
        auto dW = (dPhi - rPhi) / denom;

        out.select(0, channels_.w).narrow(0, 1, nz).copy_(dW);
        out.select(0, channels_.phi).narrow(0, 1, nz).copy_(dPhi);
        // theta remains identity preconditioned in apply()
        return out;
    }

    torch::Tensor apply_transpose(const torch::Tensor& r_fast) {
        TORCH_CHECK(r_fast.dim() == 4, "r_fast must be [nfast,nz_w,ny,nx]");
        if (std::abs(alpha_) < 1e-20) {
            return r_fast;
        }

        auto out = r_fast.clone();
        auto rW_full = r_fast.select(0, channels_.w);
        auto rPhi_full = r_fast.select(0, channels_.phi);
        auto rTh_full = r_fast.select(0, channels_.theta);

        const int64_t nz_w = rW_full.size(0);
        const int64_t nz_m = nz_w - 1;
        const int64_t nz = nz_w - 2;

        auto rW = rW_full.narrow(0, 1, nz);
        auto rPhi = rPhi_full.narrow(0, 1, nz);
        auto rTh_m = rTh_full.narrow(0, 1, nz_m);

        auto alphaT = alpha_ * T_w_;
        auto q = -alpha_ * S_w_ * bL_;
        auto s = -alpha_ * S_w_ * cL_;
        auto t = -alpha_ * S_w_ * aL_;

        auto rW_prev = torch::zeros_like(rW);
        auto rW_next = torch::zeros_like(rW);
        rW_prev.narrow(0, 1, nz - 1).copy_(rW.narrow(0, 0, nz - 1));
        rW_next.narrow(0, 0, nz - 1).copy_(rW.narrow(0, 1, nz - 1));

        auto s_prev = torch::zeros_like(s);
        auto t_next = torch::zeros_like(t);
        s_prev.narrow(0, 1, nz - 1).copy_(s.narrow(0, 0, nz - 1));
        t_next.narrow(0, 0, nz - 1).copy_(t.narrow(0, 1, nz - 1));

        auto alphaT_prev = torch::zeros_like(alphaT);
        auto alphaT_next = torch::zeros_like(alphaT);
        alphaT_prev.narrow(0, 1, nz - 1).copy_(alphaT.narrow(0, 0, nz - 1));
        alphaT_next.narrow(0, 0, nz - 1).copy_(alphaT.narrow(0, 1, nz - 1));

        auto aT = s_prev * alphaT_prev;
        auto bT = 1.0 + q * alphaT;
        auto cT = t_next * alphaT_next;
        auto rhsT = rPhi - q * rW - s_prev * rW_prev - t_next * rW_next;

        aT.select(0, 0).zero_();
        cT.select(0, nz - 1).zero_();

        auto zPhi = solve_tdma_(aT, bT, cT, rhsT);
        auto zW = rW + alphaT * zPhi;

        out.select(0, channels_.w).narrow(0, 1, nz).copy_(zW);
        out.select(0, channels_.phi).narrow(0, 1, nz).copy_(zPhi);

        // theta transpose coupling from the RHS theta-term
        auto SzW = S_w_ * zW;
        auto mass_div = diff_mass_to_w_transpose(SzW, inv_deta_w_);
        auto zTh_m = rTh_m + alpha_ * (D_m_ * inv_theta_m_) * mass_div;

        out.select(0, channels_.theta).narrow(0, 1, nz_m).copy_(zTh_m);
        out.select(0, channels_.theta).select(0, 0).zero_();
        return out;
    }

private:
    torch::Tensor solve_tdma_(const torch::Tensor& a,
                              const torch::Tensor& b,
                              const torch::Tensor& c,
                              const torch::Tensor& d) {
        const int64_t nz = a.size(0);
        auto denom0 = b.select(0, 0);
        cp_.select(0, 0).copy_(c.select(0, 0) / denom0);
        dp_.select(0, 0).copy_(d.select(0, 0) / denom0);

        for (int64_t k = 1; k < nz; ++k) {
            auto denom = b.select(0, k) - a.select(0, k) * cp_.select(0, k - 1);
            cp_.select(0, k).copy_(c.select(0, k) / denom);
            dp_.select(0, k).copy_((d.select(0, k) - a.select(0, k) * dp_.select(0, k - 1)) / denom);
        }

        xphi_.select(0, nz - 1).copy_(dp_.select(0, nz - 1));
        for (int64_t k = nz - 2; k >= 0; --k) {
            xphi_.select(0, k).copy_(dp_.select(0, k) - cp_.select(0, k) * xphi_.select(0, k + 1));
            if (k == 0) {
                break;
            }
        }
        return xphi_;
    }

    FastChannelsWPhiTheta channels_;
    torch::Tensor inv_deta_m_;
    torch::Tensor inv_deta_w_;

    torch::Tensor C_m_;
    torch::Tensor D_m_;
    torch::Tensor inv_theta_m_;
    torch::Tensor S_w_;
    torch::Tensor T_w_;

    torch::Tensor aL_;
    torch::Tensor bL_;
    torch::Tensor cL_;

    torch::Tensor cp_;
    torch::Tensor dp_;
    torch::Tensor xphi_;

    double alpha_ = 0.0;
};

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_VERTICAL_SCHUR_THETA_PRECOND_H
