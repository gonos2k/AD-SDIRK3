/**
 * @file wrf_sdirk3_acoustic_gravity_coupling.h
 * @brief Proper acoustic-gravity wave coupling for fully implicit SDIRK3
 * @date 2025-08-18
 * 
 * Header for acoustic-gravity wave coupling implementation.
 * Essential for fully implicit time integration stability.
 */

#ifndef WRF_SDIRK3_ACOUSTIC_GRAVITY_COUPLING_H
#define WRF_SDIRK3_ACOUSTIC_GRAVITY_COUPLING_H

#include <torch/torch.h>
#include <memory>
#include <mutex>  // FIX 2025-01-07 Batch41 Round7: Thread safety for LRU cache
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_state_vector.h"

namespace wrf {
namespace sdirk3 {

// PhysicsConfig is defined in wrf_sdirk3_unified_rhs.h
// Forward declaration to avoid circular dependency
class PhysicsConfig;

/**
 * @class AcousticGravityCoupling
 * @brief Handles coupled acoustic-gravity wave physics for SDIRK3
 * 
 * This class implements the proper coupling between acoustic waves
 * (sound waves) and gravity waves (buoyancy oscillations) required
 * for stable fully implicit time integration.
 * 
 * Key physics included:
 * - Acoustic pressure-velocity coupling (c_s ~ 340 m/s)
 * - Gravity buoyancy-velocity coupling (N ~ 0.01 /s)
 * - Perturbation pressure equation coupling
 * - Enhanced preconditioner for both wave types
 */
class AcousticGravityCoupling {
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: Multiple mutable std::mutex cache members
    AcousticGravityCoupling(const AcousticGravityCoupling&) = delete;
    AcousticGravityCoupling& operator=(const AcousticGravityCoupling&) = delete;
    AcousticGravityCoupling(AcousticGravityCoupling&&) = delete;
    AcousticGravityCoupling& operator=(AcousticGravityCoupling&&) = delete;

    /**
     * @brief Constructor
     * @param grid_info Grid configuration and metrics
     * @param physics_config Physical constants
     */
    AcousticGravityCoupling(
        std::shared_ptr<WRFGridInfo> grid_info,
        std::shared_ptr<PhysicsConfig> physics_config);
    
    /**
     * @brief Add coupled acoustic-gravity wave terms to RHS
     * @param F RHS vector to modify (packed state vector)
     * @param state Current state variables
     * @param th_base Base state potential temperature
     * @param p_base Base state pressure
     * @param rho_base Base state density
     */
    void add_coupled_terms(
        torch::Tensor& F,
        const StateVector& state,
        const torch::Tensor& th_base,
        const torch::Tensor& p_base,
        const torch::Tensor& rho_base);
    
    /**
     * @brief Apply enhanced preconditioner for Newton-Krylov solver
     * @param residual Residual vector
     * @param state Current state
     * @param dt Time step
     * @param gamma SDIRK3 coefficient (0.4358665215)
     * @return Preconditioned residual
     * 
     * This preconditioner is critical for convergence of the implicit solver.
     * It approximates (I - dt*gamma*J)^{-1} where J includes both acoustic
     * and gravity wave physics.
     */
    torch::Tensor apply_preconditioner(
        const torch::Tensor& residual,
        const StateVector& state,
        float dt,
        float gamma);
    
    /**
     * @brief Compute Brunt-Väisälä frequency for gravity waves
     * @param theta Potential temperature
     * @param th_base Base state potential temperature
     * @return N (buoyancy frequency) [/s]
     */
    torch::Tensor compute_brunt_vaisala(
        const torch::Tensor& theta,
        const torch::Tensor& th_base);

    /**
     * @brief Invalidate rdz tensor cache for dynamic grid updates
     *
     * FIX 2025-01-07 Batch41: Call this method when rdz values change at runtime
     * (e.g., adaptive mesh refinement, moving grids). For static grids, this is
     * not needed as the cache auto-validates via content checking.
     *
     * FIX 2025-01-07 Batch41 Round7: Actually clear cache entries instead of just
     * incrementing epoch (which wasn't being compared).
     */
    void invalidate_rdz_cache() const {
        std::lock_guard<std::mutex> lock(rdz_cache_mutex_);
        for (int i = 0; i < RDZ_CACHE_SIZE; ++i) {
            rdz_cache_[i].valid = false;
        }
    }

private:
    // Grid and physics configuration
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<PhysicsConfig> physics_config_;
    
    // Physical constants
    float g_;      // Gravity acceleration [m/s²]
    float cs_;     // Sound speed [m/s]
    float cp_;     // Specific heat at constant pressure
    float cv_;     // Specific heat at constant volume
    float rd_;     // Gas constant for dry air
    float p0_;     // Reference pressure
    float gamma_;  // cp/cv ratio

    // =========================================================================
    // FIX 2025-01-06 Batch41: Device/dtype-keyed tensor cache for rdz
    // FIX 2025-01-07 Batch41: Expanded to 2-entry LRU cache for offset=0 and offset=1 patterns
    // =========================================================================
    // Cache GPU rdz tensors to avoid repeated CPU→GPU transfers.
    // Two-entry cache handles common patterns: offset=0 (divergence) and offset=1 (adiabatic)
    // LRU eviction: slot 0 is most recently used, slot 1 is older
    //
    // Content validation via first+middle+last element check detects stale cache when
    // rdz values change at runtime (dynamic grids).
    static constexpr int RDZ_CACHE_SIZE = 2;  // 2-entry LRU cache

    struct RdzCacheEntry {
        torch::Tensor tensor;
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t offset{0};
        int64_t size{0};
        float first{0.0f};
        float middle{0.0f};  // FIX 2025-01-07 Batch41 Round7: Middle element for robust content validation
        float last{0.0f};
        bool valid{false};
    };
    mutable RdzCacheEntry rdz_cache_[RDZ_CACHE_SIZE];
    mutable std::mutex rdz_cache_mutex_;  // FIX 2025-01-07 Batch41 Round7: Thread safety

    // =========================================================================
    // FIX 2025-01-10: Cache for p_base_broadcast tensor in add_pressure_temperature_coupling
    // Avoids repeated CPU→GPU transfers and unsqueeze operations
    // FIX 2025-01-10 Round2: Added content signature for stale detection
    // =========================================================================
    struct PBaseBroadcastCache {
        torch::Tensor tensor;            // [1, nz, 1] for broadcasting
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t nz{0};
        // Content signature: first/middle/last values for stale detection
        double first{0.0};
        double middle{0.0};
        double last{0.0};
        bool valid{false};
    };
    mutable PBaseBroadcastCache p_base_broadcast_cache_;
    mutable std::mutex p_base_broadcast_cache_mutex_;

    // =========================================================================
    // FIX 2025-01-10: Cache for rdz_1d_dev tensor in compute_brunt_vaisala
    // Avoids repeated CPU→GPU transfers of rdz metric
    // FIX 2025-01-10 Round2: Added content signature for stale detection
    // =========================================================================
    struct Rdz1dDevCache {
        torch::Tensor tensor;            // [nz] on target device
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t nz{0};
        // Content signature: first/middle/last values for stale detection
        double first{0.0};
        double middle{0.0};
        double last{0.0};
        bool valid{false};
    };
    mutable Rdz1dDevCache rdz_1d_dev_cache_;
    mutable std::mutex rdz_1d_dev_cache_mutex_;

    // =========================================================================
    // FIX 2025-01-10 Round6: Cache for align_rdzw_for_mass output in add_acoustic_divergence
    // Avoids repeated normalize/align operations for 3D rdzw
    // =========================================================================
    struct RdzwMassCache {
        torch::Tensor tensor;            // [ny, nz, nx] normalized rdzw at mass levels
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t ny{0};
        int64_t nz{0};
        int64_t nx{0};
        // Content signature from source rdzw: first/middle/last
        double first{0.0};
        double middle{0.0};
        double last{0.0};
        bool valid{false};
    };
    mutable RdzwMassCache rdzw_mass_cache_;
    mutable std::mutex rdzw_mass_cache_mutex_;

    // =========================================================================
    // FIX 2025-01-10 Round6: Cache for rdz fallback interior tensor in add_acoustic_divergence
    // Avoids repeated CPU→GPU transfers of rdz slice [1, n_interior, 1]
    // =========================================================================
    struct RdzInteriorCache {
        torch::Tensor tensor;            // [1, n_interior, 1] for broadcasting
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t nz{0};                   // determines n_interior = nz - 2
        // Content signature from source rdz: first/middle/last
        double first{0.0};
        double middle{0.0};
        double last{0.0};
        bool valid{false};
    };
    mutable RdzInteriorCache rdz_interior_cache_;
    mutable std::mutex rdz_interior_cache_mutex_;

    // =========================================================================
    // FIX 2025-01-10 Round9: Cache for 3-pt signature index tensor
    // Avoids per-call torch::tensor({0, n/2, n-1}, ...) allocations
    // =========================================================================
    struct SignatureIndexCache {
        torch::Tensor tensor;            // [3] index tensor: {0, n/2, n-1}
        torch::Device device{torch::kCPU};
        int64_t numel{0};                // n value for which indices are valid
        bool valid{false};
    };
    mutable SignatureIndexCache sig_idx_cache_;
    mutable std::mutex sig_idx_cache_mutex_;

    // Private methods for individual physics components
    
    /**
     * @brief Add acoustic pressure gradient force
     */
    void add_acoustic_pressure_gradient(
        torch::Tensor& F,
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& ph,
        const torch::Tensor& mu,
        int u_offset, int v_offset, int w_offset);
    
    /**
     * @brief Add acoustic divergence in pressure equation
     */
    void add_acoustic_divergence(
        torch::Tensor& F,
        const torch::Tensor& u,
        const torch::Tensor& v,
        const torch::Tensor& w,
        const torch::Tensor& ph,
        const torch::Tensor& rho_base,
        int ph_offset);
    
    /**
     * @brief Add gravity wave buoyancy force
     */
    void add_gravity_buoyancy(
        torch::Tensor& F,
        const torch::Tensor& w,
        const torch::Tensor& theta,
        const torch::Tensor& th_base,
        int w_offset);
    
    /**
     * @brief Add gravity wave adiabatic cooling/heating
     */
    void add_gravity_adiabatic(
        torch::Tensor& F,
        const torch::Tensor& w,
        const torch::Tensor& theta,
        const torch::Tensor& th_base,
        int theta_offset);
    
    /**
     * @brief Add coupled pressure-temperature relationship
     */
    void add_pressure_temperature_coupling(
        torch::Tensor& F,
        const torch::Tensor& theta,
        const torch::Tensor& ph,
        const torch::Tensor& p_base,
        const torch::Tensor& th_base,
        int ph_offset);
    
    /**
     * @brief Add vertical coupling with both acoustic and gravity effects
     */
    void add_vertical_coupling(
        torch::Tensor& F,
        const torch::Tensor& w,
        const torch::Tensor& ph,
        const torch::Tensor& theta,
        const torch::Tensor& th_base,
        int w_offset);

    /**
     * @brief Get cached rdz tensor with device/dtype conversion
     * @param rdz_cpu CPU float vector of rdz values
     * @param offset Starting offset in rdz_cpu (default 0)
     * @param size Number of elements to use
     * @param target_device Target device for the tensor
     * @param target_dtype Target dtype for the tensor
     * @return Tensor view of rdz values on target device/dtype
     *
     * FIX 2025-01-06 Batch41: Cache GPU rdz tensors to avoid repeated transfers.
     * Note: Different offset/size combinations will invalidate the cache.
     */
    torch::Tensor get_cached_rdz_tensor(
        const std::vector<float>& rdz_cpu,
        int64_t offset,
        int64_t size,
        torch::Device target_device,
        torch::Dtype target_dtype) const;
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_ACOUSTIC_GRAVITY_COUPLING_H