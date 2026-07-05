#ifndef WRF_MEMORY_LAYOUT_ADAPTER_H
#define WRF_MEMORY_LAYOUT_ADAPTER_H

#include <torch/torch.h>
#include <memory>
#include <cstring>

namespace wrf {
namespace sdirk3 {

/**
 * WRF Memory Layout Adapter
 * 
 * Handles the critical memory layout transformation between:
 * - WRF Fortran: (i,k,j) column-major order
 * - C++ Torch:   row-major order, stored as (nk,nj,ni) for efficiency
 * 
 * Memory layout in WRF Fortran:
 * - i: east-west (fastest varying, contiguous in memory)
 * - k: vertical levels
 * - j: north-south (slowest varying)
 * 
 * Staggering on Arakawa C-grid:
 * - U: (i+1/2, j, k) - located at east/west cell faces
 * - V: (i, j+1/2, k) - located at north/south cell faces  
 * - W: (i, j, k+1/2) - located at top/bottom cell faces
 * - Scalars: (i, j, k) - cell centers
 */
class WRFMemoryLayoutAdapter {
public:
    struct GridDimensions {
        // WRF tile dimensions (computational domain)
        int its, ite;  // i-dimension tile bounds
        int jts, jte;  // j-dimension tile bounds
        int kts, kte;  // k-dimension tile bounds
        
        // WRF memory dimensions (includes halos)
        int ims, ime;  // i-dimension memory bounds
        int jms, jme;  // j-dimension memory bounds
        int kms, kme;  // k-dimension memory bounds
        
        // WRF domain dimensions (global)
        int ids, ide;  // i-dimension domain bounds
        int jds, jde;  // j-dimension domain bounds
        int kds, kde;  // k-dimension domain bounds
        
        // Computed sizes
        int ni() const { return ite - its + 1; }
        int nj() const { return jte - jts + 1; }
        int nk() const { return kte - kts + 1; }
        
        int ni_mem() const { return ime - ims + 1; }
        int nj_mem() const { return jme - jms + 1; }
        int nk_mem() const { return kme - kms + 1; }
    };
    
private:
    GridDimensions dims_;
    
    // Staggered grid dimensions - CORRECTED for (j,k,i) layout
    struct StaggeredDims {
        torch::IntArrayRef u_dims;  // (nj, nk, ni+1)
        torch::IntArrayRef v_dims;  // (nj+1, nk, ni)
        torch::IntArrayRef w_dims;  // (nj, nk+1, ni)
        torch::IntArrayRef scalar_dims;  // (nj, nk, ni)
    };
    StaggeredDims stagger_;
    
public:
    WRFMemoryLayoutAdapter(const GridDimensions& dims) : dims_(dims) {
        // Initialize staggered dimensions for torch tensors
        // CORRECTED: torch tensors use (j,k,i) layout for optimal performance
        // This preserves i-dimension contiguity for horizontal operations
        stagger_.scalar_dims = {dims.nj(), dims.nk(), dims.ni()};
        stagger_.u_dims = {dims.nj(), dims.nk(), dims.ni() + 1};
        stagger_.v_dims = {dims.nj() + 1, dims.nk(), dims.ni()};
        stagger_.w_dims = {dims.nj(), dims.nk() + 1, dims.ni()};
    }
    
    /**
     * Convert WRF Fortran array (i,k,j) to torch tensor (nk,nj,ni)
     * Zero-copy when possible, otherwise performs transpose
     */
    torch::Tensor from_wrf_3d(const float* wrf_data, 
                              bool is_u_staggered = false,
                              bool is_v_staggered = false,
                              bool is_w_staggered = false) const {
        
        int ni = dims_.ni();
        int nj = dims_.nj();
        int nk = dims_.nk();
        
        // Adjust for staggering
        if (is_u_staggered) ni++;
        if (is_v_staggered) nj++;
        if (is_w_staggered) nk++;
        
        // Create tensor with CORRECTED shape for C++ (nj,nk,ni)
        // This layout optimizes cache performance for horizontal operations
        auto tensor = torch::empty({nj, nk, ni}, torch::kFloat32);
        
        // Transpose from Fortran (i,k,j) to C++ (j,k,i)
        // WRF memory offset: i + k*ni + j*ni*nk
        // Torch memory offset: j*nk*ni + k*ni + i
        
        #pragma omp parallel for collapse(3)
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                for (int i = 0; i < ni; i++) {
                    // WRF Fortran index (column-major)
                    int wrf_idx = i + k * ni + j * ni * nk;
                    // Torch C++ index (row-major, j,k,i ordering)
                    tensor[j][k][i] = wrf_data[wrf_idx];
                }
            }
        }
        
        return tensor;
    }
    
    /**
     * Convert WRF Fortran 2D array (i,j) to torch tensor (nj,ni)
     */
    torch::Tensor from_wrf_2d(const float* wrf_data) const {
        int ni = dims_.ni();
        int nj = dims_.nj();
        
        auto tensor = torch::empty({nj, ni}, torch::kFloat32);
        
        #pragma omp parallel for collapse(2)
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                // WRF Fortran index: i + j*ni
                int wrf_idx = i + j * ni;
                tensor[j][i] = wrf_data[wrf_idx];
            }
        }
        
        return tensor;
    }
    
    /**
     * Convert torch tensor back to WRF Fortran array layout
     */
    void to_wrf_3d(const torch::Tensor& tensor, float* wrf_data,
                   bool is_u_staggered = false,
                   bool is_v_staggered = false,
                   bool is_w_staggered = false) const {
        
        int ni = dims_.ni();
        int nj = dims_.nj();
        int nk = dims_.nk();
        
        if (is_u_staggered) ni++;
        if (is_v_staggered) nj++;
        if (is_w_staggered) nk++;
        
        // Ensure tensor has CORRECTED shape
        TORCH_CHECK(tensor.sizes() == torch::IntArrayRef({nj, nk, ni}),
                   "Tensor shape mismatch for WRF conversion");

        // FIX Round193: accessor() is CPU-only, verify tensor is on CPU and correct dtype
        // FIX Round194: accessor() requires contiguous memory layout
        TORCH_CHECK(tensor.is_cpu(), "tensor_to_wrf: tensor must be on CPU for accessor()");
        TORCH_CHECK(tensor.is_contiguous(),
                   "tensor_to_wrf: tensor must be contiguous for accessor()");
        TORCH_CHECK(tensor.scalar_type() == torch::kFloat32,
                   "tensor_to_wrf: tensor must be Float32, got ", tensor.scalar_type());

        // Transpose from C++ (j,k,i) to Fortran (i,k,j)
        auto accessor = tensor.accessor<float, 3>();
        
        #pragma omp parallel for collapse(3)
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                for (int i = 0; i < ni; i++) {
                    int wrf_idx = i + k * ni + j * ni * nk;
                    wrf_data[wrf_idx] = accessor[j][k][i];
                }
            }
        }
    }
    
    /**
     * Create properly shaped tensor for WRF variable
     */
    torch::Tensor create_tensor_for_var(const std::string& var_name) const {
        if (var_name == "U" || var_name == "ru" || var_name == "ru_tend") {
            return torch::zeros(stagger_.u_dims, torch::kFloat32);
        } else if (var_name == "V" || var_name == "rv" || var_name == "rv_tend") {
            return torch::zeros(stagger_.v_dims, torch::kFloat32);
        } else if (var_name == "W" || var_name == "w" || var_name == "rw_tend") {
            return torch::zeros(stagger_.w_dims, torch::kFloat32);
        } else {
            // Default to scalar dimensions
            return torch::zeros(stagger_.scalar_dims, torch::kFloat32);
        }
    }
    
    /**
     * Optimized accessor for contiguous i-dimension operations
     * Returns a view that's efficient for horizontal operations
     */
    torch::Tensor get_i_contiguous_view(const torch::Tensor& tensor) const {
        // For operations along i-dimension (east-west)
        // Tensor is already (nk,nj,ni), so last dimension is contiguous
        return tensor;
    }
    
    /**
     * Optimized accessor for column operations (k-dimension)
     * With (j,k,i) layout, column access is more efficient
     */
    torch::Tensor get_column_view(const torch::Tensor& tensor, int i, int j) const {
        // Extract vertical column at (i,j)
        // With (j,k,i) layout: tensor[j] gives (nk,ni), then select column i
        // Returns view of shape (nk,)
        return tensor[j].select(1, i);  // More efficient than tensor.index({j, Slice(), i})
    }
    
    /**
     * Get staggered grid dimensions
     */
    const StaggeredDims& staggered_dims() const { return stagger_; }
    const GridDimensions& grid_dims() const { return dims_; }
};

/**
 * C-grid staggering utilities
 */
class CGridStaggering {
public:
    /**
     * Interpolate U from cell faces to cell centers
     */
    static torch::Tensor u_to_cell_center(const torch::Tensor& u) {
        // U is at (i+1/2, j, k), interpolate to (i, j, k)
        // With (j,k,i) layout: u.shape = (nj, nk, ni+1)
        // result.shape = (nj, nk, ni)
        
        auto ni = u.size(2) - 1;
        auto result = torch::empty({u.size(0), u.size(1), ni}, u.options());
        
        // Average adjacent U values
        result = 0.5f * (u.index({torch::indexing::Slice(), 
                                 torch::indexing::Slice(), 
                                 torch::indexing::Slice(0, ni)}) +
                        u.index({torch::indexing::Slice(), 
                                torch::indexing::Slice(), 
                                torch::indexing::Slice(1, ni+1)}));
        return result;
    }
    
    /**
     * Interpolate V from cell faces to cell centers
     */
    static torch::Tensor v_to_cell_center(const torch::Tensor& v) {
        // V is at (i, j+1/2, k), interpolate to (i, j, k)
        // With (j,k,i) layout: v.shape = (nj+1, nk, ni)
        // result.shape = (nj, nk, ni)
        
        auto nj = v.size(0) - 1;
        auto result = torch::empty({nj, v.size(1), v.size(2)}, v.options());
        
        // Average adjacent V values (along j dimension)
        result = 0.5f * (v.index({torch::indexing::Slice(0, nj), 
                                 torch::indexing::Slice(), 
                                 torch::indexing::Slice()}) +
                        v.index({torch::indexing::Slice(1, nj+1), 
                                torch::indexing::Slice(), 
                                torch::indexing::Slice()}));
        return result;
    }
    
    /**
     * Interpolate W from cell faces to cell centers
     */
    static torch::Tensor w_to_cell_center(const torch::Tensor& w) {
        // W is at (i, j, k+1/2), interpolate to (i, j, k)
        // With (j,k,i) layout: w.shape = (nj, nk+1, ni)
        // result.shape = (nj, nk, ni)
        
        auto nk = w.size(1) - 1;
        auto result = torch::empty({w.size(0), nk, w.size(2)}, w.options());
        
        // Average adjacent W values (along k dimension)
        result = 0.5f * (w.index({torch::indexing::Slice(), 
                                 torch::indexing::Slice(0, nk), 
                                 torch::indexing::Slice()}) +
                        w.index({torch::indexing::Slice(), 
                                torch::indexing::Slice(1, nk+1), 
                                torch::indexing::Slice()}));
        return result;
    }
    
    /**
     * Compute divergence on C-grid
     * div = ∂u/∂x + ∂v/∂y + ∂w/∂z
     */
    static torch::Tensor compute_divergence(const torch::Tensor& u,
                                           const torch::Tensor& v,
                                           const torch::Tensor& w,
                                           float dx, float dy,
                                           const torch::Tensor& dzw) {
        // With (j,k,i) layout:
        // u.shape = (nj, nk, ni+1)
        // v.shape = (nj+1, nk, ni)
        // w.shape = (nj, nk+1, ni)
        
        int nj = u.size(0);
        int nk = u.size(1);
        int ni = u.size(2) - 1;
        
        auto div = torch::zeros({nj, nk, ni}, u.options());
        
        // ∂u/∂x: difference of U at cell faces
        auto dudx = (u.index({torch::indexing::Slice(0, nj),
                             torch::indexing::Slice(0, nk),
                             torch::indexing::Slice(1, ni+1)}) -
                    u.index({torch::indexing::Slice(0, nj),
                            torch::indexing::Slice(0, nk),
                            torch::indexing::Slice(0, ni)})) / dx;
        
        // ∂v/∂y: difference of V at cell faces
        auto dvdy = (v.index({torch::indexing::Slice(1, nj+1),
                             torch::indexing::Slice(0, nk),
                             torch::indexing::Slice(0, ni)}) -
                    v.index({torch::indexing::Slice(0, nj),
                            torch::indexing::Slice(0, nk),
                            torch::indexing::Slice(0, ni)})) / dy;
        
        // ∂w/∂z: difference of W at cell faces (with variable dz)
        for (int k = 0; k < nk; k++) {
            auto dwdz_k = (w.index({torch::indexing::Slice(0, nj),
                                   k+1,
                                   torch::indexing::Slice(0, ni)}) -
                          w.index({torch::indexing::Slice(0, nj),
                                  k,
                                  torch::indexing::Slice(0, ni)})) / dzw[k];
            div.index_put_({torch::indexing::Slice(), k, torch::indexing::Slice()},
                          dudx.index({torch::indexing::Slice(), k, torch::indexing::Slice()}) +
                          dvdy.index({torch::indexing::Slice(), k, torch::indexing::Slice()}) +
                          dwdz_k);
        }
        
        return div;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_MEMORY_LAYOUT_ADAPTER_H