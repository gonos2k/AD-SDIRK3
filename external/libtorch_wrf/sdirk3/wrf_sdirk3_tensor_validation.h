#pragma once
/**
 * @file wrf_sdirk3_tensor_validation.h
 * @brief Validation layer for WRF-SDIRK3 tensor layout standards
 * @date 2025-08-05
 * 
 * Implements the tensor layout validation as specified in the 
 * WRF-SDIRK3 design document. Zero runtime cost in release builds.
 */

#include <torch/torch.h>
#include <cassert>
#include <string>
#include <sstream>

namespace WRF_SDIRK3 {

/**
 * @class TensorValidator
 * @brief Static validation methods for tensor layout standards
 * 
 * All validation is performed only in debug builds to ensure
 * zero runtime overhead in production.
 */
class TensorValidator {
public:
    /**
     * @brief Validate standard 3D tensor layout [j,k,i] = {ny, nz, nx}
     * @param tensor The tensor to validate
     * @param name Descriptive name for error messages
     * @param ny Expected north-south dimension
     * @param nz Expected vertical dimension
     * @param nx Expected east-west dimension
     * @throws std::runtime_error if validation fails (debug only)
     */
    static void validate_3d_layout(const torch::Tensor& tensor,
                                  const std::string& name,
                                  int ny, int nz, int nx) {
#ifdef DEBUG
        // Check dimensionality
        if (tensor.dim() != 3) {
            std::ostringstream msg;
            msg << "TensorValidator: " << name << " must be 3D, got " 
                << tensor.dim() << "D";
            throw std::runtime_error(msg.str());
        }
        
        // Check shape matches expected {ny, nz, nx}
        if (tensor.size(0) != ny || tensor.size(1) != nz || tensor.size(2) != nx) {
            std::ostringstream msg;
            msg << "TensorValidator: " << name << " shape mismatch. Expected {"
                << ny << "," << nz << "," << nx << "}, got {"
                << tensor.size(0) << "," << tensor.size(1) << "," 
                << tensor.size(2) << "}";
            throw std::runtime_error(msg.str());
        }
        
        // Verify stride pattern for contiguous tensors
        if (tensor.is_contiguous()) {
            // For {ny, nz, nx} shape with [j][k][i] indexing:
            // i-stride (innermost) should be 1
            // k-stride should be nx
            // j-stride should be nx*nz
            if (tensor.stride(2) != 1) {
                std::ostringstream msg;
                msg << "TensorValidator: " << name 
                    << " i-stride must be 1, got " << tensor.stride(2);
                throw std::runtime_error(msg.str());
            }
            
            if (tensor.stride(1) != nx) {
                std::ostringstream msg;
                msg << "TensorValidator: " << name 
                    << " k-stride must be " << nx << ", got " << tensor.stride(1);
                throw std::runtime_error(msg.str());
            }
            
            if (tensor.stride(0) != nx * nz) {
                std::ostringstream msg;
                msg << "TensorValidator: " << name 
                    << " j-stride must be " << (nx * nz) 
                    << ", got " << tensor.stride(0);
                throw std::runtime_error(msg.str());
            }
        }
#else
        // No validation in release builds
        (void)tensor;
        (void)name;
        (void)ny;
        (void)nz;
        (void)nx;
#endif
    }
    
    /**
     * @brief Validate U-staggered grid tensor {ny, nz, nx+1}
     */
    static void validate_staggered_u(const torch::Tensor& tensor,
                                    const std::string& name,
                                    int ny, int nz, int nx) {
        validate_3d_layout(tensor, name + "_U", ny, nz, nx + 1);
    }
    
    /**
     * @brief Validate V-staggered grid tensor {ny+1, nz, nx}
     */
    static void validate_staggered_v(const torch::Tensor& tensor,
                                    const std::string& name,
                                    int ny, int nz, int nx) {
        validate_3d_layout(tensor, name + "_V", ny + 1, nz, nx);
    }
    
    /**
     * @brief Validate W-staggered grid tensor {ny, nz+1, nx}
     */
    static void validate_staggered_w(const torch::Tensor& tensor,
                                    const std::string& name,
                                    int ny, int nz, int nx) {
        validate_3d_layout(tensor, name + "_W", ny, nz + 1, nx);
    }
    
    /**
     * @brief Validate 2D tensor layout {ny, nx}
     */
    static void validate_2d_layout(const torch::Tensor& tensor,
                                  const std::string& name,
                                  int ny, int nx) {
#ifdef DEBUG
        if (tensor.dim() != 2) {
            std::ostringstream msg;
            msg << "TensorValidator: " << name << " must be 2D, got " 
                << tensor.dim() << "D";
            throw std::runtime_error(msg.str());
        }
        
        if (tensor.size(0) != ny || tensor.size(1) != nx) {
            std::ostringstream msg;
            msg << "TensorValidator: " << name << " shape mismatch. Expected {"
                << ny << "," << nx << "}, got {"
                << tensor.size(0) << "," << tensor.size(1) << "}";
            throw std::runtime_error(msg.str());
        }
#else
        (void)tensor;
        (void)name;
        (void)ny;
        (void)nx;
#endif
    }
    
    /**
     * @brief Validate state vector dimensions
     * State vector contains all prognostic variables packed together
     */
    static void validate_state_vector(const torch::Tensor& state,
                                     int ny, int nz, int nx,
                                     int num_vars = 6) {
#ifdef DEBUG
        if (state.dim() != 1) {
            std::ostringstream msg;
            msg << "TensorValidator: state vector must be 1D, got " 
                << state.dim() << "D";
            throw std::runtime_error(msg.str());
        }
        
        // Calculate expected size based on staggered grids
        // U: ny*nz*(nx+1)
        // V: (ny+1)*nz*nx
        // W: ny*(nz+1)*nx
        // PH: ny*(nz+1)*nx
        // T: ny*nz*nx
        // MU: ny*nx
        int expected_size = ny*nz*(nx+1) +      // U
                           (ny+1)*nz*nx +       // V
                           ny*(nz+1)*nx +       // W
                           ny*(nz+1)*nx +       // PH
                           ny*nz*nx +           // T
                           ny*nx;               // MU
        
        if (state.size(0) != expected_size) {
            std::ostringstream msg;
            msg << "TensorValidator: state vector size mismatch. Expected "
                << expected_size << ", got " << state.size(0);
            throw std::runtime_error(msg.str());
        }
#else
        (void)state;
        (void)ny;
        (void)nz;
        (void)nx;
        (void)num_vars;
#endif
    }
};

} // namespace WRF_SDIRK3