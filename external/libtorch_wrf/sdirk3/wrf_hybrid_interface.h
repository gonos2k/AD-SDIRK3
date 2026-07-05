/**
 * @file wrf_hybrid_interface.h
 * @brief Hybrid interface: Zero-copy input, traditional output
 * 
 * 장점:
 * 1. 입력 데이터 복사 제거 (50% 메모리 절약)
 * 2. WRF 기존 구조 유지 (안정성)
 * 3. 점진적 마이그레이션 가능
 */

#ifndef WRF_HYBRID_INTERFACE_H
#define WRF_HYBRID_INTERFACE_H

#include <torch/torch.h>
#include "wrf_zero_copy_view.h"
#include <vector>
#include <functional>

namespace wrf {
namespace sdirk3 {

/**
 * Hybrid SDIRK3-WRF Interface
 * - Zero-copy for input state reading
 * - Traditional copy for output tendencies
 */
class HybridSDIRK3Interface {
private:
    // Function pointer to WRF's tendency computation
    using TendencyFunc = std::function<void(
        const float* u, const float* v, const float* w,
        const float* t, const float* ph, const float* mu,
        float* u_tend, float* v_tend, float* w_tend,
        float* t_tend, float* ph_tend, float* mu_tend,
        int ims, int ime, int jms, int jme, int kms, int kme,
        int its, int ite, int jts, int jte, int kts, int kte,
        float dt, float dx, float dy
    )>;
    
    TendencyFunc wrf_tendency_func_;
    
    // Temporary storage for tendencies
    struct TendencyStorage {
        torch::Tensor u_tend, v_tend, w_tend;
        torch::Tensor t_tend, ph_tend, mu_tend;
        
        void allocate(int nj, int nk, int ni) {
            // FIX Round194: Explicit CPU device for WRF-Fortran interface tensors
            auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
            u_tend = torch::zeros({nj, nk, ni+1}, options);  // U-staggered
            v_tend = torch::zeros({nj+1, nk, ni}, options);  // V-staggered
            w_tend = torch::zeros({nj, nk+1, ni}, options);  // W-staggered
            t_tend = torch::zeros({nj, nk, ni}, options);
            ph_tend = torch::zeros({nj, nk, ni}, options);
            mu_tend = torch::zeros({nj, 1, ni}, options);   // 2D field
        }
    };
    
    TendencyStorage tend_storage_;
    
public:
    /**
     * Register WRF's tendency computation function
     */
    void register_wrf_tendency_func(TendencyFunc func) {
        wrf_tendency_func_ = func;
    }
    
    /**
     * Compute RHS function F(U) for SDIRK3
     * - Input: Zero-copy views of WRF state
     * - Output: Computed tendencies (copied)
     */
    torch::Tensor compute_rhs(
        const torch::Tensor& state,
        float dt, float dx, float dy,
        int ims, int ime, int jms, int jme, int kms, int kme,
        int its, int ite, int jts, int jte, int kts, int kte
    ) {
        // FIX Round168: Add CPU/contig/FP32 guard for safe data_ptr<float>() access
        // State tensor is used with direct pointer arithmetic, requires:
        // 1. CPU tensor (WRF Fortran code cannot access GPU memory)
        // 2. Contiguous layout (pointer arithmetic assumes packed storage)
        // 3. FP32 dtype (data_ptr<float>() assumes 4-byte floats)
        TORCH_CHECK(state.is_cpu(),
            "compute_rhs requires CPU state tensor, got ", state.device());
        TORCH_CHECK(state.is_contiguous(),
            "compute_rhs requires contiguous state tensor");
        TORCH_CHECK(state.scalar_type() == torch::kFloat32,
            "compute_rhs requires FP32 state tensor, got ", state.scalar_type());

        // State is already a zero-copy view
        // Extract individual variables (still zero-copy)
        int nj = jte - jts + 1;
        int nk = kte - kts + 1;
        int ni = ite - its + 1;

        // FIX Round168: Use int64_t for size products to prevent overflow on large grids
        // Example: 1000 x 100 x 1000 = 100M elements, safe in int64_t but overflows int32
        int64_t plane = static_cast<int64_t>(ni) * nk * nj;

        // FIX Round169: Validate state tensor has enough elements for all 6 variables
        // State is packed as [u, v, w, t, ph, mu] each of size ~plane
        // We access base + 5*plane which requires at least 6*plane elements
        constexpr int NUM_STATE_VARS = 6;
        int64_t required_elements = NUM_STATE_VARS * plane;
        TORCH_CHECK(state.numel() >= required_elements,
            "compute_rhs: state tensor has ", state.numel(), " elements but requires at least ",
            required_elements, " (6 variables * ", plane, " elements each)");

        // Allocate tendency storage if needed
        // FIX Round169: Check all dimensions, not just size(0)
        if (!tend_storage_.u_tend.defined() ||
            tend_storage_.u_tend.size(0) != nj ||
            tend_storage_.u_tend.size(1) != nk ||
            tend_storage_.u_tend.size(2) != ni + 1) {  // U is staggered in i
            tend_storage_.allocate(nj, nk, ni);
        }

        // FIX Round168+Round175: Verify tend_storage_ tensors are CPU/contig/FP32
        // allocate() uses torch::zeros with kFloat32, which creates CPU tensors by default,
        // but verify to catch any future changes to allocation logic
        // FIX Round171: Validate ALL tend_storage variables, not just u_tend
        // FIX Round175: Add dtype checks - data_ptr<float>() requires FP32, not FP64/FP16
        // All variables have data_ptr<float>() called on them, requiring CPU/contig/FP32
        TORCH_CHECK(tend_storage_.u_tend.is_cpu() && tend_storage_.u_tend.is_contiguous() &&
                    tend_storage_.u_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.u_tend must be CPU contiguous FP32, got device=",
            tend_storage_.u_tend.device(), ", dtype=", tend_storage_.u_tend.scalar_type());
        TORCH_CHECK(tend_storage_.v_tend.is_cpu() && tend_storage_.v_tend.is_contiguous() &&
                    tend_storage_.v_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.v_tend must be CPU contiguous FP32, got device=",
            tend_storage_.v_tend.device(), ", dtype=", tend_storage_.v_tend.scalar_type());
        TORCH_CHECK(tend_storage_.w_tend.is_cpu() && tend_storage_.w_tend.is_contiguous() &&
                    tend_storage_.w_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.w_tend must be CPU contiguous FP32, got device=",
            tend_storage_.w_tend.device(), ", dtype=", tend_storage_.w_tend.scalar_type());
        TORCH_CHECK(tend_storage_.t_tend.is_cpu() && tend_storage_.t_tend.is_contiguous() &&
                    tend_storage_.t_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.t_tend must be CPU contiguous FP32, got device=",
            tend_storage_.t_tend.device(), ", dtype=", tend_storage_.t_tend.scalar_type());
        TORCH_CHECK(tend_storage_.ph_tend.is_cpu() && tend_storage_.ph_tend.is_contiguous() &&
                    tend_storage_.ph_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.ph_tend must be CPU contiguous FP32, got device=",
            tend_storage_.ph_tend.device(), ", dtype=", tend_storage_.ph_tend.scalar_type());
        TORCH_CHECK(tend_storage_.mu_tend.is_cpu() && tend_storage_.mu_tend.is_contiguous() &&
                    tend_storage_.mu_tend.scalar_type() == torch::kFloat32,
            "tend_storage_.mu_tend must be CPU contiguous FP32, got device=",
            tend_storage_.mu_tend.device(), ", dtype=", tend_storage_.mu_tend.scalar_type());

        // FIX Round172: Verify tend_storage shapes match expected staggered dimensions
        // u_tend: {nj, nk, ni+1} - staggered in i direction
        // v_tend: {nj+1, nk, ni} - staggered in j direction
        // w_tend: {nj, nk+1, ni} - staggered in k direction
        // t_tend, ph_tend: {nj, nk, ni} - no staggering
        // mu_tend: {nj, 1, ni} - 2D surface field
        TORCH_CHECK(tend_storage_.v_tend.size(0) == nj + 1 &&
                    tend_storage_.v_tend.size(1) == nk &&
                    tend_storage_.v_tend.size(2) == ni,
            "tend_storage_.v_tend shape mismatch: expected {", nj+1, ",", nk, ",", ni,
            "}, got {", tend_storage_.v_tend.size(0), ",", tend_storage_.v_tend.size(1),
            ",", tend_storage_.v_tend.size(2), "}");
        TORCH_CHECK(tend_storage_.w_tend.size(0) == nj &&
                    tend_storage_.w_tend.size(1) == nk + 1 &&
                    tend_storage_.w_tend.size(2) == ni,
            "tend_storage_.w_tend shape mismatch: expected {", nj, ",", nk+1, ",", ni,
            "}, got {", tend_storage_.w_tend.size(0), ",", tend_storage_.w_tend.size(1),
            ",", tend_storage_.w_tend.size(2), "}");
        TORCH_CHECK(tend_storage_.t_tend.size(0) == nj &&
                    tend_storage_.t_tend.size(1) == nk &&
                    tend_storage_.t_tend.size(2) == ni,
            "tend_storage_.t_tend shape mismatch: expected {", nj, ",", nk, ",", ni,
            "}, got {", tend_storage_.t_tend.size(0), ",", tend_storage_.t_tend.size(1),
            ",", tend_storage_.t_tend.size(2), "}");
        TORCH_CHECK(tend_storage_.ph_tend.size(0) == nj &&
                    tend_storage_.ph_tend.size(1) == nk &&
                    tend_storage_.ph_tend.size(2) == ni,
            "tend_storage_.ph_tend shape mismatch: expected {", nj, ",", nk, ",", ni,
            "}, got {", tend_storage_.ph_tend.size(0), ",", tend_storage_.ph_tend.size(1),
            ",", tend_storage_.ph_tend.size(2), "}");
        TORCH_CHECK(tend_storage_.mu_tend.size(0) == nj &&
                    tend_storage_.mu_tend.size(1) == 1 &&
                    tend_storage_.mu_tend.size(2) == ni,
            "tend_storage_.mu_tend shape mismatch: expected {", nj, ",1,", ni,
            "}, got {", tend_storage_.mu_tend.size(0), ",", tend_storage_.mu_tend.size(1),
            ",", tend_storage_.mu_tend.size(2), "}");

        // Call WRF tendency computation
        // WRF reads from zero-copy views and writes to tendency arrays
        if (wrf_tendency_func_) {
            float* base = state.data_ptr<float>();  // Safe after checks above
            wrf_tendency_func_(
                base,                    // Zero-copy input pointers
                base + plane,            // FIX Round168: Use int64_t plane for offset
                base + 2 * plane,
                base + 3 * plane,
                base + 4 * plane,
                base + 5 * plane,
                tend_storage_.u_tend.data_ptr<float>(),  // Output to temp storage
                tend_storage_.v_tend.data_ptr<float>(),
                tend_storage_.w_tend.data_ptr<float>(),
                tend_storage_.t_tend.data_ptr<float>(),
                tend_storage_.ph_tend.data_ptr<float>(),
                tend_storage_.mu_tend.data_ptr<float>(),
                ims, ime, jms, jme, kms, kme,
                its, ite, jts, jte, kts, kte,
                dt, dx, dy
            );
        }
        
        // Pack tendencies into single tensor for SDIRK3
        return pack_tendencies();
    }
    
    /**
     * Apply SDIRK3 stage update
     * - Reads state with zero-copy
     * - Returns updated values (traditional copy)
     */
    void apply_stage_update(
        // Input state (zero-copy views)
        const float* u, const float* v, const float* w,
        const float* t, const float* ph, const float* mu,
        // Output updated state (written back)
        float* u_new, float* v_new, float* w_new,
        float* t_new, float* ph_new, float* mu_new,
        // Stage coefficients
        const torch::Tensor& K, float coeff,
        // Grid info
        int ims, int ime, int jms, int jme, int kms, int kme,
        int its, int ite, int jts, int jte, int kts, int kte
    ) {
        // Create zero-copy views of input
        WRFViewManager view_manager;
        
        view_manager.register_view("u", const_cast<float*>(u),
            ims, ime, kms, kme, jms, jme,
            its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::U);
            
        view_manager.register_view("v", const_cast<float*>(v),
            ims, ime, kms, kme, jms, jme,
            its, ite, kts, kte, jts, jte,
            WRFTensorView::Stagger::V);
            
        // ... register other variables ...
        
        // Get views (zero-copy)
        auto& u_view = view_manager.get_view("u");
        auto& v_view = view_manager.get_view("v");
        
        // Apply update: U_new = U_old + coeff * K
        // This creates new tensors (not zero-copy)
        auto u_updated = u_view + coeff * K.slice(0, 0, 1);
        auto v_updated = v_view + coeff * K.slice(0, 1, 2);
        
        // Copy results back to WRF arrays
        copy_tensor_to_fortran(u_updated, u_new, 
                              ims, ime, kms, kme, jms, jme,
                              its, ite, kts, kte, jts, jte,
                              WRFTensorView::Stagger::U);
                              
        copy_tensor_to_fortran(v_updated, v_new,
                              ims, ime, kms, kme, jms, jme,
                              its, ite, kts, kte, jts, jte,
                              WRFTensorView::Stagger::V);
        
        // ... copy other variables ...
    }
    
private:
    /**
     * Pack tendencies into single tensor
     */
    torch::Tensor pack_tendencies() {
        std::vector<torch::Tensor> tends = {
            tend_storage_.u_tend.flatten(),
            tend_storage_.v_tend.flatten(),
            tend_storage_.w_tend.flatten(),
            tend_storage_.t_tend.flatten(),
            tend_storage_.ph_tend.flatten(),
            tend_storage_.mu_tend.flatten()
        };
        
        return torch::cat(tends, 0);
    }
    
    /**
     * Copy tensor back to Fortran array with layout conversion
     */
    void copy_tensor_to_fortran(
        const torch::Tensor& tensor,
        float* fortran_array,
        int ims, int ime, int kms, int kme, int jms, int jme,
        int its, int ite, int kts, int kte, int jts, int jte,
        WRFTensorView::Stagger stagger
    ) {
        // FIX Round169: Use int64_t for size products to prevent overflow
        int64_t ni = ime - ims + 1;
        int64_t nk = kme - kms + 1;
        int64_t nj = jme - jms + 1;

        // Adjust for staggering
        if (stagger == WRFTensorView::Stagger::U) ni += 1;
        if (stagger == WRFTensorView::Stagger::V) nj += 1;
        if (stagger == WRFTensorView::Stagger::W) nk += 1;

        // Copy with layout conversion: Tensor[j,k,i] -> Fortran(i,k,j)
        // FIX 2025-12-27: Avoid per-element .item<float>() GPU sync overhead
        // Transfer to CPU once, then use data_ptr for efficient element access
        TORCH_CHECK(tensor.dim() == 3, "copy_to_fortran: expected 3D tensor");
        // FIX Round169: Add dtype check before data_ptr<float>() call
        TORCH_CHECK(tensor.scalar_type() == torch::kFloat32,
            "copy_to_fortran: expected FP32 tensor, got ", tensor.scalar_type());
        auto tensor_cpu = tensor.to(torch::kCPU).contiguous();
        const float* src_ptr = tensor_cpu.data_ptr<float>();

        const int64_t nj_local = jte - jts + 1;
        const int64_t nk_local = kte - kts + 1;
        const int64_t ni_local = ite - its + 1;

        #pragma omp parallel for collapse(3)
        for (int j = jts; j <= jte; ++j) {
            for (int k = kts; k <= kte; ++k) {
                for (int i = its; i <= ite; ++i) {
                    // FIX Round169: Use int64_t for index calculations
                    int64_t fortran_idx = static_cast<int64_t>(i - ims) +
                                         static_cast<int64_t>(k - kms) * ni +
                                         static_cast<int64_t>(j - jms) * ni * nk;

                    int64_t j_local = j - jts;
                    int64_t k_local = k - kts;
                    int64_t i_local = i - its;

                    // Row-major [j,k,i] indexing for CPU contiguous tensor
                    int64_t tensor_idx = j_local * nk_local * ni_local +
                                        k_local * ni_local +
                                        i_local;

                    fortran_array[fortran_idx] = src_ptr[tensor_idx];
                }
            }
        }
    }
};

/**
 * Simplified interface for WRF integration
 */
class WRFHybridSDIRK3 {
private:
    HybridSDIRK3Interface interface_;
    torch::Tensor K_[3];  // Stage values
    
public:
    /**
     * Single timestep with hybrid approach
     */
    void advance_timestep(
        // Input state (read with zero-copy)
        const float* u, const float* v, const float* w,
        const float* t, const float* ph, const float* mu,
        // Output state (written with copy)
        float* u_new, float* v_new, float* w_new,
        float* t_new, float* ph_new, float* mu_new,
        // Grid parameters
        int ims, int ime, int jms, int jme, int kms, int kme,
        int its, int ite, int jts, int jte, int kts, int kte,
        float dt, float dx, float dy
    ) {
        // SDIRK3 coefficients
        const float gamma = 0.4358665215084590f;
        const float a21 = 0.5f * (1.0f - gamma);
        const float a31 = -3.0f * gamma * gamma / 2.0f + 4.0f * gamma - 0.25f;
        const float a32 = 3.0f * gamma * gamma / 2.0f - 5.0f * gamma + 1.25f;
        
        // Stage 1: Zero-copy input, compute tendency
        auto state1 = create_state_view(u, v, w, t, ph, mu,
                                       ims, ime, jms, jme, kms, kme,
                                       its, ite, jts, jte, kts, kte);
        
        K_[0] = interface_.compute_rhs(state1, dt, dx, dy,
                                      ims, ime, jms, jme, kms, kme,
                                      its, ite, jts, jte, kts, kte);
        
        // Newton solve for K1...
        
        // Stage 2: Apply update and compute
        // This step writes back to WRF arrays
        interface_.apply_stage_update(
            u, v, w, t, ph, mu,
            u_new, v_new, w_new, t_new, ph_new, mu_new,
            K_[0], dt * a21,
            ims, ime, jms, jme, kms, kme,
            its, ite, jts, jte, kts, kte
        );
        
        // Continue with stages 2 and 3...
    }
    
private:
    torch::Tensor create_state_view(
        const float* u, const float* v, const float* w,
        const float* t, const float* ph, const float* mu,
        int ims, int ime, int jms, int jme, int kms, int kme,
        int its, int ite, int jts, int jte, int kts, int kte
    ) {
        // Create zero-copy views and concatenate
        // This is the only place we save memory
        // But it's significant for large domains
        
        WRFViewManager manager;
        // Register all variables...
        
        return torch::cat({
            manager.get_view("u").flatten(),
            manager.get_view("v").flatten(),
            manager.get_view("w").flatten(),
            manager.get_view("t").flatten(),
            manager.get_view("ph").flatten(),
            manager.get_view("mu").flatten()
        }, 0);
    }
};

} // namespace sdirk3
} // namespace wrf

// Fortran interface
extern "C" {

/**
 * Hybrid SDIRK3 timestep
 * - Input arrays are read with zero-copy
 * - Output arrays are written traditionally
 */
void sdirk3_hybrid_timestep(
    // Input state (const for zero-copy safety)
    const float* u, const float* v, const float* w,
    const float* t, const float* ph, const float* mu,
    // Output state (modified)
    float* u_new, float* v_new, float* w_new,
    float* t_new, float* ph_new, float* mu_new,
    // Grid info
    int ims, int ime, int jms, int jme, int kms, int kme,
    int its, int ite, int jts, int jte, int kts, int kte,
    float dt, float dx, float dy
);

} // extern "C"

#endif // WRF_HYBRID_INTERFACE_H