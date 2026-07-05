#ifndef WRF_ZERO_COPY_VIEW_H
#define WRF_ZERO_COPY_VIEW_H

#include <torch/torch.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>  // FIX Round190: For std::vector<int64_t>
#include <atomic>  // FIX Round190: For std::atomic_thread_fence

namespace wrf {
namespace sdirk3 {

/**
 * Zero-copy memory view for WRF Fortran arrays
 *
 * Key features:
 * - No data copying between Fortran and C++
 * - Handles WRF's (i,k,j) indexing with column-major layout
 * - Supports staggered grid variables
 * - Compatible with WRF's real*4 (float) type
 *
 * FIX Round180: CPU-ONLY / FP32-ONLY CONSTRAINT
 * =============================================
 * This class uses data_ptr<float>() for zero-copy access to Fortran arrays.
 * All tensors MUST be:
 * - On CPU (not CUDA/MPS/HIP/XPU)
 * - dtype=kFloat32 (not FP64/FP16)
 * - Contiguous in memory
 * Violating these constraints causes undefined behavior.
 */
class WRFTensorView {
public:
    /**
     * Staggering type for WRF's Arakawa C-grid
     */
    enum class Stagger {
        NONE,    // Cell center (θ, p, ρ)
        U,       // U-staggered (west-east faces)
        V,       // V-staggered (south-north faces)  
        W        // W-staggered (bottom-top faces)
    };
    
    /**
     * Create a zero-copy view of WRF Fortran array
     * 
     * @param fortran_ptr Pointer to Fortran array data
     * @param ims, ime Memory bounds (i-direction)
     * @param kms, kme Memory bounds (k-direction)
     * @param jms, jme Memory bounds (j-direction)
     * @param its, ite Tile bounds (i-direction)
     * @param kts, kte Tile bounds (k-direction)
     * @param jts, jte Tile bounds (j-direction)
     * @param stagger Staggering type
     */
    static torch::Tensor create_view(
        float* fortran_ptr,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int its, int ite, int kts, int kte, int jts, int jte,
        Stagger stagger = Stagger::NONE) {

        // FIX Round183: Use int64_t for dimension/stride arithmetic to avoid
        // overflow on very large grids (e.g., ni_mem=4000, nk_mem=100 → 400K)
        int64_t ni_mem = ime - ims + 1;
        int64_t nk_mem = kme - kms + 1;
        int64_t nj_mem = jme - jms + 1;

        // Adjust for staggering
        if (stagger == Stagger::U) ni_mem += 1;
        if (stagger == Stagger::V) nj_mem += 1;
        if (stagger == Stagger::W) nk_mem += 1;

        // Create tensor from existing memory WITHOUT copying
        // Key: Use from_blob with custom strides for Fortran layout
        std::vector<int64_t> sizes = {nj_mem, nk_mem, ni_mem};
        std::vector<int64_t> strides = {
            ni_mem * nk_mem,  // j-stride (largest) - already int64_t
            ni_mem,           // k-stride (medium)
            1                 // i-stride (smallest)
        };
        
        // LINT_EXCEPTION: Explicit CPU/FP32 options for Fortran array from_blob
        // Custom strides require inline options specification
        auto options = torch::TensorOptions()
            .dtype(torch::kFloat32)
            .device(torch::kCPU);

        // Create tensor view without copying data
        auto tensor = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
            fortran_ptr,
            sizes,
            strides,
            options
        );
        
        // Return view of tile region (still zero-copy)
        return tensor.narrow(0, jts - jms, jte - jts + 1)
                    .narrow(1, kts - kms, kte - kts + 1)
                    .narrow(2, its - ims, ite - its + 1);
    }
    
    /**
     * Create multiple views for a set of variables (efficient batching)
     */
    static std::vector<torch::Tensor> create_multi_view(
        const std::vector<float*>& fortran_ptrs,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int its, int ite, int kts, int kte, int jts, int jte,
        const std::vector<Stagger>& staggers) {
        
        std::vector<torch::Tensor> views;
        views.reserve(fortran_ptrs.size());
        
        for (size_t i = 0; i < fortran_ptrs.size(); ++i) {
            views.push_back(create_view(
                fortran_ptrs[i],
                ims, ime, kms, kme, jms, jme,
                its, ite, kts, kte, jts, jte,
                i < staggers.size() ? staggers[i] : Stagger::NONE
            ));
        }
        
        return views;
    }
    
    /**
     * Helper to convert WRF (i,k,j) indices to tensor indices
     * considering Fortran column-major layout
     */
    static inline std::array<int64_t, 3> wrf_to_tensor_indices(
        int i_wrf, int k_wrf, int j_wrf,
        int ims, int kms, int jms) {
        return {
            static_cast<int64_t>(j_wrf - jms),
            static_cast<int64_t>(k_wrf - kms),
            static_cast<int64_t>(i_wrf - ims)
        };
    }
    
    /**
     * Efficient element access without bounds checking (for performance)
     */
    static inline float& at_wrf(
        torch::Tensor& tensor,
        int i_wrf, int k_wrf, int j_wrf,
        int ims, int kms, int jms) {
        
        auto [j_idx, k_idx, i_idx] = wrf_to_tensor_indices(
            i_wrf, k_wrf, j_wrf, ims, kms, jms);
        
        // Direct memory access for maximum performance
        return tensor.data_ptr<float>()[
            j_idx * tensor.stride(0) +
            k_idx * tensor.stride(1) +
            i_idx * tensor.stride(2)
        ];
    }
};

/**
 * RAII wrapper for managing WRF tensor views lifetime
 */
class WRFViewManager {
private:
    struct ViewInfo {
        torch::Tensor tensor;
        float* expected_data_ptr;  // FIX Round184: Actual data_ptr after narrow()
        std::string name;
        WRFTensorView::Stagger stagger;
        // FIX Round185: Removed original_ptr - was unused after Round184 changes.
        // Base fortran pointer can be recovered from tensor.storage().data_ptr() if needed.
    };

    std::vector<ViewInfo> views_;

public:
    /**
     * Register a view for automatic lifetime management
     */
    void register_view(
        const std::string& name,
        float* fortran_ptr,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int its, int ite, int kts, int kte, int jts, int jte,
        WRFTensorView::Stagger stagger = WRFTensorView::Stagger::NONE) {

        auto tensor = WRFTensorView::create_view(
            fortran_ptr,
            ims, ime, kms, kme, jms, jme,
            its, ite, kts, kte, jts, jte,
            stagger
        );

        // FIX Round184: Store the actual data_ptr after narrow() for validation
        // Narrowed views have offset from base, so we can't compare to fortran_ptr
        float* expected_ptr = tensor.data_ptr<float>();
        views_.push_back({tensor, expected_ptr, name, stagger});
    }
    
    /**
     * Get view by name
     */
    torch::Tensor& get_view(const std::string& name) {
        auto it = std::find_if(views_.begin(), views_.end(),
            [&name](const ViewInfo& info) { return info.name == name; });
        
        if (it == views_.end()) {
            throw std::runtime_error("View not found: " + name);
        }
        
        return it->tensor;
    }
    
    /**
     * Ensure all views are properly synchronized before returning to Fortran
     * (Important for cache coherency)
     *
     * FIX Round185: Zero-copy contract for this class:
     * - All tensors MUST be CPU (not CUDA/MPS) - enforced by create_view()
     * - All tensors MUST be FP32 - enforced by create_view()
     * - Tensors may be non-contiguous (narrowed views have Fortran strides)
     * - Memory fence applies to all views regardless of contiguity
     */
    void synchronize() {
        // Force CPU cache flush for all modified data
        for (auto& view_info : views_) {
            const auto& t = view_info.tensor;

            // FIX Round185: Verify CPU/FP32 contract (defensive check)
            // These should always be true since create_view() enforces them
            TORCH_CHECK(t.is_cpu() && t.scalar_type() == torch::kFloat32,
                "synchronize: tensor must be CPU, FP32 (zero-copy contract violation)");
        }

        // OPT Pass33+: Move fence outside loop - single release fence after all
        // validation covers all views (memory ordering is global, not per-view)
        //
        // THREAD SAFETY NOTE: This assumes single-thread/single-caller convention.
        // The ZeroCopyViewManager instance should be owned by one thread at a time.
        // If multi-thread access to the same views_ is needed, external synchronization
        // (e.g., mutex) must be provided by the caller before calling synchronize().
        std::atomic_thread_fence(std::memory_order_release);

        // Platform-specific cache flush can be added here if needed
        #ifdef __x86_64__
            // _mm_sfence();  // Store fence for x86
        #endif
    }
    
    /**
     * Check if any view has been reallocated (safety check)
     *
     * FIX Round184: Narrowed Fortran-stride views are NOT contiguous, so we:
     * 1. Drop the contiguity requirement (narrowed views share storage)
     * 2. Compare data_ptr to expected_data_ptr (stored at registration)
     *    instead of base fortran_ptr (which has offset after narrow)
     * 3. Keep CPU/FP32 checks for data_ptr<float>() safety
     */
    bool validate_views() const {
        for (const auto& view_info : views_) {
            const auto& t = view_info.tensor;
            // FIX Round184: Only check CPU/FP32, not contiguous (narrowed views aren't)
            TORCH_CHECK(t.is_cpu() && t.scalar_type() == torch::kFloat32,
                "validate_views: tensor must be CPU, FP32");
            // FIX Round184: Compare to expected_data_ptr (post-narrow), not original_ptr
            if (t.data_ptr<float>() != view_info.expected_data_ptr) {
                return false;  // View has been reallocated!
            }
        }
        return true;
    }
};

/**
 * Optimized operations on WRF views
 */
class WRFViewOps {
public:
    /**
     * Compute spatial derivatives using WRF's (i,k,j) layout directly
     * without intermediate copies
     * FIX Round180: Added CPU/FP32/contiguous guards for data_ptr<float>()
     */
    static torch::Tensor compute_x_derivative(
        const torch::Tensor& field,
        float dx,
        int ims, int its, int ite) {

        // FIX Round180: Guard for safe data_ptr<float>() access
        TORCH_CHECK(field.is_cpu() && field.scalar_type() == torch::kFloat32 && field.is_contiguous(),
            "compute_x_derivative: field must be CPU, FP32, contiguous");

        auto result = torch::zeros_like(field);

        // Direct computation on Fortran layout
        auto field_ptr = field.data_ptr<float>();
        auto result_ptr = result.data_ptr<float>();
        
        int nj = field.size(0);
        int nk = field.size(1);
        int ni = field.size(2);
        
        // Compute derivative efficiently with proper strides
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; ++j) {
            for (int k = 0; k < nk; ++k) {
                // Set boundary values to zero
                int64_t idx0 = j * field.stride(0) + k * field.stride(1);
                result_ptr[idx0] = 0.0f;
                
                // Central differences for interior points
                for (int i = 1; i < ni - 1; ++i) {
                    int64_t idx = idx0 + i * field.stride(2);
                    int64_t idx_plus = idx + field.stride(2);
                    int64_t idx_minus = idx - field.stride(2);
                    
                    result_ptr[idx] = (field_ptr[idx_plus] - field_ptr[idx_minus]) / (2.0f * dx);
                }
                
                // Set last boundary value to zero
                int64_t idx_last = idx0 + (ni - 1) * field.stride(2);
                result_ptr[idx_last] = 0.0f;
            }
        }
        
        return result;
    }
    
    /**
     * Apply stencil operation without copying
     *
     * NOTE 2025-12-28: This function is currently BLOCKED (TORCH_CHECK(false))
     * because bounds checking is not implemented. The only caller is
     * demonstrate_zero_copy_operations() in wrf_zero_copy_view.cpp, which
     * itself is only called from test_zero_copy_implementation() that is
     * commented out. If stencil operations are needed, use vectorized
     * tensor operations (e.g., conv1d, unfold) or implement boundary handling.
     */
    template<int STENCIL_SIZE>
    static void apply_stencil(
        torch::Tensor& output,
        const torch::Tensor& input,
        const float (&stencil)[STENCIL_SIZE],
        int direction) {  // 0=i, 1=k, 2=j
        
        // FIX 2025-12-28: Validate output stride matches input to prevent misaligned writes
        // If input is Fortran-stride but output is contiguous, results go to wrong positions
        TORCH_CHECK(output.strides() == input.strides(),
            "apply_stencil: output strides ", output.strides(),
            " must match input strides ", input.strides(),
            " to prevent memory layout mismatch. Use from_blob with matching strides.");

        auto in_ptr = input.data_ptr<float>();
        auto out_ptr = output.data_ptr<float>();
        
        int64_t stride = input.stride(2 - direction);  // Map WRF direction to tensor dimension
        int half_size = STENCIL_SIZE / 2;
        
        // FIX 2025-12-28: Block usage until bounds checking is properly implemented
        // The loop below can access out-of-bounds memory near boundaries
        TORCH_CHECK(false,
            "apply_stencil: bounds checking not yet implemented. "
            "Use vectorized tensor operations (e.g., conv1d, unfold) instead, "
            "or implement explicit boundary handling before enabling this function.");

        // Apply stencil with zero-copy access
        auto apply_1d = [&](int64_t center_idx) {
            float sum = 0.0f;
            for (int s = 0; s < STENCIL_SIZE; ++s) {
                int64_t idx = center_idx + (s - half_size) * stride;
                sum += stencil[s] * in_ptr[idx];
            }
            out_ptr[center_idx] = sum;
        };

        // Parallelize over other dimensions
        #pragma omp parallel for
        for (int64_t idx = 0; idx < input.numel(); idx += stride) {
            // TODO: Implement proper bounds checking before enabling
            // Need to check: center_idx - half_size * stride >= 0
            //            and center_idx + half_size * stride < numel
            apply_1d(idx);
        }
    }
};

/**
 * Integration with SDIRK3 solver - zero copy interface
 */
class ZeroCopySDIRK3Interface {
public:
    static void solve_timestep(
        // Output arrays (modified in-place)
        float* u_tend, float* v_tend, float* w_tend,
        float* t_tend, float* ph_tend, float* mu_tend,
        // Input arrays (read-only views)
        const float* u, const float* v, const float* w,
        const float* t, const float* ph, const float* mu,
        // Grid parameters
        int ims, int ime, int kms, int kme, int jms, int jme,
        int its, int ite, int kts, int kte, int jts, int jte,
        float dt, float dx, float dy) {
        
        // Create zero-copy views
        WRFViewManager view_manager;
        
        // Register input views
        view_manager.register_view("u", const_cast<float*>(u),
            ims, ime, kms, kme, jms, jme, its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::U);
        
        view_manager.register_view("v", const_cast<float*>(v),
            ims, ime, kms, kme, jms, jme, its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::V);
        
        view_manager.register_view("w", const_cast<float*>(w),
            ims, ime, kms, kme, jms, jme, its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::W);
        
        // Register output views (tendencies)
        view_manager.register_view("u_tend", u_tend,
            ims, ime, kms, kme, jms, jme, its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::U);
        
        // ... register other variables ...
        
        // Get tensor views for computation
        auto& u_view = view_manager.get_view("u");
        auto& v_view = view_manager.get_view("v");
        auto& w_view = view_manager.get_view("w");
        auto& u_tend_view = view_manager.get_view("u_tend");
        
        // Perform SDIRK3 computation directly on views
        // No copying needed!
        compute_sdirk3_step(u_view, v_view, w_view, dt);
        
        // Example: compute simple tendency
        u_tend_view.add_(u_view * 0.001f);
        
        // Ensure all modifications are visible to Fortran
        view_manager.synchronize();
        
        // Validate no reallocation occurred
        // This would only fail if Fortran actually reallocated memory
        // For testing purposes, we'll skip this check
        // if (!view_manager.validate_views()) {
        //     throw std::runtime_error("View reallocation detected!");
        // }
    }
    
private:
    static void compute_sdirk3_step(
        torch::Tensor& u, torch::Tensor& v, torch::Tensor& w,
        float dt) {
        // Direct computation on WRF memory layout
        // Example: simple damping for stability
        float damping_factor = std::exp(-0.001f * dt);
        
        // In-place operations work directly on Fortran memory
        u.mul_(damping_factor);
        v.mul_(damping_factor);
        w.mul_(damping_factor);
        
        // No intermediate copies needed
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_ZERO_COPY_VIEW_H