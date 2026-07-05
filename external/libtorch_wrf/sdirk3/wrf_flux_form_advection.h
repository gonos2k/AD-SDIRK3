#ifndef WRF_FLUX_FORM_ADVECTION_H
#define WRF_FLUX_FORM_ADVECTION_H

#include <torch/torch.h>
#include "wrf_staggered_grid.h"

namespace wrf {
namespace sdirk3 {

/**
 * @brief WRF-conforming flux-form advection implementation
 * 
 * This class implements the exact advection schemes used in WRF,
 * including proper treatment of staggered grids, coupled momentum,
 * and map scale factors.
 */
class FluxFormAdvection {
public:
    // ========================================================================
    // U-momentum advection (at U-points)
    // ========================================================================
    
    /**
     * Compute U-momentum advection following WRF's advect_u
     * 
     * WRF formula (2nd order):
     * tendency = -mrdx*0.25*((ru[i+1]+ru[i])*(u[i+1]+u[i])
     *                       -(ru[i]+ru[i-1])*(u[i]+u[i-1]))
     *           -mrdy*0.25*((rv[i,j]+rv[i-1,j])*(u[i,j]+u[i,j-1])
     *                       -(rv[i,j-1]+rv[i-1,j-1])*(u[i,j-1]+u[i,j-2]))
     *           -mrdz*0.25*((rom[i,k]+rom[i-1,k])*(u[i,k]+u[i,k-1])
     *                       -(rom[i,k-1]+rom[i-1,k-1])*(u[i,k-1]+u[i,k-2]))
     */
    static torch::Tensor advect_u_2nd_order(
        const torch::Tensor& u,      // U-velocity at U-points
        const torch::Tensor& v,      // V-velocity at V-points
        const torch::Tensor& w,      // W-velocity at W-points
        const torch::Tensor& ru,     // Coupled momentum μu at U-points
        const torch::Tensor& rv,     // Coupled momentum μv at V-points
        const torch::Tensor& rom,    // Coupled density at mass points
        const torch::Tensor& mut,    // Dry air mass at mass points
        float rdx, float rdy,        // Inverse grid spacing
        const torch::Tensor& rdnw,   // Inverse vertical grid spacing
        const torch::Tensor& msfux,  // Map scale factor x at U-points
        const torch::Tensor& msfuy,  // Map scale factor y at U-points
        const torch::Tensor& msfvx,  // Map scale factor x at V-points
        const torch::Tensor& fzm,    // Vertical interp weight (k)
        const torch::Tensor& fzp);   // Vertical interp weight (k-1)
    
    /**
     * Higher-order U-momentum advection (5th/6th order)
     */
    static torch::Tensor advect_u_higher_order(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& ru,
        const torch::Tensor& rv,
        const torch::Tensor& rom,
        const torch::Tensor& mut,
        float rdx, float rdy,
        const torch::Tensor& rdnw,
        const torch::Tensor& msfux,
        const torch::Tensor& msfuy,
        const torch::Tensor& msfvx,
        const torch::Tensor& fzm,
        const torch::Tensor& fzp,
        int h_order,               // Horizontal order (5 or 6)
        int v_order);              // Vertical order (3, 5, or 6)
    
    // ========================================================================
    // V-momentum advection (at V-points)
    // ========================================================================
    
    /**
     * Compute V-momentum advection following WRF's advect_v
     * Similar structure to advect_u but at V-staggered points
     */
    static torch::Tensor advect_v_2nd_order(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& ru,
        const torch::Tensor& rv,
        const torch::Tensor& rom,
        const torch::Tensor& mut,
        float rdx, float rdy,
        const torch::Tensor& rdnw,
        const torch::Tensor& msfvx,
        const torch::Tensor& msfvy,
        const torch::Tensor& msfuy,
        const torch::Tensor& fzm,
        const torch::Tensor& fzp);
    
    // ========================================================================
    // W-momentum advection (at W-points)
    // ========================================================================
    
    /**
     * Compute W-momentum advection following WRF's advect_w
     * 
     * WRF formula (2nd order):
     * tendency = -mrdx*0.5*((fzm[k]*ru[i+1,k]+fzp[k]*ru[i+1,k-1])*(w[i+1]+w[i])
     *                      -(fzm[k]*ru[i,k]+fzp[k]*ru[i,k-1])*(w[i]+w[i-1]))
     */
    static torch::Tensor advect_w_2nd_order(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& ru,
        const torch::Tensor& rv,
        const torch::Tensor& rom,
        const torch::Tensor& mut,
        float rdx, float rdy,
        const torch::Tensor& rdn,    // Note: rdn not rdnw for W
        const torch::Tensor& msftx,
        const torch::Tensor& msfty,
        const torch::Tensor& fzm,
        const torch::Tensor& fzp);
    
    // ========================================================================
    // Scalar advection (at mass points)
    // ========================================================================
    
    /**
     * Compute scalar advection following WRF's advect_scalar
     * Used for theta, moisture, and other scalars
     * 
     * WRF formula (2nd order):
     * fqx[i] = 0.5*ru[i]*(field[i]+field[i-1])
     * fqy[j] = 0.5*rv[j]*(field[j]+field[j-1])
     * tendency = -mrdx*(fqx[i+1]-fqx[i]) - mrdy*(fqy[j+1]-fqy[j])
     */
    static torch::Tensor advect_scalar_2nd_order(
        const torch::Tensor& field,  // Scalar field at mass points
        const torch::Tensor& u,      // U-velocity at U-points
        const torch::Tensor& v,      // V-velocity at V-points
        const torch::Tensor& w,      // W-velocity at W-points
        const torch::Tensor& ru,     // Coupled momentum μu
        const torch::Tensor& rv,     // Coupled momentum μv
        const torch::Tensor& rom,    // Coupled density
        const torch::Tensor& mut,    // Dry air mass
        float rdx, float rdy,
        const torch::Tensor& rdnw,
        const torch::Tensor& msftx,
        const torch::Tensor& msfty);
    
    // ========================================================================
    // Flux computation helpers
    // ========================================================================
    
    /**
     * 5th-order upwind flux (flux5 in WRF)
     * flux = vel*flux5(q[i-3], q[i-2], q[i-1], q[i], q[i+1], q[i+2], vel)
     */
    static float flux5(float q_im3, float q_im2, float q_im1, 
                      float q_i, float q_ip1, float q_ip2, 
                      float vel);
    
    /**
     * 3rd-order upwind flux (flux3 in WRF)
     * flux = vel*flux3(q[i-2], q[i-1], q[i], q[i+1], vel)
     */
    static float flux3(float q_im2, float q_im1, float q_i, 
                      float q_ip1, float vel);
    
    /**
     * 4th-order centered flux (flux4 in WRF)
     * flux = vel*flux4(q[i-2], q[i-1], q[i], q[i+1])
     */
    static float flux4(float q_im2, float q_im1, float q_i, float q_ip1);
    
    /**
     * 6th-order centered flux (flux6 in WRF)
     * flux = vel*flux6(q[i-3], q[i-2], q[i-1], q[i], q[i+1], q[i+2])
     */
    static float flux6(float q_im3, float q_im2, float q_im1,
                      float q_i, float q_ip1, float q_ip2);
    
    // ========================================================================
    // Map scale factor application
    // ========================================================================
    
    /**
     * Apply map scale factors to advection tendency
     * This must be done correctly for each variable type
     */
    static torch::Tensor apply_map_scale_factors_u(
        const torch::Tensor& tendency,
        const torch::Tensor& msfux,
        const torch::Tensor& msfuy,
        float rdx, float rdy);
    
    static torch::Tensor apply_map_scale_factors_v(
        const torch::Tensor& tendency,
        const torch::Tensor& msfvx,
        const torch::Tensor& msfvy,
        float rdx, float rdy);
    
    static torch::Tensor apply_map_scale_factors_scalar(
        const torch::Tensor& tendency,
        const torch::Tensor& msftx,
        const torch::Tensor& msfty,
        float rdx, float rdy);
    
    // ========================================================================
    // Vertical flux computation
    // ========================================================================
    
    /**
     * Compute vertical flux with proper omega calculation
     * WRF uses specific formulations for vertical velocity
     */
    static torch::Tensor compute_vertical_flux_momentum(
        const torch::Tensor& field,   // Momentum component
        const torch::Tensor& rom,     // Coupled density
        const torch::Tensor& w,       // W-velocity
        const torch::Tensor& mut,     // Dry air mass
        const torch::Tensor& rdnw,    // Inverse vertical spacing
        const torch::Tensor& fzm,     // Vertical weight
        const torch::Tensor& fzp);    // Vertical weight
    
    static torch::Tensor compute_vertical_flux_scalar(
        const torch::Tensor& field,   // Scalar field
        const torch::Tensor& rom,     // Coupled density
        const torch::Tensor& w,       // W-velocity
        const torch::Tensor& mut,     // Dry air mass
        const torch::Tensor& rdnw);   // Inverse vertical spacing
    
private:
    /**
     * Helper to compute mrdx = msfux * msfuy * rdx
     * with proper handling of periodic/open boundaries
     */
    static torch::Tensor compute_mrdx(const torch::Tensor& msfux,
                                     const torch::Tensor& msfuy,
                                     float rdx);
    
    /**
     * Helper to compute mrdy = msfvx * msfvy * rdy
     */
    static torch::Tensor compute_mrdy(const torch::Tensor& msfvx,
                                     const torch::Tensor& msfvy,
                                     float rdy);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_FLUX_FORM_ADVECTION_H