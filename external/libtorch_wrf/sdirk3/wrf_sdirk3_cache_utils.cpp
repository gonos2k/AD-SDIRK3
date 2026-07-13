/**
 * @file wrf_sdirk3_cache_utils.cpp
 * @brief Cache invalidation utilities for WRF SDIRK3
 *
 * FIX 2025-12-26: Provides combined cache invalidation for all metric caches.
 * Call invalidateVerticalMetricCaches() after modifying grid metrics in-place.
 *
 * FIX 2025-12-29 Batch9 Issue 5: Documentation clarification:
 * This function handles ALL grid metric caches, not just vertical:
 * - Vertical metrics: dz, dn, dnw, rdz, z_at_w
 * - Horizontal scalars: dx, dy (via global epoch for rdx_3d_/rdy_3d_ in SpatialDerivativesAutograd)
 * - Geographic: xlat
 * - ScalarMeanCache: mub, c1f, c2f, msfty, mu_base .mean() values
 * - DzMinCache: cached min(dz) for CFL checks
 *
 * If dx/dy change, the global epoch increment triggers SpatialDerivativesAutograd::refresh_metrics_if_needed()
 * to recompute rdx_3d_/rdy_3d_ from the updated scalar values.
 *
 * The function name is historical; it invalidates all grid metric caches, not just vertical.
 *
 * FIX 2025-12-31 Batch35 Issue 2: Moving nest and map-factor considerations:
 * - Map scale factors (msft*, msfu*, msfv*) are typically constant within a tile.
 * - For moving nests where the entire grid translates, dz typically stays constant while
 *   xlat/xlong change. If only xlat changes, calling invalidateLatCpuCache() suffices.
 * - If map-factors also change (rare), calling this function handles it via global epoch.
 * - When moving_nest=true, periodic full-verify via getVerifyPeriod() catches unannounced changes.
 */

#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_rayleigh_damping_ad.h"
#include "wrf_sdirk3_boundary_ad.h"
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-12-29: For invalidateStaticMetricCaches()
#include "wrf_sdirk3_unified_preconditioner.h"  // FIX 2025-12-30 Batch31 Issue 2: For invalidateScalarMeanCache()
#include "wrf_sdirk3_config.h"  // FIX Round121: For g_sdirk3_config.debug_level in safe_device_index()
#include <atomic>  // FIX Round119: For thread-safe WARN_ONCE in safe_device_index()

namespace wrf {
namespace sdirk3 {

void WRFGridInfo::invalidateVerticalMetricCaches() {
    // FIX 2025-12-26: Invalidate all metric-related caches
    // Call this after modifying dz, dn, dnw, z_at_w, xlat, dx, or dy in-place
    // FIX 2025-12-29 Batch9 Issue 5: Clarified that dx/dy changes also require this call
    invalidateZ1DCache();
    invalidateDzMinCache();
    invalidateLatCpuCache();

    // FIX 2025-12-29 Issue 1: Invalidate static 3D metric caches (rdz, dnw, dn)
    // Also triggers refresh of rdx_3d_/rdy_3d_ in SpatialDerivativesAutograd via epoch check
    // This increments the global epoch, causing all thread_local StaticMetricCache3D
    // instances to miss on next access and recompute their normalized values.
    metric_utils::invalidateStaticMetricCaches();

    // FIX 2025-12-30 Batch31 Issue 2: Invalidate ScalarMeanCache for mub/c1f/c2f/msfty/mu_base
    // These caches store .mean() results that depend on grid state
    invalidateScalarMeanCache();

    // FIX 2025-01-11 Round74/Round89: Invalidate device cache when dz/rdz may have changed device
    // This prevents stale device detection when grid tensors move between devices
    // (e.g., solver migration from CPU to GPU or vice versa).
    // FIX Round89: Also reset device index for multi-GPU support
    cached_reference_device = -1;
    cached_device_index = -1;

    // FIX Round95: Reset device mismatch warning flag when device cache is invalidated
    // This allows re-warning if mismatch recurs after device normalization.
    // Without this, a temporary mismatch would permanently suppress warnings
    // even after devices become consistent again.
    warned_dz_rdz_device_mismatch = false;
    dz_rdz_warn_throttle_counter = 0;  // FIX Round105: Reset counter alongside flag
}

// FIX 2025-01-11 Round74: Reset per-solver state for solver reinitialization/reuse
// Call this when a solver instance is being reused for a new simulation or after
// device migration to ensure warning flags and logging state start fresh.
// This prevents warning suppression when a solver instance is reused.
//
// FIX Round110: IMPORTANT - solver_unique_id is intentionally NOT reset here.
// The unique_id is assigned once at solver creation and must remain constant
// for the solver's lifetime. It's used by TLS cache validation to detect when
// a solver has been destroyed and replaced by a new one at the same address.
// Resetting it would weaken cache validation and risk UAF.
void WRFGridInfo::resetPerSolverState() {
    // Reset logging state (allows fresh logging for new solver use)
    last_fp64_transition_logged = -1;
    null_path_fp64_logged = false;
    warned_no_reference_device = false;
    warned_dz_rdz_device_mismatch = false;  // FIX Round94: Reset WARN_ONCE flag
    dz_rdz_warn_throttle_counter = 0;       // FIX Round104: Reset throttle counter

    // Reset device cache (forces fresh detection)
    // FIX Round89: Also reset device index for multi-GPU support
    cached_reference_device = -1;
    cached_device_index = -1;

    // NOTE: solver_unique_id is NOT reset - see comment above
}

// ============================================================================
// FIX Round107/Round108/Round119: Safe device index conversion with overflow protection
// ============================================================================
// Implementation in .cpp to ensure WARN_ONCE fires only once across all TUs.
// Header-only static vars cause duplicate warnings per TU.
//
// FIX Round119: Use std::atomic<bool> for thread-safe WARN_ONCE pattern.
// Non-atomic static bool is a data race when multiple threads call this function.
//
// FIX Round121/Round122: Warnings are gated by warn_on_device_index_clamp config
// option (default: false) to avoid unexpected stderr noise in large GPU clusters
// (>32767 GPUs) where overflow is expected under normal operation.
// This is independent of debug_level for fine-grained control.
int16_t safe_device_index(int64_t index) {
    constexpr int64_t MAX_VAL = static_cast<int64_t>(INT16_MAX);
    constexpr int64_t MIN_VAL = static_cast<int64_t>(INT16_MIN);

    if (index > MAX_VAL) {
        // FIX Round119: Thread-safe WARN_ONCE using atomic compare-exchange
        // FIX Round122: Use dedicated warn_on_device_index_clamp config option
        if (g_sdirk3_config.warn_on_device_index_clamp) {
            static std::atomic<bool> warned_overflow{false};
            bool expected = false;
            if (warned_overflow.compare_exchange_strong(expected, true)) {
                std::cerr << "SDIRK3 WARNING: Device index " << index
                          << " exceeds INT16_MAX (" << MAX_VAL << "). "
                          << "Clamping to " << MAX_VAL << ". (WARN_ONCE)\n"
                          << "  Consider upgrading cached_device_index to int32_t "
                          << "for >32767 GPU clusters." << std::endl;
            }
        }
        return static_cast<int16_t>(MAX_VAL);
    }
    if (index < MIN_VAL) {
        // FIX Round119: Thread-safe WARN_ONCE using atomic compare-exchange
        // FIX Round122: Use dedicated warn_on_device_index_clamp config option
        if (g_sdirk3_config.warn_on_device_index_clamp) {
            static std::atomic<bool> warned_underflow{false};
            bool expected = false;
            if (warned_underflow.compare_exchange_strong(expected, true)) {
                std::cerr << "SDIRK3 WARNING: Device index " << index
                          << " below INT16_MIN (" << MIN_VAL << "). "
                          << "Clamping to " << MIN_VAL << ". (WARN_ONCE)" << std::endl;
            }
        }
        return static_cast<int16_t>(MIN_VAL);
    }
    return static_cast<int16_t>(index);
}

} // namespace sdirk3
} // namespace wrf
