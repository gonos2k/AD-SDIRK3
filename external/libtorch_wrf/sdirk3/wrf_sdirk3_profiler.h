/**
 * @file wrf_sdirk3_profiler.h
 * @brief Lightweight profiling for SDIRK3 performance analysis
 */

#pragma once

#include <chrono>
#include <string>
#include <map>
#include <iostream>
#include <iomanip>
#include "wrf_sdirk3_config.h"  // FIX Round187: For debug_level gating

namespace wrf {
namespace sdirk3 {

class ScopedTimer {
public:
    ScopedTimer(const std::string& name, bool enabled = true) 
        : name_(name), enabled_(enabled) {
        if (enabled_) {
            start_ = std::chrono::high_resolution_clock::now();
        }
    }
    
    ~ScopedTimer() {
        if (enabled_) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
            // OPT Pass33+: Gate individual timer output with debug_level >= 2
            if (g_sdirk3_config.debug_level >= 2) {
                std::cerr << "[PROFILE] " << name_ << ": "
                          << duration.count() / 1000.0 << " ms\n";
            }
        }
    }
    
private:
    std::string name_;
    bool enabled_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

/**
 * ProfileAccumulator - Collects timing statistics across PROFILE_SCOPE calls.
 *
 * FIX Round178 THREAD-SAFETY WARNING:
 * ==================================
 * This class uses std::map without synchronization. It is NOT thread-safe.
 * Using PROFILE_SCOPE inside OpenMP parallel regions will cause data races
 * and undefined behavior (corrupt statistics, crashes).
 *
 * Safe usage patterns:
 * 1. Only call PROFILE_SCOPE from single-threaded code sections
 * 2. Call PROFILE_SCOPE outside of #pragma omp parallel regions
 * 3. For parallel profiling, use thread_local counters and merge at end
 *
 * This is intentionally kept simple as profiling is debug-only functionality.
 * If thread-safe profiling is needed, implement one of these alternatives:
 *
 * FIX Round179: Alternative A - thread_local accumulators (recommended):
 *   thread_local std::map<std::string, double> local_totals;
 *   // ... accumulate in parallel region ...
 *   #pragma omp critical
 *   { ProfileAccumulator::instance().merge(local_totals); }
 *
 * FIX Round179: Alternative B - mutex protection (higher overhead):
 *   std::mutex mtx_;
 *   void record_safe(const std::string& name, double ms) {
 *       std::lock_guard<std::mutex> lock(mtx_);
 *       record(name, ms);
 *   }
 */
class ProfileAccumulator {
public:
    static ProfileAccumulator& instance() {
        static ProfileAccumulator inst;
        return inst;
    }

    // WARNING: Not thread-safe - do not call from OpenMP parallel regions
    // FIX Round188: Gate record() with debug_level to avoid map overhead in production
    void record(const std::string& name, double ms) {
        if (g_sdirk3_config.debug_level < 1) {
            return;  // Skip profiling in production (debug_level=0)
        }
        counts_[name]++;
        totals_[name] += ms;
        if (ms > maxes_[name]) maxes_[name] = ms;
        if (mins_.find(name) == mins_.end() || ms < mins_[name]) {
            mins_[name] = ms;
        }
    }
    
    // FIX Round187: Gate print_summary with debug_level >= 2 (statistics output policy)
    // OPT Pass33+: Aligned with "stats at level 2" policy for consistency
    void print_summary() {
        if (g_sdirk3_config.debug_level < 2) {
            return;  // Stats output requires debug_level >= 2
        }
        std::cerr << "\n╔═══════════════════════════════════════════════════════════╗\n";
        std::cerr << "║  SDIRK3 Performance Profile Summary                       ║\n";
        std::cerr << "╚═══════════════════════════════════════════════════════════╝\n\n";
        std::cerr << std::fixed << std::setprecision(1);
        std::cerr << "Component                          Count    Total(ms)   Avg(ms)   Min(ms)   Max(ms)\n";
        std::cerr << "─────────────────────────────────────────────────────────────────────────────────\n";

        for (auto& kv : totals_) {
            const auto& name = kv.first;
            std::cerr << std::left << std::setw(32) << name << "  ";
            std::cerr << std::right << std::setw(6) << counts_[name] << "  ";
            std::cerr << std::setw(10) << totals_[name] << "  ";
            std::cerr << std::setw(8) << (totals_[name] / counts_[name]) << "  ";
            std::cerr << std::setw(8) << mins_[name] << "  ";
            std::cerr << std::setw(8) << maxes_[name] << "\n";
        }
        std::cerr << "─────────────────────────────────────────────────────────────────────────────────\n";
    }
    
    void reset() {
        counts_.clear();
        totals_.clear();
        mins_.clear();
        maxes_.clear();
    }
    
private:
    std::map<std::string, int> counts_;
    std::map<std::string, double> totals_;
    std::map<std::string, double> mins_;
    std::map<std::string, double> maxes_;
};

// Macro for easy profiling
// PERFORMANCE NOTE:
//   - Recording (timing collection): debug_level >= 1 (lightweight, silent)
//   - Summary output (print_summary): debug_level >= 2 (stats display)
//   - Expensive diagnostics (.item(), .min(), .max()) should use debug_level >= 3
#define PROFILE_SCOPE(name) \
    ScopedTimer _timer_##__LINE__(name, wrf::sdirk3::g_sdirk3_config.debug_level >= 1)

#define PROFILE_START(name) \
    auto _start_##name = std::chrono::high_resolution_clock::now()

#define PROFILE_END(name) \
    do { \
        auto _end_##name = std::chrono::high_resolution_clock::now(); \
        auto _dur_##name = std::chrono::duration_cast<std::chrono::microseconds>(_end_##name - _start_##name); \
        double _ms_##name = _dur_##name.count() / 1000.0; \
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) { \
            wrf::sdirk3::ProfileAccumulator::instance().record(#name, _ms_##name); \
        } \
    } while(0)

} // namespace sdirk3
} // namespace wrf
