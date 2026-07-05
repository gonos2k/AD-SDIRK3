#ifndef WRF_SDIRK3_ZERO_COPY_INTERFACE_H
#define WRF_SDIRK3_ZERO_COPY_INTERFACE_H

#include <torch/torch.h>
#include <stdexcept>
#include <string>
#include <vector>  // FIX Round190: For std::vector<int64_t>

namespace wrf {
namespace sdirk3 {

/**
 * Zero-Copy Interface with Defensive Coding
 * According to approved design v6.0
 * 
 * CRITICAL: Memory layout (j,k,i) is already correct - DO NOT CHANGE
 */
class ZeroCopyInterface {
public:
    /**
     * Create PyTorch tensor from Fortran array WITHOUT copying
     * Uses defensive coding as required by design
     */
    static torch::Tensor from_fortran_3d(
        float* fortran_data,
        int ni, int nk, int nj,
        torch::Device device = torch::kCPU) {
        
        // IMPLEMENTATION NOTE: Defensive checks for robustness
        if (fortran_data == nullptr) {
            throw std::runtime_error("ZeroCopyInterface: Fortran data pointer is null");
        }
        
        if (ni <= 0 || nk <= 0 || nj <= 0) {
            throw std::runtime_error(
                "ZeroCopyInterface: Invalid dimensions - ni=" + std::to_string(ni) + 
                " nk=" + std::to_string(nk) + " nj=" + std::to_string(nj));
        }
        
        // Create zero-copy tensor with correct strides for (j,k,i) layout
        // Fortran memory: data[i][k][j] (column-major)
        // PyTorch view: tensor[j][k][i] (row-major view)
        std::vector<int64_t> sizes = {nj, nk, ni};
        // FIX Round191: Cast BEFORE multiply to prevent int overflow on large grids
        std::vector<int64_t> strides = {
            static_cast<int64_t>(ni) * nk,   // j-stride (cast first to avoid int overflow)
            static_cast<int64_t>(ni),        // k-stride
            static_cast<int64_t>(1)          // i-stride
        };
        
        // FIX 2025-12-30 Batch34 Issue 3: Explicitly use CPU device for from_blob
        // Host pointer MUST be wrapped with CPU options, then moved to target device
        // This prevents accidental GPU interpretation of host memory
        auto tensor = torch::from_blob(  // LINT_EXCEPTION: Inline CPU opts below
            fortran_data,
            sizes,
            strides,
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)
        );

        // Move to device if needed (this will copy)
        if (device != torch::kCPU) {
            tensor = tensor.to(device);
        }

        return tensor;
    }
    
    /**
     * Copy PyTorch tensor back to Fortran array
     * FIXED: Stride-preserving copy with contiguous check
     *
     * FIX Round171: FP64→FP32 Downcast Behavior Documentation
     * =========================================================
     * This function accepts both FP32 and FP64 input tensors, but ALWAYS writes
     * FP32 data to the Fortran array (since WRF uses single precision by default).
     *
     * IMPORTANT: If input tensor is FP64, it will be SILENTLY DOWNCAST to FP32.
     * This means precision loss occurs without warning. Callers expecting FP64
     * output should NOT use this function - use a separate FP64 interface instead.
     *
     * Design Rationale:
     * - WRF's Fortran arrays are float* (single precision)
     * - PyTorch computations may use FP64 for numerical stability
     * - Downcast is intentional to match WRF's memory layout expectations
     * - No runtime warning is issued because this is expected behavior
     *
     * If you need FP64→FP64 transfer, create a separate to_fortran_3d_double()
     * function that takes double* fortran_data parameter.
     */
    static void to_fortran_3d(
        const torch::Tensor& tensor,
        float* fortran_data,
        int ni, int nk, int nj) {

        // FIX Round170: Add NoGradGuard - data transfer, not computation
        torch::NoGradGuard no_grad;

        // Defensive checks
        if (fortran_data == nullptr) {
            throw std::runtime_error("ZeroCopyInterface: Fortran data pointer is null");
        }

        if (tensor.dim() != 3) {
            throw std::runtime_error("ZeroCopyInterface: Tensor must be 3D");
        }

        if (tensor.size(0) != nj || tensor.size(1) != nk || tensor.size(2) != ni) {
            throw std::runtime_error("ZeroCopyInterface: Dimension mismatch");
        }

        // FIX Round170: Ensure FP32 or FP64 dtype before conversion
        // NOTE: FP64 input is DOWNCAST to FP32 - see function documentation above
        TORCH_CHECK(tensor.scalar_type() == torch::kFloat32 || tensor.scalar_type() == torch::kFloat64,
            "to_fortran_3d: expected FP32 or FP64 tensor, got ", tensor.scalar_type());

        // FIXED: Ensure CPU, FP32, and contiguous before copy
        // FP64 tensors are downcast here - this is intentional per WRF's float* interface
        auto cpu_tensor = tensor.to(torch::kCPU, torch::kFloat32).contiguous();

        // FIX Round170: Use size_t for byte count to prevent overflow
        // Example: 1000 * 100 * 1000 * 4 = 400MB - safe in size_t but overflows int
        size_t total_elements = static_cast<size_t>(ni) * nk * nj;
        size_t byte_count = total_elements * sizeof(float);

        // Verify expected memory layout
        auto strides = cpu_tensor.strides();
        int64_t expected_stride_0 = static_cast<int64_t>(ni) * nk;
        bool is_standard_layout = (strides[0] == expected_stride_0 &&
                                   strides[1] == ni &&
                                   strides[2] == 1);

        if (is_standard_layout) {
            // Direct memory copy is safe
            std::memcpy(fortran_data,
                       cpu_tensor.data_ptr<float>(),
                       byte_count);
        } else {
            // FIXED: Element-wise copy preserving Fortran order
            auto accessor = cpu_tensor.accessor<float, 3>();
            for (int j = 0; j < nj; ++j) {
                for (int k = 0; k < nk; ++k) {
                    for (int i = 0; i < ni; ++i) {
                        // FIX Round170: Use size_t for index calculation
                        size_t fortran_idx = static_cast<size_t>(i) +
                                            static_cast<size_t>(ni) * k +
                                            static_cast<size_t>(ni) * nk * j;
                        fortran_data[fortran_idx] = accessor[j][k][i];
                    }
                }
            }
        }
    }
    
    /**
     * Create 2D tensor from Fortran array (for mu_d, surface fields)
     */
    static torch::Tensor from_fortran_2d(
        float* fortran_data,
        int ni, int nj,
        torch::Device device = torch::kCPU) {
        
        if (fortran_data == nullptr) {
            throw std::runtime_error("ZeroCopyInterface: Fortran data pointer is null");
        }
        
        if (ni <= 0 || nj <= 0) {
            throw std::runtime_error(
                "ZeroCopyInterface: Invalid 2D dimensions - ni=" + std::to_string(ni) + 
                " nj=" + std::to_string(nj));
        }
        
        std::vector<int64_t> sizes = {nj, ni};
        std::vector<int64_t> strides = {
            static_cast<int64_t>(ni),
            static_cast<int64_t>(1)
        };

        // FIX 2025-12-30 Batch34 Issue 3: Explicitly use CPU device for from_blob
        auto tensor = torch::from_blob(  // LINT_EXCEPTION: Inline CPU opts below
            fortran_data,
            sizes,
            strides,
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)
        );

        if (device != torch::kCPU) {
            tensor = tensor.to(device);
        }

        return tensor;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_ZERO_COPY_INTERFACE_H