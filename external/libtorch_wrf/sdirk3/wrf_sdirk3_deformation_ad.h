/**
 * @file wrf_sdirk3_deformation_ad.h
 * @brief AD-compatible deformation and turbulence calculations for WRF SDIRK3
 */

#ifndef WRF_SDIRK3_DEFORMATION_AD_H
#define WRF_SDIRK3_DEFORMATION_AD_H

#include <torch/torch.h>
#include <tuple>
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

// Forward declarations
class MapScaleFactors;
void computeShearStrains(torch::Tensor& D12, torch::Tensor& D13, torch::Tensor& D23,
                        const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
                        const WRFGridInfo& grid, const MapScaleFactors& msf,
                        float rdx, float rdy);

// Core deformation calculations
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor, 
           torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor>
computeDeformationTensorWRFAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
computeVorticityAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

torch::Tensor computeDeformationMagnitudeAD(
    const torch::Tensor& D11,
    const torch::Tensor& D22,
    const torch::Tensor& D33,
    const torch::Tensor& D12,
    const torch::Tensor& D13,
    const torch::Tensor& D23,
    const WRFGridInfo& grid);

// Turbulence closure
torch::Tensor computeMixingLengthAD(
    const torch::Tensor& z_at_w,
    const torch::Tensor& theta,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
computeStressTendencyAD(
    const torch::Tensor& D11,
    const torch::Tensor& D22,
    const torch::Tensor& D33,
    const torch::Tensor& D12,
    const torch::Tensor& D13,
    const torch::Tensor& D23,
    const torch::Tensor& xkmh,
    const torch::Tensor& xkmv,
    const torch::Tensor& rho,
    const WRFGridInfo& grid);

// Complete physics integration
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
computeCompleteDeformationPhysicsAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& theta,
    const torch::Tensor& rho,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Diagnostic calculations
torch::Tensor computeHorizontalDeformationDivergenceAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const WRFGridInfo& grid);

torch::Tensor computeRichardsonNumberAD(
    const torch::Tensor& theta,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const WRFGridInfo& grid);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_DEFORMATION_AD_H