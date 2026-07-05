/**
 * @file wrf_sdirk3_advection_ad.h
 * @brief AD-compatible advection schemes for WRF SDIRK3
 */

#ifndef WRF_SDIRK3_ADVECTION_AD_H
#define WRF_SDIRK3_ADVECTION_AD_H

#include <torch/torch.h>
#include <tuple>
#include <string>
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

// Scalar advection with scheme selection
torch::Tensor advectScalarAD(
    const torch::Tensor& scalar,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    const ConfigFlags& config,
    const std::string& variable_name);

// Momentum advection (staggered)
torch::Tensor advectMomentumUAD(
    const torch::Tensor& u,
    const torch::Tensor& u_base,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

torch::Tensor advectMomentumVAD(
    const torch::Tensor& v,
    const torch::Tensor& u,
    const torch::Tensor& v_base,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

torch::Tensor advectMomentumWAD(
    const torch::Tensor& w,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w_base,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Flux-form advection for conservation
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
computeFluxFormAdvectionAD(
    const torch::Tensor& phi,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& rho,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Vertical implicit advection
torch::Tensor advectVerticalImplicitAD(
    const torch::Tensor& phi,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    float dt,
    const ConfigFlags& config);

// Individual scheme implementations
torch::Tensor advect5thOrderUpwind(
    const torch::Tensor& phi,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    int stagger_type);

torch::Tensor advect6thOrderCentered(
    const torch::Tensor& phi,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid,
    int stagger_type);

torch::Tensor advectPositiveDefinite(
    const torch::Tensor& phi,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid);

torch::Tensor advectMonotonic(
    const torch::Tensor& phi,
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_ADVECTION_AD_H