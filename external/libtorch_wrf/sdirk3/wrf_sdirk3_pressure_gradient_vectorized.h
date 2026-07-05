#ifndef WRF_SDIRK3_PRESSURE_GRADIENT_VECTORIZED_H
#define WRF_SDIRK3_PRESSURE_GRADIENT_VECTORIZED_H

#include <torch/torch.h>
#include <limits>     // FIX 2025-01-11 Round50: Explicit include for std::numeric_limits (build stability)
#include <cmath>      // FIX 2025-01-11 Round51: For std::isnan, std::isinf, std::abs in doubleNearlyEqual
#include <array>      // FIX 2025-01-11 Round50: For std::array in getCachedAlignedTensor cache
#include <algorithm>  // FIX 2025-01-11 Round53: For std::max in doubleNearlyEqual
#include <atomic>     // FIX 2025-01-11 Round54: For std::atomic in epoch counter
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-01-11 Round35: For getAutocastAwareEps
#include "wrf_sdirk3_types.h"         // FIX Round189: For safe_device_index()

namespace wrf {
namespace sdirk3 {

// OPT Pass34: Forward declarations for epoch functions (defined later in file)
// FIX 2025-01-25: Remove inline from forward declaration to avoid -Wundefined-inline
uint64_t getScalarCacheEpoch();
uint64_t getAlignedTensorCacheEpoch();

// =============================================================================
// FIX 2025-01-11 Round35: Helper to determine compute dtype and align tensors
// =============================================================================
namespace pg_detail {

// Determine optimal compute dtype based on input tensor dtype
// - FP64 inputs → FP64 compute (preserve precision)
// - FP16/BF16 inputs → FP32 compute (avoid underflow)
// - FP32 inputs → FP32 compute
inline torch::ScalarType getComputeDtype(const torch::Tensor& reference) {
    auto dtype = reference.scalar_type();
    if (dtype == torch::kFloat64) {
        return torch::kFloat64;
    } else if (dtype == torch::kFloat16 || dtype == torch::kBFloat16) {
        return torch::kFloat32;  // Upcast to avoid underflow
    }
    return torch::kFloat32;
}

// FIX 2025-01-11 Round39: Determine max compute dtype across multiple tensors
// If ANY input is FP64, compute in FP64 to preserve precision
template<typename... Tensors>
inline torch::ScalarType getMaxComputeDtype(const Tensors&... tensors) {
    bool has_fp64 = false;
    bool has_half = false;

    // Check each tensor's dtype
    auto check_dtype = [&](const torch::Tensor& t) {
        if (!t.defined()) return;
        auto dtype = t.scalar_type();
        if (dtype == torch::kFloat64) {
            has_fp64 = true;
        } else if (dtype == torch::kFloat16 || dtype == torch::kBFloat16) {
            has_half = true;
        }
    };

    (check_dtype(tensors), ...);  // Fold expression

    if (has_fp64) {
        return torch::kFloat64;
    } else if (has_half) {
        return torch::kFloat32;  // Upcast half to avoid underflow
    }
    return torch::kFloat32;
}

// FIX 2025-01-11 Round51/Round52/Round59/Round60: NaN-safe epsilon comparison for double values
// Handles: NaN==NaN for cache, float->double promotion rounding, exact matches
// FIX Round52: Tuned tolerances for better cache hit rate with float->double promotion
// - Float has ~7 significant digits, so float->double promotion introduces ~1e-7 relative error
// - Using 1e-10 relative tolerance balances precision preservation with promotion tolerance
// - Absolute tolerance 1e-15 handles denormals and near-zero values appropriately
//
// FIX Round60: ODR-SAFE tolerance constants - always use default values in header
// For strict FP64 tolerance (1e-14 rel, 1e-17 abs), use the parameterized overload:
//   doubleNearlyEqual(a, b, 1e-14, 1e-17)  // Explicit strict tolerance
// This avoids ODR risk from different TUs defining WRF_SDIRK3_STRICT_FP64_TOLERANCE differently.
// Default tolerances are safe for float->double promotion and handle most use cases.
inline constexpr double DOUBLE_NEARLY_EQUAL_REL_TOL = 1e-10;  // Default (float->double friendly)
inline constexpr double DOUBLE_NEARLY_EQUAL_ABS_TOL = 1e-15;  // Default (float->double friendly)

// FIX Round60: Named constants for strict FP64 tolerance (use with parameterized overload)
// Usage: doubleNearlyEqual(a, b, STRICT_FP64_REL_TOL, STRICT_FP64_ABS_TOL)
inline constexpr double STRICT_FP64_REL_TOL = 1e-14;  // Strict FP64 tolerance
inline constexpr double STRICT_FP64_ABS_TOL = 1e-17;  // Strict FP64 tolerance

// Parameterized version for runtime tolerance control (advanced usage)
inline bool doubleNearlyEqual(double a, double b, double rel_tol, double abs_tol) {
    // Handle NaN: both NaN -> match (for cache key purposes)
    if (std::isnan(a) && std::isnan(b)) return true;
    // One NaN, one not -> no match
    if (std::isnan(a) || std::isnan(b)) return false;
    // Handle infinities
    if (std::isinf(a) || std::isinf(b)) return a == b;
    // Exact match (common case when no promotion occurred)
    if (a == b) return true;

    double diff = std::abs(a - b);
    double larger = std::max(std::abs(a), std::abs(b));

    // Combined tolerance: max(abs_tol, rel_tol * larger)
    return diff <= std::max(abs_tol, rel_tol * larger);
}

// Default tolerance version using compile-time constants
inline bool doubleNearlyEqual(double a, double b) {
    return doubleNearlyEqual(a, b, DOUBLE_NEARLY_EQUAL_REL_TOL, DOUBLE_NEARLY_EQUAL_ABS_TOL);
}

// FIX 2025-01-11 Round48/Round55: Accept double to preserve FP64 precision from caller
// Get rdx/rdy as tensor with appropriate dtype for precision preservation
// Round48: Added single-entry cache to avoid repeated 0D tensor allocations
// Round55: Added epoch-based invalidation for moving-nest rdx/rdy micro-changes
// TODO Round66: In FP64 mode, grid_info_->rdx is already a kFloat64 tensor. Could potentially
// pass tensor directly instead of scalar to avoid cache lookup overhead. Would require API
// changes to accept Optional<Tensor> as first argument. Low priority - cache is efficient.
inline torch::Tensor getRdxTensor(double rdx, torch::ScalarType compute_dtype, torch::Device device) {
    // Single-entry thread-local cache for common case where rdx/dtype/device don't change
    struct CacheEntry {
        double value = std::numeric_limits<double>::quiet_NaN();
        torch::ScalarType dtype = torch::kFloat32;
        torch::DeviceType device_type = torch::kCPU;
        int device_index = -1;
        uint64_t epoch = 0;  // FIX Round55: Track cache epoch for invalidation
        torch::Tensor tensor;
    };
    thread_local CacheEntry cache;

    // FIX 2025-01-11 Round55/Round57: Get current epoch for cache validation
    // Round57: Use scalar-specific epoch to avoid unnecessary invalidation
    uint64_t current_epoch = getScalarCacheEpoch();

    // FIX 2025-01-11 Round49/Round50/Round51/Round55/Round57: Check ALL key components including tensor validity
    // Must verify: value, dtype request, device request, epoch, AND actual tensor state
    // FIX Round51: Use epsilon-based NaN-safe comparison for better cache hit rate
    // when rdx is promoted from float to double (bit pattern may vary slightly)
    // FIX Round55/Round57: Check scalar epoch - invalidate if rdx/rdy changed (moving nests)
    // FIX Round165: Normalize device index with has_index() guard for consistent cache keys
    // FIX Round189: Use safe_device_index() for range-safe int16_t conversion
    bool value_match = doubleNearlyEqual(cache.value, rdx);
    bool epoch_match = (cache.epoch == current_epoch);
    int16_t target_device_idx = safe_device_index(device);
    int16_t cached_tensor_device_idx = cache.tensor.defined()
                                       ? safe_device_index(cache.tensor) : -1;
    bool cache_hit = (cache.tensor.defined() &&
                      epoch_match &&  // FIX Round55: Epoch must match
                      value_match &&
                      cache.dtype == compute_dtype &&
                      cache.device_type == device.type() &&
                      cache.device_index == target_device_idx &&
                      // FIX Round49: Verify cached tensor's actual device/dtype match
                      // (guards against tensor being moved/converted externally)
                      cache.tensor.device().type() == device.type() &&
                      cached_tensor_device_idx == target_device_idx &&
                      cache.tensor.scalar_type() == compute_dtype);

    if (cache_hit) {
        return cache.tensor;
    }

    // Cache miss - create new tensor and cache it
    torch::Tensor result;
    if (compute_dtype == torch::kFloat64) {
        result = torch::tensor(rdx,
            torch::TensorOptions().dtype(torch::kFloat64).device(device));
    } else {
        result = torch::tensor(static_cast<float>(rdx),
            torch::TensorOptions().dtype(torch::kFloat32).device(device));
    }

    // Update cache
    // FIX Round165: Normalize device index with has_index() guard for consistent cache keys
    cache.value = rdx;
    cache.dtype = compute_dtype;
    cache.device_type = device.type();
    cache.device_index = device.has_index() ? static_cast<int>(device.index()) : -1;
    cache.epoch = current_epoch;  // FIX Round55: Store current epoch
    cache.tensor = result;

    return result;
}

// FIX 2025-01-11 Round41: Optimized to use single .to(device, dtype) call when both change
// This reduces allocations and transfers from 2 to 1 when both device and dtype differ
inline torch::Tensor alignTensor(const torch::Tensor& t, torch::Device device, torch::ScalarType dtype) {
    if (!t.defined()) return t;

    bool needs_device_change = (t.device() != device);
    bool needs_dtype_change = (t.scalar_type() != dtype);

    if (needs_device_change && needs_dtype_change) {
        // Single conversion for both - more efficient
        return t.to(device, dtype);
    } else if (needs_device_change) {
        return t.to(device);
    } else if (needs_dtype_change) {
        return t.to(dtype);
    }
    return t;  // Already aligned, return as-is
}

// =============================================================================
// FIX 2025-01-11 Round53/Round54/Round55/Round56/Round57/Round59: Split epoch system for caches
// =============================================================================
// Separate epochs allow independent invalidation of different cache types:
//   - g_aligned_tensor_cache_epoch: For 1D coefficient arrays (c1h, c2h, c1f, c2f, rdnw, rdn)
//   - g_scalar_cache_epoch: For scalar values (rdx, rdy)
// This reduces unnecessary cache flush when only one category changes (e.g., moving nest
// updates rdx/rdy but not vertical coefficients).
//
// FIX Round54: Use std::atomic for thread safety in multi-threaded scenarios
// FIX Round55: Use C++17 inline variable at namespace scope for single definition across TUs
// FIX Round56: Initialization order notes:
//   - C++17 inline variables are zero-initialized before dynamic initialization
//   - Safe for use in static constructors and unit test fixtures
//
// =============================================================================
// FIX Round57/Round59: DSO (Dynamic Shared Object) ISOLATION WARNING AND DESIGN GUIDANCE
// =============================================================================
// C++17 inline variables have ONE definition PER DSO (shared library). This means:
//
// CURRENT ASSUMPTION (WRF-SDIRK3 STANDARD BUILD):
//   - wrf_sdirk3 is compiled as a SINGLE executable or static library
//   - All translation units share ONE epoch counter
//   - invalidateAlignedTensorCache() affects ALL thread-local caches
//   - This is the SUPPORTED configuration
//
// MULTI-DSO RISK (NOT RECOMMENDED):
//   - If wrf_sdirk3 code is split across multiple .so/.dylib files:
//     * libA.so and libB.so each get their OWN g_aligned_tensor_cache_epoch
//     * invalidateAlignedTensorCache() in libA.so does NOT affect libB.so caches
//     * Stale cache entries in libB.so can cause numerical errors
//
// MITIGATION STRATEGIES FOR MULTI-DSO (IF REQUIRED):
//   Option 1 (RECOMMENDED): Link all wrf_sdirk3 code into single DSO
//   Option 2: Export epoch accessors via shared state object:
//     - Create singleton EpochManager in main DSO
//     - Other DSOs call EpochManager::invalidate() instead of inline function
//     - Requires build system changes
//   Option 3: Use extern declaration with definition in single .cpp:
//     - extern std::atomic<uint64_t> g_aligned_tensor_cache_epoch;
//     - Define in exactly one .cpp linked into main DSO
//     - Breaks header-only pattern, requires careful linking
//   Option 4: Per-DSO invalidation coordination:
//     - Each DSO registers callback with central coordinator
//     - Grid update triggers coordinator to call all registered invalidators
//     - Complex but preserves isolation
//
// GPU TENSOR NOTE (Round59):
//   - GPU tensors skip signature computation (D2H sync too expensive)
//   - GPU tensors rely SOLELY on epoch for cache validation
//   - Multi-DSO scenario with GPU tensors is especially risky
// =============================================================================

// Epoch for 1D coefficient arrays (c1h, c2h, c1f, c2f, rdnw, rdn)
inline std::atomic<uint64_t> g_aligned_tensor_cache_epoch{0};

// FIX 2025-01-11 Round57: Separate epoch for scalar caches (rdx, rdy)
// Moving nest typically changes rdx/rdy but not vertical coefficients
inline std::atomic<uint64_t> g_scalar_cache_epoch{0};

// FIX 2025-01-11 Round54/Round55: Thread-safe read for cache key comparison
inline uint64_t getAlignedTensorCacheEpoch() {
    return g_aligned_tensor_cache_epoch.load(std::memory_order_relaxed);
}

// FIX 2025-01-11 Round57: Thread-safe read for scalar cache epoch
inline uint64_t getScalarCacheEpoch() {
    return g_scalar_cache_epoch.load(std::memory_order_relaxed);
}

// FIX 2025-01-11 Round53/Round54/Round55/Round57: Invalidate 1D coefficient cache
// Call when vertical coefficients (c1h, c2h, c1f, c2f, rdnw, rdn) are updated
// Does NOT invalidate scalar (rdx/rdy) cache - use invalidateScalarCache() for that
inline void invalidateAlignedTensorCache() {
    g_aligned_tensor_cache_epoch.fetch_add(1, std::memory_order_relaxed);
}

// FIX 2025-01-11 Round57: Invalidate scalar (rdx/rdy) cache
// Call when horizontal grid spacing changes (moving nest, regrid)
// Does NOT invalidate 1D coefficient cache - use invalidateAlignedTensorCache() for that
inline void invalidateScalarCache() {
    g_scalar_cache_epoch.fetch_add(1, std::memory_order_relaxed);
}

// FIX 2025-01-11 Round57: Invalidate ALL caches (both 1D and scalar)
// Use when complete grid reset occurs (e.g., full regridding, domain change)
inline void invalidateAllCaches() {
    g_aligned_tensor_cache_epoch.fetch_add(1, std::memory_order_relaxed);
    g_scalar_cache_epoch.fetch_add(1, std::memory_order_relaxed);
}

// FIX 2025-01-11 Round50/Round51/Round52/Round53: Cached aligned tensor for 1D constant arrays
// These tensors rarely change, so caching the aligned version avoids repeated .to() calls
// FIX Round51: Cache key includes storage_ptr + storage_offset + numel to correctly distinguish views
// FIX Round52: Added src_dtype to key; only cache contiguous tensors (avoids stride complexity)
// FIX Round53: Added epoch-based invalidation for in-place content updates (moving nests)
// Thread-local cache with 6 entries (c1h, c2h, c1f, c2f, rdnw, rdn)
inline torch::Tensor getCachedAlignedTensor(const torch::Tensor& t, torch::Device device, torch::ScalarType dtype) {
    if (!t.defined()) return t;

    // FIX 2025-01-11 Round55/Round56: Handle non-1D tensors gracefully (skip caching)
    // This cache is designed for 1D constant arrays (c1h, c2h, c1f, c2f, rdnw, rdn)
    // Multi-dimensional tensors with same numel but different shapes would incorrectly match
    // FIX Round56: Instead of failing, skip caching and use alignTensor directly
    // This allows 2D/3D metrics from physics regrid to pass through without error
    if (t.dim() != 1) {
        // Non-1D tensor: bypass cache to avoid key collisions, just align and return
        return alignTensor(t, device, dtype);
    }

    // Fast path: already aligned
    if (t.device() == device && t.scalar_type() == dtype) {
        return t;
    }

    // FIX 2025-01-11 Round52: Only cache contiguous tensors to avoid stride-related cache collisions
    // Non-contiguous tensors with same storage/offset/numel but different strides would be incorrectly matched
    // For 1D constant arrays like c1h/c2h, they should always be contiguous anyway
    if (!t.is_contiguous()) {
        return alignTensor(t, device, dtype);
    }

    struct CacheEntry {
        // FIX 2025-01-11 Round51/Round52: Use storage pointer + offset + dtype to identify tensor uniquely
        void* storage_ptr = nullptr;      // Base storage address
        int64_t storage_offset = 0;       // Offset into storage (distinguishes views)
        int64_t numel = 0;                // Element count (catches size changes)
        torch::DeviceType src_device_type = torch::kCPU;  // Source tensor device
        int src_device_index = -1;
        torch::ScalarType src_dtype = torch::kFloat32;    // FIX Round52: Source tensor dtype
        torch::DeviceType target_device_type = torch::kCPU;
        int target_device_index = -1;
        torch::ScalarType target_dtype = torch::kFloat32;
        uint64_t epoch = 0;               // FIX Round53: Cache epoch for invalidation
        // FIX 2025-01-11 Round57: 3-point signature for in-place content change detection
        // Even if invalidation is missed, content changes will be detected
        double sig_first = std::numeric_limits<double>::quiet_NaN();   // First element
        double sig_mid = std::numeric_limits<double>::quiet_NaN();     // Middle element
        double sig_last = std::numeric_limits<double>::quiet_NaN();    // Last element
        torch::Tensor aligned;
    };

    // FIX 2025-01-11 Round56: Increased from 6 to 8 slots for additional constants
    // Base constants: c1h, c2h, c1f, c2f, rdnw, rdn (6 slots)
    // Extra capacity: fnm, fnp or other metrics that may use this cache (2 slots)
    // Round-robin eviction means heavily-used entries may be evicted if >8 unique tensors
    // are cached in the same thread. Increase further if hit rate drops significantly.
    static constexpr size_t CACHE_SIZE = 8;
    thread_local std::array<CacheEntry, CACHE_SIZE> cache;
    thread_local size_t next_slot = 0;

    // FIX Round53: Get current epoch for cache validation
    uint64_t current_epoch = getAlignedTensorCacheEpoch();

    // FIX Round51/Round52: Extract storage-level identifiers for robust view handling
    // OPT Pass34: Use .get() for PyTorch 2.x DataPtr → void* conversion
    void* stor_ptr = t.storage().data_ptr().get();
    int64_t stor_offset = t.storage_offset();
    int64_t src_numel = t.numel();
    auto src_device = t.device();
    auto src_dtype = t.scalar_type();  // FIX Round52: Include source dtype in key
    // FIX Round165: Normalize device indices with has_index() guard for consistent cache keys
    int src_device_idx = src_device.has_index() ? static_cast<int>(src_device.index()) : -1;
    int target_device_idx = device.has_index() ? static_cast<int>(device.index()) : -1;

    // FIX 2025-01-11 Round57/Round58: Compute 3-point signature for in-place change detection
    // Round58 PERF FIX: Only compute signature for CPU tensors to avoid D2H sync overhead
    // GPU tensors rely on epoch-based invalidation only (acceptable trade-off since 1D
    // constants like c1h/c2h/rdnw are typically CPU-resident anyway)
    // Round58 ROBUSTNESS: Explicit numel>0 check to handle empty tensor edge case
    double sig_first = std::numeric_limits<double>::quiet_NaN();
    double sig_mid = std::numeric_limits<double>::quiet_NaN();
    double sig_last = std::numeric_limits<double>::quiet_NaN();
    if (src_numel > 0 && t.is_cpu()) {
        // CPU tensor: safe to compute signature without D2H sync
        torch::NoGradGuard no_grad;
        sig_first = t.index({0}).item<double>();
        if (src_numel > 2) {
            sig_mid = t.index({src_numel / 2}).item<double>();
        }
        sig_last = t.index({src_numel - 1}).item<double>();
    }
    // GPU tensors: sig_* remain NaN, which will match cache entry's NaN via doubleNearlyEqual
    // This means GPU tensors rely on epoch + storage_ptr + numel for cache validation

    // Search cache
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        auto& entry = cache[i];
        // FIX Round165: Use normalized device indices for consistent cache matching
        // FIX Round193: Use safe_device_index() for overflow-checked int16_t conversion
        int16_t aligned_device_idx = entry.aligned.defined()
                                     ? wrf::sdirk3::safe_device_index(entry.aligned.device()) : -1;
        if (entry.epoch == current_epoch &&  // FIX Round53: Check epoch first
            entry.storage_ptr == stor_ptr &&
            entry.storage_offset == stor_offset &&
            entry.numel == src_numel &&
            entry.src_device_type == src_device.type() &&
            entry.src_device_index == src_device_idx &&  // FIX Round165: Use normalized index
            entry.src_dtype == src_dtype &&  // FIX Round52: Check source dtype
            entry.target_device_type == device.type() &&
            entry.target_device_index == target_device_idx &&  // FIX Round165: Use normalized index
            entry.target_dtype == dtype &&
            // FIX 2025-01-11 Round57: Check 3-point signature for in-place content changes
            doubleNearlyEqual(entry.sig_first, sig_first) &&
            doubleNearlyEqual(entry.sig_mid, sig_mid) &&
            doubleNearlyEqual(entry.sig_last, sig_last) &&
            entry.aligned.defined() &&
            // Verify aligned tensor is still valid
            entry.aligned.device().type() == device.type() &&
            aligned_device_idx == target_device_idx &&  // FIX Round165: Use normalized index
            entry.aligned.scalar_type() == dtype &&
            entry.aligned.numel() == src_numel) {
            return entry.aligned;
        }
    }

    // Cache miss - align and store
    auto aligned = alignTensor(t, device, dtype);

    auto& slot = cache[next_slot];
    slot.storage_ptr = stor_ptr;
    slot.storage_offset = stor_offset;
    slot.numel = src_numel;
    slot.src_device_type = src_device.type();
    slot.src_device_index = src_device_idx;  // FIX Round165: Use normalized index
    slot.src_dtype = src_dtype;  // FIX 2025-01-11 Round52: Store source dtype
    slot.target_device_type = device.type();
    slot.target_device_index = target_device_idx;  // FIX Round165: Use normalized index
    slot.target_dtype = dtype;
    slot.epoch = current_epoch;  // FIX 2025-01-11 Round53: Store current epoch
    // FIX 2025-01-11 Round57: Store 3-point signature for in-place change detection
    slot.sig_first = sig_first;
    slot.sig_mid = sig_mid;
    slot.sig_last = sig_last;
    slot.aligned = aligned;

    next_slot = (next_slot + 1) % CACHE_SIZE;
    return aligned;
}

}  // namespace pg_detail

/**
 * Vectorized pressure gradient computations for WRF SDIRK3
 * 
 * Replaces inefficient triple-nested loops with vectorized tensor operations
 * Achieves 100-1000x speedup over element-wise accessor patterns
 */

// =============================================================================
// Vectorized Pressure Gradient for U-momentum
// =============================================================================

/**
 * Compute pressure gradient force at U-points (fully vectorized)
 * 
 * WRF formula: dpx = (msfux/msfuy) * 0.5*rdx * (c1h*muu+c2h) * 
 *                    ((ph[k+1,i] - ph[k+1,i-1]) + (ph[k,i] - ph[k,i-1]) +
 *                     alt_avg * (p[i] - p[i-1]) + al_avg * (pb[i] - pb[i-1]))
 */
inline torch::Tensor compute_pressure_gradient_u_vectorized(
    const torch::Tensor& ph,       // [ny, nz+1, nx] geopotential perturbation
    const torch::Tensor& ph_base,  // [ny, nz+1, nx] base geopotential
    const torch::Tensor& p_pert,   // [ny, nz, nx] pressure perturbation
    const torch::Tensor& p_base,   // [ny, nz, nx] base pressure
    const torch::Tensor& al_pert,  // [ny, nz, nx] inverse density perturbation
    const torch::Tensor& alt,      // [ny, nz, nx] base inverse density
    const torch::Tensor& muu,      // [ny, nx+1] coupled mass at u-points
    const torch::Tensor& c1h,      // [nz] vertical coupling coefficient
    const torch::Tensor& c2h,      // [nz] vertical coupling coefficient
    const torch::Tensor& msfux,    // [ny, nx+1] map scale factor at u-points
    const torch::Tensor& msfuy,    // [ny, nx+1] map scale factor at u-points
    const torch::Tensor& cqu,      // [ny, nz, nx] moisture correction at mass points
    double rdx,                    // FIX 2025-01-11 Round48: double for FP64 precision
    c10::optional<torch::ScalarType> output_dtype = c10::nullopt)  // FIX Round43: Optional output dtype
{
    using namespace torch::indexing;

    // ==========================================================================
    // FIX 2025-01-11 Round35: Input shape validation
    // ==========================================================================
    TORCH_CHECK(ph.dim() == 3, "ph must be 3D [ny, nz+1, nx], got ", ph.dim(), "D");
    TORCH_CHECK(p_pert.dim() == 3, "p_pert must be 3D [ny, nz, nx], got ", p_pert.dim(), "D");
    TORCH_CHECK(c1h.dim() == 1, "c1h must be 1D [nz], got ", c1h.dim(), "D");
    TORCH_CHECK(muu.dim() == 2, "muu must be 2D [ny, nx+1], got ", muu.dim(), "D");

    // Get dimensions
    int ny = ph.size(0);
    int nz = p_pert.size(1);
    int nx = p_pert.size(2);
    int nx_u = nx + 1;  // U-staggered dimension

    // Shape consistency checks
    TORCH_CHECK(ph.size(1) == nz + 1, "ph k-dim must be nz+1=", nz+1, ", got ", ph.size(1));
    TORCH_CHECK(ph.size(2) == nx, "ph i-dim must be nx=", nx, ", got ", ph.size(2));
    TORCH_CHECK(c1h.size(0) == nz, "c1h must have nz=", nz, " elements, got ", c1h.size(0));
    TORCH_CHECK(muu.size(0) == ny && muu.size(1) == nx_u,
        "muu shape must be [", ny, ", ", nx_u, "], got [", muu.size(0), ", ", muu.size(1), "]");

    // ==========================================================================
    // FIX 2025-01-11 Round35/Round39/Round41/Round42/Round43: Determine compute dtype across ALL inputs
    // ==========================================================================
    // FIX 2025-01-11 Round42/Round43: Determine output dtype - use parameter if provided, else ph dtype
    auto result_dtype = output_dtype.value_or(ph.scalar_type());
    // FIX 2025-01-11 Round41: Expanded to include ALL tensor inputs to prevent downcast
    // If ANY input is FP64, we must compute in FP64 to preserve precision
    auto compute_dtype = pg_detail::getMaxComputeDtype(
        ph, ph_base, p_pert, p_base, al_pert, alt,
        muu, c1h, c2h, msfux, msfuy, cqu);
    auto target_device = ph.device();

    // Align all input tensors to target device/dtype
    auto ph_a = pg_detail::alignTensor(ph, target_device, compute_dtype);
    auto ph_base_a = pg_detail::alignTensor(ph_base, target_device, compute_dtype);
    auto p_pert_a = pg_detail::alignTensor(p_pert, target_device, compute_dtype);
    auto p_base_a = pg_detail::alignTensor(p_base, target_device, compute_dtype);
    auto al_pert_a = pg_detail::alignTensor(al_pert, target_device, compute_dtype);
    auto alt_a = pg_detail::alignTensor(alt, target_device, compute_dtype);
    auto muu_a = pg_detail::alignTensor(muu, target_device, compute_dtype);
    // FIX 2025-01-11 Round50: Use cached alignment for 1D constant tensors
    auto c1h_a = pg_detail::getCachedAlignedTensor(c1h, target_device, compute_dtype);
    auto c2h_a = pg_detail::getCachedAlignedTensor(c2h, target_device, compute_dtype);
    auto msfux_a = pg_detail::alignTensor(msfux, target_device, compute_dtype);
    auto msfuy_a = pg_detail::alignTensor(msfuy, target_device, compute_dtype);
    auto cqu_a = pg_detail::alignTensor(cqu, target_device, compute_dtype);

    // Convert rdx to tensor with appropriate dtype
    auto rdx_t = pg_detail::getRdxTensor(rdx, compute_dtype, target_device);

    // Total geopotential (perturbation + base)
    auto ph_total = ph_a + ph_base_a;
    
    // =============================================================================
    // Step 1: Compute geopotential gradient at u-points
    // =============================================================================
    
    // For interior u-points, gradient is between mass points i-1 and i
    // Handle boundaries with padding
    auto ph_total_padded = torch::nn::functional::pad(
        ph_total, 
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );
    
    // Geopotential gradient: sum contributions from k and k+1 levels
    // dph_k: gradient at level k
    auto dph_k = ph_total_padded.index({Slice(), Slice(0, -1), Slice(2, None)}) - 
                 ph_total_padded.index({Slice(), Slice(0, -1), Slice(0, -2)});
    
    // dph_kp1: gradient at level k+1
    auto dph_kp1 = ph_total_padded.index({Slice(), Slice(1, None), Slice(2, None)}) - 
                   ph_total_padded.index({Slice(), Slice(1, None), Slice(0, -2)});
    
    // Total geopotential gradient (sum of k and k+1 contributions)
    auto dph_dx = dph_k + dph_kp1;
    
    // =============================================================================
    // Step 2: Compute pressure gradient at mass level
    // =============================================================================

    // Pad pressure fields for gradient calculation
    auto p_pert_padded = torch::nn::functional::pad(
        p_pert_a,
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );

    auto p_base_padded = torch::nn::functional::pad(
        p_base_a,
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );
    
    // Pressure gradients at u-points
    auto dp_dx = p_pert_padded.index({Slice(), Slice(), Slice(2, None)}) - 
                 p_pert_padded.index({Slice(), Slice(), Slice(0, -2)});
    
    auto dpb_dx = p_base_padded.index({Slice(), Slice(), Slice(2, None)}) - 
                  p_base_padded.index({Slice(), Slice(), Slice(0, -2)});
    
    // =============================================================================
    // Step 3: Average inverse densities to u-points
    // =============================================================================

    // Pad inverse density fields
    auto alt_padded = torch::nn::functional::pad(
        alt_a,
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );

    auto al_pert_padded = torch::nn::functional::pad(
        al_pert_a,
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );
    
    // Average to u-points (use 0.5 instead of 0.5f for FP64 precision)
    auto alt_avg = 0.5 * (alt_padded.index({Slice(), Slice(), Slice(2, None)}) +
                          alt_padded.index({Slice(), Slice(), Slice(0, -2)}));

    auto al_avg = 0.5 * (al_pert_padded.index({Slice(), Slice(), Slice(2, None)}) +
                         al_pert_padded.index({Slice(), Slice(), Slice(0, -2)}));
    
    // =============================================================================
    // Step 4: Map scale factor ratio
    // =============================================================================

    // FIX 2025-12-28: Use autocast-aware eps to avoid FP16 underflow
    // NOTE: eps is based on msfuy's dtype because torch::where and division
    // operate in the same dtype (no implicit upcast occurs here)
    const float eps = metric_utils::getAutocastAwareEps(msfuy_a);
    auto msfuy_safe = torch::where(msfuy_a.abs() < eps,
                                   torch::ones_like(msfuy_a),
                                   msfuy_a);
    auto msf_ratio = msfux_a / msfuy_safe;
    
    // Expand to 3D for broadcasting
    auto msf_ratio_3d = msf_ratio.unsqueeze(1).expand({ny, nz, nx_u});
    
    // =============================================================================
    // Step 5: Vertical coupling factor
    // =============================================================================

    // Expand c1h and c2h to 3D (using aligned tensors)
    auto c1h_3d = c1h_a.unsqueeze(0).unsqueeze(2).expand({ny, nz, nx_u});
    auto c2h_3d = c2h_a.unsqueeze(0).unsqueeze(2).expand({ny, nz, nx_u});

    // Expand muu to 3D
    auto muu_3d = muu_a.unsqueeze(1).expand({ny, nz, nx_u});

    // Vertical coupling: (c1h * muu + c2h)
    auto vert_coupling = c1h_3d * muu_3d + c2h_3d;
    
    // =============================================================================
    // Step 6: Complete pressure gradient force (before moisture correction)
    // FIX 2025-01-11 Round35: Use rdx_t tensor for FP64 precision preservation
    // =============================================================================

    auto dpx = msf_ratio_3d * 0.5 * rdx_t * vert_coupling *
               (dph_dx + alt_avg * dp_dx + al_avg * dpb_dx);
    
    // =============================================================================
    // Step 7: Apply moisture correction (cqu)
    // =============================================================================

    // Interpolate cqu from mass points to u-points (using aligned tensor)
    auto cqu_padded = torch::nn::functional::pad(
        cqu_a,
        torch::nn::functional::PadFuncOptions({1, 1, 0, 0, 0, 0})
            .mode(torch::kReplicate)
    );
    
    auto cqu_at_u = 0.5 * (cqu_padded.index({Slice(), Slice(), Slice(2, None)}) +
                           cqu_padded.index({Slice(), Slice(), Slice(0, -2)}));

    // Apply moisture correction and return negative (force direction)
    auto result = -cqu_at_u * dpx;

    // FIX 2025-01-11 Round42/Round43: Cast result to output dtype (defaults to ph's dtype)
    if (result.scalar_type() != result_dtype) {
        return result.to(result_dtype);
    }
    return result;
}

// FIX 2025-12-28: Wrapper overload for grid_info-based call (used by unified_rhs_optimized.cpp)
// TODO: Implement proper WRFGridInfo wrapper when that file is activated in CMakeLists.txt
// For now, callers must use the explicit parameter version above.

// =============================================================================
// Vectorized Pressure Gradient for V-momentum
// =============================================================================

/**
 * Compute pressure gradient force at V-points (fully vectorized)
 */
inline torch::Tensor compute_pressure_gradient_v_vectorized(
    const torch::Tensor& ph,       // [ny, nz+1, nx] geopotential perturbation
    const torch::Tensor& ph_base,  // [ny, nz+1, nx] base geopotential
    const torch::Tensor& p_pert,   // [ny, nz, nx] pressure perturbation
    const torch::Tensor& p_base,   // [ny, nz, nx] base pressure
    const torch::Tensor& al_pert,  // [ny, nz, nx] inverse density perturbation
    const torch::Tensor& alt,      // [ny, nz, nx] base inverse density
    const torch::Tensor& muv,      // [ny+1, nx] coupled mass at v-points
    const torch::Tensor& c1h,      // [nz] vertical coupling coefficient
    const torch::Tensor& c2h,      // [nz] vertical coupling coefficient
    const torch::Tensor& msfvx,    // [ny+1, nx] map scale factor at v-points
    const torch::Tensor& msfvy,    // [ny+1, nx] map scale factor at v-points
    const torch::Tensor& cqv,      // [ny, nz, nx] moisture correction at mass points
    double rdy,                    // FIX 2025-01-11 Round48: double for FP64 precision
    c10::optional<torch::ScalarType> output_dtype = c10::nullopt)  // FIX Round43: Optional output dtype
{
    using namespace torch::indexing;

    // ==========================================================================
    // FIX 2025-01-11 Round35: Input shape validation
    // ==========================================================================
    TORCH_CHECK(ph.dim() == 3, "ph must be 3D [ny, nz+1, nx], got ", ph.dim(), "D");
    TORCH_CHECK(p_pert.dim() == 3, "p_pert must be 3D [ny, nz, nx], got ", p_pert.dim(), "D");
    TORCH_CHECK(c1h.dim() == 1, "c1h must be 1D [nz], got ", c1h.dim(), "D");
    TORCH_CHECK(muv.dim() == 2, "muv must be 2D [ny+1, nx], got ", muv.dim(), "D");

    // Get dimensions
    int ny = ph.size(0);
    int nz = p_pert.size(1);
    int nx = p_pert.size(2);
    int ny_v = ny + 1;  // V-staggered dimension

    // Shape consistency checks
    TORCH_CHECK(ph.size(1) == nz + 1, "ph k-dim must be nz+1=", nz+1, ", got ", ph.size(1));
    TORCH_CHECK(c1h.size(0) == nz, "c1h must have nz=", nz, " elements, got ", c1h.size(0));
    TORCH_CHECK(muv.size(0) == ny_v && muv.size(1) == nx,
        "muv shape must be [", ny_v, ", ", nx, "], got [", muv.size(0), ", ", muv.size(1), "]");

    // ==========================================================================
    // FIX 2025-01-11 Round35/Round39/Round41/Round42/Round43: Determine compute dtype across ALL inputs
    // ==========================================================================
    // FIX 2025-01-11 Round42/Round43: Determine output dtype - use parameter if provided, else ph dtype
    auto result_dtype = output_dtype.value_or(ph.scalar_type());
    // FIX 2025-01-11 Round41: Expanded to include ALL tensor inputs to prevent downcast
    auto compute_dtype = pg_detail::getMaxComputeDtype(
        ph, ph_base, p_pert, p_base, al_pert, alt,
        muv, c1h, c2h, msfvx, msfvy, cqv);
    auto target_device = ph.device();

    // Align all input tensors to target device/dtype
    auto ph_a = pg_detail::alignTensor(ph, target_device, compute_dtype);
    auto ph_base_a = pg_detail::alignTensor(ph_base, target_device, compute_dtype);
    auto p_pert_a = pg_detail::alignTensor(p_pert, target_device, compute_dtype);
    auto p_base_a = pg_detail::alignTensor(p_base, target_device, compute_dtype);
    auto al_pert_a = pg_detail::alignTensor(al_pert, target_device, compute_dtype);
    auto alt_a = pg_detail::alignTensor(alt, target_device, compute_dtype);
    auto muv_a = pg_detail::alignTensor(muv, target_device, compute_dtype);
    // FIX 2025-01-11 Round50: Use cached alignment for 1D constant tensors
    auto c1h_a = pg_detail::getCachedAlignedTensor(c1h, target_device, compute_dtype);
    auto c2h_a = pg_detail::getCachedAlignedTensor(c2h, target_device, compute_dtype);
    auto msfvx_a = pg_detail::alignTensor(msfvx, target_device, compute_dtype);
    auto msfvy_a = pg_detail::alignTensor(msfvy, target_device, compute_dtype);
    auto cqv_a = pg_detail::alignTensor(cqv, target_device, compute_dtype);

    // Convert rdy to tensor with appropriate dtype
    auto rdy_t = pg_detail::getRdxTensor(rdy, compute_dtype, target_device);

    // Total geopotential
    auto ph_total = ph_a + ph_base_a;
    
    // Similar vectorized implementation as U-momentum but for Y-direction
    // Pad in Y-direction for v-points
    auto ph_total_padded = torch::nn::functional::pad(
        ph_total,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );
    
    // Geopotential gradient in Y
    auto dph_k = ph_total_padded.index({Slice(2, None), Slice(0, -1), Slice()}) - 
                 ph_total_padded.index({Slice(0, -2), Slice(0, -1), Slice()});
    
    auto dph_kp1 = ph_total_padded.index({Slice(2, None), Slice(1, None), Slice()}) - 
                   ph_total_padded.index({Slice(0, -2), Slice(1, None), Slice()});
    
    auto dph_dy = dph_k + dph_kp1;
    
    // Pressure gradients
    auto p_pert_padded = torch::nn::functional::pad(
        p_pert_a,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );

    auto p_base_padded = torch::nn::functional::pad(
        p_base_a,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );
    
    auto dp_dy = p_pert_padded.index({Slice(2, None), Slice(), Slice()}) - 
                 p_pert_padded.index({Slice(0, -2), Slice(), Slice()});
    
    auto dpb_dy = p_base_padded.index({Slice(2, None), Slice(), Slice()}) - 
                  p_base_padded.index({Slice(0, -2), Slice(), Slice()});
    
    // Average inverse densities
    auto alt_padded = torch::nn::functional::pad(
        alt_a,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );

    auto al_pert_padded = torch::nn::functional::pad(
        al_pert_a,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );
    
    auto alt_avg = 0.5 * (alt_padded.index({Slice(2, None), Slice(), Slice()}) +
                          alt_padded.index({Slice(0, -2), Slice(), Slice()}));

    auto al_avg = 0.5 * (al_pert_padded.index({Slice(2, None), Slice(), Slice()}) +
                         al_pert_padded.index({Slice(0, -2), Slice(), Slice()}));
    
    // FIX 2025-12-28: Use autocast-aware eps to avoid FP16 underflow
    // NOTE: eps based on msfvy's dtype (no implicit upcast in torch::where or division)
    const float eps_v = metric_utils::getAutocastAwareEps(msfvy_a);
    auto msfvy_safe = torch::where(msfvy_a.abs() < eps_v,
                                   torch::ones_like(msfvy_a),
                                   msfvy_a);
    auto msf_ratio = msfvx_a / msfvy_safe;
    auto msf_ratio_3d = msf_ratio.unsqueeze(1).expand({ny_v, nz, nx});

    // Vertical coupling (using aligned tensors)
    auto c1h_3d = c1h_a.unsqueeze(0).unsqueeze(2).expand({ny_v, nz, nx});
    auto c2h_3d = c2h_a.unsqueeze(0).unsqueeze(2).expand({ny_v, nz, nx});
    auto muv_3d = muv_a.unsqueeze(1).expand({ny_v, nz, nx});
    auto vert_coupling = c1h_3d * muv_3d + c2h_3d;

    // Complete pressure gradient
    // FIX 2025-01-11 Round35: Use rdy_t tensor for FP64 precision preservation
    auto dpy = msf_ratio_3d * 0.5 * rdy_t * vert_coupling *
               (dph_dy + alt_avg * dp_dy + al_avg * dpb_dy);

    // Interpolate cqv to v-points (using aligned tensor)
    auto cqv_padded = torch::nn::functional::pad(
        cqv_a,
        torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 1})
            .mode(torch::kReplicate)
    );
    
    auto cqv_at_v = 0.5 * (cqv_padded.index({Slice(2, None), Slice(), Slice()}) +
                           cqv_padded.index({Slice(0, -2), Slice(), Slice()}));

    auto result = -cqv_at_v * dpy;

    // FIX 2025-01-11 Round42/Round43: Cast result to output dtype (defaults to ph's dtype)
    if (result.scalar_type() != result_dtype) {
        return result.to(result_dtype);
    }
    return result;
}

// =============================================================================
// Vectorized Pressure Gradient + Buoyancy for W-momentum
// =============================================================================

/**
 * Compute combined pressure gradient and buoyancy for W-momentum (fully vectorized)
 * 
 * WRF combines these terms in pg_buoy_w for efficiency
 */
inline torch::Tensor compute_pg_buoy_w_vectorized(
    const torch::Tensor& ph,         // [ny, nz+1, nx] geopotential perturbation
    const torch::Tensor& ph_base,    // [ny, nz+1, nx] base geopotential
    const torch::Tensor& p_pert,     // [ny, nz, nx] pressure perturbation
    const torch::Tensor& p_base,     // [ny, nz, nx] base pressure
    const torch::Tensor& al_pert,    // [ny, nz, nx] inverse density perturbation
    const torch::Tensor& alt,        // [ny, nz, nx] base inverse density
    const torch::Tensor& alb,        // [ny, nz, nx] base inverse density (1/rho_base)
    const torch::Tensor& theta_full, // [ny, nz, nx] full potential temperature
    const torch::Tensor& cqw,        // [ny, nz, nx] moisture correction at mass points
    const torch::Tensor& mu_full,    // [ny, nx] full column mass (muf in WRF)
    const torch::Tensor& mu_base,    // [ny, nx] base column mass (mubf in WRF)
    const torch::Tensor& msfty,      // [ny, nx] map scale factor y-direction
    const torch::Tensor& c1f,        // [nz+1] vertical coefficient at w-levels
    const torch::Tensor& c2f,        // [nz+1] vertical coefficient at w-levels
    const torch::Tensor& rdnw,       // [nz] reciprocal vertical spacing
    const torch::Tensor& rdn,        // [nz] reciprocal spacing between mass levels
    double g,                        // FIX 2025-01-11 Round48: double for FP64 precision
    [[maybe_unused]] double cv,      // FIX 2025-01-11 Round48/Round54: double for FP64 precision (unused, reserved for future)
    [[maybe_unused]] double cp,      // FIX 2025-01-11 Round48/Round54: double for FP64 precision (unused, reserved for future)
    c10::optional<torch::ScalarType> output_dtype = c10::nullopt)  // FIX Round43: Optional output dtype
{
    using namespace torch::indexing;

    // ==========================================================================
    // FIX 2025-01-11 Round35/Round41: Input shape validation
    // ==========================================================================
    TORCH_CHECK(ph.dim() == 3, "ph must be 3D [ny, nz+1, nx], got ", ph.dim(), "D");
    TORCH_CHECK(p_pert.dim() == 3, "p_pert must be 3D [ny, nz, nx], got ", p_pert.dim(), "D");
    TORCH_CHECK(mu_full.dim() == 2, "mu_full must be 2D [ny, nx], got ", mu_full.dim(), "D");
    TORCH_CHECK(c1f.dim() == 1, "c1f must be 1D [nz+1], got ", c1f.dim(), "D");
    TORCH_CHECK(rdnw.dim() == 1, "rdnw must be 1D [nz], got ", rdnw.dim(), "D");
    // FIX 2025-01-11 Round40: Add validation for rdn tensor
    TORCH_CHECK(rdn.dim() == 1, "rdn must be 1D [nz], got ", rdn.dim(), "D");
    // FIX 2025-01-11 Round41: Add validation for msfty, mu_base, c2f, cqw
    TORCH_CHECK(msfty.dim() == 2, "msfty must be 2D [ny, nx], got ", msfty.dim(), "D");
    TORCH_CHECK(mu_base.dim() == 2, "mu_base must be 2D [ny, nx], got ", mu_base.dim(), "D");
    TORCH_CHECK(c2f.dim() == 1, "c2f must be 1D [nz+1], got ", c2f.dim(), "D");
    TORCH_CHECK(cqw.dim() == 3, "cqw must be 3D [ny, nz, nx], got ", cqw.dim(), "D");

    // Get dimensions
    int ny = ph.size(0);
    int nz = p_pert.size(1);
    int nx = p_pert.size(2);
    int nz_w = nz + 1;

    // Shape consistency checks
    TORCH_CHECK(ph.size(1) == nz_w, "ph k-dim must be nz+1=", nz_w, ", got ", ph.size(1));
    TORCH_CHECK(c1f.size(0) == nz_w, "c1f must have nz+1=", nz_w, " elements, got ", c1f.size(0));
    TORCH_CHECK(rdnw.size(0) == nz, "rdnw must have nz=", nz, " elements, got ", rdnw.size(0));
    // FIX 2025-01-11 Round40: Add size validation for rdn tensor
    TORCH_CHECK(rdn.size(0) == nz, "rdn must have nz=", nz, " elements, got ", rdn.size(0));
    TORCH_CHECK(mu_full.size(0) == ny && mu_full.size(1) == nx,
        "mu_full shape must be [", ny, ", ", nx, "], got [", mu_full.size(0), ", ", mu_full.size(1), "]");
    // FIX 2025-01-11 Round41: Add size validation for msfty, mu_base, c2f, cqw
    TORCH_CHECK(msfty.size(0) == ny && msfty.size(1) == nx,
        "msfty shape must be [", ny, ", ", nx, "], got [", msfty.size(0), ", ", msfty.size(1), "]");
    TORCH_CHECK(mu_base.size(0) == ny && mu_base.size(1) == nx,
        "mu_base shape must be [", ny, ", ", nx, "], got [", mu_base.size(0), ", ", mu_base.size(1), "]");
    TORCH_CHECK(c2f.size(0) == nz_w, "c2f must have nz+1=", nz_w, " elements, got ", c2f.size(0));
    TORCH_CHECK(cqw.size(0) == ny && cqw.size(1) == nz && cqw.size(2) == nx,
        "cqw shape must be [", ny, ", ", nz, ", ", nx, "], got [", cqw.size(0), ", ", cqw.size(1), ", ", cqw.size(2), "]");

    // ==========================================================================
    // FIX 2025-01-11 Round35/Round39/Round41/Round42/Round43: Determine compute dtype across ALL inputs
    // ==========================================================================
    // FIX 2025-01-11 Round42/Round43: Determine output dtype - use parameter if provided, else ph dtype
    auto result_dtype = output_dtype.value_or(ph.scalar_type());
    // FIX 2025-01-11 Round41: Expanded to include ALL tensor inputs to prevent downcast
    auto compute_dtype = pg_detail::getMaxComputeDtype(
        ph, ph_base, p_pert, p_base, cqw, mu_full, mu_base, msfty, c1f, c2f, rdnw, rdn);
    auto target_device = ph.device();

    // Align all input tensors to target device/dtype
    auto ph_a = pg_detail::alignTensor(ph, target_device, compute_dtype);
    auto ph_base_a = pg_detail::alignTensor(ph_base, target_device, compute_dtype);
    auto p_pert_a = pg_detail::alignTensor(p_pert, target_device, compute_dtype);
    auto p_base_a = pg_detail::alignTensor(p_base, target_device, compute_dtype);
    auto cqw_a = pg_detail::alignTensor(cqw, target_device, compute_dtype);
    auto mu_full_a = pg_detail::alignTensor(mu_full, target_device, compute_dtype);
    auto mu_base_a = pg_detail::alignTensor(mu_base, target_device, compute_dtype);
    auto msfty_a = pg_detail::alignTensor(msfty, target_device, compute_dtype);
    // FIX 2025-01-11 Round50: Use cached alignment for 1D constant tensors
    auto c1f_a = pg_detail::getCachedAlignedTensor(c1f, target_device, compute_dtype);
    auto c2f_a = pg_detail::getCachedAlignedTensor(c2f, target_device, compute_dtype);
    auto rdnw_a = pg_detail::getCachedAlignedTensor(rdnw, target_device, compute_dtype);
    auto rdn_a = pg_detail::getCachedAlignedTensor(rdn, target_device, compute_dtype);

    // FIX 2025-01-11 Round53: Cast scalar g to compute_dtype to prevent promotion
    // When g (double) multiplies FP32 tensors, PyTorch may promote to FP64
    // Using a dtype-matched scalar tensor ensures consistent compute precision
    auto g_scalar = (compute_dtype == torch::kFloat64)
        ? torch::tensor(g, torch::TensorOptions().dtype(torch::kFloat64).device(target_device))
        : torch::tensor(static_cast<float>(g), torch::TensorOptions().dtype(torch::kFloat32).device(target_device));

    // Total pressure and geopotential (using aligned tensors)
    auto p_total = p_pert_a + p_base_a;
    auto ph_total = ph_a + ph_base_a;

    // =============================================================================
    // Step 1: Vertical pressure gradient dp/dz at w-points
    // =============================================================================

    // Initialize output tensor with compute dtype
    auto pg_buoy_w = torch::zeros({ny, nz_w, nx},
        torch::TensorOptions().dtype(compute_dtype).device(target_device));

    // FIX 2025-01-11 Round39: Vectorize k-loop for interior w-levels (k=1 to nz-1)
    // This eliminates the explicit loop and improves performance
    {
        // Pressure difference across all interior levels at once
        // dp[k] = p_total[:, k, :] - p_total[:, k-1, :] for k=1..nz-1
        auto dp = p_total.index({Slice(), Slice(1, nz), Slice()}) -
                  p_total.index({Slice(), Slice(0, nz-1), Slice()});

        // Broadcast rdn_a[1:nz] to [1, nz-1, 1] for element-wise multiply
        auto rdn_slice = rdn_a.index({Slice(1, nz)}).unsqueeze(0).unsqueeze(2);
        auto dp_dz = dp * rdn_slice;

        // Assign to interior levels (k=1 to nz-1)
        pg_buoy_w.index_put_({Slice(), Slice(1, nz), Slice()}, dp_dz);
    }
    
    // =============================================================================
    // Step 2: Column mass term (WRF's "buoyancy" term)
    // =============================================================================

    // WRF uses column mass terms, not thermodynamic buoyancy
    // Formula: -(c1f(k)*muf(i,j)) for dry dynamics

    // Expand c1f to 3D for vectorized operations (using aligned tensors)
    auto c1f_3d = c1f_a.unsqueeze(0).unsqueeze(2).expand({ny, nz_w, nx});

    // Expand mu_full to 3D (from 2D surface field)
    auto mu_full_3d = mu_full_a.unsqueeze(1).expand({ny, nz_w, nx});

    // Expand mu_base to 3D
    auto mu_base_3d = mu_base_a.unsqueeze(1).expand({ny, nz_w, nx});

    // Expand c2f to 3D
    auto c2f_3d = c2f_a.unsqueeze(0).unsqueeze(2).expand({ny, nz_w, nx});

    // Expand map scale factor to 3D
    auto msfty_3d = msfty_a.unsqueeze(1).expand({ny, nz_w, nx});
    
    // Column mass contribution per WRF: -(c1f(k)*muf(i,j))
    // For moist dynamics add: -cq2*(c1f(k)*mubf(i,j)+c2f(k))
    auto column_mass_term = c1f_3d * mu_full_3d;

    // FIX 2025-01-11 Round39: Vectorize k-loop for column mass term
    // Process all interior levels (k=1 to nz-1) at once
    // FIX 2025-01-11 Round40: Add eps clamp to prevent division by zero/tiny values
    const auto eps = metric_utils::getAutocastAwareEps(msfty_3d);
    {
        auto msfty_interior = msfty_3d.index({Slice(), Slice(1, nz), Slice()}).clamp_min(eps);
        auto map_factor = 1.0 / msfty_interior;
        auto pg_interior = pg_buoy_w.index({Slice(), Slice(1, nz), Slice()});
        auto col_mass_interior = column_mass_term.index({Slice(), Slice(1, nz), Slice()});

        pg_buoy_w.index_put_({Slice(), Slice(1, nz), Slice()},
            map_factor * g_scalar * (pg_interior - col_mass_interior));
    }
    
    // =============================================================================
    // Step 3: Apply moisture correction if needed
    // =============================================================================

    // For dry dynamics, cqw should be zero
    // WRF applies: cq1 = 1./(1.+cqw(i,k,j)) and cq2 = cqw(i,k,j)*cq1
    // Then adds: -cq2*(c1f(k)*mubf(i,j)+c2f(k)) to the tendency

    // FIX 2025-01-11 Round39: Vectorize k-loop for moisture correction
    // Process all interior levels (k=1 to nz-1) at once
    {
        // Average cqw to w-points for interior levels
        // cqw_k[k] = 0.5 * (cqw[:, k, :] + cqw[:, k-1, :]) for k=1..nz-1
        auto cqw_k = 0.5 * (cqw_a.index({Slice(), Slice(1, nz), Slice()}) +
                            cqw_a.index({Slice(), Slice(0, nz-1), Slice()}));
        auto cq1 = 1.0 / (1.0 + cqw_k);
        auto cq2 = cqw_k * cq1;

        // Apply moisture correction and add moist column mass term
        // FIX 2025-01-11 Round40: Reuse eps from earlier, add clamp for msfty division
        auto msfty_interior_moist = msfty_3d.index({Slice(), Slice(1, nz), Slice()}).clamp_min(eps);
        auto map_factor = 1.0 / msfty_interior_moist;
        auto c1f_interior = c1f_3d.index({Slice(), Slice(1, nz), Slice()});
        auto mu_base_interior = mu_base_3d.index({Slice(), Slice(1, nz), Slice()});
        auto c2f_interior = c2f_3d.index({Slice(), Slice(1, nz), Slice()});

        auto moist_term = cq2 * (c1f_interior * mu_base_interior + c2f_interior);

        auto pg_interior = pg_buoy_w.index({Slice(), Slice(1, nz), Slice()});
        // FIX 2025-01-11 Round53: Use g_scalar for dtype-aware casting
        pg_buoy_w.index_put_({Slice(), Slice(1, nz), Slice()},
            cq1 * pg_interior - map_factor * g_scalar * moist_term);
    }
    
    // =============================================================================
    // Step 4: Boundary conditions
    // =============================================================================
    
    // Bottom boundary (k=0): zero (rigid lower boundary)
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    // FIX 2025-01-11 Round39: Use 0.0 instead of 0.0f for FP64 compatibility
    pg_buoy_w.index_put_({Slice(), 0, Slice()}, 0.0);
    
    // Top boundary (k=nz): Special treatment per WRF
    // WRF uses: 2.*rdnw(k-1)*(-p(i,k-1,j)) - (c1f(k)*muf(i,j))
    {
        int k = nz; // Top w-level
        // FIX 2025-01-11 Round40: Add eps clamp for msfty division at top boundary
        auto msfty_top = msfty_3d.index({Slice(), k, Slice()}).clamp_min(eps);
        auto map_factor = 1.0 / msfty_top;
        auto dp_top = -2.0 * rdnw_a[nz-1] * p_total.index({Slice(), nz-1, Slice()});
        auto column_term = c1f_3d.index({Slice(), k, Slice()}) * mu_full_3d.index({Slice(), k, Slice()});

        // Handle moisture if present (using aligned tensor)
        auto cqw_top = cqw_a.index({Slice(), nz-1, Slice()});
        auto cq1 = 1.0 / (1.0 + cqw_top);
        auto cq2 = cqw_top * cq1;
        auto moist_term = cq2 * (c1f_3d.index({Slice(), k, Slice()}) * mu_base_3d.index({Slice(), k, Slice()}) + 
                                 c2f_3d.index({Slice(), k, Slice()}));
        
        // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
        // FIX 2025-01-11 Round53: Use g_scalar for dtype-aware casting
        pg_buoy_w.index_put_({Slice(), k, Slice()},
            map_factor * g_scalar * (cq1 * dp_top - column_term - moist_term));
    }

    // FIX 2025-01-11 Round42/Round43: Cast result to output dtype (defaults to ph's dtype)
    if (pg_buoy_w.scalar_type() != result_dtype) {
        return pg_buoy_w.to(result_dtype);
    }
    return pg_buoy_w;
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_PRESSURE_GRADIENT_VECTORIZED_H