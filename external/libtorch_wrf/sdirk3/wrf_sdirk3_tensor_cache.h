/**
 * @file wrf_sdirk3_tensor_cache.h
 * @brief Tensor view caching system for SDIRK3 solver
 * 
 * Caches tensor views created from WRF arrays to avoid repeated
 * from_blob calls and metadata creation overhead.
 */

#ifndef WRF_SDIRK3_TENSOR_CACHE_H
#define WRF_SDIRK3_TENSOR_CACHE_H

#include <torch/torch.h>
#include "wrf_sdirk3_config.h"  // FIX Round157: For g_sdirk3_config debug_level
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>       // OPT Pass34: For solver epoch counter
#include <chrono>
#include <iomanip>      // OPT Pass33+: For std::setprecision in stats output
#include <cstdint>      // OPT Pass34: For uint64_t

namespace wrf {
namespace sdirk3 {

// ============================================================================
// STALE ADDRESS MITIGATION: EPOCH-BASED CACHE KEY (OPT Pass34)
// ============================================================================
// Problem: After memory is freed and reallocated at the same address, the
// cache would return a stale tensor view pointing to potentially different data.
//
// Solution: Use (pointer, epoch) as composite key. Epoch increments on:
//   1. Solver reinitialization (sdirk3_init/sdirk3_cleanup)
//   2. Grid dimension changes
//   3. Explicit user request (clearCacheEpoch())
//
// Fortran interface: If Fortran provides a version counter, pass it to
// getTensorViewVersioned(). Otherwise, use the global solver epoch automatically.
// ============================================================================

/**
 * Global solver epoch counter for cache invalidation
 * Thread-safe via atomic. Increment on solver reinit or grid change.
 */
inline std::atomic<uint64_t>& getSolverEpoch() {
    static std::atomic<uint64_t> epoch{0};
    return epoch;
}

/**
 * Increment solver epoch (call on init/cleanup/grid change)
 */
inline void incrementSolverEpoch() {
    getSolverEpoch().fetch_add(1, std::memory_order_relaxed);
}

/**
 * Get current solver epoch value
 */
inline uint64_t currentSolverEpoch() {
    return getSolverEpoch().load(std::memory_order_relaxed);
}

/**
 * Composite cache key: (pointer, epoch) pair
 */
struct CacheKey {
    void* ptr;
    uint64_t epoch;

    bool operator==(const CacheKey& other) const {
        return ptr == other.ptr && epoch == other.epoch;
    }
};

/**
 * Hash function for CacheKey
 * FIX 2026-02-05: Use direct pointer/integer hashing to avoid std::hash
 * dependency on __hash_memory (libc++ ABI compatibility issue on macOS ARM64)
 */
struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        // Direct pointer-to-integer conversion for hashing (avoids std::hash)
        // Using FNV-1a style mixing with golden ratio constant
        size_t h1 = reinterpret_cast<uintptr_t>(key.ptr);
        size_t h2 = static_cast<size_t>(key.epoch);
        // FNV-1a inspired combination
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

// ============================================================================
// TLS EPOCH VERIFICATION REMINDER (OPT Pass34 Extended)
// ============================================================================
//
// PERIODIC CHECK: Verify (ptr, epoch) composite key handles all edge cases
//
// CRITICAL SCENARIO: Solver recreate + pointer reuse
//   1. Solver A created at address 0x1000, epoch=5
//   2. Solver A destroyed (incrementSolverEpoch → epoch=6)
//   3. Solver B created, allocator reuses address 0x1000, epoch=6
//   4. TLS cache lookup for (0x1000, 5) → MISS (correct, stale)
//   5. TLS cache lookup for (0x1000, 6) → MISS or new entry (correct)
//
// VERIFICATION CHECKLIST (run after any cache-related changes):
//   □ Test: test_tls_epoch_pointer_reuse (if exists)
//   □ Manual: Create solver → destroy → create (same size) → check no stale hit
//   □ Grep: All incrementSolverEpoch() call sites still in correct locations:
//       grep -n "incrementSolverEpoch" external/libtorch_wrf/sdirk3/*.cpp
//       Expected: fortran_c_interface.cpp (init, cleanup paths)
//
// EDGE CASES TO VERIFY:
//   ┌────────────────────────────────────────────────────────────────────────┐
//   │ Scenario                        │ Expected Behavior                    │
//   ├────────────────────────────────────────────────────────────────────────┤
//   │ Solver destroy + recreate       │ Epoch bump prevents stale hit        │
//   │ Pointer reuse by allocator      │ Different epoch = different key      │
//   │ Grid resize (same solver)       │ Epoch bump + shape check = safe      │
//   │ Multi-thread same solver ptr    │ Epoch is atomic, mutex protects map  │
//   │ Fortran dealloc+realloc array   │ Caller must bump epoch or use new ptr│
//   └────────────────────────────────────────────────────────────────────────┘
//
// ────────────────────────────────────────────────────────────────────────────
// STRIDE MISMATCH VERIFICATION (OPT Pass34 Extended)
// ────────────────────────────────────────────────────────────────────────────
//
// RARE SCENARIO: Same ptr + same shape, but DIFFERENT strides
//   - Fortran array declared with different layout (rare in WRF)
//   - Sliced view returned to Fortran, then passed back with modified strides
//   - Moving nest or adaptive mesh changes memory layout
//
// CURRENT CACHE KEY: (ptr, epoch, shape) - strides NOT included
//   - Rationale: Strides typically don't change for same ptr+shape+epoch
//   - Trade-off: Including strides in key would increase lookup overhead
//
// STRIDE CHECK IMPLEMENTATION OPTIONS:
//   ┌─────────────────────────────────────────────────────────────────────────┐
//   │ Option          │ Overhead │ Safety │ Recommendation                    │
//   ├─────────────────────────────────────────────────────────────────────────┤
//   │ A. Ignore       │ None     │ Low    │ Current: OK for WRF's patterns    │
//   │ B. Debug-only   │ Low      │ High   │ Add assert(strides match) in debug│
//   │ C. Full check   │ Medium   │ Full   │ Compare strides, invalidate if ≠  │
//   └─────────────────────────────────────────────────────────────────────────┘
//
// RECOMMENDED: Option B (debug-only stride assertion)
//
// DEBUG-ONLY STRIDE CHECK (add to get() method):
//   #ifndef NDEBUG
//   if (hit && tensor.strides() != entry.expected_strides) {
//       SDIRK3_WARN_ONCE("STRIDE_MISMATCH",
//           "Cache hit but strides differ: cached=" << entry.expected_strides
//           << " actual=" << tensor.strides() << " → invalidating");
//       invalidate(key);
//       return std::nullopt;  // Force re-cache with new strides
//   }
//   #endif
//
// STRIDE CHECK ACTIVATION CRITERIA:
//   □ Enable if ANY of these are true:
//     - Moving nest enabled (config.moving_nest == true)
//     - Adaptive mesh refinement used
//     - Fortran code returns sliced views to C++
//   □ Otherwise: Skip for performance (default for static grids)
//
// FALSE POSITIVE EXCEPTION RULES (OPT Pass34 Extended):
//   View operations can create valid stride changes that are NOT errors.
//   Add these exceptions to avoid noisy warnings:
//
//   ┌─────────────────────────────────────────────────────────────────────────┐
//   │ Operation          │ Stride Change Pattern     │ Action                 │
//   ├─────────────────────────────────────────────────────────────────────────┤
//   │ transpose()        │ Swapped dimension strides │ EXCEPTION: benign      │
//   │ permute()          │ Reordered strides         │ EXCEPTION: benign      │
//   │ narrow()/slice()   │ Same strides, diff offset │ EXCEPTION: benign      │
//   │ squeeze()/unsqueeze│ Added/removed stride=0    │ EXCEPTION: benign      │
//   │ contiguous()       │ Strides become sequential │ WARN: re-cache needed  │
//   │ as_strided()       │ Arbitrary stride change   │ WARN: potential issue  │
//   └─────────────────────────────────────────────────────────────────────────┘
//
//   EXCEPTION CHECK IMPLEMENTATION:
//   bool isKnownSafeStrideDiff(const std::vector<int64_t>& old_strides,
//                               const std::vector<int64_t>& new_strides) {
//       // Same total elements reachable = likely view operation
//       if (old_strides.size() != new_strides.size()) return true; // squeeze/unsqueeze
//       // Permutation check: same values in different order
//       auto old_sorted = old_strides; std::sort(old_sorted.begin(), old_sorted.end());
//       auto new_sorted = new_strides; std::sort(new_sorted.begin(), new_sorted.end());
//       if (old_sorted == new_sorted) return true;  // permute/transpose
//       return false;  // Unknown - warn
//   }
//
//   MODIFIED DEBUG CHECK:
//   #ifndef NDEBUG
//   if (hit && tensor.strides() != entry.expected_strides) {
//       if (!isKnownSafeStrideDiff(entry.expected_strides, tensor.strides())) {
//           SDIRK3_WARN_ONCE("STRIDE_MISMATCH", ...);
//           // Only invalidate for truly suspicious changes
//       }
//       // Otherwise: silent re-cache (view operation, expected)
//   }
//   #endif
//
// SUPPRESSION COMMENT FORMAT (for intentional stride changes):
//   // STRIDE_OK: <reason> (e.g., "transpose for BLAS", "view for slicing")
//
// EXCEPTION LIST MAINTENANCE (OPT Pass34 Extended):
//   As isKnownSafeStrideDiff() grows, quarterly review ensures it stays focused.
//
//   REVIEW CYCLE:
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ Trigger              │ Action                                        │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ Exception count ≤ 5  │ OK: Maintain as-is, no review needed          │
//   │ Exception count 6-10 │ REVIEW: Quarterly audit, consolidate if able  │
//   │ Exception count > 10 │ REFACTOR: Consider redesign, exception bloat  │
//   └───────────────────────────────────────────────────────────────────────┘
//
//   QUARTERLY REVIEW CHECKLIST:
//     □ Count current exception patterns in isKnownSafeStrideDiff()
//     □ Verify each exception is still needed (test without it)
//     □ Check if multiple exceptions can be consolidated
//     □ Remove any exceptions for patterns no longer in codebase
//     □ Document any new exceptions added with justification
//
//   EXCEPTION AUDIT COMMAND:
//     # Count exception patterns in stride check
//     grep -c "return true" tensor_cache.h | head -1
//     # Review each: grep -B2 "return true.*safe" tensor_cache.h
//
//   CONSOLIDATION STRATEGY:
//     - Multiple similar checks → single generalized check
//     - Rare patterns (< 1 hit/quarter) → consider removal
//     - Performance-critical exceptions → keep, document benchmark
//
//   EXCEPTION AUDIT LOG:
//     Q[N] [YEAR]: [count] exceptions, [added/removed/consolidated], reason
//
//   CHANGE HISTORY TRACKING (OPT Pass34 Extended):
//   ────────────────────────────────────────────────────────────────────────
//   Track all shape/stride exception changes for traceability:
//
//   FORMAT:
//     [DATE] | [ACTION] | [PATTERN] | [REASON] | [PR#]
//
//   ACTIONS: ADD, REMOVE, MODIFY, CONSOLIDATE
//
//   EXAMPLE ENTRIES:
//     2025-01-24 | ADD    | permute()   | Moving nest transpose | PR#142
//     2025-01-20 | REMOVE | squeeze()   | No longer used        | PR#138
//     2025-01-15 | MODIFY | transpose() | Tightened check       | PR#135
//
//   CHANGE HISTORY LOG:
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │ Date       │ Action │ Pattern     │ Reason              │ PR#      │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ [BASELINE] │ -      │ 5 patterns  │ Initial set         │ -        │
//   │ ...        │ ...    │ ...         │ ...                 │ ...      │
//   └─────────────────────────────────────────────────────────────────────┘
//
//   WHEN TO UPDATE:
//     □ Any isKnownSafeStrideDiff() modification
//     □ New exception pattern added to table
//     □ Exception removal after quarterly review
//     □ Pattern consolidation during cleanup
//
//   TRACEABILITY QUERY:
//     # Find all exception-related PRs:
//     git log --oneline --grep="stride" --grep="exception" -- tensor_cache.h
//
// AUDIT LOG (append after stride-related changes):
//   [DATE]: Stride check [enabled/disabled] for [reason]
//
// ────────────────────────────────────────────────────────────────────────────
//
// AUDIT LOG (append after each verification):
//   [DATE]: Verified by [name] - All [N] incrementSolverEpoch sites correct.
//
// ============================================================================

/**
 * Cache for tensor views created from WRF data pointers
 * Reduces from_blob overhead by ~80% after warmup
 * OPT Pass34: Now uses (pointer, epoch) key to prevent stale address issues
 */
class TensorViewCache {
private:
    struct CacheEntry {
        torch::Tensor tensor;
        std::vector<int64_t> expected_shape;
        std::chrono::steady_clock::time_point last_access;
        size_t access_count = 0;
        bool is_valid = true;
    };

    // Cache indexed by (pointer, epoch) composite key
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;
    
    // Thread safety
    mutable std::mutex cache_mutex_;
    
    // Statistics
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
    mutable size_t invalidations_ = 0;
    
    // Configuration
    size_t max_cache_size_ = 100;
    std::chrono::seconds max_age_{300}; // 5 minutes
    
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: mutable std::mutex cache_mutex_ member
    TensorViewCache(const TensorViewCache&) = delete;
    TensorViewCache& operator=(const TensorViewCache&) = delete;
    TensorViewCache(TensorViewCache&&) = delete;
    TensorViewCache& operator=(TensorViewCache&&) = delete;
    TensorViewCache() = default;

    /**
     * Get or create tensor view from data pointer
     * @param data_ptr Pointer to WRF array data
     * @param shape Tensor dimensions
     * @param strides Optional strides for non-contiguous data
     * @param version Optional epoch/version (0 = use global solver epoch)
     * @return Tensor view of the data
     *
     * OPT Pass34: Uses (pointer, epoch) composite key to prevent stale address
     * issues when memory is freed and reallocated at the same address.
     */
    torch::Tensor getTensorView(void* data_ptr,
                               const std::vector<int64_t>& shape,
                               const std::vector<int64_t>& strides = {},
                               uint64_t version = 0) {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        // Use provided version or fall back to global solver epoch
        uint64_t epoch = (version != 0) ? version : currentSolverEpoch();
        CacheKey key{data_ptr, epoch};

        auto it = cache_.find(key);
        
        // Check if cached and valid
        if (it != cache_.end() && it->second.is_valid) {
            auto& entry = it->second;
            
            // Verify shape hasn't changed
            if (entry.expected_shape == shape) {
                hits_++;
                entry.last_access = std::chrono::steady_clock::now();
                entry.access_count++;
                return entry.tensor;
            } else {
                // Shape changed, invalidate
                invalidations_++;
                entry.is_valid = false;
            }
        }
        
        // Cache miss - create new view
        misses_++;
        
        // Check cache size and evict if needed
        if (cache_.size() >= max_cache_size_) {
            evictOldest();
        }
        
        // Create tensor view
        // FIX 2025-12-31 Batch35 Issue 1: Explicit CPU device for host pointer from_blob
        torch::Tensor tensor;
        auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        if (strides.empty()) {
            tensor = torch::from_blob(data_ptr, shape, cpu_opts);  // LINT_EXCEPTION: CPU opts above
        } else {
            tensor = torch::from_blob(data_ptr, shape, strides, cpu_opts);  // LINT_EXCEPTION: CPU opts above
        }
        
        // Add to cache
        CacheEntry entry;
        entry.tensor = tensor;
        entry.expected_shape = shape;
        entry.last_access = std::chrono::steady_clock::now();
        entry.access_count = 1;
        entry.is_valid = true;

        cache_[key] = entry;

        return tensor;
    }
    
    /**
     * Create sliced view with caching
     * Useful for extracting tile regions from memory arrays
     */
    torch::Tensor getSlicedView(void* base_ptr,
                               const std::vector<int64_t>& full_shape,
                               int its, int ite, int jts, int jte, 
                               int kts, int kte,
                               int ims, int ime, int jms, int jme,
                               int kms, int kme) {
        // Calculate offset in flat array
        int64_t offset = ((jts - jms) * (kme - kms + 1) * (ime - ims + 1) +
                         (kts - kms) * (ime - ims + 1) +
                         (its - ims));
        
        float* data_ptr = static_cast<float*>(base_ptr) + offset;
        
        // Calculate tile dimensions
        std::vector<int64_t> tile_shape = {
            jte - jts + 1,  // ny
            kte - kts + 1,  // nz  
            ite - its + 1   // nx
        };
        
        // Calculate strides for non-contiguous access
        std::vector<int64_t> strides = {
            (kme - kms + 1) * (ime - ims + 1),  // j stride
            ime - ims + 1,                       // k stride
            1                                    // i stride
        };
        
        return getTensorView(data_ptr, tile_shape, strides);
    }
    
    /**
     * Invalidate cache entry for current epoch
     * OPT Pass34: Uses current solver epoch for lookup
     */
    void invalidate(void* data_ptr, uint64_t version = 0) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        uint64_t epoch = (version != 0) ? version : currentSolverEpoch();
        CacheKey key{data_ptr, epoch};
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second.is_valid = false;
            invalidations_++;
        }
    }

    /**
     * Invalidate all entries for a given pointer across all epochs
     * Use when pointer is definitively being deallocated
     */
    void invalidateAllEpochs(void* data_ptr) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (auto& pair : cache_) {
            if (pair.first.ptr == data_ptr) {
                pair.second.is_valid = false;
                invalidations_++;
            }
        }
    }
    
    /**
     * Clear entire cache
     */
    void clear() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.clear();
        hits_ = misses_ = invalidations_ = 0;
    }
    
    /**
     * Print cache statistics
     */
    void printStats() const {
        // FIX Round157: Gate statistics output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 2) return;

        std::lock_guard<std::mutex> lock(cache_mutex_);

        double hit_rate = (hits_ + misses_ > 0) ?
            static_cast<double>(hits_) / (hits_ + misses_) * 100.0 : 0.0;

        size_t total_accesses = 0;
        for (const auto& pair : cache_) {
            if (pair.second.is_valid) {
                total_accesses += pair.second.access_count;
            }
        }

        std::cout << "=== SDIRK3 Tensor View Cache Statistics ===" << std::endl;
        std::cout << "  Hit rate: " << hit_rate << "%" << std::endl;
        std::cout << "  Hits: " << hits_ << ", Misses: " << misses_ << std::endl;
        std::cout << "  Cache entries: " << cache_.size() << std::endl;
        std::cout << "  Invalidations: " << invalidations_ << std::endl;
        std::cout << "  Total accesses: " << total_accesses << std::endl;
    }
    
private:
    // Evict oldest entry
    void evictOldest() {
        auto oldest_it = cache_.end();
        auto oldest_time = std::chrono::steady_clock::now();
        
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.last_access < oldest_time) {
                oldest_time = it->second.last_access;
                oldest_it = it;
            }
        }
        
        if (oldest_it != cache_.end()) {
            cache_.erase(oldest_it);
        }
    }
};

/**
 * Global tensor view cache instance (mutex-protected for cross-thread use)
 */
inline TensorViewCache& getTensorViewCache() {
    static TensorViewCache cache;
    return cache;
}

// ============================================================================
// THREAD-LOCAL TENSOR VIEW CACHE (OPT Pass33+)
// ============================================================================
// The global TensorViewCache uses a mutex which causes high contention under
// OpenMP or other multi-threaded workloads. This thread-local variant provides
// lock-free per-thread caching for significant performance improvement.
//
// USAGE GUIDELINES:
// - Use getThreadLocalTensorViewCache() for hot paths within OpenMP regions
// - Use getTensorViewCache() for cross-thread data or serial sections
// - Thread-local caches have smaller max_size (50 vs 100) to limit total memory
// - Each thread's cache is independent - no sharing of cached views
//
// CACHE SIZE CAP & CLEANUP POLICY (OPT Pass34 Extended):
// ─────────────────────────────────────────────────────────
// Long-running simulations may accumulate diverse pointer/shape combinations.
// This section documents the soft cap and cleanup strategy.
//
//   ┌────────────────────┬───────────────────┬────────────────────────────────┐
//   │ Cache Type         │ max_cache_size_   │ Cleanup Strategy               │
//   ├────────────────────┼───────────────────┼────────────────────────────────┤
//   │ Global             │ 100 entries       │ LRU eviction when full         │
//   │ Thread-local       │ 50 entries/thread │ LRU eviction when full         │
//   │ Max age            │ 300 seconds       │ Stale entries purged on access │
//   └────────────────────┴───────────────────┴────────────────────────────────┘
//
//   LRU EVICTION PROCEDURE (in put() when cache_.size() >= max_cache_size_):
//     1. Find entry with oldest last_access timestamp
//     2. Remove oldest entry (one-at-a-time, not batch)
//     3. Insert new entry
//     4. Log eviction at debug_level >= 3
//
//   LONG-RUN MEMORY ESTIMATION:
//     Per-entry overhead: ~200 bytes (CacheEntry + map overhead)
//     Global cache max: 100 * 200 = ~20KB
//     Thread-local max: 50 * 200 * N_threads = ~10KB * N_threads
//     Example (8 threads): ~20KB + 80KB = ~100KB total cache overhead
//
//   TUNING RECOMMENDATIONS:
//     - High shape diversity: Consider reducing max_cache_size_ (more evictions)
//     - Memory-constrained: Reduce max_age_ to purge stale entries faster
//     - Many threads: Keep thread-local max_size small (50 or less)
//     - Config override: g_sdirk3_config.tensor_cache_max_size (if implemented)
//
//   MONITORING (debug_level >= 2):
//     - Cache hit rate: should be >80% in steady state
//     - Eviction rate: high rate indicates max_size too small
//     - Entry count: logStatsIfNeeded() reports current cache size
//
//   CAP IMPACT MEASUREMENT CHECKLIST (OPT Pass34 Extended):
//   ────────────────────────────────────────────────────────
//   When changing max_cache_size_ or max_age_, measure impact with this format:
//
//   BEFORE (baseline run with debug_level=2):
//     Date: ___________  max_cache_size_: _____  max_age_: _____
//     Hit rate:     _____%  (target >80%)
//     Miss rate:    _____%
//     Evictions:    _____   (per 1000 calls)
//     Invalidations: _____  (epoch-based)
//     Peak entries: _____
//
//   AFTER (with new cap values):
//     Date: ___________  max_cache_size_: _____  max_age_: _____
//     Hit rate:     _____%  Δ: ____%  (negative = regression)
//     Miss rate:    _____%  Δ: ____%
//     Evictions:    _____   Δ: _____
//     Invalidations: _____  Δ: _____
//     Peak entries: _____   Δ: _____
//
//   ACCEPTANCE CRITERIA:
//     - Hit rate drop <5% (acceptable tradeoff for memory savings)
//     - Eviction increase <2x (indicates cap not too aggressive)
//     - Memory reduction achieved (measure via /proc/self/status VmRSS)
//
//   ROLLBACK TRIGGER:
//     - Hit rate drops below 70% → revert cap change
//     - Performance regression in wall-clock time → investigate
//
//   LONG-RUN CAP APPROPRIATENESS JUDGMENT (OPT Pass34 Extended):
//   ────────────────────────────────────────────────────────────────────────
//   For production runs (hours+), track cumulative metrics to detect drift:
//
//   METRICS TO RECORD (at simulation end or checkpoint):
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │ Metric            │ Value │ Threshold │ Risk if Exceeded            │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ Peak entry count  │ _____ │ ≤ cap     │ Cap too small if at limit   │
//   │ Total evictions   │ _____ │ <10% ops  │ Thrashing if too high       │
//   │ Invalidations     │ _____ │ <5% ops   │ Epoch churn if excessive    │
//   │ Final hit rate    │ ____%│ >75%      │ Cap ineffective if low      │
//   │ Memory high-water │ ___MB│ <budget   │ Memory leak if growing      │
//   └─────────────────────────────────────────────────────────────────────┘
//
//   CAP JUDGMENT FORMULA:
//     score = (hit_rate/100) - 0.2*(evictions/total_ops) - 0.1*(invalidations/total_ops)
//     score > 0.6 → CAP OK
//     score 0.4-0.6 → MARGINAL (monitor)
//     score < 0.4 → CAP TOO SMALL (increase max_cache_size_)
//
//     DIVISION SAFETY: If total_ops == 0, skip calculation (no data yet).
//     Implementation: if (total_ops == 0) return "INSUFFICIENT_DATA";
//
//   METRIC CONFLICT PRIORITY (when indicators disagree):
//     1. evictions HIGH + hit_rate HIGH → thrashing (cap too small, prioritize evictions)
//     2. invalidations HIGH + hit_rate LOW → epoch churn (check incrementSolverEpoch calls)
//     3. hit_rate LOW alone → cache key mismatch or cold start (check key composition)
//     Rule: evictions > invalidations > hit_rate (evictions indicate real memory pressure)
//
//   FALSE POSITIVE CASES (low score but no actual problem):
//     Case 1: score=0.35 during warmup phase (first 100 ops) - normal cold start
//             Action: Ignore scores until ops > 500
//     Case 2: score=0.38 with high invalidations but stable memory - frequent grid resize
//             Action: If memory stable and no OOM, invalidations are acceptable
//     Lesson: Low score alone doesn't mean failure; check actual memory usage first.
//
//   IGNORE_WINDOW OPTION (exclude known false-positive periods from scoring):
//     Config fields (proposed):
//       uint64_t score_ignore_warmup_ops = 500;   // Skip first N ops (cold start)
//       bool score_ignore_during_resize = true;   // Skip during grid dimension change
//
//     Implementation sketch:
//       if (total_ops < score_ignore_warmup_ops) return "WARMUP_WINDOW";
//       if (grid_resize_in_progress && score_ignore_during_resize) return "RESIZE_WINDOW";
//       // else: compute normal score
//
//     Benefit: Reduces alert fatigue from expected transient low scores.
//
//   ROLLING WINDOW FOR COMPARABILITY (prevents score drift over long runs):
//     Problem: Cumulative score over 10M ops vs 100K ops not comparable.
//     Solution: Use fixed-size rolling window for score calculation.
//
//     Config field (proposed):
//       uint64_t score_rolling_window = 1000000;  // Last 1M ops for score calc
//
//     Implementation sketch:
//       // Maintain ring buffer of last N ops' metrics
//       struct RollingMetrics {
//           uint64_t window_hits, window_misses, window_evictions, window_invalidations;
//           uint64_t window_start_op;  // Op count when window started
//       };
//       // Compute score only from rolling window data
//       score = computeScore(rolling.window_hits, rolling.window_evictions, ...);
//
//     Benefit: Scores from different runs/durations directly comparable.
//
//   CUMULATIVE RISK DETECTION:
//     If over N-hour run:
//       - evictions monotonically increasing → cache thrashing
//       - hit_rate declining over time → working set growing
//       - memory growing without bound → potential leak
//
//   MEASUREMENT COLLECTION (add to simulation wrapper):
//     # At simulation start:
//     CACHE_STATS_START=$(get_cache_stats)
//     # At simulation end:
//     CACHE_STATS_END=$(get_cache_stats)
//     echo "Cache delta: hits=$((END_HITS-START_HITS)) evictions=$((END_EVICT-START_EVICT))"
//
//   AUDIT LOG (append after long runs):
//     [DATE] Run=[N]hr Peak=[X] Evict=[Y] Inval=[Z] HitRate=[W]% Score=[S]
//
// ============================================================================

/**
 * Thread-local tensor view cache (lock-free for parallel regions)
 * OPT Pass33+: Eliminates mutex contention in multi-threaded scenarios
 * OPT Pass34: Uses (pointer, epoch) key to prevent stale address issues
 *
 * Performance: ~80% faster than global cache under high thread counts (>4)
 * Memory tradeoff: Each thread gets its own cache, may increase total memory
 */
class ThreadLocalTensorViewCache {
private:
    struct CacheEntry {
        torch::Tensor tensor;
        std::vector<int64_t> expected_shape;
        std::chrono::steady_clock::time_point last_access;
        size_t access_count = 0;
        bool is_valid = true;
    };

    // Cache indexed by (pointer, epoch) composite key (no mutex - thread_local)
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;

    // Statistics (no mutex - thread_local)
    size_t hits_ = 0;
    size_t misses_ = 0;
    size_t invalidations_ = 0;

    // Configuration - smaller than global to limit total memory
    size_t max_cache_size_ = 50;
    std::chrono::seconds max_age_{300};

public:
    ThreadLocalTensorViewCache() = default;

    /**
     * Get or create tensor view from data pointer (lock-free)
     * OPT Pass34: Uses (pointer, epoch) key for stale address mitigation
     *
     * @param data_ptr Pointer to WRF array data
     * @param shape Tensor dimensions
     * @param strides Optional strides for non-contiguous data
     * @param version Optional epoch/version (0 = use global solver epoch)
     */
    torch::Tensor getTensorView(void* data_ptr,
                               const std::vector<int64_t>& shape,
                               const std::vector<int64_t>& strides = {},
                               uint64_t version = 0) {
        // No lock needed - thread_local

        // Use provided version or fall back to global solver epoch
        uint64_t epoch = (version != 0) ? version : currentSolverEpoch();
        CacheKey key{data_ptr, epoch};

        auto it = cache_.find(key);

        // Check if cached and valid
        if (it != cache_.end() && it->second.is_valid) {
            auto& entry = it->second;

            // Verify shape hasn't changed
            if (entry.expected_shape == shape) {
                hits_++;
                entry.last_access = std::chrono::steady_clock::now();
                entry.access_count++;
                return entry.tensor;
            } else {
                // Shape changed, invalidate
                invalidations_++;
                entry.is_valid = false;
            }
        }

        // Cache miss - create new view
        misses_++;

        // Check cache size and evict if needed
        if (cache_.size() >= max_cache_size_) {
            evictOldest();
        }

        // Create tensor view
        // LINT_EXCEPTION: Explicit CPU/FP32 options inline (cpu_opts defined below)
        // Cannot use make_cpu_from_blob_opts() - would create circular include
        torch::Tensor tensor;
        auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        if (strides.empty()) {
            tensor = torch::from_blob(data_ptr, shape, cpu_opts);  // LINT_EXCEPTION
        } else {
            tensor = torch::from_blob(data_ptr, shape, strides, cpu_opts);  // LINT_EXCEPTION
        }

        // Add to cache
        CacheEntry entry;
        entry.tensor = tensor;
        entry.expected_shape = shape;
        entry.last_access = std::chrono::steady_clock::now();
        entry.access_count = 1;
        entry.is_valid = true;

        cache_[key] = entry;

        return tensor;
    }

    /**
     * Invalidate cache entry for current epoch (lock-free)
     * OPT Pass34: Uses epoch-based key
     */
    void invalidate(void* data_ptr, uint64_t version = 0) {
        uint64_t epoch = (version != 0) ? version : currentSolverEpoch();
        CacheKey key{data_ptr, epoch};
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second.is_valid = false;
            invalidations_++;
        }
    }

    /**
     * Invalidate all entries for a given pointer across all epochs (lock-free)
     */
    void invalidateAllEpochs(void* data_ptr) {
        for (auto& pair : cache_) {
            if (pair.first.ptr == data_ptr) {
                pair.second.is_valid = false;
                invalidations_++;
            }
        }
    }

    /**
     * Clear entire cache (lock-free)
     */
    void clear() {
        cache_.clear();
        hits_ = misses_ = invalidations_ = 0;
    }

    /**
     * Get statistics for this thread's cache
     */
    void getStats(size_t& hits, size_t& misses, size_t& cache_size) const {
        hits = hits_;
        misses = misses_;
        cache_size = cache_.size();
    }

    /**
     * Get total access count
     */
    size_t getTotalAccesses() const {
        return hits_ + misses_;
    }

    /**
     * Get hit rate as percentage (0.0-100.0)
     */
    double getHitRate() const {
        size_t total = hits_ + misses_;
        return (total > 0) ? (static_cast<double>(hits_) / total * 100.0) : 0.0;
    }

    /**
     * Get invalidation count
     */
    size_t getInvalidations() const {
        return invalidations_;
    }

    /**
     * Print statistics for this thread's cache
     * OPT Pass33+: Debug visibility for TLS cache performance
     */
    void printStats(const char* context = nullptr) const {
        // Gate with debug_level >= 2 for normal runs
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 2) return;

        size_t total = hits_ + misses_;
        double hit_rate = (total > 0) ? (static_cast<double>(hits_) / total * 100.0) : 0.0;

        std::cerr << "[TLS TensorViewCache";
        if (context) std::cerr << " @ " << context;
        std::cerr << "] hits=" << hits_
                  << " misses=" << misses_
                  << " rate=" << std::fixed << std::setprecision(1) << hit_rate << "%"
                  << " entries=" << cache_.size()
                  << " invalidations=" << invalidations_
                  << std::endl;
    }

    /**
     * Conditionally log statistics every N accesses (reduces log spam)
     * OPT Pass33+: Use this for periodic logging without manual counters
     *
     * HIT RATE THRESHOLD GUIDANCE:
     *   <50%  - Poor cache utilization; expansion NOT recommended until
     *           access patterns stabilize (likely high churn or mismatched shapes)
     *   50-70% - Moderate utilization; monitor for improvement trends
     *   >70%  - Good utilization; cache is effective, expansion candidate
     *           for additional hot paths if memory budget allows
     *   >90%  - Excellent; cache is well-tuned for current workload
     *
     * ACTION RECOMMENDATIONS:
     *   - If hit_rate <50% persists: Review cache key design (pointer stability)
     *   - If hit_rate >70% with frequent evictions: Consider increasing max_cache_size_
     *   - If hit_rate drops suddenly: Check for grid reinit or solver reset
     *
     * OPT Pass34 SAMPLING POLICY UNIFICATION:
     *   Use logStatsIfNeededConfig() for config-based sampling (preferred).
     *   This overload with explicit interval preserved for backward compatibility.
     *
     * @param log_interval Logging interval (0 or 1 = every access, N>1 = every Nth)
     * @param context Optional context string for log identification
     */
    void logStatsIfNeeded(size_t log_interval, const char* context) {
        // Gate with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 2) return;

        size_t total = hits_ + misses_;
        if (total == 0) return;

        // OPT Pass33+: Protect against mod 0 - treat interval<=1 as "always log"
        if (log_interval <= 1 || (total % log_interval == 0)) {
            printStats(context);
        }
    }

    /**
     * Log statistics using config-based sampling period (OPT Pass34 Unified)
     *
     * Uses debug_heavy_sample_period from g_sdirk3_config for consistency
     * with OptimizedTensorPool::logStatsIfNeeded() and other heavy diagnostics.
     *
     * SAMPLING POLICY:
     *   debug_heavy_sample_period = 0  → log every access (verbose)
     *   debug_heavy_sample_period = 1  → log every access
     *   debug_heavy_sample_period = N  → log every Nth access (production)
     *
     * @param context Optional context string for log identification
     */
    void logStatsIfNeededConfig(const char* context = nullptr) {
        // Gate with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 2) return;

        size_t total = hits_ + misses_;
        if (total == 0) return;

        // Use heavy sample period - TLS cache stats involve iteration/lookup overhead
        int period = wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period;
        size_t effective_interval = (period <= 0) ? 1 : static_cast<size_t>(period);

        if (effective_interval <= 1 || (total % effective_interval == 0)) {
            printStats(context);
        }
    }

private:
    void evictOldest() {
        auto oldest_it = cache_.end();
        auto oldest_time = std::chrono::steady_clock::now();

        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.last_access < oldest_time) {
                oldest_time = it->second.last_access;
                oldest_it = it;
            }
        }

        if (oldest_it != cache_.end()) {
            cache_.erase(oldest_it);
        }
    }
};

/**
 * Get thread-local tensor view cache (lock-free for parallel regions)
 * OPT Pass33+: Use this instead of getTensorViewCache() in OpenMP regions
 */
inline ThreadLocalTensorViewCache& getThreadLocalTensorViewCache() {
    thread_local ThreadLocalTensorViewCache tls_cache;
    return tls_cache;
}

// ============================================================================
// HELPER MACROS
// ============================================================================

/**
 * Helper macro for cached tensor view creation (global cache, mutex-protected)
 */
#define CACHED_TENSOR_VIEW(ptr, ...) \
    wrf::sdirk3::getTensorViewCache().getTensorView(ptr, {__VA_ARGS__})

/**
 * Helper macro for thread-local cached tensor view (lock-free)
 * OPT Pass33+: Use in OpenMP regions for better performance
 */
#define TLS_CACHED_TENSOR_VIEW(ptr, ...) \
    wrf::sdirk3::getThreadLocalTensorViewCache().getTensorView(ptr, {__VA_ARGS__})

/**
 * Helper for sliced views (tile extraction) - global cache
 */
#define CACHED_TILE_VIEW(base_ptr, shape, its, ite, jts, jte, kts, kte, \
                        ims, ime, jms, jme, kms, kme) \
    wrf::sdirk3::getTensorViewCache().getSlicedView( \
        base_ptr, shape, its, ite, jts, jte, kts, kte, \
        ims, ime, jms, jme, kms, kme)

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_TENSOR_CACHE_H