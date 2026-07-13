/**
 * @file wrf_sdirk3_unified_rhs_optimized.h
 * @brief Optimized unified RHS computation with memory pooling
 * 
 * This version reduces memory allocations through:
 * - Lazy initialization of physics fields
 * - Memory pooling for temporary tensors
 * - Cached tensor views
 * - Minimal data copying
 */

#ifndef WRF_SDIRK3_UNIFIED_RHS_OPTIMIZED_H
#define WRF_SDIRK3_UNIFIED_RHS_OPTIMIZED_H

#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include "wrf_sdirk3_unified_rhs.h"
#include "wrf_sdirk3_memory_pool.h"
#include "wrf_sdirk3_tensor_cache.h"
#include <memory>
#include <unordered_map>  // FIX 2025-01-11 Round38: For tendency_cache_
#include <string>         // FIX 2025-01-11 Round38: For getCachedTendency name parameter

namespace wrf {
namespace sdirk3 {

/**
 * Optimized state components with lazy initialization
 */
struct WRFStateComponentsOptimized : public WRFStateComponents {
    // Flags for lazy initialization
    mutable bool physics_initialized = false;
    mutable bool pbl_initialized = false;
    mutable bool cumulus_initialized = false;
    mutable bool shallow_initialized = false;
    
    // Grid info for lazy tensor creation
    int nx, ny, nz;
    torch::TensorOptions options;
    
    // Constructor
    WRFStateComponentsOptimized(int nx_, int ny_, int nz_, torch::TensorOptions opts)
        : nx(nx_), ny(ny_), nz(nz_), options(opts) {}
    
    // Lazy initialization methods
    void ensurePhysicsInitialized() const {
        if (!physics_initialized) {
            auto& pool = TensorMemoryPool::getInstance();
            
            // Only initialize if physics is active
            // Note: For now, skip physics initialization to avoid const issues
            // This would need proper mutable handling or redesign
            /*
            // FIX 2025-01-11 Round47: When enabled, pass dtype to pool to avoid FP32 fixation
            // Without dtype, FP64 paths would get FP32 tensors causing dtype mismatch
            torch::ScalarType dtype = options.dtype().toScalarType();
            if (wrf::sdirk3::g_sdirk3_config.mp_physics > 0) {
                // Use pooled tensors for moisture variables
                q = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                qc = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                qr = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                qi = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                qs = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
            }
            */
            
            physics_initialized = true;
        }
    }
    
    void ensurePBLInitialized() const {
        if (!pbl_initialized) {
            auto& pool = TensorMemoryPool::getInstance();

            // Skip PBL initialization for now
            /*
            // FIX 2025-01-11 Round47: When enabled, pass dtype to pool to avoid FP32 fixation
            torch::ScalarType dtype = options.dtype().toScalarType();
            if (wrf::sdirk3::g_sdirk3_config.bl_pbl_physics > 0) {
                rublten = pool.getTensor({ny, nz, nx+1}, options.device(), false, dtype);
                rvblten = pool.getTensor({ny+1, nz, nx}, options.device(), false, dtype);
                rthblten = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                rqvblten = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                rqcblten = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
                rqiblten = pool.getTensor({ny, nz, nx}, options.device(), false, dtype);
            }
            */

            pbl_initialized = true;
        }
    }
};

/**
 * Optimized unified RHS with memory efficiency improvements
 */
class UnifiedRHSOptimized : public UnifiedRHS {
private:
    // Cache for commonly used tensors
    mutable std::unordered_map<size_t, torch::Tensor> tendency_cache_;
    
    // Statistics
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    
public:
    UnifiedRHSOptimized(std::shared_ptr<WRFGridInfo> grid_info,
                       std::shared_ptr<PhysicsConfig> physics_config,
                       const std::vector<float>& rdnw);
    
    ~UnifiedRHSOptimized() = default;
    
    // Optimized forward pass
    torch::Tensor forward(const torch::Tensor& U);
    
    // Optimized state extraction
    WRFStateComponentsOptimized extract_state_components_optimized(const torch::Tensor& U);
    
    // Print optimization statistics
    // @note Output is gated by debug_level >= 2 (stats output policy)
    void printStats() const;
    
private:
    // Get cached tendency or create new
    // FIX 2025-01-11 Round36: Added dtype parameter for proper cache key generation
    torch::Tensor getCachedTendency(const std::string& name,
                                   const std::vector<int64_t>& shape,
                                   torch::Device device,
                                   torch::ScalarType dtype = torch::kFloat32);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_UNIFIED_RHS_OPTIMIZED_H