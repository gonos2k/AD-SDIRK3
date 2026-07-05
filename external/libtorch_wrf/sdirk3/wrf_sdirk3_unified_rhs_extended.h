/**
 * @file wrf_sdirk3_unified_rhs_extended.h
 * @brief Extended WRFGridInfo structure with missing dynamics components
 * 
 * This header extends the WRFGridInfo structure to include terrain slope
 * terms and other missing dynamics components for full WRF compatibility.
 */

#ifndef WRF_SDIRK3_UNIFIED_RHS_EXTENDED_H
#define WRF_SDIRK3_UNIFIED_RHS_EXTENDED_H

#include <torch/torch.h>
#include "wrf_sdirk3_types.h"

namespace wrf {
namespace sdirk3 {

/**
 * @struct WRFGridInfoExtended
 * @brief Extended grid information including terrain slope and advanced dynamics
 * 
 * This structure extends the base WRFGridInfo to include all dynamics components
 * needed for full WRF compatibility (excluding physics parameterizations).
 */
struct WRFGridInfoExtended : public WRFGridInfo {
    // ===== TERRAIN SLOPE TERMS =====
    // These are essential for accurate pressure gradient force on steep terrain
    // FIX 2025-12-28: Changed from raw pointers to torch::Tensor values to prevent memory leaks
    // torch::Tensor has .defined() check for null semantics (no allocation if uninitialized)
    torch::Tensor sin_alpha_x;   // sin of terrain slope angle in x-direction (2D)
    torch::Tensor sin_alpha_y;   // sin of terrain slope angle in y-direction (2D)
    torch::Tensor cos_alpha_x;   // cos of terrain slope angle in x-direction (2D)
    torch::Tensor cos_alpha_y;   // cos of terrain slope angle in y-direction (2D)
    
    // Terrain slope at staggered points (computed internally)
    torch::Tensor sin_alpha_x_u;  // at u-points (ny, nx+1)
    torch::Tensor sin_alpha_y_v;  // at v-points (ny+1, nx)
    torch::Tensor cos_alpha_x_u;  // at u-points
    torch::Tensor cos_alpha_y_v;  // at v-points
    
    // ===== DIVERGENCE DAMPING CONFIGURATION =====
    int div_damp_opt = 0;         // 0=off, 1=constant, 2=proportional
    float div_damp_coef = 0.1f;   // Base divergence damping coefficient
    float div_damp_depth = 5000.0f; // Depth for height-dependent damping
    
    // ===== 6TH ORDER DIFFUSION =====
    int diff_6th_opt = 0;         // 0=off, 1=constant, 2=variable
    float diff_6th_factor = 0.12f; // 6th order diffusion coefficient
    int diff_6th_slopeopt = 0;    // 0=off, 1=slope damping on
    float diff_6th_thresh = 0.10f; // Slope threshold for damping (default from WRF)
    
    // ===== SMAGORINSKY TURBULENCE =====
    int smagorinsky_opt = 0;      // 0=off, 1=constant, 2=3D
    float c_s = 0.25f;            // Smagorinsky constant
    
    // ===== EXTERNAL MODE FILTER =====
    // Note: damp_mu already exists in base class
    float emdamp = 0.01f;         // External mode damping coefficient
    
    // ===== ADDITIONAL NUMERICAL OPTIONS =====
    bool momentum_adv_opt = 1;    // Momentum advection option
    bool moist_adv_opt = 1;       // Moisture advection option
    bool scalar_adv_opt = 1;      // Scalar advection option
    bool tke_adv_opt = 1;         // TKE advection option
    
    // ===== NON-HYDROSTATIC PRESSURE GRADIENT =====
    // FIX 2025-12-28: Changed from raw pointers to torch::Tensor values to prevent memory leaks
    torch::Tensor dpn_u;          // Non-hydrostatic pressure gradient at u-points
    torch::Tensor dpn_v;          // Non-hydrostatic pressure gradient at v-points
    torch::Tensor dpn_w;          // Non-hydrostatic pressure gradient at w-points

    // ===== GEOPOTENTIAL FOR SLOPE DAMPING =====
    // FIX 2025-12-28: Changed from raw pointer to torch::Tensor value
    torch::Tensor phb;            // Base state geopotential (3D, w-staggered)
                                  // Required for slope damping calculation
    
    // ===== HELPER FUNCTIONS =====
    
    /**
     * Initialize terrain slope arrays from WRF data
     */
    void initializeTerrainSlope(const float* sin_ax, const float* sin_ay,
                               const float* cos_ax, const float* cos_ay,
                               int nx, int ny);
    
    /**
     * Interpolate terrain slope to staggered grids
     */
    void interpolateTerrainSlope();
    
    /**
     * Check if terrain slope correction is needed
     * FIX 2025-12-28: Updated to use .defined() check for value-type tensors
     */
    bool hasTerrainSlope() const {
        return sin_alpha_x.defined() && sin_alpha_y.defined() &&
               cos_alpha_x.defined() && cos_alpha_y.defined();
    }
    
    /**
     * Check if divergence damping is enabled
     */
    bool hasDivergenceDamping() const {
        return div_damp_opt > 0 && div_damp_coef > 0.0f;
    }
    
    /**
     * Check if 6th order diffusion is enabled
     */
    bool has6thOrderDiffusion() const {
        return diff_6th_opt > 0 && diff_6th_factor > 0.0f;
    }
    
    /**
     * Check if Smagorinsky turbulence is enabled
     */
    bool hasSmagorinsky() const {
        return smagorinsky_opt > 0 && c_s > 0.0f;
    }
};

/**
 * @brief Apply terrain slope correction to pressure gradient force
 * 
 * Following WRF's pg_buoy_w formulation in module_big_step_utilities_em.F
 * 
 * @param pgf_x X-component of pressure gradient force (modified in place)
 * @param pgf_y Y-component of pressure gradient force (modified in place)
 * @param pgf_z Z-component of pressure gradient force
 * @param grid Extended grid information with terrain slope
 */
void applyTerrainSlopeCorrectionWRF(torch::Tensor& pgf_x,
                                   torch::Tensor& pgf_y,
                                   const torch::Tensor& pgf_z,
                                   const WRFGridInfoExtended& grid);

/**
 * @brief Compute divergence damping following WRF implementation
 *
 * Based on module_diffusion_em.F divergence damping implementation
 *
 * @param u_tend U-momentum tendency (modified in place)
 * @param v_tend V-momentum tendency (modified in place)
 * @param u U-velocity
 * @param v V-velocity
 * @param mu Dry air mass
 * @param grid Extended grid information
 *
 * @note PERFORMANCE: This function converts grid.msf* tensors via .to() each call.
 *       For repeated calls (non-moving-nest scenarios), consider:
 *       1. Using DivergenceDamping class as a solver member for cached conversions
 *       2. Pre-converting grid tensors once before the time loop
 *       See wrf_sdirk3_divergence_damping.h for the cached DivergenceDamping class.
 */
void computeDivergenceDampingWRF(torch::Tensor& u_tend,
                                torch::Tensor& v_tend,
                                const torch::Tensor& u,
                                const torch::Tensor& v,
                                const torch::Tensor& mu,
                                const WRFGridInfoExtended& grid);

/**
 * @brief Compute 6th order horizontal diffusion following WRF
 * 
 * Based on module_diffusion_em.F 6th order diffusion implementation
 * 
 * @param tend Variable tendency (modified in place)
 * @param var Variable to diffuse
 * @param grid Extended grid information
 * @param is_momentum True for momentum, false for scalars
 */
void compute6thOrderDiffusionWRF(torch::Tensor& tend,
                               const torch::Tensor& var,
                               const WRFGridInfoExtended& grid,
                               bool is_momentum = false);

/**
 * @brief Compute deformation tensors for Smagorinsky turbulence
 * 
 * Following WRF's cal_deform_and_div in module_diffusion_em.F
 * 
 * @param u U-velocity (staggered)
 * @param v V-velocity (staggered) 
 * @param w W-velocity (staggered)
 * @param defor11 D11 deformation tensor (output)
 * @param defor22 D22 deformation tensor (output)
 * @param defor33 D33 deformation tensor (output)
 * @param defor12 D12 deformation tensor (output)
 * @param defor13 D13 deformation tensor (output)
 * @param defor23 D23 deformation tensor (output)
 * @param div Divergence (output)
 * @param grid Extended grid information
 */
void computeDeformationTensorsWRF(const torch::Tensor& u,
                                const torch::Tensor& v,
                                const torch::Tensor& w,
                                torch::Tensor& defor11,
                                torch::Tensor& defor22,
                                torch::Tensor& defor33,
                                torch::Tensor& defor12,
                                torch::Tensor& defor13,
                                torch::Tensor& defor23,
                                torch::Tensor& div,
                                const WRFGridInfoExtended& grid);

/**
 * @brief Compute Smagorinsky mixing coefficients
 * 
 * Following WRF's smag_km in module_diffusion_em.F
 * 
 * @param xkmh Horizontal eddy viscosity (output)
 * @param xkmv Vertical eddy viscosity (output)
 * @param xkhh Horizontal eddy diffusivity (output)
 * @param xkhv Vertical eddy diffusivity (output)
 * @param defor11 D11 deformation tensor
 * @param defor22 D22 deformation tensor
 * @param defor33 D33 deformation tensor
 * @param defor12 D12 deformation tensor
 * @param defor13 D13 deformation tensor
 * @param defor23 D23 deformation tensor
 * @param BN2 Brunt-Vaisala frequency squared (optional, can be empty)
 * @param grid Extended grid information
 */
void computeSmagorinskyCoefficientsWRF(torch::Tensor& xkmh,
                                     torch::Tensor& xkmv,
                                     torch::Tensor& xkhh,
                                     torch::Tensor& xkhv,
                                     const torch::Tensor& defor11,
                                     const torch::Tensor& defor22,
                                     const torch::Tensor& defor33,
                                     const torch::Tensor& defor12,
                                     const torch::Tensor& defor13,
                                     const torch::Tensor& defor23,
                                     const torch::Tensor& BN2,
                                     const WRFGridInfoExtended& grid);

/**
 * @brief Apply Smagorinsky turbulence to momentum equations
 * 
 * Following WRF's horizontal_diffusion_u/v/w_2 in module_diffusion_em.F
 * 
 * @param u_tend U-momentum tendency (modified in place)
 * @param v_tend V-momentum tendency (modified in place)
 * @param w_tend W-momentum tendency (modified in place)
 * @param u U-velocity
 * @param v V-velocity
 * @param w W-velocity
 * @param xkmh Horizontal eddy viscosity
 * @param xkmv Vertical eddy viscosity
 * @param defor11 D11 deformation tensor
 * @param defor22 D22 deformation tensor
 * @param defor33 D33 deformation tensor
 * @param defor12 D12 deformation tensor
 * @param defor13 D13 deformation tensor
 * @param defor23 D23 deformation tensor
 * @param rho Density
 * @param grid Extended grid information
 */
void applySmagorinskyTurbulenceWRF(torch::Tensor& u_tend,
                                 torch::Tensor& v_tend,
                                 torch::Tensor& w_tend,
                                 const torch::Tensor& u,
                                 const torch::Tensor& v,
                                 const torch::Tensor& w,
                                 const torch::Tensor& xkmh,
                                 const torch::Tensor& xkmv,
                                 const torch::Tensor& defor11,
                                 const torch::Tensor& defor22,
                                 const torch::Tensor& defor33,
                                 const torch::Tensor& defor12,
                                 const torch::Tensor& defor13,
                                 const torch::Tensor& defor23,
                                 const torch::Tensor& rho,
                                 const WRFGridInfoExtended& grid);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_UNIFIED_RHS_EXTENDED_H