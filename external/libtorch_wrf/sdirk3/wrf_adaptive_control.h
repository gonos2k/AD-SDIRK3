#ifndef WRF_ADAPTIVE_CONTROL_H
#define WRF_ADAPTIVE_CONTROL_H

#include <torch/torch.h>
#include <cmath>
#include <algorithm>
#include <array>   // FIX 2025-12-30 Batch34 Issue 1: For GPU index cache
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-12-30 Batch34 Issue 1+2: For getVerifyPeriod, normalize_positive

namespace wrf {
namespace sdirk3 {

/**
 * Adaptive Tolerance Controller
 * 
 * Adjusts Newton and Krylov tolerances based on:
 * - Current convergence behavior
 * - Problem stiffness
 * - Desired accuracy
 */
class AdaptiveToleranceController {
private:
    // Tolerance bounds
    float newton_tol_min_ = 1e-8f;
    float newton_tol_max_ = 1e-4f;
    float krylov_tol_min_ = 1e-6f;
    float krylov_tol_max_ = 1e-2f;
    
    // Current tolerances
    float newton_tol_current_ = 1e-6f;
    float krylov_tol_current_ = 1e-4f;
    
    // History for adaptation
    std::vector<int> newton_iter_history_;
    std::vector<int> krylov_iter_history_;
    std::vector<float> residual_history_;
    const size_t history_size_ = 5;
    
    // Control parameters
    float tol_safety_factor_ = 0.9f;
    float tol_growth_factor_ = 1.5f;
    float tol_shrink_factor_ = 0.5f;
    
public:
    struct ToleranceSet {
        float newton_tol;
        float krylov_tol;
        float newton_rtol;
    };
    
    ToleranceSet get_tolerances() const {
        return {newton_tol_current_, krylov_tol_current_, newton_tol_current_ * 10.0f};
    }
    
    void update_tolerances(int newton_iters, int total_krylov_iters, 
                          float final_residual, bool converged) {
        // Add to history
        newton_iter_history_.push_back(newton_iters);
        krylov_iter_history_.push_back(total_krylov_iters);
        residual_history_.push_back(final_residual);
        
        // Keep history size bounded
        if (newton_iter_history_.size() > history_size_) {
            newton_iter_history_.erase(newton_iter_history_.begin());
            krylov_iter_history_.erase(krylov_iter_history_.begin());
            residual_history_.erase(residual_history_.begin());
        }
        
        // Only adapt after enough history
        if (newton_iter_history_.size() < 3) return;
        
        // Compute averages
        float avg_newton = 0.0f, avg_krylov = 0.0f;
        for (size_t i = 0; i < newton_iter_history_.size(); ++i) {
            avg_newton += newton_iter_history_[i];
            avg_krylov += krylov_iter_history_[i];
        }
        avg_newton /= newton_iter_history_.size();
        avg_krylov /= newton_iter_history_.size();
        
        // Adapt Newton tolerance
        if (!converged) {
            // Failed to converge - tighten tolerance
            newton_tol_current_ *= tol_shrink_factor_;
            krylov_tol_current_ *= tol_shrink_factor_;
        } else if (avg_newton < 2.0f) {
            // Converging too easily - can relax tolerance
            newton_tol_current_ *= tol_growth_factor_;
        } else if (avg_newton > 5.0f) {
            // Taking too many iterations - tighten tolerance
            newton_tol_current_ *= tol_shrink_factor_;
        }
        
        // Adapt Krylov tolerance based on average iterations per Newton step
        float krylov_per_newton = avg_krylov / std::max(avg_newton, 1.0f);
        if (krylov_per_newton > 20.0f) {
            krylov_tol_current_ *= tol_shrink_factor_;
        } else if (krylov_per_newton < 5.0f) {
            krylov_tol_current_ *= tol_growth_factor_;
        }
        
        // Enforce bounds
        newton_tol_current_ = std::clamp(newton_tol_current_, newton_tol_min_, newton_tol_max_);
        krylov_tol_current_ = std::clamp(krylov_tol_current_, krylov_tol_min_, krylov_tol_max_);
    }
    
    void reset() {
        newton_iter_history_.clear();
        krylov_iter_history_.clear();
        residual_history_.clear();
        newton_tol_current_ = 1e-6f;
        krylov_tol_current_ = 1e-4f;
    }
};

/**
 * Adaptive Timestep Controller
 *
 * Adjusts timestep based on:
 * - Error estimates
 * - Convergence behavior
 * - Physical constraints (CFL, diffusion)
 */
class AdaptiveTimestepController {
private:
    // Timestep bounds
    float dt_min_;
    float dt_max_;
    float dt_current_;

    // Control parameters
    float safety_factor_ = 0.9f;
    float growth_factor_max_ = 2.0f;
    float shrink_factor_min_ = 0.1f;
    float tolerance_ = 1e-4f;

    // Error estimation
    float last_error_estimate_ = 0.0f;
    int consecutive_successes_ = 0;
    int consecutive_failures_ = 0;

    // Physical constraints
    float cfl_limit_ = 1.0f;
    float diffusion_limit_ = 0.5f;

    // FIX 2025-12-30 Batch29 Issue 2: Static cache for dz.min() to avoid repeated reductions
    // FIX 2025-12-30 Batch34 Issue 1: Enhanced with device/dtype/stride + GPU sample + periodic verify
    struct DzMinCache {
        const void* data_ptr = nullptr;
        int64_t numel = 0;
        int64_t storage_offset = 0;
        int64_t stride_hash = 0;
        int8_t device_type = 0;
        int16_t device_index = 0;
        int8_t dtype = 0;
        float cached_min = 0.0f;
        float sample_first = 0.0f;
        float sample_mid = 0.0f;
        float sample_last = 0.0f;
        mutable uint64_t access_count = 0;

        static int64_t compute_stride_hash(const torch::Tensor& t) {
            int64_t hash = 14695981039346656037ULL;
            for (int64_t i = 0; i < t.dim(); ++i) {
                hash ^= static_cast<uint64_t>(t.stride(i));
                hash *= 1099511628211ULL;
            }
            return static_cast<int64_t>(hash);
        }

        // GPU 3-point sample extraction via index_select (single D2H)
        // FIX 2025-12-31 Batch37 Issue 1: Cache GPU index tensor to avoid H2D transfers
        struct Gpu3IndexCache {
            int64_t cached_n = 0;
            torch::DeviceType cached_device_type = torch::kCPU;
            int16_t cached_device_index = -1;
            torch::Tensor idx_tensor;

            // FIX Round162: Add range-safe device index helper
            static int16_t safe_device_idx(const torch::Device& dev) {
                if (!dev.has_index()) return -1;
                auto idx = dev.index();
                if (idx < 0 || idx > 32767) return -1;  // Overflow protection for large GPU clusters
                return static_cast<int16_t>(idx);
            }

            bool is_valid(int64_t n, const torch::Device& dev) const {
                if (!idx_tensor.defined()) return false;
                if (cached_n != n) return false;
                if (cached_device_type != dev.type()) return false;
                return cached_device_index == safe_device_idx(dev);
            }
        };

        static void extract_gpu_samples(const torch::Tensor& flat, int64_t n,
                                        float& first, float& mid, float& last) {
            // FIX Round163: Disable gradient tracking for diagnostic sampling
            torch::NoGradGuard no_grad;

            static thread_local Gpu3IndexCache cache;

            torch::Tensor idx_tensor;
            if (cache.is_valid(n, flat.device())) {
                // Reuse cached GPU index tensor - no H2D transfer
                idx_tensor = cache.idx_tensor;
            } else {
                // FIX Round162: Use torch::tensor() to create owned memory, avoiding UAF
                // from_blob + .to() on CPU may not copy, leaving cache pointing at freed stack memory
                std::vector<int64_t> indices = {0, n > 1 ? n/2 : 0, n > 1 ? n-1 : 0};
                idx_tensor = torch::tensor(indices, torch::TensorOptions().dtype(torch::kLong))
                                 .to(flat.device());

                // Update cache with safe device index
                cache.cached_n = n;
                cache.cached_device_type = flat.device().type();
                cache.cached_device_index = Gpu3IndexCache::safe_device_idx(flat.device());
                cache.idx_tensor = idx_tensor;
            }

            // FIX Round163: detach() before index_select to ensure no gradient tracking
            auto samples = flat.detach().index_select(0, idx_tensor).to(torch::kCPU, torch::kFloat32).contiguous();
            const float* ptr = samples.data_ptr<float>();
            first = ptr[0]; mid = ptr[1]; last = ptr[2];
        }

        bool is_valid(const torch::Tensor& dz) const {
            if (!dz.defined() || dz.numel() == 0) return false;
            if (dz.data_ptr() != data_ptr || dz.numel() != numel) return false;
            if (dz.storage_offset() != storage_offset) return false;
            if (compute_stride_hash(dz) != stride_hash) return false;
            if (static_cast<int8_t>(dz.device().type()) != device_type) return false;
            // FIX Round162: Use safe device index to prevent overflow on large GPU clusters
            if (Gpu3IndexCache::safe_device_idx(dz.device()) != device_index) return false;
            if (static_cast<int8_t>(dz.scalar_type()) != dtype) return false;

            // 3-point sample check for in-place modifications
            auto flat = dz.flatten();
            int64_t n = flat.numel();
            float cur_first, cur_mid, cur_last;
            if (flat.is_cpu() && flat.is_contiguous()) {
                // FIX Round162: Handle FP64 dtype to prevent UB from data_ptr<float>() on double tensor
                if (flat.scalar_type() == torch::kFloat64) {
                    const double* ptr = flat.data_ptr<double>();
                    cur_first = static_cast<float>(ptr[0]);
                    cur_mid = static_cast<float>((n > 1) ? ptr[n/2] : ptr[0]);
                    cur_last = static_cast<float>((n > 1) ? ptr[n-1] : ptr[0]);
                } else {
                    // Assume FP32 or convert via item() for other dtypes
                    auto flat32 = (flat.scalar_type() == torch::kFloat32) ? flat : flat.to(torch::kFloat32);
                    const float* ptr = flat32.data_ptr<float>();
                    cur_first = ptr[0];
                    cur_mid = (n > 1) ? ptr[n/2] : ptr[0];
                    cur_last = (n > 1) ? ptr[n-1] : ptr[0];
                }
            } else if (n > 0) {
                // FIX Round165: Gate GPU sampling with verify period to reduce D2H overhead
                // GPU path is expensive (D2H transfer) - only do sample check on verify period
                // Between verify periods, trust pointer/size/offset/stride/dtype match is sufficient
                uint64_t period = metric_utils::getVerifyPeriod();
                if ((access_count % period) != 0) {
                    // Not a verify period - skip expensive GPU sample check
                    return true;
                }
                // GPU path: index_select for 3-element D2H (only on verify periods)
                extract_gpu_samples(flat, n, cur_first, cur_mid, cur_last);
            } else {
                return true;
            }
            if (cur_first != sample_first) return false;
            if (n > 1 && cur_mid != sample_mid) return false;
            if (n > 1 && cur_last != sample_last) return false;
            return true;
        }

        bool needs_full_verify() const {
            uint64_t period = metric_utils::getVerifyPeriod();
            return (access_count % period) == 0;
        }

        bool full_verify(const torch::Tensor& dz) const {
            torch::NoGradGuard no_grad;
            auto normalized = metric_utils::normalize_positive(dz, 1e-6f);
            float actual_min = torch::min(normalized).to(torch::kCPU).item<float>();
            if (!std::isfinite(actual_min)) return false;
            float diff = std::abs(actual_min - cached_min);
            float scale = std::max(std::abs(actual_min), 1e-10f);
            return (diff / scale) < 1e-5f;
        }

        void update(const torch::Tensor& dz, float min_val) {
            data_ptr = dz.data_ptr();
            numel = dz.numel();
            storage_offset = dz.storage_offset();
            stride_hash = compute_stride_hash(dz);
            device_type = static_cast<int8_t>(dz.device().type());
            // FIX Round165: Use safe_device_idx to prevent overflow on large GPU clusters
            device_index = Gpu3IndexCache::safe_device_idx(dz.device());
            dtype = static_cast<int8_t>(dz.scalar_type());
            cached_min = min_val;
            access_count = 0;

            auto flat = dz.flatten();
            int64_t n = flat.numel();
            if (flat.is_cpu() && flat.is_contiguous() && n > 0) {
                const float* ptr = flat.data_ptr<float>();
                sample_first = ptr[0];
                sample_mid = (n > 1) ? ptr[n/2] : ptr[0];
                sample_last = (n > 1) ? ptr[n-1] : ptr[0];
            } else if (n > 0) {
                // GPU path: index_select for 3-element D2H
                extract_gpu_samples(flat, n, sample_first, sample_mid, sample_last);
            }
        }
    };
    static inline DzMinCache dz_min_cache_;
    
public:
    AdaptiveTimestepController(float dt_initial, float dt_min, float dt_max)
        : dt_min_(dt_min), dt_max_(dt_max), dt_current_(dt_initial) {}
    
    struct TimestepInfo {
        float dt_next;
        float dt_current;
        bool should_retry;
        std::string reason;
    };
    
    /**
     * Estimate error using embedded method or Richardson extrapolation
     *
     * AD WARNING 2025-12-26: This is a control/monitoring function that extracts
     * scalar values for timestep adaptation. It intentionally breaks the AD graph
     * since error estimates are not part of the physics computation.
     */
    float estimate_error(const torch::Tensor& U_high_order,
                        const torch::Tensor& U_low_order) {
        // AD FIX 2025-12-26: Explicit NoGradGuard for control logic
        torch::NoGradGuard no_grad;

        // Relative error estimate
        auto diff = U_high_order - U_low_order;
        auto scale = torch::maximum(torch::abs(U_high_order),
                                   torch::ones_like(U_high_order));
        // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
        auto relative_error = torch::max(torch::abs(diff) / scale).to(torch::kCPU).item<float>();

        return relative_error;
    }
    
    /**
     * Update timestep based on error estimate and convergence
     */
    TimestepInfo update_timestep(float error_estimate, bool converged,
                                int newton_iters, float max_cfl) {
        TimestepInfo info;
        info.dt_current = dt_current_;
        info.should_retry = false;
        
        last_error_estimate_ = error_estimate;
        
        if (!converged) {
            // Newton failed to converge - reduce timestep
            consecutive_failures_++;
            consecutive_successes_ = 0;
            
            float shrink_factor = std::max(0.5f / consecutive_failures_, shrink_factor_min_);
            dt_current_ *= shrink_factor;
            
            info.should_retry = true;
            info.reason = "Newton convergence failure";
        } else if (error_estimate > tolerance_) {
            // Error too large - reduce timestep
            consecutive_failures_++;
            consecutive_successes_ = 0;
            
            // Compute optimal timestep based on error
            float factor = safety_factor_ * std::pow(tolerance_ / error_estimate, 1.0f/3.0f);
            factor = std::clamp(factor, shrink_factor_min_, 1.0f);
            dt_current_ *= factor;
            
            info.should_retry = true;
            info.reason = "Error tolerance exceeded";
        } else {
            // Success - consider increasing timestep
            consecutive_successes_++;
            consecutive_failures_ = 0;
            
            if (consecutive_successes_ >= 3) {
                // Compute growth factor based on error
                float factor = safety_factor_ * std::pow(tolerance_ / error_estimate, 1.0f/3.0f);
                factor = std::min(factor, growth_factor_max_);
                
                // Also consider Newton convergence rate
                if (newton_iters > 5) {
                    factor = std::min(factor, 1.2f);
                }
                
                dt_current_ *= factor;
            }
        }
        
        // Apply CFL constraint
        if (max_cfl > 0) {
            float dt_cfl = cfl_limit_ * dt_current_ / max_cfl;
            if (dt_cfl < dt_current_) {
                dt_current_ = dt_cfl;
                info.reason = "CFL constraint";
            }
        }
        
        // Enforce bounds
        dt_current_ = std::clamp(dt_current_, dt_min_, dt_max_);
        
        info.dt_next = dt_current_;
        return info;
    }
    
    /**
     * Compute maximum CFL number for current state
     *
     * AD WARNING 2025-12-26: This is a control/monitoring function that extracts
     * scalar values for CFL computation. It intentionally breaks the AD graph
     * since CFL checks are not part of the physics computation.
     */
    float compute_max_cfl(const torch::Tensor& u, const torch::Tensor& v,
                         const torch::Tensor& w, const torch::Tensor& c_sound,
                         float dx, float dy, const torch::Tensor& dz) {
        // AD FIX 2025-12-26: Explicit NoGradGuard for control logic
        torch::NoGradGuard no_grad;

        // Max velocity magnitude and sound speed
        // OPT Pass32+: Batch 2 D2H into single torch::stack().to(kCPU)
        auto vel_mag = torch::sqrt(u*u + v*v + w*w);
        auto maxes_cpu = torch::stack({torch::max(vel_mag), torch::max(c_sound)}).to(torch::kCPU);
        float max_vel = maxes_cpu[0].item<float>();
        float max_c = maxes_cpu[1].item<float>();

        // Max characteristic speed
        float max_speed = max_vel + max_c;

        // Min grid spacing
        // FIX 2025-12-30 Batch29 Issue 2: Use DzMinCache to avoid repeated dz.min() reductions
        // FIX 2025-12-30 Batch34 Issue 1+2: Enhanced cache + periodic verify + normalize_positive
        // FIX Round166: Fix off-by-one - increment access_count BEFORE is_valid() so both
        // is_valid() (GPU sampling gate) and needs_full_verify() see the same value.
        // This ensures GPU sampling and full verification are synchronized on the same period.
        // Note: We increment before is_valid() and reset to 0 on cache miss (via update()).
        float dz_min;
        dz_min_cache_.access_count++;  // Increment FIRST to fix off-by-one
        bool cache_hit = dz_min_cache_.is_valid(dz);
        if (cache_hit) {
            // Periodic full-verify for moving_nest safety
            if (dz_min_cache_.needs_full_verify() && !dz_min_cache_.full_verify(dz)) {
                cache_hit = false;  // Full verify failed, recompute
            }
        }
        if (cache_hit) {
            dz_min = dz_min_cache_.cached_min;
        } else {
            // FIX 2025-12-30 Batch34 Issue 2: normalize_positive to handle NaN/Inf/negative
            // Note: update() resets access_count to 0, so next period starts fresh
            auto normalized_dz = metric_utils::normalize_positive(dz, 1e-6f);
            dz_min = torch::min(normalized_dz).to(torch::kCPU).item<float>();
            dz_min_cache_.update(dz, dz_min);
        }
        float dx_min = std::min({dx, dy, dz_min});

        // CFL number
        return max_speed * dt_current_ / dx_min;
    }
    
    float get_current_timestep() const { return dt_current_; }
    
    void reset(float dt_initial) {
        dt_current_ = dt_initial;
        consecutive_successes_ = 0;
        consecutive_failures_ = 0;
        last_error_estimate_ = 0.0f;
    }
};

/**
 * Combined adaptive controller for both tolerances and timestep
 */
class AdaptiveController {
private:
    AdaptiveToleranceController tol_controller_;
    std::unique_ptr<AdaptiveTimestepController> dt_controller_;
    
public:
    AdaptiveController(float dt_initial, float dt_min, float dt_max) {
        dt_controller_ = std::make_unique<AdaptiveTimestepController>(
            dt_initial, dt_min, dt_max
        );
    }
    
    struct AdaptiveParameters {
        float dt;
        float newton_tol;
        float krylov_tol;
        bool should_retry;
        std::string status_message;
    };
    
    AdaptiveParameters get_parameters(
        float error_estimate = 0.0f,
        bool converged = true,
        int newton_iters = 0,
        int krylov_iters = 0,
        float final_residual = 0.0f,
        float max_cfl = 0.0f) {
        
        // Update tolerances based on convergence history
        tol_controller_.update_tolerances(newton_iters, krylov_iters, 
                                         final_residual, converged);
        
        // Update timestep
        auto dt_info = dt_controller_->update_timestep(error_estimate, converged, 
                                                       newton_iters, max_cfl);
        
        // Get current parameters
        auto tols = tol_controller_.get_tolerances();
        
        AdaptiveParameters params;
        params.dt = dt_info.dt_next;
        params.newton_tol = tols.newton_tol;
        params.krylov_tol = tols.krylov_tol;
        params.should_retry = dt_info.should_retry;
        params.status_message = dt_info.reason;
        
        return params;
    }
    
    float get_current_timestep() const {
        return dt_controller_->get_current_timestep();
    }
    
    void reset(float dt_initial) {
        tol_controller_.reset();
        dt_controller_->reset(dt_initial);
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_ADAPTIVE_CONTROL_H