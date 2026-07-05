/**
 * @file wrf_sdirk3_divergence_damping.h
 * @brief Divergence damping for numerical stability
 *
 * This module implements divergence damping to control large-scale horizontal
 * divergence and improve numerical stability. This is particularly important
 * for maintaining mass conservation and preventing accumulation of divergence
 * errors in long simulations.
 *
 * @note USAGE: When creating a DivergenceDamping object, always pass the grid's
 * gravity constant to ensure correct height-to-geopotential conversion:
 *
 *   // Option 1 (preferred): Use factory with gravity parameter
 *   auto damping = createDivergenceDamping(opt, nx, ny, nz, dx, dy, grid.g);
 *
 *   // Option 2: Set gravity after creation
 *   auto damping = createDivergenceDamping(opt, nx, ny, nz, dx, dy);
 *   damping->setGravity(grid.g);
 *
 * The default gravity is 9.81 m/s², but grid.g may differ for some test cases.
 */

#ifndef WRF_SDIRK3_DIVERGENCE_DAMPING_H
#define WRF_SDIRK3_DIVERGENCE_DAMPING_H

#include <torch/torch.h>
#include <memory>

namespace wrf {
namespace sdirk3 {

/**
 * @class DivergenceDamping
 * @brief Implements various divergence damping schemes
 * 
 * Divergence damping adds a term to the momentum equations to control
 * horizontal divergence:
 *   u_tend -= div_damp_coef * d(divergence)/dx
 *   v_tend -= div_damp_coef * d(divergence)/dy
 * 
 * This helps maintain numerical stability and prevents accumulation of
 * divergence errors, especially important for:
 * - Long simulations
 * - High-resolution runs
 * - Strong forcing scenarios
 */
class DivergenceDamping {
private:
    // Grid dimensions
    int nx_, ny_, nz_;
    
    // Grid spacing
    float dx_, dy_;

    // Physical constants
    float g_;  // FIX 2025-12-26: Configurable gravity constant (m/s^2)

    // Map scale factors (pointers to external data)
    torch::Tensor* msfux_;  // Map factor at u-points
    torch::Tensor* msfuy_;  // Map factor at u-points
    torch::Tensor* msfvx_;  // Map factor at v-points
    torch::Tensor* msfvy_;  // Map factor at v-points
    torch::Tensor* msftx_;  // Map factor at mass points
    torch::Tensor* msfty_;  // Map factor at mass points
    
    // Damping configuration
    float base_damping_coef_;      // Base damping coefficient
    float damping_depth_;          // Vertical depth for damping variation
    int damping_option_;           // 0=off, 1=constant, 2=height-dependent
    bool use_3d_divergence_;       // Include vertical divergence
    
    // Cached divergence fields
    torch::Tensor horizontal_div_;  // Horizontal divergence at mass points
    torch::Tensor div_tend_u_;      // Divergence tendency for U
    torch::Tensor div_tend_v_;      // Divergence tendency for V

    // FIX 2025-12-27: Cache for rdnw tensor to avoid CPU→GPU copy every call
    // Uses 5-point sample signature (0%, 25%, 50%, 75%, 100%) to detect in-place value changes
    // FIX 2025-12-27: Added full-verify period for periodic O(n) comparison
    mutable torch::Tensor rdnw_tensor_cache_;
    mutable const float* rdnw_cache_ptr_ = nullptr;
    mutable size_t rdnw_cache_size_ = 0;
    mutable torch::Device rdnw_cache_device_ = torch::kCPU;
    mutable torch::ScalarType rdnw_cache_dtype_ = torch::kFloat32;
    mutable float rdnw_sample_0_ = 0.0f;    // first (0%)
    mutable float rdnw_sample_25_ = 0.0f;   // 25%
    mutable float rdnw_sample_50_ = 0.0f;   // mid (50%)
    mutable float rdnw_sample_75_ = 0.0f;   // 75%
    mutable float rdnw_sample_100_ = 0.0f;  // last (100%)
    mutable std::vector<float> rdnw_full_cache_;  // Full copy for periodic verification
    mutable uint64_t rdnw_verify_counter_ = 0;    // Call counter for full-verify period

    // FIX 2025-12-27: Cache for msf* tensors with device/dtype alignment
    // Uses data_ptr + numel to detect if underlying tensor data or size changed
    mutable torch::Tensor msfux_cache_, msfuy_cache_;
    mutable torch::Tensor msfvx_cache_, msfvy_cache_;
    mutable torch::Tensor msftx_cache_, msfty_cache_;
    mutable torch::Device msf_cache_device_ = torch::kCPU;
    mutable torch::ScalarType msf_cache_dtype_ = torch::kFloat32;
    mutable const void* msfux_ptr_ = nullptr;
    mutable const void* msfuy_ptr_ = nullptr;
    mutable const void* msfvx_ptr_ = nullptr;
    mutable const void* msfvy_ptr_ = nullptr;
    mutable const void* msftx_ptr_ = nullptr;
    mutable const void* msfty_ptr_ = nullptr;
    mutable int64_t msfux_numel_ = 0, msfuy_numel_ = 0;
    mutable int64_t msfvx_numel_ = 0, msfvy_numel_ = 0;
    mutable int64_t msftx_numel_ = 0, msfty_numel_ = 0;
    // FIX 2025-12-27: Add stride[0] for view/transpose detection
    mutable int64_t msfux_stride0_ = 0, msfuy_stride0_ = 0;
    mutable int64_t msfvx_stride0_ = 0, msfvy_stride0_ = 0;
    mutable int64_t msftx_stride0_ = 0, msfty_stride0_ = 0;
    // FIX 2025-12-27: Add stride[1] for 2D tensor layout changes (e.g., transpose)
    mutable int64_t msfux_stride1_ = 0, msfuy_stride1_ = 0;
    mutable int64_t msfvx_stride1_ = 0, msfvy_stride1_ = 0;
    mutable int64_t msftx_stride1_ = 0, msfty_stride1_ = 0;
    // FIX 2025-12-27: Epoch counter for external cache invalidation (moving nest, etc.)
    mutable uint64_t msf_cache_epoch_ = 0;
    uint64_t msf_external_epoch_ = 0;

public:
    /**
     * Constructor
     * @param nx, ny, nz Grid dimensions
     * @param dx, dy Grid spacing
     */
    DivergenceDamping(int nx, int ny, int nz, float dx, float dy);
    
    /**
     * Set map scale factors (required for correct divergence calculation)
     */
    void setMapFactors(torch::Tensor* msfux, torch::Tensor* msfuy,
                      torch::Tensor* msfvx, torch::Tensor* msfvy,
                      torch::Tensor* msftx, torch::Tensor* msfty);
    
    /**
     * Configure damping parameters
     * @param damping_coef Base damping coefficient
     * @param damping_option 0=off, 1=constant, 2=height-dependent
     * @param damping_depth Depth for height-dependent damping
     */
    void configure(float damping_coef, int damping_option,
                  float damping_depth = 5000.0f);

    /**
     * Set gravity constant (default 9.81 m/s^2)
     * FIX 2025-12-26: Allow gravity to be set from grid info instead of hardcoding
     */
    void setGravity(float g) { g_ = g; }

    /**
     * Invalidate map factor cache (for moving nest or external updates)
     * FIX 2025-12-27: Epoch-based invalidation for in-place value changes
     *
     * Call this method when map factors are updated in-place without changing data_ptr.
     * The cache uses ptr+numel+strides as keys, which won't detect value-only changes.
     *
     * CURRENT INTEGRATION STATUS:
     * - TileSDIRK3UnifiedSolver uses computeDivergenceDampingWRF() directly (no caching)
     * - This cache is for DivergenceDamping class instances with setMapFactors() pointers
     * - If DivergenceDamping is used as a persistent member, integration is required
     *
     * MUST call invalidateMapFactorCache() in these scenarios when using this class:
     * 1. After moving nest updates that modify msf* values in-place
     * 2. After from_blob() re-wrapping with same pointer but new values
     * 3. After clone() or copy_() operations that reuse the same tensor storage
     * 4. After any domain decomposition reconfiguration
     *
     * Example integration points (when DivergenceDamping is used as member):
     *   - WRF: After med_nest_move() completes map factor recalculation
     *   - SDIRK3: In TileSDIRK3UnifiedSolver::updateMapFactors() or invalidateCaches()
     *   - Test: After manually modifying msf* tensor values for test scenarios
     *
     * Integration pattern for TileSDIRK3UnifiedSolver:
     * @code
     *   // If divergence_damping_ is added as member:
     *   void TileSDIRK3UnifiedSolver::invalidateCaches() {
     *       invalidateDivergenceCache();
     *       if (divergence_damping_) {
     *           divergence_damping_->invalidateMapFactorCache();
     *       }
     *   }
     * @endcode
     */
    void invalidateMapFactorCache() { ++msf_external_epoch_; }

    /**
     * Compute horizontal divergence
     * @param u U-velocity component (staggered)
     * @param v V-velocity component (staggered)
     * @param mu Dry air mass (column integrated)
     * @return Horizontal divergence at mass points
     */
    torch::Tensor computeHorizontalDivergence(const torch::Tensor& u,
                                             const torch::Tensor& v,
                                             const torch::Tensor& mu);
    
    /**
     * Compute 3D divergence (including vertical component)
     * @param u U-velocity component
     * @param v V-velocity component
     * @param w W-velocity component (staggered vertically)
     * @param mu Dry air mass
     * @param rdnw Inverse vertical grid spacing
     * @return 3D divergence at mass points
     */
    torch::Tensor compute3DDivergence(const torch::Tensor& u,
                                     const torch::Tensor& v,
                                     const torch::Tensor& w,
                                     const torch::Tensor& mu,
                                     const std::vector<float>& rdnw);
    
    /**
     * Apply divergence damping to momentum tendencies
     * @param u_tend U-momentum tendency (modified in place)
     * @param v_tend V-momentum tendency (modified in place)
     * @param u U-velocity for divergence calculation
     * @param v V-velocity for divergence calculation
     * @param mu Dry air mass
     * @param ph Geopotential perturbation (for height-dependent damping)
     * @param phb Base state geopotential
     */
    void applyDamping(torch::Tensor& u_tend,
                     torch::Tensor& v_tend,
                     const torch::Tensor& u,
                     const torch::Tensor& v,
                     const torch::Tensor& mu,
                     const torch::Tensor& ph,
                     const torch::Tensor& phb);
    
    /**
     * Get height-dependent damping coefficient
     * @param height Height above ground (m)
     * @return Damping coefficient at given height
     */
    float getHeightDependentCoefficient(float height) const;
    
    /**
     * Get divergence tendency diagnostics
     */
    torch::Tensor getDivergenceTendencyU() const { return div_tend_u_; }
    torch::Tensor getDivergenceTendencyV() const { return div_tend_v_; }
    torch::Tensor getHorizontalDivergence() const { return horizontal_div_; }
    
    /**
     * Check if damping is enabled
     */
    bool isEnabled() const { return damping_option_ > 0; }
    
private:
    /**
     * Compute divergence gradient for momentum equations
     */
    void computeDivergenceGradient();
    
    /**
     * Apply height-dependent scaling to damping coefficient
     */
    torch::Tensor getHeightDependentScaling(const torch::Tensor& height);
};

/**
 * @brief Factory function to create appropriate divergence damping scheme
 *
 * @param damping_opt Damping option from namelist
 * @param nx, ny, nz Grid dimensions
 * @param dx, dy Grid spacing
 * @return Configured DivergenceDamping object
 * @note For height-dependent damping (option 2), caller should call
 *       setGravity(grid.g) if grid.g differs from default 9.81 m/s^2
 */
std::unique_ptr<DivergenceDamping> createDivergenceDamping(
    int damping_opt, int nx, int ny, int nz, float dx, float dy);

/**
 * @brief Factory function with gravity parameter (FIX 2025-12-27)
 *
 * @param damping_opt Damping option from namelist
 * @param nx, ny, nz Grid dimensions
 * @param dx, dy Grid spacing
 * @param g Gravity constant (m/s^2), typically from grid.g
 * @return Configured DivergenceDamping object with gravity set
 */
std::unique_ptr<DivergenceDamping> createDivergenceDamping(
    int damping_opt, int nx, int ny, int nz, float dx, float dy, float g);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_DIVERGENCE_DAMPING_H