/**
 * @file wrf_sdirk3_boundary_ad.h
 * @brief AD-compatible boundary conditions for WRF SDIRK3
 */

#ifndef WRF_SDIRK3_BOUNDARY_AD_H
#define WRF_SDIRK3_BOUNDARY_AD_H

#include <torch/torch.h>
#include <string>
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

// Core boundary operations
void applyPeriodicBoundaryAD(
    torch::Tensor& field,
    int stagger,
    const ConfigFlags& config);

void applySymmetricBoundaryAD(
    torch::Tensor& field,
    int stagger,
    const ConfigFlags& config);

void applyOpenBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    float phase_speed,
    float dt,
    const WRFGridInfo& grid,
    int stagger,
    const ConfigFlags& config);

void applySpecifiedBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& bdy_values,
    int relax_zone,
    const ConfigFlags& config);

void applyNestedBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& parent_field,
    const torch::Tensor& child_field,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Unified boundary application
void applyBoundaryConditions3DAD(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    int stagger,
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Specialized applications
void applyMomentumBoundaryConditionsAD(
    torch::Tensor& u,
    torch::Tensor& v,
    torch::Tensor& w,
    const torch::Tensor& u_old,
    const torch::Tensor& v_old,
    const torch::Tensor& w_old,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

void applyScalarBoundaryConditionsAD(
    torch::Tensor& scalar,
    const torch::Tensor& scalar_old,
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Auxiliary functions
void applyRelaxationZoneAD(
    torch::Tensor& field,
    const torch::Tensor& target,
    int relax_zone,
    const ConfigFlags& config);

void applyPolarFilterAD(
    torch::Tensor& field,
    const WRFGridInfo& grid,
    const ConfigFlags& config);

// Utilities

// CFL check - SCALAR version (breaks AD graph, use for diagnostics)
float checkBoundaryCFLAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid);

// CFL check - TENSOR version (preserves AD graph)
torch::Tensor checkBoundaryCFLTensor(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid);

std::string getBoundaryTypeString(const ConfigFlags& config);

bool validateBoundaryConfiguration(const ConfigFlags& config);

// ============================================================================
// Cache Invalidation API
// ============================================================================
// FIX 2025-12-26: Call when grid metrics (dz) are updated in-place.
// This increments the epoch counter to invalidate cached dz_min values.
// Typical call sites: after grid initialization, after regridding, after restart.

/**
 * Invalidate the DzMin cache used by CFL checking functions.
 * Call this when dz or other vertical metrics are modified in-place.
 */
void invalidateDzMinCache();

/**
 * Invalidate the LatCpu cache used by polar filtering and boundary operations.
 * Call this when xlat or other latitude data is modified in-place.
 * FIX 2025-12-26: Added for explicit cache management.
 */
void invalidateLatCpuCache();

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_BOUNDARY_AD_H