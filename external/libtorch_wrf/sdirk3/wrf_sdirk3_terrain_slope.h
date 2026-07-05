/**
 * @file wrf_sdirk3_terrain_slope.h
 * @brief Terrain slope corrections for pressure gradient force
 * 
 * This module implements terrain slope terms (sin_alpha, cos_alpha) that are
 * essential for accurate pressure gradient force calculations on steep terrain.
 * These terms modify the horizontal pressure gradient to account for the
 * non-orthogonality between the terrain-following coordinate surfaces and
 * the geopotential surfaces.
 */

#ifndef WRF_SDIRK3_TERRAIN_SLOPE_H
#define WRF_SDIRK3_TERRAIN_SLOPE_H

#include <torch/torch.h>
#include <memory>

namespace wrf {
namespace sdirk3 {

/**
 * @class TerrainSlope
 * @brief Manages terrain slope calculations and corrections
 * 
 * In WRF's terrain-following coordinate system, the pressure gradient force
 * needs to be corrected for the slope of the terrain. This is done using
 * the terrain slope angles alpha, where:
 * - sin_alpha = vertical component of terrain slope
 * - cos_alpha = horizontal component of terrain slope
 */
class TerrainSlope {
private:
    // Grid dimensions
    int nx_, ny_, nz_;
    
    // Terrain slope arrays (2D fields)
    torch::Tensor sin_alpha_x_;  // sin(alpha) in x-direction at mass points
    torch::Tensor sin_alpha_y_;  // sin(alpha) in y-direction at mass points
    torch::Tensor cos_alpha_x_;  // cos(alpha) in x-direction at mass points
    torch::Tensor cos_alpha_y_;  // cos(alpha) in y-direction at mass points
    
    // Interpolated values at staggered points
    torch::Tensor sin_alpha_x_u_;  // at u-points
    torch::Tensor sin_alpha_y_v_;  // at v-points
    torch::Tensor cos_alpha_x_u_;  // at u-points
    torch::Tensor cos_alpha_y_v_;  // at v-points
    
    // Configuration
    bool terrain_slope_enabled_;
    float max_terrain_slope_;  // Maximum allowed terrain slope (radians)

    // FIX Round189: Cached max sin(alpha) to avoid repeated GPU sync in getMaxSlope()
    float cached_max_sin_alpha_ = 0.0f;
    
public:
    /**
     * Constructor
     * @param nx, ny, nz Grid dimensions
     */
    TerrainSlope(int nx, int ny, int nz);
    
    /**
     * Initialize terrain slope arrays from WRF data
     * @param sin_alpha_x Sin of terrain slope angle in x-direction
     * @param sin_alpha_y Sin of terrain slope angle in y-direction
     * @param cos_alpha_x Cos of terrain slope angle in x-direction
     * @param cos_alpha_y Cos of terrain slope angle in y-direction
     */
    void initialize(const float* sin_alpha_x, const float* sin_alpha_y,
                   const float* cos_alpha_x, const float* cos_alpha_y);
    
    /**
     * Compute terrain slope from terrain height
     * @param terrain_height 2D array of terrain elevation
     * @param dx, dy Grid spacing in x and y directions
     */
    void computeFromTerrain(const torch::Tensor& terrain_height, 
                           float dx, float dy);
    
    /**
     * Apply terrain slope correction to U-momentum pressure gradient
     * @param pgf_x Horizontal pressure gradient force (to be modified)
     * @param pgf_z Vertical pressure gradient force component
     * @param msfu Map scale factor at u-points
     */
    void correctPressureGradientU(torch::Tensor& pgf_x,
                                 const torch::Tensor& pgf_z,
                                 const torch::Tensor& msfu);
    
    /**
     * Apply terrain slope correction to V-momentum pressure gradient
     * @param pgf_y Horizontal pressure gradient force (to be modified)
     * @param pgf_z Vertical pressure gradient force component
     * @param msfv Map scale factor at v-points
     */
    void correctPressureGradientV(torch::Tensor& pgf_y,
                                 const torch::Tensor& pgf_z,
                                 const torch::Tensor& msfv);
    
    /**
     * Apply terrain slope correction to W-momentum equation
     * @param w_tend W-momentum tendency (to be modified)
     * @param pgf_x, pgf_y Horizontal pressure gradients
     * @param msft Map scale factor at mass points
     */
    void correctVerticalMomentum(torch::Tensor& w_tend,
                               const torch::Tensor& pgf_x,
                               const torch::Tensor& pgf_y,
                               const torch::Tensor& msft);
    
    /**
     * Get terrain slope diagnostics
     */
    float getMaxSlope() const;
    torch::Tensor getSlopeAngle() const;  // Returns slope angle in degrees
    
    /**
     * Check if terrain slope corrections are enabled
     */
    bool isEnabled() const { return terrain_slope_enabled_; }
    
    /**
     * Enable/disable terrain slope corrections
     */
    void setEnabled(bool enabled) { terrain_slope_enabled_ = enabled; }
    
private:
    /**
     * Interpolate terrain slope to staggered grids
     */
    void interpolateToStaggered();
    
    /**
     * Limit terrain slope to maximum allowed value
     */
    void limitSlope();
    
    /**
     * Compute slope angle from sin and cos components
     */
    torch::Tensor computeSlopeAngle(const torch::Tensor& sin_alpha,
                                   const torch::Tensor& cos_alpha);
};

/**
 * @brief Apply full terrain slope correction to pressure gradient forces
 * 
 * This function modifies the pressure gradient forces to account for
 * terrain slope following WRF's formulation:
 * 
 * For U-momentum: pgf_x = -cos(alpha_x) * dp/dx + sin(alpha_x) * dp/dz
 * For V-momentum: pgf_y = -cos(alpha_y) * dp/dy + sin(alpha_y) * dp/dz
 * For W-momentum: Additional terms from coordinate transformation
 * 
 * @param pgf_u U-momentum pressure gradient (modified in place)
 * @param pgf_v V-momentum pressure gradient (modified in place)
 * @param pgf_w W-momentum pressure gradient (modified in place)
 * @param dp_dx Pressure gradient in x-direction
 * @param dp_dy Pressure gradient in y-direction
 * @param dp_dz Pressure gradient in z-direction
 * @param terrain_slope TerrainSlope object with slope data
 */
void applyTerrainSlopeCorrection(torch::Tensor& pgf_u,
                                torch::Tensor& pgf_v,
                                torch::Tensor& pgf_w,
                                const torch::Tensor& dp_dx,
                                const torch::Tensor& dp_dy,
                                const torch::Tensor& dp_dz,
                                const TerrainSlope& terrain_slope);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_TERRAIN_SLOPE_H