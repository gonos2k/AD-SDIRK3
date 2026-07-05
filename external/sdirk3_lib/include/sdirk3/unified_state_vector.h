#ifndef UNIFIED_STATE_VECTOR_H
#define UNIFIED_STATE_VECTOR_H

#include "sdirk3_config.h"
#include "torch_standalone.h"
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
    int ids_, ide_, jds_, jde_, kds_, kde_;
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
    // FIX 2025-12-31 Batch41: CPU-only policy - per-element access is for CPU tensors only
    // GPU tensors should use bulk operations (get_variable, get_point) instead
    // FIX Round194: Added dtype check for Float32 to match data_ptr<float>()
    float get_value(int i, int j, int k, StateVariable var) const {
        TORCH_CHECK(data_.is_cpu(),
            "UnifiedStateVector::get_value() requires CPU tensor. "
            "Use get_variable() for bulk GPU access.");
        TORCH_CHECK(data_.scalar_type() == torch::kFloat32,
            "UnifiedStateVector::get_value() requires Float32 tensor, got ",
            data_.scalar_type());
        int idx = compute_index(i, j, k);

        // CPU contiguous fast path with data_ptr (zero overhead)
        if (data_.is_contiguous()) {
            const float* ptr = data_.data_ptr<float>();
            return ptr[idx * n_vars_ + static_cast<int>(var)];
        }

        // Non-contiguous CPU: make contiguous copy for access (dtype preserved)
        auto data_contig = data_.contiguous();
        return data_contig.data_ptr<float>()[idx * n_vars_ + static_cast<int>(var)];
    }

    // Set single value
    // FIX 2025-12-31 Batch41: CPU-only policy + index_put_ for in-place assignment
    // Note: data_[idx][var] = value is a no-op in LibTorch (returns temporary)
    void set_value(int i, int j, int k, StateVariable var, float value) {
        TORCH_CHECK(data_.is_cpu(),
            "UnifiedStateVector::set_value() requires CPU tensor. "
            "Use set_variable() for bulk GPU access.");
        int idx = compute_index(i, j, k);
        data_.index_put_({idx, static_cast<int>(var)}, value);
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
    void add_(const UnifiedStateVector& other, float alpha = 1.0) {
        data_.add_(other.data_, alpha);
    }
    
    // Scale by constant
    void scale_(float alpha) {
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
        float* __restrict__ F,           // Output tendencies
        const float* __restrict__ U,     // State vector
        const TileInfo& tile,
        const float* __restrict__ grid_data
    );
    
    // Vectorized acoustic terms
    static void add_acoustic_terms_vectorized(
        float* __restrict__ F,
        const float* __restrict__ rho,
        const float* __restrict__ u,
        const float* __restrict__ v,
        const float* __restrict__ w,
        const float* __restrict__ mu,
        const float* __restrict__ phi,
        const TileInfo& tile,
        const float* __restrict__ grid_coeffs
    );
    
    // Portable optimized operations
    // These can be implemented with platform-specific intrinsics later
    static inline void load_aligned(float* dst, const float* src, int n) {
        for (int i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
    }
    
    static inline void store_aligned(float* dst, const float* src, int n) {
        for (int i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
    }
    
    // Fused multiply-add
    static inline void fma(float* result, const float* a, const float* b, const float* c, int n) {
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
        const float* rho, const float* u, const float* v, const float* w,
        const float* theta, const float* mu, const float* phi,
        const float* moist, const float* scalar,
        int n_moist, int n_scalar
    );
    
    // Unpack unified state vector to WRF arrays
    void unpack_wrf_state(
        const UnifiedStateVector& state,
        float* rho, float* u, float* v, float* w,
        float* theta, float* mu, float* phi,
        float* moist, float* scalar,
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