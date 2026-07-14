/**
 * @file wrf_sdirk3_mpi_safety.h
 * @brief MPI Safety Guards and Validation for WRF-SDIRK3
 *
 * FIX 2025-01-25: Comprehensive safety guards for potential MPI/halo issues:
 * 1. Halo ownership overlap prevention (Fortran MPI vs C++ tile)
 * 2. MPI exchange timing synchronization (stale halo prevention)
 * 3. Tile partition boundary validation (off-by-one detection)
 * 4. Periodic BC single-application guard
 * 5. Threading+MPI safety (MPI_THREAD_FUNNELED compliance)
 *
 * LOCATION: external/libtorch_wrf/sdirk3/wrf_sdirk3_mpi_safety.h
 */

#ifndef WRF_SDIRK3_MPI_SAFETY_H
#define WRF_SDIRK3_MPI_SAFETY_H

// Build-system compatibility:
// Fortran preprocessor uses DM_PARALLEL, while C++ guards often use DMPARALLEL.
// Alias DM_PARALLEL to DMPARALLEL to keep MPI safety paths consistent.
#if defined(DM_PARALLEL) && !defined(DMPARALLEL)
#define DMPARALLEL 1
#endif

#include <torch/torch.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cstdlib>   // FIX 2025-01-25: For std::abort()
#include <cstdio>    // PR 7B: abort_c_abi_exception (no-throw fprintf)
#include <iostream>  // FIX 2025-01-25: For std::cerr warnings
#include <sstream>    // PR 7B: mpi_safety::check message assembly
#include <stdexcept>  // PR 7B: mpi_safety::check throws

#ifdef DMPARALLEL
#include <mpi.h>
#endif

namespace wrf {
namespace sdirk3 {
namespace mpi_safety {

// ═══════════════════════════════════════════════════════════════════════════
// ISSUE #1: HALO OWNERSHIP OVERLAP PREVENTION
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: Both Fortran (WRF MPI) and C++ (SDIRK3) could modify halo cells,
// causing data corruption or race conditions.
//
// POLICY: WRF handles ALL halo exchanges at MPI level.
//         SDIRK3 operates on INTERIOR points ONLY.
//
// ENFORCEMENT: This guard validates that SDIRK3 operations stay within
// the interior region [its:ite, jts:jte, kts:kte].
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Validate that an operation stays within interior tile bounds
 * @param i, j, k Current indices
 * @param its, ite, jts, jte, kts, kte Interior tile bounds
 * @return true if indices are within interior, false if in halo region
 *
 * FIX 2025-01-25: Halo ownership overlap prevention
 */
inline bool isWithinInterior(
    int i, int j, int k,
    int its, int ite, int jts, int jte, int kts, int kte)
{
    return (i >= its && i <= ite &&
            j >= jts && j <= jte &&
            k >= kts && k <= kte);
}

/**
 * @brief Check if a tensor operation might touch halo regions
 * @param tensor Input tensor [nj, nk, ni] in (j,k,i) layout
 * @param its, ite, jts, jte, kts, kte Interior tile bounds
 * @param ims, ime, jms, jme, kms, kme Memory bounds
 * @return HaloAccessWarning enum value
 *
 * FIX 2025-01-25: Detect potential halo ownership conflicts
 */
enum class HaloAccessStatus {
    INTERIOR_ONLY,      // Safe: operation stays within interior
    TOUCHES_WEST_HALO,  // Warning: accesses west halo region
    TOUCHES_EAST_HALO,  // Warning: accesses east halo region
    TOUCHES_SOUTH_HALO, // Warning: accesses south halo region
    TOUCHES_NORTH_HALO, // Warning: accesses north halo region
    FULL_DOMAIN         // Info: full domain access (expected for halo exchange)
};

inline HaloAccessStatus checkHaloAccess(
    int i_start, int i_end,
    int j_start, int j_end,
    int its, int ite, int jts, int jte,
    int ims, int ime, int jms, int jme)
{
    // Check if accessing outside interior
    if (i_start < its && i_start >= ims) return HaloAccessStatus::TOUCHES_WEST_HALO;
    if (i_end > ite && i_end <= ime) return HaloAccessStatus::TOUCHES_EAST_HALO;
    if (j_start < jts && j_start >= jms) return HaloAccessStatus::TOUCHES_SOUTH_HALO;
    if (j_end > jte && j_end <= jme) return HaloAccessStatus::TOUCHES_NORTH_HALO;

    // Check if full domain
    if (i_start == ims && i_end == ime && j_start == jms && j_end == jme) {
        return HaloAccessStatus::FULL_DOMAIN;
    }

    return HaloAccessStatus::INTERIOR_ONLY;
}

// ═══════════════════════════════════════════════════════════════════════════
// ISSUE #2: MPI EXCHANGE TIMING SYNCHRONIZATION
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: If SDIRK3 reads halo data before WRF's MPI exchange completes,
// stale data may be used.
//
// SOLUTION: Add synchronization fence between WRF halo exchange and SDIRK3
// computation. Track halo freshness with epoch counter.
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Halo freshness tracker to prevent stale halo data usage
 *
 * FIX 2025-01-25: MPI exchange timing synchronization
 * FIX 2025-01-25: Single-rank/non-MPI runs return OK without warnings
 */
class HaloFreshnessGuard {
public:
    // Epoch counter - incremented after each WRF halo exchange
    static std::atomic<uint64_t> halo_exchange_epoch;

    // Last epoch when SDIRK3 consumed halo data
    static thread_local uint64_t last_consumed_epoch;

    // FIX 2025-01-25: Track if MPI is active (set by init, cleared by finalize)
    // Single-rank/non-MPI runs don't need halo freshness tracking
    static std::atomic<bool> mpi_active;

    // Telemetry counters (v20.14rXX): halo mark/verify/stale event volumes.
    static std::atomic<uint64_t> halo_mark_events;
    static std::atomic<uint64_t> halo_verify_events;
    static std::atomic<uint64_t> halo_stale_events;

    /**
     * @brief Mark MPI as active (call from halo_exchange_init)
     */
    static void setMPIActive(bool active) {
        mpi_active.store(active, std::memory_order_release);
    }

    /**
     * @brief Call this after WRF completes halo exchange
     */
    static void markHaloFresh() {
        halo_mark_events.fetch_add(1, std::memory_order_relaxed);
        halo_exchange_epoch.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief Call before SDIRK3 reads from halo region
     * @return true if halo is fresh, false if potentially stale
     *
     * FIX 2025-01-25: Always returns true for single-rank/non-MPI runs
     */
    static bool isHaloFresh() {
        // Single-rank/non-MPI: no halo exchange needed, always "fresh"
        if (!mpi_active.load(std::memory_order_acquire)) {
            return true;
        }

        uint64_t current = halo_exchange_epoch.load(std::memory_order_acquire);
        if (current > last_consumed_epoch) {
            last_consumed_epoch = current;
            return true;
        }
        // Halo might be stale if epoch hasn't advanced
        return false;
    }

    /**
     * @brief Verify halo freshness with optional warning
     * @param warn_if_stale Print warning if halo might be stale
     * @return true if fresh, false if stale
     *
     * FIX 2025-01-25: No warnings for single-rank/non-MPI runs
     */
    static bool verifyFreshness(bool warn_if_stale = true) {
        // Single-rank/non-MPI: skip warning, always OK
        if (!mpi_active.load(std::memory_order_acquire)) {
            return true;
        }

        halo_verify_events.fetch_add(1, std::memory_order_relaxed);
        const bool fresh = isHaloFresh();
        if (!fresh) {
            halo_stale_events.fetch_add(1, std::memory_order_relaxed);
            static std::atomic<int> warn_count{0};
            if (warn_if_stale && warn_count.fetch_add(1, std::memory_order_relaxed) < 10) {
                std::cerr << "[SDIRK3 MPI SAFETY] Warning: Reading potentially stale halo data. "
                          << "Ensure WRF halo exchange completes before SDIRK3 computation."
                          << std::endl;
            }
            return false;
        }
        return true;
    }

    static uint64_t getMarkEventCount() {
        return halo_mark_events.load(std::memory_order_relaxed);
    }

    static uint64_t getVerifyEventCount() {
        return halo_verify_events.load(std::memory_order_relaxed);
    }

    static uint64_t getStaleEventCount() {
        return halo_stale_events.load(std::memory_order_relaxed);
    }

    static void resetEventCounters() {
        halo_mark_events.store(0, std::memory_order_relaxed);
        halo_verify_events.store(0, std::memory_order_relaxed);
        halo_stale_events.store(0, std::memory_order_relaxed);
    }
};

// Static member definitions (in .cpp file or here for header-only)
inline std::atomic<uint64_t> HaloFreshnessGuard::halo_exchange_epoch{0};
inline thread_local uint64_t HaloFreshnessGuard::last_consumed_epoch{0};
inline std::atomic<bool> HaloFreshnessGuard::mpi_active{false};
inline std::atomic<uint64_t> HaloFreshnessGuard::halo_mark_events{0};
inline std::atomic<uint64_t> HaloFreshnessGuard::halo_verify_events{0};
inline std::atomic<uint64_t> HaloFreshnessGuard::halo_stale_events{0};

// ═══════════════════════════════════════════════════════════════════════════
// ISSUE #3: TILE PARTITION BOUNDARY VALIDATION
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: Off-by-one errors in tile boundary calculation can cause:
// - Gaps: Some cells never computed
// - Overlaps: Some cells computed twice (race condition)
//
// SOLUTION: Validate tile bounds at solver entry point
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Validate tile boundary consistency
 * @return true if boundaries are valid, false if off-by-one detected
 *
 * FIX 2025-01-25: Tile partition boundary validation
 */
struct TileBoundaryValidation {
    bool valid;
    const char* error_message;

    // Validation checks
    bool its_ite_order;     // its <= ite
    bool jts_jte_order;     // jts <= jte
    bool kts_kte_order;     // kts <= kte
    bool its_in_domain;     // its >= ids
    bool ite_in_domain;     // ite <= ide
    bool jts_in_domain;     // jts >= jds
    bool jte_in_domain;     // jte <= jde
    bool kts_in_domain;     // kts >= kds
    bool kte_in_domain;     // kte <= kde
    bool its_in_memory;     // its >= ims
    bool ite_in_memory;     // ite <= ime
    bool jts_in_memory;     // jts >= jms
    bool jte_in_memory;     // jte <= jme
    bool kts_in_memory;     // kts >= kms
    bool kte_in_memory;     // kte <= kme
};

inline TileBoundaryValidation validateTileBoundaries(
    int its, int ite, int jts, int jte, int kts, int kte,
    int ids, int ide, int jds, int jde, int kds, int kde,
    int ims, int ime, int jms, int jme, int kms, int kme)
{
    TileBoundaryValidation result;
    result.valid = true;
    result.error_message = nullptr;

    // Order checks (detect off-by-one causing invalid ranges)
    result.its_ite_order = (its <= ite);
    result.jts_jte_order = (jts <= jte);
    result.kts_kte_order = (kts <= kte);

    // Domain containment (tile within domain)
    result.its_in_domain = (its >= ids);
    result.ite_in_domain = (ite <= ide);
    result.jts_in_domain = (jts >= jds);
    result.jte_in_domain = (jte <= jde);
    result.kts_in_domain = (kts >= kds);
    result.kte_in_domain = (kte <= kde);

    // Memory containment (domain within memory)
    result.its_in_memory = (its >= ims);
    result.ite_in_memory = (ite <= ime);
    result.jts_in_memory = (jts >= jms);
    result.jte_in_memory = (jte <= jme);
    result.kts_in_memory = (kts >= kms);
    result.kte_in_memory = (kte <= kme);

    // Check all conditions
    if (!result.its_ite_order) {
        result.valid = false;
        result.error_message = "Off-by-one: its > ite (invalid x-range)";
    } else if (!result.jts_jte_order) {
        result.valid = false;
        result.error_message = "Off-by-one: jts > jte (invalid y-range)";
    } else if (!result.kts_kte_order) {
        result.valid = false;
        result.error_message = "Off-by-one: kts > kte (invalid z-range)";
    } else if (!result.its_in_domain || !result.ite_in_domain) {
        result.valid = false;
        result.error_message = "Tile x-bounds outside domain";
    } else if (!result.jts_in_domain || !result.jte_in_domain) {
        result.valid = false;
        result.error_message = "Tile y-bounds outside domain";
    } else if (!result.kts_in_domain || !result.kte_in_domain) {
        result.valid = false;
        result.error_message = "Tile z-bounds outside domain";
    } else if (!result.its_in_memory || !result.ite_in_memory ||
               !result.jts_in_memory || !result.jte_in_memory ||
               !result.kts_in_memory || !result.kte_in_memory) {
        result.valid = false;
        result.error_message = "Tile bounds outside memory allocation";
    }

    return result;
}

/**
 * @brief Macro to validate tile boundaries at function entry
 */
#define SDIRK3_VALIDATE_TILE_BOUNDS(its, ite, jts, jte, kts, kte, \
                                     ids, ide, jds, jde, kds, kde, \
                                     ims, ime, jms, jme, kms, kme) \
    do { \
        auto _tile_validation = wrf::sdirk3::mpi_safety::validateTileBoundaries( \
            its, ite, jts, jte, kts, kte, \
            ids, ide, jds, jde, kds, kde, \
            ims, ime, jms, jme, kms, kme); \
        TORCH_CHECK(_tile_validation.valid, \
            "SDIRK3 Tile Boundary Error: ", _tile_validation.error_message, \
            " its=", its, " ite=", ite, " jts=", jts, " jte=", jte, \
            " kts=", kts, " kte=", kte); \
    } while(0)

// ═══════════════════════════════════════════════════════════════════════════
// ISSUE #4: PERIODIC BC SINGLE-APPLICATION GUARD
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: If both WRF Fortran and SDIRK3 C++ apply periodic boundary
// conditions, values may be corrupted by double-application.
//
// POLICY: WRF handles ALL boundary conditions including periodic.
//         SDIRK3 should NOT apply periodic BC on its own.
//
// GUARD: Track if periodic BC was already applied to prevent double-apply
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Guard to prevent double-application of periodic BC
 *
 * FIX 2025-01-25: Periodic BC single-application guard
 */
class PeriodicBCGuard {
public:
    // Track which fields have had periodic BC applied in current timestep
    static thread_local uint64_t current_timestep;
    static thread_local uint64_t periodic_applied_mask;

    // Field bit masks
    enum FieldMask : uint64_t {
        FIELD_U      = 1ULL << 0,
        FIELD_V      = 1ULL << 1,
        FIELD_W      = 1ULL << 2,
        FIELD_PH     = 1ULL << 3,
        FIELD_T      = 1ULL << 4,
        FIELD_MU     = 1ULL << 5,
        FIELD_MOIST  = 1ULL << 6,
        FIELD_ALL    = 0xFFFFFFFFFFFFFFFFULL
    };

    /**
     * @brief Reset guard for new timestep
     */
    static void newTimestep(uint64_t timestep) {
        if (timestep != current_timestep) {
            current_timestep = timestep;
            periodic_applied_mask = 0;
        }
    }

    /**
     * @brief Check if periodic BC can be applied (not already done)
     * @param field_mask Which field(s) to check
     * @return true if BC should be applied, false if already done
     */
    static bool canApplyPeriodic(uint64_t field_mask) {
        return (periodic_applied_mask & field_mask) == 0;
    }

    /**
     * @brief Mark periodic BC as applied for a field
     */
    static void markApplied(uint64_t field_mask) {
        periodic_applied_mask |= field_mask;
    }

    /**
     * @brief Combined check-and-mark for atomic operation
     * @return true if this caller should apply BC, false if already done
     */
    static bool tryApplyPeriodic(uint64_t field_mask) {
        if (canApplyPeriodic(field_mask)) {
            markApplied(field_mask);
            return true;
        }
        return false;
    }
};

inline thread_local uint64_t PeriodicBCGuard::current_timestep{0};
inline thread_local uint64_t PeriodicBCGuard::periodic_applied_mask{0};

// ═══════════════════════════════════════════════════════════════════════════
// ISSUE #5: THREADING + MPI SAFETY (MPI_THREAD_FUNNELED COMPLIANCE)
// ═══════════════════════════════════════════════════════════════════════════
//
// PROBLEM: WRF uses MPI_THREAD_FUNNELED, meaning only the master thread
// can make MPI calls. If SDIRK3 OpenMP threads call MPI, undefined behavior.
//
// POLICY: Only call MPI from master thread or outside parallel regions.
//         Use thread-local buffers, not MPI, for inter-thread communication.
//
// GUARD: Detect and warn if MPI called from non-master thread
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Thread-safe MPI call guard for MPI_THREAD_FUNNELED compliance
 *
 * FIX 2025-01-25: Threading+MPI safety guard
 */
class MPIThreadGuard {
public:
    // Master thread ID (set once at initialization)
    static std::thread::id master_thread_id;
    static std::atomic<bool> initialized;

    /**
     * @brief Initialize the master thread ID (call from main thread)
     */
    static void initialize() {
        if (!initialized.exchange(true, std::memory_order_acq_rel)) {
            master_thread_id = std::this_thread::get_id();
        }
    }

    /**
     * @brief Check if current thread is the master thread
     */
    static bool isMasterThread() {
        return std::this_thread::get_id() == master_thread_id;
    }

    /**
     * @brief Verify MPI call is from master thread
     * @param func_name Name of calling function for error message
     */
    static void verifyMasterThread(const char* func_name) {
        if (!isMasterThread()) {
            static std::atomic<int> warn_count{0};
            if (warn_count.fetch_add(1, std::memory_order_relaxed) < 10) {
                std::cerr << "[SDIRK3 MPI SAFETY] ERROR: MPI call from non-master thread in "
                          << func_name << ". WRF uses MPI_THREAD_FUNNELED - only master thread "
                          << "can make MPI calls!" << std::endl;
            }
            // In debug mode, abort
#ifndef NDEBUG
            std::abort();
#endif
        }
    }

    /**
     * @brief Check if we're in an OpenMP parallel region
     */
    static bool inParallelRegion() {
#ifdef _OPENMP
        return omp_in_parallel() != 0;
#else
        return false;
#endif
    }

    /**
     * @brief Safe MPI wrapper that checks thread safety
     */
    static void assertMPISafe(const char* func_name) {
#ifdef DMPARALLEL
        if (inParallelRegion() && !isMasterThread()) {
            std::cerr << "[SDIRK3 MPI SAFETY] FATAL: " << func_name
                      << " called from OpenMP worker thread. This violates "
                      << "MPI_THREAD_FUNNELED contract!" << std::endl;
#ifndef NDEBUG
            std::abort();
#endif
        }
#endif
    }
};

inline std::thread::id MPIThreadGuard::master_thread_id;
inline std::atomic<bool> MPIThreadGuard::initialized{false};

/**
 * @brief Macro to guard MPI calls
 */
#ifdef DMPARALLEL
#define SDIRK3_MPI_THREAD_GUARD(func_name) \
    wrf::sdirk3::mpi_safety::MPIThreadGuard::assertMPISafe(func_name)
#else
#define SDIRK3_MPI_THREAD_GUARD(func_name) ((void)0)
#endif

// ═══════════════════════════════════════════════════════════════════════════
// COMPREHENSIVE SAFETY INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Initialize all MPI safety guards
 * Call this once from main thread before any SDIRK3 operations
 *
 * FIX 2025-01-25: Auto-detect single-rank/non-MPI runs
 */
inline void initializeMPISafety() {
    MPIThreadGuard::initialize();
    HaloFreshnessGuard::halo_exchange_epoch.store(0, std::memory_order_release);
    HaloFreshnessGuard::resetEventCounters();

    // FIX 2025-01-25: Auto-detect if MPI is active with multiple ranks
    bool mpi_active = false;
#ifdef DMPARALLEL
    int mpi_initialized = 0;
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
        int mpi_size = 1;
        // Explicit checked branch (this helper predates SDIRK3_MPI_SAFETY_CHECK
        // and must not throw): a failed size query must not silently read as
        // single-rank, which would disable halo freshness tracking.
        if (MPI_Comm_size(MPI_COMM_WORLD, &mpi_size) != MPI_SUCCESS) {
            std::cerr << "SDIRK3_MPI_CALL_FAILED: MPI_Comm_size in setMPIActiveFromEnv"
                      << std::endl;
            std::abort();
        }
        // Only activate halo freshness tracking if multiple ranks
        mpi_active = (mpi_size > 1);
    }
#endif
    HaloFreshnessGuard::setMPIActive(mpi_active);
}

/**
 * @brief Call from Fortran after WRF halo exchange completes
 * This signals SDIRK3 that halo data is fresh
 *
 * FIX 2025-01-25: No-op for single-rank runs (no halo exchange needed)
 */
inline void notifyHaloExchangeComplete() {
    // Only mark fresh if MPI is active (multi-rank)
    // Single-rank runs skip this entirely (isHaloFresh always returns true)
    HaloFreshnessGuard::markHaloFresh();
}

// PR 7B: last line of defense at every extern "C" entry point. A C++
// exception must never unwind through the C ABI into Fortran (UB); legacy
// void ABIs cannot return a status, so the only honest outcome is a loud,
// coordinated stop. The marker is deliberately GENERIC — the MPI check's
// what() already carries SDIRK3_MPI_CALL_FAILED, and renaming every
// std::exception (shape validation, bad_alloc) as an MPI failure would
// misclassify. Multi-rank termination is coordinated via MPI_Abort so no
// peer rank deadlocks waiting on a dead sender; std::abort is the fallback.
[[noreturn]] inline void abort_c_abi_exception(const char* entry,
                                               const char* detail) noexcept {
    std::fprintf(stderr, "SDIRK3_C_ABI_EXCEPTION: %s: %s\n",
                 entry, detail ? detail : "unknown exception");
    std::fflush(stderr);
#ifdef DMPARALLEL
    int initialized = 0, finalized = 0;
    MPI_Initialized(&initialized);
    MPI_Finalized(&finalized);
    if (initialized && !finalized) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
#endif
    std::abort();
}

#ifdef DMPARALLEL
// PR 7B: the single authoritative MPI return-code check.
// The previous per-file macros compiled to `(void)(call)` under NDEBUG, so
// Release production discarded every MPI error code. This check is ALWAYS on
// (Debug and Release); the success path costs one integer compare.
// On failure it prints the stable marker and throws — the exception is for
// C++ callers; extern "C" entry points must catch before the ABI boundary.
inline void check(int rc, const char* expression, const char* file, int line) {
    if (rc == MPI_SUCCESS) return;
    int rank = -1, inited = 0, finalized = 0;
    char errstr[MPI_MAX_ERROR_STRING] = "(MPI error string unavailable)";
    int errlen = 0;
    MPI_Initialized(&inited);
    MPI_Finalized(&finalized);
    if (inited && !finalized) {
        // Only query MPI while it is safely queryable; otherwise report the
        // numeric code alone rather than risking a second failure.
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Error_string(rc, errstr, &errlen);
    }
    std::ostringstream oss;
    oss << "SDIRK3_MPI_CALL_FAILED: " << expression << " rc=" << rc
        << " rank=" << rank << " at " << file << ":" << line
        << " : " << errstr;
    std::cerr << oss.str() << std::endl;
    throw std::runtime_error(oss.str());
}
#endif  // DMPARALLEL

} // namespace mpi_safety
} // namespace sdirk3
} // namespace wrf

namespace wrf { namespace sdirk3 { namespace mpi_safety {

// PR 7B (3b-2): single-flight guard for every MPI exchange AND lifecycle
// operation. One program-global state (defined in wrf_sdirk3_mpi_safety_impl
// .cpp — forward and adjoint share it). Allowed nesting is ONLY
// ForwardBatch->FieldPrimitive and AdjointBatch->FieldPrimitive on the SAME
// thread; every other re-entry (Lifecycle->Lifecycle, Lifecycle<->exchange,
// FieldPrimitive->FieldPrimitive, any other-thread entry) fails closed
// immediately — never blocks (SDIRK3_MPI_CONCURRENT_EXCHANGE_UNSUPPORTED).
// A thread other than the recorded MPI baseline thread is rejected BEFORE
// any MPI call (SDIRK3_MPI_THREAD_CONTRACT_VIOLATION); MPI_THREAD_MULTIPLE
// does not relax this — production is single-flight by contract.
enum class MPIExchangeKind { FieldPrimitive, ForwardBatch, AdjointBatch, Lifecycle };

class MPIExchangeScope {
public:
    MPIExchangeScope(MPIExchangeKind kind, const char* operation);
    ~MPIExchangeScope() noexcept;
    MPIExchangeScope(const MPIExchangeScope&) = delete;
    MPIExchangeScope& operator=(const MPIExchangeScope&) = delete;
};

}}}  // namespace wrf::sdirk3::mpi_safety

#ifdef DMPARALLEL
#define SDIRK3_MPI_SAFETY_CHECK(call) \
    ::wrf::sdirk3::mpi_safety::check((call), #call, __FILE__, __LINE__)
#endif

// ═══════════════════════════════════════════════════════════════════════════
// C INTERFACE FOR FORTRAN INTEROP
// ═══════════════════════════════════════════════════════════════════════════
// FIX 2025-01-26: Changed from inline definitions to declarations only.
// Implementations are in wrf_sdirk3_mpi_safety_impl.cpp to ensure symbols
// are exported for Fortran linkage. Inline functions don't generate symbols.
// NOTE: Fortran passes arguments by reference (pointer), so these use pointers.

extern "C" {

/**
 * @brief Initialize MPI safety guards (call from Fortran main program)
 */
void sdirk3_mpi_safety_init(void);

/**
 * @brief Notify SDIRK3 that WRF halo exchange has completed
 * Call this from Fortran AFTER halo_exchange_finish
 */
void sdirk3_notify_halo_fresh(void);

/**
 * @brief Set current timestep for periodic BC guard (64-bit version)
 * @param timestep Pointer to timestep value (Fortran passes by reference)
 */
void sdirk3_set_timestep(int64_t* timestep);

/**
 * @brief Set current timestep for periodic BC guard (32-bit version for Fortran)
 * FIX 2025-01-25: Added for easy Fortran integration (grid%itimestep is INTEGER)
 * @param timestep Pointer to timestep value (Fortran passes by reference)
 */
void sdirk3_set_timestep_i4(int* timestep);

// Alternate symbols with trailing underscore for Linux GFortran compatibility
void sdirk3_mpi_safety_init_(void);
void sdirk3_notify_halo_fresh_(void);
void sdirk3_set_timestep_(int64_t* timestep);
void sdirk3_set_timestep_i4_(int* timestep);

} // extern "C"

#endif // WRF_SDIRK3_MPI_SAFETY_H
