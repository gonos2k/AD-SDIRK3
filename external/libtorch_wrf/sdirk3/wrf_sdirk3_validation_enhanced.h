/**
 * @file wrf_sdirk3_validation_enhanced.h
 * @brief Comprehensive validation framework with multi-level support
 * @date 2025-08-05
 * 
 * Production-grade validation system that provides:
 * - Runtime configurable validation levels
 * - Numerical sanity checking
 * - Conservation property verification
 * - Detailed error reporting and diagnostics
 * 
 * Based on design document: (20250805)WRF-SDIRK3-design.md
 */

#ifndef WRF_SDIRK3_VALIDATION_ENHANCED_H
#define WRF_SDIRK3_VALIDATION_ENHANCED_H

#include <torch/torch.h>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <limits>
#include "wrf_sdirk3_config.h"  // FIX Round158: For g_sdirk3_config.debug_level
#include "wrf_sdirk3_types.h"   // FIX Round186: For centralized safe_device_index()

namespace WRF_SDIRK3 {
namespace Validation {

// FIX 2025-12-27: Helpers for safe scalar extraction in validation (handles GPU sync + AD)
// Validation functions often need scalar values; these helpers ensure proper handling
namespace {
inline float safe_scalar(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<float>();
}
inline bool safe_bool(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<bool>();
}

// FIX Round186: REMOVED duplicate safe_device_index(torch::Device&)
// Now centralized in wrf_sdirk3_types.h - use wrf::sdirk3::safe_device_index() instead
} // anonymous namespace

/**
 * @brief Validation levels for runtime configuration
 */
enum class Level {
    OFF = 0,           // No validation (production)
    CRITICAL = 1,      // Only critical checks (production with safety)
    STANDARD = 2,      // Standard checks (development)
    PARANOID = 3       // All checks including expensive ones (debugging)
};

/**
 * @brief Conservation types for physical property checking
 */
enum class ConservationType {
    MASS,              // Total mass conservation
    ENERGY,            // Total energy conservation
    MOMENTUM_X,        // X-momentum conservation
    MOMENTUM_Y,        // Y-momentum conservation
    MOMENTUM_Z,        // Z-momentum conservation
    VORTICITY,         // Vorticity conservation
    POTENTIAL_VORTICITY // Potential vorticity conservation
};

/**
 * @brief Tensor specification for validation
 */
struct TensorSpec {
    std::vector<int64_t> shape;
    torch::TensorOptions options;
    std::string name;
    
    // Expected value ranges
    float min_valid = -std::numeric_limits<float>::infinity();
    float max_valid = std::numeric_limits<float>::infinity();
    
    // Staggering information
    bool is_staggered = false;
    int stagger_dim = -1;  // Which dimension is staggered
    
    // Physical properties
    bool must_be_positive = false;
    bool must_be_monotonic = false;
};

/**
 * @brief Validation result with detailed diagnostics
 */
struct ValidationResult {
    bool passed = true;
    Level level_failed = Level::OFF;
    std::string error_message;
    std::string detailed_diagnostics;
    
    // Statistics
    size_t num_nans = 0;
    size_t num_infs = 0;
    size_t num_out_of_range = 0;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float mean_value = 0.0f;
    float std_dev = 0.0f;
    
    // Timing
    double validation_time_ms = 0.0;
};

/**
 * @class EnhancedTensorValidator
 * @brief Comprehensive tensor validation with runtime configuration
 */
class EnhancedTensorValidator {
private:
    static std::atomic<Level> validation_level_;
    static std::atomic<bool> collect_statistics_;
    static std::atomic<size_t> total_validations_;
    static std::atomic<size_t> failed_validations_;
    // FIX 2025-12-30 Batch24 Issue 4: Add frequency control for expensive statistics
    static std::atomic<size_t> statistics_call_count_;  // Counter for sampling
    static std::atomic<size_t> statistics_frequency_;   // Run stats every N calls (0=always)
    // FIX 2025-12-30 Batch27 Issue 3: Sampling threshold for large tensors
    static std::atomic<int64_t> sampling_threshold_;    // Use 9-point sampling above this numel (0=off)
    static std::atomic<bool> auto_sampling_mode_;       // Auto-enable sampling based on numel

    /**
     * @brief Check if validation should run at given level
     */
    static bool should_validate(Level required_level) {
        return static_cast<int>(validation_level_.load()) >= static_cast<int>(required_level);
    }

    /**
     * @brief Check if statistics should be computed this call (frequency limiting)
     * FIX 2025-12-30 Batch24 Issue 4: Skip expensive GPU reductions when frequency > 1
     */
    static bool should_compute_statistics() {
        if (!collect_statistics_.load()) return false;
        size_t freq = statistics_frequency_.load();
        if (freq <= 1) return true;  // Always compute if freq is 0 or 1
        // Increment counter and check if we're on a sample boundary
        size_t count = statistics_call_count_.fetch_add(1, std::memory_order_relaxed);
        return (count % freq) == 0;
    }
    
    /**
     * @brief Compute tensor statistics
     *
     * FIX 2025-12-28 Issue 6: Optimize multiple reductions using aminmax() + var_mean()
     * - Before: 6 separate reductions (min, max, mean, std, nan_count, inf_count)
     * - After: 4 reductions (aminmax combines min+max, var_mean combines variance+mean)
     * This reduces GPU kernel launches and improves performance for large tensors.
     *
     * FIX 2025-12-30 Batch27 Issue 3: 9-point sampling for very large tensors
     * When tensor.numel() > sampling_threshold_, use sampling instead of full reductions.
     * Trade-off: Lower accuracy but significantly reduced GPU overhead.
     */
    static void compute_statistics(const torch::Tensor& tensor, ValidationResult& result) {
        // FIX 2025-12-30 Batch24 Issue 4: Use frequency-limited check instead of simple bool
        if (!should_compute_statistics()) return;

        // FIX 2025-12-30 Batch22 Issue 4: Early return for empty tensors
        // aminmax() and var_mean() fail on empty tensors with undefined behavior
        if (tensor.numel() == 0) {
            result.min_value = 0.0f;
            result.max_value = 0.0f;
            result.mean_value = 0.0f;
            result.std_dev = 0.0f;
            result.num_nans = 0;
            result.num_infs = 0;
            return;
        }

        int64_t threshold = sampling_threshold_.load();
        bool use_sampling = (threshold > 0 && tensor.numel() > threshold);

        // FIX 2025-12-30 Batch27 Issue 3: Auto-mode: also sample on GPU with large tensors
        if (auto_sampling_mode_.load() && tensor.device().type() != torch::kCPU) {
            // GPU tensors: default threshold 10M elements if not set
            int64_t gpu_default = (threshold > 0) ? threshold : 10000000;
            use_sampling = (tensor.numel() > gpu_default);
        }

        if (use_sampling) {
            // 9-point sampling: corners (8) + center (1) for approximate statistics
            compute_statistics_sampled(tensor, result);
            return;
        }

        // Full statistics path (original)
        // FIX 2025-12-30 Batch21 Issue 3: Perform reductions on source device, only copy scalars
        // Before: flat.to(kCPU) copied entire tensor, then ran reductions on CPU
        // After: Run aminmax/var_mean/sum on original device, then .to(kCPU).item() for scalars only
        // FIX 2025-12-30 Batch23 Issue 4: Only convert dtype if not already float32 to avoid unnecessary copy
        auto flat = tensor.flatten();
        if (flat.scalar_type() != torch::kFloat32) {
            flat = flat.to(torch::kFloat32);  // Convert only if needed, keep on original device
        }

        // Combined min/max in single pass using aminmax() - runs on original device
        auto minmax_result = torch::aminmax(flat);
        result.min_value = std::get<0>(minmax_result).to(torch::kCPU).item<float>();
        result.max_value = std::get<1>(minmax_result).to(torch::kCPU).item<float>();

        // Combined variance/mean in single pass using var_mean() - runs on original device
        auto var_mean_result = torch::var_mean(flat);
        float variance = std::get<0>(var_mean_result).to(torch::kCPU).item<float>();
        result.mean_value = std::get<1>(var_mean_result).to(torch::kCPU).item<float>();
        result.std_dev = std::sqrt(variance);

        // Count special values - runs on original device, then copy scalar sums
        result.num_nans = torch::isnan(flat).sum().to(torch::kCPU).item<int64_t>();
        result.num_infs = torch::isinf(flat).sum().to(torch::kCPU).item<int64_t>();
    }

    /**
     * @brief Compute approximate statistics using 9-point sampling
     * FIX 2025-12-30 Batch27 Issue 3: Cheap sampling for large tensors
     * Samples: 8 corners + 1 center for 3D+, linear samples for 1D/2D
     * Trade-off: ~O(1) cost instead of O(n), but approximate results
     *
     * FIX 2025-12-30 Batch28 Issue 1: Single D2H transfer optimization
     * Previous: 9x flat[idx].to(kCPU).item<float>() = 9 D2H syncs
     * Now: index_select + 1x to(kCPU), or data_ptr for CPU contiguous
     *
     * FIX 2025-12-30 Batch30 Issue 5: Avoid flatten() for non-contiguous 3D tensors
     * Direct 8 corners + center indexing to avoid unnecessary copy
     */
    static void compute_statistics_sampled(const torch::Tensor& tensor, ValidationResult& result) {
        std::vector<float> samples;
        samples.reserve(9);

        // FIX 2025-12-30 Batch30 Issue 5: Check if we can use direct 3D indexing
        // For non-contiguous 3D tensors, avoid flatten() which causes copy
        bool use_direct_3d = (tensor.dim() == 3 && !tensor.is_contiguous());

        if (use_direct_3d) {
            // Direct 8 corners + center sampling for non-contiguous 3D tensors
            int64_t d0 = tensor.size(0), d1 = tensor.size(1), d2 = tensor.size(2);
            if (d0 == 0 || d1 == 0 || d2 == 0) {
                result.min_value = 0.0f;
                result.max_value = 0.0f;
                result.mean_value = 0.0f;
                result.std_dev = 0.0f;
                result.num_nans = 0;
                result.num_infs = 0;
                return;
            }

            // FIX Round155: Defense-in-depth check for dimension validity
            // After early return above, all dimensions must be > 0 to avoid OOB
            // in d0-1, d1-1, d2-1 corner index calculations below
            TORCH_CHECK(d0 > 0 && d1 > 0 && d2 > 0,
                "compute_statistics_sampled: 3D dimensions must be positive after empty check, got ",
                d0, "x", d1, "x", d2);

            // FIX 2025-12-30 Batch33 Issue 4: thread_local cache for 3D corner indices
            // Avoids per-call dynamic allocation; recompute only when dimensions change
            // FIX 2025-12-31 Batch37 Issue 1: Cache GPU index tensors to avoid H2D transfers
            struct Corner9IndexCache {
                int64_t d0 = -1, d1 = -1, d2 = -1;
                torch::DeviceType device_type = torch::kCPU;
                int16_t device_index = -1;
                torch::Tensor t0, t1, t2;

                bool is_valid(int64_t nd0, int64_t nd1, int64_t nd2, const torch::Device& dev) const {
                    if (!t0.defined()) return false;
                    if (d0 != nd0 || d1 != nd1 || d2 != nd2) return false;
                    if (device_type != dev.type()) return false;
                    // FIX Round156: Use safe_device_index for range validation
                    // FIX Round186: Use centralized version from wrf_sdirk3_types.h
                    return device_index == wrf::sdirk3::safe_device_index(dev);
                }
            };
            thread_local Corner9IndexCache corner_cache;

            torch::Tensor t0, t1, t2;
            if (corner_cache.is_valid(d0, d1, d2, tensor.device())) {
                // Reuse cached GPU tensors - no H2D transfer
                t0 = corner_cache.t0;
                t1 = corner_cache.t1;
                t2 = corner_cache.t2;
            } else {
                // Build multi-index sample points: 8 corners + center (fixed 9 points)
                // Corners: (0,0,0), (0,0,d2-1), (0,d1-1,0), (0,d1-1,d2-1),
                //          (d0-1,0,0), (d0-1,0,d2-1), (d0-1,d1-1,0), (d0-1,d1-1,d2-1)
                // Center: (d0/2, d1/2, d2/2)
                std::array<int64_t, 9> idx0 = {0, 0, 0, 0, d0-1, d0-1, d0-1, d0-1, d0/2};
                std::array<int64_t, 9> idx1 = {0, 0, d1-1, d1-1, 0, 0, d1-1, d1-1, d1/2};
                std::array<int64_t, 9> idx2 = {0, d2-1, 0, d2-1, 0, d2-1, 0, d2-1, d2/2};

                auto opts = torch::TensorOptions().dtype(torch::kLong).device(tensor.device());
                auto cpu_long_opts = torch::TensorOptions().dtype(torch::kLong).device(torch::kCPU);
                // FIX Round156: UAF FIX - Use torch::tensor() for safe ownership
                // PREVIOUS BUG: from_blob(stack_array).to(CPU) returns SAME tensor (no copy)
                // when target is CPU, leaving cache pointing to destroyed stack memory.
                //
                // SOLUTION: Use torch::tensor() which ALWAYS copies data into owned storage.
                // This is safer than from_blob().clone() because tensor() handles the
                // conversion from std::array to tensor in one step.
                //
                // Note: for GPU targets, .to(opts) creates a copy (device transfer),
                // but torch::tensor().to() is consistent and safe for all cases.
                t0 = torch::tensor(std::vector<int64_t>(idx0.begin(), idx0.end()), cpu_long_opts).to(opts);
                t1 = torch::tensor(std::vector<int64_t>(idx1.begin(), idx1.end()), cpu_long_opts).to(opts);
                t2 = torch::tensor(std::vector<int64_t>(idx2.begin(), idx2.end()), cpu_long_opts).to(opts);

                // Update cache
                corner_cache.d0 = d0; corner_cache.d1 = d1; corner_cache.d2 = d2;
                corner_cache.device_type = tensor.device().type();
                // FIX Round156: Use safe_device_index for range validation
                // FIX Round186: Use centralized version from wrf_sdirk3_types.h
                corner_cache.device_index = wrf::sdirk3::safe_device_index(tensor.device());
                corner_cache.t0 = t0; corner_cache.t1 = t1; corner_cache.t2 = t2;
            }

            // FIX 2025-12-31 Batch41 Issue 3: CPU contiguous direct data_ptr path
            // FIX Round156: Add dtype check - data_ptr<float>() is UB for non-float32 tensors
            if (tensor.is_cpu() && tensor.is_contiguous() &&
                tensor.scalar_type() == torch::kFloat32) {
                // CPU float32: Direct pointer access without tensor creation
                const float* ptr = tensor.data_ptr<float>();
                const int64_t stride0 = d1 * d2;
                const int64_t stride1 = d2;

                // Sample 8 corners + center directly
                samples.push_back(ptr[0]);                                      // (0,0,0)
                samples.push_back(ptr[d2-1]);                                   // (0,0,d2-1)
                samples.push_back(ptr[(d1-1)*stride1]);                         // (0,d1-1,0)
                samples.push_back(ptr[(d1-1)*stride1 + d2-1]);                  // (0,d1-1,d2-1)
                samples.push_back(ptr[(d0-1)*stride0]);                         // (d0-1,0,0)
                samples.push_back(ptr[(d0-1)*stride0 + d2-1]);                  // (d0-1,0,d2-1)
                samples.push_back(ptr[(d0-1)*stride0 + (d1-1)*stride1]);        // (d0-1,d1-1,0)
                samples.push_back(ptr[(d0-1)*stride0 + (d1-1)*stride1 + d2-1]); // (d0-1,d1-1,d2-1)
                samples.push_back(ptr[(d0/2)*stride0 + (d1/2)*stride1 + d2/2]); // center
            } else {
                // GPU or non-contiguous: Advanced indexing with single D2H transfer
                auto sampled = tensor.index({t0, t1, t2});  // [9] on device
                auto sampled_cpu = sampled.to(torch::kCPU, torch::kFloat32).contiguous();
                const float* ptr = sampled_cpu.data_ptr<float>();
                for (int64_t i = 0; i < 9; ++i) {
                    samples.push_back(ptr[i]);
                }
            }
        } else {
            // Original flatten-based path for contiguous tensors
            auto flat = tensor.flatten();
            int64_t n = flat.numel();

            // Build valid sample indices
            // FIX 2025-12-30 Batch28: thread_local cache for sample indices
            thread_local std::vector<int64_t> idx_values;
            thread_local int64_t cached_n = -1;

            if (cached_n != n) {
                idx_values = {0, n/8, n/4, 3*n/8, n/2, 5*n/8, 3*n/4, 7*n/8, n-1};
                // Filter valid indices
                idx_values.erase(
                    std::remove_if(idx_values.begin(), idx_values.end(),
                        [n](int64_t i) { return i < 0 || i >= n; }),
                    idx_values.end());
                cached_n = n;
            }

            if (idx_values.empty()) {
                result.min_value = 0.0f;
                result.max_value = 0.0f;
                result.mean_value = 0.0f;
                result.std_dev = 0.0f;
                result.num_nans = 0;
                result.num_infs = 0;
                return;
            }

            // FIX 2025-12-30 Batch28: CPU contiguous fast path with data_ptr
            // FIX Round156: Add dtype check - data_ptr<float>() is UB for non-float32 tensors
            if (flat.is_cpu() && flat.is_contiguous() &&
                flat.scalar_type() == torch::kFloat32) {
                const float* ptr = flat.data_ptr<float>();
                for (int64_t idx : idx_values) {
                    samples.push_back(ptr[idx]);
                }
            } else {
                // GPU or non-contiguous: use index_select + single to(kCPU)
                // FIX Round157: thread_local cache for idx_tensor to avoid per-call H2D transfer
                struct FlattenIdxCache {
                    int64_t n = -1;
                    torch::DeviceType device_type = torch::kCPU;
                    int16_t device_index = -1;
                    torch::Tensor idx_tensor;

                    bool is_valid(int64_t new_n, const torch::Device& dev) const {
                        if (!idx_tensor.defined()) return false;
                        if (n != new_n) return false;
                        if (device_type != dev.type()) return false;
                        // FIX Round186: Use centralized version from wrf_sdirk3_types.h
                        return device_index == wrf::sdirk3::safe_device_index(dev);
                    }
                };
                thread_local FlattenIdxCache flatten_cache;

                torch::Tensor idx_tensor;
                if (flatten_cache.is_valid(n, flat.device())) {
                    // Reuse cached GPU tensor - no H2D transfer
                    idx_tensor = flatten_cache.idx_tensor;
                } else {
                    // Create new tensor and cache it
                    auto cpu_opts = torch::TensorOptions().dtype(torch::kLong).device(torch::kCPU);
                    auto dev_opts = torch::TensorOptions().dtype(torch::kLong).device(flat.device());
                    idx_tensor = torch::tensor(idx_values, cpu_opts).to(dev_opts);

                    // Update cache
                    flatten_cache.n = n;
                    flatten_cache.device_type = flat.device().type();
                    // FIX Round186: Use centralized version from wrf_sdirk3_types.h
                    flatten_cache.device_index = wrf::sdirk3::safe_device_index(flat.device());
                    flatten_cache.idx_tensor = idx_tensor;
                }

                auto sampled = torch::index_select(flat, 0, idx_tensor);  // [9] on device
                auto sampled_cpu = sampled.to(torch::kCPU, torch::kFloat32).contiguous();  // Single D2H
                const float* ptr = sampled_cpu.data_ptr<float>();
                for (size_t i = 0; i < idx_values.size(); ++i) {
                    samples.push_back(ptr[i]);
                }
            }
        }

        if (samples.empty()) {
            result.min_value = 0.0f;
            result.max_value = 0.0f;
            result.mean_value = 0.0f;
            result.std_dev = 0.0f;
            result.num_nans = 0;
            result.num_infs = 0;
            return;
        }

        // Compute statistics from samples
        float min_val = samples[0], max_val = samples[0], sum = 0.0f;
        int64_t nan_count = 0, inf_count = 0;

        for (float v : samples) {
            if (std::isnan(v)) { nan_count++; continue; }
            if (std::isinf(v)) { inf_count++; continue; }
            min_val = std::min(min_val, v);
            max_val = std::max(max_val, v);
            sum += v;
        }

        size_t valid_count = samples.size() - nan_count - inf_count;
        float mean = (valid_count > 0) ? sum / valid_count : 0.0f;

        // Compute std dev from samples
        float var_sum = 0.0f;
        for (float v : samples) {
            if (!std::isnan(v) && !std::isinf(v)) {
                float diff = v - mean;
                var_sum += diff * diff;
            }
        }
        float std_dev = (valid_count > 1) ? std::sqrt(var_sum / (valid_count - 1)) : 0.0f;

        // Scale NaN/Inf counts to estimate for full tensor
        // OPT Pass34: Use tensor.numel() instead of out-of-scope 'n'
        float scale = static_cast<float>(tensor.numel()) / samples.size();
        result.min_value = min_val;
        result.max_value = max_val;
        result.mean_value = mean;
        result.std_dev = std_dev;
        result.num_nans = static_cast<int64_t>(nan_count * scale);  // Approximate
        result.num_infs = static_cast<int64_t>(inf_count * scale);  // Approximate
    }
    
public:
    /**
     * @brief Set global validation level
     */
    static void set_level(Level level) {
        validation_level_ = level;
    }
    
    /**
     * @brief Get current validation level
     */
    static Level get_level() {
        return validation_level_.load();
    }
    
    /**
     * @brief Enable/disable statistics collection
     * FIX 2025-12-30 Batch26 Issue 4: Reset counter on enable to avoid skip on first call
     */
    static void set_collect_statistics(bool enable) {
        collect_statistics_ = enable;
        if (enable) {
            // Reset counter when re-enabling to ensure first call computes statistics
            statistics_call_count_ = 0;
        }
    }

    /**
     * @brief Set statistics frequency (compute every N calls)
     * FIX 2025-12-30 Batch24 Issue 4: Reduce GPU overhead for frequent validations
     * @param frequency Run statistics every N calls (0 or 1 = always, higher = less frequent)
     */
    static void set_statistics_frequency(size_t frequency) {
        statistics_frequency_ = frequency;
        statistics_call_count_ = 0;  // Reset counter when frequency changes
    }

    /**
     * @brief Get current statistics frequency
     */
    static size_t get_statistics_frequency() {
        return statistics_frequency_.load();
    }

    /**
     * @brief Set sampling threshold for large tensors
     * FIX 2025-12-30 Batch27 Issue 3: 9-point sampling for tensors above threshold
     * @param threshold Use sampling when numel > threshold (0 = never sample, use full stats)
     * @param auto_mode If true, automatically switch to sampling based on numel and device
     */
    static void set_sampling_threshold(int64_t threshold, bool auto_mode = true) {
        sampling_threshold_ = threshold;
        auto_sampling_mode_ = auto_mode;
    }

    /**
     * @brief Get current sampling threshold
     */
    static int64_t get_sampling_threshold() {
        return sampling_threshold_.load();
    }

    /**
     * @brief Complete tensor validation with all checks
     */
    static ValidationResult validate_tensor_complete(
        const torch::Tensor& tensor,
        const TensorSpec& expected,
        const std::string& context) {
        
        auto start_time = std::chrono::high_resolution_clock::now();
        ValidationResult result;
        result.passed = true;
        
        total_validations_++;
        
        // Level 1: Critical checks (always run if not OFF)
        if (should_validate(Level::CRITICAL)) {
            // Check if tensor is defined
            if (!tensor.defined()) {
                result.passed = false;
                result.level_failed = Level::CRITICAL;
                result.error_message = context + ": Tensor is not defined";
                failed_validations_++;
                return result;
            }
            
            // Check shape
            if (tensor.sizes() != torch::IntArrayRef(expected.shape)) {
                result.passed = false;
                result.level_failed = Level::CRITICAL;
                std::stringstream ss;
                ss << context << ": Shape mismatch. Expected " 
                   << expected.shape << ", got " << tensor.sizes();
                result.error_message = ss.str();
                failed_validations_++;
                return result;
            }
        }
        
        // Level 2: Standard checks
        if (should_validate(Level::STANDARD)) {
            // Check data type
            if (tensor.options().dtype() != expected.options.dtype()) {
                result.passed = false;
                result.level_failed = Level::STANDARD;
                result.error_message = context + ": Data type mismatch";
            }
            
            // Check device
            if (tensor.device() != expected.options.device()) {
                result.passed = false;
                result.level_failed = Level::STANDARD;
                result.error_message = context + ": Device mismatch";
            }
            
            // Numerical sanity
            if (!check_numerical_sanity(tensor, 
                                       expected.min_valid, 
                                       expected.max_valid,
                                       true, true)) {
                result.passed = false;
                result.level_failed = Level::STANDARD;
                result.error_message = context + ": Numerical sanity check failed";
            }
        }
        
        // Level 3: Paranoid checks
        if (should_validate(Level::PARANOID)) {
            // Compute statistics
            compute_statistics(tensor, result);
            
            // Check positivity constraint
            if (expected.must_be_positive && result.min_value < 0) {
                result.passed = false;
                result.level_failed = Level::PARANOID;
                result.error_message = context + ": Negative values in positive-only field";
            }
            
            // Check monotonicity
            if (expected.must_be_monotonic && !check_monotonicity(tensor)) {
                result.passed = false;
                result.level_failed = Level::PARANOID;
                result.error_message = context + ": Monotonicity violation";
            }
            
            // Generate detailed diagnostics
            std::stringstream diag;
            diag << "Tensor: " << expected.name << " in " << context << "\n";
            diag << "Shape: " << tensor.sizes() << "\n";
            diag << "Device: " << tensor.device() << "\n";
            diag << "Min: " << result.min_value << ", Max: " << result.max_value << "\n";
            diag << "Mean: " << result.mean_value << ", StdDev: " << result.std_dev << "\n";
            diag << "NaNs: " << result.num_nans << ", Infs: " << result.num_infs << "\n";
            result.detailed_diagnostics = diag.str();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.validation_time_ms = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
        
        if (!result.passed) {
            failed_validations_++;
        }
        
        return result;
    }
    
    /**
     * @brief Check numerical sanity of tensor
     */
    static bool check_numerical_sanity(
        const torch::Tensor& tensor,
        float min_valid = -1e10f,
        float max_valid = 1e10f,
        bool check_nan = true,
        bool check_inf = true) {

        if (!should_validate(Level::STANDARD)) return true;

        // FIX 2025-12-30 Batch22 Issue 4: Early return for empty tensors
        // any(), min(), max() produce undefined behavior on empty tensors
        if (tensor.numel() == 0) {
            return true;  // Empty tensor is numerically sane by definition
        }

        // FIX 2025-12-27: Use safe_bool helper for GPU tensors
        // Check for NaN
        if (check_nan && safe_bool(torch::isnan(tensor).any())) {
            return false;
        }

        // Check for Inf
        if (check_inf && safe_bool(torch::isinf(tensor).any())) {
            return false;
        }

        // Check range using safe_scalar
        auto min_val = safe_scalar(tensor.min());
        auto max_val = safe_scalar(tensor.max());

        if (min_val < min_valid || max_val > max_valid) {
            return false;
        }

        return true;
    }
    
    /**
     * @brief Verify conservation properties
     */
    static bool verify_conservation(
        const torch::Tensor& before,
        const torch::Tensor& after,
        ConservationType type,
        float tolerance = 1e-6f) {

        if (!should_validate(Level::STANDARD)) return true;

        // FIX 2025-12-30 Batch22 Issue 4: Early return for empty tensors
        if (before.numel() == 0 || after.numel() == 0) {
            return true;  // Empty tensors trivially conserve
        }

        float total_before, total_after;

        // FIX 2025-12-27: Use safe_scalar helper for GPU tensors
        switch (type) {
            case ConservationType::MASS:
                // Sum of all mass
                total_before = safe_scalar(before.sum());
                total_after = safe_scalar(after.sum());
                break;

            case ConservationType::ENERGY:
                // Sum of kinetic + potential energy
                // This is simplified - real implementation would be more complex
                total_before = safe_scalar((before * before).sum());
                total_after = safe_scalar((after * after).sum());
                break;

            case ConservationType::MOMENTUM_X:
            case ConservationType::MOMENTUM_Y:
            case ConservationType::MOMENTUM_Z:
                // Sum of momentum component
                total_before = safe_scalar(before.sum());
                total_after = safe_scalar(after.sum());
                break;

            default:
                // Not implemented yet
                return true;
        }

        float relative_change = std::abs(total_after - total_before) /
                               (std::abs(total_before) + 1e-20f);

        return relative_change <= tolerance;
    }
    
    /**
     * @brief Check if tensor is monotonic
     */
    static bool check_monotonicity(const torch::Tensor& tensor) {
        if (tensor.dim() != 1) {
            // Only check 1D tensors for now
            return true;
        }

        // FIX 2025-12-30 Batch22 Issue 4: Early return for tensors too small for diff()
        // torch::diff() requires at least 2 elements; single-element or empty is monotonic
        if (tensor.numel() < 2) {
            return true;
        }

        auto diff = torch::diff(tensor);
        // FIX 2025-12-27: Use safe_bool helper for GPU tensors
        bool increasing = safe_bool((diff >= 0).all());
        bool decreasing = safe_bool((diff <= 0).all());

        return increasing || decreasing;
    }
    
    /**
     * @brief Validate staggered grid consistency
     */
    static bool validate_staggered_consistency(
        const torch::Tensor& u,  // [ny, nz, nx+1]
        const torch::Tensor& v,  // [ny+1, nz, nx]
        const torch::Tensor& w,  // [ny, nz+1, nx]
        const torch::Tensor& scalar) {  // [ny, nz, nx]
        
        if (!should_validate(Level::STANDARD)) return true;
        
        // Check U staggering
        if (u.size(0) != scalar.size(0) || 
            u.size(1) != scalar.size(1) ||
            u.size(2) != scalar.size(2) + 1) {
            return false;
        }
        
        // Check V staggering
        if (v.size(0) != scalar.size(0) + 1 || 
            v.size(1) != scalar.size(1) ||
            v.size(2) != scalar.size(2)) {
            return false;
        }
        
        // Check W staggering
        if (w.size(0) != scalar.size(0) || 
            w.size(1) != scalar.size(1) + 1 ||
            w.size(2) != scalar.size(2)) {
            return false;
        }
        
        return true;
    }
    
    /**
     * @brief Get validation statistics
     */
    struct Statistics {
        size_t total_validations;
        size_t failed_validations;
        double failure_rate;
        Level current_level;
        bool collecting_stats;
    };
    
    static Statistics get_statistics() {
        Statistics stats;
        stats.total_validations = total_validations_.load();
        stats.failed_validations = failed_validations_.load();
        stats.failure_rate = (stats.total_validations > 0) ?
            100.0 * stats.failed_validations / stats.total_validations : 0.0;
        stats.current_level = validation_level_.load();
        stats.collecting_stats = collect_statistics_.load();
        return stats;
    }
    
    /**
     * @brief Print validation statistics
     * FIX Round187: Gated with debug_level >= 1 to avoid production log spam
     * OPT Pass33+: Upgraded to debug_level >= 2 to reduce noise in large-scale tests
     */
    static void print_statistics() {
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 2) {
            return;  // Silent unless verbose debug mode (debug_level >= 2)
        }
        auto stats = get_statistics();

        std::cout << "\n=== Enhanced Validation Statistics ===" << std::endl;
        std::cout << "Current Level:    " << static_cast<int>(stats.current_level)
                  << " (0=OFF, 1=CRITICAL, 2=STANDARD, 3=PARANOID)" << std::endl;
        std::cout << "Total Checks:     " << stats.total_validations << std::endl;
        std::cout << "Failed Checks:    " << stats.failed_validations << std::endl;
        std::cout << "Failure Rate:     " << std::fixed << std::setprecision(2)
                  << stats.failure_rate << "%" << std::endl;
        std::cout << "Collecting Stats: " << (stats.collecting_stats ? "Yes" : "No") << std::endl;
        std::cout << "=====================================" << std::endl;
    }
    
    /**
     * @brief Reset statistics
     */
    static void reset_statistics() {
        total_validations_ = 0;
        failed_validations_ = 0;
    }
};

// Static member definitions
std::atomic<Level> EnhancedTensorValidator::validation_level_{Level::STANDARD};
std::atomic<bool> EnhancedTensorValidator::collect_statistics_{true};
std::atomic<size_t> EnhancedTensorValidator::total_validations_{0};
std::atomic<size_t> EnhancedTensorValidator::failed_validations_{0};
// FIX 2025-12-30 Batch24 Issue 4: Statistics frequency control
std::atomic<size_t> EnhancedTensorValidator::statistics_call_count_{0};
std::atomic<size_t> EnhancedTensorValidator::statistics_frequency_{1};  // Default: compute every call
// FIX 2025-12-30 Batch27 Issue 3: Sampling threshold for large tensors
std::atomic<int64_t> EnhancedTensorValidator::sampling_threshold_{0};   // Default: off (use full stats)
std::atomic<bool> EnhancedTensorValidator::auto_sampling_mode_{true};   // Default: auto-enable for GPU

/**
 * @brief Convenience macros for validation
 */
#define VALIDATE_TENSOR(tensor, spec, context) \
    if (WRF_SDIRK3::Validation::EnhancedTensorValidator::get_level() != \
        WRF_SDIRK3::Validation::Level::OFF) { \
        auto result = WRF_SDIRK3::Validation::EnhancedTensorValidator::validate_tensor_complete( \
            tensor, spec, context); \
        if (!result.passed) { \
            std::cerr << "VALIDATION FAILED: " << result.error_message << std::endl; \
            if (!result.detailed_diagnostics.empty()) { \
                std::cerr << result.detailed_diagnostics << std::endl; \
            } \
        } \
    }

#define CHECK_CONSERVATION(before, after, type, tol) \
    WRF_SDIRK3::Validation::EnhancedTensorValidator::verify_conservation( \
        before, after, type, tol)

} // namespace Validation
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_VALIDATION_ENHANCED_H