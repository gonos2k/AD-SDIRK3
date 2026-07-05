#ifndef WRF_MEMORY_LAYOUT_H
#define WRF_MEMORY_LAYOUT_H

#include "wrf_sdirk3_torch_wrapper.h"

using torch::indexing::Slice;
using torch::indexing::None;

namespace wrf {
namespace sdirk3 {

/**
 * WRF Memory Layout and Index Conversion
 * 
 * WRF uses Fortran column-major layout with (i,k,j) ordering:
 * - Fortran: array(i,k,j) with i changing fastest in memory
 * - C++/PyTorch: tensor[j,k,i] with i changing fastest in memory
 * 
 * Additionally, WRF uses staggered grids:
 * - u: (ims:ime+1, kms:kme, jms:jme)
 * - v: (ims:ime, kms:kme, jms:jme+1)
 * - w: (ims:ime, kms:kme+1, jms:jme)
 * - scalars: (ims:ime, kms:kme, jms:jme)
 */

class WRFMemoryConverter {
private:
    // Memory bounds
    int ims_, ime_, jms_, kms_, kme_;  // Memory bounds (actively used)
    [[maybe_unused]] int jme_;              // Memory bound (reserved)
    int its_, ite_, jts_, jte_, kts_, kte_;  // Tile bounds
    [[maybe_unused]] int ids_, ide_, jds_, jde_, kds_, kde_;  // Domain bounds (reserved)
    
public:
    WRFMemoryConverter(int ims, int ime, int jms, int jme, int kms, int kme,
                       int its, int ite, int jts, int jte, int kts, int kte,
                       int ids, int ide, int jds, int jde, int kds, int kde)
        : ims_(ims), ime_(ime), jms_(jms), kms_(kms), kme_(kme), jme_(jme),
          its_(its), ite_(ite), jts_(jts), jte_(jte), kts_(kts), kte_(kte),
          ids_(ids), ide_(ide), jds_(jds), jde_(jde), kds_(kds), kde_(kde) {}
    
    // Convert Fortran 3D array (i,k,j) to C++ tensor [j,k,i]
    // FIX Round177: Use int64_t for overflow safety on large grids
    torch::Tensor fortran_to_torch_3d(const float* fortran_array,
                                      int ni, int nk, int nj,
                                      bool is_u_staggered = false,
                                      bool is_v_staggered = false,
                                      bool is_w_staggered = false) {
        // Adjust dimensions for staggering (int64_t for overflow safety)
        int64_t ni_actual = is_u_staggered ? ni + 1 : ni;
        int64_t nj_actual = is_v_staggered ? nj + 1 : nj;
        int64_t nk_actual = is_w_staggered ? nk + 1 : nk;

        // Create tensor with C++ layout [j,k,i]
        auto tensor = torch::zeros({nj_actual, nk_actual, ni_actual}, torch::kFloat32);

        // OPT Round178: Use data_ptr<float>() for faster direct memory access
        // tensor is freshly created: guaranteed CPU, FP32, contiguous
        float* data = tensor.data_ptr<float>();

        // Copy with layout conversion
        // Fortran: fortran_array[i + k*(ime-ims+1) + j*(ime-ims+1)*(kme-kms+1)]
        // C++: data[j*nk*ni + k*ni + i]
        int64_t i_stride = 1;
        int64_t k_stride = static_cast<int64_t>(ime_ - ims_ + 1 + (is_u_staggered ? 1 : 0));
        int64_t j_stride = k_stride * static_cast<int64_t>(kme_ - kms_ + 1 + (is_w_staggered ? 1 : 0));

        for (int64_t j = 0; j < nj_actual; j++) {
            for (int64_t k = 0; k < nk_actual; k++) {
                for (int64_t i = 0; i < ni_actual; i++) {
                    int64_t fortran_idx = i * i_stride + k * k_stride + j * j_stride;
                    int64_t torch_idx = j * nk_actual * ni_actual + k * ni_actual + i;
                    data[torch_idx] = fortran_array[fortran_idx];
                }
            }
        }

        return tensor;
    }
    
    // Convert C++ tensor [j,k,i] back to Fortran 3D array (i,k,j)
    // FIX Round177: Added CPU/FP32 enforcement and int64_t for overflow safety
    void torch_to_fortran_3d(const torch::Tensor& tensor, float* fortran_array,
                            bool is_u_staggered = false,
                            bool is_v_staggered = false,
                            bool is_w_staggered = false) {
        // Get dimensions using int64_t to prevent overflow on large grids
        int64_t nj = tensor.size(0);
        int64_t nk = tensor.size(1);
        int64_t ni = tensor.size(2);

        // Fortran strides (int64_t for overflow safety)
        int64_t i_stride = 1;
        int64_t k_stride = static_cast<int64_t>(ime_ - ims_ + 1 + (is_u_staggered ? 1 : 0));
        int64_t j_stride = k_stride * static_cast<int64_t>(kme_ - kms_ + 1 + (is_w_staggered ? 1 : 0));

        // FIX Round177: Ensure tensor is CPU, FP32, and contiguous before data_ptr<float>()
        // GPU tensors need device transfer, FP64/FP16 tensors need dtype conversion
        auto tensor_cpu = tensor.to(torch::kCPU, torch::kFloat32).contiguous();
        const float* data = tensor_cpu.data_ptr<float>();

        // Copy with layout conversion (int64_t indices for overflow safety)
        for (int64_t j = 0; j < nj; j++) {
            for (int64_t k = 0; k < nk; k++) {
                for (int64_t i = 0; i < ni; i++) {
                    int64_t fortran_idx = i * i_stride + k * k_stride + j * j_stride;
                    int64_t torch_idx = j * nk * ni + k * ni + i;
                    fortran_array[fortran_idx] = data[torch_idx];
                }
            }
        }
    }
    
    // Convert Fortran 2D array (i,j) to C++ tensor [j,i]
    // FIX Round177: Use int64_t for overflow safety on large grids
    // OPT Round178: Use data_ptr<float>() for faster direct memory access
    torch::Tensor fortran_to_torch_2d(const float* fortran_array, int ni, int nj) {
        auto tensor = torch::zeros({nj, ni}, torch::kFloat32);

        // OPT Round178: tensor is freshly created: guaranteed CPU, FP32, contiguous
        float* data = tensor.data_ptr<float>();

        // Fortran: fortran_array[i + j*(ime-ims+1)]
        // C++: data[j*ni + i]
        int64_t i_stride = 1;
        int64_t j_stride = static_cast<int64_t>(ime_ - ims_ + 1);

        for (int64_t j = 0; j < nj; j++) {
            for (int64_t i = 0; i < ni; i++) {
                int64_t fortran_idx = i * i_stride + j * j_stride;
                int64_t torch_idx = j * ni + i;
                data[torch_idx] = fortran_array[fortran_idx];
            }
        }

        return tensor;
    }
    
    // Convert packed state vector from Fortran to C++
    torch::Tensor pack_state_fortran_to_torch(
        const float* u_3d,    // (ims:ime+1, kms:kme, jms:jme)
        const float* v_3d,    // (ims:ime, kms:kme, jms:jme+1)
        const float* w_3d,    // (ims:ime, kms:kme+1, jms:jme)
        const float* t_3d,    // (ims:ime, kms:kme, jms:jme)
        const float* ph_3d,   // (ims:ime, kms:kme+1, jms:jme)
        const float* mu_2d,   // (ims:ime, jms:jme)
        const float* p_3d) {  // (ims:ime, kms:kme, jms:jme)
        
        int ni = ite_ - its_ + 1;
        int nj = jte_ - jts_ + 1;
        int nk = kte_ - kts_ + 1;
        
        // Convert each variable
        auto u_torch = fortran_to_torch_3d(u_3d, ni, nk, nj, true, false, false);
        auto v_torch = fortran_to_torch_3d(v_3d, ni, nk, nj, false, true, false);
        auto w_torch = fortran_to_torch_3d(w_3d, ni, nk, nj, false, false, true);
        auto t_torch = fortran_to_torch_3d(t_3d, ni, nk, nj);
        auto ph_torch = fortran_to_torch_3d(ph_3d, ni, nk, nj, false, false, true);
        auto mu_torch = fortran_to_torch_2d(mu_2d, ni, nj);
        auto p_torch = fortran_to_torch_3d(p_3d, ni, nk, nj);
        
        // Stack into unified state vector
        // Shape will be [7, nj, nk, ni] for 3D vars
        // Need to handle 2D mu separately
        
        // First, expand mu to 3D by repeating in k dimension
        auto mu_3d_torch = mu_torch.unsqueeze(1).repeat({1, nk, 1});
        
        // Stack all variables
        auto state = torch::stack({
            u_torch.slice(2, 0, ni),    // Remove u staggering for mass points
            v_torch.slice(0, 0, nj),    // Remove v staggering
            w_torch.slice(1, 0, nk),    // Remove w staggering
            t_torch,
            ph_torch.slice(1, 0, nk),   // Remove ph staggering
            mu_3d_torch,
            p_torch
        }, 0);
        
        return state;
    }
    
    // Get Fortran index from (i,k,j) coordinates
    inline int fortran_index_3d(int i, int k, int j,
                               bool is_u_staggered = false,
                               bool is_w_staggered = false) {
        int i_stride = 1;
        int k_stride = (ime_ - ims_ + 1 + (is_u_staggered ? 1 : 0));
        int j_stride = k_stride * (kme_ - kms_ + 1 + (is_w_staggered ? 1 : 0));
        
        return (i - ims_) * i_stride + 
               (k - kms_) * k_stride + 
               (j - jms_) * j_stride;
    }
    
    // Get Fortran index from (i,j) coordinates
    inline int fortran_index_2d(int i, int j) {
        return (i - ims_) + (j - jms_) * (ime_ - ims_ + 1);
    }
};

/**
 * C-grid Staggered Grid Manager
 * Handles the complexity of WRF's staggered Arakawa C-grid
 */
class StaggeredGridManager {
private:
    // WRFMemoryConverter& converter_;  // Unused - commented out to avoid warnings
    
public:
    StaggeredGridManager(WRFMemoryConverter& /*converter*/) {}
    
    // Interpolate from u-points to mass points
    torch::Tensor u_to_mass(const torch::Tensor& u_staggered) {
        // u_staggered shape: [nj, nk, ni+1]
        // result shape: [nj, nk, ni]
        int nj = u_staggered.size(0);
        int nk = u_staggered.size(1);
        int ni = u_staggered.size(2) - 1;
        
        auto u_mass = torch::zeros({nj, nk, ni}, u_staggered.options());
        
        // Average: u_mass[j,k,i] = 0.5 * (u[j,k,i] + u[j,k,i+1])
        u_mass = 0.5f * (u_staggered.slice(2, 0, ni) + u_staggered.slice(2, 1, ni+1));
        
        return u_mass;
    }
    
    // Interpolate from v-points to mass points
    torch::Tensor v_to_mass(const torch::Tensor& v_staggered) {
        // v_staggered shape: [nj+1, nk, ni]
        // result shape: [nj, nk, ni]
        int nj = v_staggered.size(0) - 1;
        int nk = v_staggered.size(1);
        int ni = v_staggered.size(2);
        
        auto v_mass = torch::zeros({nj, nk, ni}, v_staggered.options());
        
        // Average: v_mass[j,k,i] = 0.5 * (v[j,k,i] + v[j+1,k,i])
        v_mass = 0.5f * (v_staggered.slice(0, 0, nj) + v_staggered.slice(0, 1, nj+1));
        
        return v_mass;
    }
    
    // Interpolate from w-points to mass points
    torch::Tensor w_to_mass(const torch::Tensor& w_staggered) {
        // w_staggered shape: [nj, nk+1, ni]
        // result shape: [nj, nk, ni]
        int nj = w_staggered.size(0);
        int nk = w_staggered.size(1) - 1;
        int ni = w_staggered.size(2);
        
        auto w_mass = torch::zeros({nj, nk, ni}, w_staggered.options());
        
        // Average: w_mass[j,k,i] = 0.5 * (w[j,k,i] + w[j,k+1,i])
        w_mass = 0.5f * (w_staggered.slice(1, 0, nk) + w_staggered.slice(1, 1, nk+1));
        
        return w_mass;
    }
    
    // Interpolate from mass points to u-points
    torch::Tensor mass_to_u(const torch::Tensor& scalar) {
        // scalar shape: [nj, nk, ni]
        // result shape: [nj, nk, ni+1]
        int nj = scalar.size(0);
        int nk = scalar.size(1);
        int ni = scalar.size(2);
        
        auto result = torch::zeros({nj, nk, ni+1}, scalar.options());
        
        // Interior points: average
        // FIX 2025-12-26: Use copy_() for in-place modification (slice/select=... rebinds temporary)
        result.slice(2, 1, ni).copy_(0.5f * (scalar.slice(2, 0, ni-1) + scalar.slice(2, 1, ni)));

        // Boundaries: extrapolate
        result.select(2, 0).copy_(scalar.select(2, 0));
        result.select(2, ni).copy_(scalar.select(2, ni-1));

        return result;
    }

    // Interpolate from mass points to v-points
    torch::Tensor mass_to_v(const torch::Tensor& scalar) {
        // scalar shape: [nj, nk, ni]
        // result shape: [nj+1, nk, ni]
        int nj = scalar.size(0);
        int nk = scalar.size(1);
        int ni = scalar.size(2);

        auto result = torch::zeros({nj+1, nk, ni}, scalar.options());

        // Interior points: average
        // FIX 2025-12-26: Use copy_() for in-place modification (slice/select=... rebinds temporary)
        result.slice(0, 1, nj).copy_(0.5f * (scalar.slice(0, 0, nj-1) + scalar.slice(0, 1, nj)));

        // Boundaries: extrapolate
        result.select(0, 0).copy_(scalar.select(0, 0));
        result.select(0, nj).copy_(scalar.select(0, nj-1));

        return result;
    }

    // Interpolate from mass points to w-points
    torch::Tensor mass_to_w(const torch::Tensor& scalar) {
        // scalar shape: [nj, nk, ni]
        // result shape: [nj, nk+1, ni]
        int nj = scalar.size(0);
        int nk = scalar.size(1);
        int ni = scalar.size(2);

        auto result = torch::zeros({nj, nk+1, ni}, scalar.options());

        // Interior points: average
        // FIX 2025-12-26: Use copy_() for in-place modification (slice/select=... rebinds temporary)
        result.slice(1, 1, nk).copy_(0.5f * (scalar.slice(1, 0, nk-1) + scalar.slice(1, 1, nk)));

        // Boundaries: special treatment for terrain-following coordinates
        result.select(1, 0).copy_(scalar.select(1, 0));    // Bottom
        result.select(1, nk).copy_(scalar.select(1, nk-1)); // Top

        return result;
    }
    
    // Interpolate velocities to corner points (i+1/2, j+1/2, k)
    torch::Tensor v_to_corners() {
        // Placeholder - implement proper corner interpolation
        // This would involve averaging v values at adjacent points
        return torch::zeros({1, 1, 1});
    }
    
    torch::Tensor u_to_corners() {
        // Placeholder - implement proper corner interpolation
        // This would involve averaging u values at adjacent points
        return torch::zeros({1, 1, 1});
    }
    
    // Interpolate mass point variables to corners
    torch::Tensor mass_to_corners(const torch::Tensor& scalar) {
        // scalar shape: [nj, nk, ni]
        // result shape: [nj+1, nk, ni+1] at corners
        int nj = scalar.size(0);
        int nk = scalar.size(1);
        int ni = scalar.size(2);
        
        auto result = torch::zeros({nj+1, nk, ni+1}, scalar.options());
        
        // Interior corners: average of 4 adjacent mass points
        // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
        for (int j = 1; j < nj; j++) {
            for (int i = 1; i < ni; i++) {
                result.index_put_({j, Slice(), i}, 0.25f * (
                    scalar.index({j-1, Slice(), i-1}) +
                    scalar.index({j-1, Slice(), i}) +
                    scalar.index({j, Slice(), i-1}) +
                    scalar.index({j, Slice(), i})
                ));
            }
        }

        // Handle boundaries (simplified - proper implementation would consider BCs)
        // Left/right edges
        // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
        for (int j = 0; j < nj+1; j++) {
            result.index_put_({j, Slice(), 0}, result.index({j, Slice(), 1}));
            result.index_put_({j, Slice(), ni}, result.index({j, Slice(), ni-1}));
        }

        // Top/bottom edges
        // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
        for (int i = 0; i < ni+1; i++) {
            result.index_put_({0, Slice(), i}, result.index({1, Slice(), i}));
            result.index_put_({nj, Slice(), i}, result.index({nj-1, Slice(), i}));
        }
        
        return result;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_MEMORY_LAYOUT_H