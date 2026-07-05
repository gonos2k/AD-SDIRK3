/**
 * @file wrf_tile_boundary_optimizer.h
 * @brief Tile boundary optimization for SDIRK3 to reduce redundant calculations
 */

#ifndef WRF_TILE_BOUNDARY_OPTIMIZER_H
#define WRF_TILE_BOUNDARY_OPTIMIZER_H

#include <torch/torch.h>
#include <unordered_map>
#include <vector>
#include <memory>

namespace wrf {
namespace sdirk3 {

/**
 * Tile boundary data structure for caching shared computations
 */
struct TileBoundaryData {
    // Boundary type
    enum BoundaryType {
        NORTH, SOUTH, EAST, WEST,
        NORTH_EAST, NORTH_WEST,
        SOUTH_EAST, SOUTH_WEST
    };
    
    // Cached boundary values
    torch::Tensor flux_x;     // Flux in x-direction
    torch::Tensor flux_y;     // Flux in y-direction
    torch::Tensor pressure_grad_x;
    torch::Tensor pressure_grad_y;
    torch::Tensor advection_tend;
    
    // Metadata
    int neighbor_tile_id;
    BoundaryType type;
    bool is_valid;
    int computation_step;  // Track when computed
    
    TileBoundaryData() : neighbor_tile_id(-1), 
                         type(NORTH), 
                         is_valid(false),
                         computation_step(-1) {}
};

/**
 * Tile boundary optimizer to minimize redundant calculations
 */
class TileBoundaryOptimizer {
private:
    // Tile dimensions
    int tile_id_;
    
    // Boundary data storage
    std::unordered_map<int, TileBoundaryData> boundary_cache_;
    
    // Computation tracking
    int current_step_;
    
    // Halo region sizes
    static constexpr int HALO_SIZE = 3;
    
    // Performance metrics
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    mutable size_t computations_saved_ = 0;
    
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: mutable std::mutex mutex_ member
    TileBoundaryOptimizer(const TileBoundaryOptimizer&) = delete;
    TileBoundaryOptimizer& operator=(const TileBoundaryOptimizer&) = delete;
    TileBoundaryOptimizer(TileBoundaryOptimizer&&) = delete;
    TileBoundaryOptimizer& operator=(TileBoundaryOptimizer&&) = delete;

    TileBoundaryOptimizer(int tile_id, int nx, int ny, int nz);
    
    /**
     * Check if boundary computation can be reused
     */
    bool can_reuse_boundary(int neighbor_tile_id, int step) const;
    
    /**
     * Get cached boundary data
     */
    const TileBoundaryData& get_boundary_data(int neighbor_tile_id) const;
    
    /**
     * Store computed boundary data
     */
    void cache_boundary_data(
        int neighbor_tile_id,
        TileBoundaryData::BoundaryType type,
        const torch::Tensor& flux_x,
        const torch::Tensor& flux_y,
        const torch::Tensor& pressure_grad_x,
        const torch::Tensor& pressure_grad_y,
        const torch::Tensor& advection_tend
    );
    
    /**
     * Compute shared boundary values
     */
    void compute_shared_boundary(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& p,
        int neighbor_tile_id,
        TileBoundaryData::BoundaryType type,
        float dx, float dy
    );
    
    /**
     * Extract boundary region from tensor
     */
    torch::Tensor extract_boundary_region(
        const torch::Tensor& field,
        TileBoundaryData::BoundaryType type,
        bool is_staggered_x = false,
        bool is_staggered_y = false
    ) const;
    
    /**
     * Apply cached boundary tendencies
     */
    void apply_boundary_tendencies(
        torch::Tensor& u_tend,
        torch::Tensor& v_tend,
        torch::Tensor& w_tend,
        int neighbor_tile_id
    );
    
    /**
     * Update computation step
     */
    void advance_step() { current_step_++; }
    
    /**
     * Clear old cache entries
     */
    void cleanup_cache(int steps_to_keep = 2);
    
    /**
     * Get performance statistics
     */
    void print_statistics() const;
    
    double get_cache_hit_rate() const {
        size_t total = cache_hits_ + cache_misses_;
        return total > 0 ? static_cast<double>(cache_hits_) / total : 0.0;
    }
    
    size_t get_computations_saved() const { return computations_saved_; }
};

/**
 * Global tile boundary manager for coordinating between tiles
 */
class TileBoundaryManager {
private:
    // Tile optimizers
    std::unordered_map<int, std::unique_ptr<TileBoundaryOptimizer>> tile_optimizers_;
    
    // Tile connectivity information
    struct TileConnection {
        int tile1_id;
        int tile2_id;
        TileBoundaryData::BoundaryType boundary_type;
    };
    std::vector<TileConnection> connections_;
    
    // Global step counter
    int global_step_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Singleton instance
    static std::unique_ptr<TileBoundaryManager> instance_;
    
public:
    static TileBoundaryManager& get_instance();
    
    /**
     * Register a tile
     */
    void register_tile(int tile_id, int nx, int ny, int nz);
    
    /**
     * Define tile connectivity
     */
    void add_connection(int tile1_id, int tile2_id, 
                       TileBoundaryData::BoundaryType type);
    
    /**
     * Get optimizer for a specific tile
     */
    TileBoundaryOptimizer* get_tile_optimizer(int tile_id);
    
    /**
     * Coordinate boundary computations between tiles
     */
    void coordinate_boundary_exchange(int step);
    
    /**
     * Global cleanup
     */
    void cleanup_all_caches();
    
    /**
     * Print global statistics
     */
    void print_global_statistics() const;
};

/**
 * Helper class for efficient boundary flux computation
 */
class BoundaryFluxComputer {
private:
    // FIX 2025-12-28: Removed unused interp_weights_x_/y_ tensors.
    // The compute_boundary_flux() uses hardcoded 0.5f for averaging.
    // If variable weights are needed in the future, create them lazily
    // with the input tensor's device/dtype to avoid device mismatch.

    // Grid spacing
    float dx_, dy_;
    
public:
    BoundaryFluxComputer(float dx, float dy, int nx, int ny, int nz);
    
    /**
     * Compute flux at boundary
     */
    std::pair<torch::Tensor, torch::Tensor> compute_boundary_flux(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& rho,
        TileBoundaryData::BoundaryType type
    );
    
    /**
     * Compute pressure gradient at boundary
     */
    std::pair<torch::Tensor, torch::Tensor> compute_boundary_pressure_gradient(
        const torch::Tensor& p,
        const torch::Tensor& mu,
        TileBoundaryData::BoundaryType type
    );
    
    /**
     * Compute advection tendency at boundary
     */
    torch::Tensor compute_boundary_advection(
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& scalar,
        TileBoundaryData::BoundaryType type
    );
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_TILE_BOUNDARY_OPTIMIZER_H