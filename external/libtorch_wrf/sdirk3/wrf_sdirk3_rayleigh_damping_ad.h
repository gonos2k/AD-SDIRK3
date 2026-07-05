/**
 * @file wrf_sdirk3_rayleigh_damping_ad.h
 * @brief AD-compatible Rayleigh damping for WRF SDIRK3
 */

#ifndef WRF_SDIRK3_RAYLEIGH_DAMPING_AD_H
#define WRF_SDIRK3_RAYLEIGH_DAMPING_AD_H

#include <torch/torch.h>
#include <tuple>
#include <string>
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

// Structure to hold all damping tendencies
struct RayleighDampingTendencies {
    torch::Tensor u_tend;
    torch::Tensor v_tend;
    torch::Tensor w_tend;
    torch::Tensor theta_tend;
    torch::Tensor qv_tend;
    torch::Tensor qc_tend;
    torch::Tensor qr_tend;
};

// Compute damping coefficient profile
// use_max_height: for mountainous terrain, use max height instead of mean
torch::Tensor computeRayleighDampingCoefficient(
    const torch::Tensor& z_at_w,
    float zdamp,
    float dampcoef,
    const WRFGridInfo& grid,
    bool use_max_height = false);

// Apply damping to momentum
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
applyRayleighDampingMomentumAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& u_base,
    const torch::Tensor& v_base,
    const torch::Tensor& gamma_m,
    const torch::Tensor& gamma_w,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Apply damping to scalars
torch::Tensor applyRayleighDampingScalarAD(
    const torch::Tensor& scalar,
    const torch::Tensor& scalar_base,
    const torch::Tensor& gamma_m,
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Apply absorbing layer
// use_max_height: for mountainous terrain, use max height instead of mean
torch::Tensor applyAbsorbingLayerAD(
    const torch::Tensor& w,
    const torch::Tensor& z_at_w,
    float zdamp,
    float dampcoef,
    const WRFGridInfo& grid,
    const ConfigFlags& config,
    bool use_max_height = false);

// Complete Rayleigh damping
RayleighDampingTendencies computeCompleteRayleighDampingAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& qc,
    const torch::Tensor& qr,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Implicit damping for strong damping
torch::Tensor applyImplicitRayleighDampingAD(
    const torch::Tensor& field,
    const torch::Tensor& field_base,
    const torch::Tensor& gamma,
    float dt,
    const WRFGridInfo& grid);

// ============================================================================
// Cache Invalidation API
// ============================================================================
// FIX 2025-12-26: Call when grid metrics (z_at_w) are updated in-place.
// This increments the epoch counter to invalidate cached 1D z profiles.
// Typical call sites: after grid initialization, after regridding, after restart.

/**
 * Invalidate the Z1D profile cache used by Rayleigh damping functions.
 * Call this when z_at_w or other vertical metrics are modified in-place.
 */
void invalidateZ1DCache();

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_RAYLEIGH_DAMPING_AD_H