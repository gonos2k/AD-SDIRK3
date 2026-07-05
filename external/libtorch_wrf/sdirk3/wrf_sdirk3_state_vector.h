#ifndef WRF_SDIRK3_STATE_VECTOR_H
#define WRF_SDIRK3_STATE_VECTOR_H

#include <torch/torch.h>
#include <vector>

namespace wrf {
namespace sdirk3 {

/**
 * StateVector: WRF state representation
 * Memory layout: (j, k, i) - ALREADY CORRECT
 * 
 * According to approved design v6.0
 */
class StateVector {
public:
    // Prognostic variables
    torch::Tensor u;      // x-velocity [nj, nk, ni+1] (u-staggered)
    torch::Tensor v;      // y-velocity [nj+1, nk, ni] (v-staggered)  
    torch::Tensor w;      // z-velocity [nj, nk+1, ni] (w-staggered)
    torch::Tensor theta;  // potential temperature [nj, nk, ni]
    torch::Tensor phi;    // geopotential [nj, nk, ni]
    torch::Tensor mu_d;   // dry column mass [nj, ni] (2D)
    
    // Optional: moisture and other scalars
    std::vector<torch::Tensor> scalars;  // qv, qc, qr, etc.
    
    // Constructor
    StateVector() = default;
    
    // Create from dimensions
    static StateVector create(int nj, int nk, int ni, 
                              torch::Device device = torch::kCPU,
                              bool requires_grad = false) {
        StateVector state;
        
        auto options = torch::TensorOptions()
            .dtype(torch::kFloat32)
            .device(device)
            .requires_grad(requires_grad);
        
        // Allocate with correct staggering
        state.u = torch::zeros({nj, nk, ni + 1}, options);
        state.v = torch::zeros({nj + 1, nk, ni}, options);
        state.w = torch::zeros({nj, nk + 1, ni}, options);
        state.theta = torch::zeros({nj, nk, ni}, options);
        state.phi = torch::zeros({nj, nk, ni}, options);
        state.mu_d = torch::zeros({nj, ni}, options);
        
        return state;
    }
    
    // Clone state
    StateVector clone() const {
        StateVector cloned;
        cloned.u = u.clone();
        cloned.v = v.clone();
        cloned.w = w.clone();
        cloned.theta = theta.clone();
        cloned.phi = phi.clone();
        cloned.mu_d = mu_d.clone();
        
        for (const auto& scalar : scalars) {
            cloned.scalars.push_back(scalar.clone());
        }
        
        return cloned;
    }
    
    // Zero-like constructor
    static StateVector zeros_like(const StateVector& other) {
        StateVector state;
        state.u = torch::zeros_like(other.u);
        state.v = torch::zeros_like(other.v);
        state.w = torch::zeros_like(other.w);
        state.theta = torch::zeros_like(other.theta);
        state.phi = torch::zeros_like(other.phi);
        state.mu_d = torch::zeros_like(other.mu_d);
        
        for (const auto& scalar : other.scalars) {
            state.scalars.push_back(torch::zeros_like(scalar));
        }
        
        return state;
    }
    
    // Arithmetic operations
    StateVector operator+(const StateVector& other) const {
        StateVector result;
        result.u = u + other.u;
        result.v = v + other.v;
        result.w = w + other.w;
        result.theta = theta + other.theta;
        result.phi = phi + other.phi;
        result.mu_d = mu_d + other.mu_d;
        
        for (size_t i = 0; i < scalars.size(); ++i) {
            result.scalars.push_back(scalars[i] + other.scalars[i]);
        }
        
        return result;
    }
    
    StateVector operator-(const StateVector& other) const {
        StateVector result;
        result.u = u - other.u;
        result.v = v - other.v;
        result.w = w - other.w;
        result.theta = theta - other.theta;
        result.phi = phi - other.phi;
        result.mu_d = mu_d - other.mu_d;
        
        for (size_t i = 0; i < scalars.size(); ++i) {
            result.scalars.push_back(scalars[i] - other.scalars[i]);
        }
        
        return result;
    }
    
    StateVector operator*(float scalar) const {
        StateVector result;
        result.u = u * scalar;
        result.v = v * scalar;
        result.w = w * scalar;
        result.theta = theta * scalar;
        result.phi = phi * scalar;
        result.mu_d = mu_d * scalar;
        
        for (const auto& s : scalars) {
            result.scalars.push_back(s * scalar);
        }
        
        return result;
    }
    
    friend StateVector operator*(float scalar, const StateVector& state) {
        return state * scalar;
    }
    
    // Compute norm
    torch::Tensor norm() const {
        torch::Tensor norm_sq = torch::zeros({1}, u.options());
        
        norm_sq = norm_sq + torch::sum(u * u);
        norm_sq = norm_sq + torch::sum(v * v);
        norm_sq = norm_sq + torch::sum(w * w);
        norm_sq = norm_sq + torch::sum(theta * theta);
        norm_sq = norm_sq + torch::sum(phi * phi);
        norm_sq = norm_sq + torch::sum(mu_d * mu_d);
        
        for (const auto& scalar : scalars) {
            norm_sq = norm_sq + torch::sum(scalar * scalar);
        }
        
        return torch::sqrt(norm_sq);
    }
    
    // Compute mass-weighted norm (for convergence checking)
    torch::Tensor mass_weighted_norm() const {
        // Expand mu_d to 3D for mass weighting
        auto mu_d_3d = mu_d.unsqueeze(1);  // [nj, 1, ni]
        
        torch::Tensor norm_sq = torch::zeros({1}, u.options());
        
        // u, v at cell faces - interpolate mu_d appropriately
        auto mu_d_u = mu_d_3d.expand({-1, u.size(1), -1});
        auto mu_d_v = mu_d_3d.expand({-1, v.size(1), -1});
        
        // Mass-weighted norms
        norm_sq = norm_sq + torch::sum(u * u * mu_d_u.slice(2, 0, u.size(2)));
        norm_sq = norm_sq + torch::sum(v * v * mu_d_v.slice(0, 0, v.size(0)));
        norm_sq = norm_sq + torch::sum(w * w * mu_d_3d.expand_as(w));
        norm_sq = norm_sq + torch::sum(theta * theta * mu_d_3d.expand_as(theta));
        norm_sq = norm_sq + torch::sum(phi * phi * mu_d_3d.expand_as(phi));
        
        return torch::sqrt(norm_sq);
    }
    
    // Dot product
    torch::Tensor dot(const StateVector& other) const {
        torch::Tensor result = torch::zeros({1}, u.options());
        
        result = result + torch::sum(u * other.u);
        result = result + torch::sum(v * other.v);
        result = result + torch::sum(w * other.w);
        result = result + torch::sum(theta * other.theta);
        result = result + torch::sum(phi * other.phi);
        result = result + torch::sum(mu_d * other.mu_d);
        
        for (size_t i = 0; i < scalars.size(); ++i) {
            result = result + torch::sum(scalars[i] * other.scalars[i]);
        }
        
        return result;
    }
    
    // Convert to flat tensor (for linear algebra operations)
    torch::Tensor flatten() const {
        std::vector<torch::Tensor> flat_tensors;
        
        // v20.14 r49-fix: Order must match StateLayout [u, v, w, ph, t, mu]
        flat_tensors.push_back(u.flatten());
        flat_tensors.push_back(v.flatten());
        flat_tensors.push_back(w.flatten());
        flat_tensors.push_back(phi.flatten());     // ph before theta
        flat_tensors.push_back(theta.flatten());   // t after ph
        flat_tensors.push_back(mu_d.flatten());
        
        for (const auto& scalar : scalars) {
            flat_tensors.push_back(scalar.flatten());
        }
        
        return torch::cat(flat_tensors, 0);
    }
    
    // Reconstruct from flat tensor
    static StateVector from_flat(const torch::Tensor& flat, 
                                 const StateVector& template_state) {
        StateVector state;
        int offset = 0;
        
        auto extract_and_reshape = [&flat, &offset](const torch::Tensor& template_tensor) {
            int numel = template_tensor.numel();
            auto extracted = flat.slice(0, offset, offset + numel);
            offset += numel;
            return extracted.reshape_as(template_tensor);
        };
        
        // v20.14 r49-fix: Order must match StateLayout [u, v, w, ph, t, mu]
        state.u = extract_and_reshape(template_state.u);
        state.v = extract_and_reshape(template_state.v);
        state.w = extract_and_reshape(template_state.w);
        state.phi = extract_and_reshape(template_state.phi);     // ph before theta
        state.theta = extract_and_reshape(template_state.theta); // t after ph
        state.mu_d = extract_and_reshape(template_state.mu_d);
        
        for (const auto& scalar_template : template_state.scalars) {
            state.scalars.push_back(extract_and_reshape(scalar_template));
        }
        
        return state;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_STATE_VECTOR_H