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

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_HYDROSTATIC_PRESSURE_H
