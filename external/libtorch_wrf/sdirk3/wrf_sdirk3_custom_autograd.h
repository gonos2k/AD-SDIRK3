// WRF-SDIRK3 Custom Autograd Functions
// Solves the .item<float>() synchronization problem with efficient custom gradients

#ifndef WRF_SDIRK3_CUSTOM_AUTOGRAD_H
#define WRF_SDIRK3_CUSTOM_AUTOGRAD_H

#include <torch/torch.h>
#include <vector>

namespace wrf {
namespace sdirk3 {
namespace autograd {

// Custom autograd function for deformation tensor calculation
// Eliminates 786,816 .item<float>() calls
class Defor12Function : public torch::autograd::Function<Defor12Function> {
public:
    // Forward pass - vectorized computation
    static torch::Tensor forward(
        torch::autograd::AutogradContext *ctx,
        const torch::Tensor& u,    // [ny, nz, nx_u]
        const torch::Tensor& v,    // [ny_v, nz, nx]
        double rdx,
        double rdy,
        bool periodic_x) {
        
        // Save tensors for backward pass
        ctx->save_for_backward({u, v});
        
        // Save scalars
        ctx->saved_data["rdx"] = rdx;
        ctx->saved_data["rdy"] = rdy;
        ctx->saved_data["periodic_x"] = periodic_x;
        
        // Get dimensions
        int ny_u = u.size(0);
        int nz = u.size(1);
        int nx_u = u.size(2);
        int ny_v = v.size(0);
        int nx = v.size(2);
        
        // Initialize output
        auto options = u.options();
        torch::Tensor defor12 = torch::zeros({ny_v, nz, nx}, options);
        
        // VECTORIZED COMPUTATION - No .item<float>() calls!
        if (ny_v > 2 && nx > 2) {
            // Compute ∂u/∂y efficiently
            auto u_upper = u.index({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, nx-1)});
            
            auto u_lower = u.index({torch::indexing::Slice(0, ny_v-2),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, nx-1)});
            
            auto du_dy = (u_upper - u_lower) * rdy;
            
            // Compute ∂v/∂x efficiently
            auto v_right = v.index({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, nx-1)});
            
            auto v_left = v.index({torch::indexing::Slice(1, ny_v-1),
                                  torch::indexing::Slice(),
                                  torch::indexing::Slice(0, nx-2)});
            
            auto dv_dx = (v_right - v_left) * rdx;
            
            // Sum derivatives
            defor12.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(),
                               torch::indexing::Slice(1, nx-1)},
                              du_dy + dv_dx);
        }
        
        // Handle periodic boundaries if needed
        if (periodic_x && ny_v > 2) {
            // i=0 boundary
            auto u_j0 = u.index({torch::indexing::Slice(1, ny_v-1),
                                torch::indexing::Slice(), 0});
            auto u_j0_prev = u.index({torch::indexing::Slice(0, ny_v-2),
                                     torch::indexing::Slice(), 0});
            auto du_dy_i0 = (u_j0 - u_j0_prev) * rdy;
            
            auto v_i0 = v.index({torch::indexing::Slice(1, ny_v-1),
                                torch::indexing::Slice(), 0});
            auto v_i0_wrap = v.index({torch::indexing::Slice(1, ny_v-1),
                                     torch::indexing::Slice(), nx-2});
            auto dv_dx_i0 = (v_i0 - v_i0_wrap) * rdx;
            
            defor12.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(), 0},
                              du_dy_i0 + dv_dx_i0);
            
            // i=nx-1 boundary
            if (nx_u > nx) {
                auto u_imax = u.index({torch::indexing::Slice(1, ny_v-1),
                                      torch::indexing::Slice(), nx});
                auto u_imax_prev = u.index({torch::indexing::Slice(0, ny_v-2),
                                           torch::indexing::Slice(), nx});
                auto du_dy_imax = (u_imax - u_imax_prev) * rdy;
                
                auto v_imax = v.index({torch::indexing::Slice(1, ny_v-1),
                                      torch::indexing::Slice(), 0});
                auto v_imax_prev = v.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(), nx-2});
                auto dv_dx_imax = (v_imax - v_imax_prev) * rdx;
                
                defor12.index_put_({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(), nx-1},
                                  du_dy_imax + dv_dx_imax);
            }
        }
        
        return defor12;
    }
    
    // Backward pass - efficient gradient computation
    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext *ctx,
        torch::autograd::variable_list grad_outputs) {
        
        auto saved = ctx->get_saved_variables();
        auto u = saved[0];
        auto v = saved[1];
        
        double rdx = ctx->saved_data["rdx"].toDouble();
        double rdy = ctx->saved_data["rdy"].toDouble();
        bool periodic_x = ctx->saved_data["periodic_x"].toBool();
        
        auto grad_defor12 = grad_outputs[0];
        
        // Get dimensions
        int ny_u = u.size(0);
        int nz = u.size(1);
        int nx_u = u.size(2);
        int ny_v = v.size(0);
        int nx = v.size(2);
        
        // Initialize gradient tensors
        torch::Tensor grad_u = torch::zeros_like(u);
        torch::Tensor grad_v = torch::zeros_like(v);
        
        // Compute gradients efficiently (reverse of forward pass)
        if (ny_v > 2 && nx > 2) {
            // Gradient w.r.t u (from ∂u/∂y term)
            auto grad_interior = grad_defor12.index({
                torch::indexing::Slice(1, ny_v-1),
                torch::indexing::Slice(),
                torch::indexing::Slice(1, nx-1)
            }) * rdy;
            
            // Upper contribution (positive)
            grad_u.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(),
                               torch::indexing::Slice(1, nx-1)},
                              grad_u.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(),
                                           torch::indexing::Slice(1, nx-1)}) + grad_interior);
            
            // Lower contribution (negative)
            grad_u.index_put_({torch::indexing::Slice(0, ny_v-2),
                               torch::indexing::Slice(),
                               torch::indexing::Slice(1, nx-1)},
                              grad_u.index({torch::indexing::Slice(0, ny_v-2),
                                           torch::indexing::Slice(),
                                           torch::indexing::Slice(1, nx-1)}) - grad_interior);
            
            // Gradient w.r.t v (from ∂v/∂x term)
            grad_interior = grad_defor12.index({
                torch::indexing::Slice(1, ny_v-1),
                torch::indexing::Slice(),
                torch::indexing::Slice(1, nx-1)
            }) * rdx;
            
            // Right contribution (positive)
            grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(),
                               torch::indexing::Slice(1, nx-1)},
                              grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(),
                                           torch::indexing::Slice(1, nx-1)}) + grad_interior);
            
            // Left contribution (negative)
            grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(),
                               torch::indexing::Slice(0, nx-2)},
                              grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(),
                                           torch::indexing::Slice(0, nx-2)}) - grad_interior);
        }
        
        // Handle periodic boundary gradients if needed
        if (periodic_x && ny_v > 2) {
            // Gradients for i=0 boundary
            auto grad_i0 = grad_defor12.index({
                torch::indexing::Slice(1, ny_v-1),
                torch::indexing::Slice(), 0
            });
            
            // u gradients at i=0
            grad_u.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(), 0},
                              grad_u.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(), 0}) + grad_i0 * rdy);
            
            grad_u.index_put_({torch::indexing::Slice(0, ny_v-2),
                               torch::indexing::Slice(), 0},
                              grad_u.index({torch::indexing::Slice(0, ny_v-2),
                                           torch::indexing::Slice(), 0}) - grad_i0 * rdy);
            
            // v gradients at i=0 (with periodic wrap)
            grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(), 0},
                              grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(), 0}) + grad_i0 * rdx);
            
            grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                               torch::indexing::Slice(), nx-2},
                              grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                           torch::indexing::Slice(), nx-2}) - grad_i0 * rdx);
            
            // Similar for i=nx-1 boundary
            if (nx_u > nx) {
                auto grad_imax = grad_defor12.index({
                    torch::indexing::Slice(1, ny_v-1),
                    torch::indexing::Slice(), nx-1
                });
                
                grad_u.index_put_({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(), nx},
                                  grad_u.index({torch::indexing::Slice(1, ny_v-1),
                                               torch::indexing::Slice(), nx}) + grad_imax * rdy);
                
                grad_u.index_put_({torch::indexing::Slice(0, ny_v-2),
                                   torch::indexing::Slice(), nx},
                                  grad_u.index({torch::indexing::Slice(0, ny_v-2),
                                               torch::indexing::Slice(), nx}) - grad_imax * rdy);
                
                grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(), 0},
                                  grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                               torch::indexing::Slice(), 0}) + grad_imax * rdx);
                
                grad_v.index_put_({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(), nx-2},
                                  grad_v.index({torch::indexing::Slice(1, ny_v-1),
                                               torch::indexing::Slice(), nx-2}) - grad_imax * rdx);
            }
        }
        
        // Return gradients (None for scalar arguments)
        return {grad_u, grad_v, torch::Tensor(), torch::Tensor(), torch::Tensor()};
    }
};

// Helper function for easy usage
inline torch::Tensor compute_defor12_autograd(
    const torch::Tensor& u,
    const torch::Tensor& v,
    float rdx,
    float rdy,
    bool periodic_x = false) {
    return Defor12Function::apply(u, v, rdx, rdy, periodic_x);
}

// Custom function for Kh averaging (eliminates 212,544 .item<float>() calls)
class KhVortFunction : public torch::autograd::Function<KhVortFunction> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext *ctx,
        const torch::Tensor& Kh) {  // [ny, nz, nx]
        
        ctx->save_for_backward({Kh});
        
        int ny = Kh.size(0);
        int nz = Kh.size(1);
        int nx = Kh.size(2);
        int ny_v = ny + 1;  // Staggered dimension
        
        auto options = Kh.options();
        torch::Tensor Kh_vort = torch::zeros({ny_v, nz, nx}, options);
        
        // Vectorized 4-point averaging
        if (ny > 1 && ny_v > 2 && nx > 1) {
            // Extract 4 shifted versions
            auto Kh_00 = Kh.index({torch::indexing::Slice(0, ny_v-2),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(0, nx-1)});
            
            auto Kh_01 = Kh.index({torch::indexing::Slice(0, ny_v-2),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, nx)});
            
            auto Kh_10 = Kh.index({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(0, nx-1)});
            
            auto Kh_11 = Kh.index({torch::indexing::Slice(1, ny_v-1),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, nx)});
            
            // Average efficiently
            auto Kh_avg = 0.25f * (Kh_00 + Kh_01 + Kh_10 + Kh_11);
            
            // Place in output
            Kh_vort.index_put_({torch::indexing::Slice(1, ny_v-1),
                                torch::indexing::Slice(),
                                torch::indexing::Slice(1, nx)},
                               Kh_avg);
        }
        
        return Kh_vort;
    }
    
    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext *ctx,
        torch::autograd::variable_list grad_outputs) {
        
        auto saved = ctx->get_saved_variables();
        auto Kh = saved[0];
        auto grad_Kh_vort = grad_outputs[0];
        
        int ny = Kh.size(0);
        int nz = Kh.size(1);
        int nx = Kh.size(2);
        int ny_v = ny + 1;
        
        torch::Tensor grad_Kh = torch::zeros_like(Kh);
        
        // Distribute gradients back to 4 points
        if (ny > 1 && ny_v > 2 && nx > 1) {
            auto grad_avg = grad_Kh_vort.index({
                torch::indexing::Slice(1, ny_v-1),
                torch::indexing::Slice(),
                torch::indexing::Slice(1, nx)
            }) * 0.25f;
            
            // Accumulate gradients
            grad_Kh.index_put_({torch::indexing::Slice(0, ny_v-2),
                                torch::indexing::Slice(),
                                torch::indexing::Slice(0, nx-1)},
                               grad_Kh.index({torch::indexing::Slice(0, ny_v-2),
                                             torch::indexing::Slice(),
                                             torch::indexing::Slice(0, nx-1)}) + grad_avg);
            
            grad_Kh.index_put_({torch::indexing::Slice(0, ny_v-2),
                                torch::indexing::Slice(),
                                torch::indexing::Slice(1, nx)},
                               grad_Kh.index({torch::indexing::Slice(0, ny_v-2),
                                             torch::indexing::Slice(),
                                             torch::indexing::Slice(1, nx)}) + grad_avg);
            
            grad_Kh.index_put_({torch::indexing::Slice(1, ny_v-1),
                                torch::indexing::Slice(),
                                torch::indexing::Slice(0, nx-1)},
                               grad_Kh.index({torch::indexing::Slice(1, ny_v-1),
                                             torch::indexing::Slice(),
                                             torch::indexing::Slice(0, nx-1)}) + grad_avg);
            
            grad_Kh.index_put_({torch::indexing::Slice(1, ny_v-1),
                                torch::indexing::Slice(),
                                torch::indexing::Slice(1, nx)},
                               grad_Kh.index({torch::indexing::Slice(1, ny_v-1),
                                             torch::indexing::Slice(),
                                             torch::indexing::Slice(1, nx)}) + grad_avg);
        }
        
        return {grad_Kh};
    }
};

// Helper function
inline torch::Tensor compute_Kh_vort_autograd(const torch::Tensor& Kh) {
    return KhVortFunction::apply(Kh);
}

} // namespace autograd
} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_CUSTOM_AUTOGRAD_H