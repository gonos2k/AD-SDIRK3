#ifndef WRF_SDIRK3_UNIFIED_STATE_VECTOR_H
#define WRF_SDIRK3_UNIFIED_STATE_VECTOR_H

#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_torch_wrapper.h"
#include <vector>
#include <memory>

namespace wrf {
namespace sdirk3 {

// Variable indices matching Fortran module
enum StateVariable {
    RHO_VAR = 0,
    U_VAR = 1,
    V_VAR = 2,
    W_VAR = 3,
    THETA_VAR = 4,
    MU_VAR = 5,
    PHI_VAR = 6,
    QV_VAR = 7,
    QC_VAR = 8,
    QR_VAR = 9,
    QI_VAR = 10,
    QS_VAR = 11,
    QG_VAR = 12,
    N_BASE_VARS = 13
};

// AoSoA configuration
constexpr int VECTOR_WIDTH = 8;  // Vector width for optimization
constexpr int SDIRK3_CACHE_LINE_SIZE = 64;

// Forward declarations
class TileInfo;
class UnifiedStateVector;

// Tile information for AoSoA layout
class TileInfo {
public:
    int tile_id;
    int its, ite, jts, jte, kts, kte;  // Tile bounds
    int n_points;                       // Points in this tile
    int global_offset;                  // Offset in global array
    
    TileInfo(int id, int is, int ie, int js, int je, int ks, int ke)
        : tile_id(id), its(is), ite(ie), jts(js), jte(je), kts(ks), kte(ke) {
        n_points = (ite - its + 1) * (jte - jts + 1) * (kte - kts + 1);
    }
};

// Unified state vector with AoSoA layout
class UnifiedStateVector {
private:
    torch::Tensor data_;           // Main data tensor [n_tiles * VECTOR_WIDTH, n_vars]
    std::vector<TileInfo> tiles_;  // Tile information
    int n_vars_;                   // Total number of variables
    int n_species_;                // Number of additional species
    int n_points_;                 // Total grid points
    
    // Domain dimensions
    // int ids_, ide_, jds_, jde_, kds_, kde_;  // Unused - commented out to avoid warnings
    int ims_, ime_, jms_, jme_, kms_, kme_;
    int its_, ite_, jts_, jte_, kts_, kte_;
    
    // Mapping from (i,j,k) to linear index
    torch::Tensor index_map_;
    
public:
    // Constructor
    UnifiedStateVector(int its, int ite, int jts, int jte, int kts, int kte,
                      int ims, int ime, int jms, int jme, int kms, int kme,
                      int n_species = 0);
    
    // Destructor
    ~UnifiedStateVector() = default;
    
    // Copy and move constructors
    UnifiedStateVector(const UnifiedStateVector& other) = default;
    UnifiedStateVector(UnifiedStateVector&& other) = default;
    
    // Assignment operators
    UnifiedStateVector& operator=(const UnifiedStateVector& other) = default;
    UnifiedStateVector& operator=(UnifiedStateVector&& other) = default;
    
    // Access methods
    torch::Tensor& data() { return data_; }
    const torch::Tensor& data() const { return data_; }
    
    // Get variable slice
    torch::Tensor get_variable(StateVariable var) const {
        return data_.index({torch::indexing::Slice(), var});
    }
    
    // Set variable slice
    void set_variable(StateVariable var, const torch::Tensor& values) {
        data_.index_put_({torch::indexing::Slice(), var}, values);
    }
    
    // Get point value (all variables)
    torch::Tensor get_point(int i, int j, int k) const {
        int idx = compute_index(i, j, k);
        return data_.index({idx, torch::indexing::Slice()});
    }
    
    // Set point value (all variables)
    void set_point(int i, int j, int k, const torch::Tensor& values) {
        int idx = compute_index(i, j, k);
        data_.index_put_({idx, torch::indexing::Slice()}, values);
    }
    
    // Get single value
    // FIX 2025-12-27: Add .to(kCPU) before .item<double>() to avoid GPU sync
    // FIX 2025-12-30 Batch28 Issue 3: data_ptr fast path for CPU contiguous
    double get_value(int i, int j, int k, StateVariable var) const {
        int idx = compute_index(i, j, k);

        // Fast path: CPU contiguous tensor - direct pointer access
        if (data_.is_cpu() && data_.is_contiguous()) {
            // data_ is [n_points, n_vars], contiguous row-major
            // linear offset = idx * n_vars_ + var
            if (data_.scalar_type() == torch::kFloat64) {
                const double* ptr = data_.data_ptr<double>();
                return ptr[static_cast<size_t>(idx) * n_vars_ + var];
            } else if (data_.scalar_type() == torch::kFloat32) {
                const float* ptr = data_.data_ptr<float>();
                return static_cast<double>(ptr[static_cast<size_t>(idx) * n_vars_ + var]);
            }
        }
        // Fallback: GPU or non-contiguous
        return data_.index({idx, var}).to(torch::kCPU).item<double>();
    }

    // Set single value
    // FIX 2025-12-27: Use index_put_ instead of operator[] assignment
    void set_value(int i, int j, int k, StateVariable var, double value) {
        int idx = compute_index(i, j, k);
        data_.index_put_({idx, var}, value);
    }
    
    // Tile operations
    torch::Tensor get_tile_data(int tile_id) const;
    void set_tile_data(int tile_id, const torch::Tensor& tile_data);
    
    // Utility methods
    int n_vars() const { return n_vars_; }
    int n_points() const { return n_points_; }
    int n_tiles() const { return tiles_.size(); }
    const std::vector<TileInfo>& tiles() const { return tiles_; }
    
    // Getters for grid bounds
    int its() const { return its_; }
    int ite() const { return ite_; }
    int jts() const { return jts_; }
    int jte() const { return jte_; }
    int kts() const { return kts_; }
    int kte() const { return kte_; }
    
    // Clone state vector
    UnifiedStateVector clone() const {
        UnifiedStateVector result(*this);
        result.data_ = data_.clone();
        return result;
    }
    
    // Zero out all data
    void zero() {
        data_.zero_();
    }
    
    // Add another state vector
    void add_(const UnifiedStateVector& other, double alpha = 1.0) {
        data_.add_(other.data_, alpha);
    }
    
    // Scale by constant
    void scale_(double alpha) {
        data_.mul_(alpha);
    }
    
    // Compute linear index from (i,j,k)
    int compute_index(int i, int j, int k) const;
    
private:
    
    // Build index mapping
    void build_index_map();
    
    // Initialize tiles
    void initialize_tiles();
};

// Helper class for vectorized operations on tiles
class TileOperations {
public:
    // Vectorized tendency computation for a tile
    static void compute_tile_tendencies(
        double* __restrict__ F,           // Output tendencies
        const double* __restrict__ U,     // State vector
        const TileInfo& tile,
        const double* __restrict__ grid_data
    );
    
    // Vectorized acoustic terms
    static void add_acoustic_terms_vectorized(
        double* __restrict__ F,
        const double* __restrict__ rho,
        const double* __restrict__ u,
        const double* __restrict__ v,
        const double* __restrict__ w,
        const double* __restrict__ mu,
        const double* __restrict__ phi,
        const TileInfo& tile,
        const double* __restrict__ grid_coeffs
    );
    
    // Portable optimized operations
    // These can be implemented with platform-specific intrinsics later
    static inline void load_aligned(double* dst, const double* src, int n) {
        for (int i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
    }
    
    static inline void store_aligned(double* dst, const double* src, int n) {
        for (int i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
    }
    
    // Fused multiply-add
    static inline void fma(double* result, const double* a, const double* b, const double* c, int n) {
        for (int i = 0; i < n; ++i) {
            result[i] = a[i] * b[i] + c[i];
        }
    }
};

// Utility functions for state vector operations
namespace utils {
    
    // Pack WRF arrays into unified state vector
    void pack_wrf_state(
        UnifiedStateVector& state,
        const double* rho, const double* u, const double* v, const double* w,
        const double* theta, const double* mu, const double* phi,
        const double* moist, const double* scalar,
        int n_moist, int n_scalar
    );
    
    // Unpack unified state vector to WRF arrays
    void unpack_wrf_state(
        const UnifiedStateVector& state,
        double* rho, double* u, double* v, double* w,
        double* theta, double* mu, double* phi,
        double* moist, double* scalar,
        int n_moist, int n_scalar
    );
    
    // Create state vector from torch tensors
    UnifiedStateVector from_torch_tensors(
        const torch::Tensor& rho,
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& theta,
        const torch::Tensor& mu,
        const torch::Tensor& phi,
        const std::vector<torch::Tensor>& species
    );
}

} // namespace sdirk3
} // namespace wrf

#endif // UNIFIED_STATE_VECTOR_H