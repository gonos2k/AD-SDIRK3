#ifndef WRF_SDIRK3_ACOUSTIC_COEFF_BUILDER_H
#define WRF_SDIRK3_ACOUSTIC_COEFF_BUILDER_H

#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

// Reference diagnostics frozen at stage reference state t*.
struct AcousticRefDiagFields {
    // mass-level fields: [nz_m, ny, nx]
    torch::Tensor mu_m;       // mu*
    torch::Tensor alpha_d_m;  // alpha_d*
    torch::Tensor cs2_m;      // c_s^2
    torch::Tensor theta_m;    // theta*

    // map factor at mass points: [ny, nx]
    torch::Tensor my2d;

    double g = 9.80665;
};

struct AcousticLinearCoeffs {
    // mass-level fields: [nz_m, ny, nx]
    torch::Tensor C_m;          // cs2 / (mu * alpha_d^2)
    torch::Tensor D_m;          // cs2 / alpha_d
    torch::Tensor inv_theta_m;  // 1 / theta*

    // interior w-level fields: [nz, ny, nx], nz = nz_m - 1
    torch::Tensor S_w;
    torch::Tensor T_w;
};

// Build inverse eta spacings from eta grids.
// eta_w_1d: [nz_w], eta_m_1d: [nz_m], with nz_m = nz_w - 1.
inline void make_inv_deta_from_eta(const torch::Tensor& eta_w_1d,
                                   const torch::Tensor& eta_m_1d,
                                   torch::Tensor& inv_deta_m_out,
                                   torch::Tensor& inv_deta_w_out) {
    TORCH_CHECK(eta_w_1d.dim() == 1 && eta_m_1d.dim() == 1, "eta arrays must be 1D");
    const int64_t nz_w = eta_w_1d.size(0);
    const int64_t nz_m = eta_m_1d.size(0);
    TORCH_CHECK(nz_m == nz_w - 1, "expected nz_m = nz_w - 1");

    inv_deta_m_out = 1.0 / (eta_w_1d.narrow(0, 0, nz_m) - eta_w_1d.narrow(0, 1, nz_m));
    const int64_t nz = nz_m - 1;
    inv_deta_w_out = 1.0 / (eta_m_1d.narrow(0, 0, nz) - eta_m_1d.narrow(0, 1, nz));
}

// Mass -> interior-w eta derivative:
// q_m: [nz_m, ny, nx] -> out: [nz_m-1, ny, nx]
inline torch::Tensor diff_mass_to_w(const torch::Tensor& q_m,
                                    const torch::Tensor& inv_deta_w_1d) {
    TORCH_CHECK(q_m.dim() == 3, "q_m must be [nz_m,ny,nx]");
    TORCH_CHECK(inv_deta_w_1d.dim() == 1, "inv_deta_w_1d must be 1D");
    const int64_t nz_m = q_m.size(0);
    const int64_t nz = nz_m - 1;
    TORCH_CHECK(inv_deta_w_1d.size(0) == nz, "inv_deta_w length mismatch");

    auto inv_dw = inv_deta_w_1d.view({nz, 1, 1});
    return (q_m.narrow(0, 0, nz) - q_m.narrow(0, 1, nz)) * inv_dw;
}

// Transpose of diff_mass_to_w: interior-w -> mass.
// r_w: [nz, ny, nx] -> out: [nz+1, ny, nx]
inline torch::Tensor diff_mass_to_w_transpose(const torch::Tensor& r_w,
                                              const torch::Tensor& inv_deta_w_1d) {
    TORCH_CHECK(r_w.dim() == 3, "r_w must be [nz,ny,nx]");
    TORCH_CHECK(inv_deta_w_1d.dim() == 1, "inv_deta_w_1d must be 1D");
    const int64_t nz = r_w.size(0);
    TORCH_CHECK(inv_deta_w_1d.size(0) == nz, "inv_deta_w length mismatch");

    auto out = torch::zeros({nz + 1, r_w.size(1), r_w.size(2)}, r_w.options());
    auto inv_dw = inv_deta_w_1d.view({nz, 1, 1});

    out.select(0, 0).add_(inv_dw.select(0, 0) * r_w.select(0, 0));
    for (int64_t k = 1; k < nz; ++k) {
        out.select(0, k).add_(inv_dw.select(0, k) * r_w.select(0, k) -
                              inv_dw.select(0, k - 1) * r_w.select(0, k - 1));
    }
    out.select(0, nz).add_(-inv_dw.select(0, nz - 1) * r_w.select(0, nz - 1));
    return out;
}

// Approximate coefficient builder following WRF acoustic linearization structure.
// S_w, T_w should be replaced by exact discrete coefficients from WRF Eq. (3.27)-(3.28)
// when full parity is required.
inline AcousticLinearCoeffs build_acoustic_coeffs_approx(const AcousticRefDiagFields& ref) {
    TORCH_CHECK(ref.mu_m.dim() == 3, "mu_m must be [nz_m,ny,nx]");
    TORCH_CHECK(ref.alpha_d_m.dim() == 3, "alpha_d_m must be [nz_m,ny,nx]");
    TORCH_CHECK(ref.cs2_m.dim() == 3, "cs2_m must be [nz_m,ny,nx]");
    TORCH_CHECK(ref.theta_m.dim() == 3, "theta_m must be [nz_m,ny,nx]");
    TORCH_CHECK(ref.my2d.dim() == 2, "my2d must be [ny,nx]");

    const int64_t nz_m = ref.mu_m.size(0);
    const int64_t nz = nz_m - 1;

    AcousticLinearCoeffs c;
    c.C_m = ref.cs2_m / (ref.mu_m * ref.alpha_d_m * ref.alpha_d_m);
    c.D_m = ref.cs2_m / ref.alpha_d_m;
    c.inv_theta_m = 1.0 / ref.theta_m;

    auto mu_w = 0.5 * (ref.mu_m.narrow(0, 0, nz) + ref.mu_m.narrow(0, 1, nz));
    auto my_w = ref.my2d.unsqueeze(0).expand({nz, ref.my2d.size(0), ref.my2d.size(1)});

    c.S_w = ref.g / my_w;
    c.T_w = (ref.g * my_w) / mu_w;
    return c;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_ACOUSTIC_COEFF_BUILDER_H
