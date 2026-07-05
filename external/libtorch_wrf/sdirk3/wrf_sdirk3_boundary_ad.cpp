/**
 * @file wrf_sdirk3_boundary_ad.cpp
 * @brief AD-compatible boundary conditions for WRF SDIRK3
 *
 * AD DESIGN NOTES (2025-12-26):
 * ============================================================================
 * Current implementation uses IN-PLACE boundary operations with NoGradGuard.
 * This is intentional because:
 * 1. Boundary conditions are EXTERNAL CONSTRAINTS, not part of physics dynamics
 * 2. In-place ops are faster and memory-efficient for production runs
 * 3. AD through boundary conditions is rarely needed (BCs are prescribed, not learned)
 *
 * If AD through boundaries IS needed (e.g., learning BC parameters), add an
 * OUT-OF-PLACE path with interior masking:
 *
 *   // Out-of-place AD-preserving boundary application
 *   torch::Tensor applyBoundaryAD_OutOfPlace(const torch::Tensor& field,
 *                                             const torch::Tensor& bc_values) {
 *       if (!field.requires_grad()) {
 *           // Fast in-place path
 *           auto result = field.clone();
 *           // ... apply in-place ...
 *           return result;
 *       }
 *       // AD-preserving path: mask interior, add boundary padding
 *       auto interior = field.slice(dim, 1, size-1);  // or use index masking
 *       auto padded = torch::constant_pad_nd(interior, {1,1,...}, 0.0f);
 *       // Add boundary values to padded tensor (pure functional ops)
 *       return padded + bc_contribution;
 *   }
 *
 * Cache invalidation:
 * - Call invalidateLatCpuCache() when xlat is modified in-place
 * - Call invalidateDzMinCache() when dz is modified in-place
 * - Or use invalidateVerticalMetricCaches() for all metric caches
 * ============================================================================
 */

#include <torch/torch.h>
#include <functional>
#include <unordered_map>
#include <vector>  // FIX 2025-12-26: For stride vector in DzMinCache
#include <array>   // FIX 2025-12-26: For multi-slot LRU cache
#include <atomic>  // FIX 2025-12-26: For thread-safe epoch counter
#include <cmath>   // FIX 2025-12-27: For std::isfinite
#include "wrf_config_flags.h"
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_boundary_ad.h"
#include "wrf_sdirk3_metric_utils.h"  // For normalize_positive
#include "wrf_sdirk3_config.h"        // FIX 2025-12-27: For g_sdirk3_config

namespace wrf {
namespace sdirk3 {

using BoundaryOp = std::function<void(torch::Tensor&, int, int, int)>;

// FIX 2025-12-26: Cache dz_min reduction to avoid repeated min() every CFL check
// Cache is invalidated when dz tensor changes (data_ptr, numel, device, dtype, or epoch)
// FIX 2025-12-30 Batch30 Issue 4: Added 3-point samples + periodic full-verify for moving_nest
struct DzMinCache {
    const void* cached_ptr = nullptr;
    int64_t cached_numel = 0;
    int64_t cached_dim = 0;                 // FIX 2025-12-26: Track tensor dim
    int64_t cached_storage_offset = 0;      // FIX 2025-12-30 Batch30 Issue 4: Storage offset
    std::vector<int64_t> cached_strides;    // FIX 2025-12-26: Track full stride vector
    torch::Device cached_device = torch::kCPU;
    torch::ScalarType cached_dtype = torch::kFloat32;
    torch::Tensor cached_dz_min;  // 0D tensor on CPU (for AD tensor path base)
    float cached_dz_min_value = 0.0f;  // FIX 2025-12-26: Scalar value to avoid GPU sync

    // FIX 2025-12-30 Batch30 Issue 4: 3-point sample signature for in-place detection
    float sample_first = 0.0f;
    float sample_mid = 0.0f;
    float sample_last = 0.0f;

    // FIX 2025-12-30 Batch30 Issue 4: Periodic full-verify for moving_nest
    // FIX 2025-12-30 Batch32 Issue 3: Removed hardcoded VERIFY_PERIOD=100, use metric_utils::getVerifyPeriod()
    mutable uint64_t access_count = 0;

    // FIX 2025-12-27: Device/dtype-specific tensor cache to avoid .to() on every hit
    torch::Device cached_tensor_device = torch::kCPU;
    torch::ScalarType cached_tensor_dtype = torch::kFloat32;
    torch::Tensor cached_dz_min_tensor;  // 0D tensor on target device/dtype

    // Share epoch with Z1DProfileCache if cross-file coordination needed
    // FIX 2025-12-26: Use std::atomic for thread safety (relaxed ordering suffices)
    static inline std::atomic<uint64_t> metric_epoch{0};
    uint64_t cached_epoch = 0;

    bool is_valid(const torch::Tensor& dz) const {
        if (!dz.defined() || dz.numel() == 0) return false;
        if (!cached_dz_min.defined()) return false;
        // Bypass cache if dz requires grad (shouldn't happen for grid metric)
        if (dz.requires_grad()) return false;
        // FIX 2025-12-26: Compare dims first (different dims = different layout)
        if (dz.dim() != cached_dim) return false;
        // FIX 2025-12-30 Batch30 Issue 4: Check storage offset for view safety
        if (dz.storage_offset() != cached_storage_offset) return false;
        // FIX 2025-12-26: Compare strides to catch view/transpose differences
        auto current_strides = dz.strides();
        if (current_strides.size() != cached_strides.size()) return false;
        for (size_t i = 0; i < current_strides.size(); ++i) {
            if (current_strides[i] != cached_strides[i]) return false;
        }
        if (dz.data_ptr() != cached_ptr ||
            dz.numel() != cached_numel ||
            dz.device() != cached_device ||
            dz.scalar_type() != cached_dtype ||
            cached_epoch != metric_epoch.load(std::memory_order_relaxed)) {
            return false;
        }
        // FIX 2025-12-30 Batch30 Issue 4: 3-point sample check for in-place modifications
        int64_t n = dz.numel();
        auto flat = dz.flatten();
        if (flat.is_cpu() && flat.is_contiguous()) {
            const float* ptr = flat.data_ptr<float>();
            if (ptr[0] != sample_first) return false;
            if (n > 1 && ptr[n/2] != sample_mid) return false;
            if (n > 1 && ptr[n-1] != sample_last) return false;
        }
        return true;
    }

    // FIX 2025-12-30 Batch30 Issue 4: Check if periodic full-verify needed
    // FIX 2025-12-30 Batch32 Issue 3: Use config-based verify period from metric_utils
    bool needs_full_verify() const {
        uint64_t period = metric_utils::getVerifyPeriod();
        return (access_count % period) == 0;
    }

    // FIX 2025-12-30 Batch30 Issue 4: Full verification by recomputing min
    // FIX 2025-12-30 Batch32 Issue 3: Apply normalize_positive() to handle NaN/Inf/negative
    bool full_verify(const torch::Tensor& dz) const {
        torch::NoGradGuard no_grad;
        // Use normalize_positive() to sanitize before min - handles NaN/Inf/negative
        auto normalized = metric_utils::normalize_positive(dz, 1e-6f);
        float actual_min = torch::min(normalized).to(torch::kCPU).item<float>();
        // If actual_min is non-finite (shouldn't happen after normalize), invalidate cache
        if (!std::isfinite(actual_min)) return false;
        float diff = std::abs(actual_min - cached_dz_min_value);
        float scale = std::max(std::abs(actual_min), 1e-10f);
        return (diff / scale) < 1e-5f;
    }

    void update(const torch::Tensor& dz, const torch::Tensor& dz_min, float dz_min_value) {
        cached_ptr = dz.data_ptr();
        cached_numel = dz.numel();
        cached_dim = dz.dim();  // FIX 2025-12-26: Track dim
        cached_storage_offset = dz.storage_offset();  // FIX 2025-12-30 Batch30 Issue 4
        // FIX 2025-12-26: Store strides for stride-based invalidation
        auto strides = dz.strides();
        cached_strides.assign(strides.begin(), strides.end());
        cached_device = dz.device();
        cached_dtype = dz.scalar_type();
        cached_epoch = metric_epoch.load(std::memory_order_relaxed);
        cached_dz_min = dz_min;
        cached_dz_min_value = dz_min_value;  // FIX 2025-12-26: Store scalar value
        access_count = 0;  // FIX 2025-12-30 Batch30 Issue 4: Reset on update
        // FIX 2025-12-27: Invalidate device/dtype tensor on base cache update
        cached_dz_min_tensor = torch::Tensor();

        // FIX 2025-12-30 Batch30 Issue 4: Store 3-point sample signature
        int64_t n = dz.numel();
        auto flat = dz.flatten();
        if (flat.is_cpu() && flat.is_contiguous() && n > 0) {
            const float* ptr = flat.data_ptr<float>();
            sample_first = ptr[0];
            sample_mid = (n > 1) ? ptr[n/2] : ptr[0];
            sample_last = (n > 1) ? ptr[n-1] : ptr[0];
        } else if (n > 0) {
            // GPU path: single transfer for 3 samples
            auto cpu = flat.to(torch::kCPU, torch::kFloat32).contiguous();
            const float* ptr = cpu.data_ptr<float>();
            sample_first = ptr[0];
            sample_mid = (n > 1) ? ptr[n/2] : ptr[0];
            sample_last = (n > 1) ? ptr[n-1] : ptr[0];
        }
    }

    // FIX 2025-12-27: Check if tensor cache matches target device/dtype
    bool has_tensor_for(torch::Device device, torch::ScalarType dtype) const {
        return cached_dz_min_tensor.defined() &&
               cached_tensor_device == device &&
               cached_tensor_dtype == dtype;
    }

    // FIX 2025-12-27: Get or create tensor for target device/dtype (avoids .to() overhead)
    torch::Tensor get_tensor_for(torch::Device device, torch::ScalarType dtype) {
        if (has_tensor_for(device, dtype)) {
            return cached_dz_min_tensor;
        }
        // Create and cache tensor for this device/dtype
        if (cached_dz_min.defined()) {
            cached_dz_min_tensor = cached_dz_min.to(device, dtype);
            cached_tensor_device = device;
            cached_tensor_dtype = dtype;
            return cached_dz_min_tensor;
        }
        // Fallback: create from scalar value
        cached_dz_min_tensor = torch::scalar_tensor(cached_dz_min_value,
            torch::TensorOptions().device(device).dtype(dtype));
        cached_tensor_device = device;
        cached_tensor_dtype = dtype;
        return cached_dz_min_tensor;
    }

    void invalidate() {
        cached_ptr = nullptr;
        cached_numel = 0;
        cached_dim = 0;
        cached_strides.clear();
        cached_epoch = 0;
        cached_dz_min = torch::Tensor();
        cached_dz_min_value = 0.0f;
        // FIX 2025-12-27: Clear device/dtype tensor cache
        cached_dz_min_tensor = torch::Tensor();
    }

    static void incrementEpoch() { metric_epoch.fetch_add(1, std::memory_order_relaxed); }
};

static thread_local DzMinCache dz_min_cache;

// FIX 2025-12-26: Cache lat_cpu and lat_tensor to avoid repeated xlat→CPU copies
// Cache is invalidated when xlat tensor changes (data_ptr, numel, strides, device, dtype, or sample values)
// FIX 2025-12-26: Uses 3-point index_select from xlat.select(1,0) to match actual usage
// FIX 2025-12-26: Caches both lat_cpu (FP32 CPU) and lat_tensor (target device/dtype)
// FIX 2025-12-26: Dual epoch tracking to avoid permanent cache miss
// FIX 2025-12-26: Multi-slot LRU cache for multi-domain scenarios (avoids cache thrash)
constexpr size_t LAT_CACHE_SLOTS = 4;  // Support up to 4 nested domains

struct LatCpuCacheSlot {
    // xlat identity
    const void* cached_ptr = nullptr;
    int64_t cached_numel = 0;
    std::vector<int64_t> cached_strides;
    torch::Device cached_device = torch::kCPU;
    torch::ScalarType cached_dtype = torch::kFloat32;

    // FIX 2025-12-27: Lat cache uses only lat_epoch, not metric_epoch
    // This prevents unnecessary invalidation when dz changes (dz and lat are independent)
    // Previously checked both epochs, causing lat cache invalidation on dz changes
    uint64_t cached_lat_epoch = 0;

    // FIX 2025-12-27: 5-point sample signature from xlat.select(1,0) for better in-place change detection
    // Matches rdnw cache pattern (0%, 25%, 50%, 75%, 100%)
    float sample_0 = 0.0f;    // first (0%)
    float sample_25 = 0.0f;   // 25%
    float sample_50 = 0.0f;   // mid (50%)
    float sample_75 = 0.0f;   // 75%
    float sample_100 = 0.0f;  // last (100%)

    // FIX 2025-12-26: Periodic sample check counter for safety without full cost
    mutable uint64_t sample_check_counter = 0;

    // Cached results
    torch::Tensor cached_lat_cpu;  // [ny] on CPU, FP32

    // Cached lat_tensor for specific target device/dtype
    torch::Device cached_tensor_device = torch::kCPU;
    torch::ScalarType cached_tensor_dtype = torch::kFloat32;
    torch::Tensor cached_lat_tensor;  // [ny] on target device/dtype

    // FIX 2025-12-29 Batch11 Issue 6: Cached polar_mask to avoid H2D transfer per call
    // polar_mask depends on (lat_cpu, filter_lat, device), so we track filter_lat too
    torch::Tensor cached_polar_mask;  // [ny] on target device
    torch::Device cached_polar_device = torch::kCPU;
    float cached_polar_filter_lat = -999.0f;  // Invalid sentinel

    // FIX 2025-12-30 Batch28 Issue 5: Cache polar_mask_cpu.any() result
    // Avoids repeated .any().item<bool>() on every call when cache hit
    bool cached_has_polar = false;
    // FIX 2025-12-31 Batch41 Issue 3: Separate filter_lat tracker for has_polar cache
    // Prevents synchronization bug where update_has_polar() would invalidate polar_mask cache
    float cached_has_polar_filter_lat = -999.0f;

    // FIX 2025-12-29 Batch11 Issue 6: Check if polar_mask is cached for (filter_lat, device)
    bool has_polar_mask_for(float filter_lat, torch::Device device) const {
        return cached_polar_mask.defined() &&
               cached_polar_device == device &&
               std::abs(cached_polar_filter_lat - filter_lat) < 1e-6f;
    }

    // FIX 2025-12-30 Batch28 Issue 5: Check if has_polar result is cached for filter_lat
    bool has_cached_has_polar(float filter_lat) const {
        // FIX 2025-12-31 Batch41 Issue 3: Use dedicated has_polar filter_lat tracker
        return std::abs(cached_has_polar_filter_lat - filter_lat) < 1e-6f;
    }

    // FIX 2025-12-29 Batch11 Issue 6: Update polar_mask cache
    void update_polar_mask(const torch::Tensor& polar_mask, float filter_lat, torch::Device device) {
        cached_polar_mask = polar_mask;
        cached_polar_filter_lat = filter_lat;
        cached_polar_device = device;
    }

    // FIX 2025-12-30 Batch28 Issue 5: Update cached has_polar result
    void update_has_polar(bool has_polar, float filter_lat) {
        cached_has_polar = has_polar;
        // FIX 2025-12-31 Batch41 Issue 3: Use dedicated tracker, don't touch polar_mask's filter_lat
        cached_has_polar_filter_lat = filter_lat;
    }

    // Static epoch for external invalidation
    static inline std::atomic<uint64_t> lat_epoch{0};

    // FIX 2025-12-27: Cache for 5-point sample indices (size, device) -> tensor
    // Avoids repeated tensor creation on periodic sample checks
    // FIX 2025-01-26: Use constructor instead of default member initializers to avoid
    // clang error with thread_local nested structs (default initializer needed outside member functions)
    struct SampleIndexCache {
        int64_t cached_n;
        torch::Device cached_device;
        torch::Tensor cached_indices;

        SampleIndexCache() : cached_n(0), cached_device(torch::kCPU), cached_indices() {}

        torch::Tensor get_indices(int64_t n, torch::Device device) {
            if (n == cached_n && device == cached_device && cached_indices.defined()) {
                return cached_indices;
            }
            // Create and cache new 5-point indices tensor (0%, 25%, 50%, 75%, 100%)
            int64_t i0 = 0;
            // FIX 2025-01-26: Use int64_t(0) instead of 0L to avoid type mismatch on ARM64
            // (long is 64-bit but int64_t is long long on ARM64)
            int64_t i25 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) / 4));
            int64_t i50 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) / 2));
            int64_t i75 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) * 3 / 4));
            int64_t i100 = (n <= 1) ? 0 : n - 1;
            cached_indices = torch::tensor({i0, i25, i50, i75, i100}, torch::kLong).to(device);
            cached_n = n;
            cached_device = device;
            return cached_indices;
        }
    };
    static inline thread_local SampleIndexCache sample_index_cache;

    // FIX 2025-12-27: Helper: Get 5-point sample from col0 (xlat.select(1,0)) efficiently
    // Matches rdnw cache pattern for better in-place change detection
    // FIX Round186: Added NoGradGuard - this is a diagnostic path that should not build AD graph
    static void get5PointSample(const torch::Tensor& col0,
                                float& s0, float& s25, float& s50, float& s75, float& s100) {
        torch::NoGradGuard no_grad;  // FIX Round186: Prevent AD pollution from diagnostic sampling
        int64_t n = col0.numel();
        if (n == 0) {
            s0 = s25 = s50 = s75 = s100 = 0.0f;
            return;
        }

        // FIX Round188: CPU fast-path - direct data_ptr read for CPU+contiguous+FP32
        // Avoids index_select overhead (~50% faster for diagnostic path)
        if (col0.is_cpu() && col0.is_contiguous() && col0.scalar_type() == torch::kFloat32) {
            auto* p = col0.data_ptr<float>();
            // FIX 2025-01-26: Use int64_t(0) instead of 0L to avoid type mismatch on ARM64
            // (long is 64-bit but int64_t is long long on ARM64)
            int64_t i25 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) / 4));
            int64_t i50 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) / 2));
            int64_t i75 = (n <= 1) ? 0 : std::min(n - 1, std::max(int64_t(0), (n - 1) * 3 / 4));
            s0 = p[0];
            s25 = p[i25];
            s50 = p[i50];
            s75 = p[i75];
            s100 = p[n - 1];
            return;
        }

        // Fallback: GPU or non-contiguous path - use index_select
        // FIX 2025-12-27: Use cached indices tensor to avoid repeated allocation
        auto indices = sample_index_cache.get_indices(n, col0.device());
        auto samples = col0.flatten().index_select(0, indices).to(torch::kCPU, torch::kFloat32);
        // FIX 2025-12-27: Use data_ptr instead of .item() for CPU tensor access
        // samples is guaranteed CPU float32, direct pointer access avoids call overhead
        auto* p = samples.data_ptr<float>();
        s0 = p[0];
        s25 = p[1];
        s50 = p[2];
        s75 = p[3];
        s100 = p[4];
    }

    bool is_valid(const torch::Tensor& xlat) const {
        if (!xlat.defined() || xlat.numel() == 0) return false;
        if (!cached_lat_cpu.defined()) return false;

        // FIX 2025-12-27: Only check lat_epoch (not metric_epoch)
        // Lat cache is independent of dz changes - only invalidate when lat changes
        uint64_t current_lat_epoch = lat_epoch.load(std::memory_order_relaxed);
        if (cached_lat_epoch != current_lat_epoch) {
            return false;
        }

        // Check identity (ptr, numel, device, dtype, strides)
        if (xlat.data_ptr() != cached_ptr) return false;
        if (xlat.numel() != cached_numel) return false;
        if (xlat.device() != cached_device) return false;
        if (xlat.scalar_type() != cached_dtype) return false;

        // Check strides
        auto current_strides = xlat.strides();
        if (current_strides.size() != cached_strides.size()) return false;
        for (size_t i = 0; i < current_strides.size(); ++i) {
            if (current_strides[i] != cached_strides[i]) return false;
        }

        // FIX 2025-12-29 Batch12 Issue 5: Always check midpoint signature as core identity
        // This catches in-place xlat modifications reliably even when moving_nest=false
        // Only use fast CPU path to avoid GPU sync; skip check if xlat is on GPU
        if (xlat.dim() == 2 && xlat.size(1) > 0) {
            auto col0 = xlat.select(1, 0);
            int64_t n = col0.numel();
            if (n > 0) {
                int64_t mid_idx = n / 2;
                // Fast CPU path: direct pointer access for CPU+contiguous+float32
                if (col0.device().type() == torch::kCPU &&
                    col0.is_contiguous() &&
                    col0.scalar_type() == torch::kFloat32) {
                    float cur_mid = col0.data_ptr<float>()[mid_idx];
                    if (std::abs(cur_mid - sample_50) > 1e-6f) return false;
                }
                // For GPU tensors, defer to periodic full check below (avoids D2H sync)
            }
        }

        // FIX 2025-12-26: Periodic sample check for safety without full cost
        // Every N calls (or when debug_level >= 2), verify sample signature
        // This catches in-place xlat modifications without epoch increment
        // FIX 2025-12-27: Use runtime-configurable period from g_sdirk3_config
        // FIX 2025-12-27: Only increment counter when all basic checks pass (valid hit path)
        // This prevents spurious counter inflation when iterating through invalid slots
        uint64_t sample_period = g_sdirk3_config.lat_cpu_sample_check_period;
        if (sample_period == 0) {
            // FIX 2025-12-28: Auto-detect based on config flags
            // Moving nest requires aggressive checking since xlat changes frequently
            if (g_sdirk3_config.moving_nest) {
                sample_period = 1;  // Check every call for moving nests
            } else {
#ifdef NDEBUG
                sample_period = 100;  // Release: every 100 calls
#else
                sample_period = 10;   // Debug: every 10 calls
#endif
            }
        }
        // Increment only on valid hit path (all basic checks passed above)
        ++sample_check_counter;
        bool do_sample_check = (sample_check_counter % sample_period == 0);
        if (do_sample_check && xlat.dim() == 2 && xlat.size(1) > 0) {
            auto col0 = xlat.select(1, 0);
            // FIX 2025-12-27: 5-point sample comparison for better in-place change detection
            float cur_0, cur_25, cur_50, cur_75, cur_100;
            get5PointSample(col0, cur_0, cur_25, cur_50, cur_75, cur_100);
            if (cur_0 != sample_0 || cur_25 != sample_25 || cur_50 != sample_50 ||
                cur_75 != sample_75 || cur_100 != sample_100) {
                // Sample mismatch - cache is stale, but counter already incremented
                // This is acceptable since we only get here on valid-looking slots
                return false;
            }
        }

        return true;
    }

    // Check if cached lat_tensor matches target device/dtype
    bool has_tensor_for(torch::Device device, torch::ScalarType dtype) const {
        return cached_lat_tensor.defined() &&
               cached_tensor_device == device &&
               cached_tensor_dtype == dtype;
    }

    void update(const torch::Tensor& xlat, const torch::Tensor& lat_cpu) {
        cached_ptr = xlat.data_ptr();
        cached_numel = xlat.numel();
        cached_device = xlat.device();
        cached_dtype = xlat.scalar_type();
        auto strides = xlat.strides();
        cached_strides.assign(strides.begin(), strides.end());
        cached_lat_cpu = lat_cpu;
        // FIX 2025-12-27: Only store lat_epoch (not metric_epoch)
        cached_lat_epoch = lat_epoch.load(std::memory_order_relaxed);
        // FIX 2025-12-27: Store 5-point sample signature from xlat.select(1,0)
        if (xlat.dim() == 2 && xlat.size(1) > 0) {
            auto col0 = xlat.select(1, 0);
            get5PointSample(col0, sample_0, sample_25, sample_50, sample_75, sample_100);
        }
        // Reset sample check counter on cache update
        sample_check_counter = 0;
        // Invalidate cached tensor (will be lazily regenerated)
        cached_lat_tensor = torch::Tensor();
    }

    void update_tensor(const torch::Tensor& lat_tensor, torch::Device device, torch::ScalarType dtype) {
        cached_lat_tensor = lat_tensor;
        cached_tensor_device = device;
        cached_tensor_dtype = dtype;
    }

    void invalidate() {
        cached_ptr = nullptr;
        cached_numel = 0;
        cached_strides.clear();
        cached_device = torch::kCPU;
        cached_dtype = torch::kFloat32;
        // FIX 2025-12-27: Only reset lat_epoch (metric_epoch removed)
        cached_lat_epoch = 0;
        // FIX 2025-12-27: Reset all 5-point sample fields
        sample_0 = sample_25 = sample_50 = sample_75 = sample_100 = 0.0f;
        sample_check_counter = 0;
        cached_lat_cpu = torch::Tensor();
        cached_lat_tensor = torch::Tensor();
        // FIX 2025-12-29 Batch11 Issue 6: Reset polar_mask cache
        cached_polar_mask = torch::Tensor();
        cached_polar_filter_lat = -999.0f;
        cached_polar_device = torch::kCPU;
        // FIX 2025-12-31 Batch41 Issue 3: Reset has_polar cache with its dedicated filter_lat tracker
        cached_has_polar = false;
        cached_has_polar_filter_lat = -999.0f;
    }

    // FIX 2025-12-26: LRU counter for multi-slot eviction
    mutable uint64_t lru_counter = 0;

    static void incrementEpoch() { lat_epoch.fetch_add(1, std::memory_order_relaxed); }
};

// FIX 2025-12-26: Multi-slot LRU cache for multi-domain scenarios
// Prevents cache thrashing when alternating between domains
struct LatCpuCache {
    std::array<LatCpuCacheSlot, LAT_CACHE_SLOTS> slots;
    mutable uint64_t global_lru_counter = 0;

    // FIX 2025-12-27: Cache last lookup to reduce redundant slot searches
    mutable const torch::Tensor* last_xlat_ptr = nullptr;
    mutable LatCpuCacheSlot* last_slot = nullptr;

    // Find valid slot for xlat, or return nullptr if not cached
    // FIX 2025-12-27: Uses cached lookup if same xlat pointer
    LatCpuCacheSlot* find_slot(const torch::Tensor& xlat) {
        // Fast path: if same xlat pointer as last lookup, return cached slot
        if (last_xlat_ptr == &xlat && last_slot != nullptr &&
            last_slot->is_valid(xlat)) {
            last_slot->lru_counter = ++global_lru_counter;
            return last_slot;
        }
        // Slow path: search all slots
        for (auto& slot : slots) {
            if (slot.is_valid(xlat)) {
                slot.lru_counter = ++global_lru_counter;
                // Cache this lookup
                last_xlat_ptr = &xlat;
                last_slot = &slot;
                return &slot;
            }
        }
        return nullptr;
    }

    // Find or allocate slot for xlat (evicts LRU if full)
    LatCpuCacheSlot* get_or_create_slot(const torch::Tensor& xlat) {
        // First check if xlat is already cached
        if (auto* slot = find_slot(xlat)) {
            return slot;
        }
        // Find empty slot or LRU slot
        LatCpuCacheSlot* target = &slots[0];
        for (auto& slot : slots) {
            if (!slot.cached_lat_cpu.defined()) {
                // Empty slot - use it
                target = &slot;
                break;
            }
            if (slot.lru_counter < target->lru_counter) {
                target = &slot;
            }
        }
        // Invalidate and return for caller to populate
        target->invalidate();
        target->lru_counter = ++global_lru_counter;
        // Cache this new slot
        last_xlat_ptr = &xlat;
        last_slot = target;
        return target;
    }

    // FIX 2025-12-27: Direct slot access API to avoid repeated lookups
    // Returns slot if found, nullptr otherwise. Caller caches pointer for reuse.
    LatCpuCacheSlot* get_slot(const torch::Tensor& xlat) {
        return find_slot(xlat);
    }

    // Convenience accessors for primary slot (backward compatibility)
    bool is_valid(const torch::Tensor& xlat) const {
        for (const auto& slot : slots) {
            if (slot.is_valid(xlat)) return true;
        }
        return false;
    }

    // Get cached_lat_cpu for xlat (assumes is_valid() returned true)
    torch::Tensor get_cached_lat_cpu(const torch::Tensor& xlat) {
        if (auto* slot = find_slot(xlat)) {
            return slot->cached_lat_cpu;
        }
        return torch::Tensor();
    }

    // Check if any slot has tensor for device/dtype
    bool has_tensor_for(const torch::Tensor& xlat, torch::Device device, torch::ScalarType dtype) {
        if (auto* slot = find_slot(xlat)) {
            return slot->has_tensor_for(device, dtype);
        }
        return false;
    }

    // Get cached_lat_tensor for xlat
    torch::Tensor get_cached_lat_tensor(const torch::Tensor& xlat) {
        if (auto* slot = find_slot(xlat)) {
            return slot->cached_lat_tensor;
        }
        return torch::Tensor();
    }

    // Update cache for xlat
    void update(const torch::Tensor& xlat, const torch::Tensor& lat_cpu) {
        auto* slot = get_or_create_slot(xlat);
        slot->update(xlat, lat_cpu);
    }

    // Update tensor cache for xlat
    void update_tensor(const torch::Tensor& xlat, const torch::Tensor& lat_tensor,
                       torch::Device device, torch::ScalarType dtype) {
        if (auto* slot = find_slot(xlat)) {
            slot->update_tensor(lat_tensor, device, dtype);
        }
    }

    // Invalidate all slots
    void invalidate_all() {
        for (auto& slot : slots) {
            slot.invalidate();
        }
        // FIX 2025-12-27: Clear cached lookup on invalidation
        last_xlat_ptr = nullptr;
        last_slot = nullptr;
    }
};

static thread_local LatCpuCache lat_cpu_cache;

// Boundary operation templates
// FIX 2025-12-26: Use copy_() instead of select()=... which doesn't modify original
// FIX 2025-12-26: These are in-place ops - caller must use NoGradGuard if AD is active
namespace boundary_ops {

    template<int dim>
    inline void periodic(torch::Tensor& field, int start_idx, int end_idx) {
        // FIX 2025-12-26: select()=... rebinds temporary; use copy_() for in-place
        field.select(dim, 0).copy_(field.select(dim, end_idx - 2));
        field.select(dim, end_idx - 1).copy_(field.select(dim, 1));
    }

    template<int dim>
    inline void symmetric_scalar(torch::Tensor& field, int idx) {
        // FIX 2025-12-26: Use copy_() for in-place modification
        field.select(dim, idx).copy_(field.select(dim, idx == 0 ? 1 : idx - 1));
    }

    template<int dim>
    inline void zero_normal(torch::Tensor& field, int idx) {
        // FIX 2025-12-26: Use zero_() or fill_() for in-place modification
        field.select(dim, idx).zero_();
    }

    template<int dim>
    inline void open_radiation(torch::Tensor& field, const torch::Tensor& field_old,
                               float phase_speed, float dt, float dx, int idx) {
        auto courant = phase_speed * dt / dx;
        // FIX 2025-12-26: Use copy_() for in-place modification
        if (idx == 0) {
            auto new_val = field_old.select(dim, 0)
                - courant * (field_old.select(dim, 1) - field_old.select(dim, 0));
            field.select(dim, 0).copy_(new_val);
        } else {
            auto new_val = field_old.select(dim, idx)
                - courant * (field_old.select(dim, idx) - field_old.select(dim, idx - 1));
            field.select(dim, idx).copy_(new_val);
        }
    }
}

// Stagger-aware boundary dispatcher
// FIX 2025-12-26: In-place boundary ops break AD graph - use NoGradGuard
// FIX 2025-12-26: In AD strict mode, throw instead of warn (user wants explicit errors)
class BoundaryDispatcher {
private:
    using StaggerMap = std::unordered_map<int, BoundaryOp>;
    StaggerMap x_ops, y_ops, z_ops;

    // Helper: Check requires_grad and warn/throw based on AD strict mode
    static void checkADSafety(const torch::Tensor& field, const char* method) {
        if (field.requires_grad()) {
            if (is_ad_strict_mode()) {
                TORCH_CHECK(false,
                    "BoundaryDispatcher::", method, ": field requires_grad but boundary ops are in-place. "
                    "AD graph will be broken. Detach boundary regions or disable ad_strict_mode.");
            } else {
                TORCH_WARN_ONCE(
                    "BoundaryDispatcher::", method, ": field requires_grad but boundary ops are in-place. "
                    "AD graph will be broken. Consider detaching boundary regions.");
            }
        }
    }

public:
    void register_x(int stagger, BoundaryOp op) { x_ops[stagger] = op; }
    void register_y(int stagger, BoundaryOp op) { y_ops[stagger] = op; }
    void register_z(int stagger, BoundaryOp op) { z_ops[stagger] = op; }

    void apply_x(torch::Tensor& field, int stagger) {
        if (x_ops.count(stagger)) {
            // FIX 2025-12-26: In-place boundary ops break AD - check and use NoGradGuard
            checkADSafety(field, "apply_x");
            torch::NoGradGuard no_grad;
            x_ops[stagger](field, 0, field.size(2), field.size(2));
        }
    }

    void apply_y(torch::Tensor& field, int stagger) {
        if (y_ops.count(stagger)) {
            // FIX 2025-12-26: In-place boundary ops break AD - check and use NoGradGuard
            checkADSafety(field, "apply_y");
            torch::NoGradGuard no_grad;
            y_ops[stagger](field, 0, field.size(0), field.size(0));
        }
    }
};

// Main boundary condition application
void applyPeriodicBoundaryAD(
    torch::Tensor& field,
    int stagger,
    const ConfigFlags& config) {
    
    const int ny = field.size(0);
    const int nz = field.size(1);
    const int nx = field.size(2);
    
    if (config.periodic_x) {
        const int x_size = (stagger == 1) ? nx + 1 : nx;
        boundary_ops::periodic<2>(field, 0, x_size);
    }
    
    if (config.periodic_y) {
        const int y_size = (stagger == 2) ? ny + 1 : ny;
        boundary_ops::periodic<0>(field, 0, y_size);
    }
}

void applySymmetricBoundaryAD(
    torch::Tensor& field,
    int stagger,
    const ConfigFlags& config) {
    
    BoundaryDispatcher dispatcher;
    
    // X-boundaries
    dispatcher.register_x(1, [](auto& f, int, int, int) { 
        boundary_ops::zero_normal<2>(f, 0); 
    });
    dispatcher.register_x(0, [](auto& f, int, int, int) { 
        boundary_ops::symmetric_scalar<2>(f, 0); 
    });
    
    // Y-boundaries  
    dispatcher.register_y(2, [](auto& f, int, int, int) {
        boundary_ops::zero_normal<0>(f, 0);
    });
    dispatcher.register_y(0, [](auto& f, int, int, int) {
        boundary_ops::symmetric_scalar<0>(f, 0);
    });
    
    if (config.symmetric_xs) dispatcher.apply_x(field, stagger);
    if (config.symmetric_xe) {
        if (stagger == 1) {
            boundary_ops::zero_normal<2>(field, field.size(2) - 1);
        } else {
            boundary_ops::symmetric_scalar<2>(field, field.size(2) - 1);
        }
    }
    
    if (config.symmetric_ys) dispatcher.apply_y(field, stagger);
    if (config.symmetric_ye) {
        if (stagger == 2) {
            boundary_ops::zero_normal<0>(field, field.size(0) - 1);
        } else {
            boundary_ops::symmetric_scalar<0>(field, field.size(0) - 1);
        }
    }
}

void applyOpenBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    float phase_speed,
    float dt,
    const WRFGridInfo& grid,
    int stagger,
    const ConfigFlags& config) {
    
    const float dx = grid.dx;
    const float dy = grid.dy;
    
    if (config.open_xs) {
        boundary_ops::open_radiation<2>(field, field_old, phase_speed, dt, dx, 0);
    }
    if (config.open_xe) {
        int idx = field.size(2) - 1;
        boundary_ops::open_radiation<2>(field, field_old, phase_speed, dt, dx, idx);
    }
    if (config.open_ys) {
        boundary_ops::open_radiation<0>(field, field_old, phase_speed, dt, dy, 0);
    }
    if (config.open_ye) {
        int idx = field.size(0) - 1;
        boundary_ops::open_radiation<0>(field, field_old, phase_speed, dt, dy, idx);
    }
}

void applySpecifiedBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& bdy_values,
    int relax_zone,
    const ConfigFlags& config) {
    
    if (!config.specified) return;
    
    const int spec_zone = config.spec_zone;
    const float pi = 3.14159265358979323846f;
    
    // Relaxation function: smooth transition from boundary to interior
    auto relax_func = [&](int i, int zone_width) -> float {
        if (i >= zone_width) return 0.0f;
        float x = static_cast<float>(i) / zone_width;
        return 0.5f * (1.0f + std::cos(pi * x));
    };
    
    // Apply relaxation zone blending
    // FIX 2025-12-26: Use copy_() for in-place modification (select()=... rebinds temporary)
    for (int i = 0; i < spec_zone; ++i) {
        float weight = relax_func(i, spec_zone);

        // West boundary
        if (bdy_values.size(0) > 0) {
            auto blended = weight * bdy_values.select(0, i)
                         + (1.0f - weight) * field.select(2, i);
            field.select(2, i).copy_(blended);
        }

        // East boundary
        int east_idx = field.size(2) - 1 - i;
        if (bdy_values.size(1) > 0) {
            auto blended = weight * bdy_values.select(1, i)
                         + (1.0f - weight) * field.select(2, east_idx);
            field.select(2, east_idx).copy_(blended);
        }
    }
}

void applyNestedBoundaryAD(
    torch::Tensor& field,
    const torch::Tensor& parent_field,
    const torch::Tensor& child_field,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    if (!config.nested) return;
    
    const int ratio = 3;  // Parent-to-child grid ratio (typical WRF value)
    
    // Interpolation from parent to child boundaries
    // FIX 2025-12-27: Use torch::indexing::Ellipsis instead of "..." string literal
    using namespace torch::indexing;
    auto interpolate = [&](int ci, int cj) -> torch::Tensor {
        int pi = ci / ratio;
        int pj = cj / ratio;
        float wx = (ci % ratio) / static_cast<float>(ratio);
        float wy = (cj % ratio) / static_cast<float>(ratio);

        return (1 - wx) * (1 - wy) * parent_field.index({pj, Ellipsis, pi}) +
               wx * (1 - wy) * parent_field.index({pj, Ellipsis, pi + 1}) +
               (1 - wx) * wy * parent_field.index({pj + 1, Ellipsis, pi}) +
               wx * wy * parent_field.index({pj + 1, Ellipsis, pi + 1});
    };
    
    // Apply interpolated boundaries
    const int ny = field.size(0);
    const int nx = field.size(2);

    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    // FIX 2025-12-27: Use torch::indexing::Ellipsis instead of "..." string literal
    using namespace torch::indexing;
    for (int j = 0; j < ny; ++j) {
        field.index_put_({j, Ellipsis, 0}, interpolate(0, j));
        field.index_put_({j, Ellipsis, nx - 1}, interpolate(nx - 1, j));
    }

    for (int i = 0; i < nx; ++i) {
        field.index_put_({0, Ellipsis, i}, interpolate(i, 0));
        field.index_put_({ny - 1, Ellipsis, i}, interpolate(i, ny - 1));
    }
}

void applyBoundaryConditions3DAD(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    int stagger,
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    // Boundary precedence: periodic > symmetric > open > specified > nested
    if (config.periodic_x || config.periodic_y) {
        applyPeriodicBoundaryAD(field, stagger, config);
    }
    
    if (config.symmetric_xs || config.symmetric_xe || 
        config.symmetric_ys || config.symmetric_ye) {
        applySymmetricBoundaryAD(field, stagger, config);
    }
    
    if (config.open_xs || config.open_xe || 
        config.open_ys || config.open_ye) {
        float phase_speed = 20.0f;  // Typical gravity wave speed
        applyOpenBoundaryAD(field, field_old, phase_speed, grid.dt, grid, stagger, config);
    }
    
    if (config.specified) {
        torch::Tensor bdy_values = torch::zeros({2, config.spec_zone});
        applySpecifiedBoundaryAD(field, bdy_values, config.relax_zone, config);
    }
    
    if (config.nested) {
        torch::Tensor parent = torch::zeros_like(field);
        torch::Tensor child = torch::zeros_like(field);
        applyNestedBoundaryAD(field, parent, child, grid, config);
    }
}

void applyMomentumBoundaryConditionsAD(
    torch::Tensor& u,
    torch::Tensor& v,
    torch::Tensor& w,
    const torch::Tensor& u_old,
    const torch::Tensor& v_old,
    const torch::Tensor& w_old,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    applyBoundaryConditions3DAD(u, u_old, 1, "U", grid, config);
    applyBoundaryConditions3DAD(v, v_old, 2, "V", grid, config);
    applyBoundaryConditions3DAD(w, w_old, 3, "W", grid, config);

    // W=0 at bottom and top boundaries (rigid lid)
    // FIX 2025-12-26: Use zero_() for in-place modification (select()=... rebinds temporary)
    // Phase 2E: NoGradGuard — boundary enforcement must not enter AD graph
    {
        torch::NoGradGuard no_grad;
        w.select(1, 0).zero_();
        w.select(1, w.size(1) - 1).zero_();
    }
}

void applyScalarBoundaryConditionsAD(
    torch::Tensor& scalar,
    const torch::Tensor& scalar_old,
    const std::string& variable_name,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {
    
    applyBoundaryConditions3DAD(scalar, scalar_old, 0, variable_name, grid, config);
    
    // Moisture positivity preservation
    // Phase 2D: copy_() so in-place modification propagates to caller's tensor
    if (variable_name.find("q") == 0) {
        scalar.copy_(torch::maximum(scalar, torch::zeros_like(scalar)));
    }
}

void applyRelaxationZoneAD(
    torch::Tensor& field,
    const torch::Tensor& target,
    int relax_zone,
    const ConfigFlags& config) {

    // FIX 2025-12-26: Relaxation zones are boundary constraints, not part of physics AD graph
    // Use NoGradGuard to prevent graph construction for in-place boundary operations
    // FIX 2025-12-26: In AD strict mode, throw instead of warn (user wants explicit errors)
    if (field.requires_grad()) {
        if (is_ad_strict_mode()) {
            TORCH_CHECK(false,
                "applyRelaxationZoneAD: field requires_grad but relaxation is in-place. "
                "AD graph will be broken at boundaries. Detach boundary regions or disable ad_strict_mode.");
        } else {
            TORCH_WARN_ONCE(
                "applyRelaxationZoneAD: field requires_grad but relaxation is in-place. "
                "AD graph will be broken at boundaries. This is expected for boundary conditions.");
        }
    }
    torch::NoGradGuard no_grad;

    const float pi = 3.14159265358979323846f;

    auto apply_zone = [&](int dim, int start, int end, bool reverse) {
        for (int i = 0; i < relax_zone; ++i) {
            int idx = reverse ? end - 1 - i : start + i;
            float x = static_cast<float>(i) / relax_zone;
            float weight = 0.5f * (1.0f + std::cos(pi * x));

            auto field_slice = field.select(dim, idx);
            auto target_slice = target.select(dim, idx);
            // FIX 2025-12-26: select()=... rebinds temporary; use copy_() for in-place
            auto blended = weight * target_slice + (1.0f - weight) * field_slice;
            field.select(dim, idx).copy_(blended);
        }
    };

    // Apply to all boundaries as configured
    if (config.open_xs || config.specified) apply_zone(2, 0, field.size(2), false);
    if (config.open_xe || config.specified) apply_zone(2, 0, field.size(2), true);
    if (config.open_ys || config.specified) apply_zone(0, 0, field.size(0), false);
    if (config.open_ye || config.specified) apply_zone(0, 0, field.size(0), true);
}

// FIX 2025-12-26: Polar filtering design decision:
// - Polar filtering is a post-processing smoothing operation at high latitudes
// - It is NOT part of the physics AD graph by design (boundary condition, not dynamics)
// - In-place modification with NoGradGuard is intentional for performance
// - If AD through polar filter is needed in the future, use the out-of-place path below
void applyPolarFilterAD(
    torch::Tensor& field,
    const WRFGridInfo& grid,
    const ConfigFlags& config) {

    if (!config.polar) return;

    const float filter_lat = config.fft_filter_lat;
    const int64_t ny = field.size(0);
    const int64_t nz = field.size(1);
    const int64_t nx = field.size(2);

    // Get latitude values for each j (use xlat if available, else fall back to loop)
    // FIX 2025-12-26: Validate xlat is 2D with size(1)>0 before select(1,0)
    // FIX 2025-12-26: Use cached lat_cpu and lat_tensor to avoid repeated conversions
    torch::Tensor lat_cpu;
    bool xlat_usable = grid.xlat.defined() &&
                       grid.xlat.dim() == 2 &&
                       grid.xlat.size(0) >= ny &&
                       grid.xlat.size(1) > 0;
    // FIX 2025-12-27: Get slot once to avoid repeated find_slot() calls and sample_counter increments
    LatCpuCacheSlot* slot = nullptr;
    if (xlat_usable) {
        slot = lat_cpu_cache.get_slot(grid.xlat);
        if (slot && slot->cached_lat_cpu.defined() && slot->cached_lat_cpu.size(0) == ny) {
            lat_cpu = slot->cached_lat_cpu;
        } else {
            // xlat is 2D [ny, nx], take first column on CPU
            lat_cpu = grid.xlat.select(1, 0).abs().to(torch::kCPU, torch::kFloat32);  // [ny] on CPU
            // Use get_or_create_slot which may update slot pointer
            slot = lat_cpu_cache.get_or_create_slot(grid.xlat);
            slot->update(grid.xlat, lat_cpu);
        }
    } else {
        // FIX 2025-01-26: xlat must be provided in WRF integration
        // The previous latitude_at_j() fallback was never implemented and cannot compile.
        // In proper WRF integration, grid.xlat should always be defined and usable.
        throw std::runtime_error("applyPolarFilter: grid.xlat is not usable (undefined, wrong dim, or too small). "
                                 "Ensure xlat is set in WRFGridInfo before calling polar filter.");
    }

    // Early-exit check on CPU - no GPU sync
    // FIX 2025-12-30 Batch28 Issue 5: Cache polar_mask_cpu.any() result
    auto polar_mask_cpu = lat_cpu > filter_lat;  // [ny] on CPU
    bool has_polar;
    if (slot && slot->has_cached_has_polar(filter_lat)) {
        // Use cached result - no .any().item<bool>() needed
        has_polar = slot->cached_has_polar;
    } else {
        // Compute and cache the result
        // FIX Round187: NoGradGuard for .item() consistency
        torch::NoGradGuard no_grad;
        has_polar = polar_mask_cpu.any().item<bool>();
        if (slot) {
            slot->update_has_polar(has_polar, filter_lat);
        }
    }
    if (!has_polar) {
        return;  // No polar filtering needed (checked on CPU, no GPU sync)
    }

    // Get lat_f32 on device - use cache if available
    // FIX 2025-12-27: Reuse slot pointer instead of repeated find_slot() calls
    torch::Tensor lat_f32;
    if (slot && slot->has_tensor_for(field.device(), torch::kFloat32)) {
        lat_f32 = slot->cached_lat_tensor;
    } else {
        lat_f32 = lat_cpu.to(field.device());  // [ny] on field's device, stays FP32
        if (slot) {
            slot->update_tensor(lat_f32, field.device(), torch::kFloat32);
        }
    }

    // FIX 2025-12-29 Batch11 Issue 6: Cache polar_mask to avoid H2D transfer per call
    // polar_mask depends on (lat_cpu, filter_lat, device), so we check all three
    torch::Tensor polar_mask;
    if (slot && slot->has_polar_mask_for(filter_lat, field.device())) {
        polar_mask = slot->cached_polar_mask;
    } else {
        polar_mask = polar_mask_cpu.to(field.device());  // [ny] on field's device
        if (slot) {
            slot->update_polar_mask(polar_mask, filter_lat, field.device());
        }
    }

    // Compute filter strength in FP32 to avoid precision loss
    auto filter_strength_f32 = ((lat_f32 - filter_lat) / (90.0f - filter_lat)).clamp(0.0f, 1.0f);
    filter_strength_f32 = filter_strength_f32 * filter_strength_f32;  // Square for smooth transition
    filter_strength_f32 = filter_strength_f32 * polar_mask.to(torch::kFloat32);  // Zero out non-polar rows
    auto filter_strength = filter_strength_f32.to(field.scalar_type());
    auto strength = filter_strength.reshape({ny, 1, 1});

    // Compute smoothed field using avg_pool1d
    // FIX Round194: Use int64_t cast to prevent int overflow in reshape
    auto field_reshaped = field.reshape({static_cast<int64_t>(ny) * nz, 1, nx});
    auto smoothed_reshaped = torch::nn::functional::avg_pool1d(
        field_reshaped,
        torch::nn::functional::AvgPool1dFuncOptions(3).stride(1).padding(1)
    );  // [ny*nz, 1, nx]
    auto smoothed = smoothed_reshaped.reshape({ny, nz, nx});

    // FIX 2025-12-26: AD path branch - out-of-place when requires_grad, in-place otherwise
    // NOTE: Polar filtering is designed as a non-AD boundary operation. If you need
    // AD through polar filter, the field should NOT have requires_grad set here.
    if (field.requires_grad()) {
        // Out-of-place path for AD compatibility (rare case)
        // This returns a new tensor; caller must capture the return value
        // For now, we warn and fall through to in-place (design decision: BC is non-AD)
        TORCH_WARN_ONCE(
            "applyPolarFilterAD: field.requires_grad() is true but polar filtering is "
            "designed as a non-AD boundary operation. Using in-place modification which "
            "will break the AD graph. If AD through polar filter is needed, consider "
            "restructuring the call site.");
    }

    // In-place path (standard case for boundary conditions)
    torch::NoGradGuard no_grad;
    field.copy_((1.0f - strength) * field + strength * smoothed);
}

// AD FIX 2025-12-26: CFL check functions with gradient awareness
// Scalar version: For monitoring/diagnostics only (intentionally breaks AD graph)
// Tensor version: For when CFL needs to participate in differentiation

/**
 * Check CFL condition - SCALAR version (breaks AD graph).
 * Use this for stability monitoring/diagnostics where gradients are not needed.
 * For gradient-preserving CFL, use checkBoundaryCFLTensor().
 */
float checkBoundaryCFLAD(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid) {

    // Explicitly guard since this is a diagnostic function that doesn't need gradients
    torch::NoGradGuard no_grad;

    // FIX 2025-12-27: Compute CFL components as tensors, then single .item() for max
    // This reduces GPU sync from 3x to 1x
    auto cfl_x_t = torch::max(torch::abs(u)) * grid.dt / grid.dx;
    auto cfl_y_t = torch::max(torch::abs(v)) * grid.dt / grid.dy;

    // FIX 2025-12-26: Use DzMinCache to avoid repeated dz.min() reduction
    // The cache is epoch-based and invalidated when grid metrics change
    float dz_min;
    if (grid.dz.defined() && grid.dz.numel() > 0) {
        bool cache_hit = dz_min_cache.is_valid(grid.dz);
        // FIX 2025-12-30 Batch30 Issue 4: Periodic full-verify for moving_nest safety
        if (cache_hit) {
            dz_min_cache.access_count++;
            if (dz_min_cache.needs_full_verify() && !dz_min_cache.full_verify(grid.dz)) {
                cache_hit = false;  // Full verify failed, recompute
            }
        }
        if (cache_hit) {
            // FIX 2025-12-26: Use cached scalar value to avoid GPU sync from .item()
            dz_min = dz_min_cache.cached_dz_min_value;
        } else {
            // FIX 2025-12-27: Cache miss - avoid full tensor copy for large GPU tensors
            // For large tensors (>100K elements) on GPU, use amin() on device then copy scalar
            constexpr int64_t GPU_AMIN_THRESHOLD = 100000;  // 100K elements
            float eps = metric_utils::getEpsForDtype(torch::kFloat32);

            if (grid.dz.is_cuda() && grid.dz.numel() > GPU_AMIN_THRESHOLD) {
                // FIX 2025-12-27: GPU fast path with normalize_positive for NaN/Inf/negative
                // normalize_positive: abs() + clamp_min() + NaN→eps, works on GPU
                auto dz_norm = metric_utils::normalize_positive(grid.dz, eps);
                auto dz_min_0d = torch::amin(dz_norm);  // GPU reduction -> 0D tensor
                // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
                dz_min = dz_min_0d.to(torch::kCPU).item<float>();
            } else {
                // CPU path or small tensor: normalize on CPU
                auto dz_cpu = metric_utils::normalize_positive(
                    grid.dz.to(torch::kCPU, torch::kFloat32), eps);
                dz_min = dz_cpu.min().item<float>();  // min() + .item() both on CPU
            }
            // FIX 2025-12-27: Only cache if result is finite and valid
            if (std::isfinite(dz_min) && dz_min >= 1e-10f) {
                auto dz_min_tensor = torch::scalar_tensor(dz_min, torch::kFloat32);
                dz_min_cache.update(grid.dz, dz_min_tensor, dz_min);
            }
        }
        // FIX 2025-12-27: Fallback if NaN/Inf or too small
        if (!std::isfinite(dz_min) || dz_min < 1e-10f) dz_min = grid.dx;
    } else {
        dz_min = grid.dx;  // Fallback to dx
    }
    // FIX 2025-12-27: Keep cfl_z as tensor, then stack + max + single .item()
    auto cfl_z_t = torch::max(torch::abs(w)) * grid.dt / dz_min;

    // Stack all CFL components and take max, then single .item() for GPU sync
    // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
    auto cfl_all = torch::stack({cfl_x_t, cfl_y_t, cfl_z_t});
    return torch::max(cfl_all).to(torch::kCPU).item<float>();
}

/**
 * Check CFL condition - TENSOR version (preserves AD graph).
 * Returns a 0D tensor containing max CFL number.
 * Use this when CFL needs to participate in differentiation.
 */
torch::Tensor checkBoundaryCFLTensor(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const WRFGridInfo& grid) {

    // Keep everything as tensors to preserve gradient graph
    auto cfl_x = torch::max(torch::abs(u)) * grid.dt / grid.dx;
    auto cfl_y = torch::max(torch::abs(v)) * grid.dt / grid.dy;

    // SHAPE FIX 2025-12-26: Ensure dz_min is 0D scalar for consistent stack shape
    // If grid.dz is 1D [nz] or 3D [ny, nz, nx], normalize_positive returns same shape,
    // and .min() reduces to 0D. This ensures cfl_z is 0D like cfl_x/cfl_y.
    torch::Tensor dz_min;
    if (grid.dz.defined() && grid.dz.numel() > 0) {
        // FIX 2025-12-26: Check cache BEFORE .to() to avoid unnecessary device transfer
        // Cache validity uses original grid.dz; only transfer on cache miss
        bool cache_hit = dz_min_cache.is_valid(grid.dz);
        // FIX 2025-12-30 Batch30 Issue 4: Periodic full-verify for moving_nest safety
        if (cache_hit) {
            dz_min_cache.access_count++;
            if (dz_min_cache.needs_full_verify() && !dz_min_cache.full_verify(grid.dz)) {
                cache_hit = false;  // Full verify failed, recompute
            }
        }
        if (cache_hit) {
            // FIX 2025-12-27: Use device/dtype cache to avoid .to() on every hit
            dz_min = dz_min_cache.get_tensor_for(w.device(), w.scalar_type());
        } else {
            // FIX 2025-12-27: Cache miss - avoid full tensor copy for large GPU tensors
            constexpr int64_t GPU_AMIN_THRESHOLD = 100000;  // 100K elements
            float eps = metric_utils::getEpsForDtype(torch::kFloat32);

            if (grid.dz.is_cuda() && grid.dz.numel() > GPU_AMIN_THRESHOLD) {
                // FIX 2025-12-27: GPU fast path with normalize_positive for NaN/Inf/negative
                // normalize_positive: abs() + clamp_min() + NaN→eps, works on GPU
                auto dz_norm = metric_utils::normalize_positive(grid.dz, eps);
                auto dz_min_0d = torch::amin(dz_norm);  // GPU reduction -> 0D tensor

                // FIX 2025-12-30 Batch28 Issue 2: Single D2H sync + C++ validity check
                // Previous: is_valid.to(kCPU).item<bool>() + dz_min_0d.to(kCPU).item<float>() = 2 syncs
                // Now: Single sync, check validity in C++ with std::isfinite
                float dz_min_value = dz_min_0d.to(torch::kCPU).item<float>();
                if (std::isfinite(dz_min_value) && dz_min_value > 1e-10f) {
                    // Valid result - use GPU 0D tensor directly
                    dz_min = dz_min_0d.to(w.options());
                    // Cache update with already-extracted scalar
                    auto dz_min_cpu = torch::scalar_tensor(dz_min_value, torch::kFloat32);
                    dz_min_cache.update(grid.dz, dz_min_cpu, dz_min_value);
                } else {
                    // Fallback to dx
                    dz_min = torch::scalar_tensor(grid.dx, w.options());
                }
            } else {
                // CPU path or small tensor: normalize on CPU
                auto dz_cpu = metric_utils::normalize_positive(
                    grid.dz.to(torch::kCPU, torch::kFloat32), eps);
                float dz_min_value = dz_cpu.min().item<float>();  // min() + .item() on CPU

                // FIX 2025-12-27: Fallback if NaN/Inf or too small
                if (!std::isfinite(dz_min_value) || dz_min_value < 1e-10f) {
                    dz_min_value = grid.dx;
                }
                // Create 0D tensor on target device for AD graph
                dz_min = torch::scalar_tensor(dz_min_value, w.options());
                // FIX 2025-12-27: Only cache if result is finite and valid
                if (std::isfinite(dz_min_value) && dz_min_value >= 1e-10f) {
                    auto dz_min_cpu = torch::scalar_tensor(dz_min_value, torch::kFloat32);
                    dz_min_cache.update(grid.dz, dz_min_cpu, dz_min_value);
                }
            }
        }
    } else {
        // Fallback to dx as scalar
        dz_min = torch::scalar_tensor(grid.dx, w.options());
    }
    auto cfl_z = torch::max(torch::abs(w)) * grid.dt / dz_min;  // 0D

    // Stack and take max to get single 0D tensor
    auto cfl_all = torch::stack({cfl_x, cfl_y, cfl_z});
    return torch::max(cfl_all);
}

std::string getBoundaryTypeString(const ConfigFlags& config) {
    std::string result;
    if (config.periodic_x) result += "periodic_x ";
    if (config.periodic_y) result += "periodic_y ";
    if (config.symmetric_xs || config.symmetric_xe) result += "symmetric_x ";
    if (config.symmetric_ys || config.symmetric_ye) result += "symmetric_y ";
    if (config.open_xs || config.open_xe) result += "open_x ";
    if (config.open_ys || config.open_ye) result += "open_y ";
    if (config.specified) result += "specified ";
    if (config.nested) result += "nested ";
    return result.empty() ? "none" : result;
}

bool validateBoundaryConfiguration(const ConfigFlags& config) {
    // Check for conflicting boundaries
    if (config.periodic_x && (config.symmetric_xs || config.symmetric_xe)) return false;
    if (config.periodic_y && (config.symmetric_ys || config.symmetric_ye)) return false;
    if (config.periodic_x && (config.open_xs || config.open_xe)) return false;
    if (config.periodic_y && (config.open_ys || config.open_ye)) return false;
    return true;
}

// ============================================================================
// Cache Invalidation Implementation
// ============================================================================

void invalidateDzMinCache() {
    // FIX 2025-12-26: Increment epoch to invalidate cached dz_min values
    // Called when grid metrics are modified in-place
    DzMinCache::incrementEpoch();
}

void invalidateLatCpuCache() {
    // FIX 2025-12-26: Increment epoch to invalidate cached lat_cpu values
    // Called when xlat or other latitude data is modified in-place
    // FIX 2025-01-26: incrementEpoch() is defined in LatCpuCacheSlot, not LatCpuCache
    LatCpuCacheSlot::incrementEpoch();
}

} // namespace sdirk3
} // namespace wrf