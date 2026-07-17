/**
 * wrf_sdirk3_mpi_safety_impl.cpp
 *
 * FIX 2025-01-26: Provides extern "C" symbol definitions for Fortran linkage.
 *
 * NOTE: Fortran name mangling varies by platform:
 *   - Linux GFortran: adds trailing underscore (sdirk3_notify_halo_fresh_)
 *   - macOS GFortran: NO trailing underscore (sdirk3_notify_halo_fresh)
 *
 * This file provides BOTH versions for cross-platform compatibility.
 */

#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include "wrf_sdirk3_mpi_safety.h"
#include "wrf_sdirk3_contract_fail.h"
#include <cstdlib>  // PR 9C.2: production fail route


extern "C" {

// =============================================================================
// Primary implementations (without trailing underscore - for macOS GFortran)
// =============================================================================

// PR 7B (3b-3 P1): markHaloFresh enforces the baseline-thread publication
// contract by throwing, and a C++ exception must never cross the C ABI.
// Active Fortran consumes this status; the legacy void spellings cannot
// report the violation, so they abort in a coordinated way instead.
int sdirk3_notify_halo_fresh_checked(void) noexcept {
    try {
        wrf::sdirk3::mpi_safety::HaloFreshnessGuard::markHaloFresh();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 0;
    } catch (...) {
        std::cerr << "SDIRK3_MPI_HALO_FRESH_NOTIFY_FAILED: non-std exception"
                  << std::endl;
        return 0;
    }
}

void sdirk3_notify_halo_fresh(void) noexcept {
    if (sdirk3_notify_halo_fresh_checked() != 1) {
        wrf::sdirk3::mpi_safety::abort_c_abi_exception(
            "sdirk3_notify_halo_fresh",
            "checked freshness publication failed");
    }
}

void sdirk3_set_timestep(int64_t* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_set_timestep_i4(int* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_mpi_safety_init(void) {
    wrf::sdirk3::mpi_safety::establish_mpi_baseline_thread("sdirk3_mpi_safety_init");
    wrf::sdirk3::mpi_safety::initializeMPISafety();
    // PR 9C.2/9C.3: production W-damping contract violations route to the
    // coordinated controlled abort. Since PR 9C.3 the final link CAN unwind
    // C++ exceptions again, but the controlled-abort policy stands (single
    // marker discipline + coordinated multi-rank stop).
    // WRF_SDIRK3_EH_SEAL_PROBE=1 (test-only, default OFF) skips the handler
    // install so a contract violation THROWS instead — the standing
    // integration negative uses it to prove the v2 ABI seal is LIVE under
    // the restored link (catch -> FATAL_INTERNAL outcome -> wrf_error_fatal,
    // no SDIRK3_C_ABI_EXCEPTION, no uncaught-terminate).
    const char* seal_probe = std::getenv("WRF_SDIRK3_EH_SEAL_PROBE");
    if (!(seal_probe && seal_probe[0] == '1')) {
        wrf::sdirk3::wdamp_contract_fail_handler() = [](const char* what) {
            wrf::sdirk3::mpi_safety::abort_c_abi_exception(
                "enabled W-damping contract", what);
        };
    }
}

// =============================================================================
// Alternate symbols (with trailing underscore - for Linux GFortran)
// =============================================================================

void sdirk3_notify_halo_fresh_(void) noexcept {
    // Delegate to the primary spelling so the two ABI names can never
    // drift apart (the baseline-wrapper drift class of defect).
    sdirk3_notify_halo_fresh();
}

void sdirk3_set_timestep_(int64_t* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_set_timestep_i4_(int* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_mpi_safety_init_(void) {
    // Same contract as the BIND(C) spelling: an underscore-ABI caller must
    // establish the baseline too, or every later MPIExchangeScope fails.
    sdirk3_mpi_safety_init();
}

} // extern "C"

// ═══════════════════════════════════════════════════════════════════════════
// PR 7B (3b-2): MPIExchangeScope — THE single-flight state for exchange and
// lifecycle. Defined once here so forward, adjoint, and lifecycle share it.
// ═══════════════════════════════════════════════════════════════════════════
#include <mutex>
#include <thread>

namespace wrf { namespace sdirk3 { namespace mpi_safety {
namespace {

std::mutex g_exchange_mutex;                 // held by the outermost scope only
std::thread::id g_owner_thread;              // valid while g_depth > 0
int g_depth = 0;                             // same-thread nesting depth
MPIExchangeKind g_outer_kind = MPIExchangeKind::FieldPrimitive;
std::atomic<bool> g_baseline_set{false};
std::mutex g_baseline_mutex;                 // serializes FIRST publication
std::thread::id g_baseline_thread;           // the MPI baseline (init) thread
int g_mpi_thread_level = -1;                 // MPI_Query_thread result, if any

const char* kind_name(MPIExchangeKind k) {
    switch (k) {
        case MPIExchangeKind::FieldPrimitive: return "FieldPrimitive";
        case MPIExchangeKind::ForwardBatch:   return "ForwardBatch";
        case MPIExchangeKind::AdjointBatch:   return "AdjointBatch";
        case MPIExchangeKind::Lifecycle:      return "Lifecycle";
    }
    return "?";
}

}  // namespace

void establish_mpi_baseline_thread(const char* who) noexcept {
    // First publication is race-free (P2): two threads racing the initial
    // establishment must not both write g_baseline_thread.
    std::lock_guard<std::mutex> lock(g_baseline_mutex);
    if (g_baseline_set.load()) {
        if (std::this_thread::get_id() != g_baseline_thread) {
            abort_c_abi_exception("establish_mpi_baseline_thread",
                "re-establishment attempted from a different thread");
        }
        return;  // idempotent from the baseline thread itself
    }
    g_baseline_thread = std::this_thread::get_id();
#ifdef DMPARALLEL
    int inited = 0, finalized = 0;
    MPI_Initialized(&inited);
    MPI_Finalized(&finalized);
    if (inited && !finalized &&
        MPI_Query_thread(&g_mpi_thread_level) == MPI_SUCCESS) {
        std::cerr << "SDIRK3: MPI baseline thread established by " << who
                  << ", MPI thread level " << g_mpi_thread_level
                  << " (single-flight contract enforced regardless)" << std::endl;
    }
#endif
    g_baseline_set.store(true);
}


int mpi_baseline_thread_level() noexcept {
    return g_mpi_thread_level;
}

bool is_mpi_baseline_thread() noexcept {
    return g_baseline_set.load() &&
           std::this_thread::get_id() == g_baseline_thread;
}

MPIExchangeScope::MPIExchangeScope(MPIExchangeKind kind, const char* operation) {
    if (!g_baseline_set.load()) {
        // NEVER adopt the first caller: the tile-worker lazy init would
        // canonize a worker thread. Fail closed until the authoritative
        // establishment ran.
        throw std::runtime_error(std::string(
            "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION: ") + operation +
            " entered before the MPI baseline thread was established "
            "(sdirk3_mpi_safety_init must run first)");
    }
    const auto self = std::this_thread::get_id();
    if (self != g_baseline_thread) {
        // Rejected BEFORE any MPI call is made from this thread.
        throw std::runtime_error(std::string(
            "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION: ") + operation +
            " entered from a thread other than the MPI baseline thread");
    }
    if (g_depth > 0) {
        // Same thread (other threads were rejected above). The ONLY legal
        // nesting is a field primitive inside a forward/adjoint batch.
        const bool legal =
            kind == MPIExchangeKind::FieldPrimitive &&
            (g_outer_kind == MPIExchangeKind::ForwardBatch ||
             g_outer_kind == MPIExchangeKind::AdjointBatch);
        if (!legal) {
            throw std::runtime_error(std::string(
                "SDIRK3_MPI_CONCURRENT_EXCHANGE_UNSUPPORTED: ") + operation +
                " (" + kind_name(kind) + ") entered while " +
                kind_name(g_outer_kind) + " is active");
        }
        ++g_depth;
        return;
    }
    if (!g_exchange_mutex.try_lock()) {
        // Another thread holds the outer scope: fail closed, never wait.
        throw std::runtime_error(std::string(
            "SDIRK3_MPI_CONCURRENT_EXCHANGE_UNSUPPORTED: ") + operation +
            " while another exchange/lifecycle operation is in flight");
    }
    g_owner_thread = self;
    g_outer_kind = kind;
    g_depth = 1;
}

MPIExchangeScope::~MPIExchangeScope() noexcept {
    if (--g_depth == 0) {
        g_exchange_mutex.unlock();
    }
}

}}}  // namespace wrf::sdirk3::mpi_safety
