/**
 * @file wrf_sdirk3_performance_benchmark.h
 * @brief Professional performance benchmarking infrastructure for WRF-SDIRK3
 * @date 2025-08-05
 * 
 * Provides comprehensive performance measurement, statistical analysis,
 * and regression detection capabilities for optimization validation.
 * 
 * Based on design document: (20250805)WRF-SDIRK3-design.md
 * Principle: 현재 구조를 최대한 활용 (Maximum Current Structure Utilization)
 */

#ifndef WRF_SDIRK3_PERFORMANCE_BENCHMARK_H
#define WRF_SDIRK3_PERFORMANCE_BENCHMARK_H

#include <torch/torch.h>
#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>

namespace WRF_SDIRK3 {
namespace Benchmarking {

/**
 * @brief Configuration for benchmark runs
 */
struct BenchmarkConfig {
    int warmup_iterations = 5;          // Iterations to warm up caches
    int measurement_iterations = 20;    // Iterations to measure
    bool collect_cache_stats = true;    // Estimate cache behavior
    bool measure_memory = true;         // Track memory usage
    bool generate_report = true;        // Generate detailed report
    std::string report_format = "text"; // "text", "json", "csv"
    
    static BenchmarkConfig default_config() {
        return BenchmarkConfig{};
    }
    
    static BenchmarkConfig quick() {
        BenchmarkConfig config;
        config.warmup_iterations = 2;
        config.measurement_iterations = 10;
        return config;
    }
    
    static BenchmarkConfig thorough() {
        BenchmarkConfig config;
        config.warmup_iterations = 10;
        config.measurement_iterations = 50;
        return config;
    }
};

/**
 * @brief Detailed benchmark results with statistical analysis
 */
struct BenchmarkResult {
    std::string operation_name;
    
    // Timing statistics (milliseconds)
    double mean_time_ms = 0.0;
    double std_dev_ms = 0.0;
    double min_time_ms = 0.0;
    double max_time_ms = 0.0;
    double median_time_ms = 0.0;
    double percentile_95_ms = 0.0;
    double percentile_99_ms = 0.0;
    
    // Performance metrics
    size_t total_flops = 0;
    double gflops = 0.0;  // Billion FLOPS
    size_t bytes_accessed = 0;
    double bandwidth_gb_s = 0.0;
    double arithmetic_intensity = 0.0;  // FLOPS/byte
    
    // Memory and cache
    size_t peak_memory_mb = 0;
    size_t cache_misses_estimated = 0;
    double cache_efficiency = 0.0;  // 0-1
    
    // Raw data
    std::vector<double> time_samples;
    std::vector<double> memory_samples;
    
    // System info
    std::string cpu_info;
    int num_threads = 1;
    std::string timestamp;
};

/**
 * @brief Comparison report between two benchmark results
 */
struct ComparisonReport {
    double speedup = 1.0;               // New/baseline
    double speedup_confidence = 0.0;    // Statistical confidence
    bool statistically_significant = false;
    double p_value = 0.0;
    
    // Detailed comparisons
    double mean_improvement_percent = 0.0;
    double bandwidth_improvement_percent = 0.0;
    double memory_reduction_percent = 0.0;
    
    std::string summary;
    std::vector<std::string> recommendations;
};

/**
 * @class PerformanceProfiler
 * @brief Professional performance profiling and benchmarking
 */
class PerformanceProfiler {
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: std::mutex results_mutex_ member
    PerformanceProfiler(const PerformanceProfiler&) = delete;
    PerformanceProfiler& operator=(const PerformanceProfiler&) = delete;
    PerformanceProfiler(PerformanceProfiler&&) = delete;
    PerformanceProfiler& operator=(PerformanceProfiler&&) = delete;
    PerformanceProfiler() = default;

private:
    std::map<std::string, BenchmarkResult> results_;
    // OPT Pass34: mutable allows locking in const methods without const_cast
    // Thread-safe const methods need to lock mutex for read access
    mutable std::mutex results_mutex_;
    bool enable_profiling_ = true;
    
    /**
     * @brief Calculate percentile from sorted samples
     */
    static double calculate_percentile(const std::vector<double>& sorted_samples, 
                                     double percentile) {
        if (sorted_samples.empty()) return 0.0;
        
        size_t n = sorted_samples.size();
        double pos = percentile / 100.0 * (n - 1);
        size_t lower = static_cast<size_t>(std::floor(pos));
        size_t upper = static_cast<size_t>(std::ceil(pos));
        
        if (lower == upper) {
            return sorted_samples[lower];
        }
        
        double weight = pos - lower;
        return sorted_samples[lower] * (1.0 - weight) + sorted_samples[upper] * weight;
    }
    
    /**
     * @brief Calculate standard deviation
     */
    static double calculate_std_dev(const std::vector<double>& samples, double mean) {
        if (samples.size() <= 1) return 0.0;
        
        double sum_sq_diff = 0.0;
        for (double sample : samples) {
            double diff = sample - mean;
            sum_sq_diff += diff * diff;
        }
        
        return std::sqrt(sum_sq_diff / (samples.size() - 1));
    }
    
    /**
     * @brief Estimate cache misses based on access pattern
     */
    static size_t estimate_cache_misses(size_t bytes_accessed, 
                                       size_t cache_line_size = 64) {
        // Simplified model - assumes random access pattern
        const size_t l1_size = 32 * 1024;      // 32KB L1
        const size_t l2_size = 256 * 1024;     // 256KB L2
        const size_t l3_size = 8 * 1024 * 1024; // 8MB L3
        
        size_t cache_lines = bytes_accessed / cache_line_size;
        
        if (bytes_accessed <= l1_size) {
            return cache_lines / 10;  // 10% miss rate in L1
        } else if (bytes_accessed <= l2_size) {
            return cache_lines / 5;   // 20% miss rate in L2
        } else if (bytes_accessed <= l3_size) {
            return cache_lines / 3;   // 33% miss rate in L3
        } else {
            return cache_lines / 2;   // 50% miss rate to memory
        }
    }
    
    /**
     * @brief Get current memory usage in MB
     */
    static size_t get_current_memory_mb() {
        // Platform-specific implementation needed
        // For now, return PyTorch's CUDA memory if available
        #ifdef USE_CUDA
        if (torch::cuda::is_available()) {
            // Note: memory_allocated is only available when CUDA is compiled in
            // For CPU-only builds, this will not be compiled
            return 0; // torch::cuda::memory_allocated() / (1024 * 1024);
        }
        #endif
        return 0;  // TODO: Implement CPU memory tracking
    }
    
public:
    /**
     * @brief Profile a function with comprehensive measurements
     */
    template<typename Func>
    BenchmarkResult profile(const std::string& name,
                           Func&& operation,
                           const BenchmarkConfig& config = BenchmarkConfig::default_config()) {
        
        if (!enable_profiling_) {
            // Run once without profiling
            operation();
            return BenchmarkResult{};
        }
        
        BenchmarkResult result;
        result.operation_name = name;
        result.timestamp = get_timestamp();
        
        // Warmup runs
        for (int i = 0; i < config.warmup_iterations; ++i) {
            operation();
        }
        
        // Measurement runs
        std::vector<double> time_samples;
        std::vector<size_t> memory_samples;
        size_t peak_memory = 0;
        
        for (int i = 0; i < config.measurement_iterations; ++i) {
            size_t mem_before = config.measure_memory ? get_current_memory_mb() : 0;
            
            auto start = std::chrono::high_resolution_clock::now();
            operation();
            auto end = std::chrono::high_resolution_clock::now();
            
            size_t mem_after = config.measure_memory ? get_current_memory_mb() : 0;
            
            double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
            time_samples.push_back(elapsed_ms);
            
            if (config.measure_memory) {
                memory_samples.push_back(mem_after - mem_before);
                peak_memory = std::max(peak_memory, mem_after);
            }
        }
        
        // Calculate statistics
        result.time_samples = time_samples;
        std::sort(time_samples.begin(), time_samples.end());
        
        result.mean_time_ms = std::accumulate(time_samples.begin(), time_samples.end(), 0.0) 
                             / time_samples.size();
        result.std_dev_ms = calculate_std_dev(time_samples, result.mean_time_ms);
        result.min_time_ms = time_samples.front();
        result.max_time_ms = time_samples.back();
        result.median_time_ms = calculate_percentile(time_samples, 50.0);
        result.percentile_95_ms = calculate_percentile(time_samples, 95.0);
        result.percentile_99_ms = calculate_percentile(time_samples, 99.0);
        
        // Calculate performance metrics if provided
        if (result.total_flops > 0 && result.mean_time_ms > 0) {
            result.gflops = (result.total_flops / 1e9) / (result.mean_time_ms / 1000.0);
        }
        
        if (result.bytes_accessed > 0 && result.mean_time_ms > 0) {
            result.bandwidth_gb_s = (result.bytes_accessed / 1e9) / (result.mean_time_ms / 1000.0);
            result.arithmetic_intensity = static_cast<double>(result.total_flops) / result.bytes_accessed;
        }
        
        // Estimate cache behavior
        if (config.collect_cache_stats && result.bytes_accessed > 0) {
            result.cache_misses_estimated = estimate_cache_misses(result.bytes_accessed);
            result.cache_efficiency = 1.0 - (static_cast<double>(result.cache_misses_estimated * 64) 
                                           / result.bytes_accessed);
        }
        
        result.peak_memory_mb = peak_memory;
        
        // Store result
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_[name] = result;
        }
        
        if (config.generate_report) {
            print_result(result);
        }
        
        return result;
    }
    
    /**
     * @brief Profile with manual FLOPS and memory specification
     */
    template<typename Func>
    BenchmarkResult profile_with_metrics(const std::string& name,
                                        Func&& operation,
                                        size_t flops,
                                        size_t bytes_accessed,
                                        const BenchmarkConfig& config = BenchmarkConfig::default_config()) {
        
        auto result = profile(name, std::forward<Func>(operation), config);
        result.total_flops = flops;
        result.bytes_accessed = bytes_accessed;
        
        // Recalculate metrics with provided values
        if (result.mean_time_ms > 0) {
            result.gflops = (flops / 1e9) / (result.mean_time_ms / 1000.0);
            result.bandwidth_gb_s = (bytes_accessed / 1e9) / (result.mean_time_ms / 1000.0);
            result.arithmetic_intensity = static_cast<double>(flops) / bytes_accessed;
        }
        
        return result;
    }
    
    /**
     * @brief Compare two benchmark results
     */
    static ComparisonReport compare(const BenchmarkResult& baseline,
                                   const BenchmarkResult& optimized) {
        ComparisonReport report;
        
        // Calculate speedup
        if (baseline.mean_time_ms > 0) {
            report.speedup = baseline.mean_time_ms / optimized.mean_time_ms;
            report.mean_improvement_percent = (1.0 - optimized.mean_time_ms / baseline.mean_time_ms) * 100.0;
        }
        
        // Statistical significance test (simplified t-test)
        if (!baseline.time_samples.empty() && !optimized.time_samples.empty()) {
            // Calculate pooled standard deviation
            double n1 = baseline.time_samples.size();
            double n2 = optimized.time_samples.size();
            double s1 = baseline.std_dev_ms;
            double s2 = optimized.std_dev_ms;
            
            double pooled_std = std::sqrt(((n1 - 1) * s1 * s1 + (n2 - 1) * s2 * s2) / (n1 + n2 - 2));
            double se = pooled_std * std::sqrt(1.0/n1 + 1.0/n2);
            
            double t_stat = (baseline.mean_time_ms - optimized.mean_time_ms) / se;
            report.speedup_confidence = std::abs(t_stat) / 2.0;  // Simplified
            report.statistically_significant = std::abs(t_stat) > 2.0;  // ~95% confidence
        }
        
        // Compare other metrics
        if (baseline.bandwidth_gb_s > 0) {
            report.bandwidth_improvement_percent = 
                (optimized.bandwidth_gb_s - baseline.bandwidth_gb_s) / baseline.bandwidth_gb_s * 100.0;
        }
        
        if (baseline.peak_memory_mb > 0) {
            report.memory_reduction_percent = 
                (1.0 - static_cast<double>(optimized.peak_memory_mb) / baseline.peak_memory_mb) * 100.0;
        }
        
        // Generate summary
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Performance improved by " << report.mean_improvement_percent << "% ";
        ss << "(speedup: " << report.speedup << "x)";
        if (report.statistically_significant) {
            ss << " [Statistically Significant]";
        }
        report.summary = ss.str();
        
        // Generate recommendations
        if (report.speedup < 0.95) {
            report.recommendations.push_back("⚠️ Performance regression detected");
        } else if (report.speedup > 1.5) {
            report.recommendations.push_back("✅ Significant performance improvement");
        }
        
        if (report.bandwidth_improvement_percent < -10) {
            report.recommendations.push_back("Consider memory access pattern optimization");
        }
        
        if (optimized.cache_efficiency < 0.8) {
            report.recommendations.push_back("Cache efficiency is low - consider data layout optimization");
        }
        
        return report;
    }
    
    /**
     * @brief Print benchmark result
     */
    static void print_result(const BenchmarkResult& result) {
        std::cout << "\n=== Benchmark: " << result.operation_name << " ===" << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        
        std::cout << "Timing Statistics (ms):" << std::endl;
        std::cout << "  Mean:    " << result.mean_time_ms << " ± " << result.std_dev_ms << std::endl;
        std::cout << "  Median:  " << result.median_time_ms << std::endl;
        std::cout << "  Min/Max: " << result.min_time_ms << " / " << result.max_time_ms << std::endl;
        std::cout << "  95th %:  " << result.percentile_95_ms << std::endl;
        
        if (result.gflops > 0) {
            std::cout << "\nPerformance Metrics:" << std::endl;
            std::cout << "  GFLOPS:     " << result.gflops << std::endl;
            std::cout << "  Bandwidth:  " << result.bandwidth_gb_s << " GB/s" << std::endl;
            std::cout << "  AI:         " << result.arithmetic_intensity << " FLOPS/byte" << std::endl;
        }
        
        if (result.cache_efficiency > 0) {
            std::cout << "\nCache Performance:" << std::endl;
            std::cout << "  Efficiency: " << (result.cache_efficiency * 100) << "%" << std::endl;
            std::cout << "  Est. Misses: " << result.cache_misses_estimated << std::endl;
        }
        
        if (result.peak_memory_mb > 0) {
            std::cout << "\nMemory Usage:" << std::endl;
            std::cout << "  Peak: " << result.peak_memory_mb << " MB" << std::endl;
        }
        
        std::cout << "===========================================" << std::endl;
    }
    
    /**
     * @brief Print comparison report
     */
    static void print_comparison(const ComparisonReport& report) {
        std::cout << "\n=== Performance Comparison ===" << std::endl;
        std::cout << report.summary << std::endl;
        
        std::cout << "\nDetailed Metrics:" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Speedup:          " << report.speedup << "x" << std::endl;
        std::cout << "  Time Reduction:   " << report.mean_improvement_percent << "%" << std::endl;
        std::cout << "  Bandwidth Change: " << std::showpos << report.bandwidth_improvement_percent 
                  << "%" << std::noshowpos << std::endl;
        std::cout << "  Memory Reduction: " << report.memory_reduction_percent << "%" << std::endl;
        
        if (!report.recommendations.empty()) {
            std::cout << "\nRecommendations:" << std::endl;
            for (const auto& rec : report.recommendations) {
                std::cout << "  • " << rec << std::endl;
            }
        }
        
        std::cout << "==============================" << std::endl;
    }
    
    /**
     * @brief Export results to file
     */
    void export_results(const std::string& filename, 
                       const std::string& format = "json") {
        std::lock_guard<std::mutex> lock(results_mutex_);
        
        if (format == "json") {
            export_json(filename);
        } else if (format == "csv") {
            export_csv(filename);
        } else {
            export_text(filename);
        }
    }
    
    /**
     * @brief Get all stored results
     */
    std::map<std::string, BenchmarkResult> get_all_results() const {
        // OPT Pass34: const_cast removed - results_mutex_ is now mutable
        std::lock_guard<std::mutex> lock(results_mutex_);
        return results_;
    }
    
    /**
     * @brief Clear all stored results
     */
    void clear_results() {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.clear();
    }
    
    /**
     * @brief Enable/disable profiling
     */
    void set_enabled(bool enabled) {
        enable_profiling_ = enabled;
    }
    
private:
    static std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    void export_json(const std::string& filename) {
        // Simplified JSON export (full implementation would use a JSON library)
        std::ofstream file(filename);
        file << "{\n";
        file << "  \"benchmark_results\": [\n";
        
        bool first = true;
        for (const auto& [name, result] : results_) {
            if (!first) file << ",\n";
            first = false;
            
            file << "    {\n";
            file << "      \"name\": \"" << name << "\",\n";
            file << "      \"mean_time_ms\": " << result.mean_time_ms << ",\n";
            file << "      \"std_dev_ms\": " << result.std_dev_ms << ",\n";
            file << "      \"gflops\": " << result.gflops << ",\n";
            file << "      \"bandwidth_gb_s\": " << result.bandwidth_gb_s << "\n";
            file << "    }";
        }
        
        file << "\n  ]\n";
        file << "}\n";
    }
    
    void export_csv(const std::string& filename) {
        std::ofstream file(filename);
        file << "Operation,Mean_ms,StdDev_ms,Min_ms,Max_ms,GFLOPS,Bandwidth_GB/s\n";
        
        for (const auto& [name, result] : results_) {
            file << name << ","
                 << result.mean_time_ms << ","
                 << result.std_dev_ms << ","
                 << result.min_time_ms << ","
                 << result.max_time_ms << ","
                 << result.gflops << ","
                 << result.bandwidth_gb_s << "\n";
        }
    }
    
    void export_text(const std::string& filename) {
        std::ofstream file(filename);
        file << "WRF-SDIRK3 Performance Benchmark Results\n";
        file << "Generated: " << get_timestamp() << "\n";
        file << "========================================\n\n";
        
        for (const auto& [name, result] : results_) {
            file << "Operation: " << name << "\n";
            file << "  Mean time: " << result.mean_time_ms << " ms\n";
            file << "  Std dev:   " << result.std_dev_ms << " ms\n";
            file << "  GFLOPS:    " << result.gflops << "\n";
            file << "  Bandwidth: " << result.bandwidth_gb_s << " GB/s\n";
            file << "\n";
        }
    }
};

/**
 * @brief Global performance profiler instance
 */
inline PerformanceProfiler& get_profiler() {
    static PerformanceProfiler profiler;
    return profiler;
}

/**
 * @brief RAII-style scoped timer for simple measurements
 */
class ScopedTimer {
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
    bool print_on_destroy_;
    
public:
    ScopedTimer(const std::string& name, bool print_on_destroy = true)
        : name_(name), print_on_destroy_(print_on_destroy) {
        start_ = std::chrono::high_resolution_clock::now();
    }
    
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start_).count();
        
        if (print_on_destroy_) {
            std::cout << name_ << " took " << std::fixed << std::setprecision(3) 
                      << elapsed_ms << " ms" << std::endl;
        }
    }
    
    double elapsed_ms() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }
};

} // namespace Benchmarking
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_PERFORMANCE_BENCHMARK_H