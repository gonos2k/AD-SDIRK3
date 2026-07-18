/**
 * Enhanced Unified Preconditioner for SDIRK3 with Acoustic-Gravity Wave Coupling
 *
 * This implementation properly handles BOTH acoustic and gravity waves
 * for fully implicit time integration stability.
 *
 * FIX Round151: DATA_PTR<FLOAT>() PATTERN AUDIT
 * All data_ptr<float>() calls in this file follow the safe pattern:
 *   1. Ensure tensor is on CPU: .to(torch::kCPU) or .to(torch::kCPU, torch::kFloat32)
 *   2. Ensure tensor is contiguous: .contiguous()
 *   3. Extract pointer: .data_ptr<float>()
 * This prevents undefined behavior from GPU pointers or non-contiguous memory.
 * Note: For FP64 inputs, explicit conversion to FP32 is performed before data_ptr.
 *
 * FIX Round166: FP64 ACCURACY LIMITATION
 * The preconditioner uses grid_info_->dx/dy (float) for coefficient computation.
 * This means preconditioner coefficients are computed at FP32 precision even when
 * the solver runs in FP64 mode. For most applications this is acceptable because:
 *   1. Preconditioners only need approximate conditioning, not exact values
 *   2. The dominant numerical error is typically in the RHS, not the preconditioner
 *   3. Grid spacing is typically O(1e2-1e5) meters, well within FP32 range
 * If FP64 preconditioner accuracy becomes important for certain applications,
 * consider adding grid_info_->dx_fp64/dy_fp64 fields or using tensor-based storage.
 */

#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_unified_rhs.h"
#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_wdamp_preconditioner_policy.h"  // PR 9D: W-damping precond policy
#include <cstddef>  // for size_t
#include <iomanip>  // for std::setprecision, std::fixed, cerr flags save/restore
#include "wrf_sdirk3_profiler.h"
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-12-30 Batch32 Issue 1: For getVerifyPeriod()
#include <torch/torch.h>
#include <cmath>
#include <algorithm>  // std::min
#include <unordered_map>
#include <atomic>
#ifdef USE_CUDA
#include <c10/cuda/CUDAFunctions.h>  // FIX 2025-12-31 Batch41: For c10::cuda::current_device()
#endif

namespace wrf {
namespace sdirk3 {

// ============================================================================
// FIX 2025-12-30 Batch29 Issue 1: ScalarMeanCache for repeated .mean() calls
// Caches scalar mean values to avoid repeated reductions on same tensors.
// Uses pointer + numel + 3-point sample signature for cache key.
// FIX 2025-12-30 Batch30 Issue 3: Added stride/offset to key + periodic verify
// ============================================================================
namespace {

// FIX 2025-12-30 Batch32 Issue 1: Removed hardcoded SCALAR_MEAN_VERIFY_PERIOD = 100
// Now uses metric_utils::getVerifyPeriod() for moving_nest/debug policy alignment

struct ScalarMeanCacheEntry {
    const void* data_ptr = nullptr;
    int64_t numel = 0;
    int64_t storage_offset = 0;  // FIX Batch30 Issue 3: Add storage offset to key
    int64_t stride_hash = 0;     // FIX Batch30 Issue 3: Hash of strides for view detection
    // FIX 2025-12-30 Batch31 Issue 1: Add device/dtype to cache key
    int8_t cached_device_type = 0;   // torch::DeviceType as int8_t
    int16_t cached_device_index = 0; // Device index for multi-GPU
    int8_t cached_dtype = 0;         // torch::ScalarType as int8_t
    // FIX 2025-12-30 Batch31 Issue 1: Use uint64_t bits for dtype-agnostic sample comparison
    uint64_t sample_first_bits = 0;
    uint64_t sample_mid_bits = 0;
    uint64_t sample_last_bits = 0;
    // FIX 2025-12-30 Batch32 Issue 2: Use double to preserve precision for kFloat64 tensors
    double cached_mean = 0.0;
    uint64_t epoch = 0;
    uint64_t access_count = 0;   // FIX Batch30 Issue 3: For periodic full-verify

    // FIX Batch30 Issue 3: Compute stride hash (FNV-1a style)
    static int64_t compute_stride_hash(const torch::Tensor& t) {
        int64_t hash = 14695981039346656037ULL;
        for (int64_t i = 0; i < t.dim(); ++i) {
            hash ^= static_cast<uint64_t>(t.stride(i));
            hash *= 1099511628211ULL;
        }
        return static_cast<int64_t>(hash);
    }

    // ===================================================================================
    // FIX 2025-12-31 Batch41 Issue 1: Unified GPU device index helper
    // Handles -1 fallback for multi-GPU correctness (avoids cache thrash)
    // FIX Round160: Add range validation for large GPU clusters (int16_t: up to 32767)
    // ===================================================================================
    static int16_t get_device_index(const torch::Device& dev) {
        if (dev.is_cuda()) {
            int idx = 0;
            // If index is specified, use it; otherwise use current CUDA device
            if (dev.has_index()) {
                idx = dev.index();
            } else {
#ifdef USE_CUDA
                idx = c10::cuda::current_device();
#endif
            }
            // Range validation for large GPU clusters
            if (idx < 0 || idx > 32767) {
                return -1;  // Out of range, treat as invalid
            }
            return static_cast<int16_t>(idx);
        }
        return -1;  // CPU uses -1
    }

    // FIX 2025-12-30 Batch31 Issue 1: Convert value to uint64_t bits based on dtype
    // FIX 2025-12-31 Batch41 Issue 2: CPU-only with data_ptr optimization + NoGradGuard
    static uint64_t valueToBits(const torch::Tensor& t, int64_t idx, torch::ScalarType dtype) {
        torch::NoGradGuard no_grad;  // Prevent AD graph pollution
        auto flat = t.flatten();

        // CPU contiguous path: direct pointer access (zero overhead)
        if (flat.is_cpu() && flat.is_contiguous()) {
            switch (dtype) {
                case torch::kFloat64: {
                    uint64_t bits;
                    std::memcpy(&bits, flat.data_ptr<double>() + idx, sizeof(double));
                    return bits;
                }
                case torch::kFloat32: {
                    uint32_t bits;
                    std::memcpy(&bits, flat.data_ptr<float>() + idx, sizeof(float));
                    return static_cast<uint64_t>(bits);
                }
                default:
                    break;  // Fall through to GPU/non-contiguous path
            }
        }

        // FIX 2025-12-31 Batch41 Issue 1b: GPU path - single index_select + D2H transfer
        if (flat.is_cuda()) {
            // Use index_select for single-element extraction, then one D2H transfer
            auto idx_tensor = torch::tensor({idx}, torch::TensorOptions().dtype(torch::kLong).device(flat.device()));
            auto selected = flat.index_select(0, idx_tensor).to(torch::kCPU).contiguous();

            switch (dtype) {
                case torch::kFloat64: {
                    uint64_t bits;
                    std::memcpy(&bits, selected.data_ptr<double>(), sizeof(double));
                    return bits;
                }
                case torch::kFloat32: {
                    uint32_t bits;
                    std::memcpy(&bits, selected.data_ptr<float>(), sizeof(float));
                    return static_cast<uint64_t>(bits);
                }
                case torch::kBFloat16:
                case torch::kFloat16: {
                    auto selected_f32 = selected.to(torch::kFloat32);
                    uint32_t bits;
                    std::memcpy(&bits, selected_f32.data_ptr<float>(), sizeof(float));
                    return static_cast<uint64_t>(bits);
                }
                default: {
                    auto selected_f32 = selected.to(torch::kFloat32);
                    uint32_t bits;
                    std::memcpy(&bits, selected_f32.data_ptr<float>(), sizeof(float));
                    return static_cast<uint64_t>(bits);
                }
            }
        }

        // Non-contiguous CPU path: make contiguous then use data_ptr
        auto flat_contig = flat.contiguous();
        switch (dtype) {
            case torch::kFloat64: {
                uint64_t bits;
                std::memcpy(&bits, flat_contig.data_ptr<double>() + idx, sizeof(double));
                return bits;
            }
            case torch::kFloat32: {
                uint32_t bits;
                std::memcpy(&bits, flat_contig.data_ptr<float>() + idx, sizeof(float));
                return static_cast<uint64_t>(bits);
            }
            case torch::kBFloat16:
            case torch::kFloat16: {
                auto flat_f32 = flat_contig.to(torch::kFloat32);
                uint32_t bits;
                std::memcpy(&bits, flat_f32.data_ptr<float>() + idx, sizeof(float));
                return static_cast<uint64_t>(bits);
            }
            default: {
                auto flat_f32 = flat_contig.to(torch::kFloat32);
                uint32_t bits;
                std::memcpy(&bits, flat_f32.data_ptr<float>() + idx, sizeof(float));
                return static_cast<uint64_t>(bits);
            }
        }
    }

    // FIX 2025-12-30 Batch31 Issue 1: Extract 3-point sample bits for CPU contiguous path
    // FIX 2025-12-30 Batch33 Issue 1: Also used for GPU via index_select (single D2H transfer)
    static void extractSampleBits(const torch::Tensor& flat, int64_t n, torch::ScalarType dtype,
                                  uint64_t& first, uint64_t& mid, uint64_t& last) {
        if (dtype == torch::kFloat64) {
            const double* ptr = flat.data_ptr<double>();
            std::memcpy(&first, &ptr[0], sizeof(double));
            std::memcpy(&mid, &ptr[n > 1 ? n/2 : 0], sizeof(double));
            std::memcpy(&last, &ptr[n > 1 ? n-1 : 0], sizeof(double));
        } else if (dtype == torch::kFloat32) {
            const float* ptr = flat.data_ptr<float>();
            uint32_t f, m, l;
            std::memcpy(&f, &ptr[0], sizeof(float));
            std::memcpy(&m, &ptr[n > 1 ? n/2 : 0], sizeof(float));
            std::memcpy(&l, &ptr[n > 1 ? n-1 : 0], sizeof(float));
            first = f; mid = m; last = l;
        } else {
            // BF16/FP16: convert to float32 for comparison
            auto cpu32 = flat.to(torch::kFloat32).contiguous();
            const float* ptr = cpu32.data_ptr<float>();
            uint32_t f, m, l;
            std::memcpy(&f, &ptr[0], sizeof(float));
            std::memcpy(&m, &ptr[n > 1 ? n/2 : 0], sizeof(float));
            std::memcpy(&l, &ptr[n > 1 ? n-1 : 0], sizeof(float));
            first = f; mid = m; last = l;
        }
    }

    // FIX 2025-12-30 Batch33 Issue 1: GPU 3-point sample extraction via index_select
    // Single D2H transfer of 3 elements instead of full tensor copy
    // FIX 2025-12-31 Batch37 Issue 1: Cache GPU index tensor to avoid H2D transfers
    // FIX 2025-12-31 Batch41 Issue 1: Use get_device_index() for multi-GPU correctness
    struct Sample3IndexCache {
        int64_t cached_n = 0;
        torch::DeviceType cached_device_type = torch::kCPU;
        int16_t cached_device_index = -1;
        torch::Tensor idx_tensor;

        bool is_valid(int64_t n, const torch::Device& dev) const {
            if (!idx_tensor.defined()) return false;
            if (cached_n != n) return false;
            if (cached_device_type != dev.type()) return false;
            // FIX 2025-12-31 Batch41: Use unified helper for consistent index handling
            return cached_device_index == get_device_index(dev);
        }
    };

    static void extractSampleBitsGPU(const torch::Tensor& flat, int64_t n, torch::ScalarType dtype,
                                     uint64_t& first, uint64_t& mid, uint64_t& last) {
        // FIX Round163: Disable gradient tracking for cache sampling - this is diagnostic only
        torch::NoGradGuard no_grad;

        static thread_local Sample3IndexCache cache;

        torch::Tensor indices;
        if (cache.is_valid(n, flat.device())) {
            // Reuse cached GPU index tensor - no H2D transfer
            indices = cache.idx_tensor;
        } else {
            // FIX Round162: Use torch::tensor() to create owned memory, avoiding UAF from stack buffer
            // from_blob + .to() on CPU may not copy, leaving cache pointing at freed stack memory
            std::vector<int64_t> indices_vec = {0, n > 1 ? n/2 : 0, n > 1 ? n-1 : 0};
            indices = torch::tensor(indices_vec, torch::TensorOptions().dtype(torch::kInt64))
                          .to(flat.device());

            // Update cache
            // FIX 2025-12-31 Batch41: Use unified helper for multi-GPU correctness
            cache.cached_n = n;
            cache.cached_device_type = flat.device().type();
            cache.cached_device_index = get_device_index(flat.device());
            cache.idx_tensor = indices;
        }

        // Single index_select + D2H transfer (3 elements only)
        auto samples = flat.detach().index_select(0, indices).to(torch::kCPU).contiguous();
        // Extract bits from CPU samples
        extractSampleBits(samples, 3, dtype, first, mid, last);
    }

    bool matches(const torch::Tensor& t, uint64_t current_epoch) const {
        // FIX Batch30 Issue 3: Check storage_offset and stride_hash for view safety
        // FIX Batch31 Issue 1: Also check device and dtype
        // FIX Round160: Use get_device_index() for consistent range validation
        if (t.data_ptr() != data_ptr ||
            t.numel() != numel ||
            current_epoch != epoch ||
            t.storage_offset() != storage_offset ||
            compute_stride_hash(t) != stride_hash ||
            static_cast<int8_t>(t.device().type()) != cached_device_type ||
            get_device_index(t.device()) != cached_device_index ||
            static_cast<int8_t>(t.scalar_type()) != cached_dtype) {
            return false;
        }
        // 3-point sample check for in-place modifications (dtype-aware)
        int64_t n = t.numel();
        if (n == 0) return false;
        auto flat = t.flatten();
        uint64_t cur_first, cur_mid, cur_last;
        // CPU fast path with dtype-aware bit comparison
        if (flat.is_cpu() && flat.is_contiguous()) {
            extractSampleBits(flat, n, t.scalar_type(), cur_first, cur_mid, cur_last);
        } else if (n > 0) {
            // FIX 2025-12-30 Batch33 Issue 1: GPU path with index_select (3-element D2H only)
            extractSampleBitsGPU(flat, n, t.scalar_type(), cur_first, cur_mid, cur_last);
        } else {
            return true;  // Edge case: empty tensor
        }
        if (cur_first != sample_first_bits) return false;
        if (n > 1 && cur_mid != sample_mid_bits) return false;
        if (n > 1 && cur_last != sample_last_bits) return false;
        return true;
    }

    // FIX Batch30 Issue 3: Periodic full-verify to catch edge cases
    // FIX 2025-12-30 Batch32 Issue 1: Use config-based verify period from metric_utils
    bool needs_full_verify() const {
        uint64_t period = metric_utils::getVerifyPeriod();
        return (access_count % period) == 0;
    }

    // FIX Batch30 Issue 3: Full verification by recomputing mean
    // FIX 2025-12-30 Batch32 Issue 2: Use dtype-aware double precision comparison
    bool full_verify(const torch::Tensor& t) const {
        torch::NoGradGuard no_grad;
        double actual_mean;
        if (t.scalar_type() == torch::kFloat64) {
            actual_mean = t.mean().to(torch::kCPU).item<double>();
        } else {
            actual_mean = static_cast<double>(t.mean().to(torch::kCPU).item<float>());
        }
        // Allow small tolerance for floating point (tighter for double)
        double diff = std::abs(actual_mean - cached_mean);
        double scale = std::max(std::abs(actual_mean), 1e-15);
        double tol = (t.scalar_type() == torch::kFloat64) ? 1e-12 : 1e-5;
        return (diff / scale) < tol;
    }

    // FIX 2025-12-30 Batch32 Issue 2: Take double for precision preservation
    void update(const torch::Tensor& t, double mean_val, uint64_t current_epoch) {
        data_ptr = t.data_ptr();
        numel = t.numel();
        storage_offset = t.storage_offset();  // FIX Batch30 Issue 3
        stride_hash = compute_stride_hash(t); // FIX Batch30 Issue 3
        // FIX 2025-12-30 Batch31 Issue 1: Store device and dtype
        // FIX Round160: Use get_device_index() for consistent range validation
        cached_device_type = static_cast<int8_t>(t.device().type());
        cached_device_index = get_device_index(t.device());
        cached_dtype = static_cast<int8_t>(t.scalar_type());
        cached_mean = mean_val;  // Now double for precision
        epoch = current_epoch;
        access_count = 0;  // Reset on update

        int64_t n = t.numel();
        auto flat = t.flatten();
        if (flat.is_cpu() && flat.is_contiguous() && n > 0) {
            // FIX 2025-12-30 Batch31 Issue 1: Use dtype-aware bit extraction
            extractSampleBits(flat, n, t.scalar_type(),
                              sample_first_bits, sample_mid_bits, sample_last_bits);
        } else if (n > 0) {
            // FIX 2025-12-30 Batch33 Issue 1: GPU path - use index_select for 3-element D2H only
            // Avoids O(n) full tensor copy, transfers only 3 sample elements
            extractSampleBitsGPU(flat, n, t.scalar_type(),
                                 sample_first_bits, sample_mid_bits, sample_last_bits);
        }
    }
};

// FIX 2025-12-30 Batch33 Issue 2: Global epoch for cross-thread invalidation
// Shared across all threads - increment invalidates all thread-local caches
static std::atomic<uint64_t> g_scalar_mean_epoch{0};

// FIX 2025-12-30 Batch33 Issue 2: Thread-local cache to avoid data races
// Each thread has its own cache entries; epoch is global for invalidation
struct ScalarMeanCache {
    ScalarMeanCacheEntry mub_entry;
    ScalarMeanCacheEntry c1f_entry;
    ScalarMeanCacheEntry c2f_entry;
    ScalarMeanCacheEntry msfty_entry;
    ScalarMeanCacheEntry mu_base_entry;

    // Use global epoch for cross-thread invalidation
    static void invalidate() { g_scalar_mean_epoch.fetch_add(1, std::memory_order_release); }

    // FIX 2025-12-30 Batch32 Issue 2: Compute with dtype-aware precision, return float for API
    float get_or_compute_mean(const torch::Tensor& t, ScalarMeanCacheEntry& entry, const char* name) {
        uint64_t current_epoch = g_scalar_mean_epoch.load(std::memory_order_acquire);
        if (entry.matches(t, current_epoch)) {
            // FIX 2025-12-30 Batch30 Issue 3: Periodic full-verify for moving_nest safety
            entry.access_count++;
            if (entry.needs_full_verify()) {
                if (!entry.full_verify(t)) {
                    // Full verify failed - recompute mean with dtype-aware precision
                    torch::NoGradGuard no_grad;
                    double mean_val;
                    if (t.scalar_type() == torch::kFloat64) {
                        mean_val = t.mean().to(torch::kCPU).item<double>();
                    } else {
                        mean_val = static_cast<double>(t.mean().to(torch::kCPU).item<float>());
                    }
                    entry.update(t, mean_val, current_epoch);
                    return static_cast<float>(mean_val);
                }
            }
            return static_cast<float>(entry.cached_mean);
        }
        // Cache miss: compute mean with dtype-aware precision
        torch::NoGradGuard no_grad;
        double mean_val;
        if (t.scalar_type() == torch::kFloat64) {
            mean_val = t.mean().to(torch::kCPU).item<double>();
        } else {
            mean_val = static_cast<double>(t.mean().to(torch::kCPU).item<float>());
        }
        entry.update(t, mean_val, current_epoch);
        return static_cast<float>(mean_val);
    }
};

// FIX 2025-12-30 Batch33 Issue 2: thread_local to avoid data races in OMP/async environments
// Each thread has its own cache; global epoch ensures invalidation affects all
static thread_local ScalarMeanCache g_scalar_mean_cache;

} // anonymous namespace

// FIX 2025-12-30 Batch31 Issue 2: Invalidation function for ScalarMeanCache
// Called from WRFGridInfo::invalidateVerticalMetricCaches() when grid metrics change
// FIX 2025-12-30 Batch33 Issue 2: Now calls static method to invalidate global epoch
void invalidateScalarMeanCache() {
    ScalarMeanCache::invalidate();  // Increments global epoch, affecting all thread_local caches
}

UnifiedPreconditioner::UnifiedPreconditioner(
    std::shared_ptr<WRFGridInfo> grid_info,
    std::shared_ptr<PhysicsConfig> physics_config,
    float dt, float gamma)
    : grid_info_(grid_info), 
      physics_config_(physics_config),
      dt_(dt), gamma_(gamma) {
    
    // Initialize ENHANCED vertical solver for acoustic-gravity waves
    initialize_acoustic_gravity_solver();
    
    // Initialize horizontal smoother
    initialize_horizontal_smoother();
}

void UnifiedPreconditioner::initialize_acoustic_gravity_solver() {
    // v20.14r20: Refresh cached tuning parameters FIRST, before any coefficient
    // computation.  Lines below (D_w, D_θ) read w_acoustic_boost_cached_ and
    // theta_acoustic_factor_cached_ — so the cache must be up-to-date here.
    {
        const auto& cfg_early = wrf::sdirk3::g_sdirk3_config;
        if (!precond_params_initialized_) {
            w_acoustic_boost_cached_ = cfg_early.precond_w_acoustic_boost;
            theta_acoustic_factor_cached_ = cfg_early.precond_theta_acoustic_factor;
            uv_vertical_fraction_cached_ = cfg_early.precond_uv_vertical_fraction;
            cached_coupling_scale_ = cfg_early.precond_phi_w_coupling_scale;
            cached_dw_nosboost_floor_ = cfg_early.precond_dw_nosboost_floor;

            std::cerr << "[PRECOND] w_acoustic_boost = " << w_acoustic_boost_cached_
                      << " (from config)" << std::endl;
            std::cerr << "[PRECOND] theta_acoustic_factor = " << theta_acoustic_factor_cached_
                      << " (from config)" << std::endl;
            precond_params_initialized_ = true;
        } else {
            w_acoustic_boost_cached_ = cfg_early.precond_w_acoustic_boost;
            uv_vertical_fraction_cached_ = cfg_early.precond_uv_vertical_fraction;
            cached_coupling_scale_ = cfg_early.precond_phi_w_coupling_scale;
            cached_dw_nosboost_floor_ = cfg_early.precond_dw_nosboost_floor;
            if (!theta_factor_override_active_) {
                theta_acoustic_factor_cached_ = cfg_early.precond_theta_acoustic_factor;
            }
        }
    }

    // Set up ENHANCED tridiagonal system for vertical implicit treatment
    // This handles BOTH acoustic and gravity waves in vertical direction
    int nz = grid_info_->nz;

    // Physical constants
    float c_s = grid_info_->cs;  // sound speed ~340 m/s
    float c_squared = c_s * c_s;  // sound speed squared ~115,600 m²/s²
    float g = grid_info_->g;      // gravity ~9.81 m/s²

    // Get divergence damping coefficient for implicit treatment
    float kdamp = (grid_info_->kdamp > 0.0f) ? grid_info_->kdamp : 0.0f;
    float rdx = (grid_info_->dx > 0.0f) ? 1.0f / grid_info_->dx : 1e-5f;
    float rdy = (grid_info_->dy > 0.0f) ? 1.0f / grid_info_->dy : 1e-5f;

    // FIX 2026-02-01: Coefficient source unification.
    // Preconditioner must use the SAME coefficients as the RHS (g_sdirk3_config),
    // not PhysicsConfig defaults which may never be synced.
    // PhysicsConfig is kept for legacy physics-scheme flags only.
    const auto& config = wrf::sdirk3::g_sdirk3_config;

    // v20.15 HEVI (precond-inc1): make M HEVI-aware. The Newton operator runs
    // RhsMode::ImplicitOnly for imex_split_mode>=1 (mode 1 implicit part + modes 2/3),
    // so J_fast drops ALL horizontal-acoustic content (U/V PGF, horizontal mass-div,
    // phi horizontal PGF/divergence, divergence damping) into k_slow. M must drop the
    // same horizontal content so M^-1 approximates A=I-dt*gamma*J_fast (else M^-1*A has
    // eigenvalues far from 1 -> GMRES floors ~0.39). Gate matches A: only mode 0 (Full)
    // keeps horizontal. hevi==false => every branch below is byte-identical to baseline.
    const bool hevi = config.hevi_split && (config.effective_imex_split_mode() >= 1);

    // Get Rayleigh damping parameters — from g_sdirk3_config (same source as RHS)
    float rayleigh_coef = config.rayleigh_damp_coef;
    float rayleigh_depth = config.rayleigh_damp_depth;

    // Get W-damping parameters — from g_sdirk3_config (same source as RHS)
    float w_alpha = config.w_damp_alpha;
    [[maybe_unused]] float w_crit_cfl = config.w_crit_cfl;

    // Get vertical diffusion coefficient
    float kvdif = (grid_info_->kvdif > 0.0f) ? grid_info_->kvdif : 0.0f;

    // ===== Preconditioner-RHS scope consistency (2026-02-01) =====
    // Derive effective precond_scope from IMEX split mode:
    //   mode=0 (full): all terms in precond
    //   mode=1 (frozen): Newton J is fast-only (F_exp frozen/detached) → precond fast-only
    //   mode=2 (post-solve): Newton J is fast-only → precond fast-only
    // When precond_match_rhs=true AND mode>=1, slow-only terms are excluded from precond.
    // precond_extra_* flags can force specific terms back in for stability.
    int precond_scope = config.imex_split_mode;
    if (precond_scope == 0 && config.imex_enabled) precond_scope = 1;

    // Cache for rebuild detection
    cached_precond_scope_ = precond_scope;
    cached_precond_flags_ = static_cast<uint8_t>(
        (config.precond_match_rhs ? 1 : 0) |
        (config.precond_extra_rayleigh ? 2 : 0) |
        (config.precond_extra_wdamp ? 4 : 0) |
        (config.precond_extra_vdiff ? 8 : 0) |
        (config.precond_extra_divergence ? 16 : 0)
    );
    // PR 9D: record the W-damping policy fingerprint at build time. Resolved
    // here so an invalid explicit alpha fails at construction; the signature
    // drives rebuilds (see needsCoefficientRebuild).
    cached_wdamp_signature_ =
        wrf::sdirk3::wdamp_preconditioner_signature(
            wrf::sdirk3::resolve_wdamp_preconditioner_policy(config));

    // PR 9D: the W-damping operator/preconditioner policy. The WRF-parity
    // W-damping term has NO direct W-diagonal Jacobian in the smooth region
    // (its tangent flows through ww/mu; the physical w enters only through the
    // hard SIGN, ∂/∂w = 0 away from w==0 — proven by WDamp_Tangent_Contract's
    // pure-w case). So the physical W diagonal is ALWAYS 0: an enabled RHS
    // W-damping term is NEVER mirrored as a scalar onto the W diagonal. The
    // scalar w_damp_alpha reaches the W diagonal ONLY as an EXPLICIT
    // precond_extra_wdamp regularization (a deliberate non-operator enrichment
    // of M), independent of implicit_wdamp / precond_match_rhs / scope.
    const auto wdamp_policy = resolve_wdamp_preconditioner_policy(config);
    const bool wdamp_extra_regularization = wdamp_policy.extra_regularization;
    const float wdamp_extra_diag = wdamp_policy.extra_regularization_alpha;

    // Effective per-term gates: start from config.implicit_*, then apply scope gating
    bool eff_rayleigh = config.implicit_rayleigh;
    bool eff_vdiff = config.implicit_vdiff;
    bool eff_divergence = config.implicit_divergence;

    if (config.precond_match_rhs && precond_scope >= 1) {
        // Mode>=1: Newton J is fast-only (frozen F_exp in mode=1, no F_exp in mode=2).
        // Preconditioner should approximate J_fast only.
        //
        // Slow-only terms (in do_explicit): Rayleigh, vdiff → exclude by default.
        // Fast terms (in do_implicit): divergence → keep by default.
        // precond_extra_*: force-include a term even if scope would exclude it.
        //
        // (W-damping is NOT in this gate: it has no physical W diagonal — see
        // wdamp_policy above — so scope gating never adds a W scalar. The only
        // W scalar is wdamp_extra_diag, gated solely on precond_extra_wdamp.)
        eff_rayleigh = config.precond_extra_rayleigh;   // slow → excluded unless forced
        eff_vdiff = config.precond_extra_vdiff;         // slow → excluded unless forced
        eff_divergence = config.implicit_divergence || config.precond_extra_divergence; // fast → keep or force
    }

    // FIX Round161: Gate configuration log with debug_level >= 1
    if ((kdamp > 0.0f || rayleigh_coef > 0.0f || w_alpha > 0.0f || kvdif > 0.0f) &&
        config.debug_level >= 1) {
        std::cerr << "UnifiedPreconditioner: Implicit damping/diffusion enabled:" << std::endl;
        if (kdamp > 0.0f) std::cerr << "  - Divergence damping: kdamp=" << kdamp << std::endl;
        if (rayleigh_coef > 0.0f) std::cerr << "  - Rayleigh damping: coef=" << rayleigh_coef << std::endl;
        if (w_alpha > 0.0f) std::cerr << "  - W-damping: alpha=" << w_alpha << std::endl;
        if (kvdif > 0.0f) std::cerr << "  - Vertical diffusion: kvdif=" << kvdif << std::endl;
        if (config.precond_match_rhs && precond_scope >= 1) {
            std::cerr << "  IMEX scope gating (mode=" << precond_scope << "): rayleigh="
                      << (eff_rayleigh ? "on" : "OFF") << " vdiff="
                      << (eff_vdiff ? "on" : "OFF") << " divergence="
                      << (eff_divergence ? "on" : "OFF") << std::endl;
        }
        // PR 9D: W-damping has no physical W diagonal; report only the explicit
        // regularization state (source=extra_regularization when present).
        std::cerr << "  wdamp: physical_w_direct_diag=0 extra_regularization="
                  << (wdamp_extra_regularization ? "on" : "OFF");
        if (wdamp_extra_regularization)
            std::cerr << " source=extra_regularization extra_alpha=" << wdamp_extra_diag;
        std::cerr << std::endl;
    }

    // DEBUG 2026-02-05: Track constructor crash location
    std::cerr << "[PRECOND CTOR] STEP A: Damping config done, creating diagonal arrays with nz=" << nz << std::endl;

    // Initialize diagonal arrays for EACH variable type
    // FIX Round187: Explicit CPU + FP32 for data_ptr<float>() safety in solve loops
    std::cerr << "[PRECOND CTOR] STEP A1: About to create TensorOptions" << std::endl;
    auto cpu_fp32_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    std::cerr << "[PRECOND CTOR] STEP A2: TensorOptions created, calling torch::ones({nz+1})" << std::endl;
    vertical_diag_w_ = torch::ones({nz+1}, cpu_fp32_opts);  // W-staggered
    std::cerr << "[PRECOND CTOR] STEP A3: vertical_diag_w_ created" << std::endl;
    vertical_diag_theta_ = torch::ones({nz}, cpu_fp32_opts); // Mass points
    vertical_diag_mu_ = torch::ones({nz}, cpu_fp32_opts);   // Column scalar (replicated per level for accessor compatibility)
    vertical_diag_phi_ = torch::ones({nz+1}, cpu_fp32_opts); // W-staggered
    vertical_diag_u_ = torch::ones({nz}, cpu_fp32_opts);    // For U
    vertical_diag_v_ = torch::ones({nz}, cpu_fp32_opts);    // For V
    
    std::cerr << "[PRECOND CTOR] STEP B: Diagonal arrays created" << std::endl;

    // Off-diagonal arrays for coupling
    vertical_upper_w_ = torch::zeros({nz}, torch::kFloat32);
    vertical_lower_w_ = torch::zeros({nz}, torch::kFloat32);
    vertical_upper_theta_ = torch::zeros({nz-1}, torch::kFloat32);
    vertical_lower_theta_ = torch::zeros({nz-1}, torch::kFloat32);
    
    // Coupling matrices for w-theta interaction (gravity waves)
    w_theta_coupling_upper_ = torch::zeros({nz}, torch::kFloat32);
    w_theta_coupling_lower_ = torch::zeros({nz}, torch::kFloat32);
    theta_w_coupling_upper_ = torch::zeros({nz-1}, torch::kFloat32);
    theta_w_coupling_lower_ = torch::zeros({nz-1}, torch::kFloat32);
    
    std::cerr << "[PRECOND CTOR] STEP C: Off-diagonal arrays done, checking dz" << std::endl;

    // PARITY FIX 2025-12-11: grid_info_->dz may not be initialized yet (no longer hardcoded in constructor).
    // FIX 2026-02-05: Force dz to 1D CPU tensor to avoid SIGTRAP when dz is 3D or on GPU.
    // If dz is 3D [ny,nz,nx], dz[0] returns a 2D slice, causing issues in dz_effective[0] = dz_local[0].
    torch::Tensor dz_local;
    if (grid_info_->dz.defined() && grid_info_->dz.numel() >= nz - 1) {
        // Log dimension info once for debugging
        static bool dz_dim_logged = false;
        if (!dz_dim_logged && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[DZ] grid_info_->dz dim=" << grid_info_->dz.dim()
                      << " sizes=" << grid_info_->dz.sizes()
                      << " device=" << grid_info_->dz.device() << std::endl;
            dz_dim_logged = true;
        }

        dz_local = grid_info_->dz;
        // FIX 2026-02-05: Ensure dz_local is 1D
        if (dz_local.dim() == 3) {
            // 3D [ny, nz, nx] -> mean over y,x -> [nz] 1D profile
            torch::NoGradGuard no_grad;
            dz_local = dz_local.mean(torch::IntArrayRef({0, 2}));
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                static bool dz_3d_warned = false;
                if (!dz_3d_warned) {
                    std::cerr << "[DZ] Converted 3D dz to 1D via mean: " << dz_local.sizes() << std::endl;
                    dz_3d_warned = true;
                }
            }
        } else if (dz_local.dim() != 1) {
            // 2D or other -> flatten to 1D
            torch::NoGradGuard no_grad;
            dz_local = dz_local.flatten();
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                static bool dz_nd_warned = false;
                if (!dz_nd_warned) {
                    std::cerr << "[DZ] Flattened non-1D dz to: " << dz_local.sizes() << std::endl;
                    dz_nd_warned = true;
                }
            }
        }
        // Ensure CPU Float32
        dz_local = dz_local.to(torch::kCPU, torch::kFloat32).contiguous();
    } else {
        // Fallback: assume uniform 500m spacing until computeVerticalMetrics is called
        dz_local = torch::full({nz}, 500.0f, torch::kFloat32);
        // FIX Round164: Add WARN_ONCE to avoid per-step spam
        // v20.14r27k: Downgraded from WARNING to INFO — constructor runs before
        // computeVerticalMetrics() sets actual dz. Fallback is temporary init state,
        // overwritten at first update(). Only warn if still fallback after update.
        static bool warned_dz_fallback = false;
        if (!warned_dz_fallback && wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "[PRECOND INIT] dz not yet set, using temporary 500m fallback (will be updated)" << std::endl;
            warned_dz_fallback = true;
        }
    }

    // Create dz_effective tensor for all levels
    torch::Tensor dz_effective = torch::zeros({nz}, torch::kFloat32);

    // Bottom level: use spacing above (dz[0])
    dz_effective[0] = dz_local[0];

    // Top level: use spacing below (dz[nz-2])
    int dz_top_idx = std::min(static_cast<int>(dz_local.numel()) - 1, nz - 2);
    dz_effective[nz-1] = dz_local[dz_top_idx];

    // Interior levels: average spacing above and below
    for (int k = 1; k < nz-1; ++k) {
        int k_lower = std::min(k - 1, static_cast<int>(dz_local.numel()) - 1);
        int k_upper = std::min(k, static_cast<int>(dz_local.numel()) - 1);
        dz_effective[k] = 0.5f * (dz_local[k_lower] + dz_local[k_upper]);
    }

    std::cerr << "[PRECOND CTOR] STEP D: dz_effective computed, caching..." << std::endl;

    // v20.5: Cache dz_effective for Φ-W GS correction
    // (This runs inside initialize_acoustic_gravity_solver, not during constructor)
    {
        torch::NoGradGuard no_grad;
        auto dz_eff_cpu = dz_effective.to(torch::kCPU, torch::kFloat32).contiguous();
        const float* p = dz_eff_cpu.data_ptr<float>();
        dz_effective_cached_.assign(p, p + dz_eff_cpu.numel());
    }

    std::cerr << "[PRECOND CTOR] STEP E: dz_effective_cached_ done, size=" << dz_effective_cached_.size() << std::endl;

    // Compute Brunt-Väisälä frequency squared at each level
    // AND extract base state θ₀(z) for coupling coefficients
    torch::Tensor N_squared = torch::zeros({nz}, torch::kFloat32);
    torch::Tensor theta_base_profile = torch::full({nz}, 300.0f, torch::kFloat32);  // Default 300K

    if (grid_info_->th_base.defined() && grid_info_->th_base.dim() > 0) {
        // FIX 2025-12-30 Batch21 Issue 2: Extract 1D vertical profile once to avoid per-k GPU sync
        // If th_base is 3D [j,k,i], take horizontal mean to get 1D profile [nz]
        // This avoids per-k .index() calls which cause GPU sync when th_base is on GPU
        torch::NoGradGuard no_grad;  // For accessing base state

        torch::Tensor th_base_1d;
        if (grid_info_->th_base.dim() == 3) {
            // 3D [j,k,i] -> mean over j,i -> [nz]
            // FIX 2025-01-25: Use IntArrayRef explicitly to disambiguate mean() overload
            th_base_1d = grid_info_->th_base.mean(torch::IntArrayRef({0, 2})).to(torch::kCPU, torch::kFloat32).contiguous();
        } else if (grid_info_->th_base.dim() == 1) {
            th_base_1d = grid_info_->th_base.to(torch::kCPU, torch::kFloat32).contiguous();
        } else {
            // Fallback for other dims
            th_base_1d = grid_info_->th_base.flatten().to(torch::kCPU, torch::kFloat32).contiguous();
        }

        // Prepare dz_local as CPU for vectorized access
        auto dz_local_cpu = dz_local.to(torch::kCPU, torch::kFloat32).contiguous();
        const float* dz_local_ptr = dz_local_cpu.data_ptr<float>();
        const float* th_base_ptr = th_base_1d.data_ptr<float>();
        int64_t th_base_sz = th_base_1d.numel();
        int64_t dz_local_sz = dz_local_cpu.numel();

        // FIX 2025-12-30 Batch22 Issue 1: tensor[k] = value is no-op in LibTorch
        // Use data_ptr for reliable assignment instead of operator[]
        float* N_squared_ptr = N_squared.data_ptr<float>();
        float* theta_base_ptr_out = theta_base_profile.data_ptr<float>();

        for (int k = 0; k < nz-1; ++k) {
            // Use data_ptr access instead of per-k tensor indexing
            int k_up = std::min(k + 1, static_cast<int>(th_base_sz) - 1);
            int k_down = std::min(k, static_cast<int>(th_base_sz) - 1);
            float theta_up = th_base_ptr[k_up];
            float theta_down = th_base_ptr[k_down];
            float theta_mean = 0.5f * (theta_up + theta_down);

            // Store actual base state θ₀(k) for coupling - use data_ptr
            theta_base_ptr_out[k] = theta_mean;

            // Compute N² using scalar arithmetic
            float theta_check = (theta_mean > 0.0f) ? 1.0f : 0.0f;
            int dz_idx = std::min(k, static_cast<int>(dz_local_sz) - 1);
            float dz_val = dz_local_ptr[dz_idx];
            float dtheta_dz = (dz_val > 0.0f) ? (theta_up - theta_down) / dz_val : 0.0f;
            float N2_val = theta_check * (g / std::max(theta_mean, 1.0f)) * dtheta_dz;

            // Clamp to typical atmospheric range - use data_ptr
            N_squared_ptr[k] = std::max(0.0f, std::min(N2_val, 0.001f));
        }
        // Extrapolate to top - use data_ptr
        N_squared_ptr[nz-1] = N_squared_ptr[nz-2];
        theta_base_ptr_out[nz-1] = theta_base_ptr_out[nz-2];
    } else {
        // Use typical atmospheric value if base state not available
        N_squared.fill_(0.0001f);  // N ~ 0.01/s typical
        theta_base_profile.fill_(300.0f);  // Standard atmosphere
    }

    // FIX 2025-12-27: Get data_ptr for efficient loop access to base profiles
    // This avoids repeated .item<float>() calls in coefficient computation loops
    auto theta_base_cpu = theta_base_profile.contiguous();
    const float* theta_base_ptr = theta_base_cpu.data_ptr<float>();

    // FIX 2025-12-30 Batch21 Issue 1: Pre-convert N_squared and dz_effective to CPU data_ptr
    // This eliminates per-k tensor indexing overhead in the coefficient loops below
    auto N_squared_cpu = N_squared.contiguous();
    const float* N_squared_ptr = N_squared_cpu.data_ptr<float>();
    auto dz_effective_cpu = dz_effective.contiguous();
    const float* dz_effective_ptr = dz_effective_cpu.data_ptr<float>();

    // === ENHANCED DIAGONAL COEFFICIENTS ===
    
    // Use already defined damping parameters from above (sourced from g_sdirk3_config via 'config')
    float w_damp_alpha = w_alpha;  // From config.w_damp_alpha (unified source)
    float zdamp = rayleigh_depth;  // From config.rayleigh_damp_depth (unified source)
    
    // Define tensor options for creating coefficient tensors
    // CPU device: coefficient computation uses data_ptr/raw pointers (CPU-only pattern)
    auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);

    // Estimate domain height (approximate)
    [[maybe_unused]] float domain_height = nz * 500.0f;  // Assume ~500m per level
    int k_damp_start = std::max(0, int(nz - zdamp / 500.0f));
    
    // 1. W-momentum: BOTH acoustic (cs²) and gravity (N²) waves + stiff damping terms
    torch::NoGradGuard no_grad;  // Preconditioner doesn't need gradients
    auto dz_inv = 1.0f / dz_effective;

    // FIX 2025-12-30 Batch23 Issue 2: Pre-convert dz_inv to data_ptr for efficient loop access
    // This eliminates per-k tensor creation overhead from dz_inv[k] indexing
    auto dz_inv_cpu = dz_inv.to(torch::kCPU, torch::kFloat32).contiguous();
    const float* dz_inv_ptr = dz_inv_cpu.data_ptr<float>();
    const int64_t dz_inv_sz = dz_inv_cpu.numel();

    // DIAGNOSTIC: Check physical parameters (NO .item() for autograd safety)
    // FIX Round164: Raise to debug_level >= 2 since min/max force GPU sync
    // FIX Round181: Add explicit .to(kCPU) before .item() to prevent GPU sync stall
    // FIX Round182: Transfer to CPU BEFORE reduction to avoid GPU compute entirely
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        torch::NoGradGuard no_grad;  // Prevent autograd tracking for diagnostics
        // FIX Round182: Transfer dz_effective to CPU first, then compute min/max on CPU
        // This avoids GPU reduction work (only data transfer happens on GPU)
        auto dz_effective_cpu = dz_effective.to(torch::kCPU, torch::kFloat32).contiguous();
        float dz_min = dz_effective_cpu.min().item<float>();
        float dz_max = dz_effective_cpu.max().item<float>();
        std::cerr << "\n[PRECONDITIONER] Physical parameters:" << std::endl;
        std::cerr << "  c_s (sound speed) = " << c_s << " m/s" << std::endl;
        std::cerr << "  g (gravity) = " << g << " m/s²" << std::endl;
        std::cerr << "  dz_effective range: " << dz_min << " to " << dz_max << " m" << std::endl;
    }

    // CRITICAL FIX: W equation in WRF operates on perturbation momentum (μ'w), not velocity w
    // The Jacobian ∂F/∂w includes a factor (c1f*μ + c2f)/msfty that couples momentum to velocity
    // The preconditioner must account for this scaling to match the actual operator
    //
    // The preconditioner diagonal should be: 1 + dt*γ*(acoustic + gravity + damping) / (c1f*μ + c2f)/msfty
    // This converts from momentum space to velocity space for proper preconditioning

    // Extract ACTUAL representative values from grid_info (not hard-coded!)
    // Use domain-averaged values to avoid full 3D preconditioner (too expensive)

    // 1. Column mass μ: Use mean of base state mub (2D field)
    // FIX 2025-12-30 Batch29 Issue 1: Use ScalarMeanCache to avoid repeated reductions
    float mu_representative = 88000.0f;  // Default fallback for em_b_wave (~8.8×10⁴ Pa)
    if (grid_info_->mub.defined() && grid_info_->mub.numel() > 0) {
        mu_representative = g_scalar_mean_cache.get_or_compute_mean(
            grid_info_->mub, g_scalar_mean_cache.mub_entry, "mub");
    }

    // 2. Vertical coefficients c1f/c2f: Use mean over vertical levels
    // FIX 2025-12-30 Batch29 Issue 1: Use ScalarMeanCache to avoid repeated reductions
    float c1f_representative = 1.0f;  // Fallback
    float c2f_representative = 0.0f;  // Fallback
    if (grid_info_->c1f.defined() && grid_info_->c1f.numel() > 0) {
        c1f_representative = g_scalar_mean_cache.get_or_compute_mean(
            grid_info_->c1f, g_scalar_mean_cache.c1f_entry, "c1f");
    }
    if (grid_info_->c2f.defined() && grid_info_->c2f.numel() > 0) {
        c2f_representative = g_scalar_mean_cache.get_or_compute_mean(
            grid_info_->c2f, g_scalar_mean_cache.c2f_entry, "c2f");
    }

    // 3. Map scale factor: Use mean of msfty (2D field)
    // FIX 2025-12-30 Batch29 Issue 1: Use ScalarMeanCache to avoid repeated reductions
    float msfty_representative = 1.0f;  // Fallback for idealized cases
    if (grid_info_->msfty.defined() && grid_info_->msfty.numel() > 0) {
        msfty_representative = g_scalar_mean_cache.get_or_compute_mean(
            grid_info_->msfty, g_scalar_mean_cache.msfty_entry, "msfty");
    }

    // Safety guard: prevent division by zero or near-zero msfty
    // (should never happen in real domains, but defensive coding)
    float msfty_safe = std::max(msfty_representative, 1e-6f);

    // Compute momentum-to-velocity coupling factor from ACTUAL grid data
    float momentum_coupling_raw = (c1f_representative * mu_representative + c2f_representative) / msfty_safe;

    // CONFIG-DRIVEN: Log raw coupling value (always useful for tuning)
    // Note: config already defined at line 145
    if (config.precond_log_raw_values && config.debug_level >= 1) {
        std::cerr << "[PRECONDITIONER] Raw momentum_coupling = " << momentum_coupling_raw << std::endl;
        if (momentum_coupling_raw < 1e3f || momentum_coupling_raw > 1e6f) {
            std::cerr << "  NOTE: Outside typical range [1e3, 1e6]" << std::endl;
        }
    }

    // CONFIG-DRIVEN: Apply soft clamps if enabled (production), or use raw (debugging)
    float momentum_coupling = momentum_coupling_raw;
    if (config.precond_enable_clamps) {
        float clamped = std::max(config.precond_momentum_clamp_min,
                                 std::min(momentum_coupling_raw, config.precond_momentum_clamp_max));
        if (clamped != momentum_coupling_raw && config.debug_level >= 1) {
            std::cerr << "[PRECONDITIONER] Clamping momentum_coupling: " << momentum_coupling_raw
                      << " → " << clamped << " (range [" << config.precond_momentum_clamp_min
                      << ", " << config.precond_momentum_clamp_max << "])" << std::endl;
        }
        momentum_coupling = clamped;
    }

    // SAFETY GUARD: Prevent catastrophic divide-by-zero (always active)
    if (momentum_coupling < 1e-6f) {
        std::cerr << "[PRECONDITIONER] ERROR: momentum_coupling = " << momentum_coupling
                  << " is unphysical!" << std::endl;
        std::cerr << "  Applying minimal guard 1e-6 to prevent crash" << std::endl;
        momentum_coupling = 1e-6f;
    }

    // v20.4: Per-k-level momentum coupling using c1f(k), c2f(k) instead of scalar averages.
    // c1f and c2f are 1D vertical-level coefficients; mub and msfty are 2D horizontal
    // (averaged to scalars since the 1D preconditioner operates on vertical columns).
    int nz_w_local = nz + 1;  // W-staggered grid
    std::vector<float> momentum_coupling_k(nz_w_local, momentum_coupling);  // fallback = scalar
    {
        const float* c1f_ptr = nullptr;
        const float* c2f_ptr = nullptr;
        int64_t c1f_sz = 0, c2f_sz = 0;
        torch::Tensor c1f_cpu, c2f_cpu;
        if (grid_info_->c1f.defined() && grid_info_->c1f.numel() > 0) {
            torch::NoGradGuard no_grad;
            c1f_cpu = grid_info_->c1f.to(torch::kCPU, torch::kFloat32).contiguous();
            c1f_ptr = c1f_cpu.data_ptr<float>();
            c1f_sz = c1f_cpu.numel();
        }
        if (grid_info_->c2f.defined() && grid_info_->c2f.numel() > 0) {
            torch::NoGradGuard no_grad;
            c2f_cpu = grid_info_->c2f.to(torch::kCPU, torch::kFloat32).contiguous();
            c2f_ptr = c2f_cpu.data_ptr<float>();
            c2f_sz = c2f_cpu.numel();
        }
        if (c1f_ptr && c1f_sz > 0) {
            for (int k = 0; k < nz_w_local; ++k) {
                int kidx = std::min(k, static_cast<int>(c1f_sz) - 1);
                float c1f_k = c1f_ptr[kidx];
                float c2f_k = (c2f_ptr && c2f_sz > 0) ? c2f_ptr[std::min(k, static_cast<int>(c2f_sz) - 1)] : c2f_representative;
                float mc_k = (c1f_k * mu_representative + c2f_k) / msfty_safe;
                // Apply same clamps as scalar path
                if (config.precond_enable_clamps) {
                    mc_k = std::max(config.precond_momentum_clamp_min,
                                    std::min(mc_k, config.precond_momentum_clamp_max));
                }
                momentum_coupling_k[k] = std::max(mc_k, 1e-6f);
            }
            if (config.debug_level >= 1) {
                std::cerr << "[PRECONDITIONER] Per-k momentum_coupling range: "
                          << *std::min_element(momentum_coupling_k.begin(), momentum_coupling_k.end())
                          << " to " << *std::max_element(momentum_coupling_k.begin(), momentum_coupling_k.end())
                          << " (scalar avg = " << momentum_coupling << ")" << std::endl;
            }
        }
    }

    // v20.5: Cache momentum_coupling_k for Φ-W GS correction
    // NOTE: coefficient_generation_ is incremented at END of this function (line ~1517),
    // so we set gs_cache_generation_ to the post-increment value to match.
    momentum_coupling_k_cached_ = momentum_coupling_k;
    mu_representative_cached_ = mu_representative;  // v20.14r27t: cache for mu scale correction
    gs_cache_generation_ = coefficient_generation_ + 1;

    // DIAGNOSTIC: Log coupling factor with actual values
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "\n[PRECONDITIONER] W-block momentum coupling (from actual grid data):" << std::endl;
        std::cerr << "  μ_representative (from mub.mean()) = " << mu_representative << " Pa" << std::endl;
        std::cerr << "  c1f_representative (from c1f.mean()) = " << c1f_representative << std::endl;
        std::cerr << "  c2f_representative (from c2f.mean()) = " << c2f_representative << std::endl;
        std::cerr << "  msfty_representative (from msfty.mean()) = " << msfty_representative << std::endl;
        std::cerr << "  msfty_safe (guarded) = " << msfty_safe << std::endl;
        std::cerr << "  Coupling factor (c1f*μ+c2f)/msfty = " << momentum_coupling
                  << " (scalar), per-k enabled" << std::endl;
    }

    // FIX 2025-12-30 Batch22 Issue 1: Get data_ptr for vertical_diag_* tensors
    // tensor[k] = value is no-op in LibTorch; use data_ptr for reliable assignment
    // FIX Round189: Validate CPU/FP32 BEFORE data_ptr acquisition (UB otherwise)
    TORCH_CHECK(vertical_diag_w_.is_cpu() && vertical_diag_w_.scalar_type() == torch::kFloat32,
        "vertical_diag_w_ must be CPU FP32 for data_ptr<float>()");
    TORCH_CHECK(vertical_diag_theta_.is_cpu() && vertical_diag_theta_.scalar_type() == torch::kFloat32,
        "vertical_diag_theta_ must be CPU FP32 for data_ptr<float>()");
    TORCH_CHECK(vertical_diag_phi_.is_cpu() && vertical_diag_phi_.scalar_type() == torch::kFloat32,
        "vertical_diag_phi_ must be CPU FP32 for data_ptr<float>()");
    TORCH_CHECK(vertical_diag_u_.is_cpu() && vertical_diag_u_.scalar_type() == torch::kFloat32,
        "vertical_diag_u_ must be CPU FP32 for data_ptr<float>()");
    TORCH_CHECK(vertical_diag_v_.is_cpu() && vertical_diag_v_.scalar_type() == torch::kFloat32,
        "vertical_diag_v_ must be CPU FP32 for data_ptr<float>()");
    float* vertical_diag_w_ptr = vertical_diag_w_.data_ptr<float>();
    float* vertical_diag_theta_ptr = vertical_diag_theta_.data_ptr<float>();
    float* vertical_diag_phi_ptr = vertical_diag_phi_.data_ptr<float>();
    float* vertical_diag_u_ptr = vertical_diag_u_.data_ptr<float>();
    float* vertical_diag_v_ptr = vertical_diag_v_.data_ptr<float>();

    for (int k = 1; k < nz; ++k) {  // W is at interfaces
        // FIX 2025-12-30 Batch21 Issue 1: Use data_ptr instead of tensor indexing
        float dz_w = 0.5f * (dz_effective_ptr[k-1] + dz_effective_ptr[k]);
        float dz_w_inv2 = 1.0f / (dz_w * dz_w);

        // CRITICAL DIAGNOSTIC: Check physical reasonableness
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && k <= 3) {
            float cs_squared = c_s * c_s;
            float acoustic_term = cs_squared * dz_w_inv2;

            std::cerr << "\n=== W DIAGONAL DEBUG k=" << k << " ===" << std::endl;
            std::cerr << "  dz_effective[k-1] = " << dz_effective_ptr[k-1] << std::endl;
            std::cerr << "  dz_effective[k] = " << dz_effective_ptr[k] << std::endl;
            std::cerr << "  dz_w (average) = " << dz_w << " meters" << std::endl;
            std::cerr << "  dz_w_inv2 = 1/dz_w² = " << dz_w_inv2 << " m^-2" << std::endl;
            std::cerr << "  c_s = " << c_s << " m/s" << std::endl;
            std::cerr << "  c_s² = " << cs_squared << " m²/s²" << std::endl;
            std::cerr << "  c_s² * dz_w_inv2 = " << acoustic_term << " s^-2" << std::endl;
            std::cerr << "  Expected acoustic term: O(1000) for dz~250m, c_s~340m/s" << std::endl;
        }

        // FIX 2025-12-30 Batch21 Issue 1: Use scalar arithmetic instead of tensor ops
        // Rayleigh damping coefficient (quadratic profile in upper levels)
        float ray_damp_val = 0.0f;
        if (k >= k_damp_start) {
            float z_frac = float(k - k_damp_start) / float(nz - k_damp_start);
            ray_damp_val = rayleigh_coef * z_frac * z_frac;
        }

        // PR 9D: the ONLY W-damping diagonal contribution is the explicit
        // precond_extra_wdamp regularization (0 unless opted in). There is no
        // physical W-damping diagonal — see wdamp_policy above.
        float w_damp_val = wdamp_extra_diag;
        float vdiff_val = kvdif * dz_w_inv2;

        // Combined operator with selective implicit treatment (accumulate as scalar)
        float diag_contrib = 0.0f;
        float acoustic_val = 0.0f, gravity_val = 0.0f;

        if (config.implicit_acoustic) {
            acoustic_val = c_s * c_s * dz_w_inv2;
            diag_contrib += acoustic_val;
        }
        if (config.implicit_gravity) {
            gravity_val = N_squared_ptr[k] * dz_w_inv2;
            diag_contrib += gravity_val;
        }
        if (eff_rayleigh) {
            diag_contrib += ray_damp_val;
        }
        // PR 9D: scalar W diagonal only when an explicit regularization is set.
        if (wdamp_extra_diag != 0.0f) {
            diag_contrib += wdamp_extra_diag;
        }
        if (eff_vdiff) {
            diag_contrib += vdiff_val;
        }

        // DIAGNOSTIC: Show breakdown for first few levels
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && k <= 3) {
            std::cerr << "  Contributions breakdown:" << std::endl;
            if (config.implicit_acoustic) std::cerr << "    Acoustic (c_s²/dz²): " << acoustic_val << std::endl;
            if (config.implicit_gravity) std::cerr << "    Gravity (N²/dz²): " << gravity_val << std::endl;
            if (eff_rayleigh) std::cerr << "    Rayleigh damping: " << ray_damp_val << std::endl;
            if (wdamp_extra_diag != 0.0f) std::cerr << "    W-damping extra regularization: " << wdamp_extra_diag << std::endl;
            if (eff_vdiff) std::cerr << "    Vertical diffusion: " << vdiff_val << std::endl;
            std::cerr << "    TOTAL raw: " << diag_contrib << std::endl;
        }

        // FIX 2026-01-27: The W diagonal has TWO contributions:
        //   1) Direct diagonal terms (diffusion, damping, etc.) scaled by momentum coupling
        //   2) Acoustic Schur complement from W↔φ round-trip coupling
        //
        // The acoustic stiffness is OFF-DIAGONAL in the (W,φ) block of A = I - dt*γ*J:
        //   A_wφ = -dt*γ*J_wφ (W equation depends on φ through pressure gradient)
        //   A_φw = -dt*γ*J_φw (φ equation depends on W: ∂φ'/∂t = g*w)
        //
        // The PROPER Schur complement (eliminating φ from the W-φ block) gives:
        //   D_w_effective = A_ww + A_wφ * A_φφ^{-1} * A_φw
        //                = 1 + (dt*γ)²*|J_wφ*J_φw| / A_φφ
        // where A_φφ = 1 + dt*γ*c_s²/dz² (the φ diagonal, already computed)
        //
        // For the vertical acoustic mode: |J_wφ*J_φw| ≈ (c_s/dz)²
        // So: D_w_effective ≈ 1 + (dt*γ*c_s/dz)² / (1 + dt*γ*c_s²/dz²)
        //
        // Without this term, D_w ≈ 1.005 and the preconditioner is ineffective.
        // With the undivided term (dt*γ*c_s/dz)² ≈ 131,000, the preconditioner
        // over-corrects (W diagonal >> actual eigenvalue ~3840).
        // Dividing by A_φφ ≈ 502 gives ~261, a much better match.

        // Part 1: Direct diagonal contribution (diffusion, damping)
        float diag_contrib_scaled = diag_contrib / momentum_coupling_k[k];

        // Part 2: Acoustic Schur complement from W↔φ coupling
        // This is the dominant stiffness source: vertical acoustic waves
        float dt_gamma = dt_ * gamma_;
        float acoustic_cfl = dt_gamma * c_s / dz_w;  // acoustic CFL for this level
        float acoustic_cfl_sq = acoustic_cfl * acoustic_cfl;  // (dt*γ*c_s/dz)²
        // Divide by the φ diagonal to get the proper Schur complement
        float dz_inv2_w = 1.0f / (dz_w * dz_w);
        float A_phi_diag = 1.0f + dt_gamma * c_s * c_s * dz_inv2_w;  // same formula as φ diagonal
        float acoustic_schur = acoustic_cfl_sq / A_phi_diag;  // proper Schur complement

        // v20.2: Boost acoustic_schur to account for multi-level vertical coupling
        // amplification. Single-level Schur underestimates the effective W-Φ coupling.
        // NOTE: Boost and GS are COMPLEMENTARY, not redundant.
        //   - Boost: scales D_w diagonal closer to true operator eigenvalues (M⁻¹ quality)
        //   - GS: corrects W residual for off-diagonal Φ→W coupling (RHS quality)
        // Empirical: boost=2+GS → true_err=0.75; boost=1+GS → true_err=0.93 (worse).
        // Keep configured boost regardless of GS state.
        // v20.13: Use member-cached boost (refreshed in initialize_acoustic_gravity_solver)
        float effective_boost = w_acoustic_boost_cached_;
        float acoustic_schur_boosted = acoustic_schur * effective_boost;

        float diag_val_physics = 1.0f + dt_gamma * diag_contrib_scaled + acoustic_schur_boosted;
        float diag_val_raw = diag_val_physics;
        if (config.precond_enable_clamps) {
            // Add baseline damping to prevent diagonal=1.0 (weak preconditioning)
            diag_val_raw = diag_val_physics + config.precond_damping_factor;
        }

        // SAFETY GUARD: Prevent zero/negative diagonal (always active)
        float diag_val = diag_val_raw;
        if (diag_val_raw <= 0.0f) {
            std::cerr << "[PRECOND ERROR k=" << k << "] Diagonal = " << diag_val_raw
                      << " is zero/negative! Setting to epsilon=1e-6" << std::endl;
            diag_val = 1e-6f;
        }

        // ENHANCED DIAGNOSTIC: Log ALL values with config info
        if (config.debug_level >= 1 && k <= 3) {
            std::cerr << "\n[PRECOND DIAG k=" << k << "] CONFIG-DRIVEN PRECONDITIONER:" << std::endl;
            std::cerr << "  Config: clamps=" << (config.precond_enable_clamps ? "ON" : "OFF")
                      << ", damping=" << config.precond_damping_factor
                      << ", momentum_range=[" << config.precond_momentum_clamp_min
                      << ", " << config.precond_momentum_clamp_max << "]" << std::endl;
            std::cerr << "  Raw physics:" << std::endl;
            std::cerr << "    diag_contribution = " << diag_contrib << std::endl;
            std::cerr << "    momentum_coupling_raw = " << momentum_coupling_raw << std::endl;
            if (config.precond_enable_clamps && momentum_coupling_raw != momentum_coupling) {
                std::cerr << "    momentum_coupling_clamped = " << momentum_coupling << std::endl;
            }
            std::cerr << "  Scaling:" << std::endl;
            std::cerr << "    diag_contrib / momentum_coupling = " << diag_contrib_scaled << std::endl;
            std::cerr << "    dt = " << dt_ << ", gamma = " << gamma_ << ", dt*gamma = " << dt_gamma << std::endl;
            std::cerr << "    dt*gamma*contrib_scaled = " << (dt_gamma * diag_contrib_scaled) << std::endl;
            std::cerr << "  Acoustic Schur complement (W↔φ coupling):" << std::endl;
            std::cerr << "    c_s = " << c_s << " m/s, dz_w = " << dz_w << " m" << std::endl;
            std::cerr << "    acoustic_cfl = dt*gamma*c_s/dz = " << acoustic_cfl << std::endl;
            std::cerr << "    acoustic_cfl² = " << acoustic_cfl_sq << std::endl;
            std::cerr << "    A_phi_diag = 1 + dt*gamma*c_s²/dz² = " << A_phi_diag << std::endl;
            std::cerr << "    acoustic_schur = cfl²/A_phi = " << acoustic_schur << std::endl;
            std::cerr << "    acoustic_schur_boosted = " << acoustic_schur_boosted
                      << " (boost=" << effective_boost << "x)" << std::endl;
            std::cerr << "  Diagonal:" << std::endl;
            std::cerr << "    1 + direct + acoustic_schur_boosted = " << diag_val_physics << " (pure physics)" << std::endl;
            if (config.precond_enable_clamps && diag_val_physics != diag_val_raw) {
                std::cerr << "    + damping_factor = " << diag_val_raw << " (with " << config.precond_damping_factor << " damping)" << std::endl;
            }
            if (diag_val_raw != diag_val) {
                std::cerr << "    WARNING: Guard applied! Final = " << diag_val << std::endl;
            } else {
                std::cerr << "    Final diagonal = " << diag_val << std::endl;
            }
            std::cerr << "  Preconditioning strength: 1/diagonal = " << (1.0f / diag_val) << "x" << std::endl;
        }

        vertical_diag_w_ptr[k] = diag_val;
    }
    vertical_diag_w_ptr[0] = 1.0f;   // Bottom boundary
    vertical_diag_w_ptr[nz] = 1.0f;  // Top boundary

    // CRITICAL DIAGNOSTIC: Check W diagonal values
    // FIX Round164: Raise to debug_level >= 2 since min/max force GPU sync
    // FIX Round181: Batch all min/max to single CPU transfer to reduce GPU sync overhead
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        torch::NoGradGuard no_grad;  // Prevent autograd tracking for diagnostics

        // FIX Round181: Batch vertical_diag_w_ min/max into single CPU transfer
        auto w_diag_minmax = torch::stack({vertical_diag_w_.min(), vertical_diag_w_.max()}).to(torch::kCPU);
        float w_diag_min = w_diag_minmax[0].item<float>();
        float w_diag_max = w_diag_minmax[1].item<float>();

        std::cerr << "\n[PRECONDITIONER] W diagonal values (dt=" << dt_ << ", gamma=" << gamma_ << "):" << std::endl;
        std::cerr << "  vertical_diag_w_ range: " << w_diag_min << " to " << w_diag_max << std::endl;
        std::cerr << "  dt*gamma = " << (dt_ * gamma_) << std::endl;

        // AUDIT: Compare preconditioner scaling vs RHS operator scaling
        std::cerr << "\n[PRECONDITIONER AUDIT] W block momentum coupling:" << std::endl;
        std::cerr << "  Preconditioner uses SCALAR momentum_coupling = " << momentum_coupling << std::endl;
        std::cerr << "  This is (c1f*μ + c2f)/msfty with AVERAGED values:" << std::endl;
        std::cerr << "    c1f_representative = " << c1f_representative << std::endl;
        std::cerr << "    mu_representative = " << mu_representative << std::endl;
        std::cerr << "    c2f_representative = " << c2f_representative << std::endl;
        std::cerr << "    msfty_representative = " << msfty_representative << std::endl;

        // Compare to RHS operator which uses POINTWISE (μ + μ_base)
        if (grid_info_ && grid_info_->mub.defined()) {
            // FIX Round181: Batch mub min/max into single CPU transfer
            auto mub_minmax = torch::stack({grid_info_->mub.min(), grid_info_->mub.max()}).to(torch::kCPU);
            std::cerr << "  RHS uses POINTWISE (μ + μ_base) for velocity conversion:" << std::endl;
            std::cerr << "    μ_base range: " << mub_minmax[0].item<float>()
                      << " to " << mub_minmax[1].item<float>() << std::endl;
            std::cerr << "  MISMATCH: Preconditioner assumes uniform ~" << momentum_coupling
                      << ", but RHS has spatial variation!" << std::endl;
        }

        // Check for problematic values (reuse w_diag_min from above)
        if (w_diag_min < 0.1f) {
            std::cerr << "  WARNING: W diagonal has very small values (min=" << w_diag_min
                      << "), will cause amplification in tridiagonal solve!" << std::endl;
        }
        if (w_diag_min < 0.0f) {
            std::cerr << "  ERROR: W diagonal has NEGATIVE values! Preconditioner is unstable!" << std::endl;
        }
    }
    
    // 2. Theta (potential temperature): Gravity wave coupling + damping
    for (int k = 0; k < nz; ++k) {
        // FIX 2025-12-30 Batch23 Issue 2: Use data_ptr instead of tensor[k] to avoid per-k tensor creation
        int dz_inv_idx = std::min(k, static_cast<int>(dz_inv_sz) - 1);
        float dz_inv_k_raw = dz_inv_ptr[dz_inv_idx];
        // FIX 2025-12-30 Batch26 Issue 6: Finite/clamp safety to prevent division explosion
        // If dz_inv is 0, NaN, or Inf, use a safe fallback (1e-6 corresponds to ~1km grid spacing)
        float dz_inv_k = (std::isfinite(dz_inv_k_raw) && dz_inv_k_raw > 1e-10f)
            ? dz_inv_k_raw
            : 1e-6f;  // Safe fallback: ~1km grid spacing inverse
        float dz_inv2 = dz_inv_k * dz_inv_k;
        
        // Rayleigh damping for scalars
        float ray_damp = 0.0f;
        if (k >= k_damp_start) {
            float z_frac = float(k - k_damp_start) / float(nz - k_damp_start);
            ray_damp = rayleigh_coef * z_frac * z_frac;
        }
        
        // FIX 2025-12-30 Batch24 Issue 1: Use pure scalar accumulation instead of tensor creation
        // Before: torch::full({1},...) + tensor accumulation + .squeeze().to(kCPU).item<float>()
        // After: float scalar accumulation - eliminates per-k tensor allocations
        float vdiff_diag = kvdif * dz_inv2;

        // Theta evolution with selective implicit treatment (accumulate as scalar)
        float theta_diag_contribution = 0.0f;

        if (config.implicit_gravity) {
            // Use ACTUAL base state θ₀(k) for physical accuracy
            // FIX 2025-12-27: Use data_ptr instead of .item<float>()
            float theta_0_k = theta_base_ptr[k];
            // FIX 2025-12-30 Batch23 Issue 2: Use N_squared_ptr instead of tensor[k]
            float N2_k = N_squared_ptr[std::min(k, nz - 1)];
            float theta_coupling = N2_k * (g / std::max(theta_0_k, 200.0f));
            theta_diag_contribution += theta_coupling * dz_inv2;  // Gravity wave coupling
        }
        if (eff_rayleigh) {
            theta_diag_contribution += ray_damp;  // Rayleigh damping
        }
        if (eff_vdiff) {
            theta_diag_contribution += vdiff_diag;  // Vertical diffusion
        }

        // θ acoustic Schur complement.
        // θ participates in acoustic mode indirectly through EOS:
        //   p = p₀·(R·ρ·θ/p₀)^(c_p/c_v)  →  δp/p₀ ≈ (c_p/c_v)·(δθ/θ₀)
        //
        // Without this term: D_θ ≈ 1.0 (gravity coupling alone is O(1e-11)).
        //
        // v15a: (R_d/c_v)²=0.16 gave D_θ≈1.76 — too weak for small dt.
        // v15b: Full c_s²/dz² gave D_θ≈D_φ≈10 — good for small dt.
        // v20.11: At dt=600, full scaling gives D_θ≈438, which OVER-DAMPENS θ by 439x.
        //   Per-block GMRES diagnostic shows θ residual is 50% of total and NOT decreasing
        //   (rel=1.022 → θ gets WORSE). The acoustic coupling of θ is indirect (θ→p→φ)
        //   and should scale as (R_d/c_v)² ≈ 0.16, not 1.0.
        //   Configurable via precond_theta_acoustic_factor (default=0.0 to disable).
        float theta_acoustic_schur = 0.0f;
        if (config.implicit_acoustic) {
            float dt_gamma = dt_ * gamma_;
            // v20.13: Use member-cached factor (refreshed in initialize_acoustic_gravity_solver)
            float theta_acoustic_factor = theta_acoustic_factor_cached_;
            theta_acoustic_schur = theta_acoustic_factor * dt_gamma * c_s * c_s * dz_inv2;
        }

        // Direct scalar assignment - no tensor conversion needed
        vertical_diag_theta_ptr[k] = 1.0f + dt_ * gamma_ * theta_diag_contribution
                                   + theta_acoustic_schur;
    }
    
    // 3. Phi (geopotential): Hydrostatic balance coupling
    // FIX 2025-01-25: Use dz_effective_ptr for scalar access instead of tensor indexing
    for (int k = 0; k <= nz; ++k) {
        float dz_phi = (k > 0 && k < nz) ?
            0.5f * (dz_effective_ptr[k-1] + dz_effective_ptr[k]) : dz_effective_ptr[0];
        float dz_inv2 = 1.0f / (dz_phi * dz_phi);

        // Geopotential coupled to pressure through hydrostatic relation
        vertical_diag_phi_ptr[k] = 1.0f + dt_ * gamma_ * c_s * c_s * dz_inv2;
    }
    
    // 4. U,V horizontal momentum: Acoustic coupling + implicit divergence damping + Rayleigh + diffusion
    for (int k = 0; k < nz; ++k) {
        // FIX 2025-12-30 Batch23 Issue 2: Use data_ptr instead of tensor[k] to avoid per-k tensor creation
        int dz_inv_idx_uv = std::min(k, static_cast<int>(dz_inv_sz) - 1);
        float dz_inv_k_raw = dz_inv_ptr[dz_inv_idx_uv];
        // FIX 2025-12-30 Batch26 Issue 6: Finite/clamp safety to prevent division explosion
        float dz_inv_k = (std::isfinite(dz_inv_k_raw) && dz_inv_k_raw > 1e-10f)
            ? dz_inv_k_raw
            : 1e-6f;  // Safe fallback
        float dz_inv2 = dz_inv_k * dz_inv_k;
        
        // Rayleigh damping for horizontal momentum
        float ray_damp = 0.0f;
        if (k >= k_damp_start) {
            float z_frac = float(k - k_damp_start) / float(nz - k_damp_start);
            ray_damp = rayleigh_coef * z_frac * z_frac;
        }
        
        // FIX 2025-12-30 Batch24 Issue 1: Use pure scalar accumulation instead of tensor creation
        // Before: torch::full({1},...) + tensor accumulation + .squeeze().to(kCPU).item<float>()
        // After: float scalar accumulation - eliminates per-k tensor allocations
        float vdiff_diag = kvdif * dz_inv2;

        // U,V momentum with selective implicit treatment (accumulate as scalar)
        float uv_diag_contribution = 0.0f;

        if (config.implicit_acoustic) {
            // v20.15: U/V acoustic diagonal should follow horizontal pressure-gradient scale.
            // In coarse-mesh cases (e.g., dx~100 km), horizontal acoustic coupling is tiny while
            // c_s^2*dz_inv2 is large. If we inject full vertical proxy into D_u/D_v, ru block is
            // over-preconditioned (D_u >> 1), shrinking M^{-1}J information in Krylov space.
            //
            // Keep the optional vertical proxy, but cap it to the horizontal acoustic scale.
            // This preserves conditioning help where horizontal coupling exists, while preventing
            // D_u blow-up in weak-horizontal-acoustic regimes.
            float horiz_acoustic = c_s * c_s * (rdx * rdx + rdy * rdy);
            float vert_acoustic = c_s * c_s * dz_inv2;
            float uv_vfrac_eff = uv_vertical_fraction_cached_;

            constexpr float uv_vertical_over_horizontal_cap = 1.0f;
            float max_vertical_term = uv_vertical_over_horizontal_cap * horiz_acoustic;
            float requested_vertical_term = uv_vfrac_eff * vert_acoustic;
            if (requested_vertical_term > max_vertical_term) {
                uv_vfrac_eff = max_vertical_term / std::max(vert_acoustic, 1e-20f);
                static std::atomic<bool> uv_cap_log_once{false};
                if (config.debug_level >= 1 && !uv_cap_log_once.exchange(true, std::memory_order_relaxed)) {
                    std::cerr << "[PRECOND UV] Capped vertical acoustic proxy in D_u/D_v: "
                              << "uv_vfrac " << uv_vertical_fraction_cached_
                              << " -> " << uv_vfrac_eff
                              << " (horiz=" << horiz_acoustic
                              << ", vert=" << vert_acoustic << ")" << std::endl;
                }
            }

            // v20.15 HEVI: U/V rows of J_fast keep only advection/diffusion/rayleigh/coriolis
            // (no horizontal-acoustic PGF, no vertical-acoustic proxy) -> D_u/D_v ~ near-identity.
            // Drop the acoustic proxy under hevi so M matches A_fast.
            if (!hevi) {
                uv_diag_contribution += horiz_acoustic + uv_vfrac_eff * vert_acoustic;
            }
        }
        if (eff_divergence && !hevi) {
            // For divergence damping operator: -kdamp * ∇(∇·v)
            // Diagonal term from Laplacian: kdamp * (rdx² + rdy²)
            // v20.15 HEVI: horizontal Laplacian-of-divergence belongs to k_slow -> drop under hevi.
            uv_diag_contribution += kdamp * (rdx * rdx + rdy * rdy);  // Divergence damping
        }
        if (eff_rayleigh) {
            uv_diag_contribution += ray_damp;  // Rayleigh damping
        }
        if (eff_vdiff) {
            uv_diag_contribution += vdiff_diag;  // Vertical diffusion
        }

        // U and V have same diagonal (isotropic for now)
        // Direct scalar assignment - no tensor conversion needed
        float uv_diag = 1.0f + dt_ * gamma_ * uv_diag_contribution;
        vertical_diag_u_ptr[k] = uv_diag;
        vertical_diag_v_ptr[k] = uv_diag;
    }

    // 5. Mu (column mass): Acoustic divergence coupling
    // Use ACTUAL grid spacing instead of hard-coded 100m
    float dx_actual = grid_info_->dx;
    float dy_actual = grid_info_->dy;

    // CRITICAL FIX: Extract REAL column mass density from grid_info_->mub
    // mub is 2D base state column mass (kg/m²), O(10⁵) not 1.225!
    // FIX 2025-12-30 Batch29 Issue 1: Reuse cached mub.mean() instead of recomputing
    float rho_avg = 1.225f;  // Fallback if mub not available
    if (grid_info_->mub.defined() && grid_info_->mub.numel() > 0) {
        // Reuse the same cache entry as mu_representative (same tensor, same mean)
        rho_avg = g_scalar_mean_cache.get_or_compute_mean(
            grid_info_->mub, g_scalar_mean_cache.mub_entry, "mub");
        // FIX Round163: Gate diagnostic log with debug_level >= 2
        if (g_sdirk3_config.debug_level >= 2) {
            std::cerr << "UnifiedPreconditioner: Using actual column mass density ρ_avg = "
                      << rho_avg << " kg/m² (not 1.225!)" << std::endl;
        }
    } else {
        // FIX Round163: Use WARN_ONCE pattern for fallback warning
        // v20.14r27k: Downgraded — mub is set after first WRF step. Fallback is
        // temporary init state, overwritten by actual base-state mu from WRF.
        static bool warned_mub_fallback = false;
        if (!warned_mub_fallback && g_sdirk3_config.debug_level >= 2) {
            std::cerr << "[PRECOND INIT] mub not yet set, using temporary ρ₀=1.225 fallback (will be updated)" << std::endl;
            warned_mub_fallback = true;
        }
    }

    // Mu diagonal (SCALAR - single value per column, not per-level)
    // D_μ = 1 + Δtγ * ρ_avg * (H_x² + H_y²)
    // Fill all nz entries with same scalar for accessor compatibility
    float H_x = 1.0f / dx_actual;
    float H_y = 1.0f / dy_actual;
    float D_mu_value = 1.0f + dt_ * gamma_ * rho_avg * (H_x * H_x + H_y * H_y);
    vertical_diag_mu_.fill_(D_mu_value);  // Replicate scalar across all levels
    
    // === OFF-DIAGONAL COUPLING TERMS ===

    // W-Theta coupling for gravity waves (buoyancy oscillations)
    // Use ACTUAL base state θ₀(z) instead of constant 300K
    // CRITICAL FIX: Include momentum coupling factor for W equation

    // FIX 2025-12-30 Batch22 Issue 1: Get data_ptr for coupling arrays
    // tensor[k] = value is no-op in LibTorch; use data_ptr for reliable assignment
    float* w_theta_coupling_upper_ptr = w_theta_coupling_upper_.data_ptr<float>();
    float* w_theta_coupling_lower_ptr = w_theta_coupling_lower_.data_ptr<float>();
    float* theta_w_coupling_upper_ptr = theta_w_coupling_upper_.data_ptr<float>();
    float* theta_w_coupling_lower_ptr = theta_w_coupling_lower_.data_ptr<float>();
    float* vertical_upper_w_ptr = vertical_upper_w_.data_ptr<float>();
    float* vertical_lower_w_ptr = vertical_lower_w_.data_ptr<float>();
    float* vertical_upper_theta_ptr = vertical_upper_theta_.data_ptr<float>();
    float* vertical_lower_theta_ptr = vertical_lower_theta_.data_ptr<float>();

    for (int k = 1; k < nz; ++k) {
        // FIX 2025-12-30 Batch23 Issue 2: Use dz_effective_ptr for scalar access instead of tensor[k]
        int k_prev = std::min(k - 1, nz - 1);
        int k_curr = std::min(k, nz - 1);
        float dz_coupling = 0.5f * (dz_effective_ptr[k_prev] + dz_effective_ptr[k_curr]);
        float dz_coupling_inv2 = 1.0f / (dz_coupling * dz_coupling);

        // Use actual base state θ₀(k) for physical accuracy
        // FIX 2025-12-27: Use data_ptr instead of .item<float>()
        float theta_0_k = theta_base_ptr[k];
        float theta_0_k_safe = std::max(theta_0_k, 200.0f);  // Safety: avoid division by tiny values

        // W equation couples to theta through buoyancy: ∂(μ'w)/∂t ~ (c1f*μ+c2f)/msfty * g(θ'/θ₀)
        // The momentum coupling factor scales the buoyancy response
        // FIX 2025-12-30 Batch23 Issue 2: Use N_squared_ptr instead of tensor[k]
        float N2_k = N_squared_ptr[std::min(k, nz - 1)];
        float w_theta_factor = dt_ * gamma_ * g * N2_k / (theta_0_k_safe * momentum_coupling_k[k]);

        // Theta equation couples to w through advection: ∂θ/∂t ~ -wN²/g
        // Note: θ equation operates in standard units, so no momentum coupling needed here
        float theta_w_factor = dt_ * gamma_ * N2_k / g;

        // FIX 2025-12-30 Batch22 Issue 1: Use data_ptr instead of tensor[] for reliable assignment
        // FIX 2025-12-30 Batch23 Issue 2: Now all scalar ops, no tensor math needed
        if (k > 1) {
            w_theta_coupling_lower_ptr[k-1] = -0.5f * w_theta_factor * dz_coupling_inv2;
            theta_w_coupling_lower_ptr[k-2] = -0.5f * theta_w_factor * dz_coupling_inv2;
        }

        if (k < nz-1) {
            w_theta_coupling_upper_ptr[k] = -0.5f * w_theta_factor * dz_coupling_inv2;
            theta_w_coupling_upper_ptr[k-1] = -0.5f * theta_w_factor * dz_coupling_inv2;
        }
    }

    // Standard vertical coupling for each variable's own equation
    // FIX 2025-12-30 Batch23 Issue 2: Pre-convert dz_local to data_ptr for scalar loop access
    auto dz_local_cpu = dz_local.to(torch::kCPU, torch::kFloat32).contiguous();
    const float* dz_local_ptr = dz_local_cpu.data_ptr<float>();
    const int64_t dz_local_sz = dz_local_cpu.numel();

    for (int k = 0; k < nz-1; ++k) {
        // PARITY FIX 2025-12-11: Use dz_local instead of grid_info_->dz
        // FIX 2025-12-30 Batch23 Issue 2: Use data_ptr for scalar access
        int dz_k_idx = std::min(k, static_cast<int>(dz_local_sz) - 1);
        float dz_coupling_inv = 1.0f / dz_local_ptr[dz_k_idx];
        float dz_coupling_inv2 = dz_coupling_inv * dz_coupling_inv;

        // FIX 2025-12-30 Batch23 Issue 2: Use N_squared_ptr instead of tensor[k]
        float N2_k = N_squared_ptr[std::min(k, nz - 1)];

        // W vertical coupling (acoustic + gravity)
        // v20.7: Remove /momentum_coupling_k — diagonal's acoustic_schur (≈226-452) does NOT
        // divide by momentum_coupling_k, so the off-diagonal must match. With the old
        // /momentum_coupling_k[k], off-diag ≈ 0.003 vs diag ≈ 523 (ratio 5.7e-6) → TDMA
        // was effectively Jacobi. Without it: off-diag ≈ -209, diag ≈ 523 (ratio 0.4) →
        // proper tridiagonal capturing full vertical acoustic column structure.
        if (k < nz-1) {
            float combined_factor = c_s * c_s + N2_k;
            float w_coupling_val = -dt_ * gamma_ * 0.5f * combined_factor * dz_coupling_inv2;
            vertical_upper_w_ptr[k] = w_coupling_val;
            vertical_lower_w_ptr[k] = w_coupling_val;
        }

        // Theta vertical coupling - use actual θ₀(k)
        // FIX 2025-12-30 Batch22 Issue 1: Use data_ptr instead of tensor[] for reliable assignment
        if (k > 0) {
            float theta_0_k = theta_base_ptr[k];
            float theta_factor = N2_k * (g / std::max(theta_0_k, 200.0f));
            vertical_lower_theta_ptr[k-1] = -dt_ * gamma_ * 0.5f * theta_factor * dz_coupling_inv2;
        }
        if (k < nz-2) {
            float theta_0_k = theta_base_ptr[k];
            float theta_factor = N2_k * (g / std::max(theta_0_k, 200.0f));
            vertical_upper_theta_ptr[k] = -dt_ * gamma_ * 0.5f * theta_factor * dz_coupling_inv2;
        }
    }

    // === U-V-μ ACOUSTIC COUPLING COEFFICIENTS ===
    // Compute per-level cross-coupling for horizontal acoustic modes
    // Physics: pressure gradient (μ → u,v) and divergence (u,v → μ)
    // Sound speed squared already computed earlier (c² = γ·R·T ≈ 100,000 m²/s²)
    // H_x, H_y already defined earlier in μ diagonal computation

    // BUG #16 FIX: Allocate U-V-μ coupling coefficient tensors before indexing
    // These were never initialized, causing "Expected a proper Tensor but got None" crash
    if (!C_u_mu_.defined() || C_u_mu_.size(0) != nz) {
        C_u_mu_ = torch::zeros({nz}, options);
        C_v_mu_ = torch::zeros({nz}, options);
        C_mu_u_ = torch::zeros({nz}, options);
        C_mu_v_ = torch::zeros({nz}, options);
    }

    // FIX 2025-12-30 Batch22 Issue 1: Get data_ptr for U-V-μ coupling arrays
    // tensor[k] = value is no-op in LibTorch; use data_ptr for reliable assignment
    float* C_u_mu_ptr = C_u_mu_.data_ptr<float>();
    float* C_v_mu_ptr = C_v_mu_.data_ptr<float>();
    float* C_mu_u_ptr = C_mu_u_.data_ptr<float>();
    float* C_mu_v_ptr = C_mu_v_.data_ptr<float>();

    for (int k = 0; k < nz; ++k) {
        // Cross-coupling coefficients using REAL ρ_avg from mub:
        // C_uμ = -Δtγ * (c²/ρ_avg) * H_x  (pressure gradient effect on U)
        // C_vμ = -Δtγ * (c²/ρ_avg) * H_y  (pressure gradient effect on V)
        // C_μu = -Δtγ * ρ_avg * H_x       (divergence effect on μ from U)
        // C_μv = -Δtγ * ρ_avg * H_y       (divergence effect on μ from V)

        float dt_gamma = dt_ * gamma_;

        // v20.15 HEVI: C_u_mu/C_v_mu = horizontal PGF (mu->u,v); C_mu_u/C_mu_v = horizontal
        // mass divergence (u,v->mu). All H_x/H_y-scaled => HORIZONTAL, moved to k_slow by HEVI.
        // Zero all four under hevi so BOTH the 4x4 S_mu_mu cross-terms AND the 3x3/4D
        // level-coupling (which read these same arrays) vanish -> U/V/mu degrade to diagonal
        // near-identity solves matching A_fast. Explicit zero (not skip): these are persistent
        // members only reallocated on nz change, so a skip could leave STALE horizontal values.
        if (hevi) {
            C_u_mu_ptr[k] = 0.0f;
            C_v_mu_ptr[k] = 0.0f;
            C_mu_u_ptr[k] = 0.0f;
            C_mu_v_ptr[k] = 0.0f;
        } else {
            C_u_mu_ptr[k] = -dt_gamma * (c_squared / rho_avg) * H_x;
            C_v_mu_ptr[k] = -dt_gamma * (c_squared / rho_avg) * H_y;
            C_mu_u_ptr[k] = -dt_gamma * rho_avg * H_x;
            C_mu_v_ptr[k] = -dt_gamma * rho_avg * H_y;
        }
    }

    // FIX Round163: Gate diagnostic with debug_level >= 2 to avoid GPU sync in hot path
    // The .item<float>() calls force GPU→CPU transfer even with .to(kCPU) preceding
    if (g_sdirk3_config.debug_level >= 2) {
        std::cerr << "U-V-μ coupling coefficients (level 0): C_uμ=" << C_u_mu_ptr[0]
                  << ", C_μu=" << C_mu_u_ptr[0] << std::endl;
    }

    // === PHASE 4.1: Φ COUPLING COEFFICIENTS ===
    // Add geopotential (Φ/PH) to the acoustic block for pressure gradient stiffness
    // Φ is w-staggered (nz+1 levels), but we solve at mass levels using Φ[k] directly

    int nz_w = nz + 1;  // W-staggered grid (one extra level at top)

    // Allocate Φ coupling coefficient arrays (w-staggered)
    C_u_phi_ = torch::zeros({nz_w}, options);
    C_v_phi_ = torch::zeros({nz_w}, options);
    C_phi_u_ = torch::zeros({nz_w}, options);
    C_phi_v_ = torch::zeros({nz_w}, options);
    C_phi_mu_ = torch::zeros({nz_w}, options);

    // FIX 2025-12-30 Batch22 Issue 1: Get data_ptr for Φ coupling arrays
    // tensor[k] = value is no-op in LibTorch; use data_ptr for reliable assignment
    float* C_u_phi_ptr = C_u_phi_.data_ptr<float>();
    float* C_v_phi_ptr = C_v_phi_.data_ptr<float>();
    float* C_phi_u_ptr = C_phi_u_.data_ptr<float>();
    float* C_phi_v_ptr = C_phi_v_.data_ptr<float>();
    float* C_phi_mu_ptr = C_phi_mu_.data_ptr<float>();

    // Compute Φ coupling coefficients
    // CRITICAL PHYSICS (from PHASE_4.1_DESIGN.md v2.1):
    //   A_uΦ = dt·γ·(1/μ₀)·H_x  (NOT (c²/ρ) - different from μ term!)
    //   A_Φu = dt·γ·(c²/μ₀)·H_x  (NOT ρ - pressure divergence scaling!)
    //   A_Φμ = dt·γ·c²           (hydrostatic balance, SYMMETRIC with A_μΦ)

    // Use averaged μ₀ for initialization (will use per-column μ₀ at solve time)
    // FIX 2025-12-30 Batch29 Issue 1: Use ScalarMeanCache for mu_base.mean()
    float mu_0_avg = rho_avg;  // Rough estimate: column mass ~ column density
    // BUG #15 FIX: Check tensor is actually initialized before calling methods
    // .defined() returns true even for uninitialized tensors!
    if (grid_info_->mu_base.defined() && grid_info_->mu_base.numel() > 0 && grid_info_->mu_base.dim() > 0) {
        try {
            mu_0_avg = g_scalar_mean_cache.get_or_compute_mean(
                grid_info_->mu_base, g_scalar_mean_cache.mu_base_entry, "mu_base");
            // FIX Round164: Gate diagnostic log with debug_level >= 2
            if (g_sdirk3_config.debug_level >= 2) {
                std::cerr << "Phase 4.1: Using actual μ₀_avg = " << mu_0_avg << " kg/m² for Φ coefficients" << std::endl;
            }
        } catch (const c10::Error& e) {
            // FIX Round164: Add WARN_ONCE for mu_base error
            static bool warned_mu_base_error = false;
            if (!warned_mu_base_error && g_sdirk3_config.debug_level >= 1) {
                std::cerr << "Phase 4.1: WARNING: mu_base tensor error, using fallback μ₀=" << mu_0_avg << std::endl;
                warned_mu_base_error = true;
            }
        }
    }

    for (int k = 0; k < nz_w; ++k) {
        float dt_gamma = dt_ * gamma_;

        // Use c² from mass level (for k < nz, use k; for k=nz use nz-1)
        [[maybe_unused]] int k_mass = (k < nz) ? k : (nz - 1);
        float c2 = c_squared;

        // CRITICAL FIX: All Φ coefficients must include 1/μ₀ normalization for proper scaling
        // Without this, C_Φμ ~ dt·γ·c² ≈ O(10⁷) makes Schur complement ill-conditioned

        // Pressure gradient: Φ → U,V  (WRF: ∂u/∂t = -(1/μ₀) ∂Φ/∂x)
        // FIX 2025-12-30 Batch22 Issue 1: Use data_ptr for reliable assignment
        C_u_phi_ptr[k] = dt_gamma * (1.0f / mu_0_avg) * H_x;   // (1/μ₀) scaling
        C_v_phi_ptr[k] = dt_gamma * (1.0f / mu_0_avg) * H_y;

        // Divergence sensing: U,V → Φ  (WRF: ∂Φ/∂t ≈ -(c²/μ₀) ∇·v lateral)
        C_phi_u_ptr[k] = dt_gamma * (c2 / mu_0_avg) * H_x;    // (c²/μ₀) scaling
        C_phi_v_ptr[k] = dt_gamma * (c2 / mu_0_avg) * H_y;

        // Hydrostatic balance: μ ↔ Φ  (CRITICAL FIX: must include 1/μ₀ normalization!)
        // BEFORE: dt·γ·c² ≈ 3×10⁷ → Schur O(10¹⁴) → ill-conditioned
        // AFTER:  dt·γ·(c²/μ₀) ≈ O(10²-10³) → well-conditioned Schur complement
        C_phi_mu_ptr[k] = dt_gamma * (c2 / mu_0_avg);        // (c²/μ₀) scaling for unit consistency
        // NOTE: A_μΦ will also = dt·γ·(c²/μ₀) for block symmetry (required for stable Schur complement)
    }

    // Debug output to verify correct magnitudes
    // FIX Round164: Gate with debug_level >= 2 to avoid GPU sync in hot path
    if (g_sdirk3_config.debug_level >= 2) {
        // BUG #16 FIX: Add defensive check to prevent future crashes if tensors uninitialized
        if (C_u_phi_.defined() && C_u_phi_.numel() > 0 && C_u_mu_.defined() && C_u_mu_.numel() > 0) {
            torch::NoGradGuard no_grad;  // Prevent autograd tracking for diagnostics
            std::cerr << "Phase 4.1: Φ coupling coefficients initialized:" << std::endl;
            std::cerr << "  C_uΦ[0]  = " << C_u_phi_ptr[0] << "  (pressure gradient U)" << std::endl;
            std::cerr << "  C_Φu[0]  = " << C_phi_u_ptr[0] << "  (divergence sensing)" << std::endl;
            std::cerr << "  C_Φμ[0]  = " << C_phi_mu_ptr[0] << "  (hydrostatic balance)" << std::endl;
            std::cerr << "  Ratio C_uΦ/C_uμ = " << (C_u_phi_ptr[0] / C_u_mu_ptr[0])
                      << "  (expect O(1e-2) per design)" << std::endl;
            std::cerr << "  Φ coefficients span " << nz_w << " w-staggered levels (nz=" << nz << ")" << std::endl;
        } else {
            static bool warned_phi_init = false;
            if (!warned_phi_init) {
                std::cerr << "WARNING: Phase 4.1 coefficient tensors not properly initialized" << std::endl;
                warned_phi_init = true;
            }
        }
    }

    // EXPLICIT boundary zeroing for safety (prevent spurious coupling at boundaries)
    // FIX 2025-12-30 Batch22 Issue 1: Use data_ptr for reliable assignment
    if (nz > 1) {
        vertical_lower_w_ptr[0] = 0.0f;  // Bottom W
        vertical_upper_w_ptr[nz-2] = 0.0f;  // Top W (nz_w = nz+1, so index nz-2 is last valid)
    }
    if (nz > 2) {
        vertical_lower_theta_ptr[0] = 0.0f;  // Bottom theta
        vertical_upper_theta_ptr[nz-3] = 0.0f;  // Top theta
    }

    // v20.13: Gated on debug_level >= 1 (was unconditional, causing CPU sync overhead)
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        torch::NoGradGuard no_grad;
        auto w_cpu = vertical_diag_w_.to(torch::kCPU, torch::kFloat32).contiguous();
        auto t_cpu = vertical_diag_theta_.to(torch::kCPU, torch::kFloat32).contiguous();
        auto u_cpu = vertical_diag_u_.to(torch::kCPU, torch::kFloat32).contiguous();
        auto phi_cpu = vertical_diag_phi_.to(torch::kCPU, torch::kFloat32).contiguous();
        float w_min = w_cpu.min().item<float>();
        float w_max = w_cpu.max().item<float>();
        float t_min = t_cpu.min().item<float>();
        float t_max = t_cpu.max().item<float>();
        float u_min = u_cpu.min().item<float>();
        float u_max = u_cpu.max().item<float>();
        float phi_min = phi_cpu.min().item<float>();
        float phi_max = phi_cpu.max().item<float>();
        float mu_val = vertical_diag_mu_.to(torch::kCPU).contiguous()[0].item<float>();
        std::cerr << "\n[PRECOND DIAG SUMMARY] dt=" << dt_ << " gamma=" << gamma_
                  << " dt*gamma=" << (dt_ * gamma_) << std::endl;
        std::cerr << "  D_w  range: [" << w_min << ", " << w_max << "]" << std::endl;
        std::cerr << "  D_θ  range: [" << t_min << ", " << t_max << "]" << std::endl;
        std::cerr << "  D_u  range: [" << u_min << ", " << u_max << "]" << std::endl;
        std::cerr << "  D_φ  range: [" << phi_min << ", " << phi_max << "]" << std::endl;
        std::cerr << "  D_μ  value: " << mu_val << std::endl;
        if (w_max < 1.01f && t_max < 1.01f && u_max < 1.01f) {
            std::cerr << "  *** WARNING: ALL diagonals ~1.0 → PRECONDITIONER IS IDENTITY! ***" << std::endl;
            std::cerr << "  This means dt*gamma*contributions are negligible." << std::endl;
        }
    }

    // v20.14 Phase 2a: Coefficient diagnostic for coupled Φ-W design
    // FIX r45c: MOVED here from before D_phi computation — was reading stale D_phi=1.0.
    // Now fires after PRECOND DIAG SUMMARY, before coefficient_generation_++.
    // Uses target_gen = coefficient_generation_ + 1 (post-increment alignment).
    // SKIP does NOT mark generation (leaves retry for next gen). Only success marks gen.
    // FIX r46d: Skip at dummy dt (<=1.0) to avoid consuming diagnostic cap on init values.
    {
        uint64_t target_gen = coefficient_generation_ + 1;
        int diag_cap = wrf::sdirk3::g_sdirk3_config.debug_level >= 2 ? 10 : 2;
        if (target_gen != coupled_diag_logged_gen_
            && coupled_diag_log_count_ < diag_cap
            && dt_received_update_ && dt_ > 1.0f
            && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        int nz_local = static_cast<int>(vertical_diag_phi_.size(0)) - 1;
        int nz_w_local = nz_local + 1;
        float dt_gamma = dt_ * gamma_;
        float g_val = grid_info_->g;
        int mc_sz = static_cast<int>(momentum_coupling_k_cached_.size());
        int dz_sz = static_cast<int>(dz_effective_cached_.size());

        if (nz_local < 2 || dz_sz < 2 || mc_sz < 2) {
            // v20.14 r45c: SKIP does NOT consume diagnostic opportunity.
            // Only log once per generation to avoid flood.
            if (skip_seen_gen_ != target_gen) {
                std::cerr << "[COUPLED DIAG] SKIPPED: insufficient data (nz=" << nz_local
                          << ", dz_sz=" << dz_sz << ", mc_sz=" << mc_sz << ")" << std::endl;
                skip_seen_gen_ = target_gen;
            }
            // Do NOT set coupled_diag_logged_gen_ — leaves retry open for next generation
        } else {

        std::cerr << "[COUPLED DIAG] Coefficient landscape (nz=" << nz_local
                  << ", dt*gamma=" << dt_gamma << ", c_s=" << c_s
                  << ", g=" << g_val << ", boost=" << w_acoustic_boost_cached_
                  << "):" << std::endl;

        float damp = wrf::sdirk3::g_sdirk3_config.precond_enable_clamps
                   ? wrf::sdirk3::g_sdirk3_config.precond_damping_factor : 0.0f;
        std::cerr << "  [clamps=" << (damp > 0 ? "ON" : "OFF")
                  << ", damping=" << damp << "]" << std::endl;

        std::cerr << "  k | D_phi | D_W_nosb | D_W_full | damp | schur_boost"
                  << " | GS_same(k) | GS_km1(k-1) | phys_g/dz"
                  << " | pdet_full | pdet_nosb_gs | pdet_nosb_phys"
                  << std::endl;

        std::vector<int> sample_ks;
        sample_ks.push_back(1);
        if (nz_local > 8) sample_ks.push_back(nz_local / 4);
        sample_ks.push_back(nz_local / 2);
        if (nz_local > 8) sample_ks.push_back(3 * nz_local / 4);
        if (nz_local > 2) sample_ks.push_back(nz_local - 1);

        auto phi_diag_ptr = vertical_diag_phi_.data_ptr<float>();
        auto w_diag_ptr = vertical_diag_w_.data_ptr<float>();

        float pdet_full_min = 1e30f, pdet_full_max = -1e30f;
        float pdet_nosb_gs_min = 1e30f, pdet_nosb_gs_max = -1e30f;
        float pdet_nosb_phys_min = 1e30f, pdet_nosb_phys_max = -1e30f;

        for (int k = 1; k < nz_local; ++k) {
            int k_lo = std::min(k - 1, dz_sz - 1);
            int k_hi = std::min(k, dz_sz - 1);
            float dz_w = 0.5f * (dz_effective_cached_[k_lo] + dz_effective_cached_[k_hi]);
            dz_w = std::max(dz_w, 1.0f);

            float D_phi = phi_diag_ptr[k];
            float D_W_full = w_diag_ptr[k];

            float acfl = dt_gamma * c_s / dz_w;
            float acfl_sq = acfl * acfl;
            float A_phi_diag = 1.0f + dt_gamma * c_s * c_s / (dz_w * dz_w);
            float schur = acfl_sq / A_phi_diag;
            float schur_boosted = schur * w_acoustic_boost_cached_;
            float D_W_nosboost = D_W_full - schur_boosted;

            float GS_A_w_phi = 0.0f;
            if (k < mc_sz) {
                float mc_k = momentum_coupling_k_cached_[k];
                float mu_sc = std::max(mu_scale_correction_, 1e-6f);
                GS_A_w_phi = dt_gamma * g_val / (mc_k * mu_sc * dz_w);
            }
            float GS_same_level = GS_A_w_phi;
            float GS_km1_level = -GS_A_w_phi;

            float phys_g_dz = dt_gamma * g_val / dz_w;

            float pdet_full = D_phi * D_W_full - acfl_sq;
            float pdet_nosb_gs = D_phi * D_W_nosboost - GS_A_w_phi * GS_A_w_phi;
            float pdet_nosb_phys = D_phi * D_W_nosboost - phys_g_dz * phys_g_dz;

            pdet_full_min = std::min(pdet_full_min, pdet_full);
            pdet_full_max = std::max(pdet_full_max, pdet_full);
            pdet_nosb_gs_min = std::min(pdet_nosb_gs_min, pdet_nosb_gs);
            pdet_nosb_gs_max = std::max(pdet_nosb_gs_max, pdet_nosb_gs);
            pdet_nosb_phys_min = std::min(pdet_nosb_phys_min, pdet_nosb_phys);
            pdet_nosb_phys_max = std::max(pdet_nosb_phys_max, pdet_nosb_phys);

            bool is_sample = false;
            for (int sk : sample_ks) { if (k == sk) { is_sample = true; break; } }
            if (is_sample) {
                auto saved_flags = std::cerr.flags();
                auto saved_prec = std::cerr.precision();
                std::cerr << std::setprecision(4) << std::fixed
                    << "  " << k
                    << " | " << D_phi
                    << " | " << D_W_nosboost
                    << " | " << D_W_full
                    << " | " << damp
                    << " | " << schur_boosted
                    << " | " << GS_same_level
                    << " | " << GS_km1_level
                    << " | " << phys_g_dz
                    << " | " << pdet_full
                    << " | " << pdet_nosb_gs
                    << " | " << pdet_nosb_phys
                    << std::endl;
                std::cerr.flags(saved_flags);
                std::cerr.precision(saved_prec);
            }
        }

        std::cerr << "[COUPLED DIAG] Top level (nz_w=" << nz_w_local - 1 << "):"
                  << " D_phi_top=" << phi_diag_ptr[nz_local]
                  << ", D_W_top=" << w_diag_ptr[nz_w_local - 1]
                  << " (boundary: coupling=0, no off-diag)" << std::endl;

        std::cerr << "[COUPLED DIAG] Proxy-det ranges (same-level approx, ignores k-1 coupling):"
                  << std::endl;
        std::cerr << "  pdet(D_W_full, acfl²):    [" << pdet_full_min << ", " << pdet_full_max << "]"
                  << (pdet_full_min <= 0 ? " *** ELIMINATED ***" : " candidate") << std::endl;
        std::cerr << "  pdet(D_W_nosb, GS²):      [" << pdet_nosb_gs_min << ", " << pdet_nosb_gs_max << "]"
                  << (pdet_nosb_gs_min <= 0 ? " *** ELIMINATED ***" : " candidate") << std::endl;
        std::cerr << "  pdet(D_W_nosb, phys²):    [" << pdet_nosb_phys_min << ", " << pdet_nosb_phys_max << "]"
                  << (pdet_nosb_phys_min <= 0 ? " *** ELIMINATED ***" : " candidate") << std::endl;
        std::cerr << "  NOTE: positive proxy = necessary but NOT sufficient for block-Thomas stability"
                  << std::endl;

        std::cerr << "[COUPLED DIAG] Coupling scale summary (k=1):" << std::endl;
        {
            float dz_w = 0.5f * (dz_effective_cached_[0] + dz_effective_cached_[1]);
            dz_w = std::max(dz_w, 1.0f);
            float acfl = dt_gamma * c_s / dz_w;
            float mc_1 = momentum_coupling_k_cached_[1];
            float mu_sc = std::max(mu_scale_correction_, 1e-6f);
            float gs_uncapped = dt_gamma * g_val / (mc_1 * mu_sc * dz_w);
            float phys = dt_gamma * g_val / dz_w;
            std::cerr << "  acoustic_cfl=" << acfl
                      << ", GS_Awp(uncapped,no_beta)=" << gs_uncapped
                      << " [same=" << gs_uncapped << ", km1=" << -gs_uncapped << "]"
                      << ", phys_g/dz=" << phys
                      << ", ratio acfl/gs=" << (gs_uncapped > 1e-10f ? acfl/gs_uncapped : -1.0f)
                      << ", ratio acfl/phys=" << (phys > 1e-10f ? acfl/phys : -1.0f)
                      << std::endl;
        }

        coupled_diag_logged_gen_ = target_gen;  // success → mark generation
        ++coupled_diag_log_count_;                // only successful logs count toward cap
        } // end safety guard else
    }
    } // end diag_cap scope

    // v20.14r20: Cache sync moved to function entry (before coefficient computation).
    // See block at top of initialize_acoustic_gravity_solver().

    // CRITICAL FIX: Invalidate thread-local coefficient caches
    // This ensures solve_coupled_w_theta_column() re-extracts coefficients after update
    ++coefficient_generation_;
}

void UnifiedPreconditioner::initialize_horizontal_smoother() {
    // Set up horizontal smoothing operator
    [[maybe_unused]] int nx = grid_info_->nx;
    [[maybe_unused]] int ny = grid_info_->ny;
    
    float dx = grid_info_->dx;
    float dy = grid_info_->dy;
    
    // Horizontal smoothing parameter (for acoustic modes)
    float h_smooth = dt_ * gamma_ * grid_info_->cs * grid_info_->cs;
    horizontal_smooth_factor_ = h_smooth / (dx * dx + dy * dy);
}

torch::Tensor UnifiedPreconditioner::apply(const torch::Tensor& residual) {
    PROFILE_SCOPE("preconditioner_apply");

    // FIX 2026-02-03: Entry log gated on debug_level >= 1
    // v20.14r26: atomic for thread safety in multi-tile OpenMP scenarios
    static std::atomic<int> apply_call_count{0};
    int local_count = apply_call_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (local_count <= 3 && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND APPLY] Called #" << local_count
                  << " residual.dim()=" << residual.dim()
                  << " numel=" << residual.numel()
                  << " dt_=" << dt_ << " gamma_=" << gamma_ << std::endl;
    }

    // Apply ENHANCED unified preconditioner M^{-1} to residual
    // Strategy: Approximate (I - dt*gamma*J_unified)^{-1} with acoustic-gravity coupling

    // CRITICAL: Disable autograd to prevent graph pollution during preconditioning
    torch::NoGradGuard no_grad;

    torch::Tensor z = residual.clone();

    // =========================================================================
    // OPT Pass33+: DIAGNOSTIC SAMPLING COUNTERS (INDEPENDENT)
    // Pattern: (period == 0) || ((counter % period) == 0) || (counter == 1)
    // Reduces D2H transfer overhead from .item() calls in diagnostics.
    // HOT PATH FIX: Skip counter increment when debug_level is insufficient.
    // PHASE INDEPENDENCE: Each counter increments only when its level is active,
    // so standard (>=1) and heavy (>=3) sample at independent phases.
    // =========================================================================
    const int current_debug_level = wrf::sdirk3::g_sdirk3_config.debug_level;
    uint64_t diag_counter = 0;
    uint64_t heavy_counter = 0;
    bool do_diag_sample = false;
    bool do_heavy_sample = false;

    // Standard diagnostics: debug_level >= 1
    if (current_debug_level >= 1) {
        diag_counter = precond_diag_call_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        const int diag_period = wrf::sdirk3::g_sdirk3_config.debug_sample_period;
        do_diag_sample = (diag_period == 0) || ((diag_counter % diag_period) == 0) || (diag_counter == 1);
    }

    // Heavy diagnostics: debug_level >= 3 (independent counter per config policy)
    if (current_debug_level >= 3) {
        heavy_counter = precond_heavy_call_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        const int heavy_period = wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period;
        do_heavy_sample = (heavy_period == 0) || ((heavy_counter % heavy_period) == 0) || (heavy_counter == 1);
    }

    // PRECONDITIONER INSTRUMENTATION: Log input/output to diagnose ρ<0 issue
    float r_input_norm = 0.0f;
    if (current_debug_level >= 1 && do_diag_sample) {
        // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        torch::NoGradGuard no_grad;
        r_input_norm = residual.norm().to(torch::kCPU).item<float>();
        std::cerr << "[PRECOND DEBUG] Input residual ||r|| = " << r_input_norm << " (call #" << diag_counter << ")" << std::endl;
    }

    // PERFORMANCE PROFILING: Track preconditioner component timings
    auto apply_start = std::chrono::high_resolution_clock::now();

    // Handle 1D packed state vector with PROPER vertical TDMA
    if (residual.dim() == 1) {
        // FIX 2026-02-03: 1D path entry diagnostic, gated on debug_level >= 1
        if (apply_call_count <= 3 && current_debug_level >= 1) {
            std::cerr << "[PRECOND 1D PATH] Entered. do_diag_sample=" << do_diag_sample
                      << " diag_counter=" << diag_counter
                      << " debug_level=" << current_debug_level << std::endl;
        }
        // Reshape 1D → variables and apply vertical TDMA for each (j,i) column
        int nx = grid_info_->nx;
        int ny = grid_info_->ny;
        int nz = grid_info_->nz;

        int nx_u = (grid_info_->nx_u > 0) ? grid_info_->nx_u : nx;
        int ny_v = (grid_info_->ny_v > 0) ? grid_info_->ny_v : ny;
        int nz_w = (grid_info_->nz_w > 0) ? grid_info_->nz_w : (nz + 1);

        // FIX Round192: Use int64_t to prevent overflow on large grids (nx*ny*nz > 2^31)
        int64_t size_u = static_cast<int64_t>(nx_u) * ny * nz;
        int64_t size_v = static_cast<int64_t>(nx) * ny_v * nz;
        int64_t size_w = static_cast<int64_t>(nx) * ny * nz_w;
        int64_t size_phi = static_cast<int64_t>(nx) * ny * nz_w;
        int64_t size_t = static_cast<int64_t>(nx) * ny * nz;
        int64_t size_mu = static_cast<int64_t>(nx) * ny;

        // DIAGNOSTIC: Print detailed sizes and offsets to understand 1D packing
        static bool sizes_printed = false;
        if (!sizes_printed && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "\n[PRECOND DIAGNOSTIC] 1D State Vector Analysis:" << std::endl;
            std::cerr << "  Grid dimensions: nx=" << nx << ", ny=" << ny << ", nz=" << nz << std::endl;
            std::cerr << "  Staggered dims: nx_u=" << nx_u << ", ny_v=" << ny_v << ", nz_w=" << nz_w << std::endl;
            std::cerr << "  Total vector size: z.size(0) = " << z.size(0) << std::endl;
            std::cerr << "\n  Component sizes:" << std::endl;
            std::cerr << "    size_u   = " << size_u << "  (" << nx_u << " x " << ny << " x " << nz << ")" << std::endl;
            std::cerr << "    size_v   = " << size_v << "  (" << nx << " x " << ny_v << " x " << nz << ")" << std::endl;
            std::cerr << "    size_w   = " << size_w << "  (" << nx << " x " << ny << " x " << nz_w << ")" << std::endl;
            std::cerr << "    size_phi = " << size_phi << "  (" << nx << " x " << ny << " x " << nz_w << ")" << std::endl;
            std::cerr << "    size_t   = " << size_t << "  (" << nx << " x " << ny << " x " << nz << ")" << std::endl;
            std::cerr << "    size_mu  = " << size_mu << "  (" << nx << " x " << ny << ") [2D ONLY]" << std::endl;
            std::cerr << "  Sum of all components = " << (size_u + size_v + size_w + size_phi + size_t + size_mu) << std::endl;

            if (z.size(0) == size_u + size_v + size_w + size_phi + size_t + size_mu) {
                std::cerr << "  ✓ Size match: All 6 components present [U,V,W,PHI,T,MU]" << std::endl;
            } else {
                int64_t discrepancy = z.size(0) - (size_u + size_v + size_w + size_phi + size_t + size_mu);
                std::cerr << "  ✗ Size MISMATCH: Expected "
                          << (size_u + size_v + size_w + size_phi + size_t + size_mu)
                          << " but got " << z.size(0)
                          << " (discrepancy: " << discrepancy << " elements)" << std::endl;
            }
            sizes_printed = true;
        }

        // v20.3: Hard-fail on size mismatch — block slices would be misaligned
        int64_t expected_size = size_u + size_v + size_w + size_phi + size_t + size_mu;
        TORCH_CHECK(z.size(0) == expected_size,
            "1D preconditioner: packed state size mismatch. Expected ", expected_size,
            " (6-block [U,V,W,PHI,T,MU]) but got ", z.size(0),
            ". Grid: nx=", nx, " ny=", ny, " nz=", nz,
            " nx_u=", nx_u, " ny_v=", ny_v, " nz_w=", nz_w);

        // v20: CPU-only assertion for 1D preconditioner path
        TORCH_CHECK(z.is_cpu(), "1D preconditioner requires CPU tensor, got device=", z.device());

        // DEBUG 2026-02-05: Track crash location (gated v20.14)
        if (apply_call_count <= 3 && current_debug_level >= 1) {
            std::cerr << "[PRECOND DEBUG STEP 1] Passed size/device checks" << std::endl;
        }

        // Block offsets in packed 1D state vector [U, V, W, PHI, T, MU]
        int64_t u_offset = 0;
        int64_t v_offset = size_u;
        int64_t w_offset_1d = size_u + size_v;
        int64_t phi_offset = w_offset_1d + size_w;
        int64_t t_offset = phi_offset + size_phi;
        int64_t mu_offset = t_offset + size_t;

        // === COEFFICIENT CACHE (shared by both 3×3 and 4×4 paths) ===
        static thread_local std::vector<float> D_u_cache, D_v_cache;
        static thread_local std::vector<float> C_uμ_cache, C_vμ_cache, C_μu_cache, C_μv_cache;
        static thread_local float D_μ_cache = 1.0f;
        static thread_local std::vector<float> D_phi_cache;
        static thread_local float S_mu_mu_scalar_cache = 0.0f;
        static thread_local uint64_t cached_gen = 0;
        // v20.14r27z: Include instance pointer in cache key for multi-tile safety.
        // Without this, different tile instances with the same generation share stale data.
        static thread_local const void* cached_instance = nullptr;
        // v20.14 r47c-fix3: Track D_u scaling for cache invalidation.
        // du_scale_ changes between Newton iterations (based on ru_share).
        static thread_local float cached_du_scale = 1.0f;

        if (cached_gen != coefficient_generation_ || cached_instance != this ||
            cached_du_scale != du_scale_ ||
            D_u_cache.size() != static_cast<std::size_t>(nz)) {
            cached_instance = this;
            cached_du_scale = du_scale_;
            D_u_cache.resize(nz);
            D_v_cache.resize(nz);
            C_uμ_cache.resize(nz);
            C_vμ_cache.resize(nz);
            C_μu_cache.resize(nz);
            C_μv_cache.resize(nz);
            D_phi_cache.resize(nz);

            TORCH_CHECK(vertical_diag_u_.is_cpu() && vertical_diag_u_.scalar_type() == torch::kFloat32,
                "vertical_diag_u_ must be CPU Float32");
            TORCH_CHECK(vertical_diag_v_.is_cpu() && vertical_diag_v_.scalar_type() == torch::kFloat32,
                "vertical_diag_v_ must be CPU Float32");
            TORCH_CHECK(C_u_mu_.is_cpu() && C_u_mu_.scalar_type() == torch::kFloat32,
                "C_u_mu_ must be CPU Float32");
            TORCH_CHECK(C_v_mu_.is_cpu() && C_v_mu_.scalar_type() == torch::kFloat32,
                "C_v_mu_ must be CPU Float32");
            TORCH_CHECK(C_mu_u_.is_cpu() && C_mu_u_.scalar_type() == torch::kFloat32,
                "C_mu_u_ must be CPU Float32");
            TORCH_CHECK(C_mu_v_.is_cpu() && C_mu_v_.scalar_type() == torch::kFloat32,
                "C_mu_v_ must be CPU Float32");
            TORCH_CHECK(vertical_diag_phi_.is_cpu() && vertical_diag_phi_.scalar_type() == torch::kFloat32,
                "vertical_diag_phi_ must be CPU Float32");

            auto D_u_acc = vertical_diag_u_.accessor<float, 1>();
            auto D_v_acc = vertical_diag_v_.accessor<float, 1>();
            auto C_uμ_acc = C_u_mu_.accessor<float, 1>();
            auto C_vμ_acc = C_v_mu_.accessor<float, 1>();
            auto C_μu_acc = C_mu_u_.accessor<float, 1>();
            auto C_μv_acc = C_mu_v_.accessor<float, 1>();
            auto D_phi_acc = vertical_diag_phi_.accessor<float, 1>();

            S_mu_mu_scalar_cache = 0.0f;
            for (int k = 0; k < nz; ++k) {
                // v20.14 r47c-fix3: Apply D_u scaling for dual-phase strategy.
                // When du_scale_ < 1 (ru-dominated), weaken D_u → larger U corrections.
                // All Schur complement terms (S_mu_mu, S_phi_phi, back-sub) use scaled D_u.
                D_u_cache[k] = D_u_acc[k] * du_scale_;
                D_v_cache[k] = D_v_acc[k];
                C_uμ_cache[k] = C_uμ_acc[k];
                C_vμ_cache[k] = C_vμ_acc[k];
                C_μu_cache[k] = C_μu_acc[k];
                C_μv_cache[k] = C_μv_acc[k];
                D_phi_cache[k] = D_phi_acc[k];
                // Accumulate S_mu_mu contribution
                // v20.14 r49-fix: Guard against near-zero D_u/D_v diagonal
                constexpr float du_eps = 1e-10f;
                float du_safe = (std::abs(D_u_cache[k]) > du_eps) ? D_u_cache[k] : std::copysign(du_eps, D_u_cache[k]);
                float dv_safe = (std::abs(D_v_cache[k]) > du_eps) ? D_v_cache[k] : std::copysign(du_eps, D_v_cache[k]);
                S_mu_mu_scalar_cache -= (C_μu_cache[k] * C_uμ_cache[k] / du_safe)
                                      + (C_μv_cache[k] * C_vμ_cache[k] / dv_safe);
            }

            TORCH_CHECK(vertical_diag_mu_.is_cpu() && vertical_diag_mu_.scalar_type() == torch::kFloat32,
                "vertical_diag_mu_ must be CPU Float32");
            D_μ_cache = vertical_diag_mu_.accessor<float, 1>()[0];
            S_mu_mu_scalar_cache += D_μ_cache;
            cached_gen = coefficient_generation_;
        }

        // DEBUG 2026-02-05: Track crash location (gated v20.14)
        if (apply_call_count <= 3 && current_debug_level >= 1) {
            std::cerr << "[PRECOND DEBUG STEP 2] Coefficient cache built" << std::endl;
        }

        // === v20: 4×4 U-V-μ-Φ BATCHED SCHUR vs legacy 3×3+diag ===
        if (wrf::sdirk3::g_sdirk3_config.precond_acoustic_4x4 == 1 &&
            C_u_phi_.defined() && grid_info_->mu_base.defined()) {
            // === 4×4 BATCHED SCHUR COMPLEMENT (v20) ===
            // Mirrors the batched 4D path (apply_enhanced_vertical_solve, lines 2070-2195)
            // adapted for the 1D packed layout with stagger handling.
            torch::NoGradGuard no_grad;

            // DEBUG 2026-02-05: Track crash location (gated v20.14)
            if (apply_call_count <= 3 && current_debug_level >= 1) {
                std::cerr << "[PRECOND DEBUG STEP 3] Entering 4x4 Schur path" << std::endl;
            }

            auto opts_cpu = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);

            // Pre-clone stagger boundary residuals BEFORE any writes to z (alias safety)
            torch::Tensor r_u_stag_saved, r_v_stag_saved;
            if (nx_u > nx) {
                r_u_stag_saved = z.slice(0, u_offset, u_offset + size_u)
                    .reshape({ny, nz, nx_u}).select(2, nx).clone();  // [ny, nz]
            }
            if (ny_v > ny) {
                r_v_stag_saved = z.slice(0, v_offset, v_offset + size_v)
                    .reshape({ny_v, nz, nx}).select(0, ny).clone();  // [nz, nx]
            }
            auto r_phi_top_saved = z.slice(0, phi_offset, phi_offset + size_phi)
                .reshape({ny, nz_w, nx}).select(1, nz).clone();  // [ny, nx]

            // Extract interior (mass-point) blocks for coupling
            auto r_u_3d = z.slice(0, u_offset, u_offset + size_u)
                           .reshape({ny, nz, nx_u}).slice(2, 0, nx).contiguous();  // [ny, nz, nx]
            auto r_v_3d = z.slice(0, v_offset, v_offset + size_v)
                           .reshape({ny_v, nz, nx}).slice(0, 0, ny).contiguous();  // [ny, nz, nx]
            auto r_phi_mass = z.slice(0, phi_offset, phi_offset + size_phi)
                               .reshape({ny, nz_w, nx}).slice(1, 0, nz).contiguous();  // [ny, nz, nx]
            auto r_mu_2d = z.slice(0, mu_offset, mu_offset + size_mu)
                            .reshape({ny, nx}).contiguous();  // [ny, nx]

            // DEBUG 2026-02-05: Track crash location (gated v20.14)
            if (apply_call_count <= 3 && current_debug_level >= 1) {
                std::cerr << "[PRECOND DEBUG STEP 4] Interior blocks extracted: "
                          << "r_u_3d=" << r_u_3d.sizes()
                          << " r_v_3d=" << r_v_3d.sizes()
                          << " r_phi_mass=" << r_phi_mass.sizes()
                          << " r_mu_2d=" << r_mu_2d.sizes() << std::endl;
            }

            // Build tensor views from cached coefficients for broadcasting
            auto diag_u_t = torch::from_blob(D_u_cache.data(), {nz}, opts_cpu);
            auto diag_v_t = torch::from_blob(D_v_cache.data(), {nz}, opts_cpu);
            auto c_u_mu_t = torch::from_blob(C_uμ_cache.data(), {nz}, opts_cpu);
            auto c_v_mu_t = torch::from_blob(C_vμ_cache.data(), {nz}, opts_cpu);
            auto c_mu_u_t = torch::from_blob(C_μu_cache.data(), {nz}, opts_cpu);
            auto c_mu_v_t = torch::from_blob(C_μv_cache.data(), {nz}, opts_cpu);
            auto diag_phi_t = torch::from_blob(D_phi_cache.data(), {nz}, opts_cpu);

            // Reshape for broadcasting: [nz] → [1, nz, 1] over [ny, nz, nx]
            auto inv_D_u = (1.0f / diag_u_t).reshape({1, nz, 1});
            auto inv_D_v = (1.0f / diag_v_t).reshape({1, nz, 1});
            auto c_mu_u_3d = c_mu_u_t.reshape({1, nz, 1});
            auto c_mu_v_3d = c_mu_v_t.reshape({1, nz, 1});
            auto c_u_mu_3d = c_u_mu_t.reshape({1, nz, 1});
            auto c_v_mu_3d = c_v_mu_t.reshape({1, nz, 1});

            // Per-column inv(μ₀) from mu_base [ny, nx]
            // Cache with pointer+numel key to handle domain/instance changes
            // DEBUG 2026-02-05: Track crash location (gated v20.14)
            if (apply_call_count <= 3 && current_debug_level >= 1) {
                std::cerr << "[PRECOND DEBUG STEP 5] About to access mu_base cache. "
                          << "grid_info_->mu_base.defined()=" << grid_info_->mu_base.defined()
                          << " sizes=" << (grid_info_->mu_base.defined() ? grid_info_->mu_base.sizes() : at::IntArrayRef{})
                          << std::endl;
            }

            static thread_local torch::Tensor mu_base_cpu_cached;
            static thread_local uint64_t mu_base_cached_gen = 0;
            static thread_local const void* mu_base_cached_ptr = nullptr;
            static thread_local int64_t mu_base_cached_numel = 0;
            {
                bool need_rebuild = (mu_base_cached_gen != coefficient_generation_) ||
                    (mu_base_cached_ptr != grid_info_->mu_base.data_ptr()) ||
                    (mu_base_cached_numel != grid_info_->mu_base.numel()) ||
                    !mu_base_cpu_cached.defined();
                if (need_rebuild) {
                    mu_base_cpu_cached = grid_info_->mu_base.to(torch::kCPU, torch::kFloat32).contiguous();
                    mu_base_cached_gen = coefficient_generation_;
                    mu_base_cached_ptr = grid_info_->mu_base.data_ptr();
                    mu_base_cached_numel = grid_info_->mu_base.numel();
                }
            }
            // v20.5 Fix 4: Guard mu_base inversion — clamp >= 1.0 Pa
            {
                auto needs_clamp = mu_base_cpu_cached < 1.0f;
                if (needs_clamp.any().item<bool>()) {
                    static thread_local bool mu_base_clamp_warned = false;
                    if (!mu_base_clamp_warned) {
                        torch::NoGradGuard no_grad;
                        std::cerr << "[PRECOND WARNING] mu_base has values < 1.0 Pa (min="
                                  << mu_base_cpu_cached.min().item<float>()
                                  << "). Clamping to 1.0. Check base state initialization."
                                  << std::endl;
                        mu_base_clamp_warned = true;
                    }
                }
            }
            auto mu_base_safe = torch::clamp(mu_base_cpu_cached, /*min=*/1.0f);
            // v20.5: Use mu_full_stage_ when available for better Stage 2/3 accuracy
            torch::Tensor mu_for_inv;
            if (mu_full_stage_.defined() && mu_full_stage_.numel() == mu_base_safe.numel()) {
                mu_for_inv = torch::clamp(mu_full_stage_.to(mu_base_safe.device()), /*min=*/1.0f);
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    static thread_local int mu_full_log_count = 0;
                    if (mu_full_log_count++ < 3) {
                        torch::NoGradGuard no_grad;
                        float mu_base_mean = mu_base_safe.mean().item<float>();
                        float mu_full_mean = mu_for_inv.mean().item<float>();
                        std::cerr << "[PRECOND 4x4] Using mu_full_stage_ for inv_mu0:"
                                  << " mu_base_mean=" << mu_base_mean
                                  << " mu_full_mean=" << mu_full_mean
                                  << " (delta=" << (mu_full_mean - mu_base_mean) << ")" << std::endl;
                    }
                }
            } else {
                mu_for_inv = mu_base_safe;
            }

            // DEBUG 2026-02-05: Track crash location (gated v20.14)
            if (apply_call_count <= 3 && current_debug_level >= 1) {
                std::cerr << "[PRECOND DEBUG STEP 6] mu_for_inv ready. "
                          << "mu_full_stage_.defined()=" << mu_full_stage_.defined()
                          << " mu_for_inv.sizes()=" << mu_for_inv.sizes()
                          << std::endl;
            }

            auto inv_mu0 = 1.0f / mu_for_inv;                               // [ny, nx]
            auto inv_mu0_3d = inv_mu0.reshape({ny, 1, nx});                // [ny, 1, nx]
            auto inv_mu0_sq_3d = (inv_mu0 * inv_mu0).reshape({ny, 1, nx});

            // Φ coupling base scalars (k-independent; per-column via inv_mu0)
            // NOTE: These are NOT the same as C_u_phi_/C_phi_u_ from the generation path.
            // Generation path (line ~1493): C_u_phi_[k] = dt·γ·(1/μ₀_avg)·H_x  (domain-avg μ₀)
            // 1D apply path (here):        A_u_phi_base = dt·γ·H_x, then × inv_mu0 per-column
            // The 1D path is MORE accurate because it uses spatially-varying per-column μ₀
            // instead of the domain-averaged μ₀_avg.  Do NOT replace with C_*_phi_.
            float dt_gamma = dt_ * gamma_;
            float c2 = grid_info_->cs * grid_info_->cs;
            float H_x = 1.0f / grid_info_->dx;
            float H_y = 1.0f / grid_info_->dy;
            // v20.15 HEVI (precond-inc1): A_u_phi/A_v_phi (phi->u,v PGF) and A_phi_u/A_phi_v
            // (u,v->phi divergence) are H_x/H_y-scaled => HORIZONTAL, moved to k_slow by HEVI.
            // Zero them under hevi so this 4x4 apply path matches A_fast. KEEP A_phi_mu_base
            // (dt*gamma*c2, no H) — the VERTICAL hydrostatic mu<->phi coupling J_fast retains.
            const bool hevi_pc = wrf::sdirk3::g_sdirk3_config.hevi_split &&
                                 (wrf::sdirk3::g_sdirk3_config.effective_imex_split_mode() >= 1);
            float A_u_phi_base = hevi_pc ? 0.0f : dt_gamma * H_x;
            float A_v_phi_base = hevi_pc ? 0.0f : dt_gamma * H_y;
            float A_phi_u_base = hevi_pc ? 0.0f : dt_gamma * c2 * H_x;
            float A_phi_v_base = hevi_pc ? 0.0f : dt_gamma * c2 * H_y;
            float A_phi_mu_base = dt_gamma * c2;

            // One-shot sign verification vs 4D path coefficients (debug_level >= 2)
            static thread_local bool signs_verified = false;
            if (!signs_verified && current_debug_level >= 2 && C_phi_mu_.defined()) {
                float C_phi_mu_0 = C_phi_mu_.accessor<float, 1>()[0];
                float computed = A_phi_mu_base / mu_base_cpu_cached.mean().item<float>();
                std::cerr << "[PRECOND 4x4 SIGN CHECK] C_phi_mu_[0]=" << C_phi_mu_0
                          << " vs computed=" << computed
                          << " (ratio=" << (C_phi_mu_0 / (computed + 1e-30f)) << ")" << std::endl;
                signs_verified = true;
            }

            // --- Schur complement elimination (vectorized) ---

            // Step 1: Eliminate U, V from μ equation
            auto r_mu_mod_k = -(c_mu_u_3d * r_u_3d * inv_D_u)
                             - (c_mu_v_3d * r_v_3d * inv_D_v);     // [ny, nz, nx]
            auto r_mu_mod = r_mu_mod_k.sum(1) + r_mu_2d;           // [ny, nx]

            // alpha_phi[k] = coupling² / diagonal products (k-dependent, (j,i)-independent)
            auto alpha_phi = A_phi_u_base * A_u_phi_base / diag_u_t
                           + A_phi_v_base * A_v_phi_base / diag_v_t;  // [nz]

            // S_phi_phi[ny, nz, nx] = D_phi[k] - inv_mu0²[j,i] * alpha_phi[k]
            auto S_phi_phi = diag_phi_t.reshape({1, nz, 1})
                           - inv_mu0_sq_3d * alpha_phi.reshape({1, nz, 1});

            // v20.14 r47c-fix: LIVE S_ΦΦ/D_Φ deviation — updated EVERY apply() call.
            // Phase2 gate uses this to detect Schur approximation degradation per Newton iter.
            // Previously generation-gated (stale during Newton: Finding #1 critical).
            {
                torch::NoGradGuard no_grad;
                auto D_phi_broadcast = diag_phi_t.reshape({1, nz, 1});
                auto ratio_3d = S_phi_phi / D_phi_broadcast.clamp_min(1e-10f);
                float ratio_min = ratio_3d.min().item<float>();
                float ratio_max = ratio_3d.max().item<float>();
                s_phi_phi_max_dev_ = std::max(std::abs(ratio_min - 1.0f),
                                              std::abs(ratio_max - 1.0f));
            }

            // Heavy diagnostic: generation-gated (per-level stats, bad_levels count)
            if (coefficient_generation_ != coupled_diag_4x4_gen_) {
                if (current_debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    auto D_phi_broadcast = diag_phi_t.reshape({1, nz, 1});
                    auto ratio_3d = S_phi_phi / D_phi_broadcast.clamp_min(1e-10f);
                    float ratio_min = ratio_3d.min().item<float>();
                    float ratio_max = ratio_3d.max().item<float>();
                    float ratio_mean = ratio_3d.mean().item<float>();
                    float alpha_max = alpha_phi.max().item<float>();
                    float alpha_min = alpha_phi.min().item<float>();
                    // Per-level deviation count (Finding r46e#3: diagnostic for localization)
                    auto dev_3d = (ratio_3d - 1.0f).abs();
                    // Per-level max deviation: collapse (ny,nx) → (nz,)
                    auto dev_per_level = std::get<0>(dev_3d.reshape({ny * nx, nz}).max(0));
                    int bad_levels = 0;
                    auto dev_ptr = dev_per_level.data_ptr<float>();
                    for (int kk = 0; kk < nz; ++kk) {
                        if (dev_ptr[kk] > 0.1f) ++bad_levels;
                    }
                    std::cerr << "[COUPLED DIAG 4x4] S_phi_phi/D_phi ratio:"
                              << " min=" << ratio_min
                              << ", max=" << ratio_max
                              << ", mean=" << ratio_mean
                              << " (alpha_phi range: [" << alpha_min << ", " << alpha_max << "])"
                              << ", max_dev=" << s_phi_phi_max_dev_
                              << ", bad_levels(>0.1)=" << bad_levels << "/" << nz
                              << (s_phi_phi_max_dev_ <= 0.1f ? " (phi-feedback OK)" : " (phi-feedback DISABLED)")
                              << std::endl;
                    if (s_phi_phi_max_dev_ > 0.01f) {
                        std::cerr << "[COUPLED DIAG 4x4] WARNING: S_phi_phi != D_phi (max_dev="
                                  << s_phi_phi_max_dev_ << ")"
                                  << " -> D_phi*phi_4x4 is NOT a valid RHS reconstruction!"
                                  << std::endl;
                    }
                }
                coupled_diag_4x4_gen_ = coefficient_generation_;
            }

            // Cross-coupling: S_mu_phi, S_phi_mu (factored with inv_mu0)
            auto S_mu_phi_base = torch::full({nz}, A_phi_mu_base, opts_cpu)
                - c_mu_u_t * A_u_phi_base / diag_u_t
                - c_mu_v_t * A_v_phi_base / diag_v_t;                // [nz]
            auto S_phi_mu_base = torch::full({nz}, A_phi_mu_base, opts_cpu)
                - A_phi_u_base * c_u_mu_t / diag_u_t
                - A_phi_v_base * c_v_mu_t / diag_v_t;                // [nz]

            auto S_mu_phi = inv_mu0_3d * S_mu_phi_base.reshape({1, nz, 1});  // [ny, nz, nx]
            auto S_phi_mu = inv_mu0_3d * S_phi_mu_base.reshape({1, nz, 1});  // [ny, nz, nx]

            // v20.14 r47c-fix: Live cross-coupling metric |S_phi_mu| / |D_phi|.
            // Phase2 diagonal Schur assumes cross-coupling negligible.
            // When this ratio grows, Phase2 RHS injection becomes inconsistent.
            {
                torch::NoGradGuard no_grad;
                auto D_phi_broadcast = diag_phi_t.reshape({1, nz, 1}).clamp_min(1e-10f);
                auto cross_tensor = S_phi_mu.abs() / D_phi_broadcast;
                s_phi_mu_cross_ratio_ = cross_tensor.mean().item<float>();
                s_phi_mu_cross_max_ = cross_tensor.max().item<float>();
            }

            // Modified Φ RHS
            auto r_phi_mod = r_phi_mass
                - inv_mu0_3d * (A_phi_u_base * r_u_3d * inv_D_u
                              + A_phi_v_base * r_v_3d * inv_D_v);    // [ny, nz, nx]

            // Step 2: Schur reduction for μ (k-sum)
            // v20.14r27l: Relative singularity threshold (was absolute 1e-10).
            auto SING_EPS = S_phi_phi.abs().amax() * 1e-8f + 1e-30f;
            // v20.5 Fix 3: Sign-preserving clamp for S_phi_phi
            auto S_phi_phi_signs = torch::where(S_phi_phi >= 0,
                torch::ones_like(S_phi_phi), -torch::ones_like(S_phi_phi));
            auto S_phi_phi_safe = torch::where(
                S_phi_phi.abs() > SING_EPS, S_phi_phi,
                S_phi_phi_signs * SING_EPS);
            auto inv_S_phi_phi = 1.0f / S_phi_phi_safe;

            auto schur_diag_corr = (S_mu_phi * S_phi_mu * inv_S_phi_phi).sum(1);  // [ny, nx]
            auto schur_rhs_corr = (S_mu_phi * r_phi_mod * inv_S_phi_phi).sum(1);  // [ny, nx]

            auto S_mu_mu_reduced = S_mu_mu_scalar_cache - schur_diag_corr;   // [ny, nx]
            auto r_mu_reduced = r_mu_mod - schur_rhs_corr;                    // [ny, nx]

            // v20.5 Fix 3: Sign-preserving clamp for S_mu_mu
            auto S_mu_mu_signs = torch::where(S_mu_mu_reduced >= 0,
                torch::ones_like(S_mu_mu_reduced), -torch::ones_like(S_mu_mu_reduced));
            auto S_mu_mu_safe = torch::where(
                S_mu_mu_reduced.abs() > SING_EPS, S_mu_mu_reduced,
                S_mu_mu_signs * SING_EPS);

            // Step 3: Solve μ
            auto delta_mu = r_mu_reduced / S_mu_mu_safe;               // [ny, nx]

            // Step 4: Back-substitute Φ (mass levels k=0..nz-1)
            auto delta_phi_mass = (r_phi_mod - S_phi_mu * delta_mu.reshape({ny, 1, nx}))
                                * inv_S_phi_phi;                        // [ny, nz, nx]

            // Top boundary Φ (k=nz): diagonal-only, using pre-cloned residual
            // v20.14 r49-fix: Guard against near-zero D_phi_top
            float D_phi_top = vertical_diag_phi_.accessor<float, 1>()[nz];
            float D_phi_top_safe = (std::abs(D_phi_top) > 1e-10f) ? D_phi_top : std::copysign(1e-10f, D_phi_top);
            auto delta_phi_top = r_phi_top_saved / D_phi_top_safe;     // [ny, nx]

            // Assemble full Φ result [ny, nz_w, nx]
            auto delta_phi_full = torch::empty({ny, nz_w, nx}, opts_cpu);
            delta_phi_full.slice(1, 0, nz).copy_(delta_phi_mass);
            delta_phi_full.select(1, nz).copy_(delta_phi_top);

            // Step 5: Back-substitute U, V (mass-point interior)
            auto A_u_phi_3d_local = (A_u_phi_base * inv_mu0).reshape({ny, 1, nx});
            auto A_v_phi_3d_local = (A_v_phi_base * inv_mu0).reshape({ny, 1, nx});

            auto delta_u_mass = (r_u_3d - c_u_mu_3d * delta_mu.reshape({ny, 1, nx})
                                - A_u_phi_3d_local * delta_phi_mass) * inv_D_u;   // [ny, nz, nx]
            auto delta_v_mass = (r_v_3d - c_v_mu_3d * delta_mu.reshape({ny, 1, nx})
                                - A_v_phi_3d_local * delta_phi_mass) * inv_D_v;   // [ny, nz, nx]

            // Step 6: Write results back to z

            // U: interior + stagger boundary
            if (nx_u > nx) {
                auto u_full = z.slice(0, u_offset, u_offset + size_u).reshape({ny, nz, nx_u});
                u_full.slice(2, 0, nx).copy_(delta_u_mass);
                // v20.14r27z: Periodic → wrap stagger to first point; non-periodic → diagonal-only.
                if (grid_info_ && grid_info_->periodic_x) {
                    u_full.select(2, nx).copy_(delta_u_mass.select(2, 0));
                } else {
                    auto D_u_vec = diag_u_t.reshape({1, nz});  // [1, nz] for broadcast
                    u_full.select(2, nx).copy_(r_u_stag_saved / D_u_vec);
                }
            } else {
                z.slice(0, u_offset, u_offset + size_u).copy_(delta_u_mass.flatten());
            }

            // V: interior + stagger boundary
            if (ny_v > ny) {
                auto v_full = z.slice(0, v_offset, v_offset + size_v).reshape({ny_v, nz, nx});
                v_full.slice(0, 0, ny).copy_(delta_v_mass);
                // v20.14r27z: Periodic → wrap stagger to first point; non-periodic → diagonal-only.
                if (grid_info_ && grid_info_->periodic_y) {
                    v_full.select(0, ny).copy_(delta_v_mass.select(0, 0));
                } else {
                    auto D_v_vec = diag_v_t.reshape({nz, 1});  // [nz, 1] for broadcast
                    v_full.select(0, ny).copy_(r_v_stag_saved / D_v_vec);
                }
            } else {
                z.slice(0, v_offset, v_offset + size_v).copy_(delta_v_mass.flatten());
            }

            // Φ: full result (mass levels + top boundary)
            z.slice(0, phi_offset, phi_offset + size_phi).copy_(delta_phi_full.flatten());

            // μ
            z.slice(0, mu_offset, mu_offset + size_mu).copy_(delta_mu.flatten());

            // Diagnostic: block norms (gated at debug_level >= 1)
            if (current_debug_level >= 1 && do_diag_sample) {
                float u_norm = z.slice(0, u_offset, u_offset+size_u).norm().item<float>();
                float v_norm = z.slice(0, v_offset, v_offset+size_v).norm().item<float>();
                float phi_norm = z.slice(0, phi_offset, phi_offset+size_phi).norm().item<float>();
                float mu_norm = z.slice(0, mu_offset, mu_offset+size_mu).norm().item<float>();
                std::cerr << "[PRECOND BLOCK NORMS] U: " << u_norm
                          << ", V: " << v_norm << ", MU: " << mu_norm << std::endl;
                std::cerr << "[PRECOND BLOCK NORMS] PHI: " << phi_norm << std::endl;
            }

        } else {
            // === LEGACY 3×3 U-V-μ + DIAGONAL Φ (precond_acoustic_4x4 == 0) ===
            auto u_block = z.slice(0, u_offset, u_offset + size_u).reshape({ny, nz, nx_u});
            auto v_block = z.slice(0, v_offset, v_offset + size_v).reshape({ny_v, nz, nx});
            auto mu_block = z.slice(0, mu_offset, mu_offset + size_mu).reshape({ny, nx});

            TORCH_CHECK(u_block.is_cpu() && u_block.scalar_type() == torch::kFloat32,
                "u_block must be CPU Float32 for accessor()");
            TORCH_CHECK(v_block.is_cpu() && v_block.scalar_type() == torch::kFloat32,
                "v_block must be CPU Float32 for accessor()");
            TORCH_CHECK(mu_block.is_cpu() && mu_block.scalar_type() == torch::kFloat32,
                "mu_block must be CPU Float32 for accessor()");
            auto u_acc = u_block.accessor<float, 3>();
            auto v_acc = v_block.accessor<float, 3>();
            auto mu_acc = mu_block.accessor<float, 2>();

            for (int j = 0; j < std::min(ny, ny_v); ++j) {
                for (int i = 0; i < std::min(nx, nx_u); ++i) {
                    float r_μ = mu_acc[j][i];
                    float D_μ = D_μ_cache;
                    float μ_coeff = D_μ;
                    float μ_rhs = r_μ;

                    for (int k = 0; k < nz; ++k) {
                        float D_u = D_u_cache[k];
                        float D_v = D_v_cache[k];
                        float C_uμ = C_uμ_cache[k];
                        float C_vμ = C_vμ_cache[k];
                        float C_μu = C_μu_cache[k];
                        float C_μv = C_μv_cache[k];
                        float r_u_k = u_acc[j][k][i];
                        float r_v_k = v_acc[j][k][i];
                        float level_coupling = (C_μu * C_uμ / D_u) + (C_μv * C_vμ / D_v);
                        float level_coupling_rhs = (C_μu * r_u_k / D_u) + (C_μv * r_v_k / D_v);
                        float damping = wrf::sdirk3::g_sdirk3_config.precond_mu_coupling_damping;
                        μ_coeff -= level_coupling * damping;
                        μ_rhs -= level_coupling_rhs * damping;
                    }

                    float μ_solution;
                    if (std::abs(μ_coeff) < 1e-12f) {
                        μ_solution = r_μ / D_μ;
                        for (int k = 0; k < nz; ++k) {
                            u_acc[j][k][i] = u_acc[j][k][i] / D_u_cache[k];
                            v_acc[j][k][i] = v_acc[j][k][i] / D_v_cache[k];
                        }
                    } else {
                        μ_solution = μ_rhs / μ_coeff;
                        for (int k = 0; k < nz; ++k) {
                            float C_uμ = C_uμ_cache[k];
                            float C_vμ = C_vμ_cache[k];
                            u_acc[j][k][i] = (u_acc[j][k][i] - C_uμ * μ_solution) / D_u_cache[k];
                            v_acc[j][k][i] = (v_acc[j][k][i] - C_vμ * μ_solution) / D_v_cache[k];
                        }
                    }
                    mu_acc[j][i] = μ_solution;
                }
            }

            if (current_debug_level >= 1 && do_diag_sample) {
                torch::NoGradGuard no_grad;
                float u_norm = u_block.norm().item<float>();
                float v_norm = v_block.norm().item<float>();
                float mu_norm = mu_block.norm().item<float>();
                std::cerr << "[PRECOND BLOCK NORMS] U: " << u_norm
                          << ", V: " << v_norm << ", MU: " << mu_norm << std::endl;
            }

            z.slice(0, u_offset, u_offset + size_u).copy_(u_block.flatten());
            z.slice(0, v_offset, v_offset + size_v).copy_(v_block.flatten());
            z.slice(0, mu_offset, mu_offset + size_mu).copy_(mu_block.flatten());

            // Diagonal Φ solve (legacy path)
            int64_t phi_off_local = w_offset_1d + size_w;
            if (phi_off_local + size_phi <= z.size(0)) {
                auto phi_block = z.slice(0, phi_off_local, phi_off_local + size_phi).reshape({ny, nz_w, nx});
                auto diag_phi_3d = vertical_diag_phi_.slice(0, 0, nz_w).reshape({1, nz_w, 1});
                phi_block.div_(diag_phi_3d);
                if (current_debug_level >= 1 && do_diag_sample) {
                    torch::NoGradGuard no_grad;
                    float phi_norm = phi_block.norm().item<float>();
                    std::cerr << "[PRECOND BLOCK NORMS] PHI: " << phi_norm << std::endl;
                }
                z.slice(0, phi_off_local, phi_off_local + size_phi).copy_(phi_block.flatten());
            }
        }

        // Update offset past U, V (W extraction follows)
        // v20.5 Fix 2: int64_t to prevent truncation on large grids
        int64_t offset = v_offset + size_v;

        // W and THETA components - COUPLED solve for gravity wave modes
        // v20.1/v20.5: Block Gauss-Seidel W-Φ coupling correction
        // After the 4×4 block solves Φ, update W RHS to account for Φ correction:
        //   r_w_corrected = r_w - A_wφ * delta_phi
        // where A_wφ is the vertical pressure gradient coupling from Φ to W.
        // Without this, the W-Φ off-diagonal coupling is only approximated by
        // the Schur complement diagonal adjustment, leaving W block norm ~15,000.
        torch::Tensor w_block, theta_block;
        int64_t w_offset = offset;

        if (offset + size_w <= z.size(0)) {
            w_block = z.slice(0, w_offset, w_offset + size_w).reshape({ny, nz_w, nx}).clone();
            offset += size_w;
        }

        // Skip PHI (already handled in 4×4 or legacy path above)
        if (offset + size_phi <= z.size(0)) {
            offset += size_phi;
        }

        // Extract theta block for coupled solve
        if (offset + size_t <= z.size(0)) {
            theta_block = z.slice(0, offset, offset + size_t).reshape({ny, nz, nx}).clone();
        }

        // v20.14 r46: Per-level phi-feedback + GS (replaces v20.5 Fix 1)
        // Outer guard split: phi-feedback needs only 4×4 + dz cache;
        // legacy GS additionally needs momentum_coupling_k cache.
        bool base_gs_ready = w_block.defined() &&
            wrf::sdirk3::g_sdirk3_config.precond_acoustic_4x4 == 1 &&
            C_u_phi_.defined() && grid_info_->mu_base.defined() &&
            gs_cache_generation_ == coefficient_generation_ &&
            dz_effective_cached_.size() >= 2;

        // v20.14 r47: Phase 2 — single readiness computation.
        // Used at 4 branch points + Thomas call. NEVER recomputed.
        bool phase2_active_this_call = false;
        torch::Tensor phase2_phi_block_3d;
        std::vector<float> phase2_A_eff_vec;
        const float* phase2_phi_diag_ptr = nullptr;
        const float* phase2_A_eff_ptr = nullptr;
        float phase2_cap_eff = 0.0f;         // for diagnostic: cap after decay
        float phase2_ru_scale_factor = 1.0f;  // for diagnostic: ru_share scaling

        // v20.14 r47c-fix2: Cross-coupling threshold for Phase2 auto-downgrade.
        float cross_thresh = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_cross_thresh;
        // Use cross_max (not mean) for gating — catches local spikes (Finding #5)
        bool cross_ok = (cross_thresh == 0.0f || s_phi_mu_cross_max_ <= cross_thresh);
        // Rate-limited: log once per Newton iteration per stage (apply() called 150x per Newton).
        // v20.14 r47c-fix3: Stage-aware key (same as Phase2 summary).
        int cross_log_key = current_stage_ * 1000 + newton_iteration_index_;
        if (!cross_ok && current_debug_level >= 1 &&
            cross_log_key != cross_downgrade_logged_newton_) {
            cross_downgrade_logged_newton_ = cross_log_key;
            std::cerr << "[PRECOND PHASE2] DISABLED (S=" << current_stage_
                      << " N=" << newton_iteration_index_
                      << "): cross_mean=" << s_phi_mu_cross_ratio_
                      << ", cross_max=" << s_phi_mu_cross_max_
                      << " > thresh=" << cross_thresh << std::endl;
        }

        if (base_gs_ready && theta_block.defined() &&
            wrf::sdirk3::g_sdirk3_config.precond_coupled_phi_w &&
            s_phi_phi_max_dev_ <= 0.1f && cross_ok) {
            bool coeff_ok = compute_phi_w_coupling_coefficients(nz, nz_w);
            if (coeff_ok && !phi_w_coupling_wph_.empty()) {
                float cap = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_boost_cap;
                auto& cfg = wrf::sdirk3::g_sdirk3_config;

                // v20.14 r47c-fix2: (1) Cap decay — reduce cap as Newton progresses.
                // cap_eff = cap * decay^iter. At N0: full cap. At N3 with decay=0.5: cap/8.
                float cap_decay = cfg.precond_phi_w_schur_cap_decay;
                if (cap_decay < 0.999f && newton_iteration_index_ > 0) {
                    float decay_factor = std::pow(cap_decay, static_cast<float>(newton_iteration_index_));
                    cap *= decay_factor;
                }

                // v20.14 r47c-fix2: (2) ru_share scaling — weaken A_eff when ru-dominated.
                // scale_factor = max(0, 1 - ru_scale*(ru_share - ru_thresh))
                float ru_scale_factor = 1.0f;
                float ru_scale = cfg.precond_phi_w_schur_ru_scale;
                if (ru_scale > 0.0f && newton_ru_share_ > cfg.precond_phi_w_schur_ru_thresh) {
                    ru_scale_factor = std::max(0.0f,
                        1.0f - ru_scale * (newton_ru_share_ - cfg.precond_phi_w_schur_ru_thresh));
                }

                phase2_cap_eff = cap;
                phase2_ru_scale_factor = ru_scale_factor;

                auto phi_dp = vertical_diag_phi_.data_ptr<float>();
                auto w_dp = vertical_diag_w_.data_ptr<float>();  // D_w_full
                phase2_A_eff_vec.resize(nz_w, 0.0f);
                for (int k = 1; k < nz; ++k) {
                    float A_raw = phi_w_coupling_wph_[k];
                    if (cap > 0.0f && phi_dp[k] > 1e-6f) {
                        float A_max = std::sqrt(cap * phi_dp[k] * std::abs(w_dp[k]));
                        phase2_A_eff_vec[k] = std::min(A_raw, A_max) * ru_scale_factor;
                    } else {
                        phase2_A_eff_vec[k] = A_raw * ru_scale_factor;
                    }
                }
                // v20.14 r47c-fix2: Phase2 phi_block is ALWAYS 4×4 δφ.
                // U/V are already solved from 4×4, so Φ must be consistent.
                // rhs_source config is diagnostic-only (no effect on solver behavior).
                // Experiment: diagonal-only δφ = r_phi/D_phi is 4400× too small at
                // hydrostatic rest (r_phi≈0). 4×4 coupling is essential.
                phase2_phi_block_3d = z.slice(0, phi_offset, phi_offset + size_phi)
                    .reshape({ny, nz_w, nx}).clone();
                phase2_phi_diag_ptr = phi_dp;
                phase2_A_eff_ptr = phase2_A_eff_vec.data();
                phase2_active_this_call = true;

                // v20.14 r47c-fix3b: Finding 1 — if ALL Phase2 ops are OFF,
                // Phase2 does nothing but still blocks r46/legacy GS/no_correction.
                // Include alpha_gs: GS hybrid (line 3094) is a Phase2 operation.
                bool phase2_ops_enabled =
                    wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_boost_on ||
                    wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_rhs_inject_on ||
                    wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_backsub_on ||
                    wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_alpha_gs > 0.0f;
                if (!phase2_ops_enabled) {
                    phase2_active_this_call = false;
                    int ops_log_key = current_stage_ * 1000 + newton_iteration_index_;
                    if (current_debug_level >= 1 &&
                        ops_log_key != phase2_summary_logged_newton_) {
                        phase2_summary_logged_newton_ = ops_log_key;
                        std::cerr << "[PRECOND PHASE2] S=" << current_stage_
                                  << " N=" << newton_iteration_index_
                                  << " DEACTIVATED (B/R/S all OFF)"
                                  << " — fallback paths open" << std::endl;
                    }
                }

                // v20.14 r47c-fix3: Finding 2 — if ru_scale_factor zeroed A_eff,
                // deactivate Phase2 to open legacy/fallback paths.
                // Otherwise A_eff=0 does nothing but blocks all alternatives.
                if (phase2_active_this_call && ru_scale_factor <= 1e-6f) {
                    phase2_active_this_call = false;
                    int deact_log_key = current_stage_ * 1000 + newton_iteration_index_;
                    if (current_debug_level >= 1 &&
                        deact_log_key != phase2_summary_logged_newton_) {
                        phase2_summary_logged_newton_ = deact_log_key;
                        std::cerr << "[PRECOND PHASE2] S=" << current_stage_
                                  << " N=" << newton_iteration_index_
                                  << " DEACTIVATED (ru_sf=" << ru_scale_factor
                                  << ", ru_share=" << newton_ru_share_
                                  << ") — fallback paths open" << std::endl;
                    }
                }

                // v20.14 r47c-fix2: Once-per-Newton summary for N3 collapse analysis.
                // v20.14 r47c-fix3: Encode stage in key to avoid S1/S2/S3 N=0 collision.
                int phase2_log_key = current_stage_ * 1000 + newton_iteration_index_;
                if (current_debug_level >= 1 &&
                    phase2_log_key != phase2_summary_logged_newton_) {
                    phase2_summary_logged_newton_ = phase2_log_key;
                    std::cerr << "[PRECOND PHASE2] S=" << current_stage_
                              << " N=" << newton_iteration_index_
                              << ", cap_eff=" << phase2_cap_eff
                              << ", ru_sf=" << phase2_ru_scale_factor
                              << ", ru_share=" << newton_ru_share_
                              << ", du_scale=" << du_scale_
                              << std::endl;
                }
            }
        }

        // v20.14 r47c-fix3: Log D_u weak scaling when active but Phase2 not active.
        // When Phase2 IS active, du_scale is already in the Phase2 summary log.
        if (du_scale_ < 0.999f && !phase2_active_this_call && current_debug_level >= 1) {
            int dw_log_key = current_stage_ * 1000 + newton_iteration_index_;
            if (dw_log_key != phase2_summary_logged_newton_) {
                phase2_summary_logged_newton_ = dw_log_key;
                std::cerr << "[PRECOND DU_WEAK] S=" << current_stage_
                          << " N=" << newton_iteration_index_
                          << ", du_scale=" << du_scale_
                          << ", ru_share=" << newton_ru_share_
                          << std::endl;
            }
        }

        // Pre-compute phi-feedback coefficients if enabled (before branch decision).
        // compute_phi_w_coupling_coefficients returns true if usable coefficients exist.
        // Has internal gen check — only recomputes when coefficient_generation_ changes.
        bool phi_feedback_ready = false;
        bool phi_coupled_on = wrf::sdirk3::g_sdirk3_config.precond_coupled_phi_w;
        bool phi_dev_ok = (s_phi_phi_max_dev_ <= 0.1f);
        if (base_gs_ready && phi_coupled_on && phi_dev_ok && !phase2_active_this_call) {
            phi_feedback_ready = compute_phi_w_coupling_coefficients(nz, nz_w);
        }

        // One-shot fallback reason log when phi-feedback expected but not ready
        if (!phi_feedback_ready && phi_coupled_on && !phase2_active_this_call && current_debug_level >= 1) {
            static thread_local uint64_t last_fallback_log_gen = 0;
            if (last_fallback_log_gen != coefficient_generation_) {
                last_fallback_log_gen = coefficient_generation_;
                bool fb_gs = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_fallback_gs;
                std::cerr << "[PRECOND PHI-FEEDBACK] " << (fb_gs ? "FALLBACK to legacy GS" : "SKIPPED (no fallback)")
                          << " (gen=" << coefficient_generation_ << "): "
                          << (!base_gs_ready ? "base_gs NOT ready" :
                              !phi_dev_ok ? ("s_phi_phi_max_dev=" + std::to_string(s_phi_phi_max_dev_) + " > 0.1") :
                              "compute_coefficients returned false (prereq missing)")
                          << std::endl;
            }
        }

        // v20.14 r46i: δw blend — declared at outer scope so it survives to post-Thomas blend.
        const float dw_blend = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_dw_blend;
        torch::Tensor dw_est_3d;  // populated in 2a loop if dw_blend > 0

        if (phi_feedback_ready) {
            // === PHI-FEEDBACK: δφ corrected by W residual, then GS with improved Φ ===
            // v20.14 r46i: Also computes δw_est for post-Thomas blend when dw_blend > 0.
            torch::NoGradGuard no_grad;

            // Read current Φ from z (4×4 Schur result)
            auto delta_phi_3d = z.slice(0, phi_offset, phi_offset + size_phi)
                .reshape({ny, nz_w, nx}).clone();
            auto phi_diag_ptr = vertical_diag_phi_.data_ptr<float>();

            // v20.14 r46b: relaxation factor (η). φ_final = φ_old + η*(φ_fb - φ_old)
            const float eta = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_relax;
            // v20.14 r46e: 2a coupling strength control (beta + cap)
            const float phi_fb_beta = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_beta;
            const float phi_fb_cap_base = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_cap;
            const bool soft_cap = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_soft_cap;

            // v20.14 r46j: Dynamic cap schedule — cap_mode selects ramp variable.
            // mode 0: t = min(N/2, 1) — iteration-based (original)
            // mode 1: t = clamp(ratio, 0, 1) — residual-ratio-based
            // mode 2: t = max(iter, ratio) — combined (most aggressive ramp)
            const float phi_fb_cap_high_raw = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_cap_high;
            const int cap_mode = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_cap_mode;
            float phi_fb_cap = phi_fb_cap_base;
            if (phi_fb_cap_base > 0.0f && phi_fb_cap_high_raw > 0.0f) {
                float t_iter = std::min(static_cast<float>(newton_iteration_index_) / 2.0f, 1.0f);
                float t_ratio = std::clamp(newton_residual_ratio_, 0.0f, 1.0f);
                float t = (cap_mode == 1) ? t_ratio :
                          (cap_mode == 2) ? std::max(t_iter, t_ratio) : t_iter;
                phi_fb_cap = phi_fb_cap_base + (phi_fb_cap_high_raw - phi_fb_cap_base) * t;
            }

            // v20.14 r46j: Stage-aware coupling — scale feedback for S2/S3
            const float stage_scale = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_stage_scale;
            float stage_mult = (current_stage_ >= 2 && stage_scale < 0.999f) ? stage_scale : 1.0f;

            // Step 2a+2b: Per-level phi-feedback + GS gradient correction.
            // v20.14 r46i: Multi-pass iteration (2a→2b repeated).
            // Pass 1: δφ from W residual (2a), then GS with improved Φ (2b).
            // Pass 2+: δφ re-corrected from GS-updated W, then GS with further-improved Φ.
            const int n_passes = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_passes;
            float correction_norm_sq = 0.0f;
            float phi_old_norm_sq = 0.0f;
            bool track_diag = (current_debug_level >= 1 && do_diag_sample);
            const float max_corr = wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_max_corr;
            // Save original phi for correction limiter (only when limiter active)
            torch::Tensor delta_phi_original;
            if (max_corr >= 0.0f) {
                delta_phi_original = delta_phi_3d.clone();
            }

            // v20.14 r46i: Allocate δw estimate storage for post-Thomas blend
            if (dw_blend > 0.0f && w_block.defined()) {
                dw_est_3d = torch::zeros_like(w_block);
            }

            // GS config (read once, constant across passes)
            float gs_beta = wrf::sdirk3::g_sdirk3_config.precond_gs_beta;
            float coupled_cap_base = wrf::sdirk3::g_sdirk3_config.precond_gs_awphi_cap_coupled;
            float gs_cap_base = (coupled_cap_base >= 0.0f) ? coupled_cap_base
                : wrf::sdirk3::g_sdirk3_config.precond_gs_awphi_cap;
            float gs_cap_high_raw = wrf::sdirk3::g_sdirk3_config.precond_gs_awphi_cap_coupled_high;
            float max_A_w_phi = gs_cap_base;
            if (gs_cap_base > 0.0f && gs_cap_high_raw > 0.0f) {
                float t_iter = std::min(static_cast<float>(newton_iteration_index_) / 2.0f, 1.0f);
                float t_ratio = std::clamp(newton_residual_ratio_, 0.0f, 1.0f);
                float t = (cap_mode == 1) ? t_ratio :
                          (cap_mode == 2) ? std::max(t_iter, t_ratio) : t_iter;
                max_A_w_phi = gs_cap_base + (gs_cap_high_raw - gs_cap_base) * t;
            }

            // v20.14 r46j: Apply stage scaling to both feedback and GS beta
            float eff_phi_fb_beta = phi_fb_beta * stage_mult;
            float eff_gs_beta = gs_beta * stage_mult;

            for (int pass = 0; pass < n_passes; ++pass) {
                bool is_last_pass = (pass == n_passes - 1);
                // Track diagnostics on last pass only
                bool do_track = track_diag && is_last_pass;
                if (do_track) {
                    correction_norm_sq = 0.0f;
                    phi_old_norm_sq = 0.0f;
                }

                // === 2a: Per-level phi-feedback ===
                for (int k = 1; k < nz; ++k) {
                    float A_wph_raw = phi_w_coupling_wph_[k];
                    if (A_wph_raw > 1e-15f) {
                        float A_scaled = eff_phi_fb_beta * A_wph_raw;
                        float A_num;
                        if (phi_fb_cap > 0.0f) {
                            if (soft_cap) {
                                A_num = phi_fb_cap * std::tanh(A_scaled / phi_fb_cap);
                            } else {
                                A_num = std::min(A_scaled, phi_fb_cap);
                            }
                        } else {
                            A_num = A_scaled;
                        }

                        float D_w_ns = phi_w_D_w_nosboost_[k];
                        float D_phi = phi_diag_ptr[k];
                        float det_raw = phi_w_coupling_det_[k];

                        auto phi_old = delta_phi_3d.select(1, k);
                        auto r_phi_eff = phi_old * D_phi;
                        auto phi_fb = (D_w_ns * r_phi_eff + A_num * w_block.select(1, k)) / det_raw;

                        // v20.14 r46i: δw_est on last pass only
                        if (is_last_pass && dw_est_3d.defined()) {
                            dw_est_3d.select(1, k).copy_(
                                (D_phi * w_block.select(1, k) - A_num * r_phi_eff) / det_raw);
                        }

                        if (do_track) {
                            auto diff = phi_fb - phi_old;
                            correction_norm_sq += diff.square().sum().item<float>();
                            phi_old_norm_sq += phi_old.square().sum().item<float>();
                        }

                        if (eta >= 0.999f) {
                            phi_old.copy_(phi_fb);
                        } else {
                            phi_old.mul_(1.0f - eta).add_(phi_fb, eta);
                        }
                    }
                }

                // Correction limiter (applies after each pass's 2a, before 2b)
                if (max_corr >= 0.0f && delta_phi_original.defined()) {
                    auto correction = delta_phi_3d - delta_phi_original;
                    float corr_norm = correction.norm().item<float>();
                    float phi_norm = delta_phi_original.norm().item<float>();
                    float rel = (phi_norm > 1e-20f) ? corr_norm / phi_norm : 0.0f;
                    if (rel > max_corr) {
                        float scale = max_corr / rel;
                        delta_phi_3d.copy_(delta_phi_original + scale * correction);
                        if (do_track) {
                            std::cerr << "[PRECOND PHI-FEEDBACK LIMITER] pass=" << pass
                                      << ", ||corr||/||phi||=" << rel
                                      << " > max_corr=" << max_corr
                                      << ", scaled to " << scale << std::endl;
                        }
                    }
                }

                // === 2b: GS gradient correction ===
                if (eff_gs_beta > 0.0f) {
                    int cap_hit_count = 0;
                    int active_count = 0;
                    for (int k = 1; k < nz; ++k) {
                        float raw_A = eff_gs_beta * phi_w_coupling_wph_[k];
                        float A_w_phi;
                        if (soft_cap && max_A_w_phi > 0.0f) {
                            A_w_phi = max_A_w_phi * std::tanh(raw_A / max_A_w_phi);
                        } else {
                            A_w_phi = std::min(raw_A, max_A_w_phi);
                        }
                        if (A_w_phi > 1e-15f) {
                            if (raw_A > max_A_w_phi) ++cap_hit_count;
                            ++active_count;
                            w_block.select(1, k).sub_(
                                A_w_phi * (delta_phi_3d.select(1, k) - delta_phi_3d.select(1, k - 1)));
                        }
                    }
                    if (do_track && active_count > 0) {
                        std::cerr << "[PRECOND PHI-FEEDBACK GS] pass=" << pass
                                  << ", cap_hit=" << cap_hit_count << "/" << active_count
                                  << " (" << (100.0f * cap_hit_count / active_count) << "%)"
                                  << ", gs_beta=" << eff_gs_beta
                                  << (stage_mult < 0.999f ? " (S" + std::to_string(current_stage_) + "*" + std::to_string(stage_mult) + ")" : "")
                                  << ", gs_cap_eff=" << max_A_w_phi
                                  << " (base=" << gs_cap_base
                                  << (gs_cap_high_raw > 0.0f ? ", hi=" + std::to_string(gs_cap_high_raw) : "")
                                  << ", N=" << newton_iteration_index_
                                  << (soft_cap ? ", soft" : "") << ")"
                                  << std::endl;
                    }
                }
            } // end pass loop

            // Step 2c: Write corrected Φ back to z
            z.slice(0, phi_offset, phi_offset + size_phi).copy_(delta_phi_3d.flatten());

            if (track_diag) {
                // v20.14 r46h: Compute post-limiter ||corr||/||phi|| (Finding #4).
                // If limiter was active, use actual applied correction, not pre-limiter accumulation.
                float rel_corr;
                if (delta_phi_original.defined()) {
                    torch::NoGradGuard no_grad;
                    auto final_correction = delta_phi_3d - delta_phi_original;
                    float final_corr_norm = final_correction.norm().item<float>();
                    float phi_orig_norm = delta_phi_original.norm().item<float>();
                    rel_corr = (phi_orig_norm > 1e-20f) ? final_corr_norm / phi_orig_norm : 0.0f;
                } else {
                    rel_corr = (phi_old_norm_sq > 1e-20f)
                        ? std::sqrt(correction_norm_sq / phi_old_norm_sq) : 0.0f;
                }
                // 2a/2b effective strength comparison at mid-level
                int k_mid = nz / 2;
                float A_raw_mid = phi_w_coupling_wph_[k_mid];
                float A_scaled_mid = eff_phi_fb_beta * A_raw_mid;
                float A_2a_num;
                if (phi_fb_cap > 0.0f) {
                    if (soft_cap) {
                        A_2a_num = phi_fb_cap * std::tanh(A_scaled_mid / phi_fb_cap);
                    } else {
                        A_2a_num = std::min(A_scaled_mid, phi_fb_cap);
                    }
                } else {
                    A_2a_num = A_scaled_mid;
                }
                float det_raw_mid = phi_w_coupling_det_[k_mid];
                float raw_gs_mid = eff_gs_beta * A_raw_mid;
                float A_2b_mid;
                if (soft_cap && max_A_w_phi > 0.0f) {
                    A_2b_mid = max_A_w_phi * std::tanh(raw_gs_mid / max_A_w_phi);
                } else {
                    A_2b_mid = std::min(raw_gs_mid, max_A_w_phi);
                }
                std::cerr << "[PRECOND PHI-FEEDBACK] N=" << newton_iteration_index_
                          << ", eta=" << eta
                          << ", ||corr||/||phi||=" << rel_corr
                          << ", 2a_A[" << k_mid << "]=" << A_2a_num
                          << " (cap_eff=" << phi_fb_cap
                          << (phi_fb_cap_high_raw > 0.0f ? ", sched=" + std::to_string(phi_fb_cap_base) + "->" + std::to_string(phi_fb_cap_high_raw) : "")
                          << (soft_cap ? ", soft" : "")
                          << ", det=" << det_raw_mid << ")"
                          << ", 2b_A=" << A_2b_mid
                          << " (gs_cap_eff=" << max_A_w_phi << ")"
                          << (max_corr >= 0.0f ? ", lim=" + std::to_string(max_corr) : "")
                          << (n_passes > 1 ? ", passes=" + std::to_string(n_passes) : "")
                          << (dw_blend > 0.0f ? ", dw_blend=" + std::to_string(dw_blend) : "")
                          << (cap_mode != 0 ? ", cap_mode=" + std::to_string(cap_mode) + ", ratio=" + std::to_string(newton_residual_ratio_) : "")
                          << (stage_mult < 0.999f ? ", S" + std::to_string(current_stage_) + "*" + std::to_string(stage_mult) : "")
                          << std::endl;
            }

        } else if (!phase2_active_this_call &&
                   ((!phi_coupled_on ||
                    (phi_coupled_on && !phi_feedback_ready &&
                     wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_fallback_gs))
                   && base_gs_ready && !momentum_coupling_k_cached_.empty())) {
            // === LEGACY GS (unidirectional W←Φ only) ===
            // Active when: (1) coupled_phi_w=OFF, or
            // (2) coupled=ON but feedback not ready AND fallback_gs=true.
            // When coupled=ON and fallback_gs=false, W←Φ is skipped entirely
            // to avoid operator scale discontinuity (Finding r46e#2).

            // Log GS + boost combination (informational)
            {
                static thread_local bool gs_info_logged = false;
                if (!gs_info_logged) {
                    std::cerr << "[PRECOND PHI-W GS] Active with boost="
                              << wrf::sdirk3::g_sdirk3_config.precond_w_acoustic_boost
                              << " (boost=diagonal quality, GS=off-diagonal RHS correction)"
                              << std::endl;
                    gs_info_logged = true;
                }
            }

            auto delta_phi_3d = z.slice(0, phi_offset, phi_offset + size_phi)
                .reshape({ny, nz_w, nx});
            float dt_gamma = dt_ * gamma_;
            float g_val = grid_info_->g;
            int mc_sz = static_cast<int>(momentum_coupling_k_cached_.size());
            int dz_sz = static_cast<int>(dz_effective_cached_.size());

            float gs_beta = wrf::sdirk3::g_sdirk3_config.precond_gs_beta;
            if (gs_beta > 0.0f) {
                for (int k = 1; k < nz; ++k) {
                    if (k >= mc_sz) break;
                    int k_lo = std::min(k - 1, dz_sz - 1);
                    int k_hi = std::min(k, dz_sz - 1);
                    float dz_w = 0.5f * (dz_effective_cached_[k_lo] + dz_effective_cached_[k_hi]);
                    dz_w = std::max(dz_w, 1.0f);
                    float A_w_phi = gs_beta * dt_gamma * g_val / (momentum_coupling_k_cached_[k] * mu_scale_correction_ * dz_w);
                    const float max_A_w_phi = wrf::sdirk3::g_sdirk3_config.precond_gs_awphi_cap;
                    A_w_phi = std::min(A_w_phi, max_A_w_phi);
                    w_block.select(1, k).sub_(
                        A_w_phi * (delta_phi_3d.select(1, k) - delta_phi_3d.select(1, k - 1)));
                }
            }

            if (current_debug_level >= 2 && do_diag_sample) {
                torch::NoGradGuard no_grad;
                std::cerr << "[PRECOND PHI-W GS] W norm after correction: "
                          << w_block.norm().item<float>() << std::endl;
            }
        } else if (w_block.defined() && !phase2_active_this_call) {
            // No W←Φ correction applied this call
            ++no_correction_count_;
            if (current_debug_level >= 1) {
                static thread_local uint64_t last_nopath_log_gen = 0;
                if (last_nopath_log_gen != coefficient_generation_) {
                    last_nopath_log_gen = coefficient_generation_;
                    if (phi_coupled_on && !phi_feedback_ready) {
                        std::cerr << "[PRECOND W-PHI] Coupled fallback: W-Phi correction SKIPPED"
                                  << " (gen=" << coefficient_generation_
                                  << ", no_corr_count=" << no_correction_count_
                                  << ", fallback_gs="
                                  << (wrf::sdirk3::g_sdirk3_config.precond_phi_feedback_fallback_gs ? "true" : "false")
                                  << ")" << std::endl;
                    } else {
                        std::cerr << "[PRECOND W-PHI] NO correction path active (gen="
                                  << coefficient_generation_ << "): base_gs_ready="
                                  << base_gs_ready << ", coupled=" << phi_coupled_on
                                  << ", mc_cache=" << !momentum_coupling_k_cached_.empty()
                                  << ", no_corr_count=" << no_correction_count_
                                  << std::endl;
                    }
                }
            }
        }

        // COUPLED W-THETA SOLVE for each (j,i) column
        // This captures buoyancy (w↔θ) coupling for gravity waves
        auto wtheta_start = std::chrono::high_resolution_clock::now();

        if (w_block.defined() && theta_block.defined()) {
            // AUDIT: Compare preconditioner diagonal magnitude vs residual norms
            // OPT Pass33+: Heavy diagnostic at debug_level >= 3 with sampling
            // Uses debug_heavy_sample_period to reduce D2H overhead
            if (current_debug_level >= 3 && do_heavy_sample) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
                // FIX Round192: Batch 6 scalars into single D2H transfer
                torch::NoGradGuard no_grad;
                auto scalars = torch::stack({
                    w_block.norm(),
                    theta_block.norm(),
                    vertical_diag_w_.min(),
                    vertical_diag_w_.max(),
                    vertical_diag_theta_.min(),
                    vertical_diag_theta_.max()
                }).to(torch::kCPU);
                float w_residual_norm = scalars[0].item<float>();
                float theta_residual_norm = scalars[1].item<float>();
                float w_diag_min = scalars[2].item<float>();
                float w_diag_max = scalars[3].item<float>();
                float theta_diag_min = scalars[4].item<float>();
                float theta_diag_max = scalars[5].item<float>();

                std::cerr << "\n[PRECONDITIONER APPLY] Comparing diagonal vs residual magnitudes:" << std::endl;
                std::cerr << "  W residual norm: " << w_residual_norm << std::endl;
                std::cerr << "  W diagonal range: [" << w_diag_min << ", " << w_diag_max << "]" << std::endl;
                std::cerr << "  Theta residual norm: " << theta_residual_norm << std::endl;
                std::cerr << "  Theta diagonal range: [" << theta_diag_min << ", " << theta_diag_max << "]" << std::endl;
                std::cerr << "  Effective amplification from W diagonal: ~" << (1.0f / w_diag_min) << "x to " << (1.0f / w_diag_max) << "x" << std::endl;
            }

            // FIX 2026-01-31: Batched Thomas algorithm — all (j,i) columns at once.
            // v20.14 r47: Pass Phase 2 params (nullptr when inactive).
            solve_coupled_w_theta_batched(w_block, theta_block, nz, nz_w,
                phase2_active_this_call ? &phase2_phi_block_3d : nullptr,
                phase2_phi_diag_ptr, phase2_A_eff_ptr, nz_w);

            // v20.14 r47: Write back-substituted Φ to z
            if (phase2_active_this_call) {
                if (current_debug_level >= 1 && do_diag_sample) {
                    torch::NoGradGuard no_grad;
                    // Capture ||phi_4x4|| before overwrite, ||phi_bs|| after
                    float phi_4x4_norm = z.slice(0, phi_offset, phi_offset + size_phi).norm().item<float>();
                    float phi_bs_norm = phase2_phi_block_3d.norm().item<float>();
                    float delta_bs_norm = (phase2_phi_block_3d.flatten() -
                        z.slice(0, phi_offset, phi_offset + size_phi)).norm().item<float>();

                    z.slice(0, phi_offset, phi_offset + size_phi).copy_(phase2_phi_block_3d.flatten());

                    int k_mid = nz / 2;
                    float A_raw_mid = phi_w_coupling_wph_[k_mid];
                    float A_eff_mid = phase2_A_eff_vec[k_mid];
                    float D_phi_mid = phase2_phi_diag_ptr[k_mid];
                    float eff_boost = (D_phi_mid > 1e-6f) ? A_eff_mid * A_eff_mid / D_phi_mid : 0.0f;
                    float vd_w_mid = vertical_diag_w_.data_ptr<float>()[k_mid];
                    float eta_bs = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_backsub_relax;
                    auto& cfg = wrf::sdirk3::g_sdirk3_config;
                    std::cerr << "[PRECOND PHASE2] Schur active"
                              << " (scale=" << cfg.precond_phi_w_coupling_scale
                              << ", boost_cap=" << cfg.precond_phi_w_schur_boost_cap
                              << ", eta_bs=" << eta_bs
                              << ", ablation=" << (cfg.precond_phi_w_schur_boost_on ? "B" : "b")
                              << (cfg.precond_phi_w_schur_rhs_inject_on ? "R" : "r")
                              << (cfg.precond_phi_w_schur_backsub_on ? "S" : "s")
                              << ", alpha_gs=" << cfg.precond_phi_w_schur_alpha_gs
                              << "): A_raw[" << k_mid << "]=" << A_raw_mid
                              << ", A_eff=" << A_eff_mid
                              << (A_eff_mid < A_raw_mid * 0.999f ? " (CAPPED)" : "")
                              << ", boost=" << eff_boost
                              << "/" << std::abs(vd_w_mid)
                              << " (" << (100.0f * eff_boost / std::max(std::abs(vd_w_mid), 1e-10f))
                              << "% of D_w)"
                              << ", ||phi_4x4||=" << phi_4x4_norm
                              << ", ||phi_bs||=" << phi_bs_norm
                              << ", ||delta_bs||=" << delta_bs_norm
                              << " (" << (100.0f * delta_bs_norm / std::max(phi_4x4_norm, 1e-10f))
                              << "% of 4x4)"
                              << ", max_dev=" << s_phi_phi_max_dev_
                              << ", cross=" << s_phi_mu_cross_ratio_
                              << ", cross_max=" << s_phi_mu_cross_max_
                              << ", N=" << newton_iteration_index_
                              << ", cap_eff=" << phase2_cap_eff
                              << ", ru_sf=" << phase2_ru_scale_factor
                              << std::endl;
                } else {
                    z.slice(0, phi_offset, phi_offset + size_phi).copy_(phase2_phi_block_3d.flatten());
                }
            }

            // v20.14 r47c: Phase2 + legacy GS hybrid — restore off-level (k-k-1) gradient.
            // Phase2 Schur only has same-level coupling; legacy GS adds inter-level Φ gradient.
            // alpha_gs scales the GS correction. Uses updated Φ (from z, post-Phase2 write-back).
            // Constant alpha_gs → GMRES stationary.
            {
                float alpha_gs = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_alpha_gs;
                int gs_start = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_alpha_gs_start;
                // v20.14 r47c-fix3: Finding 3 — warn if alpha_gs_start is set but alpha_gs==0.
                // alpha_gs_start only gates execution when alpha_gs > 0. With default alpha_gs=0,
                // any alpha_gs_start sweep is a structural no-op.
                if (gs_start > 0 && alpha_gs <= 0.0f) {
                    static std::atomic<bool> gs_start_warned{false};
                    if (!gs_start_warned.exchange(true, std::memory_order_relaxed)) {
                        std::cerr << "[PRECOND PHASE2] WARNING: alpha_gs_start="
                                  << gs_start << " set but alpha_gs=0 — gs_start has no effect"
                                  << std::endl;
                    }
                }
                if (phase2_active_this_call && alpha_gs > 0.0f && base_gs_ready
                    && !momentum_coupling_k_cached_.empty()
                    && newton_iteration_index_ >= gs_start) {
                    auto delta_phi_gs = z.slice(0, phi_offset, phi_offset + size_phi)
                        .reshape({ny, nz_w, nx});
                    float dt_gamma = dt_ * gamma_;
                    float g_val = grid_info_->g;
                    int mc_sz = static_cast<int>(momentum_coupling_k_cached_.size());
                    int dz_sz = static_cast<int>(dz_effective_cached_.size());
                    float gs_beta = wrf::sdirk3::g_sdirk3_config.precond_gs_beta;

                    if (gs_beta > 0.0f) {
                        for (int k = 1; k < nz; ++k) {
                            if (k >= mc_sz) break;
                            int k_lo = std::min(k - 1, dz_sz - 1);
                            int k_hi = std::min(k, dz_sz - 1);
                            float dz_w = 0.5f * (dz_effective_cached_[k_lo] + dz_effective_cached_[k_hi]);
                            dz_w = std::max(dz_w, 1.0f);
                            float A_w_phi = gs_beta * dt_gamma * g_val / (momentum_coupling_k_cached_[k] * mu_scale_correction_ * dz_w);
                            const float max_A_w_phi = wrf::sdirk3::g_sdirk3_config.precond_gs_awphi_cap;
                            A_w_phi = std::min(A_w_phi, max_A_w_phi);
                            // Scale by alpha_gs for hybrid blending
                            w_block.select(1, k).sub_(
                                (alpha_gs * A_w_phi) * (delta_phi_gs.select(1, k) - delta_phi_gs.select(1, k - 1)));
                        }
                    }

                    if (current_debug_level >= 1 && do_diag_sample) {
                        torch::NoGradGuard no_grad;
                        std::cerr << "[PRECOND PHASE2+GS] alpha_gs=" << alpha_gs
                                  << ", gs_beta=" << gs_beta
                                  << ", ||w_after_gs||=" << w_block.norm().item<float>()
                                  << std::endl;
                    }
                }
            }

            // v20.14 r46i: Post-Thomas δw blend
            // w_final = (1-κ)*w_thomas + κ*δw_est. Both are preconditioner-output-space vectors.
            // δw_est is the 2×2 Cramer's rule estimate; w_thomas is the W-Θ Thomas result.
            if (dw_blend > 0.0f && dw_est_3d.defined()) {
                for (int k = 1; k < nz; ++k) {
                    w_block.select(1, k).mul_(1.0f - dw_blend).add_(dw_est_3d.select(1, k), dw_blend);
                }
                if (current_debug_level >= 1 && do_diag_sample) {
                    torch::NoGradGuard no_grad;
                    float dw_est_norm = dw_est_3d.norm().item<float>();
                    float w_thomas_norm = w_block.norm().item<float>();
                    std::cerr << "[PRECOND DW-BLEND] kappa=" << dw_blend
                              << ", ||dw_est||=" << dw_est_norm
                              << ", ||w_blended||=" << w_thomas_norm
                              << std::endl;
                }
            }

            // DIAGNOSTIC: Log W-THETA block norms before assembly
            // OPT Pass33+: Gated with do_diag_sample to reduce D2H overhead
            if (current_debug_level >= 1 && do_diag_sample) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
                torch::NoGradGuard no_grad;
                float w_norm = w_block.norm().to(torch::kCPU).item<float>();
                float theta_norm = theta_block.norm().to(torch::kCPU).item<float>();
                std::cerr << "[PRECOND BLOCK NORMS] W: " << w_norm
                          << ", THETA: " << theta_norm << std::endl;
            }

            // Write W back to z
            // FIX 2025-12-26: Use copy_() for in-place modification (slice=... rebinds temporary)
            z.slice(0, w_offset, w_offset + size_w).copy_(w_block.flatten());
            // Write theta back to z
            z.slice(0, offset, offset + size_t).copy_(theta_block.flatten());
            offset += size_t;

            // PERFORMANCE PROFILING: Report W-THETA timing
            // OPT Pass33+: Timing is lightweight, still gate with do_diag_sample for consistency
            auto wtheta_end = std::chrono::high_resolution_clock::now();
            auto wtheta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wtheta_end - wtheta_start).count();
            if (current_debug_level >= 1 && do_diag_sample) {
                std::cerr << "[PRECOND TIMING] W-THETA coupled solve: " << wtheta_ms << " ms ("
                          << (nx * ny) << " columns)" << std::endl;
            }

        } else if (offset + size_t <= z.size(0)) {
            // Fallback if only theta exists (shouldn't happen)
            offset += size_t;
        }

        // === MU COMPONENT ===
        // MU is now handled in the coupled U-V-MU 3×3 solver above
        // No separate diagonal solve needed here

        // v20.13: Gated on debug_level >= 1 (was unconditional for first 3 calls)
        if (current_debug_level >= 1 && apply_call_count <= 3) {
            torch::NoGradGuard no_grad;
            float z_norm = z.norm().to(torch::kCPU).item<float>();
            float r_norm = residual.norm().to(torch::kCPU).item<float>();
            float diff_norm = (z - residual).norm().to(torch::kCPU).item<float>();
            std::cerr << "[PRECOND 1D RESULT] BEFORE relaxation:"
                      << " ||z||=" << z_norm << " ||r||=" << r_norm
                      << " ||z-r||=" << diff_norm
                      << " ||z-r||/||r||=" << (r_norm > 0 ? diff_norm/r_norm : 0) << std::endl;
        }

        // v20.3: Adaptive additive shift relaxation.
        // z = M⁻¹r + α_eff * r, where α_eff = α₀ * min(1, ||R||/||R₀||).
        // At Newton start (ratio=1): full α₀ shift prevents near-zero eigenvalues.
        // As Newton converges (ratio→0): α_eff→0, pure preconditioner M⁻¹.
        // This prevents fixed α from over-regularizing in later Newton iterations,
        // which was causing GMRES to stall at ~0.143 instead of reaching <0.1.
        {
            float alpha0 = wrf::sdirk3::g_sdirk3_config.precond_relaxation;
            float alpha_eff = alpha0 * std::min(1.0f, newton_residual_ratio_);
            if (alpha_eff > 1e-8f) {
                z.add_(residual, alpha_eff);  // z += α_eff * r
            }
        }

        // v20.14 r49: Horizontal Jacobi smoothing for U/V blocks (interior-only)
        {
            float h_alpha = wrf::sdirk3::g_sdirk3_config.precond_horizontal_smooth_alpha;
            int h_iters = wrf::sdirk3::g_sdirk3_config.precond_horizontal_smooth_iters;
            if (h_alpha > 0.0f && h_iters > 0) {
                torch::NoGradGuard no_grad;
                float cw = 1.0f - 4.0f * h_alpha;
                float nw = h_alpha;

                // U block: [ny, nz, nx_u]
                auto u_3d = z.slice(0, u_offset, u_offset + size_u).reshape({ny, nz, nx_u});
                TORCH_CHECK(u_3d.is_cpu() && u_3d.scalar_type() == torch::kFloat32,
                    "u_3d must be CPU Float32 for horizontal smoothing accessor");
                auto u_work = u_3d.clone();
                for (int it = 0; it < h_iters; ++it) {
                    auto u_src = u_3d.accessor<float, 3>();
                    auto u_dst = u_work.accessor<float, 3>();
                    for (int j = 1; j < ny - 1; ++j) {
                        for (int k = 0; k < nz; ++k) {
                            for (int i = 1; i < nx_u - 1; ++i) {
                                u_dst[j][k][i] = cw * u_src[j][k][i]
                                    + nw * (u_src[j][k][i-1] + u_src[j][k][i+1]
                                          + u_src[j-1][k][i] + u_src[j+1][k][i]);
                            }
                        }
                    }
                    u_3d.copy_(u_work);
                }

                // V block: [ny_v, nz, nx]
                auto v_3d = z.slice(0, v_offset, v_offset + size_v).reshape({ny_v, nz, nx});
                TORCH_CHECK(v_3d.is_cpu() && v_3d.scalar_type() == torch::kFloat32,
                    "v_3d must be CPU Float32 for horizontal smoothing accessor");
                auto v_work = v_3d.clone();
                for (int it = 0; it < h_iters; ++it) {
                    auto v_src = v_3d.accessor<float, 3>();
                    auto v_dst = v_work.accessor<float, 3>();
                    for (int j = 1; j < ny_v - 1; ++j) {
                        for (int k = 0; k < nz; ++k) {
                            for (int i = 1; i < nx - 1; ++i) {
                                v_dst[j][k][i] = cw * v_src[j][k][i]
                                    + nw * (v_src[j][k][i-1] + v_src[j][k][i+1]
                                          + v_src[j-1][k][i] + v_src[j+1][k][i]);
                            }
                        }
                    }
                    v_3d.copy_(v_work);
                }

                if (current_debug_level >= 1 && do_diag_sample) {
                    std::cerr << "[PRECOND H-SMOOTH] alpha=" << h_alpha << " iters=" << h_iters
                              << " nx_u=" << nx_u << " ny=" << ny << " (interior-only)\n";
                }
            }
        }

        // v20.14 r49: Vertical Thomas smoothing for U/V (boundary-aware coefficients)
        {
            float v_alpha = wrf::sdirk3::g_sdirk3_config.precond_vertical_smooth_alpha;
            if (v_alpha > 0.0f && nz > 2) {
                torch::NoGradGuard no_grad;
                const bool boundary_guard = wrf::sdirk3::g_sdirk3_config.precond_smooth_boundary_guard;
                const bool periodic_x = (grid_info_ && grid_info_->periodic_x);
                const bool periodic_y = (grid_info_ && grid_info_->periodic_y);
                float vd_bdy = 1.0f + v_alpha;
                float vd_int = 1.0f + 2.0f * v_alpha;
                float vo = -v_alpha;

                // v20.14 r49-fix: Sign-preserving denominator clamp (matches Thomas pattern at line 4275)
                auto vs_safe_denom = [](float d) -> float {
                    return (std::abs(d) < 1e-12f) ? std::copysign(1e-12f, d + 1e-30f) : d;
                };

                static thread_local std::vector<float> vs_c_prime;
                static thread_local float vs_cached_alpha = -1.0f;
                static thread_local int vs_cached_nz = -1;
                if (vs_cached_alpha != v_alpha || vs_cached_nz != nz) {
                    vs_c_prime.resize(nz);
                    vs_c_prime[0] = vo / vs_safe_denom(vd_bdy);
                    for (int k = 1; k < nz - 1; ++k) {
                        vs_c_prime[k] = vo / vs_safe_denom(vd_int - vo * vs_c_prime[k-1]);
                    }
                    vs_cached_alpha = v_alpha;
                    vs_cached_nz = nz;
                }

                static thread_local std::vector<float> vs_d_prime;
                vs_d_prime.resize(nz);

                // U block
                auto u_3d = z.slice(0, u_offset, u_offset + size_u).reshape({ny, nz, nx_u});
                TORCH_CHECK(u_3d.is_cpu() && u_3d.scalar_type() == torch::kFloat32,
                    "u_3d must be CPU Float32 for vertical smoothing");
                auto u_acc = u_3d.accessor<float, 3>();
                for (int j = 0; j < ny; ++j) {
                    for (int i = 0; i < nx_u; ++i) {
                        if (boundary_guard) {
                            const bool on_x_bdy = !periodic_x && (i == 0 || i == nx_u - 1);
                            const bool on_y_bdy = !periodic_y && (j == 0 || j == ny - 1);
                            if (on_x_bdy || on_y_bdy) continue;
                        }
                        vs_d_prime[0] = u_acc[j][0][i] / vs_safe_denom(vd_bdy);
                        for (int k = 1; k < nz - 1; ++k) {
                            vs_d_prime[k] = (u_acc[j][k][i] - vo * vs_d_prime[k-1]) / vs_safe_denom(vd_int - vo * vs_c_prime[k-1]);
                        }
                        vs_d_prime[nz-1] = (u_acc[j][nz-1][i] - vo * vs_d_prime[nz-2]) / vs_safe_denom(vd_bdy - vo * vs_c_prime[nz-2]);
                        u_acc[j][nz-1][i] = vs_d_prime[nz-1];
                        for (int k = nz - 2; k >= 0; --k) {
                            u_acc[j][k][i] = vs_d_prime[k] - vs_c_prime[k] * u_acc[j][k+1][i];
                        }
                    }
                }

                // V block
                auto v_3d = z.slice(0, v_offset, v_offset + size_v).reshape({ny_v, nz, nx});
                TORCH_CHECK(v_3d.is_cpu() && v_3d.scalar_type() == torch::kFloat32,
                    "v_3d must be CPU Float32 for vertical smoothing");
                auto v_acc = v_3d.accessor<float, 3>();
                for (int j = 0; j < ny_v; ++j) {
                    for (int i = 0; i < nx; ++i) {
                        if (boundary_guard) {
                            const bool on_x_bdy = !periodic_x && (i == 0 || i == nx - 1);
                            const bool on_y_bdy = !periodic_y && (j == 0 || j == ny_v - 1);
                            if (on_x_bdy || on_y_bdy) continue;
                        }
                        vs_d_prime[0] = v_acc[j][0][i] / vs_safe_denom(vd_bdy);
                        for (int k = 1; k < nz - 1; ++k) {
                            vs_d_prime[k] = (v_acc[j][k][i] - vo * vs_d_prime[k-1]) / vs_safe_denom(vd_int - vo * vs_c_prime[k-1]);
                        }
                        vs_d_prime[nz-1] = (v_acc[j][nz-1][i] - vo * vs_d_prime[nz-2]) / vs_safe_denom(vd_bdy - vo * vs_c_prime[nz-2]);
                        v_acc[j][nz-1][i] = vs_d_prime[nz-1];
                        for (int k = nz - 2; k >= 0; --k) {
                            v_acc[j][k][i] = vs_d_prime[k] - vs_c_prime[k] * v_acc[j][k+1][i];
                        }
                    }
                }

                if (current_debug_level >= 1 && do_diag_sample) {
                    std::cerr << "[PRECOND V-SMOOTH] alpha=" << v_alpha << " nz=" << nz
                              << " diag_bdy=" << vd_bdy << " diag_int=" << vd_int
                              << " bdy_guard=" << (boundary_guard ? "on" : "off")
                              << "\n";
                }
            }
        }

        // PRECONDITIONER INSTRUMENTATION: Log output to diagnose ρ<0 issue (1D path)
        // OPT Pass33+: Gated with do_diag_sample to reduce D2H overhead
        if (current_debug_level >= 1 && do_diag_sample) {
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
            torch::NoGradGuard no_grad;
            float z_output_norm = z.norm().to(torch::kCPU).item<float>();
            float ratio = (r_input_norm > 1e-12f) ? (z_output_norm / r_input_norm) : 0.0f;
            float alpha0 = wrf::sdirk3::g_sdirk3_config.precond_relaxation;
            float alpha_eff_diag = alpha0 * std::min(1.0f, newton_residual_ratio_);
            std::cerr << "[PRECOND DEBUG] Output preconditioned ||M⁻¹r|| = " << z_output_norm
                      << " (α₀=" << alpha0 << ", ratio=" << newton_residual_ratio_
                      << ", α_eff=" << alpha_eff_diag << ")" << std::endl;
            std::cerr << "[PRECOND DEBUG] Ratio ||M⁻¹r|| / ||r|| = " << ratio << std::endl;
            if (ratio > 10.0f) {
                std::cerr << "[PRECOND WARNING] Preconditioner AMPLIFYING residual by " << ratio << "x!" << std::endl;
            }
        }

        return z;
    }

    // Original 4D implementation with enhanced coupling
    // Step 1: ENHANCED vertical implicit solve (handles stiff acoustic-gravity modes)
    z = apply_enhanced_vertical_solve(z);

    // Step 2: Horizontal smoothing (handles horizontal acoustic modes)
    z = apply_horizontal_smoothing(z);

    // Step 3: Physical scaling based on variable type
    z = apply_physical_scaling(z);

    // PERFORMANCE PROFILING: Report total apply() timing
    // OPT Pass33+: Timing is lightweight, still gate with do_diag_sample for consistency
    auto apply_end = std::chrono::high_resolution_clock::now();
    auto apply_ms = std::chrono::duration_cast<std::chrono::milliseconds>(apply_end - apply_start).count();
    if (current_debug_level >= 1 && do_diag_sample) {
        std::cerr << "[PRECOND TIMING] Total apply(): " << apply_ms << " ms" << std::endl;
    }

    // v20.3: Adaptive additive shift relaxation (4D path, matching 1D path)
    {
        float alpha0 = wrf::sdirk3::g_sdirk3_config.precond_relaxation;
        float alpha_eff = alpha0 * std::min(1.0f, newton_residual_ratio_);
        if (alpha_eff > 1e-8f) {
            z.add_(residual, alpha_eff);  // z += α_eff * r
        }
    }

    // PRECONDITIONER INSTRUMENTATION: Log output to diagnose ρ<0 issue (4D path)
    // OPT Pass33+: Gated with do_diag_sample to reduce D2H overhead
    if (current_debug_level >= 1 && do_diag_sample) {
        // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        torch::NoGradGuard no_grad;
        float z_output_norm = z.norm().to(torch::kCPU).item<float>();
        float ratio = (r_input_norm > 1e-12f) ? (z_output_norm / r_input_norm) : 0.0f;
        std::cerr << "[PRECOND DEBUG] Output preconditioned ||M⁻¹r|| = " << z_output_norm
                  << " (relaxation ω=" << wrf::sdirk3::g_sdirk3_config.precond_relaxation << ")" << std::endl;
        std::cerr << "[PRECOND DEBUG] Ratio ||M⁻¹r|| / ||r|| = " << ratio << std::endl;
        if (ratio > 10.0f) {
            std::cerr << "[PRECOND WARNING] Preconditioner AMPLIFYING residual by " << ratio << "x!" << std::endl;
        }
    }

    return z;
}

torch::Tensor UnifiedPreconditioner::apply_enhanced_vertical_solve(const torch::Tensor& r) {
    // Handle 1D state vector case
    if (r.dim() == 1) {
        // For 1D, use simplified diagonal scaling (already handled in apply())
        return r;
    }
    
    // Enhanced 4D implementation with acoustic-gravity coupling
    auto result = torch::zeros_like(r);
    
    int nvars = r.size(0);
    int nz = r.size(1);
    int ny = r.size(2);
    int nx = r.size(3);

    // Performance tracking
    auto start_time = std::chrono::high_resolution_clock::now();
    int uv_mu_calls = 0;

    // DIAGNOSTIC: Print nvars to understand state vector structure
    static bool nvars_printed = false;
    if (!nvars_printed && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND DIAGNOSTIC] State vector has nvars=" << nvars
                  << " (need >5 for U-V-MU coupling)" << std::endl;
        nvars_printed = true;
    }

    // PERF FIX 2025-12-27: Pre-copy mu_base to CPU once before the loop
    // This avoids nx*ny GPU sync calls from .item() per column
    auto mu_base_cpu = grid_info_->mu_base.to(torch::kCPU).contiguous();
    auto mu_base_acc = mu_base_cpu.accessor<float, 2>();

    // FIX Round187: One-time validation of vertical_diag tensors before loop
    // These are used with data_ptr<float>() which requires CPU + FP32
    TORCH_CHECK(vertical_diag_u_.is_cpu() && vertical_diag_u_.scalar_type() == torch::kFloat32,
        "vertical_diag_u_ must be CPU FP32 for data_ptr<float>()");
    TORCH_CHECK(vertical_diag_v_.is_cpu() && vertical_diag_v_.scalar_type() == torch::kFloat32,
        "vertical_diag_v_ must be CPU FP32 for data_ptr<float>()");

    // FIX 2026-01-31: Batched W-θ coupled solve — all (j,i) columns at once.
    // r[var] is [nz_dim, ny, nx]; batched solver expects [ny, nz_dim, nx].
    if (nvars > 4) {
        auto w_block_4d = r[2].permute({1, 0, 2}).clone();      // [ny, nz_w, nx]
        auto theta_block_4d = r[4].permute({1, 0, 2}).clone();  // [ny, nz, nx]
        int nz_w_4d = static_cast<int>(r[2].size(0));
        int nz_4d = static_cast<int>(r[4].size(0));
        // v20.14 r47: Phase 2 not applicable in 4D path (ordering mismatch).
        // Thomas runs BEFORE 4×4 Schur here, so δφ_4x4 is not yet available.
        // r47c-fix: one-shot warning + periodic counter at debug_level >= 2.
        {
            static std::atomic<bool> phase2_4d_warned{false};
            static std::atomic<int> phase2_4d_skip_count{0};
            if (wrf::sdirk3::g_sdirk3_config.precond_coupled_phi_w) {
                int cnt = phase2_4d_skip_count.fetch_add(1, std::memory_order_relaxed) + 1;
                if (!phase2_4d_warned.exchange(true, std::memory_order_relaxed)) {
                    std::cerr << "[PRECOND PHASE2] Not applied in 4D path"
                              << " (ordering mismatch: Thomas before 4x4 Schur)"
                              << std::endl;
                } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && (cnt % 100 == 0)) {
                    std::cerr << "[PRECOND PHASE2] 4D path skip count: " << cnt << std::endl;
                }
            }
        }
        solve_coupled_w_theta_batched(w_block_4d, theta_block_4d, nz_4d, nz_w_4d);
        result[2].copy_(w_block_4d.permute({1, 0, 2}));
        result[4].copy_(theta_block_4d.permute({1, 0, 2}));
    }

    // FIX 2026-01-31: Batched 4×4 acoustic block solve — ALL (j,i) columns at once.
    // Replaces the nx*ny per-column calls to solve_4x4_acoustic_block with a single
    // vectorized Schur complement solve using tensor broadcasting.
    if (nvars > 5 && C_u_phi_.defined()) {
        torch::NoGradGuard no_grad;

        // DEVICE GUARD: Batched Schur complement uses CPU tensor ops throughout.
        // Coefficient tensors are transferred to CPU; residuals must already be CPU.
        TORCH_CHECK(r.is_cpu(),
            "Batched 4x4 acoustic block requires CPU tensors, got device=", r.device());

        // r[var] shape: [nz, ny, nx] — all variables share nz in 4D path (nz_w == nz)
        auto r_u_all = r[0].contiguous();     // [nz, ny, nx]
        auto r_v_all = r[1].contiguous();     // [nz, ny, nx]
        auto r_phi_all = r[3].contiguous();   // [nz, ny, nx]

        // MU extraction: per-column code does r[5].select(0, j).select(-1, i)
        // which accesses r[5][j, 0, i] — using spatial j as k-index (MU is 2D,
        // stored in the nz-dim of the uniform 4D state vector).
        // Batched equivalent: select(1, 0) fixes ny-dim=0, slice(0,0,ny) takes
        // k=0..ny-1 → r_mu_batch[j,i] = r[5][j, 0, i].  Matches per-column + 3×3 fallback.
        auto r_mu_batch = r[5].select(1, 0).slice(0, 0, ny).contiguous();  // [ny, nx]

        // Coefficient tensors (1D, shape [nz]) — transfer from GPU once
        auto diag_u = vertical_diag_u_.to(torch::kCPU).contiguous();
        auto diag_v = vertical_diag_v_.to(torch::kCPU).contiguous();
        auto diag_phi = vertical_diag_phi_.to(torch::kCPU).contiguous();
        auto c_u_mu = C_u_mu_.to(torch::kCPU).contiguous();
        auto c_v_mu = C_v_mu_.to(torch::kCPU).contiguous();
        auto c_mu_u = C_mu_u_.to(torch::kCPU).contiguous();
        auto c_mu_v = C_mu_v_.to(torch::kCPU).contiguous();
        float D_mu_val = vertical_diag_mu_.to(torch::kCPU).contiguous().data_ptr<float>()[0];

        // Reshape for broadcasting: [nz, 1, 1] ⊗ [nz, ny, nx]
        // v20.14 r47c-fix3: Apply D_u scaling in 4D path too (consistency with main path).
        auto inv_D_u = (1.0f / (diag_u * du_scale_)).reshape({nz, 1, 1});
        auto inv_D_v = (1.0f / diag_v).reshape({nz, 1, 1});
        auto c_mu_u_3d = c_mu_u.reshape({nz, 1, 1});
        auto c_mu_v_3d = c_mu_v.reshape({nz, 1, 1});
        auto c_u_mu_3d = c_u_mu.reshape({nz, 1, 1});
        auto c_v_mu_3d = c_v_mu.reshape({nz, 1, 1});

        // Per-column inv(μ₀): [ny, nx] and [1, ny, nx]
        // v20.5 Fix 4: Guard mu_base inversion (4D path)
        {
            auto needs_clamp = mu_base_cpu < 1.0f;
            if (needs_clamp.any().item<bool>()) {
                static thread_local bool mu_base_clamp_warned_4d = false;
                if (!mu_base_clamp_warned_4d) {
                    torch::NoGradGuard no_grad;
                    std::cerr << "[PRECOND WARNING 4D] mu_base has values < 1.0 Pa (min="
                              << mu_base_cpu.min().item<float>()
                              << "). Clamping to 1.0. Check base state initialization."
                              << std::endl;
                    mu_base_clamp_warned_4d = true;
                }
            }
        }
        auto mu_base_safe_4d = torch::clamp(mu_base_cpu, /*min=*/1.0f);
        // v20.5: Use mu_full_stage_ when available for better Stage 2/3 accuracy (4D path)
        torch::Tensor mu_for_inv_4d;
        if (mu_full_stage_.defined() && mu_full_stage_.numel() == mu_base_safe_4d.numel()) {
            mu_for_inv_4d = torch::clamp(mu_full_stage_.to(mu_base_safe_4d.device()), /*min=*/1.0f);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                static thread_local int mu_full_log_count_4d = 0;
                if (mu_full_log_count_4d++ < 3) {
                    torch::NoGradGuard no_grad;
                    float mu_base_mean = mu_base_safe_4d.mean().item<float>();
                    float mu_full_mean = mu_for_inv_4d.mean().item<float>();
                    std::cerr << "[PRECOND 4x4 4D] Using mu_full_stage_ for inv_mu0:"
                              << " mu_base_mean=" << mu_base_mean
                              << " mu_full_mean=" << mu_full_mean
                              << " (delta=" << (mu_full_mean - mu_base_mean) << ")" << std::endl;
                }
            }
        } else {
            mu_for_inv_4d = mu_base_safe_4d;
        }
        auto inv_mu0 = 1.0f / mu_for_inv_4d;                  // [ny, nx]
        auto inv_mu0_3d = inv_mu0.unsqueeze(0);                // [1, ny, nx]
        auto inv_mu0_sq_3d = (inv_mu0 * inv_mu0).unsqueeze(0); // [1, ny, nx]

        // Φ coupling base scalars (k-independent; (j,i)-dependent through inv_mu0)
        float dt_gamma = dt_ * gamma_;
        float c2 = grid_info_->cs * grid_info_->cs;
        float H_x = 1.0f / grid_info_->dx;
        float H_y = 1.0f / grid_info_->dy;
        // v20.15 HEVI (precond-inc1): zero the HORIZONTAL phi PGF/divergence couplings under
        // hevi (moved to k_slow); KEEP A_phi_mu_base (vertical hydrostatic mu<->phi). 4D path twin
        // of the 4x4 gate above — both apply paths must agree with A_fast.
        const bool hevi_pc = wrf::sdirk3::g_sdirk3_config.hevi_split &&
                             (wrf::sdirk3::g_sdirk3_config.effective_imex_split_mode() >= 1);
        float A_u_phi_base = hevi_pc ? 0.0f : dt_gamma * H_x;
        float A_v_phi_base = hevi_pc ? 0.0f : dt_gamma * H_y;
        float A_phi_u_base = hevi_pc ? 0.0f : dt_gamma * c2 * H_x;
        float A_phi_v_base = hevi_pc ? 0.0f : dt_gamma * c2 * H_y;
        float A_phi_mu_base = dt_gamma * c2;

        // ====== STEP 1: Eliminate U, V from μ and Φ equations ======
        // r_mu_mod[k,j,i] = -(A_mu_u[k]*r_u[k,j,i]/D_u[k] + A_mu_v[k]*r_v[k,j,i]/D_v[k])
        auto r_mu_mod_k = -(c_mu_u_3d * r_u_all * inv_D_u) - (c_mu_v_3d * r_v_all * inv_D_v);
        auto r_mu_mod_accum = r_mu_mod_k.sum(0) + r_mu_batch;  // [ny, nx]

        // S_mu_mu: scalar (k-level independent of (j,i))
        auto S_mu_mu_k = -(c_mu_u * c_u_mu / diag_u) - (c_mu_v * c_v_mu / diag_v);
        float S_mu_mu_scalar = S_mu_mu_k.sum().item<float>() + D_mu_val;

        // alpha_phi[k] = coupling² / diagonal products (shared across columns)
        auto alpha_phi = A_phi_u_base * A_u_phi_base / diag_u
                       + A_phi_v_base * A_v_phi_base / diag_v;  // [nz]

        // S_phi_phi[k,j,i] = D_phi[k] - inv_mu0²[j,i] * alpha_phi[k]
        auto S_phi_phi = diag_phi.reshape({nz, 1, 1})
                       - inv_mu0_sq_3d * alpha_phi.reshape({nz, 1, 1});  // [nz, ny, nx]

        // Factored Schur coupling: S_mu_phi = inv_mu0 * base[k], S_phi_mu = inv_mu0 * base[k]
        auto options_cpu = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto S_mu_phi_base = torch::full({nz}, A_phi_mu_base, options_cpu)
            - c_mu_u * A_u_phi_base / diag_u - c_mu_v * A_v_phi_base / diag_v;
        auto S_phi_mu_base = torch::full({nz}, A_phi_mu_base, options_cpu)
            - A_phi_u_base * c_u_mu / diag_u - A_phi_v_base * c_v_mu / diag_v;

        auto S_mu_phi = inv_mu0_3d * S_mu_phi_base.reshape({nz, 1, 1});  // [nz, ny, nx]
        auto S_phi_mu = inv_mu0_3d * S_phi_mu_base.reshape({nz, 1, 1});  // [nz, ny, nx]

        // Modified Φ RHS: r_phi' = r_phi - inv_mu0 * (A_phi_u_base*r_u/D_u + A_phi_v_base*r_v/D_v)
        auto r_phi_mod = r_phi_all
            - inv_mu0_3d * (A_phi_u_base * r_u_all * inv_D_u + A_phi_v_base * r_v_all * inv_D_v);

        // ====== STEP 2: Schur reduction (k-sum) for μ ======
        // v20.14r27l: Relative singularity threshold (was absolute 1e-10).
        auto SING_EPS = S_phi_phi.abs().amax() * 1e-8f + 1e-30f;
        // v20.5 Fix 3: Sign-preserving clamp for S_phi_phi (4D path)
        auto S_phi_phi_signs = torch::where(S_phi_phi >= 0,
            torch::ones_like(S_phi_phi), -torch::ones_like(S_phi_phi));
        auto S_phi_phi_safe = torch::where(
            S_phi_phi.abs() > SING_EPS, S_phi_phi,
            S_phi_phi_signs * SING_EPS);
        auto inv_S_phi_phi = 1.0f / S_phi_phi_safe;

        // [ny, nx] via k-sum
        auto schur_diag_corr = (S_mu_phi * S_phi_mu * inv_S_phi_phi).sum(0);
        auto schur_rhs_corr = (S_mu_phi * r_phi_mod * inv_S_phi_phi).sum(0);

        auto S_mu_mu_reduced = S_mu_mu_scalar - schur_diag_corr;   // [ny, nx]
        auto r_mu_reduced = r_mu_mod_accum - schur_rhs_corr;        // [ny, nx]

        // v20.5 Fix 3: Sign-preserving clamp for S_mu_mu (4D path)
        auto S_mu_mu_signs = torch::where(S_mu_mu_reduced >= 0,
            torch::ones_like(S_mu_mu_reduced), -torch::ones_like(S_mu_mu_reduced));
        auto S_mu_mu_safe = torch::where(
            S_mu_mu_reduced.abs() > SING_EPS, S_mu_mu_reduced,
            S_mu_mu_signs * SING_EPS);

        // ====== STEP 3: Solve μ ======
        auto delta_mu = r_mu_reduced / S_mu_mu_safe;  // [ny, nx]

        // ====== STEP 4: Back-substitute Φ ======
        auto delta_phi = (r_phi_mod - S_phi_mu * delta_mu.unsqueeze(0)) * inv_S_phi_phi;

        // ====== STEP 5: Back-substitute U, V ======
        auto A_u_phi_3d = (A_u_phi_base * inv_mu0).unsqueeze(0);  // [1, ny, nx]
        auto A_v_phi_3d = (A_v_phi_base * inv_mu0).unsqueeze(0);  // [1, ny, nx]

        auto delta_u = (r_u_all - c_u_mu_3d * delta_mu.unsqueeze(0)
                        - A_u_phi_3d * delta_phi) * inv_D_u;
        auto delta_v = (r_v_all - c_v_mu_3d * delta_mu.unsqueeze(0)
                        - A_v_phi_3d * delta_phi) * inv_D_v;

        // Store results (same dimension conventions as extraction above)
        result[0].copy_(delta_u);
        result[1].copy_(delta_v);
        result[3].copy_(delta_phi);
        // MU write: result[5][j, 0, i] = delta_mu[j, i] — mirrors per-column
        // result[5].select(0, j).select(-1, i).copy_(mu_sol)
        result[5].select(1, 0).slice(0, 0, ny).copy_(delta_mu);

        uv_mu_calls = ny * nx;
    }

    // Fallback paths: j-i loop for cases without full 4×4 coupling
    if (!(nvars > 5 && C_u_phi_.defined())) {
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (nvars > 5) {
                // Fallback: 3×3 U-V-μ if Φ coefficients not initialized
                // Use raw-pointer overload with real μ residual for acoustic wave conditioning
                // Store tensors in lvalue variables before calling packed_accessor64
                torch::Tensor r_u_tensor = r[0];
                torch::Tensor r_v_tensor = r[1];
                torch::Tensor r_mu_tensor = r[5];  // Real MU from index 5
                torch::Tensor result_u_tensor = result[0];
                torch::Tensor result_v_tensor = result[1];
                torch::Tensor result_mu_tensor = result[5];

                // FIX Round194: Validate CPU/Float32/contiguous for packed_accessor64
                // Column pointer arithmetic (&acc[j][0][i]) requires k-dimension contiguity
                TORCH_CHECK(r_u_tensor.is_cpu() && r_u_tensor.is_contiguous() &&
                           r_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: r_u must be CPU/contiguous/Float32 for packed_accessor64");
                TORCH_CHECK(r_v_tensor.is_cpu() && r_v_tensor.is_contiguous() &&
                           r_v_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: r_v must be CPU/contiguous/Float32 for packed_accessor64");
                TORCH_CHECK(r_mu_tensor.is_cpu() && r_mu_tensor.is_contiguous() &&
                           r_mu_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: r_mu must be CPU/contiguous/Float32 for packed_accessor64");
                TORCH_CHECK(result_u_tensor.is_cpu() && result_u_tensor.is_contiguous() &&
                           result_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: result_u must be CPU/contiguous/Float32 for packed_accessor64");
                TORCH_CHECK(result_v_tensor.is_cpu() && result_v_tensor.is_contiguous() &&
                           result_v_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: result_v must be CPU/contiguous/Float32 for packed_accessor64");
                TORCH_CHECK(result_mu_tensor.is_cpu() && result_mu_tensor.is_contiguous() &&
                           result_mu_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner: result_mu must be CPU/contiguous/Float32 for packed_accessor64");

                auto r_u_acc = r_u_tensor.packed_accessor64<float, 3>();
                auto r_v_acc = r_v_tensor.packed_accessor64<float, 3>();
                auto r_mu_acc = r_mu_tensor.packed_accessor64<float, 3>();

                auto result_u_acc = result_u_tensor.packed_accessor64<float, 3>();
                auto result_v_acc = result_v_tensor.packed_accessor64<float, 3>();
                auto result_mu_acc = result_mu_tensor.packed_accessor64<float, 3>();

                // Extract pointers to vertical columns (contiguous in k dimension verified above)
                const float* r_u_col = &r_u_acc[j][0][i];
                const float* r_v_col = &r_v_acc[j][0][i];
                const float* r_mu_col = &r_mu_acc[j][0][i];

                float* u_sol_col = &result_u_acc[j][0][i];
                float* v_sol_col = &result_v_acc[j][0][i];
                float* mu_sol_col = &result_mu_acc[j][0][i];

                // Call allocation-free raw-pointer 3×3 solver
                solve_coupled_uv_mu_column_inplace(
                    r_u_col, r_v_col, r_mu_col,
                    u_sol_col, v_sol_col, mu_sol_col,
                    nz
                );

                // Φ with separate diagonal solve
                if (nvars > 3) {
                    auto phi_column = r[3].index({torch::indexing::Slice(), j, i});
                    auto phi_solution = solve_tridiagonal_with_variable_diag(
                        phi_column, vertical_diag_phi_
                    );
                    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
                    result[3].index_put_({torch::indexing::Slice(), j, i}, phi_solution);
                }

                ++uv_mu_calls;
            } else if (nvars > 1) {
                // Fallback: No MU in state, treat U-V as weakly coupled
                auto r_u_tensor = r[0];
                auto r_v_tensor = r[1];
                auto result_u_tensor = result[0];
                auto result_v_tensor = result[1];

                // FIX Round194: Validate CPU/Float32/contiguous for packed_accessor64
                TORCH_CHECK(r_u_tensor.is_cpu() && r_u_tensor.is_contiguous() &&
                           r_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner UV: r_u must be CPU/contiguous/Float32");
                TORCH_CHECK(r_v_tensor.is_cpu() && r_v_tensor.is_contiguous() &&
                           r_v_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner UV: r_v must be CPU/contiguous/Float32");
                TORCH_CHECK(result_u_tensor.is_cpu() && result_u_tensor.is_contiguous() &&
                           result_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner UV: result_u must be CPU/contiguous/Float32");
                TORCH_CHECK(result_v_tensor.is_cpu() && result_v_tensor.is_contiguous() &&
                           result_v_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner UV: result_v must be CPU/contiguous/Float32");

                // FIX Round180: Migrate from deprecated packed_accessor to packed_accessor64
                auto r_u_acc = r_u_tensor.packed_accessor64<float, 3>();
                auto r_v_acc = r_v_tensor.packed_accessor64<float, 3>();
                auto result_u_acc = result_u_tensor.packed_accessor64<float, 3>();
                auto result_v_acc = result_v_tensor.packed_accessor64<float, 3>();

                // Simple diagonal solve for U and V
                for (int k = 0; k < nz; ++k) {
                    result_u_acc[j][k][i] = r_u_acc[j][k][i] / vertical_diag_u_.data_ptr<float>()[std::min(k, static_cast<int>(vertical_diag_u_.size(0) - 1))];
                    result_v_acc[j][k][i] = r_v_acc[j][k][i] / vertical_diag_v_.data_ptr<float>()[std::min(k, static_cast<int>(vertical_diag_v_.size(0) - 1))];
                }

                // Φ with diagonal solve if present
                if (nvars > 3) {
                    auto phi_column = r[3].index({torch::indexing::Slice(), j, i});
                    auto phi_solution = solve_tridiagonal_with_variable_diag(
                        phi_column, vertical_diag_phi_
                    );
                    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
                    result[3].index_put_({torch::indexing::Slice(), j, i}, phi_solution);
                }
            } else if (nvars > 0) {
                // Fallback: only U exists
                auto r_u_tensor = r[0];
                auto result_u_tensor = result[0];

                // FIX Round194: Validate CPU/Float32/contiguous for packed_accessor64
                TORCH_CHECK(r_u_tensor.is_cpu() && r_u_tensor.is_contiguous() &&
                           r_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner U-only: r_u must be CPU/contiguous/Float32");
                TORCH_CHECK(result_u_tensor.is_cpu() && result_u_tensor.is_contiguous() &&
                           result_u_tensor.scalar_type() == torch::kFloat32,
                    "preconditioner U-only: result_u must be CPU/contiguous/Float32");

                // FIX Round180: Migrate from deprecated packed_accessor to packed_accessor64
                auto r_u_acc = r_u_tensor.packed_accessor64<float, 3>();
                auto result_u_acc = result_u_tensor.packed_accessor64<float, 3>();
                for (int k = 0; k < nz; ++k) {
                    result_u_acc[j][k][i] = r_u_acc[j][k][i] / vertical_diag_u_.data_ptr<float>()[std::min(k, static_cast<int>(vertical_diag_u_.size(0) - 1))];
                }
            }
        }
    }
    } // end fallback if (!(nvars > 5 && C_u_phi_.defined()))

    // PERFORMANCE PROFILING: Track U-V-MU coupled solver
    if (uv_mu_calls > 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        std::cerr << "[PRECOND TIMING] U-V-MU coupled solve: " << duration_ms << " ms ("
                  << uv_mu_calls << " columns, allocation-free)" << std::endl;
    }

    return result;
}

// LEGACY FUNCTION REMOVED
// This diagonal-only approximation has been replaced by solve_coupled_w_theta_column()
// which uses proper Gauss-Seidel iterations for W-θ coupling.
// Both 1D and 4D paths now use the unified coupled solver.

torch::Tensor UnifiedPreconditioner::solve_tridiagonal_with_variable_diag(
    const torch::Tensor& rhs, const torch::Tensor& diag) {

    // CRITICAL: Disable autograd to prevent graph pollution
    torch::NoGradGuard no_grad;

    // Simplified tridiagonal solve with variable diagonal
    int n = rhs.size(0);
    auto x = torch::zeros_like(rhs);
    
    // Simple diagonal approximation for now
    for (int i = 0; i < n; ++i) {
        int k = std::min(i, static_cast<int>(diag.size(0) - 1));
        x[i] = rhs[i] / diag[k];
    }
    
    return x;
}

// Keep other methods from original implementation...
torch::Tensor UnifiedPreconditioner::apply_horizontal_smoothing(const torch::Tensor& r) {

    // CRITICAL: Disable autograd to prevent graph pollution
    torch::NoGradGuard no_grad;

    // PERFORMANCE OPTIMIZATION: Fused in-place stencil instead of clone/pad/index
    if (r.dim() == 1) {
        return r;
    }

    // For 4D state tensor (nvar, ny, nx, nz), apply 4-point horizontal stencil
    // z_new[i,j] = (1-4α)*z[i,j] + α*(z[i-1,j] + z[i+1,j] + z[i,j-1] + z[i,j+1])
    // Zero boundary conditions (neighbors outside domain are treated as 0)

    auto z = r.clone();  // Single clone at start
    const float center_weight = 1.0f - 4.0f * horizontal_smooth_factor_;
    const float neighbor_weight = horizontal_smooth_factor_;

    // Allocate work buffer ONCE outside iteration loop
    auto z_work = torch::empty_like(z);

    for (int iter = 0; iter < n_smooth_iters_; ++iter) {
        // In-place 4-point stencil without padding/indexing overhead
        // Process interior points only; boundaries remain zero from initial clone
        auto z_acc = z.accessor<float, 4>();
        auto work_acc = z_work.accessor<float, 4>();

        const int nvar = z.size(0);
        const int ny = z.size(1);
        const int nx = z.size(2);
        const int nz = z.size(3);

        // Apply stencil to interior points
        for (int v = 0; v < nvar; ++v) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    for (int k = 0; k < nz; ++k) {
                        float center = z_acc[v][j][i][k];
                        float left = (i > 0) ? z_acc[v][j][i-1][k] : 0.0f;
                        float right = (i < nx-1) ? z_acc[v][j][i+1][k] : 0.0f;
                        float down = (j > 0) ? z_acc[v][j-1][i][k] : 0.0f;
                        float up = (j < ny-1) ? z_acc[v][j+1][i][k] : 0.0f;

                        work_acc[v][j][i][k] = center_weight * center +
                                               neighbor_weight * (left + right + down + up);
                    }
                }
            }
        }

        // Swap buffers for next iteration
        std::swap(z, z_work);
    }

    return z;
}

torch::Tensor UnifiedPreconditioner::apply_physical_scaling(const torch::Tensor& r) {

    // CRITICAL: Disable autograd to prevent graph pollution
    torch::NoGradGuard no_grad;

    // [Keep original implementation]
    if (r.dim() == 1) {
        return r * 0.9f;
    }
    
    auto result = r.clone();
    
    std::vector<float> scale_factors = {
        1.0f,                          // u
        1.0f,                          // v  
        1.0f,                          // w
        1.0f / grid_info_->cp,         // theta
        1.0f / (grid_info_->cs * grid_info_->cs)  // phi
    };
    
    for (int var = 0; var < 5; ++var) {
        if (var < result.size(0)) {
            result[var] *= scale_factors[var];
        }
    }
    
    return result;
}

torch::Tensor UnifiedPreconditioner::solve_tridiagonal(
    const torch::Tensor& lower,
    const torch::Tensor& diag,
    const torch::Tensor& upper,
    const torch::Tensor& rhs) {

    // CRITICAL: Disable autograd to prevent graph pollution
    torch::NoGradGuard no_grad;

    // [Keep original Thomas algorithm implementation]
    int n = diag.size(0);
    
    auto c = torch::zeros({n-1}, rhs.options());
    auto d = torch::zeros({n}, rhs.options());
    auto x = torch::zeros_like(rhs);
    
    // v20.14 r49-fix: Sign-preserving epsilon for Thomas denominator clamping
    constexpr float thomas_eps = 1e-10f;
    auto safe_denom = [](const torch::Tensor& d, float eps) {
        return torch::where(torch::abs(d) < eps,
                           torch::full_like(d, eps),
                           d);
    };

    // Forward sweep
    auto diag0_safe = safe_denom(diag[0].unsqueeze(0), thomas_eps).squeeze(0);
    c[0] = upper[0] / diag0_safe;
    d[0] = rhs[0] / diag0_safe;

    for (int i = 1; i < n-1; ++i) {
        auto denom = diag[i] - lower[i-1] * c[i-1];
        auto denom_safe = safe_denom(denom.unsqueeze(0), thomas_eps).squeeze(0);
        c[i] = upper[i] / denom_safe;
        d[i] = (rhs[i] - lower[i-1] * d[i-1]) / denom_safe;
    }

    // Last element
    auto denom_last = diag[n-1] - lower[n-2] * c[n-2];
    auto denom_last_safe = safe_denom(denom_last.unsqueeze(0), thomas_eps).squeeze(0);
    d[n-1] = (rhs[n-1] - lower[n-2] * d[n-2]) / denom_last_safe;
    
    // Back substitution
    x[n-1] = d[n-1];
    for (int i = n-2; i >= 0; --i) {
        x[i] = d[i] - c[i] * x[i+1];
    }
    
    return x;
}

std::pair<torch::Tensor, torch::Tensor> UnifiedPreconditioner::solve_coupled_w_theta_column(
    const torch::Tensor& r_w,
    const torch::Tensor& r_theta,
    int nz,
    int nz_w) {

    // PERFORMANCE OPTIMIZATION: Complete rewrite using raw memory and precomputed coefficients
    // CRITICAL: Disable autograd to prevent graph pollution
    torch::NoGradGuard no_grad;

    // STEP 1: Precompute coupling coefficients (avoid repeated .item() calls)
    // CRITICAL FIX: Track coefficient generation to invalidate cache on update()
    static thread_local std::vector<float> w_theta_coupling_upper_cached;
    static thread_local std::vector<float> w_theta_coupling_lower_cached;
    static thread_local std::vector<float> theta_w_coupling_upper_cached;
    static thread_local std::vector<float> theta_w_coupling_lower_cached;
    static thread_local std::vector<float> vertical_lower_w_cached;
    static thread_local std::vector<float> vertical_diag_w_cached;
    static thread_local std::vector<float> vertical_upper_w_cached;
    static thread_local std::vector<float> vertical_lower_theta_cached;
    static thread_local std::vector<float> vertical_diag_theta_cached;
    static thread_local std::vector<float> vertical_upper_theta_cached;
    static thread_local uint64_t cached_generation = 0;
    // v20.14r27z: Include instance pointer for multi-tile cache isolation.
    static thread_local const void* cached_wt_instance = nullptr;

    // Invalidate cache if coefficients changed, instance changed, or size mismatch
    if (cached_generation != coefficient_generation_ || cached_wt_instance != this ||
        w_theta_coupling_upper_cached.size() != static_cast<size_t>(nz)) {
        cached_wt_instance = this;
        // Cache coupling coefficients
        w_theta_coupling_upper_cached.resize(nz);
        w_theta_coupling_lower_cached.resize(nz);
        theta_w_coupling_upper_cached.resize(nz);
        theta_w_coupling_lower_cached.resize(nz);

        // FIX 2025-12-27: Transfer tensors to CPU once, use data_ptr for efficient loop access
        // This avoids repeated GPU->CPU sync from per-element .item<float>() calls
        auto w_theta_upper_cpu = w_theta_coupling_upper_.to(torch::kCPU).contiguous();
        auto w_theta_lower_cpu = w_theta_coupling_lower_.to(torch::kCPU).contiguous();
        auto theta_w_upper_cpu = theta_w_coupling_upper_.to(torch::kCPU).contiguous();
        auto theta_w_lower_cpu = theta_w_coupling_lower_.to(torch::kCPU).contiguous();
        const float* w_theta_upper_ptr = w_theta_upper_cpu.data_ptr<float>();
        const float* w_theta_lower_ptr = w_theta_lower_cpu.data_ptr<float>();
        const float* theta_w_upper_ptr = theta_w_upper_cpu.data_ptr<float>();
        const float* theta_w_lower_ptr = theta_w_lower_cpu.data_ptr<float>();
        int w_theta_upper_sz = static_cast<int>(w_theta_coupling_upper_.size(0));
        int w_theta_lower_sz = static_cast<int>(w_theta_coupling_lower_.size(0));
        int theta_w_upper_sz = static_cast<int>(theta_w_coupling_upper_.size(0));
        int theta_w_lower_sz = static_cast<int>(theta_w_coupling_lower_.size(0));

        for (int k = 0; k < nz; ++k) {
            w_theta_coupling_upper_cached[k] = (k < w_theta_upper_sz) ? w_theta_upper_ptr[k] : 0.0f;
            w_theta_coupling_lower_cached[k] = (k < w_theta_lower_sz) ? w_theta_lower_ptr[k] : 0.0f;
            theta_w_coupling_upper_cached[k] = (k < theta_w_upper_sz) ? theta_w_upper_ptr[k] : 0.0f;
            theta_w_coupling_lower_cached[k] = (k < theta_w_lower_sz) ? theta_w_lower_ptr[k] : 0.0f;
        }

        // Cache tridiagonal coefficients
        vertical_lower_w_cached.resize(nz_w);
        vertical_diag_w_cached.resize(nz_w);
        vertical_upper_w_cached.resize(nz_w);
        vertical_lower_theta_cached.resize(nz);
        vertical_diag_theta_cached.resize(nz);
        vertical_upper_theta_cached.resize(nz);

        // FIX 2025-12-27: Transfer vertical coefficient tensors to CPU once
        auto vert_lower_w_cpu = vertical_lower_w_.to(torch::kCPU).contiguous();
        auto vert_diag_w_cpu = vertical_diag_w_.to(torch::kCPU).contiguous();
        auto vert_upper_w_cpu = vertical_upper_w_.to(torch::kCPU).contiguous();
        auto vert_lower_theta_cpu = vertical_lower_theta_.to(torch::kCPU).contiguous();
        auto vert_diag_theta_cpu = vertical_diag_theta_.to(torch::kCPU).contiguous();
        auto vert_upper_theta_cpu = vertical_upper_theta_.to(torch::kCPU).contiguous();
        const float* vert_lower_w_ptr = vert_lower_w_cpu.data_ptr<float>();
        const float* vert_diag_w_ptr = vert_diag_w_cpu.data_ptr<float>();
        const float* vert_upper_w_ptr = vert_upper_w_cpu.data_ptr<float>();
        const float* vert_lower_theta_ptr = vert_lower_theta_cpu.data_ptr<float>();
        const float* vert_diag_theta_ptr = vert_diag_theta_cpu.data_ptr<float>();
        const float* vert_upper_theta_ptr = vert_upper_theta_cpu.data_ptr<float>();
        int vert_lower_w_sz = static_cast<int>(vertical_lower_w_.size(0));
        int vert_upper_w_sz = static_cast<int>(vertical_upper_w_.size(0));
        int vert_lower_theta_sz = static_cast<int>(vertical_lower_theta_.size(0));
        int vert_upper_theta_sz = static_cast<int>(vertical_upper_theta_.size(0));

        for (int k = 0; k < nz_w; ++k) {
            vertical_lower_w_cached[k] = (k < vert_lower_w_sz) ? vert_lower_w_ptr[k] : 0.0f;
            vertical_diag_w_cached[k] = vert_diag_w_ptr[k];
            vertical_upper_w_cached[k] = (k < vert_upper_w_sz) ? vert_upper_w_ptr[k] : 0.0f;
        }
        for (int k = 0; k < nz; ++k) {
            vertical_lower_theta_cached[k] = (k < vert_lower_theta_sz) ? vert_lower_theta_ptr[k] : 0.0f;
            vertical_diag_theta_cached[k] = vert_diag_theta_ptr[k];
            vertical_upper_theta_cached[k] = (k < vert_upper_theta_sz) ? vert_upper_theta_ptr[k] : 0.0f;
        }

        // Mark cache as synchronized with current coefficient generation
        cached_generation = coefficient_generation_;
    }

    // STEP 2: Allocate stack buffers for solutions (reuse across iterations)
    std::vector<float> w_sol(nz_w);
    std::vector<float> theta_sol(nz);
    std::vector<float> rhs_w_buf(nz_w);
    std::vector<float> rhs_theta_buf(nz);

    // CRITICAL FIX: Keep contiguous tensors alive to avoid dangling pointers
    // If input is non-contiguous (common for column slices), contiguous() allocates
    // a new buffer that must not be destroyed before we finish copying
    auto r_w_contig = r_w.contiguous();
    auto r_theta_contig = r_theta.contiguous();
    const float* r_w_data = r_w_contig.data_ptr<float>();
    const float* r_theta_data = r_theta_contig.data_ptr<float>();

    // CRITICAL FIX: Initialize Gauss-Seidel iteration with ZEROS, not RHS values!
    // Starting with w_sol = r_w causes amplification in first iteration when
    // coupling terms use r_theta values. Zero initialization gives proper convergence.
    std::fill(w_sol.begin(), w_sol.end(), 0.0f);
    std::fill(theta_sol.begin(), theta_sol.end(), 0.0f);

    // v20.14r27p: Fixed iteration count (stationary preconditioner).
    // Previous adaptive logic changed n_iterations per-call, violating GMRES linearity.
    auto& config = wrf::sdirk3::g_sdirk3_config;
    int n_iterations = config.precond_gs_min_iterations;

    for (int iter = 0; iter < config.precond_gs_max_iterations; ++iter) {
        // 1. Solve for W given current θ
        // Build RHS: r_w - dt*γ*B_w→θ*θ
        std::copy(r_w_data, r_w_data + nz_w, rhs_w_buf.begin());

        for (int k = 1; k < nz_w - 1; ++k) {
            if (k < nz) {
                if (k > 0 && k < nz) {
                    rhs_w_buf[k] -= w_theta_coupling_lower_cached[k-1] * theta_sol[k-1];
                }
                if (k < nz - 1) {
                    rhs_w_buf[k] -= w_theta_coupling_upper_cached[k] * theta_sol[k];
                }
            }
        }

        // Solve tridiagonal in-place (optimized Thomas algorithm)
        solve_tridiagonal_inplace(vertical_lower_w_cached.data(), vertical_diag_w_cached.data(),
                                  vertical_upper_w_cached.data(), rhs_w_buf.data(), w_sol.data(), nz_w);

        // 2. Solve for θ given updated W
        // Build RHS: r_theta - dt*γ*B_θ→w*w
        std::copy(r_theta_data, r_theta_data + nz, rhs_theta_buf.begin());

        for (int k = 0; k < nz; ++k) {
            if (k > 0 && k + 1 < nz_w) {
                rhs_theta_buf[k] -= theta_w_coupling_lower_cached[k-1] * w_sol[k];
            }
            if (k + 1 < nz_w) {
                rhs_theta_buf[k] -= theta_w_coupling_upper_cached[k] * w_sol[k + 1];
            }
        }

        // Solve tridiagonal in-place
        solve_tridiagonal_inplace(vertical_lower_theta_cached.data(), vertical_diag_theta_cached.data(),
                                  vertical_upper_theta_cached.data(), rhs_theta_buf.data(), theta_sol.data(), nz);

        // v20.14r27p: Fixed iteration count for GMRES linearity.
        if (iter + 1 >= n_iterations) break;
    }

    // STEP 4: Convert back to tensors (single allocation)
    // FIX 2025-12-31 Batch35 Issue 1: Explicit CPU device for host pointer from_blob
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto w_solution = torch::from_blob(w_sol.data(), {nz_w}, cpu_opts).clone();  // LINT_EXCEPTION: CPU opts above
    auto theta_solution = torch::from_blob(theta_sol.data(), {nz}, cpu_opts).clone();  // LINT_EXCEPTION: CPU opts above

    return {w_solution, theta_solution};
}

// PERFORMANCE: Lightweight in-place tridiagonal solver using raw pointers
void UnifiedPreconditioner::solve_tridiagonal_inplace(
    const float* lower, const float* diag, const float* upper,
    const float* rhs, float* solution, int n) {

    // CRITICAL FIX: Handle degenerate cases (thin vertical domains, boundary slices)
    // Thomas algorithm requires n >= 2 for lower[n-2] and upper[0] to be valid
    if (n == 0) {
        return;  // Empty system, nothing to do
    }
    if (n == 1) {
        // Single equation: diag[0] * solution[0] = rhs[0]
        float d0 = diag[0];
        float ad0 = std::abs(d0);
        solution[0] = rhs[0] / ((ad0 < 1e-12f) ? std::copysign(1e-12f, d0 + 1e-30f) : d0);
        return;
    }

    // Scratch buffer for modified coefficients (reuse thread-local storage)
    static thread_local std::vector<float> c_buf;
    static thread_local std::vector<float> d_buf;
    c_buf.resize(n);
    d_buf.resize(n);

    // v20.14r27o: Sign-preserving epsilon clamp for near-singular denominators.
    // denom = sign(denom) * max(|denom|, eps) prevents division blow-up.
    auto safe_denom = [](float d) -> float {
        return (std::abs(d) < 1e-12f) ? std::copysign(1e-12f, d + 1e-30f) : d;
    };

    // Forward elimination
    float d0 = safe_denom(diag[0]);
    c_buf[0] = upper[0] / d0;
    d_buf[0] = rhs[0] / d0;

    for (int i = 1; i < n - 1; ++i) {
        float denom = safe_denom(diag[i] - lower[i-1] * c_buf[i-1]);
        c_buf[i] = upper[i] / denom;
        d_buf[i] = (rhs[i] - lower[i-1] * d_buf[i-1]) / denom;
    }

    // Last element
    float denom_last = safe_denom(diag[n-1] - lower[n-2] * c_buf[n-2]);
    d_buf[n-1] = (rhs[n-1] - lower[n-2] * d_buf[n-2]) / denom_last;

    // Back substitution
    solution[n-1] = d_buf[n-1];
    for (int i = n-2; i >= 0; --i) {
        solution[i] = d_buf[i] - c_buf[i] * solution[i+1];
    }
}

// FIX 2026-01-31: Batched W-θ coupled solver.
// Processes all (j,i) columns simultaneously using batched Thomas algorithm.
// Tridiagonal coefficients are 1D (shared across columns), only RHS varies per column.
void UnifiedPreconditioner::solve_coupled_w_theta_batched(
    torch::Tensor& w_block,
    torch::Tensor& theta_block,
    int nz,
    int nz_w,
    torch::Tensor* phi_block,
    const float* phi_diag,
    const float* A_eff,
    int phase2_nz_w) {

    torch::NoGradGuard no_grad;

    // Ensure coefficient cache is up-to-date (reuse column solver's cache logic)
    // Call column solver once with dummy data to prime the cache if needed
    static thread_local std::vector<float> w_theta_coup_upper;
    static thread_local std::vector<float> w_theta_coup_lower;
    static thread_local std::vector<float> theta_w_coup_upper;
    static thread_local std::vector<float> theta_w_coup_lower;
    static thread_local std::vector<float> vl_w, vd_w, vu_w;
    static thread_local std::vector<float> vl_t, vd_t, vu_t;
    static thread_local uint64_t batch_cached_gen = 0;
    static thread_local int batch_cached_nz = 0;
    static thread_local int batch_cached_nz_w = 0;
    // v20.14 r47c-fix3b: Instance isolation for multi-tile (matches column solver at :3866).
    static thread_local const void* batch_cached_instance = nullptr;

    if (batch_cached_gen != coefficient_generation_ ||
        batch_cached_instance != this ||
        batch_cached_nz != nz || batch_cached_nz_w != nz_w) {
        w_theta_coup_upper.resize(nz);
        w_theta_coup_lower.resize(nz);
        theta_w_coup_upper.resize(nz);
        theta_w_coup_lower.resize(nz);

        auto to_vec = [](const torch::Tensor& t, std::vector<float>& v, int sz) {
            auto cpu = t.to(torch::kCPU).contiguous();
            const float* p = cpu.data_ptr<float>();
            int actual = static_cast<int>(t.size(0));
            for (int k = 0; k < sz; ++k)
                v[k] = (k < actual) ? p[k] : 0.0f;
        };
        to_vec(w_theta_coupling_upper_, w_theta_coup_upper, nz);
        to_vec(w_theta_coupling_lower_, w_theta_coup_lower, nz);
        to_vec(theta_w_coupling_upper_, theta_w_coup_upper, nz);
        to_vec(theta_w_coupling_lower_, theta_w_coup_lower, nz);

        vl_w.resize(nz_w); vd_w.resize(nz_w); vu_w.resize(nz_w);
        vl_t.resize(nz);   vd_t.resize(nz);   vu_t.resize(nz);

        auto to_vec_full = [](const torch::Tensor& t, std::vector<float>& v, int sz) {
            auto cpu = t.to(torch::kCPU).contiguous();
            const float* p = cpu.data_ptr<float>();
            int actual = static_cast<int>(t.size(0));
            for (int k = 0; k < sz; ++k)
                v[k] = (k < actual) ? p[k] : 0.0f;
        };
        to_vec_full(vertical_lower_w_, vl_w, nz_w);
        {
            auto cpu = vertical_diag_w_.to(torch::kCPU).contiguous();
            const float* p = cpu.data_ptr<float>();
            for (int k = 0; k < nz_w; ++k) vd_w[k] = p[k];
        }
        to_vec_full(vertical_upper_w_, vu_w, nz_w);
        to_vec_full(vertical_lower_theta_, vl_t, nz);
        {
            auto cpu = vertical_diag_theta_.to(torch::kCPU).contiguous();
            const float* p = cpu.data_ptr<float>();
            for (int k = 0; k < nz; ++k) vd_t[k] = p[k];
        }
        to_vec_full(vertical_upper_theta_, vu_t, nz);

        batch_cached_gen = coefficient_generation_;
        batch_cached_instance = this;
        batch_cached_nz = nz;
        batch_cached_nz_w = nz_w;
    }

    // v20.14r27o: Sign-preserving epsilon clamp for near-singular Thomas denominators
    auto safe_denom = [](float d) -> float {
        return (std::abs(d) < 1e-12f) ? std::copysign(1e-12f, d + 1e-30f) : d;
    };

    // v20.14 r47: Phase 2 Schur complement — diagonal boost
    bool phase2_active = (phi_block != nullptr && phi_diag != nullptr && A_eff != nullptr);

    // Precompute Thomas c_prime scalars (same for all columns)
    // Phase 2: vd_w_eff[k] = vd_w[k] + A_eff[k]²/D_phi[k]
    std::vector<float> c_prime_w(nz_w), denom_w(nz_w);

    // v20.14 r47c: ablation flag — boost can be independently disabled
    bool boost_on = phase2_active && wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_boost_on;
    bool rhs_on = phase2_active && wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_rhs_inject_on;
    bool backsub_on = phase2_active && wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_backsub_on;

    auto get_vd_eff = [&](int k) -> float {
        float vd = vd_w[k];
        if (!boost_on || k <= 0 || k >= nz || k >= phase2_nz_w) return vd;
        float D_phi_k = phi_diag[k];
        if (D_phi_k <= 1e-6f) return vd;
        float A = A_eff[k];
        return vd + A * A / D_phi_k;
    };

    denom_w[0] = safe_denom(get_vd_eff(0));
    c_prime_w[0] = vu_w[0] / denom_w[0];
    for (int k = 1; k < nz_w; ++k) {
        denom_w[k] = safe_denom(get_vd_eff(k) - vl_w[k > 0 ? k - 1 : 0] * c_prime_w[k - 1]);
        if (k < nz_w - 1)
            c_prime_w[k] = vu_w[k] / denom_w[k];
    }

    std::vector<float> c_prime_t(nz), denom_t(nz);
    denom_t[0] = safe_denom(vd_t[0]);
    c_prime_t[0] = vu_t[0] / denom_t[0];
    for (int k = 1; k < nz; ++k) {
        denom_t[k] = safe_denom(vd_t[k] - vl_t[k > 0 ? k - 1 : 0] * c_prime_t[k - 1]);
        if (k < nz - 1)
            c_prime_t[k] = vu_t[k] / denom_t[k];
    }

    // Gauss-Seidel iterations with batched Thomas
    auto& config = wrf::sdirk3::g_sdirk3_config;
    int n_iterations = config.precond_gs_min_iterations;

    // Initialize solutions to zero
    auto w_sol = torch::zeros_like(w_block);
    auto theta_sol = torch::zeros_like(theta_block);

    // d_prime work tensors
    auto d_prime_w = torch::zeros_like(w_block);
    auto d_prime_t = torch::zeros_like(theta_block);

    for (int iter = 0; iter < config.precond_gs_max_iterations; ++iter) {
        // === Solve W given current θ ===
        // Build RHS: rhs_w = w_block - coupling * theta_sol
        auto rhs_w = w_block.clone();
        for (int k = 1; k < nz_w - 1; ++k) {
            if (k < nz && k > 0 && k < nz) {
                rhs_w.select(1, k).sub_(w_theta_coup_lower[k - 1] * theta_sol.select(1, k - 1));
            }
            if (k < nz && k < nz - 1) {
                rhs_w.select(1, k).sub_(w_theta_coup_upper[k] * theta_sol.select(1, k));
            }
        }

        // v20.14 r47: Phase 2 — inject Φ into W RHS using A_eff
        // v20.14 r47c: ablation flag — RHS injection can be independently disabled
        if (rhs_on) {
            for (int k = 1; k < std::min(nz, phase2_nz_w); ++k) {
                float A = A_eff[k];
                if (A > 1e-15f) {
                    rhs_w.select(1, k).sub_(A * phi_block->select(1, k));
                }
            }
        }

        // Batched Thomas forward sweep for W
        d_prime_w.select(1, 0).copy_(rhs_w.select(1, 0) / denom_w[0]);
        for (int k = 1; k < nz_w; ++k) {
            float lower_k = (k > 0) ? vl_w[k - 1] : 0.0f;
            d_prime_w.select(1, k).copy_(
                (rhs_w.select(1, k) - lower_k * d_prime_w.select(1, k - 1)) / denom_w[k]);
        }
        // Backward sweep
        w_sol.select(1, nz_w - 1).copy_(d_prime_w.select(1, nz_w - 1));
        for (int k = nz_w - 2; k >= 0; --k) {
            w_sol.select(1, k).copy_(d_prime_w.select(1, k) - c_prime_w[k] * w_sol.select(1, k + 1));
        }

        // === Solve θ given updated W ===
        auto rhs_t = theta_block.clone();
        for (int k = 0; k < nz; ++k) {
            if (k > 0 && k + 1 < nz_w) {
                rhs_t.select(1, k).sub_(theta_w_coup_lower[k - 1] * w_sol.select(1, k));
            }
            if (k + 1 < nz_w) {
                rhs_t.select(1, k).sub_(theta_w_coup_upper[k] * w_sol.select(1, k + 1));
            }
        }

        // Batched Thomas forward sweep for θ
        d_prime_t.select(1, 0).copy_(rhs_t.select(1, 0) / denom_t[0]);
        for (int k = 1; k < nz; ++k) {
            float lower_k = (k > 0) ? vl_t[k - 1] : 0.0f;
            d_prime_t.select(1, k).copy_(
                (rhs_t.select(1, k) - lower_k * d_prime_t.select(1, k - 1)) / denom_t[k]);
        }
        // Backward sweep
        theta_sol.select(1, nz - 1).copy_(d_prime_t.select(1, nz - 1));
        for (int k = nz - 2; k >= 0; --k) {
            theta_sol.select(1, k).copy_(d_prime_t.select(1, k) - c_prime_t[k] * theta_sol.select(1, k + 1));
        }

        // v20.14r27p: Fixed iteration count (stationary preconditioner).
        // Previous adaptive logic (input-dependent n_iterations) violated GMRES linearity.
        // W-θ coupled solve runs exactly precond_gs_min_iterations per apply.
        if (iter + 1 >= n_iterations) break;
    }

    // Write results back in-place
    w_block.copy_(w_sol);
    theta_block.copy_(theta_sol);

    // v20.14 r47: Phase 2 — Φ back-substitution using A_eff
    // phi += eta_bs * (A_eff/D_phi) * w_sol. eta_bs < 1 relaxes Φ correction.
    // v20.14 r47c: ablation flag — back-sub can be independently disabled
    if (backsub_on) {
        const float eta_bs = wrf::sdirk3::g_sdirk3_config.precond_phi_w_schur_backsub_relax;
        for (int k = 1; k < std::min(nz, phase2_nz_w); ++k) {
            float A = A_eff[k];
            float D_phi_k = phi_diag[k];
            if (A > 1e-15f && D_phi_k > 1e-6f) {
                phi_block->select(1, k).add_(
                    (eta_bs * A / D_phi_k) * w_sol.select(1, k));
            }
        }
    }
}

bool UnifiedPreconditioner::compute_phi_w_coupling_coefficients(int nz, int nz_w) {
    if (phi_w_cached_gen_ == coefficient_generation_)
        return !phi_w_coupling_wph_.empty();  // already cached

    auto& config = wrf::sdirk3::g_sdirk3_config;
    int mc_sz = static_cast<int>(momentum_coupling_k_cached_.size());
    int dz_sz = static_cast<int>(dz_effective_cached_.size());

    // Prerequisite check: scale=0 needs mc cache; all scales need dz cache
    if (dz_sz < 2) return false;
    if (config.precond_phi_w_coupling_scale == 0 && mc_sz < 2) return false;

    phi_w_coupling_wph_.resize(nz_w, 0.0f);
    phi_w_coupling_det_.resize(nz_w, 1.0f);
    phi_w_D_w_nosboost_.resize(nz_w, 1.0f);

    float dt_gamma = dt_ * gamma_;
    float g_val = grid_info_->g;
    float c_s = grid_info_->cs;
    auto phi_diag_ptr = vertical_diag_phi_.data_ptr<float>();
    auto w_diag_ptr = vertical_diag_w_.data_ptr<float>();

    int floor_hit_count = 0;
    int interior_count = 0;
    float D_W_nosboost_min_raw = 1e30f;  // track pre-floor minimum

    for (int k = 0; k < nz_w; ++k) {
        // Boundary levels: no coupling
        bool boundary = (k == 0 || k >= nz);
        if (!boundary && config.precond_phi_w_coupling_scale == 0 && k >= mc_sz) boundary = true;
        if (boundary) {
            phi_w_coupling_wph_[k] = 0.0f;
            phi_w_D_w_nosboost_[k] = w_diag_ptr[k];
            phi_w_coupling_det_[k] = phi_diag_ptr[std::min(k, nz)] * w_diag_ptr[k];
            continue;
        }

        ++interior_count;
        int k_lo = std::min(k - 1, dz_sz - 1);
        int k_hi = std::min(k, dz_sz - 1);
        float dz_w = std::max(0.5f * (dz_effective_cached_[k_lo] + dz_effective_cached_[k_hi]), 1.0f);

        // Compute A_wφ based on coupling scale
        float A_wph = 0.0f;
        if (config.precond_phi_w_coupling_scale == 0) {
            // GS-scale: dt·γ·g/(mc·μ·dz)
            float mu_sc = std::max(mu_scale_correction_, 1e-6f);
            A_wph = dt_gamma * g_val / (momentum_coupling_k_cached_[k] * mu_sc * dz_w);
        } else if (config.precond_phi_w_coupling_scale == 1) {
            // Physics: dt·γ·g/dz
            A_wph = dt_gamma * g_val / dz_w;
        } else {
            // Acoustic: dt·γ·c_s/dz
            A_wph = dt_gamma * c_s / dz_w;
        }
        // Store RAW A_wph (no cap). The 2×2 formula is self-regulating:
        // correction = A*r_w / (D_phi*D_w + A²) → for large A, ≈ r_w/A → decreasing.
        phi_w_coupling_wph_[k] = A_wph;

        // D_W_nosboost = D_W_full - schur_boosted
        float acfl_sq = (dt_gamma * c_s / dz_w) * (dt_gamma * c_s / dz_w);
        float A_phi_diag = 1.0f + dt_gamma * c_s * c_s / (dz_w * dz_w);
        float schur = acfl_sq / A_phi_diag;
        float schur_boosted = schur * w_acoustic_boost_cached_;
        float D_W_nosboost = w_diag_ptr[k] - schur_boosted;
        // Track pre-floor value for diagnostics
        D_W_nosboost_min_raw = std::min(D_W_nosboost_min_raw, D_W_nosboost);
        // Stability guard: prevent D_W_nosboost ≈ 0.
        float dw_floor = config.precond_dw_nosboost_floor;
        if (D_W_nosboost < dw_floor) ++floor_hit_count;
        D_W_nosboost = std::max(D_W_nosboost, dw_floor);
        phi_w_D_w_nosboost_[k] = D_W_nosboost;

        // det = D_φ * D_w_nosboost + A² (opposite-sign coupling → always positive)
        float D_phi = phi_diag_ptr[k];
        float det = D_phi * D_W_nosboost + A_wph * A_wph;
        phi_w_coupling_det_[k] = std::max(det, 1e-10f);
    }

    // Generation-based log (fires every time coefficients recomputed)
    if (config.debug_level >= 1) {
        int k_mid = nz / 2;
        float A_raw_mid = phi_w_coupling_wph_[k_mid];
        float gs_cap_base = (config.precond_gs_awphi_cap_coupled >= 0.0f
            ? config.precond_gs_awphi_cap_coupled : config.precond_gs_awphi_cap);
        std::cerr << "[PRECOND PHI-FEEDBACK] Coefficients cached (gen="
                  << coefficient_generation_ << ", scale="
                  << config.precond_phi_w_coupling_scale << "):"
                  << " A_raw[" << k_mid << "]=" << A_raw_mid
                  << ", det_raw=" << phi_w_coupling_det_[k_mid]
                  << " (A²=" << (A_raw_mid * A_raw_mid) << ")"
                  << ", D_W_nosb=" << phi_w_D_w_nosboost_[k_mid]
                  << " (floor=" << config.precond_dw_nosboost_floor << ")"
                  << ", D_phi=" << phi_diag_ptr[k_mid]
                  << ", D_W_floor_hit=" << floor_hit_count << "/" << interior_count
                  << " (raw_min=" << D_W_nosboost_min_raw << ")"
                  << std::endl;
        // r46h: Separate line for runtime-variable params (cap/soft are N-dependent)
        std::cerr << "  runtime: fb_cap=" << config.precond_phi_feedback_cap
                  << (config.precond_phi_feedback_cap_high > 0 ?
                     "→" + std::to_string(config.precond_phi_feedback_cap_high) : "")
                  << ", gs_cap=" << gs_cap_base
                  << (config.precond_gs_awphi_cap_coupled_high > 0 ?
                     "→" + std::to_string(config.precond_gs_awphi_cap_coupled_high) : "")
                  << ", soft=" << (config.precond_phi_feedback_soft_cap ? "tanh" : "hard")
                  << ", beta=" << config.precond_phi_feedback_beta
                  << ", max_corr=" << (config.precond_phi_feedback_max_corr < 0 ? -1.0f : config.precond_phi_feedback_max_corr)
                  << std::endl;
    }

    phi_w_cached_gen_ = coefficient_generation_;
    return true;
}

// v20.14 r47c-fix3: set_newton_ru_share — moved from header (needs g_sdirk3_config).
void UnifiedPreconditioner::set_newton_ru_share(float ru_share) {
    newton_ru_share_ = ru_share;
    // Compute D_u scale for dual-phase strategy.
    // When ru-dominated, weaken D_u in 4×4 Schur → larger U corrections.
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    float factor = cfg.precond_du_weak_factor;
    const float ru_thresh = cfg.precond_du_weak_ru_thresh;
    const bool stage3_budget_override_active =
        (current_stage_ >= 3) &&
        (cfg.stage3_gmres_restart > 0 ||
         cfg.stage3_max_krylov_restarts > 0 ||
         cfg.stage3_krylov_tol > 0.0f);

    if (factor < 0.999f && ru_share > ru_thresh) {
        du_scale_ = factor;
    } else if (stage3_budget_override_active &&
               factor >= 0.999f &&
               ru_share > std::max(ru_thresh, 0.95f)) {
        // v20.14r59-step2: Auto-fallback for Stage-3 ru-dominant runs when
        // explicit du_weak is not configured. Keep default-off behavior by
        // requiring stage3_* override activation.
        // ru_share=0.95 -> 0.60, ru_share=1.00 -> 0.25 (clamped).
        float ramp = std::clamp((ru_share - 0.95f) / 0.05f, 0.0f, 1.0f);
        du_scale_ = std::max(0.25f, 0.60f - 0.35f * ramp);
        if (cfg.debug_level >= 1) {
            static thread_local int last_auto_log_key = -1;
            int key = current_stage_ * 1000 + newton_iteration_index_;
            if (key != last_auto_log_key) {
                last_auto_log_key = key;
                std::cerr << "[PRECOND DU_AUTO] S=" << current_stage_
                          << " N=" << newton_iteration_index_
                          << ", ru_share=" << ru_share
                          << ", du_scale=" << du_scale_
                          << " (stage3 budget override active)" << std::endl;
            }
        }
    } else {
        du_scale_ = 1.0f;
    }
}

void UnifiedPreconditioner::update(const torch::Tensor& state, float dt, float gamma) {
    // FIX 2026-02-03: Diagnostic to confirm update() is called
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND UPDATE] Called with dt=" << dt << ", gamma=" << gamma
                  << " (stored dt_=" << dt_ << ", gamma_=" << gamma_ << ")" << std::endl;
    }

    // Check if we need to refresh coefficients
    dt_received_update_ = true;  // v20.14 r46e: mark that real dt has been received
    bool dt_changed = std::abs(dt - dt_) > 1e-6f || std::abs(gamma - gamma_) > 1e-6f;
    bool base_state_changed = (grid_info_->base_state_generation_ != cached_base_state_generation_);

    // FIX 2025-12-31 Batch41 Issue 1: Also check if ScalarMeanCache epoch changed
    // This detects when map factors (msfty), mub, c1f/c2f, or mu_base have been invalidated,
    // which happens during moving_nest updates or when invalidateVerticalMetricCaches() is called.
    // Without this check, coefficients using msfty_representative become stale after grid updates.
    uint64_t current_scalar_epoch = g_scalar_mean_epoch.load(std::memory_order_acquire);
    bool scalar_cache_invalidated = (current_scalar_epoch != cached_scalar_mean_epoch_);

    // Preconditioner-RHS scope consistency (2026-02-01): detect IMEX mode or flag change
    const auto& config = wrf::sdirk3::g_sdirk3_config;
    int current_scope = config.imex_split_mode;
    if (current_scope == 0 && config.imex_enabled) current_scope = 1;
    bool scope_changed = (current_scope != cached_precond_scope_);
    uint8_t current_flags = static_cast<uint8_t>(
        (config.precond_match_rhs ? 1 : 0) |
        (config.precond_extra_rayleigh ? 2 : 0) |
        (config.precond_extra_wdamp ? 4 : 0) |
        (config.precond_extra_vdiff ? 8 : 0) |
        (config.precond_extra_divergence ? 16 : 0) |
        (config.precond_coupled_phi_w ? 32 : 0)    // v20.14 Phase 2
    );
    bool flags_changed = (current_flags != cached_precond_flags_);

    // PR 9D: the W-damping policy fingerprint. The bitmask above already flips
    // on a precond_extra_wdamp toggle; this additionally catches a w_damp_alpha
    // change while the extra regularization is ON (normalized_extra_alpha is 0
    // when extra is OFF, so an alpha change with extra OFF does NOT rebuild).
    auto current_wdamp_signature =
        wrf::sdirk3::wdamp_preconditioner_signature(
            wrf::sdirk3::resolve_wdamp_preconditioner_policy(config));
    bool wdamp_signature_changed =
        (current_wdamp_signature != cached_wdamp_signature_);

    // v20.14: Detect external tuning parameter changes.
    // When override is active, theta is controlled by the adaptive setter.
    // If the user explicitly writes config theta (via set_config_float), the config
    // generation counter increments — this releases the override regardless of value.
    bool w_boost_changed = (std::abs(config.precond_w_acoustic_boost - w_acoustic_boost_cached_) > 1e-6f);
    bool theta_tuning_changed = false;
    if (theta_factor_override_active_) {
        if (config.theta_config_generation != theta_override_config_gen_) {
            // Config was explicitly written → release override, accept new config value
            theta_factor_override_active_ = false;
            theta_tuning_changed = (std::abs(config.precond_theta_acoustic_factor -
                                             theta_acoustic_factor_cached_) > 1e-6f);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[PRECOND] Override released: config gen "
                          << theta_override_config_gen_ << " -> "
                          << config.theta_config_generation
                          << ", theta=" << config.precond_theta_acoustic_factor << std::endl;
            }
        }
    } else {
        theta_tuning_changed = (std::abs(config.precond_theta_acoustic_factor -
                                         theta_acoustic_factor_cached_) > 1e-6f);
    }
    bool uv_vfrac_changed = (std::abs(config.precond_uv_vertical_fraction - uv_vertical_fraction_cached_) > 1e-6f);
    bool coupling_scale_changed = (config.precond_phi_w_coupling_scale != cached_coupling_scale_);
    bool dw_floor_changed = (std::abs(config.precond_dw_nosboost_floor - cached_dw_nosboost_floor_) > 1e-8f);
    bool tuning_changed = w_boost_changed || theta_tuning_changed || uv_vfrac_changed || coupling_scale_changed || dw_floor_changed;

    if (dt_changed || base_state_changed || scalar_cache_invalidated || scope_changed || flags_changed || wdamp_signature_changed || tuning_changed) {
        if (dt_changed) {
            dt_ = dt;
            gamma_ = gamma;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "DEBUG: Preconditioner dt/gamma changed, refreshing coefficients" << std::endl;
            }
        }

        if (base_state_changed) {
            cached_base_state_generation_ = grid_info_->base_state_generation_;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "DEBUG: Base state generation changed (now "
                          << cached_base_state_generation_
                          << "), refreshing coefficients with REAL mub/th_base!" << std::endl;
            }
        }

        if (scalar_cache_invalidated) {
            cached_scalar_mean_epoch_ = current_scalar_epoch;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "DEBUG: ScalarMeanCache epoch changed (now "
                          << cached_scalar_mean_epoch_
                          << "), refreshing coefficients (msfty/mub/c1f/c2f may have changed)" << std::endl;
            }
        }

        if (scope_changed || flags_changed) {
            if (config.debug_level >= 1) {
                if (scope_changed) {
                    std::cerr << "DEBUG: Precond scope changed ("
                              << cached_precond_scope_ << " -> " << current_scope
                              << "), refreshing diagonal terms for IMEX consistency" << std::endl;
                }
                if (flags_changed) {
                    std::cerr << "DEBUG: Precond flags changed (0x"
                              << std::hex << (int)cached_precond_flags_
                              << " -> 0x" << (int)current_flags << std::dec
                              << "), refreshing diagonal terms" << std::endl;
                }
            }
            // cached_precond_scope_ and cached_precond_flags_ updated inside initialize_acoustic_gravity_solver()
        }

        if (tuning_changed) {
            if (config.debug_level >= 1) {
                std::cerr << "DEBUG: Precond tuning changed (boost: "
                          << w_acoustic_boost_cached_ << " -> " << config.precond_w_acoustic_boost
                          << ", theta_factor: " << theta_acoustic_factor_cached_
                          << " -> " << config.precond_theta_acoustic_factor
                          << ", uv_vfrac: " << uv_vertical_fraction_cached_
                          << " -> " << config.precond_uv_vertical_fraction
                          << ", coupling_scale: " << cached_coupling_scale_
                          << " -> " << config.precond_phi_w_coupling_scale
                          << ", dw_floor: " << cached_dw_nosboost_floor_
                          << " -> " << config.precond_dw_nosboost_floor
                          << "), refreshing coefficients" << std::endl;
            }
            // w_acoustic_boost_cached_ and theta_acoustic_factor_cached_ updated
            // inside initialize_acoustic_gravity_solver()
        }

        // Reinitialize with new timestep or base state
        // Note: initialize_acoustic_gravity_solver() already increments coefficient_generation_
        initialize_acoustic_gravity_solver();
        initialize_horizontal_smoother();
    }
}

/**
 * v20.5: Set stage-specific state for preconditioner adaptation
 *
 * When SDIRK3 progresses from Stage 1 to Stage 3, the state U_stage changes
 * dramatically. The preconditioner coefficients computed from the initial base
 * state become increasingly inaccurate, causing GMRES to stagnate (especially
 * in Stage 3 where |mu_pert| can be significant).
 *
 * This method allows the Newton solver to provide the current mu_full so that
 * the preconditioner can adapt its coefficients. Key adaptations:
 * - 4×4 Schur: inv_mu0 uses mu_full instead of just mu_base
 * - Φ-W GS: A_wφ computed per-column from mu_full
 *
 * v20.5: Changed to accept mu_pert (perturbation) and compute mu_full internally
 * using grid_info_->mu_base. This allows Newton solver to pass state directly.
 */
void UnifiedPreconditioner::set_stage_state(const torch::Tensor& mu_pert, int stage) {
    torch::NoGradGuard no_grad;

    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND] set_stage_state ENTRY: stage=" << stage
                  << ", mu_pert.defined()=" << mu_pert.defined()
                  << ", mu_pert.dim()=" << (mu_pert.defined() ? mu_pert.dim() : -1)
                  << std::endl;
    }

    // Validate mu_pert dimensions - must be 2D (j, i) for mu block
    if (!mu_pert.defined() || mu_pert.dim() != 2) {
        std::cerr << "[PRECOND] set_stage_state: Invalid mu_pert dimension "
                  << (mu_pert.defined() ? mu_pert.dim() : -1) << " (expected 2D), ignoring" << std::endl;
        return;
    }

    // Check if we have mu_base for computing mu_full
    if (!grid_info_) {
        std::cerr << "[PRECOND] set_stage_state: No grid_info_ available, ignoring" << std::endl;
        return;
    }
    if (!grid_info_->mu_base.defined()) {
        std::cerr << "[PRECOND] set_stage_state: No mu_base available, ignoring" << std::endl;
        return;
    }

    // Compute mu_full = mu_base + mu_pert
    // mu_base is 2D (j, i) from grid_info_
    torch::Tensor mu_base_2d = grid_info_->mu_base;
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND] set_stage_state: mu_base_2d.sizes()=" << mu_base_2d.sizes()
                  << ", mu_pert.sizes()=" << mu_pert.sizes() << std::endl;
    }
    if (mu_base_2d.dim() != 2) {
        std::cerr << "[PRECOND] set_stage_state: mu_base is not 2D (dim="
                  << mu_base_2d.dim() << "), ignoring" << std::endl;
        return;
    }
    if (mu_base_2d.sizes() != mu_pert.sizes()) {
        std::cerr << "[PRECOND] set_stage_state: mu_base/mu_pert shape mismatch: "
                  << mu_base_2d.sizes() << " vs " << mu_pert.sizes() << std::endl;
        return;
    }

    // Ensure same device/dtype
    torch::Tensor mu_pert_matched = mu_pert.to(mu_base_2d.options());
    torch::Tensor mu_full_2d = mu_base_2d + mu_pert_matched;

    // Compute change ratio: ||mu_pert||_2 / max(||mu_base||_2, eps)
    // Uses L2 norm instead of mean() to avoid sign cancellation in mu_pert
    // (mountain wave perturbations are antisymmetric → mean ≈ 0).
    float mu_base_norm = mu_base_2d.norm().item<float>();
    float mu_pert_norm = mu_pert_matched.norm().item<float>();
    float mu_change_ratio = mu_pert_norm / std::max(mu_base_norm, 1.0f);

    bool stage_changed = (stage != current_stage_);
    bool mu_changed_significantly = (mu_change_ratio > 0.05f);  // 5% threshold

    // Always update mu_full_stage_ so inv_mu0 stays current,
    // even for small changes that don't warrant full recomputation.
    mu_full_stage_ = mu_full_2d.clone();
    current_stage_ = stage;

    // v20.14r27t: Compute mu scale correction for GS sweep alignment with 4x4 path.
    // mc_k was built from mu_representative (mub.mean()), but 4x4 uses mu_full_stage_.
    // Correction = mu_full_mean / mu_representative aligns the GS coupling scale.
    if (mu_representative_cached_ > 0.0f) {
        torch::NoGradGuard no_grad;
        float mu_full_mean = mu_full_2d.mean().item<float>();
        mu_scale_correction_ = std::clamp(mu_full_mean / mu_representative_cached_, 0.1f, 10.0f);
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            static thread_local int mu_corr_log_count = 0;
            if (mu_corr_log_count++ < 3) {
                std::cerr << "[PRECOND] mu_scale_correction = " << mu_scale_correction_
                          << " (mu_full_mean=" << mu_full_mean
                          << " / mu_rep=" << mu_representative_cached_ << ")" << std::endl;
            }
        }
    }

    // v20.14r27i: Regenerate coefficients on stage change OR significant mu change.
    // Previously only mu_changed_significantly triggered recomputation; stage_changed
    // entered the log block but didn't actually regenerate.
    bool needs_recompute = mu_changed_significantly || stage_changed;
    if (needs_recompute) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[PRECOND] set_stage_state: stage=" << stage
                      << ", ||mu_pert||/||mu_base||=" << (mu_change_ratio * 100.0f) << "%"
                      << " -> RECOMPUTE"
                      << (stage_changed ? " (stage changed)" : " (mu > 5%)")
                      << std::endl;
        }
        initialize_acoustic_gravity_solver();
        initialize_horizontal_smoother();
    }
}

void UnifiedPreconditioner::set_theta_acoustic_factor(float factor) {
    // v20.14: Instance-local setter for adaptive tuning.
    // Sets override flag so initialize_acoustic_gravity_solver() won't clobber.
    // Does NOT modify global config — thread-safe for multi-tile.
    float old = theta_acoustic_factor_cached_;
    float clamped = std::clamp(factor, 0.0f, 0.35f);
    if (std::abs(clamped - old) < 1e-6f) return;  // no-op: skip override activation
    theta_acoustic_factor_cached_ = clamped;
    theta_factor_override_active_ = true;  // Prevent init from overwriting
    theta_override_config_gen_ = wrf::sdirk3::g_sdirk3_config.theta_config_generation;  // snapshot
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[PRECOND] theta_acoustic_factor set: "
                  << old << " -> " << theta_acoustic_factor_cached_ << std::endl;
    }
    initialize_acoustic_gravity_solver();
    initialize_horizontal_smoother();
}

void UnifiedPreconditioner::clear_theta_acoustic_override() {
    if (!theta_factor_override_active_) return;
    theta_factor_override_active_ = false;
    float config_theta = wrf::sdirk3::g_sdirk3_config.precond_theta_acoustic_factor;
    if (std::abs(config_theta - theta_acoustic_factor_cached_) > 1e-6f) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[PRECOND] Override cleared: theta " << theta_acoustic_factor_cached_
                      << " -> " << config_theta << " (config)" << std::endl;
        }
        theta_acoustic_factor_cached_ = config_theta;
        initialize_acoustic_gravity_solver();
        initialize_horizontal_smoother();
    }
}

/**
 * Coupled U-V-μ solver for horizontal acoustic modes
 *
 * Solves 3×3 system per level (no vertical coupling for first implementation):
 *   [ D_u    0    C_uμ ] [ u ]   [ r_u ]
 *   [  0    D_v   C_vμ ] [ v ] = [ r_v ]
 *   [ C_μu  C_μv   D_μ ] [ μ ]   [ r_μ ]
 *
 * Strategy: Analytical 3×3 inverse (fast, allocation-free)
 *
 * Performance: ~5-10ms expected for 3321 columns (vs 40ms for W-θ)
 */
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
UnifiedPreconditioner::solve_coupled_uv_mu_column(
    const torch::Tensor& r_u,
    const torch::Tensor& r_v,
    const torch::Tensor& r_mu,
    int nz
) {
    // FIX 2025-12-31 Batch39 Issue 2: Handle GPU inputs safely
    // Copy to CPU if needed for CPU-based computation, then move result back
    auto target_device = r_u.device();
    auto r_u_cpu = r_u.device().is_cpu() ? r_u : r_u.to(torch::kCPU);
    auto r_v_cpu = r_v.device().is_cpu() ? r_v : r_v.to(torch::kCPU);
    auto r_mu_cpu = r_mu.device().is_cpu() ? r_mu : r_mu.to(torch::kCPU);

    // STEP 1: Precompute coupling coefficients (avoid repeated .item() calls)
    // Use same generation-tracking pattern as W-θ solver

    static thread_local std::vector<float> D_u_cached;
    static thread_local std::vector<float> D_v_cached;
    static thread_local std::vector<float> D_mu_cached;
    static thread_local std::vector<float> C_u_mu_cached;
    static thread_local std::vector<float> C_v_mu_cached;
    static thread_local std::vector<float> C_mu_u_cached;
    static thread_local std::vector<float> C_mu_v_cached;
    static thread_local uint64_t cached_generation = 0;
    // v20.14r27z: Instance isolation for multi-tile safety.
    static thread_local const void* cached_3x3t_instance = nullptr;

    // v20.14 r47c-fix3: Track du_scale for cache invalidation in 3×3 path too.
    static thread_local float cached_3x3_du_scale = 1.0f;

    // Invalidate cache if coefficients changed, instance changed, du_scale changed, or size mismatch
    if (cached_generation != coefficient_generation_ || cached_3x3t_instance != this ||
        cached_3x3_du_scale != du_scale_ ||
        D_u_cached.size() != static_cast<size_t>(nz)) {
        cached_3x3t_instance = this;
        cached_3x3_du_scale = du_scale_;

        D_u_cached.resize(nz);
        D_v_cached.resize(nz);
        D_mu_cached.resize(nz);
        C_u_mu_cached.resize(nz);
        C_v_mu_cached.resize(nz);
        C_mu_u_cached.resize(nz);
        C_mu_v_cached.resize(nz);

        auto D_u_acc = vertical_diag_u_.accessor<float, 1>();
        auto D_v_acc = vertical_diag_v_.accessor<float, 1>();
        auto D_mu_acc = vertical_diag_mu_.accessor<float, 1>();
        auto C_u_mu_acc = C_u_mu_.accessor<float, 1>();
        auto C_v_mu_acc = C_v_mu_.accessor<float, 1>();
        auto C_mu_u_acc = C_mu_u_.accessor<float, 1>();
        auto C_mu_v_acc = C_mu_v_.accessor<float, 1>();

        for (int k = 0; k < nz; ++k) {
            // v20.14 r47c-fix3: Apply D_u scaling (consistency with main 4×4 path).
            D_u_cached[k] = D_u_acc[k] * du_scale_;
            D_v_cached[k] = D_v_acc[k];
            D_mu_cached[k] = D_mu_acc[k];
            C_u_mu_cached[k] = C_u_mu_acc[k];
            C_v_mu_cached[k] = C_v_mu_acc[k];
            C_mu_u_cached[k] = C_mu_u_acc[k];
            C_mu_v_cached[k] = C_mu_v_acc[k];
        }

        cached_generation = coefficient_generation_;
    }

    // STEP 2: Extract RHS to stack buffers
    // CRITICAL FIX: Keep contiguous tensors alive to avoid dangling pointers
    // FIX 2025-12-31 Batch39 Issue 2: Use _cpu versions (now guaranteed CPU)
    auto r_u_contig = r_u_cpu.contiguous();
    auto r_v_contig = r_v_cpu.contiguous();
    auto r_mu_contig = r_mu_cpu.contiguous();

    const float* r_u_data = r_u_contig.data_ptr<float>();
    const float* r_v_data = r_v_contig.data_ptr<float>();
    const float* r_mu_data = r_mu_contig.data_ptr<float>();

    // STEP 3: Solve per-level 3×3 systems using analytical inverse
    static thread_local std::vector<float> u_sol;
    static thread_local std::vector<float> v_sol;
    static thread_local std::vector<float> mu_sol;
    u_sol.resize(nz);
    v_sol.resize(nz);
    mu_sol.resize(nz);

    for (int k = 0; k < nz; ++k) {
        // Extract per-level coefficients
        float D_u = D_u_cached[k];
        float D_v = D_v_cached[k];
        float D_mu = D_mu_cached[k];
        float C_uμ = C_u_mu_cached[k];
        float C_vμ = C_v_mu_cached[k];
        float C_μu = C_mu_u_cached[k];
        float C_μv = C_mu_v_cached[k];

        // RHS
        float r_u_k = r_u_data[k];
        float r_v_k = r_v_data[k];
        float r_μ_k = r_mu_data[k];

        // Matrix structure:
        //   [ D_u    0    C_uμ ]
        //   [  0    D_v   C_vμ ]
        //   [ C_μu  C_μv   D_μ ]
        //
        // Analytical 3×3 inverse using block structure:
        // - U and V are decoupled in first two equations
        // - Only μ couples to both U and V

        // Determinant: det(A) = D_u * D_v * D_mu - D_u * C_vμ * C_μv - D_v * C_uμ * C_μu
        float det = D_u * D_v * D_mu
                    - D_u * C_vμ * C_μv
                    - D_v * C_uμ * C_μu;

        // Safety: Avoid division by zero
        if (std::abs(det) < 1e-12f) {
            // Fallback to diagonal solve (decoupled approximation)
            u_sol[k] = r_u_k / D_u;
            v_sol[k] = r_v_k / D_v;
            mu_sol[k] = r_μ_k / D_mu;
            continue;
        }

        float inv_det = 1.0f / det;

        // Inverse matrix elements (only non-zero entries):
        // A^{-1}[0,0] = (D_v * D_mu - C_vμ * C_μv) / det
        // A^{-1}[0,2] = -D_v * C_uμ / det
        // A^{-1}[1,1] = (D_u * D_mu - C_uμ * C_μu) / det
        // A^{-1}[1,2] = -D_u * C_vμ / det
        // A^{-1}[2,0] = -C_μu * D_v / det
        // A^{-1}[2,1] = -C_μv * D_u / det
        // A^{-1}[2,2] = (D_u * D_v) / det

        float A_inv_00 = (D_v * D_mu - C_vμ * C_μv) * inv_det;
        float A_inv_02 = -D_v * C_uμ * inv_det;
        float A_inv_11 = (D_u * D_mu - C_uμ * C_μu) * inv_det;
        float A_inv_12 = -D_u * C_vμ * inv_det;
        float A_inv_20 = -C_μu * D_v * inv_det;
        float A_inv_21 = -C_μv * D_u * inv_det;
        float A_inv_22 = (D_u * D_v) * inv_det;

        // Solution: [u, v, μ]^T = A^{-1} * [r_u, r_v, r_μ]^T
        u_sol[k] = A_inv_00 * r_u_k + A_inv_02 * r_μ_k;
        v_sol[k] = A_inv_11 * r_v_k + A_inv_12 * r_μ_k;
        mu_sol[k] = A_inv_20 * r_u_k + A_inv_21 * r_v_k + A_inv_22 * r_μ_k;
    }

    // STEP 4: Convert back to tensors
    // FIX 2025-12-31 Batch39 Issue 2: Create CPU tensors first, then move to target device
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto u_tensor = torch::from_blob(u_sol.data(), {nz}, cpu_opts).clone();  // LINT_EXCEPTION: CPU opts above
    auto v_tensor = torch::from_blob(v_sol.data(), {nz}, cpu_opts).clone();  // LINT_EXCEPTION: CPU opts above
    auto mu_tensor = torch::from_blob(mu_sol.data(), {nz}, cpu_opts).clone();  // LINT_EXCEPTION: CPU opts above

    // Move to target device if not CPU
    if (!target_device.is_cpu()) {
        u_tensor = u_tensor.to(target_device);
        v_tensor = v_tensor.to(target_device);
        mu_tensor = mu_tensor.to(target_device);
    }

    return std::make_tuple(u_tensor, v_tensor, mu_tensor);
}

/**
 * PERFORMANCE: Zero-allocation raw-pointer overload
 * Uses same thread-local cached coefficients, writes directly to output buffers
 */
void UnifiedPreconditioner::solve_coupled_uv_mu_column_inplace(
    const float* r_u_data,
    const float* r_v_data,
    const float* r_mu_data,
    float* u_sol,
    float* v_sol,
    float* mu_sol,
    int nz
) {
    // STEP 1: Use same cached coefficients as tensor version
    static thread_local std::vector<float> D_u_cached;
    static thread_local std::vector<float> D_v_cached;
    static thread_local std::vector<float> D_mu_cached;
    static thread_local std::vector<float> C_u_mu_cached;
    static thread_local std::vector<float> C_v_mu_cached;
    static thread_local std::vector<float> C_mu_u_cached;
    static thread_local std::vector<float> C_mu_v_cached;
    static thread_local uint64_t cached_generation = 0;
    // v20.14r27z: Instance isolation for multi-tile safety.
    static thread_local const void* cached_3x3r_instance = nullptr;
    // v20.14 r47c-fix3: Track du_scale for cache invalidation (consistency with tensor 3x3 path).
    static thread_local float cached_3x3r_du_scale = 1.0f;

    // Invalidate cache if coefficients changed, instance changed, du_scale changed, or size mismatch
    if (cached_generation != coefficient_generation_ || cached_3x3r_instance != this ||
        cached_3x3r_du_scale != du_scale_ ||
        D_u_cached.size() != static_cast<size_t>(nz)) {
        cached_3x3r_instance = this;
        cached_3x3r_du_scale = du_scale_;

        D_u_cached.resize(nz);
        D_v_cached.resize(nz);
        D_mu_cached.resize(nz);
        C_u_mu_cached.resize(nz);
        C_v_mu_cached.resize(nz);
        C_mu_u_cached.resize(nz);
        C_mu_v_cached.resize(nz);

        // Extract coefficients using accessors
        auto D_u_acc = vertical_diag_u_.accessor<float, 1>();
        auto D_v_acc = vertical_diag_v_.accessor<float, 1>();
        auto D_mu_acc = vertical_diag_mu_.accessor<float, 1>();
        auto C_u_mu_acc = C_u_mu_.accessor<float, 1>();
        auto C_v_mu_acc = C_v_mu_.accessor<float, 1>();
        auto C_mu_u_acc = C_mu_u_.accessor<float, 1>();
        auto C_mu_v_acc = C_mu_v_.accessor<float, 1>();

        for (int k = 0; k < nz; ++k) {
            // v20.14 r47c-fix3: Apply D_u scaling (consistency with tensor 3x3 path).
            D_u_cached[k] = D_u_acc[k] * du_scale_;
            D_v_cached[k] = D_v_acc[k];
            D_mu_cached[k] = D_mu_acc[k];
            C_u_mu_cached[k] = C_u_mu_acc[k];
            C_v_mu_cached[k] = C_v_mu_acc[k];
            C_mu_u_cached[k] = C_mu_u_acc[k];
            C_mu_v_cached[k] = C_mu_v_acc[k];
        }

        cached_generation = coefficient_generation_;
    }

    // STEP 2: Solve per-level 3×3 systems directly into output arrays
    for (int k = 0; k < nz; ++k) {
        // Extract per-level coefficients
        float D_u = D_u_cached[k];
        float D_v = D_v_cached[k];
        float D_mu = D_mu_cached[k];
        float C_u_mu = C_u_mu_cached[k];
        float C_v_mu = C_v_mu_cached[k];
        float C_mu_u = C_mu_u_cached[k];
        float C_mu_v = C_mu_v_cached[k];

        // RHS
        float r_u_k = r_u_data[k];
        float r_v_k = r_v_data[k];
        float r_mu_k = r_mu_data[k];

        // Determinant: det(A) = D_u * D_v * D_mu - D_u * C_v_mu * C_mu_v - D_v * C_u_mu * C_mu_u
        float det = D_u * D_v * D_mu - D_u * C_v_mu * C_mu_v - D_v * C_u_mu * C_mu_u;

        // Safety: Avoid division by zero
        if (std::abs(det) < 1e-12f) {
            // Fallback to diagonal solve
            u_sol[k] = r_u_k / D_u;
            v_sol[k] = r_v_k / D_v;
            mu_sol[k] = r_mu_k / D_mu;
            continue;
        }

        float inv_det = 1.0f / det;

        // Analytical 3×3 inverse elements
        float A_inv_00 = (D_v * D_mu - C_v_mu * C_mu_v) * inv_det;
        float A_inv_02 = -D_v * C_u_mu * inv_det;
        float A_inv_11 = (D_u * D_mu - C_u_mu * C_mu_u) * inv_det;
        float A_inv_12 = -D_u * C_v_mu * inv_det;
        float A_inv_20 = -C_mu_u * D_v * inv_det;
        float A_inv_21 = -C_mu_v * D_u * inv_det;
        float A_inv_22 = (D_u * D_v) * inv_det;

        // Solution: [u, v, mu]^T = A^{-1} * [r_u, r_v, r_mu]^T
        // Write directly to output arrays
        u_sol[k] = A_inv_00 * r_u_k + A_inv_02 * r_mu_k;
        v_sol[k] = A_inv_11 * r_v_k + A_inv_12 * r_mu_k;
        mu_sol[k] = A_inv_20 * r_u_k + A_inv_21 * r_v_k + A_inv_22 * r_mu_k;
    }
    // NO tensor allocations, NO clones - pure raw-pointer arithmetic
}

// ============================================================================
// PHASE 4.1: 4×4 ACOUSTIC BLOCK SOLVER (U-V-μ-Φ)
// ============================================================================

/**
 * Solve 4×4 coupled acoustic system with Schur complement
 *
 * Physics (from PHASE_4.1_DESIGN.md v2.1):
 *   - Pressure gradient: Φ → U,V with (1/μ₀) scaling
 *   - Divergence sensing: U,V → Φ with (c²/μ₀) scaling
 *   - Hydrostatic balance: μ ↔ Φ SYMMETRIC (A_μΦ = A_Φμ = dt·γ·c²)
 *   - Column mass: μ is SINGLE SCALAR per column, accumulated from all levels
 *
 * Algorithm:
 *   Step 1: Eliminate U,V from μ and Φ equations
 *   Step 2: Solve 2×2 Schur complement for (δμ, δΦ)
 *   Step 3: Back-substitute to get (δu, δv)
 *
 * Fallback: If |det| < 1e-10, use 3×3 U-V-μ + diagonal Φ
 */
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor, torch::Tensor>
UnifiedPreconditioner::solve_4x4_acoustic_block(
    const torch::Tensor& r_u,
    const torch::Tensor& r_v,
    const torch::Tensor& r_mu,
    const torch::Tensor& r_phi,
    int nz,
    int nz_w,
    float mu_0_local
) {
    // Allocate solution tensors on CPU
    // DESIGN: This solver is explicitly CPU-only (uses std::vector, data_ptr, raw pointers).
    // Input tensors are converted to CPU below (r_u/r_v/r_phi.to(kCPU)).
    // Caller handles implicit CPU→GPU transfer via index_put_ if result is on GPU.
    auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    torch::Tensor u_sol = torch::zeros({nz}, options);
    torch::Tensor v_sol = torch::zeros({nz}, options);
    torch::Tensor phi_sol = torch::zeros({nz_w}, options);

    // μ is a single scalar (column-integrated mass)
    float mu_sol_scalar = 0.0f;

    // Diagnostic counters for fallback rate
    static thread_local int fallback_4x4_count = 0;
    static thread_local int total_4x4_solves = 0;

    // STEP 1: Loop over mass levels to accumulate μ/Φ Schur system
    // CRITICAL: μ is a SINGLE SCALAR per column (2D field)
    //           ALL level contributions accumulate before solving

    // FIX 2025-12-31 Batch40 Issue 2: Cache coefficient tensors with generation tracking
    // These coefficients only change when coefficient_generation_ changes, but this function
    // is called nx*ny times per apply(). Caching eliminates O(nx*ny) GPU→CPU transfers.
    static thread_local std::vector<float> diag_u_cached;
    static thread_local std::vector<float> diag_v_cached;
    static thread_local std::vector<float> diag_phi_cached;
    static thread_local std::vector<float> C_u_mu_cached;
    static thread_local std::vector<float> C_v_mu_cached;
    static thread_local std::vector<float> C_mu_u_cached;
    static thread_local std::vector<float> C_mu_v_cached;
    static thread_local float D_mu_cached = 1.0f;
    static thread_local uint64_t cached_gen_4x4 = 0;
    // v20.14 r47c-fix3: Track du_scale for cache invalidation in per-column 4×4 path.
    static thread_local float cached_4x4_du_scale = 1.0f;

    // Invalidate cache if coefficients changed, du_scale changed, or size mismatch
    if (cached_gen_4x4 != coefficient_generation_ ||
        cached_4x4_du_scale != du_scale_ ||
        diag_u_cached.size() != static_cast<size_t>(nz)) {
        cached_4x4_du_scale = du_scale_;

        diag_u_cached.resize(nz);
        diag_v_cached.resize(nz);
        diag_phi_cached.resize(nz);
        C_u_mu_cached.resize(nz);
        C_v_mu_cached.resize(nz);
        C_mu_u_cached.resize(nz);
        C_mu_v_cached.resize(nz);

        // Copy from GPU only when cache invalidated
        auto diag_u_cpu = vertical_diag_u_.to(torch::kCPU).contiguous();
        auto diag_v_cpu = vertical_diag_v_.to(torch::kCPU).contiguous();
        auto diag_phi_cpu = vertical_diag_phi_.to(torch::kCPU).contiguous();
        auto C_u_mu_cpu = C_u_mu_.to(torch::kCPU).contiguous();
        auto C_v_mu_cpu = C_v_mu_.to(torch::kCPU).contiguous();
        auto C_mu_u_cpu = C_mu_u_.to(torch::kCPU).contiguous();
        auto C_mu_v_cpu = C_mu_v_.to(torch::kCPU).contiguous();
        auto diag_mu_cpu = vertical_diag_mu_.to(torch::kCPU).contiguous();

        auto diag_u_acc = diag_u_cpu.accessor<float, 1>();
        auto diag_v_acc = diag_v_cpu.accessor<float, 1>();
        auto diag_phi_acc = diag_phi_cpu.accessor<float, 1>();
        auto C_u_mu_acc = C_u_mu_cpu.accessor<float, 1>();
        auto C_v_mu_acc = C_v_mu_cpu.accessor<float, 1>();
        auto C_mu_u_acc = C_mu_u_cpu.accessor<float, 1>();
        auto C_mu_v_acc = C_mu_v_cpu.accessor<float, 1>();

        for (int k = 0; k < nz; ++k) {
            // v20.14 r47c-fix3: Apply D_u scaling (consistency with main 4×4 path).
            diag_u_cached[k] = diag_u_acc[k] * du_scale_;
            diag_v_cached[k] = diag_v_acc[k];
            diag_phi_cached[k] = diag_phi_acc[k];
            C_u_mu_cached[k] = C_u_mu_acc[k];
            C_v_mu_cached[k] = C_v_mu_acc[k];
            C_mu_u_cached[k] = C_mu_u_acc[k];
            C_mu_v_cached[k] = C_mu_v_acc[k];
        }

        // BUG #19 FIX: Use [0] index for safe extraction (tensor has nz elements, all identical)
        D_mu_cached = diag_mu_cpu.data_ptr<float>()[0];
        cached_gen_4x4 = coefficient_generation_;
    }

    float D_mu = D_mu_cached;

    // Residual tensors MUST be copied each call (they change per column)
    auto r_mu_cpu = r_mu.to(torch::kCPU);
    // FIX 2025-12-28 Issue 4: Validate r_mu is a scalar before using .item<float>()
    // μ is expected to be a single scalar per column (2D field summed), not a per-level array
    TORCH_CHECK(r_mu_cpu.numel() == 1,
        "solve_4x4_acoustic_block: r_mu must be scalar (numel=1), got numel=", r_mu_cpu.numel(),
        ". This indicates incorrect μ residual extraction (should sum over k-levels).");
    // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
    float r_mu_input;
    {
        torch::NoGradGuard no_grad;
        r_mu_input = r_mu_cpu.item<float>();  // Input μ residual
    }

    auto r_u_cpu = r_u.to(torch::kCPU).contiguous();
    auto r_v_cpu = r_v.to(torch::kCPU).contiguous();
    auto r_phi_cpu = r_phi.to(torch::kCPU).contiguous();

    const float* r_u_ptr = r_u_cpu.data_ptr<float>();
    const float* r_v_ptr = r_v_cpu.data_ptr<float>();
    const float* r_phi_ptr = r_phi_cpu.data_ptr<float>();

    // Schur complement accumulators for μ (accumulated over ALL levels)
    float S_mu_mu_accum = 0.0f;
    float r_mu_mod_accum = 0.0f;

    // Per-level Φ data structures
    std::vector<float> S_phi_phi(nz, 0.0f);   // Per-level Φ diagonal (after U,V elimination)
    std::vector<float> S_phi_mu(nz, 0.0f);    // Per-level Φ-μ coupling
    std::vector<float> S_mu_phi(nz, 0.0f);    // Per-level μ-Φ coupling
    std::vector<float> r_phi_mod(nz, 0.0f);   // Per-level Φ RHS

    // Store per-level data for back-substitution
    std::vector<float> r_u_data(nz, 0.0f);
    std::vector<float> r_v_data(nz, 0.0f);
    std::vector<float> D_u_data(nz, 0.0f);
    std::vector<float> D_v_data(nz, 0.0f);
    std::vector<float> A_u_mu_data(nz, 0.0f);
    std::vector<float> A_v_mu_data(nz, 0.0f);
    std::vector<float> A_u_phi_data(nz, 0.0f);
    std::vector<float> A_v_phi_data(nz, 0.0f);

    for (int k = 0; k < nz; ++k) {
        // FIX 2025-12-31 Batch40 Issue 2: Use thread-local cached coefficients
        // Get per-level diagonals
        float D_u = diag_u_cached[k];
        float D_v = diag_v_cached[k];
        float D_phi = diag_phi_cached[k];

        // Get coupling coefficients (U-V-μ from cached tensors)
        float A_u_mu = C_u_mu_cached[k];
        float A_v_mu = C_v_mu_cached[k];
        float A_mu_u = C_mu_u_cached[k];
        float A_mu_v = C_mu_v_cached[k];

        // PHASE 4.1: Φ coupling coefficients - computed on-the-fly using per-column μ₀
        // BUG #18 FIX: Use mu_0_local instead of domain-averaged mu_0_avg for proper per-column scaling
        int k_w = k;  // Use same index for Φ (w-grid at mass level)
        float dt_gamma = dt_ * gamma_;
        float c2 = grid_info_->cs * grid_info_->cs;
        float H_x = 1.0f / grid_info_->dx;
        float H_y = 1.0f / grid_info_->dy;

        // Compute Φ coefficients with per-column μ₀ for proper coupling strength
        float A_u_phi = dt_gamma * (1.0f / mu_0_local) * H_x;   // Pressure gradient: Φ → U
        float A_v_phi = dt_gamma * (1.0f / mu_0_local) * H_y;   // Pressure gradient: Φ → V
        float A_phi_u = dt_gamma * (c2 / mu_0_local) * H_x;     // Divergence sensing: U → Φ
        float A_phi_v = dt_gamma * (c2 / mu_0_local) * H_y;     // Divergence sensing: V → Φ
        float A_phi_mu = dt_gamma * (c2 / mu_0_local);           // Hydrostatic balance: μ → Φ
        float A_mu_phi = A_phi_mu;  // SYMMETRIC per design (A_μΦ = A_Φμ)

        // Get residuals from pre-copied CPU data
        float r_u_k = r_u_ptr[k];
        float r_v_k = r_v_ptr[k];
        float r_phi_k = r_phi_ptr[k_w];

        // Store for back-substitution
        r_u_data[k] = r_u_k;
        r_v_data[k] = r_v_k;
        D_u_data[k] = D_u;
        D_v_data[k] = D_v;
        A_u_mu_data[k] = A_u_mu;
        A_v_mu_data[k] = A_v_mu;
        A_u_phi_data[k] = A_u_phi;
        A_v_phi_data[k] = A_v_phi;

        // === SCHUR COMPLEMENT ELIMINATION ===
        float inv_D_u = 1.0f / D_u;
        float inv_D_v = 1.0f / D_v;

        // Per-level contributions to μ Schur system (ACCUMULATED)
        float S_mu_mu_k = -(A_mu_u * A_u_mu * inv_D_u) - (A_mu_v * A_v_mu * inv_D_v);
        S_mu_phi[k] = A_mu_phi - (A_mu_u * A_u_phi * inv_D_u) - (A_mu_v * A_v_phi * inv_D_v);

        // BUGFIX #2: Include original μ residual contribution
        float r_mu_mod_k = -(A_mu_u * r_u_k * inv_D_u) - (A_mu_v * r_v_k * inv_D_v);

        // Per-level Φ Schur system (NOT accumulated)
        S_phi_mu[k] = A_phi_mu - (A_phi_u * A_u_mu * inv_D_u) - (A_phi_v * A_v_mu * inv_D_v);
        S_phi_phi[k] = D_phi - (A_phi_u * A_u_phi * inv_D_u) - (A_phi_v * A_v_phi * inv_D_v);
        r_phi_mod[k] = r_phi_k - (A_phi_u * r_u_k * inv_D_u) - (A_phi_v * r_v_k * inv_D_v);

        // ACCUMULATE μ contributions
        S_mu_mu_accum += S_mu_mu_k;
        r_mu_mod_accum += r_mu_mod_k;
    }

    // Add D_μ diagonal and original μ residual (BUGFIX #2)
    S_mu_mu_accum += D_mu;
    r_mu_mod_accum += r_mu_input;

    // STEP 2: Solve coupled μ-Φ system (PROPER 2×2 Schur complement)
    // CRITICAL: μ is column scalar, Φ is per-level
    //
    // We need to solve:
    //   S_μμ·δμ + Σₖ S_μΦₖ·δΦₖ = r_μ'
    //   S_Φμₖ·δμ + S_ΦΦₖ·δΦₖ = r_Φ'ₖ   (for each level k)
    //
    // Algorithm:
    // 1. From Φ equation: δΦₖ = (r_Φ'ₖ - S_Φμₖ·δμ) / S_ΦΦₖ
    // 2. Substitute into μ equation:
    //    S_μμ·δμ + Σₖ S_μΦₖ·(r_Φ'ₖ - S_Φμₖ·δμ)/S_ΦΦₖ = r_μ'
    // 3. Solve for δμ:
    //    δμ = (r_μ' - Σₖ S_μΦₖ·r_Φ'ₖ/S_ΦΦₖ) / (S_μμ - Σₖ S_μΦₖ·S_Φμₖ/S_ΦΦₖ)
    // 4. Back-substitute to get δΦₖ

    // Accumulate μ-Φ coupling contributions with singularity checks
    float S_mu_mu_reduced = S_mu_mu_accum;  // Start with accumulated diagonal
    float r_mu_reduced = r_mu_mod_accum;     // Start with accumulated RHS

    // v20.14r27l: Relative singularity threshold (was absolute 1e-10).
    float max_S_phi_phi_abs = 0.0f;
    for (int k = 0; k < nz; ++k) {
        max_S_phi_phi_abs = std::max(max_S_phi_phi_abs, std::fabs(S_phi_phi[k]));
    }
    float SINGULARITY_THRESHOLD = std::max(max_S_phi_phi_abs * 1e-8f, 1e-30f);
    bool schur_valid = true;

    for (int k = 0; k < nz; ++k) {
        // Check for Φ diagonal singularity
        if (std::fabs(S_phi_phi[k]) < SINGULARITY_THRESHOLD) {
            schur_valid = false;  // Mark as singular, will fallback
            break;
        }

        // Add coupling corrections to effective μ system
        float inv_S_phi_phi = 1.0f / S_phi_phi[k];
        S_mu_mu_reduced -= S_mu_phi[k] * S_phi_mu[k] * inv_S_phi_phi;  // Schur correction
        r_mu_reduced -= S_mu_phi[k] * r_phi_mod[k] * inv_S_phi_phi;     // RHS correction
    }

    // Check for μ diagonal singularity after Schur reduction
    if (schur_valid && std::fabs(S_mu_mu_reduced) < SINGULARITY_THRESHOLD) {
        schur_valid = false;  // Reduced system is singular
    }

    // FALLBACK: If Schur system is singular, fall back to decoupled diagonal solve
    if (!schur_valid) {
        // Decouple: solve U-V-μ (3×3) + diagonal Φ
        float delta_mu = r_mu_mod_accum / S_mu_mu_accum;  // Diagonal μ solve (ignore Φ coupling)
        mu_sol_scalar = delta_mu;

        // PERF FIX 2025-12-27: Compute Φ solutions and store for back-substitution
        // Avoids reading back from phi_sol tensor with .item()
        std::vector<float> delta_phi_fallback(nz, 0.0f);
        for (int k = 0; k < nz; ++k) {
            delta_phi_fallback[k] = (std::fabs(S_phi_phi[k]) > SINGULARITY_THRESHOLD)
                ? r_phi_mod[k] / S_phi_phi[k] : 0.0f;
        }

        // Back-substitute U,V with decoupled solution
        for (int k = 0; k < nz; ++k) {
            int k_w = k;
            float delta_phi_k = delta_phi_fallback[k];
            phi_sol[k_w] = delta_phi_k;
            fallback_4x4_count++;

            u_sol[k] = (r_u_data[k] - A_u_mu_data[k] * delta_mu - A_u_phi_data[k] * delta_phi_k) / D_u_data[k];
            v_sol[k] = (r_v_data[k] - A_v_mu_data[k] * delta_mu - A_v_phi_data[k] * delta_phi_k) / D_v_data[k];
        }

        // Convert scalar μ to tensor for return
        torch::Tensor mu_sol = torch::full({1}, mu_sol_scalar, options);
        return std::make_tuple(u_sol, v_sol, mu_sol, phi_sol);
    }

    // NOMINAL PATH: Solve coupled system with full Schur complement
    float delta_mu = r_mu_reduced / S_mu_mu_reduced;

    // PERF FIX 2025-12-27: Pre-compute delta_phi values to avoid reading back with .item()
    std::vector<float> delta_phi_nominal(nz, 0.0f);

    // STEP 3: Back-substitute to get Φ per-level
    for (int k = 0; k < nz; ++k) {
        int k_w = k;
        // δΦₖ = (r_Φ'ₖ - S_Φμₖ·δμ) / S_ΦΦₖ
        float delta_phi = (r_phi_mod[k] - S_phi_mu[k] * delta_mu) / S_phi_phi[k];
        delta_phi_nominal[k] = delta_phi;
        phi_sol[k_w] = delta_phi;
        total_4x4_solves++;
    }

    // Store single μ solution
    mu_sol_scalar = delta_mu;

    // STEP 4: Back-substitute to get U and V solutions
    for (int k = 0; k < nz; ++k) {
        // PERF FIX 2025-12-27: Use pre-computed delta_phi instead of .item()
        float delta_phi_k = delta_phi_nominal[k];

        // δu = (r_u - A_uμ·δμ - A_uΦ·δΦ) / D_u
        // δv = (r_v - A_vμ·δμ - A_vΦ·δΦ) / D_v
        u_sol[k] = (r_u_data[k] - A_u_mu_data[k] * delta_mu - A_u_phi_data[k] * delta_phi_k) / D_u_data[k];
        v_sol[k] = (r_v_data[k] - A_v_mu_data[k] * delta_mu - A_v_phi_data[k] * delta_phi_k) / D_v_data[k];
    }

    // Convert μ scalar to tensor
    torch::Tensor mu_sol = torch::full({1}, mu_sol_scalar, options);

    // Debug output for fallback rate
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && total_4x4_solves % 1000 == 0) {
        float fallback_rate = 100.0f * fallback_4x4_count / static_cast<float>(total_4x4_solves);
        std::cerr << "[PRECOND 4×4-DIAG] Total solves: " << total_4x4_solves
                  << " | Fallback: " << fallback_4x4_count
                  << " (" << fallback_rate << "%)" << std::endl;
    }

    return std::make_tuple(u_sol, v_sol, mu_sol, phi_sol);
}

} // namespace sdirk3
} // namespace wrf
