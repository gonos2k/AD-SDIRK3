/**
 * @file wrf_sdirk3_memory_pool_adapter.h
 * @brief Adapter to connect TensorFactory with existing TensorMemoryPool
 * @date 2025-08-05
 * 
 * This adapter bridges the interface differences between TensorFactory
 * (which expects OptimizedTensorPool) and the existing TensorMemoryPool
 * implementation, maximizing reuse of current structure.
 */

#ifndef WRF_SDIRK3_MEMORY_POOL_ADAPTER_H
#define WRF_SDIRK3_MEMORY_POOL_ADAPTER_H

#include <torch/torch.h>
#include <atomic>
#include "wrf_sdirk3_memory_pool.h"

namespace WRF_SDIRK3 {

/**
 * @class OptimizedTensorPool
 * @brief Adapter class that wraps existing TensorMemoryPool
 * 
 * This class provides the interface expected by TensorFactory
 * while delegating to the existing wrf::sdirk3::TensorMemoryPool
 * implementation, following the design principle of maximum
 * current structure utilization.
 */
class OptimizedTensorPool {
private:
    // Reference to the singleton TensorMemoryPool
    wrf::sdirk3::TensorMemoryPool& pool_;

public:
    // FIX 2025-01-11 Round46: Removed auto-configure from constructor
    // The underlying TensorMemoryPool already has sensible defaults (max_per_shape=10, max_total=100)
    // Users can call configure() explicitly if they need different settings.
    // This prevents overwriting user settings that were applied to the pool before adapter creation.
    OptimizedTensorPool()
        : pool_(wrf::sdirk3::TensorMemoryPool::getInstance()) {
        // No auto-configure - pool uses its own defaults
    }
    
    /**
     * @brief Acquire a tensor from the pool
     * 
     * Adapts TensorFactory's acquire() interface to TensorMemoryPool's
     * getTensor() interface.
     * 
     * @param shape Tensor dimensions
     * @param options Tensor options (dtype, device, etc.)
     * @return Tensor from pool or newly allocated
     */
    torch::Tensor acquire(torch::IntArrayRef shape,
                         torch::TensorOptions options) {
        // Convert IntArrayRef to vector for TensorMemoryPool
        std::vector<int64_t> shape_vec(shape.begin(), shape.end());

        // Extract device, requires_grad, and dtype from options
        torch::Device device = options.device();
        bool requires_grad = options.requires_grad();
        // FIX 2025-01-11 Round44: Pass dtype to avoid always falling back to FP32
        torch::ScalarType dtype = options.dtype().toScalarType();

        // Get tensor from existing pool with correct dtype
        return pool_.getTensor(shape_vec, device, requires_grad, dtype);
    }
    
    /**
     * @brief Release a tensor back to the pool
     *
     * Adapts TensorFactory's release() interface to TensorMemoryPool's
     * returnTensor() interface.
     *
     * @param tensor Tensor to return to pool
     *
     * OPT Pass34: No const_cast needed - returnTensor takes by value (copy semantics).
     * Passing const ref to by-value param is safe - tensor copied implicitly.
     */
    void release(const torch::Tensor& tensor) {
        pool_.returnTensor(tensor);
    }
    
    /**
     * @brief Print pool statistics
     * 
     * Delegates to TensorMemoryPool's printStats()
     */
    void print_stats() {
        pool_.printStats();
    }
    
    /**
     * @brief Clear all tensors from pool
     * 
     * Delegates to TensorMemoryPool's clear()
     */
    void clear() {
        pool_.clear();
    }
    
    /**
     * @brief Configure pool parameters
     * 
     * @param max_per_shape Maximum tensors per shape
     * @param max_total Maximum total tensors
     */
    void configure(size_t max_per_shape, size_t max_total) {
        pool_.configure(max_per_shape, max_total);
    }
};

} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_MEMORY_POOL_ADAPTER_H