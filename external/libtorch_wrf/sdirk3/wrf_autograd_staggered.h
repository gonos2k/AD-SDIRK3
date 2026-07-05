/**
 * WRF Staggered Grid Custom Autograd Functions
 * 
 * 자동미분을 위한 WRF 스태거드 그리드 커스텀 함수
 * Maintains WRF computational consistency (WRF 연산 정합성 유지)
 * No clamping, no temporary workarounds (임시방편 없이 정석대로)
 */

#ifndef WRF_AUTOGRAD_STAGGERED_H
#define WRF_AUTOGRAD_STAGGERED_H

#include <torch/torch.h>
#include <vector>
#include <memory>

namespace wrf {
namespace autograd {

/**
 * Custom autograd function for V-momentum X-advection
 * Handles mixed dimensions: V-points [ny_v, nz, nx] with U-velocities [ny, nz, nx_u]
 */
class AdvectVPointScalarX : public torch::autograd::Function<AdvectVPointScalarX> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        const torch::Tensor& f,  // Scalar at V-points [ny_v, nz, nx]
        const torch::Tensor& u,  // Velocity at U-points [ny, nz, nx_u]
        float rdx,
        bool periodic_x) {
        
        // Save tensors for backward pass
        ctx->save_for_backward({f, u});
        ctx->saved_data["rdx"] = rdx;
        ctx->saved_data["periodic_x"] = periodic_x;
        
        // Get dimensions
        int ny_v = f.size(0);
        int nz = f.size(1);
        int nx = f.size(2);
        int ny = u.size(0);
        int nx_u = u.size(2);
        
        auto options = f.options();
        torch::Tensor advect = torch::zeros_like(f);
        
        // Average U from U-points to V-points in Y-direction
        // This is where dimension mixing occurs in standard PyTorch
        torch::Tensor u_at_v_stagger = torch::zeros({ny_v, nz, nx_u}, options);
        
        if (ny_v == ny + 1) {
            // FIX 2026-01-31 Tier3: Vectorized Y-averaging — slice replaces j-loop.
            // Interior: u_at_v[1:ny,:,:] = 0.5*(u[0:ny-1,:,:] + u[1:ny,:,:])
            if (ny > 1) {
                u_at_v_stagger.slice(0, 1, ny).copy_(
                    0.5f * (u.slice(0, 0, ny - 1) + u.slice(0, 1, ny)));
            }
            // Boundaries: extrapolate from nearest U row
            u_at_v_stagger.select(0, 0).copy_(u.select(0, 0));
            u_at_v_stagger.select(0, ny).copy_(u.select(0, ny - 1));
        } else {
            // FIX 2026-01-31 Tier3: Vectorized boundary tile Y-averaging — slice replaces j-loop.
            int ny_min = std::min(ny, ny_v);
            if (ny_min > 1) {
                u_at_v_stagger.slice(0, 0, ny_min - 1).copy_(
                    0.5f * (u.slice(0, 0, ny_min - 1) + u.slice(0, 1, ny_min)));
            }
            if (ny_min > 0) {
                u_at_v_stagger.select(0, ny_min - 1).copy_(
                    u.select(0, std::min(ny - 1, ny_v - 1)));
            }
        }
        
        // FIX 2026-01-31 Tier3: Vectorized flux-form advection — bulk slice replaces i-loop.
        // Flux at interface i: F[i] = 0.25 * u_at_v[:,:,i] * (f[:,:,i] + f[:,:,i+1])
        // Tendency: advect[:,:,i] = -(F[i] - F[i-1]) * rdx
        if (periodic_x && nx > 0) {
            // Periodic: all nx cells active, wrap f and flux
            auto f_right = torch::cat({f.slice(2, 1, nx), f.slice(2, 0, 1)}, 2);  // f shifted left (wrap)
            auto u_flux = u_at_v_stagger.slice(2, 0, nx);  // [ny_v, nz, nx]
            auto flux_all = 0.25f * u_flux * (f + f_right);  // flux at each interface [ny_v, nz, nx]
            // flux_west[i] = flux[i-1], with wrap: flux_west[0] = flux[nx-1]
            auto flux_west = torch::cat({flux_all.slice(2, nx - 1, nx), flux_all.slice(2, 0, nx - 1)}, 2);
            advect.copy_(-(flux_all - flux_west) * rdx);
        } else if (nx > 2) {
            // Non-periodic: interior cells i=1..nx-2
            // Fluxes at interfaces 0..nx-2: F[i] = 0.25*u[:,:,i]*(f[:,:,i]+f[:,:,i+1])
            auto flux_all = 0.25f * u_at_v_stagger.slice(2, 0, nx - 1) *
                            (f.slice(2, 0, nx - 1) + f.slice(2, 1, nx));  // [ny_v, nz, nx-1]
            // advect[:,:,i] = -(F[i] - F[i-1]) for i=1..nx-2
            advect.slice(2, 1, nx - 1).copy_(
                -(flux_all.slice(2, 1, nx - 1) - flux_all.slice(2, 0, nx - 2)) * rdx);
        }
        
        return advect;
    }
    
    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext* ctx,
        torch::autograd::variable_list grad_outputs) {
        
        auto saved = ctx->get_saved_variables();
        auto f = saved[0];
        auto u = saved[1];
        float rdx = ctx->saved_data["rdx"].toDouble();
        bool periodic_x = ctx->saved_data["periodic_x"].toBool();
        
        auto grad_advect = grad_outputs[0];
        
        // Compute gradients w.r.t. f and u
        // This requires implementing the adjoint of the forward operations
        // respecting the staggered grid structure
        
        torch::Tensor grad_f = torch::zeros_like(f);
        torch::Tensor grad_u = torch::zeros_like(u);
        
        // Adjoint computation (simplified - full implementation needed)
        // The key is to properly handle the dimension differences
        // when propagating gradients through the averaging operations
        
        int ny_v = f.size(0);
        int nz = f.size(1);
        int nx = f.size(2);
        int ny = u.size(0);
        int nx_u = u.size(2);
        
        // Gradient computation respecting staggered grid averaging
        // ... (detailed adjoint implementation needed)
        
        return {grad_f, grad_u, torch::Tensor(), torch::Tensor()};
    }
};

/**
 * Custom autograd function for U-momentum Y-advection  
 * Handles mixed dimensions: U-points [ny, nz, nx_u] with V-velocities [ny_v, nz, nx]
 */
class AdvectUPointScalarY : public torch::autograd::Function<AdvectUPointScalarY> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        const torch::Tensor& f,  // Scalar at U-points [ny, nz, nx_u]
        const torch::Tensor& v,  // Velocity at V-points [ny_v, nz, nx]
        float rdy,
        bool symmetric_ys,
        bool symmetric_ye) {
        
        ctx->save_for_backward({f, v});
        ctx->saved_data["rdy"] = rdy;
        ctx->saved_data["symmetric_ys"] = symmetric_ys;
        ctx->saved_data["symmetric_ye"] = symmetric_ye;
        
        // Similar implementation pattern as AdvectVPointScalarX
        // but for U-momentum in Y-direction
        
        auto options = f.options();
        torch::Tensor advect = torch::zeros_like(f);
        
        // ... (implementation following WRF flux-form)
        
        return advect;
    }
    
    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext* ctx,
        torch::autograd::variable_list grad_outputs) {
        
        // Adjoint implementation for U-Y advection
        // ... (detailed implementation needed)
        
        return {torch::Tensor(), torch::Tensor(), torch::Tensor(), 
                torch::Tensor(), torch::Tensor()};
    }
};

/**
 * Helper function to use custom autograd in computeUnifiedRHS
 */
inline torch::Tensor advect_v_point_scalar_x_autograd(
    const torch::Tensor& f, 
    const torch::Tensor& u, 
    float rdx, 
    bool periodic_x = false) {
    return AdvectVPointScalarX::apply(f, u, rdx, periodic_x);
}

inline torch::Tensor advect_u_point_scalar_y_autograd(
    const torch::Tensor& f,
    const torch::Tensor& v,
    float rdy,
    bool symmetric_ys = false,
    bool symmetric_ye = false) {
    return AdvectUPointScalarY::apply(f, v, rdy, symmetric_ys, symmetric_ye);
}

} // namespace autograd
} // namespace wrf

#endif // WRF_AUTOGRAD_STAGGERED_H