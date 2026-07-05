/**
 * @file wrf_sdirk3_unified_rhs_extended.cpp
 * @brief Extended RHS computations with WRF-consistent deformation tensors
 *
 * This file implements deformation tensor calculations with exact WRF consistency
 * while maintaining automatic differentiation compatibility for JVP calculations.
 *
 * FIX Round151: PERFORMANCE TODO PRIORITY ASSESSMENT
 * ═══════════════════════════════════════════════════
 * Priority rankings for remaining optimization TODOs:
 *
 * HIGH PRIORITY (affects every timestep):
 *   - Line ~2254: align_msf_3d recalculation - called every step, consider caching
 *
 * MEDIUM PRIORITY (hot path but mitigated):
 *   - Line ~159: safe_mean D2H sync - has caching but moving_nest bypasses it
 *
 * LOW PRIORITY (features not yet implemented):
 *   - Line ~1641: 6th order diffusion - feature placeholder
 *   - Line ~2898: terrain slope correction - feature placeholder
 *
 * COMPLETED (already optimized):
 *   - D2H sync reduction via DnwMeanCache
 *   - Pre-copy dnw/dn to CPU for O(1) loop access
 *   - Non-AD execution fast paths
 */

#include "wrf_sdirk3_unified_rhs_extended.h"
#include "wrf_sdirk3_metric_utils.h"  // For safe_inv_dn helper
#include "wrf_sdirk3_autograd_utils.h"  // FIX Round181: For make_cpu_from_blob_opts
#include "wrf_sdirk3_config.h"  // GR v9 G12: For g_sdirk3_config.use_autograd
#include <torch/torch.h>
#include <optional>  // FIX 2025-12-26: For std::optional<NoGradGuard>
#include <cmath>     // FIX 2025-01-11 Round29: Explicit include for std::abs (not relying on transitive)

namespace wrf {
namespace sdirk3 {

// Bring indexing utilities into namespace for cleaner code
using torch::indexing::Slice;
using torch::indexing::None;

namespace {
// FIX 2025-12-26: Removed local extract1DVerticalMetric - use shared metric_utils::extract1DVerticalMetric

// FIX 2025-12-29 Batch11 Issue 2: Cache for safe_mean(grid.dnw) to avoid repeated D2H sync
// FIX 2025-12-29 Batch12 Issue 1: Added cached_numel + cached_contiguous for view/slice detection
// FIX 2025-12-30 Batch14 Issue 4: Extract 1D vertical metric before mean for correct semantics
// FIX 2025-12-30 Batch15 Issue 1: Bypass cache for non-contiguous to avoid view false positive
// FIX 2025-12-30 Batch15 Issue 5: Add 3-point signature for in-place change detection
struct DnwMeanCache {
    void* cached_ptr = nullptr;
    uint64_t cached_epoch = 0;
    int64_t cached_numel = 0;
    // FIX 2025-12-30 Batch15 Issue 1: Track strides for view disambiguation
    int64_t cached_stride0 = -1;  // First stride (or -1 if 1D or undefined)
    // FIX 2025-12-30 Batch17 Issue 2: Track storage_offset and stride1 for view detection
    int64_t cached_storage_offset = 0;
    int64_t cached_stride1 = -1;  // Second stride for 2D+ tensors
    // FIX 2025-12-30 Batch18 Issue 2: Track dtype and device for complete key
    torch::ScalarType cached_dtype = torch::kFloat32;
    torch::DeviceType cached_device_type = torch::kCPU;
    int16_t cached_device_idx = -1;
    // FIX 2025-01-11 Round28: Add kdim to cache key to invalidate when extraction axis changes
    int64_t cached_kdim = -1;
    // FIX 2025-01-11 Round31 Issue 5: Add nz/ny/nx to cache key to invalidate on grid resize
    // Without this, if grid dims change but epoch is not bumped, stale mean could be reused
    int64_t cached_nz = -1;
    int64_t cached_ny = -1;
    int64_t cached_nx = -1;
    // FIX 2025-01-10 Round24: Use double for precision preservation when dnw is FP64
    double cached_mean = 100.0;  // Default fallback

    // FIX 2025-12-30 Batch15 Issue 5: 3-point signature for in-place detection
    uint64_t sig_bits[3] = {0, 0, 0};  // [0], [mid], [-1] as raw FP bits
    mutable uint64_t verify_call_count = 0;

    // Convert value to bits for exact comparison
    static uint64_t valueToBits(float val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    static uint64_t valueToBits(double val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return bits;
    }

    // FIX 2025-12-30 Batch17 Issue 4: Unified device index resolution helper
    // Handles CUDA current_device fallback when device.index() is not set
    // FIX Round165: Add range check to prevent int16_t overflow on large GPU clusters
    static int16_t get_device_index(const torch::Device& dev) {
        int64_t idx = -1;
        if (dev.has_index()) {
            idx = dev.index();
        }
        #ifdef USE_CUDA
        else if (dev.type() == torch::kCUDA) {
            idx = at::cuda::current_device();
        }
        #endif
        // Range check for int16_t safety (supports up to 32767 GPUs)
        if (idx < 0 || idx > 32767) return -1;
        return static_cast<int16_t>(idx);
    }

    // FIX 2025-12-30 Batch16 Issue 2: Cache for 3-point index tensor
    // FIX 2025-12-30 Batch17 Issue 4: Use get_device_index helper for consistency
    struct Sig3IndexCache {
        int64_t cached_n = 0;
        torch::DeviceType cached_device_type = torch::kCPU;
        int16_t cached_device_idx = -1;
        torch::Tensor idx_tensor;

        bool is_valid(int64_t n, const torch::Device& dev) const {
            if (!idx_tensor.defined()) return false;
            if (cached_n != n) return false;
            if (cached_device_type != dev.type()) return false;
            return cached_device_idx == get_device_index(dev);
        }

        torch::Tensor get_or_compute(int64_t n, const torch::Device& dev) {
            if (is_valid(n, dev)) return idx_tensor;

            std::vector<int64_t> indices = {0, n/2, n-1};
            idx_tensor = torch::tensor(indices, torch::TensorOptions().dtype(torch::kLong).device(dev));
            cached_n = n;
            cached_device_type = dev.type();
            cached_device_idx = get_device_index(dev);
            return idx_tensor;
        }
    };
    static thread_local Sig3IndexCache sig3_idx_cache;

    // Compute 3-point signature from 1D tensor
    static void compute_sig(const torch::Tensor& t, uint64_t* sig) {
        sig[0] = sig[1] = sig[2] = 0;
        if (!t.defined() || t.numel() == 0) return;

        torch::NoGradGuard no_grad;
        int64_t n = t.numel();
        auto dtype = t.scalar_type();
        int64_t indices[3] = {0, n/2, n-1};

        // CPU-contiguous fast path: direct data_ptr access
        if (t.device().type() == torch::kCPU && t.is_contiguous()) {
            auto flat = t.flatten();
            if (dtype == torch::kFloat64) {
                const double* ptr = flat.data_ptr<double>();
                for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[indices[i]]);
            } else if (dtype == torch::kFloat32) {
                const float* ptr = flat.data_ptr<float>();
                for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[indices[i]]);
            }
            return;
        }

        // OPT Round182: CPU non-contiguous path - avoid index_select overhead
        // Use simple tensor indexing which is cheaper on CPU than index_select
        if (t.device().type() == torch::kCPU) {
            auto flat = t.flatten().contiguous();  // Make contiguous copy on CPU
            if (dtype == torch::kFloat64) {
                const double* ptr = flat.data_ptr<double>();
                for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[indices[i]]);
            } else if (dtype == torch::kFloat32) {
                const float* ptr = flat.data_ptr<float>();
                for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[indices[i]]);
            }
            return;
        }

        // GPU path: Use cached index tensor for index_select
        // FIX 2025-12-30 Batch16 Issue 2: Use cached index tensor
        auto idx = sig3_idx_cache.get_or_compute(n, t.device());
        auto target_dtype = (dtype == torch::kFloat64) ? torch::kFloat64 : torch::kFloat32;
        auto samples = t.flatten().index_select(0, idx).to(torch::kCPU, target_dtype);

        if (dtype == torch::kFloat64) {
            auto ptr = samples.data_ptr<double>();
            for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[i]);
        } else {
            auto ptr = samples.data_ptr<float>();
            for (int i = 0; i < 3; ++i) sig[i] = valueToBits(ptr[i]);
        }
    }

    static bool sig_matches(const uint64_t* cached, const uint64_t* current) {
        constexpr uint64_t SENTINEL = 0xFFFFFFFFFFFFFFFFULL;
        for (int i = 0; i < 3; ++i) {
            if (cached[i] == SENTINEL || current[i] == SENTINEL) return false;
            if (cached[i] != current[i]) return false;
        }
        return true;
    }

    // FIX 2025-12-30 Batch14 Issue 4: Added grid dims for proper 1D extraction
    // FIX 2025-01-10 Round24: Return double for precision preservation when dnw is FP64
    // FIX 2025-01-11 Round27: Added kdim parameter for cubic grid disambiguation
    // TODO 2025-01-10 Round24 PERF: safe_mean uses .to(kCPU).item() causing D2H sync.
    // For moving_nest/non-contiguous paths with frequent calls, consider:
    // 1. safe_mean_tensor() to keep result on device until final scalar extraction
    // 2. CPU-side cache of 1D profile to avoid repeated GPU mean reductions
    double get_or_compute(const torch::Tensor& dnw, int64_t nz = -1, int64_t ny = -1, int64_t nx = -1, int64_t kdim = -1) {
        if (!dnw.defined() || dnw.numel() == 0) {
            return 100.0;  // Default vertical spacing
        }

        // FIX 2025-12-30 Batch15 Issue 1 + Batch16 Issue 5: Handle non-contiguous tensors
        // - moving_nest=true: bypass cache entirely (metrics change frequently)
        if (!dnw.is_contiguous() && metric_utils::isMovingNestMode().load(std::memory_order_acquire)) {
            // Bypass cache entirely in moving_nest mode for safety
            torch::Tensor dnw_for_mean = dnw;
            if (dnw.dim() >= 2 && nz > 0) {
                if (ny > 0 && nx > 0) {
                    // FIX 2025-01-11 Round27: Use passed kdim for cubic grid disambiguation
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, ny, nx, kdim);
                } else if (dnw.dim() == 2) {
                    // FIX 2025-01-10 Round13: Use 3-arg overload with kdim for 2D ambiguity
                    // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, kdim);
                } else if (dnw.dim() == 3) {
                    // FIX 2025-01-10 Round17: Use 3-arg overload for consistent k-halo/FP16 handling
                    // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, kdim);
                }
            }
            // FIX 2025-01-10 Round24: Use safe_mean_double for FP64 precision preservation
            return metric_utils::safe_mean_double(dnw_for_mean);
        }

        // FIX 2025-12-30 Batch17 Issue 2: Key on ORIGINAL tensor properties to detect view changes
        // FIX 2025-12-30 Batch18 Issue 2: Include dtype and device for complete key
        // Use storage_offset + strides to disambiguate views sharing same underlying storage
        void* current_ptr = dnw.data_ptr();
        uint64_t current_epoch = metric_utils::getGlobalMetricEpoch();
        int64_t current_numel = dnw.numel();
        int64_t current_stride0 = dnw.dim() > 0 ? dnw.stride(0) : -1;
        int64_t current_stride1 = dnw.dim() > 1 ? dnw.stride(1) : -1;
        int64_t current_storage_offset = dnw.storage_offset();
        torch::ScalarType current_dtype = dnw.scalar_type();
        torch::DeviceType current_device_type = dnw.device().type();
        int16_t current_device_idx = get_device_index(dnw.device());

        // Cache validation using original tensor properties (full key check)
        // FIX 2025-01-11 Round28: Include kdim in key to invalidate when extraction axis changes
        // FIX 2025-01-11 Round31 Issue 5: Include nz/ny/nx to invalidate on grid resize
        bool key_match = (current_ptr == cached_ptr &&
                          current_epoch == cached_epoch &&
                          current_numel == cached_numel &&
                          current_stride0 == cached_stride0 &&
                          current_stride1 == cached_stride1 &&
                          current_storage_offset == cached_storage_offset &&
                          current_dtype == cached_dtype &&
                          current_device_type == cached_device_type &&
                          current_device_idx == cached_device_idx &&
                          kdim == cached_kdim &&
                          nz == cached_nz &&
                          ny == cached_ny &&
                          nx == cached_nx);

        if (key_match) {
            // FIX 2025-12-30 Batch15 Issue 5: Periodic signature verification
            // FIX 2025-12-30 Batch17 Issue 2: Compute signature on ORIGINAL tensor
            uint64_t verify_period = metric_utils::getVerifyPeriod();
            ++verify_call_count;
            if (verify_period > 0 && (verify_call_count % verify_period) == 0) {
                uint64_t current_sig[3];
                compute_sig(dnw, current_sig);  // Original tensor, not contiguous copy
                if (!sig_matches(sig_bits, current_sig)) {
                    key_match = false;  // Force recompute
                }
            }
        }

        if (key_match) {
            return cached_mean;
        }

        // Cache miss: make contiguous if needed, then compute
        torch::Tensor dnw_work = dnw.is_contiguous() ? dnw : dnw.contiguous();
        torch::Tensor dnw_for_mean = dnw_work;
        if (dnw_work.dim() >= 2 && nz > 0) {
            if (ny > 0 && nx > 0) {
                // FIX 2025-01-11 Round27: Use passed kdim for cubic grid disambiguation
                dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw_work, nz, ny, nx, kdim);
            } else if (dnw_work.dim() == 2) {
                // FIX 2025-01-10 Round13: Use 3-arg overload with kdim for 2D ambiguity
                // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw_work, nz, kdim);
            } else if (dnw_work.dim() == 3) {
                // FIX 2025-01-10 Round17: Use 3-arg overload for consistent k-halo/FP16 handling
                // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw_work, nz, kdim);
            }
        }

        // FIX 2025-01-10 Round24: Use safe_mean_double for FP64 precision preservation
        cached_mean = metric_utils::safe_mean_double(dnw_for_mean);
        // Cache key uses ORIGINAL tensor properties
        cached_ptr = current_ptr;
        cached_epoch = current_epoch;
        cached_numel = current_numel;
        cached_stride0 = current_stride0;
        cached_stride1 = current_stride1;
        cached_storage_offset = current_storage_offset;
        // FIX 2025-12-30 Batch18 Issue 2: Store dtype and device
        cached_dtype = current_dtype;
        cached_device_type = current_device_type;
        cached_device_idx = current_device_idx;
        // FIX 2025-01-11 Round28: Store kdim to detect extraction axis changes
        cached_kdim = kdim;
        // FIX 2025-01-11 Round31 Issue 5: Store nz/ny/nx to detect grid resize
        cached_nz = nz;
        cached_ny = ny;
        cached_nx = nx;

        // FIX 2025-12-30 Batch17 Issue 2: Update signature from ORIGINAL tensor
        compute_sig(dnw, sig_bits);
        verify_call_count = 0;

        return cached_mean;
    }

    // FIX 2025-12-30 Batch17 Issue 3: AD-compatible path returning 0D tensor
    // Use this when dnw.requires_grad() to preserve computational graph
    // FIX 2025-01-11 Round27: Added kdim parameter for cubic grid disambiguation
    torch::Tensor get_or_compute_tensor(const torch::Tensor& dnw, int64_t nz = -1, int64_t ny = -1, int64_t nx = -1, int64_t kdim = -1) {
        if (!dnw.defined() || dnw.numel() == 0) {
            return torch::tensor(100.0f, dnw.options().requires_grad(false));
        }

        // If requires_grad, compute fresh to preserve AD graph (no caching)
        if (dnw.requires_grad() || wrf::sdirk3::g_sdirk3_config.use_autograd) {
            torch::Tensor dnw_for_mean = dnw;
            if (dnw.dim() >= 2 && nz > 0) {
                if (ny > 0 && nx > 0) {
                    // FIX 2025-01-11 Round27: Use passed kdim for cubic grid disambiguation
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, ny, nx, kdim);
                } else if (dnw.dim() == 2) {
                    // FIX 2025-01-10 Round13: Use 3-arg overload with kdim for 2D ambiguity
                    // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, kdim);
                } else if (dnw.dim() == 3) {
                    // FIX 2025-01-10 Round17: Use 3-arg overload for consistent k-halo/FP16 handling
                    // FIX 2025-01-11 Round27: Use passed kdim instead of hardcoded 0
                    dnw_for_mean = metric_utils::extract1DVerticalMetric(dnw, nz, kdim);
                }
            }
            // Return 0D tensor to preserve AD graph
            return dnw_for_mean.mean();
        }

        // No requires_grad: use cached scalar path and wrap in tensor
        // FIX 2025-01-11 Round27: Keep double precision until final tensor creation
        // to avoid FP64 precision loss when dnw is double
        // FIX 2025-01-11 Round27: Pass kdim to get_or_compute for cubic grid support
        double mean_val = get_or_compute(dnw, nz, ny, nx, kdim);
        auto opts = dnw.options().requires_grad(false);
        if (opts.dtype() == torch::kFloat64) {
            return torch::tensor(mean_val, opts);
        } else {
            // Downcast to float for FP32/FP16/BF16 tensors
            return torch::tensor(static_cast<float>(mean_val), opts);
        }
    }
};

// Out-of-class definition for nested static thread_local member
thread_local DnwMeanCache::Sig3IndexCache DnwMeanCache::sig3_idx_cache;

static thread_local DnwMeanCache dnw_mean_cache;

// FIX 2025-01-11 Round27: Helper to select appropriate kdim for dnw/dn
// FIX 2025-01-11 Round28: Enhanced detection for haloed w-level (nz+1+2*halo) and cubic grids
// FIX 2025-01-11 Round29: Generalized parity-based halo detection (not just {1,2,5})
//                         Added ny/nx halo tolerance for horizontal halo cases
// dnw is typically mass-level (nz elements), but some WRF configurations may pass
// w-level metrics (nz+1 elements). This function detects which case and returns
// the appropriate kdim (rdz_kdim for nz, rdzw_kdim for nz+1).
inline int64_t selectDnwKdim(const torch::Tensor& dnw, int64_t nz, int64_t ny, int64_t nx,
                             int64_t rdz_kdim, int64_t rdzw_kdim) {
    if (!dnw.defined() || dnw.dim() < 1) {
        return rdz_kdim;  // Default to mass-level
    }

    // FIX 2025-01-11 Round29: Parity-based mass/w-level detection
    // Mass-level: size = nz + 2*halo → (size - nz) is even and >= 0
    // W-level:    size = nz+1 + 2*halo → (size - (nz+1)) is even and >= 0
    auto matches_mass_parity = [nz](int64_t sz) -> bool {
        int64_t diff = sz - nz;
        return diff >= 0 && (diff % 2 == 0);  // nz, nz+2, nz+4, ...
    };
    auto matches_w_parity = [nz](int64_t sz) -> bool {
        int64_t diff = sz - (nz + 1);
        return diff >= 0 && (diff % 2 == 0);  // nz+1, nz+3, nz+5, ...
    };

    // FIX 2025-01-11 Round29: ny/nx matching with halo tolerance
    // Allows for horizontal halo: ny+2*halo or nx+2*halo
    // FIX 2025-01-11 Round30 Issue 7: Also handle stagger+halo (odd diff >= 3)
    // - diff=0: exact ny
    // - diff=1: v-stagger (ny+1)
    // - diff=2,4,6...: symmetric halo (ny + 2*h)
    // - diff=3,5,7...: stagger + symmetric halo (ny + 1 + 2*h)
    // FIX 2025-01-11 Round31 Issue 6: Extended from 12 to 20 for larger halos (up to halo=9 with stagger)
    // NOTE: For extremely large halos (>9) or cubic-like grids where ny≈nx≈nz,
    // set WRFGridInfo::rdz_kdim or rdzw_kdim explicitly to avoid kdim misjudgment.
    // See wrf_sdirk3_types.h for rdz_kdim/rdzw_kdim documentation.
    constexpr int64_t kMaxHaloTolerance = 20;  // Allow up to halo=9 with stagger
    auto matches_ny_with_halo = [ny](int64_t sz) -> bool {
        int64_t diff = sz - ny;
        // Allow any non-negative diff up to max tolerance (covers stagger, halo, stagger+halo)
        return diff >= 0 && diff <= kMaxHaloTolerance;
    };
    auto matches_nx_with_halo = [nx](int64_t sz) -> bool {
        int64_t diff = sz - nx;
        // Allow any non-negative diff up to max tolerance (covers stagger, halo, stagger+halo)
        return diff >= 0 && diff <= kMaxHaloTolerance;
    };

    // 1D case: check if matches nz (mass) or nz+1 (w-level) using parity
    if (dnw.dim() == 1) {
        int64_t len = dnw.size(0);
        // Exact match check first (fast path)
        if (len == nz) return rdz_kdim;
        if (len == nz + 1) return rdzw_kdim;
        // Parity-based check for any halo size
        bool is_mass = matches_mass_parity(len);
        bool is_w = matches_w_parity(len);
        // If only one matches, use it; otherwise prefer mass-level
        if (is_w && !is_mass) return rdzw_kdim;
        if (is_mass && !is_w) return rdz_kdim;
        // Both match (ambiguous) - use proximity heuristic
        // FIX 2025-01-11 Round32 Issue 6: Warn on ambiguous cubic+halo case
        TORCH_WARN_ONCE("selectDnwKdim: 1D dnw size ", len, " is ambiguous (matches both mass-level nz=",
                        nz, " and w-level nz+1=", nz+1, " with halo). Using proximity heuristic. "
                        "Set WRFGridInfo::rdz_kdim or rdzw_kdim explicitly for cubic-like grids.");
        return (std::abs(len - (nz + 1)) < std::abs(len - nz)) ? rdzw_kdim : rdz_kdim;
    }

    // 2D case: need parity-based detection to avoid cubic grid (nx==nz+1) confusion
    if (dnw.dim() == 2) {
        int64_t d0 = dnw.size(0);
        int64_t d1 = dnw.size(1);

        // Parity-based: check which dimension is vertical by comparing with ny/nx
        // WRF 2D metric convention: [nz,nx] has kdim=0, [ny,nz] has kdim=1
        bool d0_matches_ny = matches_ny_with_halo(d0);
        bool d1_matches_nx = matches_nx_with_halo(d1);
        [[maybe_unused]] bool d0_matches_nx = matches_nx_with_halo(d0);
        [[maybe_unused]] bool d1_matches_ny = matches_ny_with_halo(d1);

        // Case: [nz, nx] layout → kdim=0
        if ((matches_mass_parity(d0) || matches_w_parity(d0)) && d1_matches_nx && !d0_matches_ny) {
            return matches_w_parity(d0) ? rdzw_kdim : rdz_kdim;
        }
        // Case: [ny, nz] layout → kdim=1
        if (d0_matches_ny && (matches_mass_parity(d1) || matches_w_parity(d1)) && !d1_matches_nx) {
            return matches_w_parity(d1) ? rdzw_kdim : rdz_kdim;
        }
        // Ambiguous case: both dims could be vertical - use parity priority
        bool d0_is_w = matches_w_parity(d0) && !matches_mass_parity(d0);
        bool d1_is_w = matches_w_parity(d1) && !matches_mass_parity(d1);
        if (d0_is_w && !d1_is_w) return rdzw_kdim;
        if (d1_is_w && !d0_is_w) return rdzw_kdim;
        // FIX 2025-01-11 Round32 Issue 6: Warn on ambiguous cubic+halo 2D case
        // Both dims match ny/nx with halo but neither clearly distinguishes vertical
        TORCH_WARN_ONCE("selectDnwKdim: 2D dnw shape [", d0, ",", d1, "] is ambiguous (cannot "
                        "distinguish kdim for ny=", ny, ", nx=", nx, ", nz=", nz, "). "
                        "Defaulting to mass-level kdim=0. Set WRFGridInfo::rdz_kdim explicitly.");
        return rdz_kdim;  // Default to mass-level
    }

    // 3D case: [ny, nz, nx] layout - kdim is always 1 (middle dim)
    // Check if middle dimension is mass (nz) or w-level (nz+1) using parity
    if (dnw.dim() == 3) {
        int64_t kmid = dnw.size(1);
        // Parity-based check
        bool is_w = matches_w_parity(kmid);
        bool is_mass = matches_mass_parity(kmid);
        if (is_w && !is_mass) return rdzw_kdim;
        return rdz_kdim;  // Default to mass-level
    }

    return rdz_kdim;  // Default fallback
}

// =============================================================================
// FIX 2025-01-11 Round33: Common center-slice helper for deformation tensor halo handling
// =============================================================================
// This helper extracts the center region from a tensor with symmetric halo padding.
// Used by D12/D13/D23 vectorized paths in computeSmagorinskyCoefficientsWRF and
// applySmagorinskyTurbulenceWRF to reduce code duplication.
//
// Parameters:
//   input      - Input tensor with potential halo padding
//   target_j   - Expected size in j-dimension (dim 0)
//   target_k   - Expected size in k-dimension (dim 1)
//   target_i   - Expected size in i-dimension (dim 2)
//   func_name  - Function name for warning messages
//   tensor_name - Tensor name (e.g., "D12", "D13") for warning messages
//
// Returns:
//   Center-sliced tensor of shape [target_j, target_k, target_i]
//   For odd halos, uses floor(diff/2) offset for best centering
// =============================================================================
inline torch::Tensor extractCenterSlice3D(
    const torch::Tensor& input,
    int64_t target_j, int64_t target_k, int64_t target_i,
    const char* func_name, const char* tensor_name) {

    if (!input.defined() || input.dim() != 3) {
        return input;  // Pass through undefined or non-3D tensors
    }

    const int64_t diff_j = input.size(0) - target_j;
    const int64_t diff_k = input.size(1) - target_k;
    const int64_t diff_i = input.size(2) - target_i;

    // If no halo, return as-is (fast path)
    if (diff_j == 0 && diff_k == 0 && diff_i == 0) {
        return input;
    }

    // Check for negative diffs (input smaller than target)
    if (diff_j < 0 || diff_k < 0 || diff_i < 0) {
        TORCH_WARN_ONCE(func_name, ": ", tensor_name, " shape [", input.size(0), ",",
            input.size(1), ",", input.size(2), "] smaller than target [", target_j, ",",
            target_k, ",", target_i, "]; returning input unchanged");
        return input;
    }

    // Compute halo offsets (floor division for symmetric halo)
    const int64_t halo_j = diff_j / 2;
    const int64_t halo_k = diff_k / 2;
    const int64_t halo_i = diff_i / 2;

    // Warn on odd halo (asymmetric padding)
    const bool has_odd_halo = (diff_j > 0 && diff_j % 2 != 0) ||
                              (diff_k > 0 && diff_k % 2 != 0) ||
                              (diff_i > 0 && diff_i % 2 != 0);
    if (has_odd_halo) {
        TORCH_WARN_ONCE(func_name, ": ", tensor_name, " has odd halo (shape=[",
            input.size(0), ",", input.size(1), ",", input.size(2), "], target=[",
            target_j, ",", target_k, ",", target_i, "]); using floor(halo/2) offset");
    }

    return input.slice(0, halo_j, halo_j + target_j)
                .slice(1, halo_k, halo_k + target_k)
                .slice(2, halo_i, halo_i + target_i);
}

// Convenience overload with HaloInfo struct for cases needing explicit offset control
struct HaloInfo3D {
    int64_t diff_j, diff_k, diff_i;
    int64_t halo_j, halo_k, halo_i;
    bool has_odd_halo;

    static HaloInfo3D compute(const torch::Tensor& input,
                              int64_t target_j, int64_t target_k, int64_t target_i) {
        HaloInfo3D info;
        info.diff_j = input.size(0) - target_j;
        info.diff_k = input.size(1) - target_k;
        info.diff_i = input.size(2) - target_i;
        info.halo_j = info.diff_j / 2;
        info.halo_k = info.diff_k / 2;
        info.halo_i = info.diff_i / 2;
        info.has_odd_halo = (info.diff_j > 0 && info.diff_j % 2 != 0) ||
                            (info.diff_k > 0 && info.diff_k % 2 != 0) ||
                            (info.diff_i > 0 && info.diff_i % 2 != 0);
        return info;
    }

    bool is_valid() const {
        return diff_j >= 0 && diff_k >= 0 && diff_i >= 0;
    }
};

// FIX 2025-01-11 Round27: Cache for rdx_t/rdy_t 0D tensors in applySmagorinskyTurbulenceWRF
// Avoids repeated scalar_tensor() allocation per call when dx/dy/dtype/device unchanged
// FIX 2025-01-11 Round28 Issue 7: Use FP32 compute dtype for FP16/BF16 inputs to avoid precision loss
struct RdxRdyCache {
    double cached_dx = 0.0;
    double cached_dy = 0.0;
    torch::ScalarType cached_input_dtype = torch::kFloat32;  // Original input dtype for cache key
    torch::ScalarType cached_compute_dtype = torch::kFloat32;  // Actual compute dtype (FP32 for half)
    torch::DeviceType cached_device_type = torch::kCPU;
    int16_t cached_device_idx = -1;
    torch::Tensor cached_rdx_t;  // 0D tensor (always FP32 or FP64, never half)
    torch::Tensor cached_rdy_t;  // 0D tensor (always FP32 or FP64, never half)

    // Helper to get compute dtype (FP32 for half precision types)
    static torch::ScalarType compute_dtype(torch::ScalarType input_dtype) {
        if (input_dtype == torch::kFloat64) {
            return torch::kFloat64;
        }
        // FP16, BF16, and FP32 all use FP32 for computation
        return torch::kFloat32;
    }

    // Check if cache is valid for given parameters
    bool is_valid(double dx, double dy, const torch::TensorOptions& opts) const {
        if (!cached_rdx_t.defined() || !cached_rdy_t.defined()) return false;
        if (cached_dx != dx || cached_dy != dy) return false;
        auto input_dtype = opts.dtype().toScalarType();
        if (cached_input_dtype != input_dtype) return false;
        auto device = opts.device();
        if (cached_device_type != device.type()) return false;
        // OPT Pass33+: Use get_device_index helper for consistency with rest of file
        if (cached_device_idx != DnwMeanCache::get_device_index(device)) return false;
        return true;
    }

    // Update cache with new values
    std::pair<torch::Tensor, torch::Tensor> get_or_compute(
        double dx, double dy, const torch::TensorOptions& opts) {
        if (is_valid(dx, dy, opts)) {
            return {cached_rdx_t, cached_rdy_t};
        }
        // Compute new tensors with proper compute dtype
        const double rdx_d = 1.0 / dx;
        const double rdy_d = 1.0 / dy;
        auto input_dtype = opts.dtype().toScalarType();
        auto comp_dtype = compute_dtype(input_dtype);
        auto metric_opts = opts.dtype(comp_dtype).requires_grad(false);

        if (comp_dtype == torch::kFloat64) {
            cached_rdx_t = torch::scalar_tensor(rdx_d, metric_opts);
            cached_rdy_t = torch::scalar_tensor(rdy_d, metric_opts);
        } else {
            cached_rdx_t = torch::scalar_tensor(static_cast<float>(rdx_d), metric_opts);
            cached_rdy_t = torch::scalar_tensor(static_cast<float>(rdy_d), metric_opts);
        }
        // Update cache keys
        cached_dx = dx;
        cached_dy = dy;
        cached_input_dtype = input_dtype;
        cached_compute_dtype = comp_dtype;
        auto device = metric_opts.device();
        cached_device_type = device.type();
        // FIX Round165: Use get_device_index helper for range-safe device index
        cached_device_idx = DnwMeanCache::get_device_index(device);
        return {cached_rdx_t, cached_rdy_t};
    }
};
static thread_local RdxRdyCache rdx_rdy_cache;

// FIX 2025-01-11 Round27: Cache for mlen_h/mlen_v 0D tensors in computeSmagorinskyCoefficientsWRF
// Avoids repeated scalar_tensor() allocation per call for constant mixing length case
struct MlenCache {
    double cached_dx = 0.0;
    double cached_dnw_mean = 0.0;
    torch::ScalarType cached_dtype = torch::kFloat32;
    torch::DeviceType cached_device_type = torch::kCPU;
    int16_t cached_device_idx = -1;
    torch::Tensor cached_mlen_h;  // 0D tensor
    torch::Tensor cached_mlen_v;  // 0D tensor (only for scalar case, not 3D expanded)

    // OPT Pass34: Local get_device_index helper (delegates to DnwMeanCache implementation)
    static int16_t get_device_index(const torch::Device& dev) {
        return DnwMeanCache::get_device_index(dev);
    }

    // Check if cache is valid for given parameters (scalar mlen_v only)
    bool is_valid(double dx, double dnw_mean, const torch::TensorOptions& opts) const {
        if (!cached_mlen_h.defined() || !cached_mlen_v.defined()) return false;
        if (cached_dx != dx || cached_dnw_mean != dnw_mean) return false;
        auto dtype = opts.dtype().toScalarType();
        if (cached_dtype != dtype) return false;
        auto device = opts.device();
        if (cached_device_type != device.type()) return false;
        // FIX Round165: Use get_device_index helper for range-safe comparison
        if (get_device_index(device) != cached_device_idx) return false;
        return true;
    }

    // Get cached mlen_h (always 0D)
    torch::Tensor get_mlen_h(double dx, const torch::TensorOptions& opts) {
        // FIX 2025-01-11 Round28 Issue 6: Add device index check for multi-GPU correctness
        // FIX Round165: Use get_device_index helper for range-safe device index
        const auto device = opts.device();
        const int16_t device_idx = get_device_index(device);
        if (cached_mlen_h.defined() && cached_dx == dx &&
            cached_dtype == opts.dtype().toScalarType() &&
            cached_device_type == device.type() &&
            cached_device_idx == device_idx) {
            return cached_mlen_h;
        }
        // Compute new tensor - use same precision as opts
        if (opts.dtype().toScalarType() == torch::kFloat64) {
            cached_mlen_h = torch::scalar_tensor(dx, opts);
        } else {
            cached_mlen_h = torch::scalar_tensor(static_cast<float>(dx), opts);
        }
        cached_dx = dx;
        cached_dtype = opts.dtype().toScalarType();
        cached_device_type = opts.device().type();
        cached_device_idx = get_device_index(opts.device());
        return cached_mlen_h;
    }

    // Get cached mlen_v (scalar case only - 0D tensor)
    torch::Tensor get_mlen_v_scalar(double dnw_mean, const torch::TensorOptions& opts) {
        // FIX 2025-01-11 Round28 Issue 6: Add device index check for multi-GPU correctness
        // FIX Round165: Use get_device_index helper for range-safe device index
        const auto device = opts.device();
        const int16_t device_idx = get_device_index(device);
        if (cached_mlen_v.defined() && cached_dnw_mean == dnw_mean &&
            cached_dtype == opts.dtype().toScalarType() &&
            cached_device_type == device.type() &&
            cached_device_idx == device_idx) {
            return cached_mlen_v;
        }
        // Compute new tensor - use same precision as opts
        if (opts.dtype().toScalarType() == torch::kFloat64) {
            cached_mlen_v = torch::scalar_tensor(dnw_mean, opts);
        } else {
            cached_mlen_v = torch::scalar_tensor(static_cast<float>(dnw_mean), opts);
        }
        cached_dnw_mean = dnw_mean;
        cached_dtype = opts.dtype().toScalarType();
        cached_device_type = opts.device().type();
        cached_device_idx = get_device_index(opts.device());
        return cached_mlen_v;
    }
};
static thread_local MlenCache mlen_cache;

// FIX 2025-12-29 Batch13 Issue 5: Cache for msfux/msfuy interior 3D tensors
// FIX 2025-12-30 Batch14 Issues 1-3: Added signature sampling, stride validation, proper device index
// FIX 2025-12-30 Batch20 Issue 6: GPU signature check design rationale:
//   - CPU: 4-corner cheap check every call (direct mem access, no sync) + 5-point on period
//   - GPU/MPS: 5-point only on period (D2H sync cost same for 4-corner vs 5-point)
//   - Separate GPU 4-corner check would be redundant since 5-point includes all corners + center
//   - If higher-frequency GPU checking is needed, reduce verify_period via setMetricDebugLevel()
// Avoids repeated .to(opts) + slice + unsqueeze + expand per call
struct MsfInteriorCache {
    // Key: source tensor identity + target shape/device/dtype
    void* cached_msfux_ptr = nullptr;
    void* cached_msfuy_ptr = nullptr;
    int64_t cached_ny = 0, cached_nz = 0, cached_nx = 0;
    torch::DeviceType cached_device_type = torch::kCPU;
    int16_t cached_device_idx = -1;
    torch::ScalarType cached_dtype = torch::kFloat32;
    uint64_t cached_epoch = 0;

    // FIX 2025-12-30 Batch14 Issue 2: Track strides for view/transpose detection
    int64_t cached_msfux_stride0 = -1, cached_msfux_stride1 = -1;
    int64_t cached_msfuy_stride0 = -1, cached_msfuy_stride1 = -1;

    // FIX 2025-12-30 Batch14 Issue 1: 5-point signature for in-place change detection
    // Samples: [0,0], [0,-1], [-1,0], [-1,-1], [mid,mid]
    // FIX 2025-12-30 Batch15 Issue 4: Use valueToBits for FP64 precision
    // FIX 2025-12-30 Batch17 Issue 5: First 4 points = corners (cheap check every call)
    //                                 5th point = center (full check on period)
    uint64_t sig_msfux_bits[5] = {0, 0, 0, 0, 0};
    uint64_t sig_msfuy_bits[5] = {0, 0, 0, 0, 0};
    mutable uint64_t verify_call_count = 0;
    // FIX 2025-12-30 Batch19 Issue 5: Once-per-instance NaN/Inf warning flag
    mutable bool warned_nonfinite_ = false;

    // Cached results
    torch::Tensor msfux_3d;  // [ny, nz, nx-1]
    torch::Tensor msfuy_3d;  // [ny, nz, nx-1]

    // FIX 2025-12-30 Batch14 Issue 3: Proper device index resolution
    // FIX Round178: Use safe_device_index for range-checked int16_t conversion
    // FIX Round181: Handle MPS consistently - MPS typically has no index (single device)
    //              Return 0 for MPS without index to ensure consistent cache keying
    static int16_t get_device_index(const torch::Device& dev) {
        if (dev.has_index()) return safe_device_index(dev.index());
        #ifdef USE_CUDA
        if (dev.type() == torch::kCUDA) return safe_device_index(at::cuda::current_device());
        #endif
        // MPS devices typically don't have explicit indices (single GPU)
        // Return 0 for consistent cache keying instead of -1
        if (dev.type() == torch::kMPS) return 0;
        return -1;  // CPU or unknown device type
    }

    // FIX 2025-12-30 Batch15 Issue 3: Cache for index tensor in compute_5point_sig
    // Avoids repeated tensor creation when dimensions and device match
    struct Sig5IndexCache {
        int64_t cached_ny = 0, cached_nx = 0;
        torch::DeviceType cached_device_type = torch::kCPU;
        int16_t cached_device_idx = -1;
        torch::Tensor idx_tensor;

        bool is_valid(int64_t ny, int64_t nx, const torch::Device& dev) const {
            if (!idx_tensor.defined()) return false;
            if (cached_ny != ny || cached_nx != nx) return false;
            if (cached_device_type != dev.type()) return false;
            // FIX Round181: Use centralized get_device_index for consistent handling
            return cached_device_idx == get_device_index(dev);
        }

        torch::Tensor get_or_compute(int64_t ny, int64_t nx, const torch::Device& dev) {
            if (is_valid(ny, nx, dev)) return idx_tensor;

            // Compute new index tensor
            int64_t mid_y = ny / 2;
            int64_t mid_x = nx / 2;
            std::vector<int64_t> indices = {
                0,                          // [0, 0]
                nx - 1,                     // [0, nx-1]
                (ny - 1) * nx,              // [ny-1, 0]
                (ny - 1) * nx + (nx - 1),   // [ny-1, nx-1]
                mid_y * nx + mid_x          // [mid_y, mid_x] - CENTER
            };

            idx_tensor = torch::tensor(indices, torch::TensorOptions().dtype(torch::kLong).device(dev));
            cached_ny = ny;
            cached_nx = nx;
            cached_device_type = dev.type();
            // FIX Round165: Use get_device_index helper for range-safe device index
            cached_device_idx = get_device_index(dev);
            #ifdef USE_CUDA
            // Note: get_device_index already handles CUDA current_device fallback
            if (false && dev.type() == torch::kCUDA && !dev.has_index()) {  // Disabled - handled by helper
                cached_device_idx = static_cast<int16_t>(at::cuda::current_device());
            }
            #endif
            return idx_tensor;
        }
    };
    static thread_local Sig5IndexCache sig5_idx_cache;

    // FIX 2025-12-30 Batch15 Issue 4: valueToBits helpers for exact FP comparison
    static uint64_t valueToBits(float val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    static uint64_t valueToBits(double val) {
        if (!std::isfinite(val)) return 0xFFFFFFFFFFFFFFFFULL;
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        return bits;
    }

    // Compute 5-point signature from 2D tensor using valueToBits
    // FIX 2025-12-30 Batch15 Issue 4: Preserve dtype precision
    static void compute_5point_sig(const torch::Tensor& t, uint64_t* sig_bits) {
        for (int i = 0; i < 5; ++i) sig_bits[i] = 0;
        if (!t.defined() || t.numel() == 0 || t.dim() < 2) return;

        torch::NoGradGuard no_grad;
        int64_t ny = t.size(0);
        int64_t nx = t.size(1);

        // FIX 2025-12-30 Batch15 Issue 3: Use cached index tensor
        auto idx = sig5_idx_cache.get_or_compute(ny, nx, t.device());
        torch::Tensor t_flat = t.is_contiguous() ? t.flatten() : t.contiguous().flatten();

        // Preserve dtype for precision-aware bit comparison
        auto samples = t_flat.index_select(0, idx).to(torch::kCPU);

        if (samples.scalar_type() == torch::kFloat64) {
            auto* ptr = samples.data_ptr<double>();
            for (int i = 0; i < 5; ++i) sig_bits[i] = valueToBits(ptr[i]);
        } else {
            auto samples_f32 = samples.to(torch::kFloat32);
            auto* ptr = samples_f32.data_ptr<float>();
            for (int i = 0; i < 5; ++i) sig_bits[i] = valueToBits(ptr[i]);
        }
    }

    // FIX 2025-12-30 Batch18 Issue 5: SENTINEL rejection to prevent NaN/Inf cache thrash
    // NaN/Inf values produce SENTINEL bits which should never match (unstable data)
    static constexpr uint64_t SENTINEL = 0xFFFFFFFFFFFFFFFFULL;

    // Exact bit comparison for signature matching with SENTINEL rejection
    static bool sig_matches(const uint64_t* cached, const uint64_t* current, int len = 5) {
        for (int i = 0; i < len; ++i) {
            // Reject if either signature has NaN/Inf (SENTINEL)
            if (cached[i] == SENTINEL || current[i] == SENTINEL) return false;
            if (cached[i] != current[i]) return false;
        }
        return true;
    }

    // Check if signature contains any NaN/Inf values
    static bool sig_has_sentinel(const uint64_t* sig, int len = 5) {
        for (int i = 0; i < len; ++i) {
            if (sig[i] == SENTINEL) return true;
        }
        return false;
    }

    // FIX 2025-12-30 Batch17 Issue 5: Cheap 4-corner check (no GPU index tensor, direct access)
    // FIX 2025-12-30 Batch18 Issue 3: Skip D2H for CUDA/MPS - rely on periodic full check instead
    // Only checks corners [0,0], [0,-1], [-1,0], [-1,-1] - faster than full 5-point
    // Returns true if check was performed (CPU contiguous), false if skipped (CUDA/MPS/non-contiguous)
    static bool compute_4corner_sig_cheap(const torch::Tensor& t, uint64_t* sig_bits) {
        for (int i = 0; i < 4; ++i) sig_bits[i] = 0;
        if (!t.defined() || t.numel() == 0 || t.dim() < 2) return false;

        torch::NoGradGuard no_grad;
        int64_t ny = t.size(0);
        int64_t nx = t.size(1);
        if (ny < 1 || nx < 1) return false;

        // FIX 2025-12-30 Batch18 Issue 3: Only perform cheap check on CPU contiguous tensors
        // For CUDA/MPS, this would require D2H sync which is expensive
        // Skip check and rely on: (1) key components (ptr, epoch, strides) + (2) periodic full check
        if (t.device().type() != torch::kCPU || !t.is_contiguous()) {
            return false;  // Signal caller to skip corner-based invalidation
        }

        // CPU contiguous fast path: direct memory access (no sync needed)
        if (t.scalar_type() == torch::kFloat64) {
            const double* ptr = t.data_ptr<double>();
            sig_bits[0] = valueToBits(ptr[0]);                           // [0,0]
            sig_bits[1] = valueToBits(ptr[nx - 1]);                      // [0,-1]
            sig_bits[2] = valueToBits(ptr[(ny - 1) * nx]);               // [-1,0]
            sig_bits[3] = valueToBits(ptr[(ny - 1) * nx + (nx - 1)]);    // [-1,-1]
        } else {
            // FP16/BF16/other -> convert to FP32 for comparison
            auto t_f32 = t.to(torch::kFloat32);
            const float* ptr = t_f32.data_ptr<float>();
            sig_bits[0] = valueToBits(ptr[0]);
            sig_bits[1] = valueToBits(ptr[nx - 1]);
            sig_bits[2] = valueToBits(ptr[(ny - 1) * nx]);
            sig_bits[3] = valueToBits(ptr[(ny - 1) * nx + (nx - 1)]);
        }
        return true;  // Check was performed
    }

    bool is_valid(const torch::Tensor& msfux, const torch::Tensor& msfuy,
                  int64_t ny, int64_t nz, int64_t nx,
                  const torch::TensorOptions& opts) {
        if (!msfux_3d.defined() || !msfuy_3d.defined()) return false;

        uint64_t current_epoch = metric_utils::getGlobalMetricEpoch();
        if (cached_epoch != current_epoch) return false;

        if (cached_ny != ny || cached_nz != nz || cached_nx != nx) return false;

        void* ux_ptr = msfux.defined() ? msfux.data_ptr() : nullptr;
        void* uy_ptr = msfuy.defined() ? msfuy.data_ptr() : nullptr;
        if (cached_msfux_ptr != ux_ptr || cached_msfuy_ptr != uy_ptr) return false;

        // FIX 2025-12-30 Batch14 Issue 2: Stride validation
        if (msfux.defined() && msfux.dim() >= 2) {
            if (cached_msfux_stride0 != msfux.stride(0) ||
                cached_msfux_stride1 != msfux.stride(1)) return false;
        }
        if (msfuy.defined() && msfuy.dim() >= 2) {
            if (cached_msfuy_stride0 != msfuy.stride(0) ||
                cached_msfuy_stride1 != msfuy.stride(1)) return false;
        }

        // FIX 2025-12-30 Batch14 Issue 3: Proper device index
        torch::DeviceType dev_type = opts.device().type();
        int16_t dev_idx = get_device_index(opts.device());
        torch::ScalarType dt = opts.dtype().toScalarType();

        if (cached_device_type != dev_type || cached_device_idx != dev_idx ||
            cached_dtype != dt) return false;

        // FIX 2025-12-30 Batch17 Issue 5: Two-tier signature verification
        // FIX 2025-12-30 Batch18 Issue 3: Cheap check only on CPU, CUDA relies on periodic full check
        // Tier 1: Cheap 4-corner check (only for CPU contiguous - avoids D2H sync)
        uint64_t corner_sig[4];
        bool ux_checked = compute_4corner_sig_cheap(msfux, corner_sig);
        if (ux_checked && !sig_matches(sig_msfux_bits, corner_sig, 4)) return false;
        bool uy_checked = compute_4corner_sig_cheap(msfuy, corner_sig);
        if (uy_checked && !sig_matches(sig_msfuy_bits, corner_sig, 4)) return false;

        // Tier 2: Full 5-point check on period (catches center changes + CUDA/MPS in-place changes)
        // FIX 2025-12-30 Batch15 Issue 2: Dynamic verify_period via config/moving_nest
        uint64_t verify_period = metric_utils::getVerifyPeriod();
        ++verify_call_count;
        // For CUDA/MPS (where cheap check was skipped), periodic full check is the only in-place detection
        bool needs_full_check = (verify_period > 0 && (verify_call_count % verify_period) == 0);
        if (needs_full_check) {
            uint64_t current_sig[5];
            compute_5point_sig(msfux, current_sig);
            if (!sig_matches(sig_msfux_bits, current_sig)) return false;
            compute_5point_sig(msfuy, current_sig);
            if (!sig_matches(sig_msfuy_bits, current_sig)) return false;
        }

        return true;
    }

    void update(const torch::Tensor& msfux, const torch::Tensor& msfuy,
                int64_t ny, int64_t nz, int64_t nx,
                const torch::TensorOptions& opts,
                const torch::Tensor& ux_3d, const torch::Tensor& uy_3d) {
        cached_msfux_ptr = msfux.defined() ? msfux.data_ptr() : nullptr;
        cached_msfuy_ptr = msfuy.defined() ? msfuy.data_ptr() : nullptr;
        cached_ny = ny;
        cached_nz = nz;
        cached_nx = nx;
        cached_device_type = opts.device().type();
        cached_device_idx = get_device_index(opts.device());
        cached_dtype = opts.dtype().toScalarType();
        cached_epoch = metric_utils::getGlobalMetricEpoch();

        // FIX 2025-12-30 Batch14 Issue 2: Cache strides
        if (msfux.defined() && msfux.dim() >= 2) {
            cached_msfux_stride0 = msfux.stride(0);
            cached_msfux_stride1 = msfux.stride(1);
        } else {
            cached_msfux_stride0 = cached_msfux_stride1 = -1;
        }
        if (msfuy.defined() && msfuy.dim() >= 2) {
            cached_msfuy_stride0 = msfuy.stride(0);
            cached_msfuy_stride1 = msfuy.stride(1);
        } else {
            cached_msfuy_stride0 = cached_msfuy_stride1 = -1;
        }

        // FIX 2025-12-30 Batch14 Issue 1: Compute signatures
        // FIX 2025-12-30 Batch15 Issue 4: Use valueToBits for FP64 precision
        compute_5point_sig(msfux, sig_msfux_bits);
        compute_5point_sig(msfuy, sig_msfuy_bits);
        verify_call_count = 0;

        // FIX 2025-12-30 Batch18 Issue 5: Warn if NaN/Inf detected in source data
        // FIX 2025-12-30 Batch19 Issue 5: Once-per-instance warning using member flag
        if (sig_has_sentinel(sig_msfux_bits) || sig_has_sentinel(sig_msfuy_bits)) {
            if (!warned_nonfinite_) {
                warned_nonfinite_ = true;
                std::cerr << "[MsfInteriorCache] Warning: NaN/Inf detected in msf source data. "
                          << "Cache disabled for affected entries (thrash prevention). "
                          << "Check numerical stability.\n";
            }
        }

        msfux_3d = ux_3d;
        msfuy_3d = uy_3d;
    }
};

// Out-of-class definition for nested static thread_local member
thread_local MsfInteriorCache::Sig5IndexCache MsfInteriorCache::sig5_idx_cache;

static thread_local MsfInteriorCache msf_interior_cache;

// FIX 2025-12-29 Batch13 Issue 5: Cache for msfvx/msfvy interior 3D tensors (v-staggered)
// FIX 2025-12-30 Batch14 Issues 1-3: Added signature sampling, stride validation, proper device index
// FIX 2025-12-30 Batch20 Issue 6: GPU signature check - same rationale as MsfInteriorCache above
// Separate cache from u-staggered because output shapes differ: [ny-1, nz, nx] vs [ny, nz, nx-1]
struct MsfVInteriorCache {
    void* cached_msfvx_ptr = nullptr;
    void* cached_msfvy_ptr = nullptr;
    int64_t cached_ny = 0, cached_nz = 0, cached_nx = 0;
    torch::DeviceType cached_device_type = torch::kCPU;
    int16_t cached_device_idx = -1;
    torch::ScalarType cached_dtype = torch::kFloat32;
    uint64_t cached_epoch = 0;

    // FIX 2025-12-30 Batch14 Issue 2: Track strides
    int64_t cached_msfvx_stride0 = -1, cached_msfvx_stride1 = -1;
    int64_t cached_msfvy_stride0 = -1, cached_msfvy_stride1 = -1;

    // FIX 2025-12-30 Batch14 Issue 1: 5-point signature
    // FIX 2025-12-30 Batch15 Issue 4: Use valueToBits for FP64 precision
    // FIX 2025-12-30 Batch17 Issue 5: First 4 points = corners (cheap), 5th = center (full)
    uint64_t sig_msfvx_bits[5] = {0, 0, 0, 0, 0};
    uint64_t sig_msfvy_bits[5] = {0, 0, 0, 0, 0};
    mutable uint64_t verify_call_count = 0;
    // FIX 2025-12-30 Batch19 Issue 5: Once-per-instance NaN/Inf warning flag
    mutable bool warned_nonfinite_ = false;

    // Cached results
    torch::Tensor msfvx_3d;  // [ny-1, nz, nx]
    torch::Tensor msfvy_3d;  // [ny-1, nz, nx]

    // Reuse helpers from MsfInteriorCache
    static int16_t get_device_index(const torch::Device& dev) {
        return MsfInteriorCache::get_device_index(dev);
    }

    bool is_valid(const torch::Tensor& msfvx, const torch::Tensor& msfvy,
                  int64_t ny, int64_t nz, int64_t nx,
                  const torch::TensorOptions& opts) {
        if (!msfvx_3d.defined() || !msfvy_3d.defined()) return false;

        uint64_t current_epoch = metric_utils::getGlobalMetricEpoch();
        if (cached_epoch != current_epoch) return false;

        if (cached_ny != ny || cached_nz != nz || cached_nx != nx) return false;

        void* vx_ptr = msfvx.defined() ? msfvx.data_ptr() : nullptr;
        void* vy_ptr = msfvy.defined() ? msfvy.data_ptr() : nullptr;
        if (cached_msfvx_ptr != vx_ptr || cached_msfvy_ptr != vy_ptr) return false;

        // FIX 2025-12-30 Batch14 Issue 2: Stride validation
        if (msfvx.defined() && msfvx.dim() >= 2) {
            if (cached_msfvx_stride0 != msfvx.stride(0) ||
                cached_msfvx_stride1 != msfvx.stride(1)) return false;
        }
        if (msfvy.defined() && msfvy.dim() >= 2) {
            if (cached_msfvy_stride0 != msfvy.stride(0) ||
                cached_msfvy_stride1 != msfvy.stride(1)) return false;
        }

        // FIX 2025-12-30 Batch14 Issue 3: Proper device index
        torch::DeviceType dev_type = opts.device().type();
        int16_t dev_idx = get_device_index(opts.device());
        torch::ScalarType dt = opts.dtype().toScalarType();

        if (cached_device_type != dev_type || cached_device_idx != dev_idx ||
            cached_dtype != dt) return false;

        // FIX 2025-12-30 Batch17 Issue 5: Two-tier signature verification
        // FIX 2025-12-30 Batch18 Issue 3: Cheap check only on CPU, CUDA relies on periodic full check
        // Tier 1: Cheap 4-corner check (only for CPU contiguous - avoids D2H sync)
        uint64_t corner_sig[4];
        bool vx_checked = MsfInteriorCache::compute_4corner_sig_cheap(msfvx, corner_sig);
        if (vx_checked && !MsfInteriorCache::sig_matches(sig_msfvx_bits, corner_sig, 4)) return false;
        bool vy_checked = MsfInteriorCache::compute_4corner_sig_cheap(msfvy, corner_sig);
        if (vy_checked && !MsfInteriorCache::sig_matches(sig_msfvy_bits, corner_sig, 4)) return false;

        // Tier 2: Full 5-point check on period (catches center changes + CUDA/MPS in-place changes)
        // FIX 2025-12-30 Batch15 Issue 2: Dynamic verify_period via config/moving_nest
        uint64_t verify_period = metric_utils::getVerifyPeriod();
        ++verify_call_count;
        bool needs_full_check = (verify_period > 0 && (verify_call_count % verify_period) == 0);
        if (needs_full_check) {
            uint64_t current_sig[5];
            MsfInteriorCache::compute_5point_sig(msfvx, current_sig);
            if (!MsfInteriorCache::sig_matches(sig_msfvx_bits, current_sig)) return false;
            MsfInteriorCache::compute_5point_sig(msfvy, current_sig);
            if (!MsfInteriorCache::sig_matches(sig_msfvy_bits, current_sig)) return false;
        }

        return true;
    }

    void update(const torch::Tensor& msfvx, const torch::Tensor& msfvy,
                int64_t ny, int64_t nz, int64_t nx,
                const torch::TensorOptions& opts,
                const torch::Tensor& vx_3d, const torch::Tensor& vy_3d) {
        cached_msfvx_ptr = msfvx.defined() ? msfvx.data_ptr() : nullptr;
        cached_msfvy_ptr = msfvy.defined() ? msfvy.data_ptr() : nullptr;
        cached_ny = ny;
        cached_nz = nz;
        cached_nx = nx;
        cached_device_type = opts.device().type();
        cached_device_idx = get_device_index(opts.device());
        cached_dtype = opts.dtype().toScalarType();
        cached_epoch = metric_utils::getGlobalMetricEpoch();

        // FIX 2025-12-30 Batch14 Issue 2: Cache strides
        if (msfvx.defined() && msfvx.dim() >= 2) {
            cached_msfvx_stride0 = msfvx.stride(0);
            cached_msfvx_stride1 = msfvx.stride(1);
        } else {
            cached_msfvx_stride0 = cached_msfvx_stride1 = -1;
        }
        if (msfvy.defined() && msfvy.dim() >= 2) {
            cached_msfvy_stride0 = msfvy.stride(0);
            cached_msfvy_stride1 = msfvy.stride(1);
        } else {
            cached_msfvy_stride0 = cached_msfvy_stride1 = -1;
        }

        // FIX 2025-12-30 Batch14 Issue 1: Compute signatures
        // FIX 2025-12-30 Batch15 Issue 4: Use valueToBits for FP64 precision
        MsfInteriorCache::compute_5point_sig(msfvx, sig_msfvx_bits);
        MsfInteriorCache::compute_5point_sig(msfvy, sig_msfvy_bits);
        verify_call_count = 0;

        // FIX 2025-12-30 Batch18 Issue 5: Warn if NaN/Inf detected in source data
        // FIX 2025-12-30 Batch19 Issue 5: Once-per-instance warning using member flag
        if (MsfInteriorCache::sig_has_sentinel(sig_msfvx_bits) ||
            MsfInteriorCache::sig_has_sentinel(sig_msfvy_bits)) {
            if (!warned_nonfinite_) {
                warned_nonfinite_ = true;
                std::cerr << "[MsfVInteriorCache] Warning: NaN/Inf detected in msfv source data. "
                          << "Cache disabled for affected entries (thrash prevention). "
                          << "Check numerical stability.\n";
            }
        }

        msfvx_3d = vx_3d;
        msfvy_3d = vy_3d;
    }
};

static thread_local MsfVInteriorCache msf_v_interior_cache;

}  // anonymous namespace

void computeDeformationTensorsWRF(const torch::Tensor& u,
                                 const torch::Tensor& v,
                                 const torch::Tensor& w,
                                 torch::Tensor& defor11,
                                 torch::Tensor& defor22,
                                 torch::Tensor& defor33,
                                 torch::Tensor& defor12,
                                 torch::Tensor& defor13,
                                 torch::Tensor& defor23,
                                 torch::Tensor& div,
                                 const WRFGridInfoExtended& grid) {
    
    // Ensure all tensors are on the same device
    auto device = u.device();
    auto dtype = u.dtype();
    
    // Get dimensions
    int ny = grid.ny;
    int nx = grid.nx;
    int nz = grid.nz;

    // FIX 2025-12-27: Use unified helper for rdzw alignment (handles 1D/2D/3D)
    // FIX 2025-12-29 Batch12 Issue 2: Enable normalize=true for robust vertical derivative scaling
    // FIX 2025-01-10 Round19: Pass grid.rdzw_kdim for cubic grid disambiguation
    auto opts = u.options();
    torch::Tensor rdzw_3d = metric_utils::align_rdzw_for_mass(grid.rdzw, ny, nz, nx, opts, grid.rdzw_kdim, /*normalize=*/true);

    // Get vertical interpolation weights (1D tensors)
    torch::Tensor fnm = grid.fnm;
    torch::Tensor fnp = grid.fnp;
    
    // Get extrapolation coefficients (already tensors in grid)
    torch::Tensor cf1 = grid.cf1.to(device).to(dtype);
    torch::Tensor cf2 = grid.cf2.to(device).to(dtype);
    torch::Tensor cf3 = grid.cf3.to(device).to(dtype);
    // Top boundary extrapolation coefficients (default values from WRF)
    torch::Tensor cft1 = torch::scalar_tensor(1.5f, torch::TensorOptions().dtype(dtype).device(device));
    torch::Tensor cft2 = torch::scalar_tensor(-0.5f, torch::TensorOptions().dtype(dtype).device(device));
    
    // Working arrays
    torch::Tensor hat = torch::zeros({ny+1, nz+1, nx+1}, u.options());
    torch::Tensor hatavg = torch::zeros({ny+1, nz+1, nx+1}, u.options());
    torch::Tensor tmp1 = torch::zeros({ny, nz, nx}, u.options());
    
    // ========================================================================
    // defor11 = 2*du/dx at mass points (WRF-consistent with AD compatibility)
    // ========================================================================
    // D11 = 2 * m^2 * (∂u^/∂X + ∂ψ/∂x * ∂u^/∂ψ)
    
    // Apply coordinate transformation to u: hat = u / msfuy
    // FIX 2025-12-27: Use unified helper for msf alignment (handles 2D/3D, device/dtype)
    // TODO 2025-01-10 Round20: Consider caching aligned metrics at grid-level or using
    // StaticMetricCache3D for repeated align_msf_3d/align_rdz_for_u/v calls in this function.
    // Multiple calls with same grid/device/dtype could benefit from caching expansion/padding.
    torch::Tensor msfuy_3d = metric_utils::align_msf_3d(grid.msfuy, ny, nz, nx, metric_utils::Stagger::U, opts);
    torch::Tensor u_hat = u / msfuy_3d;  // Broadcasting handles the division
    
    // Vertical interpolation to w-levels using fnm/fnp weights
    // fnm, fnp are 1D tensors of size (nz+1)
    torch::Tensor fnm_3d = fnm.view({1, -1, 1});  // Shape: (1, nz+1, 1)
    torch::Tensor fnp_3d = fnp.view({1, -1, 1});  // Shape: (1, nz+1, 1)
    
    // Average in z direction - need to handle w-staggered levels
    torch::Tensor u_hat_avg = torch::zeros({ny, nz+1, nx+1}, u.options());
    
    // Interior points: weighted average
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    u_hat_avg.index_put_({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()},
        0.5f * (fnm.index({torch::indexing::Slice(1, nz)}).unsqueeze(0).unsqueeze(-1) *
                (u_hat.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()}) +
                 u_hat.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz-1), torch::indexing::Slice()}))));

    // Bottom boundary extrapolation
    u_hat_avg.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice()},
        cf1 * u_hat.index({torch::indexing::Slice(), 0, torch::indexing::Slice()}) +
        cf2 * u_hat.index({torch::indexing::Slice(), 1, torch::indexing::Slice()}) +
        cf3 * u_hat.index({torch::indexing::Slice(), 2, torch::indexing::Slice()}));

    // Top boundary extrapolation
    u_hat_avg.index_put_({torch::indexing::Slice(), nz, torch::indexing::Slice()},
        cft1 * u_hat.index({torch::indexing::Slice(), nz-2, torch::indexing::Slice()}) +
        cft2 * u_hat.index({torch::indexing::Slice(), nz-3, torch::indexing::Slice()}));
    
    // Average to mass points in x direction
    torch::Tensor u_hat_mass = 0.5f * (u_hat_avg.index({torch::indexing::Slice(), torch::indexing::Slice(), torch::indexing::Slice(0, nx)}) +
                                       u_hat_avg.index({torch::indexing::Slice(), torch::indexing::Slice(), torch::indexing::Slice(1, nx+1)}));
    
    // Terrain slope correction: ∂ψ/∂x * ∂u^/∂ψ
    torch::Tensor tmpzx = torch::zeros({ny, nz, nx}, u.options());
    if (grid.zx.defined() && grid.zx.numel() > 0) {
        // Average zx to mass points
        tmpzx = 0.25f * (grid.zx.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice(0, nx)}) +
                        grid.zx.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz+1), torch::indexing::Slice(0, nx)}) +
                        grid.zx.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice(1, nx+1)}) +
                        grid.zx.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz+1), torch::indexing::Slice(1, nx+1)}));
        
        // Vertical derivative with terrain correction
        tmp1 = (u_hat_mass.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz+1), torch::indexing::Slice()}) -
                u_hat_mass.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice()})) *
               tmpzx * rdzw_3d;
    }
    
    // Map scale factor squared: mm = msftx * msfty
    // FIX 2025-12-27: Use unified helper for msf alignment
    torch::Tensor msftx_3d = metric_utils::align_msf_3d(grid.msftx, ny, nz, nx, metric_utils::Stagger::Mass, opts);
    torch::Tensor msfty_3d = metric_utils::align_msf_3d(grid.msfty, ny, nz, nx, metric_utils::Stagger::Mass, opts);
    torch::Tensor mm = msftx_3d * msfty_3d;
    
    // Compute du/dx with map scale factor
    torch::Tensor dudx = mm * (grid.rdx * (u_hat.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice(1, nx+1)}) -
                                           u_hat.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice(0, nx)})) - tmp1);
    
    // D11 = 2 * du/dx
    defor11 = 2.0f * dudx;
    
    // Initialize divergence with du/dx
    div = dudx.clone();
    
    // ========================================================================
    // defor22 = 2*dv/dy at mass points (WRF-consistent with AD compatibility)
    // ========================================================================
    // D22 = 2 * m^2 * (∂v^/∂Y + ∂ψ/∂y * ∂v^/∂ψ)
    
    // Apply coordinate transformation to v: hat = v / msfvx
    // FIX 2025-12-28: Use align_msf_3d for unified 2D/3D + device/dtype handling
    torch::Tensor msfvx_3d = metric_utils::align_msf_3d(grid.msfvx, ny, nz, nx, metric_utils::Stagger::V, opts);
    torch::Tensor v_hat = torch::zeros({ny+1, nz, nx}, v.options());
    
    // Handle polar boundary conditions
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    v_hat.index_put_({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice()},
        v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice()}) /
        msfvx_3d.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice()}));
    
    // Vertical interpolation to w-levels
    torch::Tensor v_hat_avg = torch::zeros({ny, nz+1, nx}, v.options());
    
    // Average in y direction first, then vertical interpolation
    torch::Tensor v_hat_j_avg = 0.5f * (v_hat.index({torch::indexing::Slice(0, ny), torch::indexing::Slice(), torch::indexing::Slice()}) +
                                        v_hat.index({torch::indexing::Slice(1, ny+1), torch::indexing::Slice(), torch::indexing::Slice()}));
    
    // Interior points with fnm/fnp weights
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    v_hat_avg.index_put_({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()},
        fnm.index({torch::indexing::Slice(1, nz)}).unsqueeze(0).unsqueeze(-1) * v_hat_j_avg +
        fnp.index({torch::indexing::Slice(1, nz)}).unsqueeze(0).unsqueeze(-1) *
        v_hat_j_avg.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz-1), torch::indexing::Slice()}));

    // Bottom and top extrapolation
    v_hat_avg.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice()},
        cf1 * v_hat_j_avg.index({torch::indexing::Slice(), 0, torch::indexing::Slice()}) +
        cf2 * v_hat_j_avg.index({torch::indexing::Slice(), 1, torch::indexing::Slice()}) +
        cf3 * v_hat_j_avg.index({torch::indexing::Slice(), 2, torch::indexing::Slice()}));

    v_hat_avg.index_put_({torch::indexing::Slice(), nz, torch::indexing::Slice()},
        cft1 * v_hat_j_avg.index({torch::indexing::Slice(), nz-2, torch::indexing::Slice()}) +
        cft2 * v_hat_j_avg.index({torch::indexing::Slice(), nz-3, torch::indexing::Slice()}));
    
    // Terrain slope correction: ∂ψ/∂y * ∂v^/∂ψ
    torch::Tensor tmpzy = torch::zeros({ny, nz, nx}, v.options());
    if (grid.zy.defined() && grid.zy.numel() > 0) {
        tmpzy = 0.25f * (grid.zy.index({torch::indexing::Slice(0, ny), torch::indexing::Slice(0, nz), torch::indexing::Slice()}) +
                        grid.zy.index({torch::indexing::Slice(1, ny+1), torch::indexing::Slice(0, nz), torch::indexing::Slice()}) +
                        grid.zy.index({torch::indexing::Slice(0, ny), torch::indexing::Slice(1, nz+1), torch::indexing::Slice()}) +
                        grid.zy.index({torch::indexing::Slice(1, ny+1), torch::indexing::Slice(1, nz+1), torch::indexing::Slice()}));
        
        tmp1 = (v_hat_avg.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz+1), torch::indexing::Slice()}) -
                v_hat_avg.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice()})) *
               tmpzy * rdzw_3d;
    } else {
        tmp1.zero_();
    }
    
    // Compute dv/dy with map scale factor
    torch::Tensor dvdy = mm * (grid.rdy * (v_hat.index({torch::indexing::Slice(1, ny+1), torch::indexing::Slice(0, nz), torch::indexing::Slice()}) -
                                           v_hat.index({torch::indexing::Slice(0, ny), torch::indexing::Slice(0, nz), torch::indexing::Slice()})) - tmp1);
    
    // D22 = 2 * dv/dy
    defor22 = 2.0f * dvdy;
    
    // Add to divergence
    div = div + dvdy;
    
    // ========================================================================
    // defor33 = 2*dw/dz at mass points (straightforward)
    // ========================================================================
    // D33 = 2 * ∂w/∂z
    
    torch::Tensor dwdz = (w.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz+1), torch::indexing::Slice()}) -
                         w.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz), torch::indexing::Slice()})) * rdzw_3d;
    
    defor33 = 2.0f * dwdz;
    div = div + dwdz;
    
    // ========================================================================
    // defor12 = du/dy + dv/dx at vorticity points (WRF-consistent)
    // ========================================================================
    // D12 = m^2 * (∂v^/∂X + ∂u^/∂Y + ∂ψ/∂x * ∂v^/∂ψ + ∂ψ/∂y * ∂u^/∂ψ)

    // Create map scale factor at vorticity points
    // FIX 2025-12-27: Enforce 2D msfux/msfvy for vorticity point averaging
    // These are 4-corner averages assuming 2D map scale factors (k-invariant)
    TORCH_CHECK(!grid.msfux.defined() || grid.msfux.dim() == 2,
        "computeDeformationRatesExtended: msfux must be 2D for vorticity averaging, got ",
        grid.msfux.defined() ? grid.msfux.dim() : -1, "D");
    TORCH_CHECK(!grid.msfvy.defined() || grid.msfvy.dim() == 2,
        "computeDeformationRatesExtended: msfvy must be 2D for vorticity averaging, got ",
        grid.msfvy.defined() ? grid.msfvy.dim() : -1, "D");

    torch::Tensor mm_vort_x = 0.25f * (grid.msfux.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(1, nx)}) +
                                       grid.msfux.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nx)}) +
                                       grid.msfux.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(0, nx-1)}) +
                                       grid.msfux.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(0, nx-1)}));

    torch::Tensor mm_vort_y = 0.25f * (grid.msfvy.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(0, nx-1)}) +
                                       grid.msfvy.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nx)}) +
                                       grid.msfvy.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(0, nx-1)}) +
                                       grid.msfvy.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(1, nx)}));

    // FIX 2025-12-27: Ensure device alignment for vorticity point msf
    // Note: mm_vort_x/y are 2D corner averages, need unsqueeze for 3D operations
    torch::Tensor mm_vort = (mm_vort_x.to(opts) * mm_vort_y.to(opts));
    if (mm_vort.dim() == 2) {
        mm_vort = mm_vort.unsqueeze(1);  // Add k dimension for 3D broadcast
    }
    
    // Calculate du/dy and dv/dx at vorticity points
    // These calculations need to account for staggering
    
    // For simplicity in this AD-compatible version, we compute defor12 directly
    torch::Tensor dudy_vort = grid.rdy * (u.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice(1, nx)}) -
                                          u.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(), torch::indexing::Slice(1, nx)}));
    
    torch::Tensor dvdx_vort = grid.rdx * (v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice(1, nx)}) -
                                          v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice(0, nx-1)}));
    
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    defor12.index_put_({torch::indexing::Slice(1, ny), torch::indexing::Slice(), torch::indexing::Slice(1, nx)},
        mm_vort * (dudy_vort + dvdx_vort));

    // ========================================================================
    // defor13 = dw/dx + du/dz at u,w-staggered points
    // ========================================================================

    // Map scale factor at u-points
    // FIX 2025-12-27: Use unified helper for msf alignment
    torch::Tensor msfux_3d = metric_utils::align_msf_3d(grid.msfux, ny, nz, nx, metric_utils::Stagger::U, opts);
    torch::Tensor msfuy_u_3d = metric_utils::align_msf_3d(grid.msfuy, ny, nz, nx, metric_utils::Stagger::U, opts);
    torch::Tensor mm_u = msfux_3d * msfuy_u_3d;

    // Calculate dw/dx at u,w-staggered points
    torch::Tensor dwdx_u = grid.rdx * (w.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice(1, nx+1)}) -
                                       w.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice(0, nx)}));

    // Calculate du/dz at u,w-staggered points
    // FIX 2025-12-27: Use unified helper for rdz alignment (handles 1D/2D/3D + U-stagger padding)
    // FIX 2025-12-29 Batch12 Issue 2: Enable normalize=true for robust vertical derivative scaling
    // FIX 2025-01-10 Round20: Pass grid.rdz_kdim for cubic grid disambiguation
    torch::Tensor rdz_3d = metric_utils::align_rdz_for_u(grid.rdz, ny, nz, nx, opts, grid.rdz_kdim, /*normalize=*/true);
    torch::Tensor dudz_u = torch::zeros({ny, nz, nx+1}, u.options());
    dudz_u.index_put_({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()},
        (u.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()}) -
         u.index({torch::indexing::Slice(), torch::indexing::Slice(0, nz-1), torch::indexing::Slice()})) *
        rdz_3d.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice()}));

    defor13.index_put_({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice(1, nx)},
        mm_u.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice(1, nx)}) *
        (dwdx_u.index({torch::indexing::Slice(), torch::indexing::Slice(), torch::indexing::Slice(0, nx-1)}) +
         dudz_u.index({torch::indexing::Slice(), torch::indexing::Slice(1, nz), torch::indexing::Slice(1, nx)})));

    // Set boundaries to zero
    defor13.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice()}, 0.0f);
    defor13.index_put_({torch::indexing::Slice(), nz, torch::indexing::Slice()}, 0.0f);
    
    // ========================================================================
    // defor23 = dw/dy + dv/dz at v,w-staggered points
    // ========================================================================
    
    // Map scale factor at v-points
    // FIX 2025-12-27: Use unified helper for msf alignment
    // OPT Pass34: msfvx_3d already defined above for defor22, reuse if same - else use msfvx_3d_d23
    torch::Tensor msfvx_3d_d23 = metric_utils::align_msf_3d(grid.msfvx, ny, nz, nx, metric_utils::Stagger::V, opts);
    torch::Tensor msfvy_3d_d23 = metric_utils::align_msf_3d(grid.msfvy, ny, nz, nx, metric_utils::Stagger::V, opts);
    torch::Tensor mm_v = msfvx_3d_d23 * msfvy_3d_d23;

    // Calculate dw/dy at v,w-staggered points
    torch::Tensor dwdy_v = grid.rdy * (w.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()}) -
                                       w.index({torch::indexing::Slice(0, ny-1), torch::indexing::Slice(1, nz), torch::indexing::Slice()}));

    // Calculate dv/dz at v,w-staggered points
    // FIX 2025-12-27: Use unified helper for rdz alignment (handles 1D/2D/3D + V-stagger padding)
    // FIX 2025-12-29 Batch12 Issue 2: Enable normalize=true for robust vertical derivative scaling
    // FIX 2025-01-10 Round20: Pass grid.rdz_kdim for cubic grid disambiguation
    torch::Tensor rdz_3d_v = metric_utils::align_rdz_for_v(grid.rdz, ny, nz, nx, opts, grid.rdz_kdim, /*normalize=*/true);
    torch::Tensor dvdz_v = torch::zeros({ny+1, nz, nx}, v.options());
    dvdz_v.index_put_({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()},
        (v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()}) -
         v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(0, nz-1), torch::indexing::Slice()})) *
        rdz_3d_v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()}));

    defor23.index_put_({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()},
        mm_v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()}) *
        (dwdy_v + dvdz_v.index({torch::indexing::Slice(1, ny), torch::indexing::Slice(1, nz), torch::indexing::Slice()})));

    // Set boundaries to zero
    defor23.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice()}, 0.0f);
    defor23.index_put_({torch::indexing::Slice(), nz, torch::indexing::Slice()}, 0.0f);
}

// Placeholder implementations for other functions
void computeDivergenceDampingWRF(torch::Tensor& u_tend,
                                torch::Tensor& v_tend,
                                const torch::Tensor& u,
                                const torch::Tensor& v,
                                const torch::Tensor& mu,
                                const WRFGridInfoExtended& grid) {
    // WRF divergence damping implementation
    // Reference: module_big_step_utilities_em.F divergence_driver
    
    if (grid.kdamp <= 0.0f) {
        return;  // No divergence damping
    }
    
    const int nx = grid.nx;
    const int ny = grid.ny;
    const int nz = grid.nz;
    
    // Get grid metrics as tensors for AD
    auto rdx = grid.rdx.defined() ? grid.rdx.to(u.options()) : torch::zeros({1}, u.options());
    auto rdy = grid.rdy.defined() ? grid.rdy.to(u.options()) : torch::zeros({1}, v.options());
    auto kdamp = torch::tensor({grid.kdamp}, u.options());
    
    // Compute horizontal divergence at mass points
    // WRF: mm(i,j) * (rdx*(u(i+1,k,j)-u(i,k,j)) + rdy*(v(i,k,j+1)-v(i,k,j)))
    
    // dudx: [ny, nz, nx] from u: [ny, nz, nx+1]
    auto dudx = rdx * (u.index({Slice(), Slice(), Slice(1, nx+1)}) - 
                       u.index({Slice(), Slice(), Slice(0, nx)}));
    
    // dvdy: [ny, nz, nx] from v: [ny+1, nz, nx]  
    auto dvdy = rdy * (v.index({Slice(1, ny+1), Slice(), Slice()}) - 
                       v.index({Slice(0, ny), Slice(), Slice()}));
    
    // Apply map scale factors if available
    // FIX 2025-12-28: Use align_msf_3d for unified 2D/3D + device/dtype handling
    // NOTE: For cached version, use DivergenceDamping::applyDamping() instead
    if (grid.msftx.defined() && grid.msftx.numel() > 0) {
        auto opts = u.options().requires_grad(false);
        auto msftx_3d = metric_utils::align_msf_3d(grid.msftx, ny, nz, nx, metric_utils::Stagger::Mass, opts);
        auto msfty_3d = metric_utils::align_msf_3d(grid.msfty, ny, nz, nx, metric_utils::Stagger::Mass, opts);

        // Map factor squared for divergence
        auto mm = msftx_3d * msfty_3d;
        dudx = dudx * mm;
        dvdy = dvdy * mm;
    }
    
    // Compute divergence
    torch::Tensor div = dudx + dvdy;  // [ny, nz, nx]
    
    // Apply external mode filter if specified
    if (grid.damp_mu > 0.0f && mu.defined()) {
        // WRF: div = div - damp_mu * mu_tendency / mu
        // This filters out external (barotropic) mode
        auto mu_2d = mu.index({Slice(), 0, Slice()});  // Extract 2D mu [ny, nx]
        auto mu_3d = mu_2d.unsqueeze(1).expand({ny, nz, nx});
        // Note: mu_tendency would need to be computed separately
        // For now, apply basic filtering
        div = div * (1.0f - grid.damp_mu);
    }
    
    // Compute damping for U-momentum: -kdamp * d(div)/dx
    // At u-points (staggered in x)
    // Interior points only
    auto ddiv_dx = rdx * (div.index({Slice(), Slice(), Slice(1, nx)}) - 
                          div.index({Slice(), Slice(), Slice(0, nx-1)}));
    
    // FIX 2025-12-27: Align device/dtype before slicing and expansion
    // FIX 2025-12-27: Validate 2D/3D layout for msfux/msfuy
    // FIX 2025-12-29 Batch13 Issue 5: Use cache to avoid repeated .to() + slice + expand
    // NOTE: Manual alignment required here (not align_msf_3d) because we need
    // stagger-specific interior slicing: [ny,nx+1] → [ny,nx-1] for divergence damping
    if (grid.msfux.defined() && grid.msfux.numel() > 0) {
        // FIX 2025-12-28: Check msfuy is also defined when using msfux
        TORCH_CHECK(grid.msfuy.defined() && grid.msfuy.numel() > 0,
                    "msfuy must be defined when msfux is used for divergence damping");

        torch::Tensor msfux_3d, msfuy_3d;

        // Check cache first
        if (msf_interior_cache.is_valid(grid.msfux, grid.msfuy, ny, nz, nx, u.options())) {
            // Cache hit: reuse pre-computed tensors
            msfux_3d = msf_interior_cache.msfux_3d;
            msfuy_3d = msf_interior_cache.msfuy_3d;
        } else {
            // Cache miss: compute and update cache
            auto msfux_aligned = grid.msfux.to(u.options());
            auto msfuy_aligned = grid.msfuy.to(u.options());

            if (msfux_aligned.dim() == 2) {
                // 2D (ny, nx+1) → slice → unsqueeze → expand
                // FIX 2025-12-27: Validate staggered dimension (nx+1) for u-point tensors
                TORCH_CHECK(msfux_aligned.size(0) == ny && msfux_aligned.size(1) == nx + 1,
                           "msfux 2D layout mismatch: expected (", ny, ", ", nx + 1, "), got ", msfux_aligned.sizes());
                TORCH_CHECK(msfuy_aligned.size(0) == ny && msfuy_aligned.size(1) == nx + 1,
                           "msfuy 2D layout mismatch: expected (", ny, ", ", nx + 1, "), got ", msfuy_aligned.sizes());
                auto msfux_interior = msfux_aligned.index({Slice(), Slice(1, nx)});
                auto msfuy_interior = msfuy_aligned.index({Slice(), Slice(1, nx)});
                msfux_3d = msfux_interior.unsqueeze(1).expand({ny, nz, nx-1});
                msfuy_3d = msfuy_interior.unsqueeze(1).expand({ny, nz, nx-1});
            } else if (msfux_aligned.dim() == 3) {
                // 3D (ny, nz, nx+1) → slice only
                // FIX 2025-12-27: Validate staggered dimension (nx+1) for u-point tensors
                TORCH_CHECK(msfux_aligned.size(0) == ny && msfux_aligned.size(1) == nz && msfux_aligned.size(2) == nx + 1,
                           "msfux 3D layout mismatch: expected (", ny, ", ", nz, ", ", nx + 1, "), got ", msfux_aligned.sizes());
                TORCH_CHECK(msfuy_aligned.size(0) == ny && msfuy_aligned.size(1) == nz && msfuy_aligned.size(2) == nx + 1,
                           "msfuy 3D layout mismatch: expected (", ny, ", ", nz, ", ", nx + 1, "), got ", msfuy_aligned.sizes());
                msfux_3d = msfux_aligned.index({Slice(), Slice(), Slice(1, nx)});
                msfuy_3d = msfuy_aligned.index({Slice(), Slice(), Slice(1, nx)});
            } else {
                TORCH_CHECK(false, "msfux must be 2D or 3D, got dim=", msfux_aligned.dim());
            }

            // Update cache
            msf_interior_cache.update(grid.msfux, grid.msfuy, ny, nz, nx, u.options(), msfux_3d, msfuy_3d);
        }
        ddiv_dx = ddiv_dx * msfux_3d * msfuy_3d;
    }

    // Apply damping to interior u-points
    // FIX 2025-12-28: Use out-of-place for AD compatibility, in-place for non-AD
    // C2 FIX 2026-02-15: Forward-mode AD dual tensors don't set requires_grad()
    if (u_tend.requires_grad() || wrf::sdirk3::g_sdirk3_config.use_autograd) {
        auto upd = u_tend.index({Slice(), Slice(), Slice(1, nx)}) - kdamp * ddiv_dx;
        u_tend = u_tend.index_put_({Slice(), Slice(), Slice(1, nx)}, upd);
    } else {
        u_tend.index({Slice(), Slice(), Slice(1, nx)}).sub_(kdamp * ddiv_dx);
    }
    
    // Compute damping for V-momentum: -kdamp * d(div)/dy
    // At v-points (staggered in y)
    // Interior points only
    auto ddiv_dy = rdy * (div.index({Slice(1, ny), Slice(), Slice()}) - 
                          div.index({Slice(0, ny-1), Slice(), Slice()}));
    
    // FIX 2025-12-27: Align device/dtype before slicing and expansion
    // FIX 2025-12-27: Validate 2D/3D layout for msfvx/msfvy
    // FIX 2025-12-29 Batch13 Issue 5: Use cache to avoid repeated .to() + slice + expand
    // NOTE: Manual alignment required here (not align_msf_3d) because we need
    // stagger-specific interior slicing: [ny+1,nx] → [ny-1,nx] for divergence damping
    if (grid.msfvx.defined() && grid.msfvx.numel() > 0) {
        // FIX 2025-12-28: Check msfvy is also defined when using msfvx
        TORCH_CHECK(grid.msfvy.defined() && grid.msfvy.numel() > 0,
                    "msfvy must be defined when msfvx is used for divergence damping");

        torch::Tensor msfvx_3d, msfvy_3d;

        // Check cache first
        if (msf_v_interior_cache.is_valid(grid.msfvx, grid.msfvy, ny, nz, nx, v.options())) {
            // Cache hit: reuse pre-computed tensors
            msfvx_3d = msf_v_interior_cache.msfvx_3d;
            msfvy_3d = msf_v_interior_cache.msfvy_3d;
        } else {
            // Cache miss: compute and update cache
            auto msfvx_aligned = grid.msfvx.to(v.options());
            auto msfvy_aligned = grid.msfvy.to(v.options());

            if (msfvx_aligned.dim() == 2) {
                // 2D (ny+1, nx) → slice → unsqueeze → expand
                // FIX 2025-12-27: Validate staggered dimension (ny+1) for v-point tensors
                TORCH_CHECK(msfvx_aligned.size(0) == ny + 1 && msfvx_aligned.size(1) == nx,
                           "msfvx 2D layout mismatch: expected (", ny + 1, ", ", nx, "), got ", msfvx_aligned.sizes());
                TORCH_CHECK(msfvy_aligned.size(0) == ny + 1 && msfvy_aligned.size(1) == nx,
                           "msfvy 2D layout mismatch: expected (", ny + 1, ", ", nx, "), got ", msfvy_aligned.sizes());
                auto msfvx_interior = msfvx_aligned.index({Slice(1, ny), Slice()});
                auto msfvy_interior = msfvy_aligned.index({Slice(1, ny), Slice()});
                msfvx_3d = msfvx_interior.unsqueeze(1).expand({ny-1, nz, nx});
                msfvy_3d = msfvy_interior.unsqueeze(1).expand({ny-1, nz, nx});
            } else if (msfvx_aligned.dim() == 3) {
                // 3D (ny+1, nz, nx) → slice only
                // FIX 2025-12-27: Validate staggered dimension (ny+1) for v-point tensors
                TORCH_CHECK(msfvx_aligned.size(0) == ny + 1 && msfvx_aligned.size(1) == nz && msfvx_aligned.size(2) == nx,
                           "msfvx 3D layout mismatch: expected (", ny + 1, ", ", nz, ", ", nx, "), got ", msfvx_aligned.sizes());
                TORCH_CHECK(msfvy_aligned.size(0) == ny + 1 && msfvy_aligned.size(1) == nz && msfvy_aligned.size(2) == nx,
                           "msfvy 3D layout mismatch: expected (", ny + 1, ", ", nz, ", ", nx, "), got ", msfvy_aligned.sizes());
                msfvx_3d = msfvx_aligned.index({Slice(1, ny), Slice(), Slice()});
                msfvy_3d = msfvy_aligned.index({Slice(1, ny), Slice(), Slice()});
            } else {
                TORCH_CHECK(false, "msfvx must be 2D or 3D, got dim=", msfvx_aligned.dim());
            }

            // Update cache
            msf_v_interior_cache.update(grid.msfvx, grid.msfvy, ny, nz, nx, v.options(), msfvx_3d, msfvy_3d);
        }
        ddiv_dy = ddiv_dy * msfvx_3d * msfvy_3d;
    }
    
    // Apply damping to interior v-points
    // FIX 2025-12-28: Use out-of-place for AD compatibility, in-place for non-AD
    // C2 FIX 2026-02-15: Forward-mode AD dual tensors don't set requires_grad()
    if (v_tend.requires_grad() || wrf::sdirk3::g_sdirk3_config.use_autograd) {
        auto upd = v_tend.index({Slice(1, ny), Slice(), Slice()}) - kdamp * ddiv_dy;
        v_tend = v_tend.index_put_({Slice(1, ny), Slice(), Slice()}, upd);
    } else {
        v_tend.index({Slice(1, ny), Slice(), Slice()}).sub_(kdamp * ddiv_dy);
    }
}

void compute6thOrderDiffusionWRF(torch::Tensor& tend,
                               const torch::Tensor& var,
                               const WRFGridInfoExtended& grid,
                               bool is_momentum) {
    // TODO: Implement 6th order diffusion following WRF
}

void computeSmagorinskyCoefficientsWRF(torch::Tensor& xkmh,
                                     torch::Tensor& xkmv,
                                     torch::Tensor& xkhh,
                                     torch::Tensor& xkhv,
                                     const torch::Tensor& defor11,
                                     const torch::Tensor& defor22,
                                     const torch::Tensor& defor33,
                                     const torch::Tensor& defor12,
                                     const torch::Tensor& defor13,
                                     const torch::Tensor& defor23,
                                     const torch::Tensor& BN2,
                                     const WRFGridInfoExtended& grid) {
    // WRF Smagorinsky turbulence model implementation
    // Reference: module_diffusion_em.F lines 1850-1920

    // FIX 2025-01-11 Round27: Validate primary input tensor (defor11 used for dimensions)
    TORCH_CHECK(defor11.defined() && defor11.numel() > 0,
        "computeSmagorinskyCoefficientsWRF: defor11 must be defined and non-empty");
    TORCH_CHECK(defor11.dim() == 3,
        "computeSmagorinskyCoefficientsWRF: defor11 must be 3D [ny,nz,nx], got dim=", defor11.dim());

    const int ny = defor11.size(0);
    const int nz = defor11.size(1);
    const int nx = defor11.size(2);

    // FIX 2025-01-11 Round27: Validate all deformation inputs share shape/device/dtype
    auto ref_device = defor11.device();
    auto ref_dtype = defor11.scalar_type();
    auto validate_tensor = [&](const torch::Tensor& t, const char* name, bool allow_staggered = false) {
        if (!t.defined() || t.numel() == 0) return;  // Optional tensors OK if undefined
        TORCH_CHECK(t.dim() == 3,
            "computeSmagorinskyCoefficientsWRF: ", name, " must be 3D, got dim=", t.dim());
        TORCH_CHECK(t.device() == ref_device,
            "computeSmagorinskyCoefficientsWRF: ", name, " device mismatch (",
            t.device().str(), " vs ", ref_device.str(), ")");
        // Allow staggered tensors to have +1 in vertical dimension
        if (allow_staggered) {
            TORCH_CHECK(t.size(0) == ny || t.size(0) == ny + 1,
                "computeSmagorinskyCoefficientsWRF: ", name, " ny mismatch (", t.size(0), " vs ", ny, ")");
            TORCH_CHECK(t.size(1) == nz || t.size(1) == nz + 1,
                "computeSmagorinskyCoefficientsWRF: ", name, " nz mismatch (", t.size(1), " vs ", nz, ")");
            TORCH_CHECK(t.size(2) == nx || t.size(2) == nx + 1,
                "computeSmagorinskyCoefficientsWRF: ", name, " nx mismatch (", t.size(2), " vs ", nx, ")");
        } else {
            TORCH_CHECK(t.size(0) == ny && t.size(1) == nz && t.size(2) == nx,
                "computeSmagorinskyCoefficientsWRF: ", name, " shape mismatch [",
                t.size(0), ",", t.size(1), ",", t.size(2), "] vs [", ny, ",", nz, ",", nx, "]");
        }
    };
    // Mass-point deformation tensors (D11, D22, D33)
    validate_tensor(defor22, "defor22");
    validate_tensor(defor33, "defor33");
    // Vorticity/staggered deformation tensors (D12, D13, D23) - allow staggered
    validate_tensor(defor12, "defor12", /*allow_staggered=*/true);
    validate_tensor(defor13, "defor13", /*allow_staggered=*/true);
    validate_tensor(defor23, "defor23", /*allow_staggered=*/true);
    validate_tensor(BN2, "BN2");

    // FIX 2025-01-11 Round27: Dtype consistency check
    // FIX 2025-01-11 Round29: Track dtype mismatches and cast to compute_opts to prevent runtime errors
    // Mixed FP32/FP64 inputs could cause dtype mismatch in def2.add_() without casting
    bool has_dtype_mismatch = false;
    auto check_dtype = [&](const torch::Tensor& t, const char* name) {
        if (!t.defined() || t.numel() == 0) return;
        auto t_dtype = t.scalar_type();
        // Allow half-precision (will be upcasted) or matching dtype
        if (t_dtype != torch::kHalf && t_dtype != torch::kBFloat16 && t_dtype != ref_dtype) {
            TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: ", name, " dtype (",
                torch::toString(t_dtype), ") differs from defor11 (",
                torch::toString(ref_dtype), "). Will cast to compute dtype.");
            has_dtype_mismatch = true;
        }
    };
    check_dtype(defor22, "defor22");
    check_dtype(defor33, "defor33");
    check_dtype(defor12, "defor12");
    check_dtype(defor13, "defor13");
    check_dtype(defor23, "defor23");
    check_dtype(BN2, "BN2");

    // FIX 2025-01-11 Round27: Check ALL inputs for FP16/BF16, not just defor11
    // Any half-precision input should trigger upcast for numerical stability
    auto is_half_dtype = [](const torch::Tensor& t) {
        if (!t.defined()) return false;
        auto dtype = t.scalar_type();
        return (dtype == torch::kHalf || dtype == torch::kBFloat16);
    };
    const bool needs_upcast = is_half_dtype(defor11) || is_half_dtype(defor22) ||
                              is_half_dtype(defor33) || is_half_dtype(defor12) ||
                              is_half_dtype(defor13) || is_half_dtype(defor23) ||
                              is_half_dtype(BN2);

    // FIX 2025-01-11 Round34 Issue 2: Use max dtype across ALL inputs, not just defor11
    // If defor11 is FP32 but other inputs are FP64, we should compute in FP64
    auto is_fp64 = [](const torch::Tensor& t) {
        return t.defined() && t.scalar_type() == torch::kFloat64;
    };
    const bool has_fp64 = is_fp64(defor11) || is_fp64(defor22) || is_fp64(defor33) ||
                          is_fp64(defor12) || is_fp64(defor13) || is_fp64(defor23) ||
                          is_fp64(BN2);
    // OPT Pass34: Save input_dtype before upcast for final downcast
    const auto input_dtype = defor11.scalar_type();
    auto compute_opts = defor11.options();
    if (needs_upcast) {
        // Half inputs: upcast to FP32, but if FP64 is also present, use FP64
        compute_opts = compute_opts.dtype(has_fp64 ? torch::kFloat64 : torch::kFloat32);
    } else if (has_fp64) {
        // No half inputs but FP64 present: use FP64 for precision
        compute_opts = compute_opts.dtype(torch::kFloat64);
    }
    // else: keep defor11's dtype (FP32)

    // FIX 2025-01-11 Round27: Validate k-dim sizes for D13/D23 to prevent out-of-bounds access
    // D13 is at U-W points [ny, nz+1, nx+1], D23 is at V-W points [ny+1, nz+1, nx]
    if (defor13.defined() && defor13.numel() > 0 && nz > 1) {
        TORCH_CHECK(defor13.size(1) >= nz + 1,
            "computeSmagorinskyCoefficientsWRF: defor13 k-dim size (", defor13.size(1),
            ") must be >= nz+1 (", nz + 1, ") for k+1 indexing");
    }
    if (defor23.defined() && defor23.numel() > 0 && nz > 1) {
        TORCH_CHECK(defor23.size(1) >= nz + 1,
            "computeSmagorinskyCoefficientsWRF: defor23 k-dim size (", defor23.size(1),
            ") must be >= nz+1 (", nz + 1, ") for k+1 indexing");
    }

    // Smagorinsky coefficient (typically 0.25)
    const float c_s = grid.c_s > 0 ? grid.c_s : 0.25f;
    const float c_s_squared = c_s * c_s;

    // Prandtl number for heat diffusion
    const float pr = 1.0f / 3.0f;

    // FIX 2025-12-27: Cache safe_mean(grid.dnw) to avoid redundant computation
    // FIX 2025-12-29 Batch11 Issue 2: Use DnwMeanCache to avoid D2H sync on GPU per-call
    // FIX 2025-12-30 Batch14 Issue 4: Pass grid dims for proper 1D extraction before mean
    // FIX 2025-01-10 Round24: Use double for precision preservation when dnw is FP64
    // FIX 2025-01-11 Round27: Select appropriate kdim for dnw (mass-level vs w-level)
    // dnw is typically nz elements (mass-level), but may be nz+1 (w-level) in some configs
    // FIX 2025-01-11 Round28: Pass ny/nx for parity-based cubic/halo detection
    const int64_t dnw_kdim = selectDnwKdim(grid.dnw, nz, ny, nx, grid.rdz_kdim, grid.rdzw_kdim);
    // FIX 2025-01-11 Round30 Issue 6: When dnw is w-level (nz+1 elements), pass nz+1 to extraction
    // so all vertical levels are included in the mean. selectDnwKdim returns rdzw_kdim for w-level.
    const bool dnw_is_w_level = (dnw_kdim == grid.rdzw_kdim);
    const int64_t dnw_extract_nz = dnw_is_w_level ? (nz + 1) : nz;
    // The cache uses epoch + data_ptr to detect when dnw changes (moving_nest, regrid)
    // FIX 2025-01-11 Round32 Issue 3 NOTE: When dnw is w-level, cached_dnw_mean is mean of nz+1
    // w-level values, not mean of nz mass-level values. For typical grids, these are very similar.
    // The 3D case (smagorinsky_opt==2) properly uses mass-averaged mlen_v, so this only affects
    // the scalar approximation case. For strict consistency, mass-averaging could be added to cache.
    const double cached_dnw_mean = dnw_mean_cache.get_or_compute(grid.dnw, dnw_extract_nz, ny, nx, dnw_kdim);

    // Initialize output tensors
    // FIX 2025-01-10 Round26: Use compute_opts (FP32 for half inputs) for intermediate computation
    xkmh = torch::zeros({ny, nz, nx}, compute_opts);
    xkmv = torch::zeros({ny, nz, nx}, compute_opts);
    xkhh = torch::zeros({ny, nz, nx}, compute_opts);
    xkhv = torch::zeros({ny, nz, nx}, compute_opts);

    // FIX 2025-01-10 Round26: Upcast inputs to compute dtype if needed for numerical stability
    // FIX 2025-01-11 Round29: Also cast when dtype mismatch to prevent def2.add_() runtime errors
    const bool needs_cast = needs_upcast || has_dtype_mismatch;
    const auto compute_dtype = compute_opts.dtype().toScalarType();
    auto cast_to_compute = [&](const torch::Tensor& t) -> torch::Tensor {
        if (!t.defined()) return t;
        if (t.scalar_type() == compute_dtype) return t;
        return t.to(compute_dtype);
    };
    torch::Tensor d11_work = needs_cast ? cast_to_compute(defor11) : defor11;
    torch::Tensor d22_work = needs_cast ? cast_to_compute(defor22) : defor22;
    torch::Tensor d33_work = needs_cast ? cast_to_compute(defor33) : defor33;

    // Compute def2 = 0.5*(D11^2 + D22^2 + D33^2) + D12^2 + D13^2 + D23^2
    // WRF formula from module_diffusion_em.F line 1853
    torch::Tensor def2 = 0.5f * (d11_work * d11_work +
                                  d22_work * d22_work +
                                  d33_work * d33_work);
    
    // Average D12 to mass points and add contribution
    if (defor12.defined() && defor12.numel() > 0) {
        // FIX 2025-01-11 Round30: Add shape validation and center-slice for D12 halo handling
        // D12 at vorticity points [ny+1, nz, nx+1] - need stagger for j+1/i+1 indexing
        // FIX 2025-01-11 Round32 Issue 1: Add k-dim validation and center-slice
        const bool d12_has_j_stagger = (defor12.size(0) >= ny + 1);
        const bool d12_has_k_size = (defor12.size(1) >= nz);
        const bool d12_has_i_stagger = (defor12.size(2) >= nx + 1);

        // FIX 2025-01-10 Round26: Use compute_opts for intermediate tensor
        torch::Tensor d12_work = needs_cast ? cast_to_compute(defor12) : defor12;
        torch::Tensor D12_mass = torch::zeros({ny, nz, nx}, compute_opts);

        if (d12_has_j_stagger && d12_has_k_size && d12_has_i_stagger) {
            // FIX 2025-01-11 Round30: Center-slice for symmetric halo handling
            // FIX 2025-01-11 Round32 Issue 1: Include k-dim in halo calculation
            const int64_t diff_j = d12_work.size(0) - (ny + 1);
            const int64_t diff_k = d12_work.size(1) - nz;
            const int64_t diff_i = d12_work.size(2) - (nx + 1);
            // Warn on odd halo
            const bool has_odd_halo = (diff_j > 0 && diff_j % 2 != 0) ||
                                      (diff_k > 0 && diff_k % 2 != 0) ||
                                      (diff_i > 0 && diff_i % 2 != 0);
            if (has_odd_halo) {
                TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor12 has odd halo "
                    "(shape=[", d12_work.size(0), ",", d12_work.size(1), ",", d12_work.size(2),
                    "], expected=[", ny+1, ",", nz, ",", nx+1, "]); using floor(halo/2) offset");
                // FIX 2025-01-11 Round32: Use floor(halo/2) offset for best centering
                const int64_t halo_j_odd = diff_j / 2;
                const int64_t halo_k_odd = diff_k / 2;
                const int64_t halo_i_odd = diff_i / 2;
                auto d12_c = d12_work.slice(0, halo_j_odd, halo_j_odd + ny + 1)
                                     .slice(1, halo_k_odd, halo_k_odd + nz)
                                     .slice(2, halo_i_odd, halo_i_odd + nx + 1);
                D12_mass.slice(0, 0, ny-1).slice(2, 0, nx-1).copy_(
                    0.25f * (d12_c.slice(0, 0, ny-1).slice(2, 0, nx-1) +
                             d12_c.slice(0, 1, ny).slice(2, 0, nx-1) +
                             d12_c.slice(0, 0, ny-1).slice(2, 1, nx) +
                             d12_c.slice(0, 1, ny).slice(2, 1, nx)));
            } else {
                const int64_t halo_j = diff_j / 2;
                const int64_t halo_k = diff_k / 2;
                const int64_t halo_i = diff_i / 2;
                // Center-sliced D12 averaging with k-dim
                auto d12_c = d12_work.slice(0, halo_j, halo_j + ny + 1)
                                     .slice(1, halo_k, halo_k + nz)
                                     .slice(2, halo_i, halo_i + nx + 1);
                D12_mass.slice(0, 0, ny-1).slice(2, 0, nx-1).copy_(
                    0.25f * (d12_c.slice(0, 0, ny-1).slice(2, 0, nx-1) +
                             d12_c.slice(0, 1, ny).slice(2, 0, nx-1) +
                             d12_c.slice(0, 0, ny-1).slice(2, 1, nx) +
                             d12_c.slice(0, 1, ny).slice(2, 1, nx)));
            }
        } else {
            // Fallback: D12 at mass points or insufficient stagger - use directly
            TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor12 shape [", defor12.size(0),
                ",", defor12.size(1), ",", defor12.size(2), "] lacks vorticity stagger; using fallback");
            // Copy what we can from d12_work
            int64_t copy_j = std::min(d12_work.size(0), static_cast<int64_t>(ny));
            int64_t copy_i = std::min(d12_work.size(2), static_cast<int64_t>(nx));
            D12_mass.slice(0, 0, copy_j).slice(2, 0, copy_i).copy_(
                d12_work.slice(0, 0, copy_j).slice(2, 0, copy_i));
        }
        def2 = def2 + D12_mass * D12_mass;
    }
    
    // Average D13 to mass points and add contribution
    if (defor13.defined() && defor13.numel() > 0 && nz > 1) {
        // FIX 2025-01-11 Round27: Strict shape validation for vectorized path
        // FIX 2025-01-11 Round30: Added size(0) >= ny check to prevent negative slice
        // D13 U-W staggering requires [ny, nz+1, nx+1] for safe k+1 and i+1 indexing
        const bool d13_has_j_size = (defor13.size(0) >= ny);
        const bool d13_has_w_stagger = (defor13.size(1) >= nz + 1);
        const bool d13_has_u_stagger = (defor13.size(2) >= nx + 1);

        torch::Tensor d13_work = needs_cast ? cast_to_compute(defor13) : defor13;

        if (d13_has_j_size && d13_has_w_stagger && d13_has_u_stagger) {
            // Vectorized path: D13 is at U-W points [ny, nz+1, nx+1], average to mass points
            // FIX 2025-01-11 Round28: Center-slice for symmetric halo handling
            // FIX 2025-01-11 Round29: Add halo_j for complete halo handling on all axes
            // Compute halo offsets: if input is larger than expected, assume symmetric halo
            const int64_t diff_j = d13_work.size(0) - ny;
            const int64_t diff_k = d13_work.size(1) - (nz + 1);
            const int64_t diff_i = d13_work.size(2) - (nx + 1);
            // FIX 2025-01-11 Round29: Warn on odd halo (asymmetric) which silently offsets
            // FIX 2025-01-11 Round30: Fallback instead of just warning for odd halo
            const bool has_odd_halo = (diff_j > 0 && diff_j % 2 != 0) ||
                                      (diff_k > 0 && diff_k % 2 != 0) ||
                                      (diff_i > 0 && diff_i % 2 != 0);
            if (has_odd_halo) {
                // FIX 2025-01-11 Round31: Use floor(diff/2) offset for best centering even with odd halo
                // This is better than front-slice (offset=0) which has large boundary bias for big halos
                TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor13 has odd halo "
                    "(shape=[", d13_work.size(0), ",", d13_work.size(1), ",", d13_work.size(2),
                    "], expected=[", ny, ",", nz+1, ",", nx+1, "]); using floor(halo/2) offset");
                const int64_t halo_j_odd = diff_j / 2;  // floor division for best centering
                const int64_t halo_k_odd = diff_k / 2;
                const int64_t halo_i_odd = diff_i / 2;
                auto d13_j = d13_work.slice(0, halo_j_odd, halo_j_odd + ny);
                auto d13_k0_i0 = d13_j.slice(1, halo_k_odd, halo_k_odd+nz).slice(2, halo_i_odd, halo_i_odd+nx);
                auto d13_k1_i0 = d13_j.slice(1, halo_k_odd+1, halo_k_odd+nz+1).slice(2, halo_i_odd, halo_i_odd+nx);
                auto d13_k0_i1 = d13_j.slice(1, halo_k_odd, halo_k_odd+nz).slice(2, halo_i_odd+1, halo_i_odd+nx+1);
                auto d13_k1_i1 = d13_j.slice(1, halo_k_odd+1, halo_k_odd+nz+1).slice(2, halo_i_odd+1, halo_i_odd+nx+1);
                auto D13_interior = 0.25f * (d13_k0_i0 + d13_k1_i0 + d13_k0_i1 + d13_k1_i1);
                def2.slice(2, 0, nx-1).add_(D13_interior.slice(2, 0, nx-1).square());
            } else {
                const int64_t halo_j = diff_j / 2;
                const int64_t halo_k = diff_k / 2;
                const int64_t halo_i = diff_i / 2;
                const int64_t j0 = halo_j;
                const int64_t j1 = halo_j + ny;       // exclusive end for ny elements
                const int64_t k0 = halo_k;
                const int64_t k1 = halo_k + nz + 1;   // exclusive end for nz+1 elements
                const int64_t i0 = halo_i;
                const int64_t i1 = halo_i + nx + 1;   // exclusive end for nx+1 elements
                // Apply j-slice first, then k and i slices
                auto d13_j = d13_work.slice(0, j0, j1);  // [ny, ...]
                auto d13_k0_i0 = d13_j.slice(1, k0, k0+nz).slice(2, i0, i0+nx);      // k=0..nz-1, i=0..nx-1
                auto d13_k1_i0 = d13_j.slice(1, k0+1, k1).slice(2, i0, i0+nx);       // k=1..nz,   i=0..nx-1
                auto d13_k0_i1 = d13_j.slice(1, k0, k0+nz).slice(2, i0+1, i1);       // k=0..nz-1, i=1..nx
                auto d13_k1_i1 = d13_j.slice(1, k0+1, k1).slice(2, i0+1, i1);        // k=1..nz,   i=1..nx
                auto D13_interior = 0.25f * (d13_k0_i0 + d13_k1_i0 + d13_k0_i1 + d13_k1_i1);
                def2.slice(2, 0, nx-1).add_(D13_interior.slice(2, 0, nx-1).square());
            }
        } else {
            // Fallback: D13 at mass points [ny, nz, nx], use directly
            // This handles legacy or non-staggered deformation tensor inputs
            TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor13 shape [", defor13.size(0),
                ",", defor13.size(1), ",", defor13.size(2), "] lacks U-W stagger; using fallback averaging");
            torch::Tensor D13_mass = torch::zeros({ny, nz, nx}, compute_opts);
            D13_mass.slice(2, 0, nx-1).copy_(d13_work.slice(2, 0, nx-1));
            def2 = def2 + D13_mass * D13_mass;
        }
    }
    
    // Average D23 to mass points and add contribution
    if (defor23.defined() && defor23.numel() > 0 && nz > 1) {
        // FIX 2025-01-11 Round27: Strict shape validation for vectorized path
        // FIX 2025-01-11 Round30: Added size(2) >= nx check to prevent negative slice
        // D23 V-W staggering requires [ny+1, nz+1, nx] for safe j+1 and k+1 indexing
        const bool d23_has_v_stagger = (defor23.size(0) >= ny + 1);
        const bool d23_has_w_stagger = (defor23.size(1) >= nz + 1);
        const bool d23_has_i_size = (defor23.size(2) >= nx);

        torch::Tensor d23_work = needs_cast ? cast_to_compute(defor23) : defor23;

        if (d23_has_v_stagger && d23_has_w_stagger && d23_has_i_size) {
            // Vectorized path: D23 is at V-W points [ny+1, nz+1, nx], average to mass points
            // FIX 2025-01-11 Round28: Center-slice for symmetric halo handling
            // FIX 2025-01-11 Round29: Add halo_i for complete halo handling on all axes
            // Compute halo offsets: if input is larger than expected, assume symmetric halo
            const int64_t diff_j = d23_work.size(0) - (ny + 1);
            const int64_t diff_k = d23_work.size(1) - (nz + 1);
            const int64_t diff_i = d23_work.size(2) - nx;
            // FIX 2025-01-11 Round29: Warn on odd halo (asymmetric) which silently offsets
            // FIX 2025-01-11 Round30: Fallback instead of just warning for odd halo
            const bool has_odd_halo = (diff_j > 0 && diff_j % 2 != 0) ||
                                      (diff_k > 0 && diff_k % 2 != 0) ||
                                      (diff_i > 0 && diff_i % 2 != 0);
            if (has_odd_halo) {
                // FIX 2025-01-11 Round31: Use floor(diff/2) offset for best centering even with odd halo
                // This is better than front-slice (offset=0) which has large boundary bias for big halos
                TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor23 has odd halo "
                    "(shape=[", d23_work.size(0), ",", d23_work.size(1), ",", d23_work.size(2),
                    "], expected=[", ny+1, ",", nz+1, ",", nx, "]); using floor(halo/2) offset");
                const int64_t halo_j_odd = diff_j / 2;
                const int64_t halo_k_odd = diff_k / 2;
                const int64_t halo_i_odd = diff_i / 2;
                auto d23_i = d23_work.slice(2, halo_i_odd, halo_i_odd + nx);  // [..., nx]
                auto d23_j0_k0 = d23_i.slice(0, halo_j_odd, halo_j_odd+ny-1).slice(1, halo_k_odd, halo_k_odd+nz);
                auto d23_j1_k0 = d23_i.slice(0, halo_j_odd+1, halo_j_odd+ny).slice(1, halo_k_odd, halo_k_odd+nz);
                auto d23_j0_k1 = d23_i.slice(0, halo_j_odd, halo_j_odd+ny-1).slice(1, halo_k_odd+1, halo_k_odd+nz+1);
                auto d23_j1_k1 = d23_i.slice(0, halo_j_odd+1, halo_j_odd+ny).slice(1, halo_k_odd+1, halo_k_odd+nz+1);
                auto D23_interior = 0.25f * (d23_j0_k0 + d23_j1_k0 + d23_j0_k1 + d23_j1_k1);
                def2.slice(0, 0, ny-1).add_(D23_interior.square());
            } else {
                const int64_t halo_j = diff_j / 2;
                const int64_t halo_k = diff_k / 2;
                const int64_t halo_i = diff_i / 2;
                const int64_t j0 = halo_j;
                [[maybe_unused]] const int64_t j1 = halo_j + ny + 1;  // exclusive end for ny+1 elements
                const int64_t k0 = halo_k;
                const int64_t k1 = halo_k + nz + 1;  // exclusive end for nz+1 elements
                const int64_t i0 = halo_i;
                const int64_t i1 = halo_i + nx;      // exclusive end for nx elements
                // Apply i-slice first for consistent halo handling
                auto d23_i = d23_work.slice(2, i0, i1);  // [..., nx]
                // d23_j0: d23[j0:j0+ny-1, :, :], d23_j1: d23[j0+1:j0+ny, :, :]
                // d23_k0: d23[:, k0:k0+nz, :],   d23_k1: d23[:, k0+1:k0+nz+1, :]
                auto d23_j0_k0 = d23_i.slice(0, j0, j0+ny-1).slice(1, k0, k0+nz);      // j=0..ny-2, k=0..nz-1
                auto d23_j1_k0 = d23_i.slice(0, j0+1, j0+ny).slice(1, k0, k0+nz);      // j=1..ny-1, k=0..nz-1
                auto d23_j0_k1 = d23_i.slice(0, j0, j0+ny-1).slice(1, k0+1, k1);       // j=0..ny-2, k=1..nz
                auto d23_j1_k1 = d23_i.slice(0, j0+1, j0+ny).slice(1, k0+1, k1);       // j=1..ny-1, k=1..nz
                auto D23_interior = 0.25f * (d23_j0_k0 + d23_j1_k0 + d23_j0_k1 + d23_j1_k1);
                def2.slice(0, 0, ny-1).add_(D23_interior.square());
            }
        } else {
            // Fallback: D23 at mass points [ny, nz, nx], use directly
            // This handles legacy or non-staggered deformation tensor inputs
            TORCH_WARN_ONCE("computeSmagorinskyCoefficientsWRF: defor23 shape [", defor23.size(0),
                ",", defor23.size(1), ",", defor23.size(2), "] lacks V-W stagger; using fallback averaging");
            torch::Tensor D23_mass = torch::zeros({ny, nz, nx}, compute_opts);
            D23_mass.slice(0, 0, ny-1).copy_(d23_work.slice(0, 0, ny-1));
            def2 = def2 + D23_mass * D23_mass;
        }
    }
    
    // Compute mixing length scales
    // FIX 2025-01-10 Round26: Use compute_opts for mixing length tensors
    // FIX 2025-01-11 Round27: Use cached 0D tensors to avoid repeated allocation
    torch::Tensor mlen_h = mlen_cache.get_mlen_h(static_cast<double>(grid.dx), compute_opts);

    // Vertical mixing length - use average dnw if available, otherwise use a default
    // NOTE: Using cached_dnw_mean computed at function entry
    // FIX 2025-01-11 Round27: Use cached 0D tensor (addresses TODO from Round25)
    torch::Tensor mlen_v = mlen_cache.get_mlen_v_scalar(cached_dnw_mean, compute_opts);

    // Expand scalar mixing lengths if needed for 3D turbulence
    if (grid.smagorinsky_opt == 2 && grid.dnw.defined()) {
        // Use level-dependent mixing length for 3D Smagorinsky
        // FIX 2025-12-27: Use extract1DVerticalMetric instead of loop with [] (no-op in C++ PyTorch)
        // Also handles 2D/3D grid.dnw and ensures device/dtype alignment
        // FIX 2025-01-11 Round27: Use dnw_kdim computed at function entry (mass vs w-level aware)
        // FIX 2025-01-11 Round32 Issue 2: Apply mass-average for w-level dnw
        // W-level has nz+1 values at interfaces; convert to nz mass-level values
        auto dnw_1d_raw = metric_utils::extract1DVerticalMetric(grid.dnw, dnw_extract_nz, ny, nx, dnw_kdim);
        torch::Tensor dnw_1d;
        if (dnw_is_w_level && dnw_1d_raw.size(0) > 1) {
            // Mass-level value at k = 0.5 * (w-level[k] + w-level[k+1])
            dnw_1d = 0.5f * (dnw_1d_raw.slice(0, 0, -1) + dnw_1d_raw.slice(0, 1));
        } else {
            dnw_1d = dnw_1d_raw;
        }
        mlen_v = dnw_1d.to(compute_opts);
        mlen_v = mlen_v.unsqueeze(0).unsqueeze(2).expand({ny, nz, nx});
    }

    // Apply stability correction if Brunt-Vaisala frequency is provided
    torch::Tensor stability_corrected_def2 = def2;
    if (BN2.defined() && BN2.numel() > 0) {
        // Richardson number correction: def2_corrected = max(0, def2 - BN2/Pr)
        // FIX 2025-01-10 Round26: Upcast BN2 if needed for numerical consistency
        torch::Tensor bn2_work = needs_cast ? cast_to_compute(BN2) : BN2;
        stability_corrected_def2 = torch::maximum(
            torch::zeros_like(def2),
            def2 - bn2_work / pr
        );
    }
    
    // Compute turbulent viscosity coefficients
    // xkmh = c_s^2 * mlen_h^2 * sqrt(def2)
    // xkmv = c_s^2 * mlen_v^2 * sqrt(def2)
    torch::Tensor sqrt_def2 = torch::sqrt(stability_corrected_def2);
    
    // Horizontal momentum diffusivity
    xkmh = c_s_squared * mlen_h * mlen_h * sqrt_def2;
    
    // Vertical momentum diffusivity
    if (mlen_v.dim() == 3) {
        xkmv = c_s_squared * mlen_v * mlen_v * sqrt_def2;
    } else {
        xkmv = c_s_squared * mlen_v * mlen_v * sqrt_def2;
    }
    
    // Add minimum diffusivity for numerical stability
    // FIX 2025-01-10 Round26: Use double precision for min_diff_h computation (grid.dx may be large)
    // FIX 2025-01-11 Round27: Create scalar_tensor with appropriate precision for compute dtype
    // When compute_opts is FP64, use double constants to avoid float truncation
    const double min_diff_h_dbl = 1.0e-6 * static_cast<double>(grid.dx) * static_cast<double>(grid.dx);
    const double min_diff_v_dbl = 1.0e-6 * cached_dnw_mean * cached_dnw_mean;

    const bool use_double = (compute_opts.dtype() == torch::kFloat64);
    torch::Tensor min_diff_h_t = use_double
        ? torch::scalar_tensor(min_diff_h_dbl, compute_opts)
        : torch::scalar_tensor(static_cast<float>(min_diff_h_dbl), compute_opts);
    torch::Tensor min_diff_v_t = use_double
        ? torch::scalar_tensor(min_diff_v_dbl, compute_opts)
        : torch::scalar_tensor(static_cast<float>(min_diff_v_dbl), compute_opts);

    xkmh = torch::maximum(xkmh, min_diff_h_t);
    xkmv = torch::maximum(xkmv, min_diff_v_t);
    
    // Heat diffusivity (using inverse Prandtl number)
    xkhh = xkmh / pr;
    xkhv = xkmv / pr;

    // FIX 2025-01-10 Round26: Downcast outputs back to original dtype if upcast was used
    if (needs_upcast) {
        xkmh = xkmh.to(input_dtype);
        xkmv = xkmv.to(input_dtype);
        xkhh = xkhh.to(input_dtype);
        xkhv = xkhv.to(input_dtype);
    }
}

void applySmagorinskyTurbulenceWRF(torch::Tensor& u_tend,
                                 torch::Tensor& v_tend,
                                 torch::Tensor& w_tend,
                                 const torch::Tensor& u,
                                 const torch::Tensor& v,
                                 const torch::Tensor& w,
                                 const torch::Tensor& xkmh,
                                 const torch::Tensor& xkmv,
                                 const torch::Tensor& defor11,
                                 const torch::Tensor& defor22,
                                 const torch::Tensor& defor33,
                                 const torch::Tensor& defor12,
                                 const torch::Tensor& defor13,
                                 const torch::Tensor& defor23,
                                 const torch::Tensor& rho,
                                 const WRFGridInfoExtended& grid) {
    // Apply Smagorinsky turbulent stresses to momentum equations
    // Reference: WRF module_diffusion_em.F

    // FIX 2025-01-11 Round27: Validate primary input tensors
    TORCH_CHECK(u_tend.defined() && u_tend.numel() > 0,
        "applySmagorinskyTurbulenceWRF: u_tend must be defined and non-empty");
    TORCH_CHECK(u_tend.dim() == 3,
        "applySmagorinskyTurbulenceWRF: u_tend must be 3D [ny,nz,nx_u], got dim=", u_tend.dim());
    TORCH_CHECK(u.defined() && v.defined() && w.defined(),
        "applySmagorinskyTurbulenceWRF: u, v, w must all be defined");
    TORCH_CHECK(xkmh.defined() && xkmv.defined(),
        "applySmagorinskyTurbulenceWRF: xkmh and xkmv (diffusion coefficients) must be defined");
    TORCH_CHECK(rho.defined() && rho.numel() > 0,
        "applySmagorinskyTurbulenceWRF: rho (density) must be defined and non-empty");

    const int ny = u_tend.size(0);
    const int nz = u_tend.size(1);
    const int nx_u = u_tend.size(2);
    const int nx = nx_u - 1;

    // FIX 2025-01-11 Round27: Additional shape validation for staggered grids
    auto ref_device = u_tend.device();
    auto ref_dtype = u.scalar_type();
    TORCH_CHECK(u.device() == ref_device && v.device() == ref_device && w.device() == ref_device,
        "applySmagorinskyTurbulenceWRF: u, v, w must be on same device as u_tend");
    TORCH_CHECK(xkmh.device() == ref_device && xkmv.device() == ref_device,
        "applySmagorinskyTurbulenceWRF: xkmh, xkmv must be on same device as u_tend");
    TORCH_CHECK(rho.device() == ref_device,
        "applySmagorinskyTurbulenceWRF: rho must be on same device as u_tend");

    // FIX 2025-01-11 Round27: Dtype consistency check for numerical stability
    // Mixed FP32/FP64 inputs could cause implicit promotion in vectorized path
    auto warn_dtype_mismatch = [&](const torch::Tensor& t, const char* name) {
        if (!t.defined() || t.numel() == 0) return;
        auto t_dtype = t.scalar_type();
        if (t_dtype != ref_dtype) {
            TORCH_WARN_ONCE("applySmagorinskyTurbulenceWRF: ", name, " dtype (",
                torch::toString(t_dtype), ") differs from u (",
                torch::toString(ref_dtype), "). Mixed precision may cause implicit promotion.");
        }
    };
    warn_dtype_mismatch(v, "v");
    warn_dtype_mismatch(w, "w");
    warn_dtype_mismatch(xkmh, "xkmh");
    warn_dtype_mismatch(xkmv, "xkmv");
    warn_dtype_mismatch(defor11, "defor11");
    warn_dtype_mismatch(defor22, "defor22");
    warn_dtype_mismatch(defor33, "defor33");
    warn_dtype_mismatch(defor12, "defor12");
    warn_dtype_mismatch(defor13, "defor13");
    warn_dtype_mismatch(defor23, "defor23");
    warn_dtype_mismatch(rho, "rho");

    // FIX 2025-01-11 Round27: Use double precision for rdx/rdy when input is FP64
    // Compute in double precision for loop path - avoids truncation when multiplying FP64 tensors
    const double rdx_d = 1.0 / static_cast<double>(grid.dx);
    const double rdy_d = 1.0 / static_cast<double>(grid.dy);
    // FIX 2025-01-11 Round28 Issue 8: Add float versions to avoid double promotion on FP32/FP16/BF16
    // Using double scalar on FP32 tensor promotes to double (performance loss)
    // Using float scalar on FP32 tensor keeps FP32 (optimal)
    const float rdx_f = 1.0f / grid.dx;
    const float rdy_f = 1.0f / grid.dy;
    // FIX 2025-01-11 Round32 Issue 4: Check ALL inputs for max dtype, not just u
    // If any input is FP64, use double to avoid precision loss in intermediate computations
    auto is_fp64 = [](const torch::Tensor& t) {
        return t.defined() && t.scalar_type() == torch::kFloat64;
    };
    const bool use_double_rdx = is_fp64(u) || is_fp64(v) || is_fp64(w) ||
                                is_fp64(xkmh) || is_fp64(xkmv) || is_fp64(rho) ||
                                is_fp64(defor11) || is_fp64(defor22) || is_fp64(defor33) ||
                                is_fp64(defor12) || is_fp64(defor13) || is_fp64(defor23);

    // FIX 2025-01-11 Round27: Tensor scalar versions for vectorized path - use cached 0D tensors
    // Avoids repeated scalar_tensor() allocation per call when grid.dx/dy unchanged
    // FIX 2025-01-11 Round34 Issue 4: Use max input dtype for cache key (not just u_tend)
    // If use_double_rdx is true, vectorized path should also use FP64 tensors
    auto metric_opts = u_tend.options().requires_grad(false);
    if (use_double_rdx && metric_opts.dtype() != torch::kFloat64) {
        metric_opts = metric_opts.dtype(torch::kFloat64);
    }
    auto [rdx_t, rdy_t] = rdx_rdy_cache.get_or_compute(
        static_cast<double>(grid.dx), static_cast<double>(grid.dy), metric_opts);

    // PERFORMANCE FIX 2025-12-26: Pre-copy dnw/dn to CPU for O(1) loop access
    // Hoist outside all nested loops to avoid GPU sync per iteration
    // FIX 2025-12-26: Extract 1D from potentially 3D metrics before caching
    // FIX 2025-12-27: Use 4-arg version with kdim=0 for cubic grid safety
    // FIX 2025-12-28: Use getMetricOrFallback to avoid 1/eps explosion when undefined
    // If grid.dnw/dn not injected, fallback to ones prevents rdz ~1e10
    auto dnw_safe = metric_utils::getMetricOrFallback(grid.dnw, nz, u.options(), "grid.dnw", "computeMomentumTendenciesAD");
    auto dn_safe = metric_utils::getMetricOrFallback(grid.dn, nz, u.options(), "grid.dn", "computeMomentumTendenciesAD");
    // FIX 2025-01-11 Round27: Select appropriate kdim for dnw/dn (mass vs w-level aware)
    // FIX 2025-01-11 Round28: Pass ny/nx for parity-based cubic/halo detection
    const int64_t dnw_kdim = selectDnwKdim(dnw_safe, nz, ny, nx, grid.rdz_kdim, grid.rdzw_kdim);
    const int64_t dn_kdim = selectDnwKdim(dn_safe, nz, ny, nx, grid.rdz_kdim, grid.rdzw_kdim);
    // FIX 2025-01-11 Round30 Issue 6: When dnw/dn is w-level (nz+1 elements), extract nz+1
    // so all vertical levels are included. For momentum tendencies, we average to mass-levels.
    const bool dnw_is_w_level = (dnw_kdim == grid.rdzw_kdim);
    const bool dn_is_w_level = (dn_kdim == grid.rdzw_kdim);
    const int64_t dnw_extract_nz = dnw_is_w_level ? (nz + 1) : nz;
    const int64_t dn_extract_nz = dn_is_w_level ? (nz + 1) : nz;
    auto dnw_1d_raw = metric_utils::extract1DVerticalMetric(dnw_safe, dnw_extract_nz, ny, nx, dnw_kdim);
    auto dn_1d_raw = metric_utils::extract1DVerticalMetric(dn_safe, dn_extract_nz, ny, nx, dn_kdim);
    // FIX 2025-01-11 Round30 Issue 6: If w-level, average adjacent levels to convert to mass-level
    // w-level has nz+1 values at k-1/2 interfaces; mass-level needs nz values at k centers
    // Mass-level value at k = 0.5 * (w-level[k] + w-level[k+1])
    auto dnw_1d = dnw_is_w_level && dnw_1d_raw.size(0) > 1
        ? 0.5f * (dnw_1d_raw.slice(0, 0, -1) + dnw_1d_raw.slice(0, 1))
        : dnw_1d_raw;
    auto dn_1d = dn_is_w_level && dn_1d_raw.size(0) > 1
        ? 0.5f * (dn_1d_raw.slice(0, 0, -1) + dn_1d_raw.slice(0, 1))
        : dn_1d_raw;
    // FIX 2025-01-11 Round34 Issue 3 NOTE: MetricCache always uses FP32 internally.
    // When use_double_rdx is true (FP64 inputs), the loop path rdz values from
    // dnw_cache.safe_inv_dn(k) are FP32, causing precision loss in intermediate
    // computations. The vectorized path (CUDA) uses FP64 rdx_t/rdy_t tensors.
    // For full FP64 precision in loop path, consider implementing MetricCache64
    // or using vectorized tensor operations (safe_inv_tensor) instead.
    auto dnw_cache = metric_utils::makeMetricCache(dnw_1d);
    auto dn_cache = metric_utils::makeMetricCache(dn_1d);
    
    // FIX 2025-12-28: Use align_msf_3d for unified 2D/3D + device/dtype handling
    // grid.msf* may be CPU (from_blob) while u/v/w are GPU; also handles 3D input
    // TODO PERF 2025-01-11 Round34: These align_msf_3d calls are recomputed every call.
    // For non-moving-nest scenarios, consider grid-level or thread_local caching keyed on
    // (grid.msf*.data_ptr(), ny, nz, nx, stagger, device, dtype). MsfInteriorCache pattern
    // could be adapted to cache the aligned 3D tensors.
    auto opts = u.options().requires_grad(false);
    torch::Tensor msfux_3d = metric_utils::align_msf_3d(grid.msfux, ny, nz, nx, metric_utils::Stagger::U, opts);
    torch::Tensor msfuy_3d = metric_utils::align_msf_3d(grid.msfuy, ny, nz, nx, metric_utils::Stagger::U, opts);
    torch::Tensor msfvx_3d = metric_utils::align_msf_3d(grid.msfvx, ny, nz, nx, metric_utils::Stagger::V, opts);
    torch::Tensor msfvy_3d = metric_utils::align_msf_3d(grid.msfvy, ny, nz, nx, metric_utils::Stagger::V, opts);
    torch::Tensor msftx_3d = metric_utils::align_msf_3d(grid.msftx, ny, nz, nx, metric_utils::Stagger::Mass, opts);
    torch::Tensor msfty_3d = metric_utils::align_msf_3d(grid.msfty, ny, nz, nx, metric_utils::Stagger::Mass, opts);
    
    // U-momentum turbulent stress tendency
    // ∂u/∂t = (1/ρ) * [∂(τ_11)/∂x + ∂(τ_12)/∂y + ∂(τ_13)/∂z]
    // where τ_ij = -2 * ρ * K * D_ij

    // FIX 2025-12-26: Disable gradient graph construction when AD not needed
    // This saves memory and time for the many index() operations in the loops
    // FIX 2025-12-26: Expanded to include ALL input tensors (was missing defor33, defor12/13/23, rho)
    // NOTE 2026-02-16: DO NOT add use_autograd here — turbulence function has in-place ops
    // (add_(), index_put_()) that corrupt forward-mode AD tangents. Need full out-of-place
    // conversion before enabling (see GR7 analysis). For b_wave (khdif=kvdif=0), turbulence
    // contribution is zero anyway.
    const bool needs_grad = u.requires_grad() || v.requires_grad() || w.requires_grad() ||
                            xkmh.requires_grad() || xkmv.requires_grad() ||
                            defor11.requires_grad() || defor22.requires_grad() ||
                            defor33.requires_grad() || defor12.requires_grad() ||
                            defor13.requires_grad() || defor23.requires_grad() ||
                            rho.requires_grad();
    std::optional<torch::NoGradGuard> no_grad_guard;
    if (!needs_grad) {
        no_grad_guard.emplace();
    }

    // FIX 2025-01-11 Round30 Issue 8: Helper to cast FP32 computation result to tend dtype
    // When u_tend is half but rdx_t is FP32, the computed result is FP32. In-place add_()
    // to half tensor will fail, so cast down before adding. No-op if dtypes already match.
    const auto u_tend_dtype = u_tend.scalar_type();
    const auto v_tend_dtype = v_tend.scalar_type();
    const auto w_tend_dtype = w_tend.scalar_type();
    auto cast_to_u = [u_tend_dtype](const torch::Tensor& t) -> torch::Tensor {
        return (t.scalar_type() == u_tend_dtype) ? t : t.to(u_tend_dtype);
    };
    auto cast_to_v = [v_tend_dtype](const torch::Tensor& t) -> torch::Tensor {
        return (t.scalar_type() == v_tend_dtype) ? t : t.to(v_tend_dtype);
    };
    auto cast_to_w = [w_tend_dtype](const torch::Tensor& t) -> torch::Tensor {
        return (t.scalar_type() == w_tend_dtype) ? t : t.to(w_tend_dtype);
    };

    // ============================================================================
    // PERFORMANCE FIX 2025-12-26: Optimized paths for non-AD execution
    // ============================================================================
    // Problem: Triple loops with index() create kernel launch overhead on CUDA
    // Solutions:
    // - CUDA path: Use vectorized tensor operations (roll/slice)
    // - CPU path: Use contiguous accessor for O(1) element access
    //
    // Note: Full vectorization is complex due to staggered grid averaging.
    // Below we use a hybrid approach: vectorize the dominant D11/D22 terms,
    // fall back to loop for complex D12/D13/D23 with multi-point averaging.
    // ============================================================================

    const bool is_cuda = u.is_cuda();
    const bool is_contiguous = xkmh.is_contiguous() && defor11.is_contiguous() &&
                                rho.is_contiguous() && u_tend.is_contiguous();

    // FIX 2025-01-11 Round34 Issue 1: Declare tau12_vort_shared outside vectorized block
    // so used_vectorized_D12 can check .defined() to gate loop path correctly
    torch::Tensor tau12_vort_shared;
    // FIX 2025-01-11 Round34: Declare tau13/tau23 shared tensors outside block for tracking
    torch::Tensor tau13_shared;   // U-momentum D13 (dtau13_dz)
    torch::Tensor tau23_shared;   // V-momentum D23 (dtau23_dz)
    torch::Tensor tau13_w_shared; // W-momentum D13 (dtau13_dx)
    torch::Tensor tau23_w_shared; // W-momentum D23 (dtau23_dy)

    // FIX 2025-12-26: Vectorized CUDA path for dominant D11/D22 terms
    // This avoids triple-loop kernel explosion when gradients are not needed
    if (!needs_grad && is_cuda && is_contiguous && ny > 2 && nx > 2) {
        // ============================================================================
        // Vectorized U-momentum D11 term: dtau11_dx = msf * rdx * d(2*K*D11)/dx
        // ============================================================================
        // tau11 at mass points: [ny, nz, nx]
        auto tau11 = 2.0f * xkmh * defor11;  // [ny, nz, nx]

        // Staggered gradient in x: dtau11_dx at u-points [ny, nz, nx-1] from i=1 to nx-1
        // tau11[:,:,1:] - tau11[:,:,:-1] gives gradient at u-points (excluding i=0)
        auto tau11_east = tau11.slice(2, 1, nx);     // [ny, nz, nx-1]
        auto tau11_west = tau11.slice(2, 0, nx - 1); // [ny, nz, nx-1]
        // FIX 2025-01-11 Round27: Use rdx_t tensor for FP64 precision preservation
        auto dtau11_dx_interior = msfux_3d.slice(2, 1, nx) * rdx_t * (tau11_east - tau11_west);

        // Density at u-point uses western mass point: rho[:,:,0:nx-1]
        auto rho_u = rho.slice(2, 0, nx - 1);  // [ny, nz, nx-1]

        // Add to u_tend interior: j=1:ny-1, all k, i=1:nx
        auto u_tend_interior = u_tend.slice(0, 1, ny - 1).slice(2, 1, nx);  // [ny-2, nz, nx-1]
        auto dtau11_dx_trimmed = dtau11_dx_interior.slice(0, 1, ny - 1);    // [ny-2, nz, nx-1]
        auto rho_u_trimmed = rho_u.slice(0, 1, ny - 1);                      // [ny-2, nz, nx-1]
        // FIX 2025-01-11 Round30 Issue 8: Cast to u_tend dtype before in-place add
        u_tend_interior.add_(cast_to_u(dtau11_dx_trimmed / rho_u_trimmed));

        // ============================================================================
        // Vectorized V-momentum D22 term: dtau22_dy = msf * rdy * d(2*K*D22)/dy
        // ============================================================================
        auto tau22 = 2.0f * xkmh * defor22;  // [ny, nz, nx]

        // Staggered gradient in y: dtau22_dy at v-points [ny-1, nz, nx] from j=1 to ny-1
        auto tau22_north = tau22.slice(0, 1, ny);     // [ny-1, nz, nx]
        auto tau22_south = tau22.slice(0, 0, ny - 1); // [ny-1, nz, nx]
        // FIX 2025-01-11 Round27: Use rdy_t tensor for FP64 precision preservation
        auto dtau22_dy_interior = msfvy_3d.slice(0, 1, ny) * rdy_t * (tau22_north - tau22_south);

        // Density at v-point uses southern mass point: rho[0:ny-1,:,:]
        auto rho_v = rho.slice(0, 0, ny - 1);  // [ny-1, nz, nx]

        // Add to v_tend interior: j=1:ny, k all, i=1:nx-1
        auto v_tend_interior = v_tend.slice(0, 1, ny).slice(2, 1, nx - 1);   // [ny-1, nz, nx-2]
        auto dtau22_dy_trimmed = dtau22_dy_interior.slice(2, 1, nx - 1);     // [ny-1, nz, nx-2]
        auto rho_v_trimmed = rho_v.slice(2, 1, nx - 1);                       // [ny-1, nz, nx-2]
        // FIX 2025-01-11 Round30 Issue 8: Cast to v_tend dtype before in-place add
        v_tend_interior.add_(cast_to_v(dtau22_dy_trimmed / rho_v_trimmed));

        // ============================================================================
        // FIX 2025-01-11 Round27: Vectorized D12 term for U-momentum and V-momentum
        // D12 at vorticity points [ny+1, nz, nx+1], average 4-point to u/v-stagger
        // FIX 2025-01-11 Round28: Add stagger shape validation before vectorized D12
        // FIX 2025-01-11 Round29: Strengthen to >= ny+1 / >= nx+1 to reject non-staggered [ny,nz,nx]
        // FIX 2025-01-11 Round31 Issue 2: Add k-dim validation (size(1) >= nz)
        // FIX 2025-01-11 Round32 Issue 5: Compute K12/tau12 once, share between U/V momentum
        // ============================================================================
        const bool d12_has_j_stagger = defor12.defined() && defor12.size(0) >= ny + 1;
        const bool d12_has_k_size = defor12.defined() && defor12.size(1) >= nz;
        const bool d12_has_i_stagger = defor12.defined() && defor12.size(2) >= nx + 1;

        // Shared D12 computation: K12_vort and tau12_vort computed once, used for both U and V momentum
        // FIX 2025-01-11 Round34: tau12_vort_shared declared outside block for used_vectorized_D12 check
        if (d12_has_j_stagger && d12_has_k_size && d12_has_i_stagger && defor12.numel() > 0 && ny > 2 && nx > 2) {
            // FIX 2025-01-11 Round31 Issue 1: Center-slice for D12 halo handling
            // D12 at vorticity points [ny+1+2*halo_j, nz+2*halo_k, nx+1+2*halo_i]
            // Need to extract center [ny+1, nz, nx+1] then slice interior [ny-1, nz, nx-1]
            const int64_t diff_j = defor12.size(0) - (ny + 1);
            const int64_t diff_k = defor12.size(1) - nz;
            const int64_t diff_i = defor12.size(2) - (nx + 1);
            const int64_t halo_j = diff_j / 2;  // symmetric halo in j
            const int64_t halo_k = diff_k / 2;  // symmetric halo in k
            const int64_t halo_i = diff_i / 2;  // symmetric halo in i
            const bool has_odd_halo = (diff_j > 0 && diff_j % 2 != 0) ||
                                      (diff_k > 0 && diff_k % 2 != 0) ||
                                      (diff_i > 0 && diff_i % 2 != 0);

            torch::Tensor D12_interior;
            if (has_odd_halo) {
                // Fallback: use front-slice for odd halo (can't center perfectly)
                TORCH_WARN_ONCE("applySmagorinskyTurbulenceWRF: D12 has odd halo, using front-slice fallback");
                D12_interior = defor12.slice(0, 1, ny).slice(1, 0, nz).slice(2, 1, nx);
            } else {
                // Center-slice: offset by halo to extract true interior
                D12_interior = defor12.slice(0, halo_j + 1, halo_j + ny)
                                      .slice(1, halo_k, halo_k + nz)
                                      .slice(2, halo_i + 1, halo_i + nx);
            }  // [ny-1, nz, nx-1]

            // Compute K12 as average of surrounding mass points for interior vorticity points
            // K12_interior: [ny-1, nz, nx-1] at j=1..ny-1, i=1..nx-1
            // K12[j,k,i] = 0.25*(xkmh[j-1,k,i-1] + xkmh[j-1,k,i] + xkmh[j,k,i-1] + xkmh[j,k,i])
            auto xk_j0_i0 = xkmh.slice(0, 0, ny-1).slice(2, 0, nx-1);   // j=0..ny-2, i=0..nx-2
            auto xk_j0_i1 = xkmh.slice(0, 0, ny-1).slice(2, 1, nx);     // j=0..ny-2, i=1..nx-1
            auto xk_j1_i0 = xkmh.slice(0, 1, ny).slice(2, 0, nx-1);     // j=1..ny-1, i=0..nx-2
            auto xk_j1_i1 = xkmh.slice(0, 1, ny).slice(2, 1, nx);       // j=1..ny-1, i=1..nx-1
            auto K12_vort = 0.25f * (xk_j0_i0 + xk_j0_i1 + xk_j1_i0 + xk_j1_i1);  // [ny-1, nz, nx-1]

            // tau12 at vorticity points: K12 * D12 (for j=1..ny-1, i=1..nx-1)
            // Shared between U-momentum (y-gradient) and V-momentum (x-gradient)
            tau12_vort_shared = K12_vort * D12_interior;  // [ny-1, nz, nx-1]
        }

        // ============================================================================
        // D12 term for U-momentum: dtau12_dy at u-points
        // ============================================================================
        if (tau12_vort_shared.defined() && tau12_vort_shared.size(0) > 1) {
            // d(tau12)/dy at u-points: (tau12[j+1] - tau12[j]) at j=1..ny-2
            // FIX 2025-01-11 Round28: Fixed off-by-one - was j=2..ny-2, now j=1..ny-2 to match loop
            // tau12_north: j=2..ny-1 -> index [1, ny-1] in tau12_vort
            // tau12_south: j=1..ny-2 -> index [0, ny-2] in tau12_vort
            auto tau12_north = tau12_vort_shared.slice(0, 1, ny-1);  // [ny-2, nz, nx-1]
            auto tau12_south = tau12_vort_shared.slice(0, 0, ny-2);  // [ny-2, nz, nx-1]
            // FIX 2025-01-11 Round27: Use rdy_t tensor for FP64 precision preservation
            auto dtau12_dy = msfuy_3d.slice(0, 1, ny-1).slice(2, 1, nx) * rdy_t * (tau12_north - tau12_south);

            // rho at u-point: uses western mass point
            auto rho_for_u = rho.slice(0, 1, ny-1).slice(2, 0, nx-1);  // [ny-2, nz, nx-1]

            // Add to u_tend interior: j=1..ny-2, k=all, i=1..nx
            auto u_tend_D12 = u_tend.slice(0, 1, ny-1).slice(2, 1, nx);  // [ny-2, nz, nx-1]
            // FIX 2025-01-11 Round30 Issue 8: Cast to u_tend dtype before in-place add
            u_tend_D12.add_(cast_to_u(dtau12_dy / rho_for_u));
        }

        // ============================================================================
        // D12 term for V-momentum: dtau12_dx at v-points
        // ============================================================================
        if (tau12_vort_shared.defined() && tau12_vort_shared.size(2) > 1) {
            // d(tau12)/dx at v-points: (tau12[i+1] - tau12[i]) at i=1..nx-2
            // FIX 2025-01-11 Round28: Fixed off-by-one - was i=2..nx-2, now i=1..nx-2 to match loop
            auto tau12_east = tau12_vort_shared.slice(2, 1, nx-1);  // [ny-1, nz, nx-2]
            auto tau12_west = tau12_vort_shared.slice(2, 0, nx-2);  // [ny-1, nz, nx-2]
            // FIX 2025-01-11 Round27: Use rdx_t tensor for FP64 precision preservation
            auto dtau12_dx = msfvx_3d.slice(0, 1, ny).slice(2, 1, nx-1) * rdx_t * (tau12_east - tau12_west);

            // rho at v-point: uses southern mass point
            auto rho_for_v = rho.slice(0, 0, ny-1).slice(2, 1, nx-1);  // [ny-1, nz, nx-2]

            // Add to v_tend: j=1..ny, k=all, i=1..nx-2
            auto v_tend_D12 = v_tend.slice(0, 1, ny).slice(2, 1, nx-1);  // [ny-1, nz, nx-2]
            // FIX 2025-01-11 Round30 Issue 8: Cast to v_tend dtype before in-place add
            v_tend_D12.add_(cast_to_v(dtau12_dx / rho_for_v));
        }

        // ============================================================================
        // FIX 2025-01-11 Round34: Vectorized D13 term for U-momentum
        // D13 at U-W points [ny, nz+1, nx+1]: dtau13_dz = rdz * (tau13[k+1] - tau13[k])
        // Loop range: j=1..ny-2, k=1..nz-2, i=1..nx-1
        // ============================================================================
        const bool d13_has_j_size = defor13.defined() && defor13.size(0) >= ny;
        const bool d13_has_w_stagger = defor13.defined() && defor13.size(1) >= nz + 1;
        const bool d13_has_u_stagger = defor13.defined() && defor13.size(2) >= nx + 1;

        if (d13_has_j_size && d13_has_w_stagger && d13_has_u_stagger &&
            defor13.numel() > 0 && nz > 2 && ny > 2 && nx > 2) {
            // Extract center-sliced D13 for halo handling
            auto D13_full = extractCenterSlice3D(defor13, ny, nz + 1, nx + 1,
                "applySmagorinskyTurbulenceWRF", "D13");

            // rdz vectorized: 1/dnw as 1D then broadcast
            // dnw_1d is already mass-level corrected (nz elements)
            auto rdnw_1d = metric_utils::safe_inv_tensor(dnw_1d);  // [nz]

            // For k=1..nz-2: rdz_below = rdnw[k-1], rdz_above = rdnw[k]
            // rdz_avg = 0.5 * (rdz_below + rdz_above) for k=1..nz-2
            // rdnw[:nz-2] is k=0..nz-3 (rdz_below for k=1..nz-2)
            // rdnw[1:nz-1] is k=1..nz-2 (rdz_above for k=1..nz-2)
            auto rdz_avg_1d = 0.5f * (rdnw_1d.slice(0, 0, nz-2) + rdnw_1d.slice(0, 1, nz-1));  // [nz-2]
            auto rdz_avg_3d = rdz_avg_1d.view({1, nz-2, 1});  // [1, nz-2, 1] for broadcast

            // Kv at staggered points via slice averaging (i-direction for u-stagger)
            // Kv_below[j, k-1, i] = 0.5 * (xkmv[j, k-1, i-1] + xkmv[j, k-1, i])
            // Kv_above[j, k, i] = 0.5 * (xkmv[j, k, i-1] + xkmv[j, k, i])
            // For k=1..nz-2: need xkmv[:, 0:nz-2, :] for k-1 and xkmv[:, 1:nz-1, :] for k
            // For i averaging: xkmv[:, :, 0:nx-1] + xkmv[:, :, 1:nx]
            auto xkmv_j = xkmv.slice(0, 1, ny-1);  // j=1..ny-2
            auto Kv_below = 0.5f * (xkmv_j.slice(1, 0, nz-2).slice(2, 0, nx-1) +
                                    xkmv_j.slice(1, 0, nz-2).slice(2, 1, nx));  // [ny-2, nz-2, nx-1]
            auto Kv_above = 0.5f * (xkmv_j.slice(1, 1, nz-1).slice(2, 0, nx-1) +
                                    xkmv_j.slice(1, 1, nz-1).slice(2, 1, nx));  // [ny-2, nz-2, nx-1]

            // D13 slicing: D13[j, k, i] at w-level k, D13[j, k+1, i] at w-level k+1
            // For k=1..nz-2: D13[:, 1:nz-1, :] and D13[:, 2:nz, :]
            // For j=1..ny-2: D13[1:ny-1, :, :]
            // For i=1..nx-1 (u-stagger interior): D13[:, :, 1:nx]
            auto D13_j = D13_full.slice(0, 1, ny-1);  // [ny-2, nz+1, nx+1]
            auto D13_below = D13_j.slice(1, 1, nz-1).slice(2, 1, nx);  // k=1..nz-2, i=1..nx-1 → [ny-2, nz-2, nx-1]
            auto D13_above = D13_j.slice(1, 2, nz).slice(2, 1, nx);    // k=2..nz-1, i=1..nx-1 → [ny-2, nz-2, nx-1]

            auto tau13_below = Kv_below * D13_below;  // [ny-2, nz-2, nx-1]
            auto tau13_above = Kv_above * D13_above;  // [ny-2, nz-2, nx-1]

            // dtau13_dz = rdz_avg * (tau13_above - tau13_below)
            auto dtau13_dz = rdz_avg_3d * (tau13_above - tau13_below);  // [ny-2, nz-2, nx-1]

            // rho at u-point uses western mass point: rho[j, k, i-1]
            // For j=1..ny-2, k=1..nz-2, i=1..nx-1: rho[1:ny-1, 1:nz-1, 0:nx-1]
            auto rho_u = rho.slice(0, 1, ny-1).slice(1, 1, nz-1).slice(2, 0, nx-1);  // [ny-2, nz-2, nx-1]

            // Add to u_tend: j=1..ny-2, k=1..nz-2, i=1..nx-1
            auto u_tend_D13 = u_tend.slice(0, 1, ny-1).slice(1, 1, nz-1).slice(2, 1, nx);  // [ny-2, nz-2, nx-1]
            u_tend_D13.add_(cast_to_u(dtau13_dz / rho_u));

            tau13_shared = tau13_above;  // Store for potential W-momentum use
        }

        // ============================================================================
        // FIX 2025-01-11 Round34: Vectorized D23 term for V-momentum
        // D23 at V-W points [ny+1, nz+1, nx]: dtau23_dz = rdz * (tau23[k+1] - tau23[k])
        // Loop range: j=1..ny-1, k=1..nz-2, i=1..nx-2
        // ============================================================================
        const bool d23_has_v_stagger = defor23.defined() && defor23.size(0) >= ny + 1;
        const bool d23_has_w_stagger = defor23.defined() && defor23.size(1) >= nz + 1;
        const bool d23_has_i_size = defor23.defined() && defor23.size(2) >= nx;

        if (d23_has_v_stagger && d23_has_w_stagger && d23_has_i_size &&
            defor23.numel() > 0 && nz > 2 && ny > 2 && nx > 2) {
            // Extract center-sliced D23 for halo handling
            auto D23_full = extractCenterSlice3D(defor23, ny + 1, nz + 1, nx,
                "applySmagorinskyTurbulenceWRF", "D23");

            // rdz vectorized: 1/dnw as 1D (already computed if D13 ran)
            auto rdnw_1d = metric_utils::safe_inv_tensor(dnw_1d);  // [nz]
            auto rdz_avg_1d = 0.5f * (rdnw_1d.slice(0, 0, nz-2) + rdnw_1d.slice(0, 1, nz-1));  // [nz-2]
            auto rdz_avg_3d = rdz_avg_1d.view({1, nz-2, 1});  // [1, nz-2, 1] for broadcast

            // Kv at staggered points via slice averaging (j-direction for v-stagger)
            // Kv_below[j, k-1, i] = 0.5 * (xkmv[j-1, k-1, i] + xkmv[j, k-1, i])
            // Kv_above[j, k, i] = 0.5 * (xkmv[j-1, k, i] + xkmv[j, k, i])
            // For j=1..ny-1 (v-stagger interior): need xkmv[0:ny-1, :, :] and xkmv[1:ny, :, :]
            // For i=1..nx-2: xkmv[:, :, 1:nx-1]
            auto Kv_below = 0.5f * (xkmv.slice(0, 0, ny-1).slice(1, 0, nz-2).slice(2, 1, nx-1) +
                                    xkmv.slice(0, 1, ny).slice(1, 0, nz-2).slice(2, 1, nx-1));  // [ny-1, nz-2, nx-2]
            auto Kv_above = 0.5f * (xkmv.slice(0, 0, ny-1).slice(1, 1, nz-1).slice(2, 1, nx-1) +
                                    xkmv.slice(0, 1, ny).slice(1, 1, nz-1).slice(2, 1, nx-1));  // [ny-1, nz-2, nx-2]

            // D23 slicing: D23[j, k, i] at w-level k, D23[j, k+1, i] at w-level k+1
            // For k=1..nz-2: D23[:, 1:nz-1, :] and D23[:, 2:nz, :]
            // For j=1..ny-1 (v-stagger): D23[1:ny, :, :]
            // For i=1..nx-2: D23[:, :, 1:nx-1]
            auto D23_j = D23_full.slice(0, 1, ny);  // [ny-1, nz+1, nx]
            auto D23_below = D23_j.slice(1, 1, nz-1).slice(2, 1, nx-1);  // [ny-1, nz-2, nx-2]
            auto D23_above = D23_j.slice(1, 2, nz).slice(2, 1, nx-1);    // [ny-1, nz-2, nx-2]

            auto tau23_below = Kv_below * D23_below;  // [ny-1, nz-2, nx-2]
            auto tau23_above = Kv_above * D23_above;  // [ny-1, nz-2, nx-2]

            // dtau23_dz = rdz_avg * (tau23_above - tau23_below)
            auto dtau23_dz = rdz_avg_3d * (tau23_above - tau23_below);  // [ny-1, nz-2, nx-2]

            // rho at v-point uses southern mass point: rho[j-1, k, i]
            // For j=1..ny-1, k=1..nz-2, i=1..nx-2: rho[0:ny-1, 1:nz-1, 1:nx-1]
            auto rho_v = rho.slice(0, 0, ny-1).slice(1, 1, nz-1).slice(2, 1, nx-1);  // [ny-1, nz-2, nx-2]

            // Add to v_tend: j=1..ny-1, k=1..nz-2, i=1..nx-2
            auto v_tend_D23 = v_tend.slice(0, 1, ny).slice(1, 1, nz-1).slice(2, 1, nx-1);  // [ny-1, nz-2, nx-2]
            v_tend_D23.add_(cast_to_v(dtau23_dz / rho_v));

            tau23_shared = tau23_above;  // Store for potential W-momentum use
        }

        // ============================================================================
        // FIX 2025-01-11 Round34: Vectorized D13 term for W-momentum
        // D13 at U-W points [ny, nz+1, nx+1]: dtau13_dx = rdx * (tau13[i+1] - tau13[i])
        // Loop range: j=1..ny-2, k=1..nz-1, i=1..nx-2
        // Kh averaging: k-direction for w-stagger, i-direction for west/east
        // ============================================================================
        if (d13_has_j_size && d13_has_w_stagger && d13_has_u_stagger &&
            defor13.numel() > 0 && nz > 1 && ny > 2 && nx > 2) {
            // Extract center-sliced D13 for halo handling
            auto D13_full = extractCenterSlice3D(defor13, ny, nz + 1, nx + 1,
                "applySmagorinskyTurbulenceWRF", "D13_W");

            // Kh averaging at w-level: 0.5*(xkmh[k-1] + xkmh[k]) for k=1..nz-1
            // For i-stagger west: xkmh[:, :, i-1], east: xkmh[:, :, i]
            // j=1..ny-2: xkmh[1:ny-1, :, :]
            auto xkmh_j = xkmh.slice(0, 1, ny-1);  // [ny-2, nz, nx]

            // K13_w[j,k,i] = 0.5*(xkmh[j,k-1,i-1] + xkmh[j,k,i-1]) for k=1..nz-1, i=1..nx-2
            // xkmh[:, 0:nz-1, :] is k-1 for k=1..nz-1, xkmh[:, 1:nz, :] is k for k=1..nz-1
            auto K13_w = 0.5f * (xkmh_j.slice(1, 0, nz-1).slice(2, 0, nx-2) +
                                 xkmh_j.slice(1, 1, nz).slice(2, 0, nx-2));  // [ny-2, nz-1, nx-2]
            // K13_e[j,k,i] = 0.5*(xkmh[j,k-1,i] + xkmh[j,k,i]) for k=1..nz-1, i=1..nx-2
            auto K13_e = 0.5f * (xkmh_j.slice(1, 0, nz-1).slice(2, 1, nx-1) +
                                 xkmh_j.slice(1, 1, nz).slice(2, 1, nx-1));  // [ny-2, nz-1, nx-2]

            // D13 slicing: D13[j,k,i] at w-level k
            // For j=1..ny-2: D13[1:ny-1, :, :]
            // For k=1..nz-1 (w-level interior): D13[:, 1:nz, :]
            // For i differencing: west=D13[:,:,i], east=D13[:,:,i+1] for i=1..nx-2
            //   → west: D13[:, :, 1:nx-1], east: D13[:, :, 2:nx]
            auto D13_j = D13_full.slice(0, 1, ny-1);  // [ny-2, nz+1, nx+1]
            auto D13_w = D13_j.slice(1, 1, nz).slice(2, 1, nx-1);   // [ny-2, nz-1, nx-2]
            auto D13_e = D13_j.slice(1, 1, nz).slice(2, 2, nx);     // [ny-2, nz-1, nx-2]

            auto tau13_w_val = K13_w * D13_w;  // [ny-2, nz-1, nx-2]
            auto tau13_e_val = K13_e * D13_e;  // [ny-2, nz-1, nx-2]

            // msftx at [j, k-1, i] for mass level below w-point
            // j=1..ny-2, k=1..nz-1 → msftx[1:ny-1, 0:nz-1, 1:nx-1]
            auto msftx_w = msftx_3d.slice(0, 1, ny-1).slice(1, 0, nz-1).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]

            // dtau13_dx = msftx * rdx * (tau13_e - tau13_w)
            auto dtau13_dx = use_double_rdx
                ? msftx_w * rdx_t * (tau13_e_val - tau13_w_val)
                : msftx_w * rdx_t * (tau13_e_val - tau13_w_val);

            // rho at mass level below w-point: rho[j, k-1, i] for j=1..ny-2, k=1..nz-1, i=1..nx-2
            auto rho_w = rho.slice(0, 1, ny-1).slice(1, 0, nz-1).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]

            // Add to w_tend: j=1..ny-2, k=1..nz-1, i=1..nx-2
            auto w_tend_D13 = w_tend.slice(0, 1, ny-1).slice(1, 1, nz).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]
            w_tend_D13.add_(cast_to_w(dtau13_dx / rho_w));

            tau13_w_shared = tau13_e_val;  // Track that vectorization ran
        }

        // ============================================================================
        // FIX 2025-01-11 Round34: Vectorized D23 term for W-momentum
        // D23 at V-W points [ny+1, nz+1, nx]: dtau23_dy = rdy * (tau23[j+1] - tau23[j])
        // Loop range: j=1..ny-2, k=1..nz-1, i=1..nx-2
        // Kh averaging: k-direction for w-stagger, j-direction for south/north
        // ============================================================================
        if (d23_has_v_stagger && d23_has_w_stagger && d23_has_i_size &&
            defor23.numel() > 0 && nz > 1 && ny > 2 && nx > 2) {
            // Extract center-sliced D23 for halo handling
            auto D23_full = extractCenterSlice3D(defor23, ny + 1, nz + 1, nx,
                "applySmagorinskyTurbulenceWRF", "D23_W");

            // Kh averaging at w-level: 0.5*(xkmh[k-1] + xkmh[k]) for k=1..nz-1
            // For j-stagger south: xkmh[j-1, :, :], north: xkmh[j, :, :]
            // j=1..ny-2: south uses xkmh[0:ny-2, :, :], north uses xkmh[1:ny-1, :, :]
            // i=1..nx-2: xkmh[:, :, 1:nx-1]

            // K23_s[j,k,i] = 0.5*(xkmh[j-1,k-1,i] + xkmh[j-1,k,i]) for k=1..nz-1, j=1..ny-2
            auto K23_s = 0.5f * (xkmh.slice(0, 0, ny-2).slice(1, 0, nz-1).slice(2, 1, nx-1) +
                                 xkmh.slice(0, 0, ny-2).slice(1, 1, nz).slice(2, 1, nx-1));  // [ny-2, nz-1, nx-2]
            // K23_n[j,k,i] = 0.5*(xkmh[j,k-1,i] + xkmh[j,k,i]) for k=1..nz-1, j=1..ny-2
            auto K23_n = 0.5f * (xkmh.slice(0, 1, ny-1).slice(1, 0, nz-1).slice(2, 1, nx-1) +
                                 xkmh.slice(0, 1, ny-1).slice(1, 1, nz).slice(2, 1, nx-1));  // [ny-2, nz-1, nx-2]

            // D23 slicing: D23[j,k,i] at w-level k
            // For j differencing: south=D23[j,:,:], north=D23[j+1,:,:] for j=1..ny-2
            //   → south: D23[1:ny-1, :, :], north: D23[2:ny, :, :]
            // For k=1..nz-1: D23[:, 1:nz, :]
            // For i=1..nx-2: D23[:, :, 1:nx-1]
            auto D23_s = D23_full.slice(0, 1, ny-1).slice(1, 1, nz).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]
            auto D23_n = D23_full.slice(0, 2, ny).slice(1, 1, nz).slice(2, 1, nx-1);    // [ny-2, nz-1, nx-2]

            auto tau23_s_val = K23_s * D23_s;  // [ny-2, nz-1, nx-2]
            auto tau23_n_val = K23_n * D23_n;  // [ny-2, nz-1, nx-2]

            // msfty at [j, k-1, i] for mass level below w-point
            // j=1..ny-2, k=1..nz-1 → msfty[1:ny-1, 0:nz-1, 1:nx-1]
            auto msfty_w = msfty_3d.slice(0, 1, ny-1).slice(1, 0, nz-1).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]

            // dtau23_dy = msfty * rdy * (tau23_n - tau23_s)
            auto dtau23_dy = use_double_rdx
                ? msfty_w * rdy_t * (tau23_n_val - tau23_s_val)
                : msfty_w * rdy_t * (tau23_n_val - tau23_s_val);

            // rho at mass level below w-point: rho[j, k-1, i] for j=1..ny-2, k=1..nz-1, i=1..nx-2
            auto rho_w = rho.slice(0, 1, ny-1).slice(1, 0, nz-1).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]

            // Add to w_tend: j=1..ny-2, k=1..nz-1, i=1..nx-2
            auto w_tend_D23 = w_tend.slice(0, 1, ny-1).slice(1, 1, nz).slice(2, 1, nx-1);  // [ny-2, nz-1, nx-2]
            w_tend_D23.add_(cast_to_w(dtau23_dy / rho_w));

            tau23_w_shared = tau23_n_val;  // Track that vectorization ran
        }
    }

    // Track which terms were handled by vectorized path
    // FIX 2025-01-11 Round27: Track D12 vectorization separately
    // FIX 2025-01-11 Round34 Issue 1: Use tau12_vort_shared.defined() to ensure D12 was actually vectorized
    // Previous check (defor12.defined()) missed cases where shape/size conditions weren't met
    const bool used_vectorized_D11_D22 = (!needs_grad && is_cuda && is_contiguous && ny > 2 && nx > 2);
    const bool used_vectorized_D12 = (used_vectorized_D11_D22 && tau12_vort_shared.defined());
    // FIX 2025-01-11 Round34: Track D13/D23 vectorization to skip loop path
    // U-momentum D13 (dtau13_dz) and V-momentum D23 (dtau23_dz)
    const bool used_vectorized_D13 = (used_vectorized_D11_D22 && tau13_shared.defined());
    const bool used_vectorized_D23 = (used_vectorized_D11_D22 && tau23_shared.defined());
    // W-momentum D13 (dtau13_dx) and D23 (dtau23_dy)
    const bool used_vectorized_D13_W = (used_vectorized_D11_D22 && tau13_w_shared.defined());
    const bool used_vectorized_D23_W = (used_vectorized_D11_D22 && tau23_w_shared.defined());

    // FIX 2026-01-31: Vectorized stress divergence for U, V, W momentum.
    // Previous triple-nested loops with index_put_ were always active when needs_grad=true
    // (the vectorized CUDA path requires !needs_grad && is_cuda).
    // Now uses bulk slice operations for full AD compatibility.

    // ===== U-momentum stress divergence =====
    // Interior: j=1..ny-2, k=0..nz-1, i=1..nx-1
    {
        int j0 = 1, j1 = ny - 1;  // j range
        int i0 = 1, i1 = nx;       // i range (u-stagger)

        // D11: ∂(2K*D11)/∂x
        if (!used_vectorized_D11_D22 && j0 < j1 && i0 < i1) {
            auto tau11_w = 2.0f * xkmh.slice(0, j0, j1).slice(2, i0 - 1, i1 - 1) *
                                  defor11.slice(0, j0, j1).slice(2, i0 - 1, i1 - 1);
            auto tau11_e = 2.0f * xkmh.slice(0, j0, j1).slice(2, i0, i1) *
                                  defor11.slice(0, j0, j1).slice(2, i0, i1);
            auto msfux_s = msfux_3d.slice(0, j0, j1).slice(2, i0, i1);
            auto dtau11_dx = msfux_s * (use_double_rdx ? rdx_d : rdx_f) * (tau11_e - tau11_w);
            auto rho_u = rho.slice(0, j0, j1).slice(2, i0 - 1, i1 - 1);
            u_tend.slice(0, j0, j1).slice(2, i0, i1).add_(dtau11_dx / rho_u);
        }

        // D12: ∂(K*D12)/∂y (needs j-1 and j+1, so j=1..ny-2 is safe)
        if (!used_vectorized_D12 && defor12.defined() && j0 < j1 && i0 < i1) {
            auto xkmh_jm1 = xkmh.slice(0, j0 - 1, j1 - 1);  // j-1
            auto xkmh_j   = xkmh.slice(0, j0, j1);
            auto xkmh_jp1 = xkmh.slice(0, j0 + 1, j1 + 1);  // j+1
            // K12_s = 0.25*(xkmh[j-1,k,i-1]+xkmh[j-1,k,i]+xkmh[j,k,i-1]+xkmh[j,k,i])
            auto K12_s = 0.25f * (xkmh_jm1.slice(2, i0 - 1, i1 - 1) + xkmh_jm1.slice(2, i0, i1) +
                                   xkmh_j.slice(2, i0 - 1, i1 - 1)   + xkmh_j.slice(2, i0, i1));
            auto K12_n = 0.25f * (xkmh_j.slice(2, i0 - 1, i1 - 1)   + xkmh_j.slice(2, i0, i1) +
                                   xkmh_jp1.slice(2, i0 - 1, i1 - 1) + xkmh_jp1.slice(2, i0, i1));
            auto tau12_s = K12_s * defor12.slice(0, j0, j1).slice(2, i0, i1);
            auto tau12_n = K12_n * defor12.slice(0, j0 + 1, j1 + 1).slice(2, i0, i1);
            auto msfuy_s = msfuy_3d.slice(0, j0, j1).slice(2, i0, i1);
            auto dtau12_dy = msfuy_s * (use_double_rdx ? rdy_d : rdy_f) * (tau12_n - tau12_s);
            auto rho_u = rho.slice(0, j0, j1).slice(2, i0 - 1, i1 - 1);
            u_tend.slice(0, j0, j1).slice(2, i0, i1).add_(dtau12_dy / rho_u);
        }

        // D13: ∂(K*D13)/∂z (k=1..nz-2 interior only)
        if (!used_vectorized_D13 && defor13.defined() && nz > 2) {
            int k0 = 1, k1 = nz - 1;
            // Build rdz tensor: 0.5*(1/dnw[k-1] + 1/dnw[k]) for k=1..nz-2
            std::vector<float> rdz_vals(k1 - k0);
            for (int k = k0; k < k1; ++k) {
                rdz_vals[k - k0] = 0.5f * (dnw_cache.safe_inv_dn(k - 1) + dnw_cache.safe_inv_dn(k));
            }
            auto rdz_t = torch::from_blob(rdz_vals.data(), {(int)rdz_vals.size()},
                torch::TensorOptions().dtype(torch::kFloat32))
                .to(u_tend.device()).reshape({1, k1 - k0, 1});

            auto Kv_below = 0.5f * (xkmv.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0 - 1, i1 - 1) +
                                     xkmv.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1));
            auto Kv_above = 0.5f * (xkmv.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0 - 1, i1 - 1) +
                                     xkmv.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1));
            auto tau13_below = Kv_below * defor13.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1);
            auto tau13_above = Kv_above * defor13.slice(0, j0, j1).slice(1, k0 + 1, k1 + 1).slice(2, i0, i1);
            auto dtau13_dz = rdz_t * (tau13_above - tau13_below);
            auto rho_u = rho.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0 - 1, i1 - 1);
            u_tend.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1).add_(dtau13_dz / rho_u);
        }
    }

    // ===== V-momentum stress divergence =====
    // Interior: j=1..ny-1, k=0..nz-1, i=1..nx-2
    {
        int j0 = 1, j1 = ny;       // j range (v-stagger)
        int i0 = 1, i1 = nx - 1;   // i range

        // D12: ∂(K*D12)/∂x
        if (!used_vectorized_D12 && defor12.defined() && j0 < j1 && i0 < i1) {
            auto xkmh_jm1 = xkmh.slice(0, j0 - 1, j1 - 1);
            auto xkmh_j   = xkmh.slice(0, j0, j1);
            auto K12_w = 0.25f * (xkmh_jm1.slice(2, i0 - 1, i1 - 1) + xkmh_j.slice(2, i0 - 1, i1 - 1) +
                                   xkmh_jm1.slice(2, i0, i1)         + xkmh_j.slice(2, i0, i1));
            auto K12_e = 0.25f * (xkmh_jm1.slice(2, i0, i1)         + xkmh_j.slice(2, i0, i1) +
                                   xkmh_jm1.slice(2, i0 + 1, i1 + 1) + xkmh_j.slice(2, i0 + 1, i1 + 1));
            auto tau12_w = K12_w * defor12.slice(0, j0, j1).slice(2, i0, i1);
            auto tau12_e = K12_e * defor12.slice(0, j0, j1).slice(2, i0 + 1, i1 + 1);
            auto msfvx_s = msfvx_3d.slice(0, j0, j1).slice(2, i0, i1);
            auto dtau12_dx = msfvx_s * (use_double_rdx ? rdx_d : rdx_f) * (tau12_e - tau12_w);
            auto rho_v = rho.slice(0, j0 - 1, j1 - 1).slice(2, i0, i1);
            v_tend.slice(0, j0, j1).slice(2, i0, i1).add_(dtau12_dx / rho_v);
        }

        // D22: ∂(2K*D22)/∂y
        if (!used_vectorized_D11_D22 && j0 < j1 && i0 < i1) {
            auto tau22_s = 2.0f * xkmh.slice(0, j0 - 1, j1 - 1).slice(2, i0, i1) *
                                  defor22.slice(0, j0 - 1, j1 - 1).slice(2, i0, i1);
            auto tau22_n = 2.0f * xkmh.slice(0, j0, j1).slice(2, i0, i1) *
                                  defor22.slice(0, j0, j1).slice(2, i0, i1);
            auto msfvy_s = msfvy_3d.slice(0, j0, j1).slice(2, i0, i1);
            auto dtau22_dy = msfvy_s * (use_double_rdx ? rdy_d : rdy_f) * (tau22_n - tau22_s);
            auto rho_v = rho.slice(0, j0 - 1, j1 - 1).slice(2, i0, i1);
            v_tend.slice(0, j0, j1).slice(2, i0, i1).add_(dtau22_dy / rho_v);
        }

        // D23: ∂(K*D23)/∂z (k=1..nz-2)
        if (!used_vectorized_D23 && defor23.defined() && nz > 2) {
            int k0 = 1, k1 = nz - 1;
            std::vector<float> rdz_vals(k1 - k0);
            for (int k = k0; k < k1; ++k) {
                rdz_vals[k - k0] = 0.5f * (dnw_cache.safe_inv_dn(k - 1) + dnw_cache.safe_inv_dn(k));
            }
            auto rdz_t = torch::from_blob(rdz_vals.data(), {(int)rdz_vals.size()},
                torch::TensorOptions().dtype(torch::kFloat32))
                .to(v_tend.device()).reshape({1, k1 - k0, 1});

            auto Kv_below = 0.5f * (xkmv.slice(0, j0 - 1, j1 - 1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1) +
                                     xkmv.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1));
            auto Kv_above = 0.5f * (xkmv.slice(0, j0 - 1, j1 - 1).slice(1, k0, k1).slice(2, i0, i1) +
                                     xkmv.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1));
            auto tau23_below = Kv_below * defor23.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1);
            auto tau23_above = Kv_above * defor23.slice(0, j0, j1).slice(1, k0 + 1, k1 + 1).slice(2, i0, i1);
            auto dtau23_dz = rdz_t * (tau23_above - tau23_below);
            auto rho_v = rho.slice(0, j0 - 1, j1 - 1).slice(1, k0, k1).slice(2, i0, i1);
            v_tend.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1).add_(dtau23_dz / rho_v);
        }
    }

    // ===== W-momentum stress divergence =====
    // Interior: j=1..ny-2, k=1..nz-1, i=1..nx-2
    if (nz > 1) {
        int j0 = 1, j1 = ny - 1;
        int k0 = 1, k1 = nz;
        int i0 = 1, i1 = nx - 1;

        // D13 for W: ∂(K*D13)/∂x
        if (!used_vectorized_D13_W && defor13.defined() && j0 < j1 && i0 < i1) {
            auto K13_w = 0.5f * (xkmh.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0 - 1, i1 - 1) +
                                  xkmh.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0 - 1, i1 - 1));
            auto K13_e = 0.5f * (xkmh.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1) +
                                  xkmh.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1));
            auto tau13_w = K13_w * defor13.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1);
            auto tau13_e = K13_e * defor13.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0 + 1, i1 + 1);
            auto msftx_s = msftx_3d.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            auto dtau13_dx = msftx_s * (use_double_rdx ? rdx_d : rdx_f) * (tau13_e - tau13_w);
            auto rho_w = rho.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            w_tend.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1).add_(dtau13_dx / rho_w);
        }

        // D23 for W: ∂(K*D23)/∂y
        if (!used_vectorized_D23_W && defor23.defined() && j0 < j1 && i0 < i1) {
            auto K23_s = 0.5f * (xkmh.slice(0, j0 - 1, j1 - 1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1) +
                                  xkmh.slice(0, j0 - 1, j1 - 1).slice(1, k0, k1).slice(2, i0, i1));
            auto K23_n = 0.5f * (xkmh.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1) +
                                  xkmh.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1));
            auto tau23_s = K23_s * defor23.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1);
            auto tau23_n = K23_n * defor23.slice(0, j0 + 1, j1 + 1).slice(1, k0, k1).slice(2, i0, i1);
            auto msfty_s = msfty_3d.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            auto dtau23_dy = msfty_s * (use_double_rdx ? rdy_d : rdy_f) * (tau23_n - tau23_s);
            auto rho_w = rho.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            w_tend.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1).add_(dtau23_dy / rho_w);
        }

        // D33: ∂(2K*D33)/∂z
        {
            // Build rdz tensor: 1/dn[k-1] for k=1..nz-1
            std::vector<float> rdz_vals(k1 - k0);
            for (int k = k0; k < k1; ++k) {
                rdz_vals[k - k0] = dn_cache.safe_inv_dn(k - 1);
            }
            auto rdz_t = torch::from_blob(rdz_vals.data(), {(int)rdz_vals.size()},
                torch::TensorOptions().dtype(torch::kFloat32))
                .to(w_tend.device()).reshape({1, k1 - k0, 1});

            auto tau33_below = 2.0f * xkmv.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1) *
                                      defor33.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            auto tau33_above = 2.0f * xkmv.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1) *
                                      defor33.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1);
            auto dtau33_dz = rdz_t * (tau33_above - tau33_below);
            auto rho_w = rho.slice(0, j0, j1).slice(1, k0 - 1, k1 - 1).slice(2, i0, i1);
            w_tend.slice(0, j0, j1).slice(1, k0, k1).slice(2, i0, i1).add_(dtau33_dz / rho_w);
        }
    }
}

void applyTerrainSlopeCorrectionWRF(torch::Tensor& pgf_x,
                                   torch::Tensor& pgf_y,
                                   const torch::Tensor& pgf_z,
                                   const WRFGridInfoExtended& grid) {
    // TODO: Implement terrain slope correction following WRF
}

/**
 * @brief Initialize terrain slope arrays from WRF data
 * 
 * Converts float arrays from WRF to torch tensors for GPU/CPU compatibility
 * 
 * @param sin_ax Sine of terrain slope angle in x-direction
 * @param sin_ay Sine of terrain slope angle in y-direction  
 * @param cos_ax Cosine of terrain slope angle in x-direction
 * @param cos_ay Cosine of terrain slope angle in y-direction
 * @param nx Grid dimension in x
 * @param ny Grid dimension in y
 */
void WRFGridInfoExtended::initializeTerrainSlope(const float* sin_ax, const float* sin_ay,
                                                const float* cos_ax, const float* cos_ay,
                                                int nx, int ny) {
    // FIX Round181: Use make_cpu_from_blob_opts() for lint_from_blob consistency
    // Previously used manual opts which bypassed the lint target.

    // FIX 2025-12-28: Changed from new torch::Tensor(...) to direct assignment
    // This fixes memory leak from repeated calls without delete.
    // torch::Tensor assignment properly handles reference counting.

    // FIX 2025-12-29 Issue 4: Reset to undefined when pointer is NULL
    // This prevents stale values from persisting when terrain slope is disabled.
    // Also reset derived staggered tensors to force re-interpolation.

    // Convert float arrays to torch tensors (2D arrays)
    // Note: WRF uses (j,i) indexing but we store as [ny, nx] for consistency
    if (sin_ax != nullptr) {
        this->sin_alpha_x = torch::from_blob(const_cast<float*>(sin_ax),  // LINT_EXCEPTION: CPU opts inline
                                             {ny, nx}, make_cpu_from_blob_opts()).clone();
    } else {
        this->sin_alpha_x = torch::Tensor();  // Reset to undefined
        this->sin_alpha_x_u = torch::Tensor();  // Reset derived staggered tensor
    }

    if (sin_ay != nullptr) {
        this->sin_alpha_y = torch::from_blob(const_cast<float*>(sin_ay),  // LINT_EXCEPTION: CPU opts inline
                                             {ny, nx}, make_cpu_from_blob_opts()).clone();
    } else {
        this->sin_alpha_y = torch::Tensor();  // Reset to undefined
        this->sin_alpha_y_v = torch::Tensor();  // Reset derived staggered tensor
    }

    if (cos_ax != nullptr) {
        this->cos_alpha_x = torch::from_blob(const_cast<float*>(cos_ax),  // LINT_EXCEPTION: CPU opts inline
                                             {ny, nx}, make_cpu_from_blob_opts()).clone();
    } else {
        this->cos_alpha_x = torch::Tensor();  // Reset to undefined
        this->cos_alpha_x_u = torch::Tensor();  // Reset derived staggered tensor
    }

    if (cos_ay != nullptr) {
        this->cos_alpha_y = torch::from_blob(const_cast<float*>(cos_ay),  // LINT_EXCEPTION: CPU opts inline
                                             {ny, nx}, make_cpu_from_blob_opts()).clone();
    } else {
        this->cos_alpha_y = torch::Tensor();  // Reset to undefined
        this->cos_alpha_y_v = torch::Tensor();  // Reset derived staggered tensor
    }
}

} // namespace sdirk3
} // namespace wrf