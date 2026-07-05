#ifndef MEMORY_LAYOUT_CONVERTER_H
#define MEMORY_LAYOUT_CONVERTER_H

#include "torch_standalone.h"
#include "cpu_optimization.h"  // FIX Round194: For CHECK_CPU_CONTIGUOUS_FLOAT32 macro
#include <cstring>
#include <omp.h>

namespace wrf {
namespace sdirk3 {

// Memory layout converter between Fortran (column-major) and C++ (row-major)
class MemoryLayoutConverter {
public:
    // Convert 3D Fortran array (i,k,j) to C++ tensor (j,k,i)
    // Fortran: array(ims:ime, kms:kme, jms:jme) 
    // C++:     tensor[j-jms][k-kms][i-ims]
    static torch::Tensor fortran_to_tensor_3d(
        const float* fortran_data,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int ips, int ipe, int kps, int kpe, int jps, int jpe) {
        
        // Calculate dimensions
        int ni = ipe - ips + 1;
        int nk = kpe - kps + 1;
        int nj = jpe - jps + 1;
        
        // Memory strides for Fortran array
        int fi_stride = 1;
        int fk_stride = ime - ims + 1;
        int fj_stride = fk_stride * (kme - kms + 1);
        
        // Create output tensor
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        torch::Tensor tensor = torch::empty({nj, nk, ni}, options);
        float* tensor_data = tensor.data_ptr<float>();
        
        // Parallel conversion with cache-friendly access pattern
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; ++j) {
            for (int k = 0; k < nk; ++k) {
                // Process i-dimension with SIMD
                #pragma omp simd
                for (int i = 0; i < ni; ++i) {
                    // Fortran index
                    int fi = (ips + i) - ims;
                    int fk = (kps + k) - kms;
                    int fj = (jps + j) - jms;
                    int f_idx = fi * fi_stride + fk * fk_stride + fj * fj_stride;
                    
                    // C++ tensor index
                    int t_idx = j * nk * ni + k * ni + i;
                    
                    tensor_data[t_idx] = fortran_data[f_idx];
                }
            }
        }
        
        return tensor;
    }
    
    // Convert C++ tensor back to Fortran array
    static void tensor_to_fortran_3d(
        const torch::Tensor& tensor,
        float* fortran_data,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int ips, int ipe, int kps, int kpe, int jps, int jpe) {
        
        // FIX Round194: Use Float32-aware check for data_ptr<float>() safety
        CHECK_CPU_CONTIGUOUS_FLOAT32(tensor);

        // Calculate dimensions
        int ni = ipe - ips + 1;
        int nk = kpe - kps + 1;
        int nj = jpe - jps + 1;

        TORCH_CHECK(tensor.size(0) == nj && tensor.size(1) == nk && tensor.size(2) == ni,
                    "Tensor dimensions don't match Fortran array bounds");

        const float* tensor_data = tensor.data_ptr<float>();
        
        // Memory strides for Fortran array
        int fi_stride = 1;
        int fk_stride = ime - ims + 1;
        int fj_stride = fk_stride * (kme - kms + 1);
        
        // Parallel conversion
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; ++j) {
            for (int k = 0; k < nk; ++k) {
                #pragma omp simd
                for (int i = 0; i < ni; ++i) {
                    // C++ tensor index
                    int t_idx = j * nk * ni + k * ni + i;
                    
                    // Fortran index
                    int fi = (ips + i) - ims;
                    int fk = (kps + k) - kms;
                    int fj = (jps + j) - jms;
                    int f_idx = fi * fi_stride + fk * fk_stride + fj * fj_stride;
                    
                    fortran_data[f_idx] = tensor_data[t_idx];
                }
            }
        }
    }
    
    // Convert 2D Fortran array (i,j) to C++ tensor (j,i)
    static torch::Tensor fortran_to_tensor_2d(
        const float* fortran_data,
        int ims, int ime, int jms, int jme,
        int ips, int ipe, int jps, int jpe) {
        
        int ni = ipe - ips + 1;
        int nj = jpe - jps + 1;
        
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        torch::Tensor tensor = torch::empty({nj, ni}, options);
        float* tensor_data = tensor.data_ptr<float>();
        
        // Memory strides for Fortran array
        int fi_stride = 1;
        int fj_stride = ime - ims + 1;
        
        #pragma omp parallel for
        for (int j = 0; j < nj; ++j) {
            #pragma omp simd
            for (int i = 0; i < ni; ++i) {
                int fi = (ips + i) - ims;
                int fj = (jps + j) - jms;
                int f_idx = fi * fi_stride + fj * fj_stride;
                int t_idx = j * ni + i;
                
                tensor_data[t_idx] = fortran_data[f_idx];
            }
        }
        
        return tensor;
    }
    
    // Convert unified state with multiple variables
    // Pack WRF arrays into a single tensor with shape [n_points, n_vars]
    static torch::Tensor pack_wrf_state(
        const float* rho, const float* u, const float* v, const float* w,
        const float* theta, const float* mu, const float* phi,
        const float* moist, const float* scalar,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int ips, int ipe, int kps, int kpe, int jps, int jpe,
        int n_moist, int n_scalar) {
        
        int ni = ipe - ips + 1;
        int nk = kpe - kps + 1;
        int nj = jpe - jps + 1;
        int n_points = ni * nk * nj;
        int n_vars = 7 + n_moist + n_scalar; // rho,u,v,w,theta,mu,phi + species
        
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        torch::Tensor state = torch::zeros({n_points, n_vars}, options);
        float* state_data = state.data_ptr<float>();
        
        // Memory strides for Fortran arrays
        int fi_stride = 1;
        int fk_stride = ime - ims + 1;
        int fj_stride = fk_stride * (kme - kms + 1);
        
        // Pack 3D variables in parallel
        #pragma omp parallel for
        for (int idx = 0; idx < n_points; ++idx) {
            // Convert linear index to (i,j,k)
            int i = idx % ni;
            int k = (idx / ni) % nk;
            int j = idx / (ni * nk);
            
            // Fortran indices
            int fi = (ips + i) - ims;
            int fk = (kps + k) - kms;
            int fj = (jps + j) - jms;
            int f3d_idx = fi * fi_stride + fk * fk_stride + fj * fj_stride;
            
            // Pack variables
            state_data[idx * n_vars + 0] = rho[f3d_idx];
            state_data[idx * n_vars + 1] = u[f3d_idx];
            state_data[idx * n_vars + 2] = v[f3d_idx];
            state_data[idx * n_vars + 3] = w[f3d_idx];
            state_data[idx * n_vars + 4] = theta[f3d_idx];
            state_data[idx * n_vars + 5] = 0.0f; // mu is 2D, handle separately
            state_data[idx * n_vars + 6] = phi[f3d_idx];
            
            // Pack moisture variables
            for (int m = 0; m < n_moist; ++m) {
                int moist_idx = m * (ime-ims+1) * (kme-kms+1) * (jme-jms+1) + f3d_idx;
                state_data[idx * n_vars + 7 + m] = moist[moist_idx];
            }
            
            // Pack scalar variables
            for (int s = 0; s < n_scalar; ++s) {
                int scalar_idx = s * (ime-ims+1) * (kme-kms+1) * (jme-jms+1) + f3d_idx;
                state_data[idx * n_vars + 7 + n_moist + s] = scalar[scalar_idx];
            }
        }
        
        // Handle 2D mu variable
        int fi_stride_2d = 1;
        int fj_stride_2d = ime - ims + 1;
        
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; ++j) {
            for (int i = 0; i < ni; ++i) {
                int fi = (ips + i) - ims;
                int fj = (jps + j) - jms;
                int f2d_idx = fi * fi_stride_2d + fj * fj_stride_2d;
                
                // Set mu for all k levels at this (i,j)
                for (int k = 0; k < nk; ++k) {
                    int idx = j * nk * ni + k * ni + i;
                    state_data[idx * n_vars + 5] = mu[f2d_idx];
                }
            }
        }
        
        return state;
    }
    
    // Unpack tensor back to WRF arrays
    static void unpack_wrf_state(
        const torch::Tensor& state,
        float* rho, float* u, float* v, float* w,
        float* theta, float* mu, float* phi,
        float* moist, float* scalar,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int ips, int ipe, int kps, int kpe, int jps, int jpe,
        int n_moist, int n_scalar) {
        
        // FIX Round194: Use Float32-aware check for data_ptr<float>() safety
        CHECK_CPU_CONTIGUOUS_FLOAT32(state);

        int ni = ipe - ips + 1;
        int nk = kpe - kps + 1;
        int nj = jpe - jps + 1;
        int n_points = ni * nk * nj;
        int n_vars = 7 + n_moist + n_scalar;

        TORCH_CHECK(state.size(0) == n_points && state.size(1) == n_vars,
                    "State tensor dimensions don't match");

        const float* state_data = state.data_ptr<float>();
        
        // Memory strides
        int fi_stride = 1;
        int fk_stride = ime - ims + 1;
        int fj_stride = fk_stride * (kme - kms + 1);
        
        // Unpack in parallel
        #pragma omp parallel for
        for (int idx = 0; idx < n_points; ++idx) {
            int i = idx % ni;
            int k = (idx / ni) % nk;
            int j = idx / (ni * nk);
            
            int fi = (ips + i) - ims;
            int fk = (kps + k) - kms;
            int fj = (jps + j) - jms;
            int f3d_idx = fi * fi_stride + fk * fk_stride + fj * fj_stride;
            
            rho[f3d_idx] = state_data[idx * n_vars + 0];
            u[f3d_idx] = state_data[idx * n_vars + 1];
            v[f3d_idx] = state_data[idx * n_vars + 2];
            w[f3d_idx] = state_data[idx * n_vars + 3];
            theta[f3d_idx] = state_data[idx * n_vars + 4];
            phi[f3d_idx] = state_data[idx * n_vars + 6];
            
            // Unpack moisture
            for (int m = 0; m < n_moist; ++m) {
                int moist_idx = m * (ime-ims+1) * (kme-kms+1) * (jme-jms+1) + f3d_idx;
                moist[moist_idx] = state_data[idx * n_vars + 7 + m];
            }
            
            // Unpack scalars
            for (int s = 0; s < n_scalar; ++s) {
                int scalar_idx = s * (ime-ims+1) * (kme-kms+1) * (jme-jms+1) + f3d_idx;
                scalar[scalar_idx] = state_data[idx * n_vars + 7 + n_moist + s];
            }
        }
        
        // Extract 2D mu (use k=0 slice)
        int fi_stride_2d = 1;
        int fj_stride_2d = ime - ims + 1;
        
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; ++j) {
            for (int i = 0; i < ni; ++i) {
                int idx = j * nk * ni + 0 * ni + i; // k=0
                int fi = (ips + i) - ims;
                int fj = (jps + j) - jms;
                int f2d_idx = fi * fi_stride_2d + fj * fj_stride_2d;
                
                mu[f2d_idx] = state_data[idx * n_vars + 5];
            }
        }
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // MEMORY_LAYOUT_CONVERTER_H