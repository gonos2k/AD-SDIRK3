#ifndef WRF_SDIRK3_JVP_OPTIMIZED_H
#define WRF_SDIRK3_JVP_OPTIMIZED_H

#include <torch/torch.h>
#include <functional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <array>   // FIX 2025-12-30 Batch24 Issue 2: For JvpCacheKey strides array
#include <atomic>  // FIX 2025-12-30 Batch31 Issue 3: For thread-safe gpu_auto_epoch_counter_
#include "wrf_sdirk3_newton_solver.h"

namespace wrf {
namespace sdirk3 {

/**
 * Optimized JVP (Jacobian-Vector Product) Computations
 * For Newton-Krylov solver and 4D-Var applications
 */

// Single JVP computation
torch::Tensor compute_jvp_optimized(
    std::function<torch::Tensor(const torch::Tensor&)> F,
    const torch::Tensor& x,
    const torch::Tensor& v);

// Batched JVP for multiple vectors (e.g., GMRES Krylov vectors)
torch::Tensor compute_jvp_batch_optimized(
    std::function<torch::Tensor(const torch::Tensor&)> F,
    const torch::Tensor& x,
    const std::vector<torch::Tensor>& v_batch);

// JVP with checkpointing for memory efficiency
torch::Tensor compute_jvp_checkpointed(
    std::function<torch::Tensor(const torch::Tensor&)> F,
    const torch::Tensor& x,
    const torch::Tensor& v,
    int checkpoint_segments = 4);

/**
 * Specialized JVP for SDIRK3 implicit stage
 */
class SDIRK3StageJVP {
private:
    float dt_;
    float gamma_;
    std::function<torch::Tensor(const torch::Tensor&)> rhs_func_;
    
public:
    SDIRK3StageJVP(float dt, float gamma,
                   std::function<torch::Tensor(const torch::Tensor&)> rhs);
    
    // JVP for implicit stage: (I - dt*γ*J_F) * v
    torch::Tensor compute_stage_jvp(const torch::Tensor& k,
                                   const torch::Tensor& U_n,
                                   const torch::Tensor& v);
    
    // Batched version for GMRES
    torch::Tensor compute_stage_jvp_batch(const torch::Tensor& k,
                                         const torch::Tensor& U_n,
                                         const std::vector<torch::Tensor>& v_batch);
};

/**
 * FIX 2025-12-30 Batch24 Issue 2+3: Struct-based cache key for efficient hashing
 * Replaces ostringstream string construction with direct numeric hash.
 * FIX 2025-12-30 Batch25 Issue 1: device_index int8_t→int16_t for large GPU clusters
 * FIX 2025-12-30 Batch25 Issue 2: signatures uint64_t bits for FP64/FP16 precision
 * FIX 2025-12-30 Batch25 Issue 3: TORCH_CHECK for dim>8
 */
struct JvpCacheKey {
    uintptr_t data_ptr;
    int64_t numel;
    int8_t device_type;
    int16_t device_index;  // FIX Batch25 Issue 1: int16_t for >=128 GPU clusters
    int8_t scalar_type;
    int64_t storage_offset;
    int64_t dim;
    std::array<int64_t, 8> strides;  // Support up to 8 dimensions
    // FIX 2025-12-30 Batch27 Issue 2: Add sizes hash for broadcast view safety
    // numel+stride alone can collide on 0-stride broadcast views
    uint64_t sizes_hash;
    // FIX 2025-12-30 Batch25 Issue 2: Content signature as uint64_t bits
    // Stores raw bit representation of sampled values for FP64/FP16/FP32 precision
    uint64_t sig_first_bits;
    uint64_t sig_mid_bits;
    uint64_t sig_last_bits;
    // Additional hash for dim>8 strides (0 if dim<=8)
    uint64_t extra_strides_hash;
    // Epoch (for GPU tensors)
    uint64_t cache_epoch;

    bool operator==(const JvpCacheKey& other) const {
        return data_ptr == other.data_ptr &&
               numel == other.numel &&
               device_type == other.device_type &&
               device_index == other.device_index &&
               scalar_type == other.scalar_type &&
               storage_offset == other.storage_offset &&
               dim == other.dim &&
               strides == other.strides &&
               sizes_hash == other.sizes_hash &&
               sig_first_bits == other.sig_first_bits &&
               sig_mid_bits == other.sig_mid_bits &&
               sig_last_bits == other.sig_last_bits &&
               extra_strides_hash == other.extra_strides_hash &&
               cache_epoch == other.cache_epoch;
    }
};

struct JvpCacheKeyHash {
    size_t operator()(const JvpCacheKey& key) const {
        // FNV-1a style hash for good distribution
        size_t hash = 14695981039346656037ULL;
        auto mix = [&hash](size_t val) {
            hash ^= val;
            hash *= 1099511628211ULL;
        };
        mix(key.data_ptr);
        mix(static_cast<size_t>(key.numel));
        // FIX Batch25 Issue 1: device_index now int16_t, shift by 16 bits
        mix(static_cast<size_t>(key.device_type) |
            (static_cast<size_t>(key.device_index & 0xFFFF) << 8) |
            (static_cast<size_t>(key.scalar_type & 0xFF) << 24));
        mix(static_cast<size_t>(key.storage_offset));
        mix(static_cast<size_t>(key.dim));
        for (int64_t i = 0; i < key.dim && i < 8; ++i) {
            mix(static_cast<size_t>(key.strides[i]));
        }
        // FIX 2025-12-30 Batch27 Issue 2: Mix sizes_hash for broadcast view safety
        mix(key.sizes_hash);
        // FIX Batch25 Issue 2: Mix uint64_t signatures directly (no reinterpret_cast)
        mix(key.sig_first_bits);
        mix(key.sig_mid_bits);
        mix(key.sig_last_bits);
        // FIX Batch25 Issue 3: Mix extra strides hash for dim>8
        mix(key.extra_strides_hash);
        mix(key.cache_epoch);
        return hash;
    }
};

/**
 * Graph-based JVP with caching
 *
 * FIX 2025-12-30 Batch23 Issue 1: Added cache_epoch support for GPU tensors
 * FIX 2025-12-30 Batch24 Issue 2: Struct-based cache key for efficient hashing
 * FIX 2025-12-30 Batch24 Issue 3: Device index for multi-GPU safety
 */
class GraphJVP {
private:
    struct ComputationNode {
        std::string op_name;
        std::vector<torch::Tensor> inputs;
        torch::Tensor output;
        std::function<torch::Tensor(const std::vector<torch::Tensor>&)> forward_fn;
        std::function<std::vector<torch::Tensor>(const torch::Tensor&)> jvp_fn;
    };

    // FIX 2025-12-30 Batch32 Issue 4: Max cache size to prevent unbounded memory growth
    // Cache is cleared when exceeded (simple eviction policy)
    static constexpr size_t MAX_JVP_CACHE_SIZE = 1024;

    std::vector<ComputationNode> graph_;
    // FIX 2025-12-30 Batch24 Issue 2: Use struct-based key instead of string
    std::unordered_map<JvpCacheKey, torch::Tensor, JvpCacheKeyHash> jvp_cache_;
    uint64_t current_cache_epoch_ = 0;  // For GPU cache key stability within same epoch
    // FIX 2025-12-30 Batch27 Issue 1: Auto-increment counter for GPU when cache_epoch==0
    // FIX 2025-12-30 Batch31 Issue 3: Use std::atomic for thread safety
    std::atomic<uint64_t> gpu_auto_epoch_counter_{1};  // Starts at 1 to avoid 0 (invalid)

    // FIX 2025-12-30 Batch29 Issue 3: Reusable storage for node_jvps to avoid heap churn
    // FIX 2025-12-30 Batch32 Issue 5: Changed from unordered_map to vector for O(1) index access
    // Vector is more efficient than map when indices are sequential 0..N
    std::vector<torch::Tensor> node_jvps_;

    JvpCacheKey compute_cache_key(const torch::Tensor& v, uint64_t cache_epoch);
    std::vector<size_t> get_parents(size_t node_id);

public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: std::atomic member (gpu_auto_epoch_counter_)
    GraphJVP(const GraphJVP&) = delete;
    GraphJVP& operator=(const GraphJVP&) = delete;
    GraphJVP(GraphJVP&&) = delete;
    GraphJVP& operator=(GraphJVP&&) = delete;
    GraphJVP() = default;

    // Build computation graph
    void trace_computation(std::function<torch::Tensor(const torch::Tensor&)> F,
                          const torch::Tensor& x);

    // Compute JVP using traced graph
    // cache_epoch: solver step identifier for GPU cache reuse within same step
    torch::Tensor compute_graph_jvp(const torch::Tensor& v,
                                   bool use_cache = true,
                                   uint64_t cache_epoch = 0);

    // Set/get cache epoch for GPU tensor reuse
    void set_cache_epoch(uint64_t epoch) { current_cache_epoch_ = epoch; }
    uint64_t get_cache_epoch() const { return current_cache_epoch_; }

    // Clear cache
    void clear_cache() { jvp_cache_.clear(); }
};

/**
 * Newton-Krylov configuration with JVP
 */
struct NewtonKrylovConfig {
    float dt;
    float gamma;
    int max_newton_iter = 10;
    float newton_tol = 1e-6f;
    
    // GMRES parameters
    int gmres_restart = 30;
    int gmres_max_iter = 100;
    float gmres_tol = 1e-5f;
    
    std::function<torch::Tensor(const torch::Tensor&)> rhs_func;
    std::function<torch::Tensor(const torch::Tensor&)> preconditioner;
    std::function<torch::Tensor(const torch::Tensor&, const torch::Tensor&, const torch::Tensor&)> line_search;
    
    torch::Tensor U_n;  // Current state

    // v20.14r25: Standalone-only flag — always true for this config.
    // This path has no layout/halo/periodic plumbing; WRF integration
    // must use the Newton solver call site (which provides all GMRES params).
    static constexpr bool standalone_only = true;
};

// Newton-Krylov solver with optimized JVP.
// standalone_call MUST be true (default). This function has no layout/halo/periodic
// plumbing — passing WRF packed-state tensors will produce wrong results silently.
// For WRF integration, use WRFNewtonKrylovSolver::solve() which provides all GMRES params.
torch::Tensor newton_krylov_with_optimized_jvp(
    std::function<torch::Tensor(const torch::Tensor&)> residual_fn,
    const torch::Tensor& initial_guess,
    const NewtonKrylovConfig& config,
    bool standalone_call = true);

/**
 * 4D-Var specific optimizations
 */
namespace fourDVar {

// Observation operator types
struct ObservationOperator {
    virtual torch::Tensor apply(const torch::Tensor& state) const = 0;
    virtual bool is_linear() const = 0;
    virtual torch::Tensor matrix() const { return torch::Tensor(); }
    virtual ~ObservationOperator() = default;
};

// Model propagator
struct ModelPropagator {
    virtual torch::Tensor integrate(const torch::Tensor& x0, int num_steps) const = 0;
    virtual ~ModelPropagator() = default;
};

// Observation operator JVP
torch::Tensor observation_jvp(const ObservationOperator& H,
                             const torch::Tensor& x,
                             const torch::Tensor& dx);

// Model propagator JVP (TLM replacement)
torch::Tensor model_jvp(const ModelPropagator& M,
                       const torch::Tensor& x0,
                       const torch::Tensor& dx0,
                       int num_steps);

// Adjoint via transposed JVP (ADM replacement)
torch::Tensor model_vjp(const ModelPropagator& M,
                       const torch::Tensor& x0,
                       const torch::Tensor& lambda_final,
                       int num_steps);

} // namespace fourDVar

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_JVP_OPTIMIZED_H