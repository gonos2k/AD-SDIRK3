#ifndef WRF_STAGGERED_GRID_H
#define WRF_STAGGERED_GRID_H

#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

/**
 * @brief WRF Arakawa C-grid staggered grid implementation
 * 
 * Variable locations:
 * - Mass points (i,j,k): theta, p, rho, mu
 * - U-points (i+1/2,j,k): u velocity 
 * - V-points (i,j+1/2,k): v velocity
 * - W-points (i,j,k+1/2): w velocity, geopotential
 * 
 * Array dimensions:
 * - Mass: (nz, ny, nx)
 * - U-staggered: (nz, ny, nx+1)
 * - V-staggered: (nz, ny+1, nx)
 * - W-staggered: (nz+1, ny, nx)
 */
class StaggeredGrid {
public:
    // ========================================================================
    // Interpolation from mass points to staggered points
    // ========================================================================
    
    /**
     * Interpolate from mass points to U-staggered points
     * f(i,j,k) -> f_u(i+1/2,j,k)
     * f_u[i] = 0.5 * (f[i-1] + f[i])
     */
    static torch::Tensor mass_to_u(const torch::Tensor& f);
    
    /**
     * Interpolate from mass points to V-staggered points
     * f(i,j,k) -> f_v(i,j+1/2,k)
     * f_v[j] = 0.5 * (f[j-1] + f[j])
     */
    static torch::Tensor mass_to_v(const torch::Tensor& f);
    
    /**
     * Interpolate from mass points to W-staggered points
     * f(i,j,k) -> f_w(i,j,k+1/2)
     * f_w[k] = 0.5 * (f[k-1] + f[k])
     */
    static torch::Tensor mass_to_w(const torch::Tensor& f);
    
    // ========================================================================
    // Interpolation from staggered points to mass points
    // ========================================================================
    
    /**
     * Average from U-staggered points to mass points
     * f_u(i+1/2,j,k) -> f(i,j,k)
     * f[i] = 0.5 * (f_u[i] + f_u[i+1])
     */
    static torch::Tensor u_to_mass(const torch::Tensor& f_u);
    
    /**
     * Average from V-staggered points to mass points
     * f_v(i,j+1/2,k) -> f(i,j,k)
     * f[j] = 0.5 * (f_v[j] + f_v[j+1])
     */
    static torch::Tensor v_to_mass(const torch::Tensor& f_v);
    
    /**
     * Average from W-staggered points to mass points
     * f_w(i,j,k+1/2) -> f(i,j,k)
     * f[k] = 0.5 * (f_w[k] + f_w[k+1])
     */
    static torch::Tensor w_to_mass(const torch::Tensor& f_w);
    
    // ========================================================================
    // Cross-staggering interpolations
    // ========================================================================
    
    /**
     * Interpolate from U-points to V-points
     * Requires averaging in both X and Y directions
     */
    static torch::Tensor u_to_v(const torch::Tensor& f_u);
    
    /**
     * Interpolate from V-points to U-points
     * Requires averaging in both X and Y directions
     */
    static torch::Tensor v_to_u(const torch::Tensor& f_v);
    
    /**
     * Interpolate from U-points to W-points
     * Requires averaging in both X and Z directions
     */
    static torch::Tensor u_to_w(const torch::Tensor& f_u);
    
    /**
     * Interpolate from V-points to W-points
     * Requires averaging in both Y and Z directions
     */
    static torch::Tensor v_to_w(const torch::Tensor& f_v);
    
    // ========================================================================
    // WRF-specific flux computations
    // ========================================================================
    
    /**
     * Compute flux at U-points for U-momentum advection
     * WRF: flux = 0.25 * (ru[i+1] + ru[i]) * (u[i+1] + u[i])
     */
    static torch::Tensor compute_u_flux_x(const torch::Tensor& u, 
                                          const torch::Tensor& ru);
    
    /**
     * Compute flux at V-points for V-momentum advection
     * WRF: flux = 0.25 * (rv[j+1] + rv[j]) * (v[j+1] + v[j])
     */
    static torch::Tensor compute_v_flux_y(const torch::Tensor& v, 
                                          const torch::Tensor& rv);
    
    /**
     * Compute flux for scalar advection
     * WRF: flux = 0.5 * vel * (field[i] + field[i-1])
     */
    static torch::Tensor compute_scalar_flux(const torch::Tensor& field,
                                            const torch::Tensor& vel,
                                            int dim);
    
    // ========================================================================
    // Coupled momentum handling
    // ========================================================================
    
    /**
     * Compute coupled momentum ru = mu * u at U-points
     * mu needs to be interpolated from mass points to U-points
     */
    static torch::Tensor compute_ru(const torch::Tensor& u, 
                                   const torch::Tensor& mu);
    
    /**
     * Compute coupled momentum rv = mu * v at V-points
     * mu needs to be interpolated from mass points to V-points
     */
    static torch::Tensor compute_rv(const torch::Tensor& v, 
                                   const torch::Tensor& mu);
    
    /**
     * Compute coupled momentum rw = mu * w at W-points
     * mu needs to be interpolated from mass points to W-points
     */
    static torch::Tensor compute_rw(const torch::Tensor& w, 
                                   const torch::Tensor& mu);
    
    // ========================================================================
    // Map scale factor operations
    // ========================================================================
    
    /**
     * Apply map scale factors to U-tendency
     * WRF: tendency = tendency * msfux * msfuy
     */
    static torch::Tensor apply_msf_u(const torch::Tensor& tendency,
                                     const torch::Tensor& msfux,
                                     const torch::Tensor& msfuy);
    
    /**
     * Apply map scale factors to V-tendency
     * WRF: tendency = tendency * msfvx * msfvy
     */
    static torch::Tensor apply_msf_v(const torch::Tensor& tendency,
                                     const torch::Tensor& msfvx,
                                     const torch::Tensor& msfvy);
    
    /**
     * Apply map scale factors to scalar tendency
     * WRF: tendency = tendency * msftx * msfty
     */
    static torch::Tensor apply_msf_scalar(const torch::Tensor& tendency,
                                          const torch::Tensor& msftx,
                                          const torch::Tensor& msfty);
    
    // ========================================================================
    // Utilities
    // ========================================================================
    
    /**
     * Check if tensor dimensions match expected staggered grid dimensions
     */
    static bool validate_u_dimensions(const torch::Tensor& u, 
                                     int nx, int ny, int nz);
    static bool validate_v_dimensions(const torch::Tensor& v, 
                                     int nx, int ny, int nz);
    static bool validate_w_dimensions(const torch::Tensor& w, 
                                     int nx, int ny, int nz);
    static bool validate_mass_dimensions(const torch::Tensor& f, 
                                        int nx, int ny, int nz);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_STAGGERED_GRID_H