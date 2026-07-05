/**
 * @file wrf_sdirk3_pressure_gradient_ad.h
 * @brief AD-compatible pressure gradient and buoyancy for WRF SDIRK3
 */

#ifndef WRF_SDIRK3_PRESSURE_GRADIENT_AD_H
#define WRF_SDIRK3_PRESSURE_GRADIENT_AD_H

#include <torch/torch.h>
#include <tuple>
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

// Pressure from equation of state
torch::Tensor computePressureAD(
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& mu,
    const torch::Tensor& mu_base,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Vertical pressure gradient and buoyancy
torch::Tensor computePressureGradientWAD(
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& qc,
    const torch::Tensor& qr,
    const torch::Tensor& p,
    const torch::Tensor& p_base,
    const torch::Tensor& theta_base,
    const torch::Tensor& mu,
    const torch::Tensor& mu_base,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Horizontal pressure gradients
torch::Tensor computePressureGradientUAD(
    const torch::Tensor& p,
    const torch::Tensor& p_base,
    const torch::Tensor& mu,
    const torch::Tensor& mu_base,
    const torch::Tensor& phi,
    const torch::Tensor& alpha,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

torch::Tensor computePressureGradientVAD(
    const torch::Tensor& p,
    const torch::Tensor& p_base,
    const torch::Tensor& mu,
    const torch::Tensor& mu_base,
    const torch::Tensor& phi,
    const torch::Tensor& alpha,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Geopotential from hydrostatic balance
torch::Tensor computeGeopotentialAD(
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& p,
    const torch::Tensor& p_base,
    const torch::Tensor& phb,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Specific volume (inverse density)
torch::Tensor computeSpecificVolumeAD(
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& p,
    const torch::Tensor& p_base,
    const ConfigFlags& config);

// Complete pressure gradient forcing
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
computeCompletePressureGradientAD(
    const torch::Tensor& theta,
    const torch::Tensor& qv,
    const torch::Tensor& qc,
    const torch::Tensor& qr,
    const torch::Tensor& p,
    const torch::Tensor& mu,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_PRESSURE_GRADIENT_AD_H