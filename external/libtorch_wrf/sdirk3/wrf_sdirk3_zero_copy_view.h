/**
 * @file wrf_sdirk3_zero_copy_view.h
 * @brief Zero-copy tensor views for efficient Fortran-C++ interface
 * @date 2025-08-05
 * 
 * Provides centralized, error-free creation of tensor views from
 * Fortran arrays, handling all stride calculations and staggered
 * grid complexities automatically.
 * 
 * Based on design document: (20250805)WRF-SDIRK3-design.md
 */

#ifndef WRF_SDIRK3_ZERO_COPY_VIEW_H
#define WRF_SDIRK3_ZERO_COPY_VIEW_H

#include <torch/torch.h>
#include <stdexcept>
#include <string>
#include <vector>  // FIX Round190: For std::vector<int64_t>

namespace WRF_SDIRK3 {

/**
 * @brief WRF dimension structure matching Fortran conventions
 */
struct WRFDimensions {
    // Memory dimensions (includes halos)
    int ims, ime;  // i memory start/end
    int jms, jme;  // j memory start/end
    int kms, kme;  // k memory start/end
    
    // Tile dimensions (computational region)
    int its, ite;  // i tile start/end
    int jts, jte;  // j tile start/end
    int kts, kte;  // k tile start/end
    
    // Domain dimensions (global for this MPI task)
    int ids, ide;  // i domain start/end
    int jds, jde;  // j domain start/end
    int kds, kde;  // k domain start/end
    
    // Computed tile sizes
    int nx() const { return ite - its + 1; }
    int ny() const { return jte - jts + 1; }
    int nz() const { return kte - kts + 1; }
    
    // Memory sizes
    int ni_mem() const { return ime - ims + 1; }
    int nj_mem() const { return jme - jms + 1; }
    int nk_mem() const { return kme - kms + 1; }
};

/**
 * @brief Grid variable types in WRF
 */
enum class GridVariable {
    SCALAR,    // Cell-centered (θ, p, μ, etc.)
    U_STAG,    // U-velocity (staggered in x)
    V_STAG,    // V-velocity (staggered in y)  
    W_STAG,    // W-velocity (staggered in z)
    CORNER     // Corner points (rare)
};

/**
 * @brief Staggering type for automatic detection
 */
enum class StaggerType {
    NONE,      // Cell-centered
    X_STAG,    // Staggered in X (U-points)
    Y_STAG,    // Staggered in Y (V-points)
    Z_STAG,    // Staggered in Z (W-points)
    XY_STAG,   // Staggered in X and Y (vorticity points)
    XZ_STAG,   // Staggered in X and Z
    YZ_STAG,   // Staggered in Y and Z
    XYZ_STAG   // Staggered in all directions
};

/**
 * @class ZeroCopyTensorView
 * @brief Centralized zero-copy tensor view creation from Fortran arrays
 * 
 * This class eliminates manual stride calculations and ensures correct
 * tensor creation for all WRF array types. It handles the complexity
 * of Fortran column-major to C++ row-major layout conversion.
 */
class ZeroCopyTensorView {
private:
    /**
     * @brief Calculate offset into Fortran array
     */
    static int calculate_fortran_offset(const WRFDimensions& dims,
                                       GridVariable var_type) {
        int ni = dims.ni_mem();
        int nk = dims.nk_mem();
        
        // Fortran arrays are (ims:ime, kms:kme, jms:jme)
        // Offset = (j-jms)*ni*nk + (k-kms)*ni + (i-ims)
        int i_offset = dims.its - dims.ims;
        int j_offset = dims.jts - dims.jms;
        int k_offset = dims.kts - dims.kms;
        
        // Adjust for staggering
        switch (var_type) {
            case GridVariable::U_STAG:
                // U is dimensioned (ims:ime+1, kms:kme, jms:jme)
                ni += 1;
                break;
            case GridVariable::V_STAG:
                // V is dimensioned (ims:ime, kms:kme, jms:jme+1)
                // No change to ni, but different interpretation
                break;
            case GridVariable::W_STAG:
                // W is dimensioned (ims:ime, kms:kme+1, jms:jme)
                nk += 1;
                break;
            default:
                break;
        }
        
        return j_offset * ni * nk + k_offset * ni + i_offset;
    }
    
    /**
     * @brief Get tensor shape for given variable type
     */
    static std::vector<int64_t> get_tensor_shape(const WRFDimensions& dims,
                                                 GridVariable var_type) {
        int nx = dims.nx();
        int ny = dims.ny();
        int nz = dims.nz();
        
        switch (var_type) {
            case GridVariable::U_STAG:
                return {ny, nz, nx + 1};
            case GridVariable::V_STAG:
                return {ny + 1, nz, nx};
            case GridVariable::W_STAG:
                return {ny, nz + 1, nx};
            default:
                return {ny, nz, nx};
        }
    }
    
    /**
     * @brief Calculate strides for zero-copy view
     *
     * WRF Fortran arrays are (i,k,j) with i varying fastest
     * We want C++ tensors as [j,k,i] with i varying fastest
     *
     * FIX Round183: Use int64_t for stride arithmetic to avoid overflow
     * on very large grids (e.g., ni_mem=4000, nk_mem=100 → 400K elements per plane)
     */
    static std::vector<int64_t> calculate_strides(const WRFDimensions& dims,
                                                  GridVariable var_type) {
        int64_t ni = dims.ni_mem();
        int64_t nk = dims.nk_mem();

        // Adjust for staggering
        switch (var_type) {
            case GridVariable::U_STAG:
                ni += 1;
                break;
            case GridVariable::W_STAG:
                nk += 1;
                break;
            default:
                break;
        }

        // Fortran layout: (i,k,j)
        // C++ view: [j,k,i]
        // j-stride = ni * nk (jump entire i-k plane)
        // k-stride = ni (jump entire i-line)
        // i-stride = 1 (contiguous)

        return {ni * nk, ni, 1};
    }
    
public:
    /**
     * @brief Create zero-copy tensor view from WRF Fortran array
     *
     * @param fortran_ptr Pointer to start of Fortran array
     * @param dims WRF dimension structure
     * @param var_type Type of grid variable
     * @param options Tensor options (dtype only; device forced to CPU)
     * @return Zero-copy tensor view in standard [j,k,i] layout
     *
     * FIX Round183: Force CPU regardless of options.device() to prevent
     * invalid host-pointer usage if caller accidentally passes GPU device.
     */
    static torch::Tensor create_from_wrf_array(
        float* fortran_ptr,
        const WRFDimensions& dims,
        GridVariable var_type,
        torch::TensorOptions options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)) {

        if (!fortran_ptr) {
            throw std::invalid_argument("Null pointer passed to create_from_wrf_array");
        }

        // Calculate offset to tile start
        int offset = calculate_fortran_offset(dims, var_type);

        // Get shape and strides
        auto shape = get_tensor_shape(dims, var_type);
        auto strides = calculate_strides(dims, var_type);

        // FIX Round183: Force CPU device for host pointer from_blob
        // Preserve dtype from options but override device to CPU
        auto cpu_options = torch::TensorOptions()
            .dtype(options.dtype())
            .device(torch::kCPU);

        // Create zero-copy view
        return torch::from_blob(fortran_ptr + offset, shape, strides, cpu_options);  // LINT_EXCEPTION: CPU opts above
    }
    
    /**
     * @brief Create staggered tensor view with automatic detection
     *
     * @param fortran_ptr Pointer to Fortran array
     * @param dims WRF dimensions
     * @param stagger Staggering type
     * @param options Tensor options (dtype only; device forced to CPU)
     * @return Zero-copy tensor view
     *
     * FIX Round183: Force CPU regardless of options.device()
     */
    static torch::Tensor create_staggered(
        float* fortran_ptr,
        const WRFDimensions& dims,
        StaggerType stagger,
        torch::TensorOptions options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)) {
        
        // Map stagger type to grid variable
        GridVariable var_type;
        switch (stagger) {
            case StaggerType::NONE:
                var_type = GridVariable::SCALAR;
                break;
            case StaggerType::X_STAG:
                var_type = GridVariable::U_STAG;
                break;
            case StaggerType::Y_STAG:
                var_type = GridVariable::V_STAG;
                break;
            case StaggerType::Z_STAG:
                var_type = GridVariable::W_STAG;
                break;
            default:
                throw std::invalid_argument("Unsupported stagger type");
        }
        
        return create_from_wrf_array(fortran_ptr, dims, var_type, options);
    }
    
    /**
     * @brief Create 2D tensor view (for surface fields)
     *
     * @param fortran_ptr Pointer to 2D Fortran array
     * @param dims WRF dimensions
     * @param options Tensor options (dtype only; device forced to CPU)
     * @return Zero-copy 2D tensor view [j,i]
     *
     * FIX Round183: Force CPU regardless of options.device()
     */
    static torch::Tensor create_2d_from_wrf(
        float* fortran_ptr,
        const WRFDimensions& dims,
        torch::TensorOptions options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)) {

        if (!fortran_ptr) {
            throw std::invalid_argument("Null pointer passed to create_2d_from_wrf");
        }

        // FIX Round183: Use int64_t for stride arithmetic
        int64_t ni = dims.ni_mem();
        int nx = dims.nx();
        int ny = dims.ny();

        // 2D Fortran arrays are (ims:ime, jms:jme)
        int i_offset = dims.its - dims.ims;
        int j_offset = dims.jts - dims.jms;
        int64_t offset = static_cast<int64_t>(j_offset) * ni + i_offset;

        // FIX Round183: Force CPU device for host pointer from_blob
        auto cpu_options = torch::TensorOptions()
            .dtype(options.dtype())
            .device(torch::kCPU);

        // Shape: [j, i]
        // Strides: [ni, 1]
        return torch::from_blob(fortran_ptr + offset,  // LINT_EXCEPTION: CPU opts above
                               {ny, nx},
                               {ni, 1},
                               cpu_options);
    }
    
    /**
     * @brief Verify that a tensor is truly zero-copy
     *
     * @param tensor Tensor to verify (must be CPU/Float32)
     * @param original_ptr Original Fortran pointer
     * @param expected_offset Expected offset from base
     * @return True if tensor uses same memory
     */
    static bool verify_zero_copy(const torch::Tensor& tensor,
                                const float* original_ptr,
                                int expected_offset = 0) {
        if (!tensor.defined() || !original_ptr) {
            return false;
        }

        // FIX Round194: Validate CPU/FP32 for data_ptr<float>()
        // Zero-copy is only possible with CPU tensors from host pointers
        if (!tensor.is_cpu() || tensor.scalar_type() != torch::kFloat32) {
            return false;  // Cannot be zero-copy if not CPU/FP32
        }

        // Check if data pointers match
        const float* tensor_ptr = tensor.data_ptr<float>();
        return (tensor_ptr == original_ptr + expected_offset);
    }
    
    /**
     * @brief Create tensor with validation
     * 
     * Same as create_from_wrf_array but with built-in validation
     */
    static torch::Tensor create_validated(
        float* fortran_ptr,
        const WRFDimensions& dims,
        GridVariable var_type,
        const std::string& var_name,
        torch::TensorOptions options = torch::kFloat32) {
        
        auto tensor = create_from_wrf_array(fortran_ptr, dims, var_type, options);
        
        // Validate shape
        auto expected_shape = get_tensor_shape(dims, var_type);
        if (tensor.sizes() != torch::IntArrayRef(expected_shape)) {
            throw std::runtime_error("Shape mismatch for " + var_name);
        }
        
        // Validate zero-copy
        int offset = calculate_fortran_offset(dims, var_type);
        if (!verify_zero_copy(tensor, fortran_ptr, offset)) {
            throw std::runtime_error("Zero-copy verification failed for " + var_name);
        }
        
        return tensor;
    }
    
    /**
     * @brief Get readable description of tensor layout
     */
    static std::string describe_layout(const torch::Tensor& tensor) {
        std::stringstream ss;
        ss << "Tensor shape: " << tensor.sizes() << "\n";
        ss << "Tensor strides: " << tensor.strides() << "\n";
        ss << "Is contiguous: " << tensor.is_contiguous() << "\n";
        ss << "Data pointer: " << tensor.data_ptr() << "\n";
        ss << "Device: " << tensor.device() << "\n";
        ss << "Dtype: " << tensor.dtype() << "\n";
        return ss.str();
    }
    
    /**
     * @brief Batch creation of multiple views
     * 
     * Useful for creating all state variables at once
     */
    struct TensorSet {
        torch::Tensor u, v, w;
        torch::Tensor theta, p, phi;
        torch::Tensor mu;  // 2D
    };
    
    static TensorSet create_state_tensors(
        float* u_ptr, float* v_ptr, float* w_ptr,
        float* theta_ptr, float* p_ptr, float* phi_ptr,
        float* mu_ptr,
        const WRFDimensions& dims,
        torch::TensorOptions options = torch::kFloat32) {
        
        TensorSet tensors;
        tensors.u = create_from_wrf_array(u_ptr, dims, GridVariable::U_STAG, options);
        tensors.v = create_from_wrf_array(v_ptr, dims, GridVariable::V_STAG, options);
        tensors.w = create_from_wrf_array(w_ptr, dims, GridVariable::W_STAG, options);
        tensors.theta = create_from_wrf_array(theta_ptr, dims, GridVariable::SCALAR, options);
        tensors.p = create_from_wrf_array(p_ptr, dims, GridVariable::SCALAR, options);
        tensors.phi = create_from_wrf_array(phi_ptr, dims, GridVariable::W_STAG, options);
        tensors.mu = create_2d_from_wrf(mu_ptr, dims, options);
        
        return tensors;
    }
};

} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_ZERO_COPY_VIEW_H