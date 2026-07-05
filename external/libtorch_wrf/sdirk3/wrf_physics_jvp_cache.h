/**
 * @file wrf_physics_jvp_cache.h
 * @brief Physics JVP computational graph caching for SDIRK3
 */

#ifndef WRF_PHYSICS_JVP_CACHE_H
#define WRF_PHYSICS_JVP_CACHE_H

#include <torch/torch.h>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <list>
#include <mutex>

namespace wrf {
namespace sdirk3 {

/**
 * Compiled physics graph for efficient JVP computation
 */
class CompiledPhysicsGraph {
private:
    bool is_compiled_;
    
    // Performance metrics
    mutable size_t execution_count_ = 0;
    mutable double total_time_ms_ = 0.0;
    
public:
    CompiledPhysicsGraph() : is_compiled_(false) {}
    
    CompiledPhysicsGraph(int /*physics_option*/) 
        : is_compiled_(false) {}
    
    /**
     * Compile the physics graph for a specific scheme
     */
    void compile(const std::function<torch::Tensor(const torch::Tensor&)>& physics_func,
                 const torch::Tensor& example_input);
    
    /**
     * Execute compiled graph with new inputs
     */
    torch::Tensor execute(const torch::Tensor& U, const torch::Tensor& v) const;
    
    /**
     * Check if graph is compiled and ready
     */
    bool is_ready() const { return is_compiled_; }
    
    /**
     * Get performance statistics
     */
    double get_average_time_ms() const {
        return execution_count_ > 0 ? total_time_ms_ / execution_count_ : 0.0;
    }
};

/**
 * Physics JVP Cache with LRU eviction
 *
 * NOTE 2025-01-25: This cache is currently CPU-only and device-specific.
 * All cached CompiledPhysicsGraph instances store CPU-based computation patterns.
 * If GPU JVP paths are added in the future, separate device-specific caches
 * should be maintained (e.g., per-device cache instances or device field in CacheKey).
 * The precompile_all_threads() uses explicit torch::kCPU device for this reason.
 */
class PhysicsJVPCache {
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: Contains non-copyable unique_ptr graph cache and LRU containers
    PhysicsJVPCache(const PhysicsJVPCache&) = delete;
    PhysicsJVPCache& operator=(const PhysicsJVPCache&) = delete;
    PhysicsJVPCache(PhysicsJVPCache&&) = delete;
    PhysicsJVPCache& operator=(PhysicsJVPCache&&) = delete;
    PhysicsJVPCache() = default;

    // Cache key based on physics configuration
    struct CacheKey {
        int physics_option;
        int mp_physics;      // Microphysics scheme
        int ra_lw_physics;   // Longwave radiation
        int ra_sw_physics;   // Shortwave radiation
        int bl_pbl_physics;  // PBL scheme
        int cu_physics;      // Cumulus scheme
        
        bool operator==(const CacheKey& other) const {
            return physics_option == other.physics_option &&
                   mp_physics == other.mp_physics &&
                   ra_lw_physics == other.ra_lw_physics &&
                   ra_sw_physics == other.ra_sw_physics &&
                   bl_pbl_physics == other.bl_pbl_physics &&
                   cu_physics == other.cu_physics;
        }
    };
    
private:
    struct CacheKeyHash {
        size_t operator()(const CacheKey& key) const {
            size_t h1 = std::hash<int>()(key.physics_option);
            size_t h2 = std::hash<int>()(key.mp_physics);
            size_t h3 = std::hash<int>()(key.ra_lw_physics);
            size_t h4 = std::hash<int>()(key.bl_pbl_physics);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };
    
    // Compiled graph cache
    std::unordered_map<CacheKey, std::unique_ptr<CompiledPhysicsGraph>, CacheKeyHash> graph_cache_;
    
    // LRU tracking
    std::list<CacheKey> lru_list_;
    std::unordered_map<CacheKey, std::list<CacheKey>::iterator, CacheKeyHash> lru_map_;
    
    // Cache statistics
    mutable size_t hit_count_ = 0;
    mutable size_t miss_count_ = 0;
    mutable size_t compilation_count_ = 0;
    
    static constexpr size_t MAX_CACHE_SIZE = 20;  // Different physics combinations
    
    // Thread-local storage for per-thread caches
    static thread_local std::unique_ptr<PhysicsJVPCache> tls_instance_;
    
public:
    /**
     * Get thread-local cache instance
     */
    static PhysicsJVPCache& get_thread_local_instance();
    
    /**
     * Compute JVP with caching
     */
    torch::Tensor compute_jvp(
        const std::function<torch::Tensor(const torch::Tensor&)>& physics_func,
        const torch::Tensor& U,
        const torch::Tensor& v,
        const CacheKey& key);
    
    /**
     * Precompile physics graphs during initialization
     */
    void precompile_common_schemes(
        const std::vector<CacheKey>& common_configs,
        const torch::Tensor& example_input);
    
    /**
     * Get cache statistics
     * @note Output is gated by debug_level >= 2 (stats output policy)
     */
    void print_statistics() const;
    
    double get_hit_rate() const {
        size_t total = hit_count_ + miss_count_;
        return total > 0 ? static_cast<double>(hit_count_) / total : 0.0;
    }
    
    /**
     * Clear cache (for testing or memory management)
     */
    void clear_cache();
    
private:
    /**
     * Update LRU tracking
     */
    void update_lru(const CacheKey& key);
    
    /**
     * Evict least recently used entry if needed
     */
    void evict_if_needed();
    
    // Friend class for manager access
    friend class PhysicsJVPCacheManager;
};

/**
 * Helper to create physics function for specific configuration
 */
class PhysicsFunctionFactory {
public:
    /**
     * Create combined physics tendency function
     */
    static std::function<torch::Tensor(const torch::Tensor&)> 
    create_physics_function(const PhysicsJVPCache::CacheKey& config);
    
    /**
     * Individual physics components
     */
    static torch::Tensor compute_microphysics(const torch::Tensor& state, int scheme);
    static torch::Tensor compute_radiation(const torch::Tensor& state, int lw_scheme, int sw_scheme);
    static torch::Tensor compute_pbl(const torch::Tensor& state, int scheme);
    static torch::Tensor compute_cumulus(const torch::Tensor& state, int scheme);
};

/**
 * Global cache manager for all threads
 */
class PhysicsJVPCacheManager {
private:
    static std::unique_ptr<PhysicsJVPCacheManager> instance_;
    std::vector<PhysicsJVPCache*> thread_caches_;
    mutable std::mutex mutex_;

public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: mutable std::mutex mutex_ member
    PhysicsJVPCacheManager(const PhysicsJVPCacheManager&) = delete;
    PhysicsJVPCacheManager& operator=(const PhysicsJVPCacheManager&) = delete;
    PhysicsJVPCacheManager(PhysicsJVPCacheManager&&) = delete;
    PhysicsJVPCacheManager& operator=(PhysicsJVPCacheManager&&) = delete;
    PhysicsJVPCacheManager() = default;

    static PhysicsJVPCacheManager& get_instance();
    
    /**
     * Register thread cache
     */
    void register_thread_cache(PhysicsJVPCache* cache);
    
    /**
     * Print global statistics
     */
    void print_global_statistics() const;
    
    /**
     * Precompile for all threads
     */
    void precompile_all_threads(const std::vector<PhysicsJVPCache::CacheKey>& configs);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_PHYSICS_JVP_CACHE_H