// wrf_hydrostatic_pressure.h -- declarations for the base-state EOS and hydrostatic
// pressure helpers defined in wrf_hydrostatic_pressure.cpp.
//
// 9F.D49 (review section 2). These were FORWARD-DECLARED inside
// wrf_sdirk3_tile_unified_impl.cpp rather than shared through a header, which is the
// same structural weakness that let sdirk3_tile_set_base_state_zerocopy_v2's declaration
// and definition disagree until D44 added the missing include: with no single
// declaration, a signature can drift and still link.
//
// A header costs nothing here and makes the compiler the gate.

#ifndef WRF_HYDROSTATIC_PRESSURE_H
#define WRF_HYDROSTATIC_PRESSURE_H

#include <torch/torch.h>

#include <vector>

namespace wrf {
namespace sdirk3 {

// THE base-state equation of state. One implementation; everything else calls it.
//
//     alpha = (rd/p1000mb) * theta * (pressure/p1000mb)^cvpm,   cvpm = -cv/cp
//
// which is WRF's form (dyn_em/start_em.F:636) and equals R_d*theta*Pi/p exactly, since
// cvpm = R_d/cp - 1. Verified against em_b_wave/wrfinput_d01: reproduces WRF's stored
// ALB to 0.000e+00 relative error.
//
// theta must be the ABSOLUTE potential temperature (t_init + t0), NOT the perturbation.
// Passing theta - t0 is a ~300 K error; Base_EOS_Contract asserts that it is detectable.
//
// The tangent matters as much as the value in this project:
//     d(alpha)/d(theta) = alpha/theta
//     d(alpha)/d(p)     = cvpm * alpha/p
// The superseded rd*theta/p form gets d/dp wrong by a factor cp/cv = 1.4 even where the
// values are made to agree, so a forward-only check cannot detect it.
torch::Tensor compute_inverse_density(
    const torch::Tensor& theta,
    const torch::Tensor& pressure,
    float rd, float cv, float cp, float p1000mb);

// Perturbation inverse density: compute_inverse_density(theta_full, p_full) - alb.
torch::Tensor compute_inverse_density_hydrostatic(
    const torch::Tensor& t_full,
    const torch::Tensor& p_full,
    const torch::Tensor& p_base,
    const torch::Tensor& th_base,
    const torch::Tensor& alb,
    float rd, float cv, float cp, float p1000mb);

// Vector-based overload (legacy, causes CPU round-trips)
torch::Tensor compute_pressure_hydrostatic(
    const torch::Tensor& t_full,
    const torch::Tensor& mu_full,
    const torch::Tensor& mu_base,
    const torch::Tensor& p_base,
    const torch::Tensor& muts,
    const std::vector<float>& c1h,
    const std::vector<float>& c2h,
    const std::vector<float>& rdnw,
    const std::vector<float>& rdn,
    float rd, float cv, float cp, float p0, float p1000mb);

// PARITY FIX 2025-12-13: Tensor-based overload to avoid CPU round-trips in PGF
torch::Tensor compute_pressure_hydrostatic(
    const torch::Tensor& t_full,
    const torch::Tensor& mu_full,
    const torch::Tensor& mu_base,
    const torch::Tensor& p_base,
    const torch::Tensor& muts,
    const torch::Tensor& c1h,
    const torch::Tensor& c2h,
    const torch::Tensor& rdnw,
    const torch::Tensor& rdn,
    float rd, float cv, float cp, float p0, float p1000mb);

// WRF calc_p_rho (module_big_step_utilities_em.F, dry): the inverse density comes from the
// layer thickness and the pressure from the equation of state -- NOT from a hydrostatic
// integral of mu'. rdnw_abs is |1/dnw| (WRF's rdnw is negative: dnw < 0).
struct WrfPRho { torch::Tensor al_pert, alt, p_pert; };
inline WrfPRho calc_p_rho_wrf(const torch::Tensor& ph_pert,   // [ny, nz+1, nx]  phi' at w levels
                              const torch::Tensor& t_pert,    // [ny, nz, nx]    theta - t0
                              const torch::Tensor& mu_pert,   // [ny, nx]
                              const torch::Tensor& mu_base,   // [ny, nx]
                              const torch::Tensor& alb,       // [ny, nz, nx]
                              const torch::Tensor& p_base,    // [ny, nz, nx]
                              const torch::Tensor& rdnw_abs,  // [nz]
                              const torch::Tensor& c1h,       // [nz]
                              const torch::Tensor& c2h,       // [nz]
                              float rd, float cv, float cp, float p0, float t0) {
    const int64_t nz = t_pert.size(1);
    auto c1 = c1h.slice(0, 0, nz).view({1, nz, 1});
    auto c2 = c2h.slice(0, 0, nz).view({1, nz, 1});
    auto muts = (mu_base + mu_pert).unsqueeze(1);                        // WRF muts = mub + mu
    auto dph = ph_pert.slice(1, 1, nz + 1) - ph_pert.slice(1, 0, nz);    // phi'(k+1) - phi'(k)
    auto rdnw_wrf = -rdnw_abs.slice(0, 0, nz).view({1, nz, 1});          // WRF sign (dnw < 0)
    auto al_pert = -(alb * c1 * mu_pert.unsqueeze(1) + rdnw_wrf * dph) / (c1 * muts + c2);
    auto alt = al_pert + alb;
    const double cpovcv = static_cast<double>(cp) / static_cast<double>(cv);
    auto p = static_cast<double>(p0) * torch::pow(rd * (t_pert + t0) / (p0 * alt), cpovcv);
    return {al_pert, alt, p - p_base};
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_HYDROSTATIC_PRESSURE_H
