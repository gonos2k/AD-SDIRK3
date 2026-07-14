/**
 * @file wrf_sdirk3_halo_exchange.cpp
 * @brief MPI halo exchange implementation for WRF-SDIRK3 solver
 * 
 * This file provides the actual MPI halo exchange implementation
 * that interfaces with WRF's communication infrastructure
 */

#include "wrf_sdirk3_halo_exchange.h"
#include "wrf_sdirk3_mpi_safety.h"  // PR 7B: shared always-on MPI check
#include "wrf_sdirk3_common_macros.h"  // OPT Pass34: For SDIRK3_MPI_CHECK
#include <torch/torch.h>
#include <vector>
#include <cstring>
#include <iostream>
#include <limits>   // FIX Round169: For std::numeric_limits
#include <memory>   // OPT Pass34: For std::unique_ptr
#include <atomic>   // PR 7B: g_halo_ready pre-check
#include <cstdlib>  // OPT Pass34: For std::atexit

#ifdef DMPARALLEL
#include <mpi.h>

// PR 7B: Release builds previously compiled this to (void)(call),
// discarding every MPI error code in production. Route to the single
// always-on check in wrf_sdirk3_mpi_safety.h.
#define SDIRK3_MPI_CHECK(mpi_call) SDIRK3_MPI_SAFETY_CHECK(mpi_call)

#endif // DMPARALLEL

namespace {
// FIX 2025-12-27: Helper to ensure tensor is CPU contiguous for direct pointer access
// This avoids per-element GPU sync when packing/unpacking halo data
// FIX Round179: Use !is_cpu() for HIP/XPU support, enforce FP32 for data_ptr<float>()
inline torch::Tensor ensure_cpu_contiguous(const torch::Tensor& t) {
    if (!t.is_cpu() || t.scalar_type() != torch::kFloat32) {
        return t.to(torch::kCPU, torch::kFloat32).contiguous();
    }
    return t.contiguous();
}

// Helper to calculate linear index for (j,k,i) layout
// FIX Round172: Use int64_t parameters to prevent narrowing on large grids
// Previously int parameters could truncate int64_t loop indices
inline int64_t jki_index(int64_t j, int64_t k, int64_t i, int64_t nk, int64_t ni) {
    return j * nk * ni + k * ni + i;
}
} // anonymous namespace

namespace wrf {
namespace sdirk3 {

// Internal data structure for managing halo regions
struct HaloExchangeImpl {
    // Grid dimensions
    int ids, ide, jds, jde, kds, kde;  // Domain dimensions
    int ims, ime, jms, jme, kms, kme;  // Memory dimensions
    int ips, ipe, jps, jpe, kps, kpe;  // Patch dimensions
    
    // Halo widths
    int halo_width_x;
    int halo_width_y;
    
    // MPI info
#ifdef DMPARALLEL
    // PR 7B: we never borrow WRF's communicator long-term. init() dups it
    // into owned_comm (with MPI_ERRORS_RETURN) and frees it exactly once on
    // finalize/re-init — never after MPI_Finalize.
    MPI_Comm owned_comm = MPI_COMM_NULL;
#endif
    int mpi_rank;
    int mpi_size;
    int nprocx, nprocy;  // Process grid dimensions
    int mypx, mypy;      // My position in process grid
    
    // Neighbor ranks
    int neighbor_north, neighbor_south;
    int neighbor_east, neighbor_west;
    int neighbor_ne, neighbor_nw;
    int neighbor_se, neighbor_sw;

    // FIX Round176: Removed unused MPI derived types and tracking variables
    // The actual halo communication uses MPI_FLOAT + packed buffers, not derived types.
    // Keeping derived types would just add overhead without benefit.

    // FIX Round187: Cached buffers for E/W halo exchange to reduce per-call allocations
    // Only resized when dimensions change (halo_width, nj, nk)
    std::vector<float> cached_send_buffer_east;
    std::vector<float> cached_recv_buffer_east;
    std::vector<float> cached_send_buffer_west;
    std::vector<float> cached_recv_buffer_west;
    int64_t cached_ew_buffer_size = 0;  // Track last allocated size

#ifdef DMPARALLEL
    MPI_Comm comm;
    bool has_cart_comm = false;  // True when using WRF's Cartesian communicator
#endif

    // Constructor: defers comm setup to halo_exchange_init().
    // Actual communicator (Cartesian from WRF or fallback MPI_COMM_WORLD) set during init.
    HaloExchangeImpl() : mpi_rank(0), mpi_size(1) {
#ifdef DMPARALLEL
        // PR 7B (3b-2 part 2): a not-yet-validated object must not carry a
        // usable communicator — MPI_COMM_NULL until init() publishes it.
        comm = MPI_COMM_NULL;
        has_cart_comm = false;
#endif
    }
    
    // Compute neighbor ranks based on process grid
    void compute_neighbors() {
        neighbor_north = neighbor_south = -1;
        neighbor_east = neighbor_west = -1;
        neighbor_ne = neighbor_nw = -1;
        neighbor_se = neighbor_sw = -1;
        
        if (mypy > 0) {
            neighbor_south = (mypy - 1) * nprocx + mypx;
        }
        if (mypy < nprocy - 1) {
            neighbor_north = (mypy + 1) * nprocx + mypx;
        }
        if (mypx > 0) {
            neighbor_west = mypy * nprocx + (mypx - 1);
        }
        if (mypx < nprocx - 1) {
            neighbor_east = mypy * nprocx + (mypx + 1);
        }
        
        // Corner neighbors
        if (neighbor_north >= 0 && neighbor_east >= 0) {
            neighbor_ne = (mypy + 1) * nprocx + (mypx + 1);
        }
        if (neighbor_north >= 0 && neighbor_west >= 0) {
            neighbor_nw = (mypy + 1) * nprocx + (mypx - 1);
        }
        if (neighbor_south >= 0 && neighbor_east >= 0) {
            neighbor_se = (mypy - 1) * nprocx + (mypx + 1);
        }
        if (neighbor_south >= 0 && neighbor_west >= 0) {
            neighbor_sw = (mypy - 1) * nprocx + (mypx - 1);
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// OPT Pass34: Global Singleton Lifetime Documentation
// ═══════════════════════════════════════════════════════════════════════════
// g_halo_impl is a module-level singleton managing MPI halo exchange state.
//
// LIFETIME CONTRACT:
//   - Created: First call to halo_exchange_init()
//   - Destroyed: Call to halo_exchange_finalize() (REQUIRED before MPI_Finalize)
//   - Recreate: Allowed - calling init() after finalize() creates new instance
//
// THREAD SAFETY:
//   - init/finalize: NOT thread-safe (call from main thread only)
//   - exchange ops: Thread-safe after init completes (MPI handles synchronization)
//
// MEMORY SAFETY:
//   - Using unique_ptr ensures cleanup if process exits abnormally (via atexit)
//   - Explicit finalize() still recommended before MPI_Finalize for clean shutdown
// ═══════════════════════════════════════════════════════════════════════════
static std::unique_ptr<HaloExchangeImpl> g_halo_impl = nullptr;
// PR 7B (review): race-free existence pre-check. The unique_ptr itself may
// only be read under an MPIExchangeScope; this atomic preserves the
// historical serial/uninitialized NO-OP without touching the pointer.
// It is a fast pre-check, not a lifetime lock — re-check g_halo_impl
// under the scope.
static std::atomic<bool> g_halo_ready{false};
// PR 7B (review P1): 'state published' and 'MPI exchange actually needed'
// are DIFFERENT facts — a fully-published single-rank serial state is ready
// but must keep the historical silent no-op on every exchange primitive
// (a tile worker calling a primitive on 1 rank needs no communication and
// must not die on the thread contract). requires-exchange is the primitives'
// pre-check; ready remains the is_initialized() answer.
static std::atomic<bool> g_halo_exchange_active{false};

// Epoch counter: incremented on every comm change or finalize event.
static uint64_t g_halo_exchange_epoch = 0;

// v11: Promoted from function-static to file-scope so set_wrf_communicator()
// can reset it when the comm changes, allowing re-init to print updated status.
static bool g_halo_init_logged = false;

#ifdef DMPARALLEL
static MPI_Fint g_wrf_fortran_comm = 0;
static bool g_wrf_comm_set = false;
static bool g_wrf_periodic_x = false;
static bool g_wrf_periodic_y = false;
// WRF standard builds use 4-byte default integers. -i8 / -fdefault-integer-8 NOT supported.
static_assert(sizeof(MPI_Fint) == sizeof(int),
    "MPI_Fint must match int size — required for WRF Fortran-C interop via integer(c_int)");
#endif

// OPT Pass34: atexit handler for safety cleanup if finalize() not called
// POLICY: If MPI already finalized, skip cleanup that might invoke MPI calls.
// Current HaloExchangeImpl destructor is MPI-free (no MPI_Type_free, etc.),
// but this check future-proofs against adding MPI cleanup in destructor.
static void halo_exchange_atexit_cleanup() {
    if (!g_halo_impl) {
        return;  // Already cleaned up via explicit finalize()
    }

#ifdef DMPARALLEL
    // Check if MPI is still active - if finalized, destructor might fail
    int mpi_finalized = 0;
    // Note: MPI_Finalized is safe to call even after MPI_Finalize
    if (MPI_Finalized(&mpi_finalized) == MPI_SUCCESS && mpi_finalized) {
        // MPI already finalized - just release pointer, skip any MPI cleanup
        // Current destructor is MPI-free, but this documents the policy
        g_halo_exchange_active.store(false, std::memory_order_release);
        g_halo_ready.store(false, std::memory_order_release);
        g_halo_impl.reset();
        return;
    }
#endif

    // MPI still active (or non-MPI build) - safe to do full cleanup.
    // CONTRACT NOTE (review P2): the owned-communicator exactly-once free is
    // guaranteed on the EXPLICIT finalize/re-init paths only. This atexit
    // safety net deliberately performs no MPI calls; at process exit the OS
    // reclaims the resource.
    g_halo_exchange_active.store(false, std::memory_order_release);
    g_halo_ready.store(false, std::memory_order_release);
    g_halo_impl.reset();
}

#ifdef DMPARALLEL
static void free_owned_comm(HaloExchangeImpl& impl) {
    if (impl.owned_comm == MPI_COMM_NULL) return;
    int mpi_finalized = 0;
    if (MPI_Finalized(&mpi_finalized) == MPI_SUCCESS && !mpi_finalized) {
        SDIRK3_MPI_CHECK(MPI_Comm_free(&impl.owned_comm));
    }
    impl.owned_comm = MPI_COMM_NULL;
}
#endif

// Initialize halo exchange
static void halo_exchange_init_impl(int ids, int ide, int jds, int jde, int kds, int kde,
                       int ims, int ime, int jms, int jme, int kms, int kme,
                       int ips, int ipe, int jps, int jpe, int kps, int kpe,
                       int nprocx, int nprocy, int mypx, int mypy,
                       int halo_width) {

    // OPT Pass34: Register atexit handler on first init (once per process)
    static bool atexit_registered = false;
    if (!atexit_registered) {
        std::atexit(halo_exchange_atexit_cleanup);
        atexit_registered = true;
    }

    // PR 7B (3b-2 part 2): TRANSACTIONAL publication. The old flow mutated
    // the global object first, so a validation failure left a half-written
    // impl behind and halo_exchange_is_initialized() reported true. Now a
    // candidate is fully built and validated; the global is replaced only
    // after everything passed. On failure the previous valid state (or the
    // uninitialized state) is preserved and the candidate's communicator is
    // rolled back without throwing.
    auto candidate = std::make_unique<HaloExchangeImpl>();
    auto& impl = *candidate;

#ifdef DMPARALLEL
    // noexcept rollback (3b-2 part 2): if any validation below throws, the
    // candidate is destroyed on unwind but its destructor is deliberately
    // MPI-free (atexit policy) — this guard frees a partially-duplicated
    // communicator instead. Disarmed at publish.
    struct CommRollback {
        HaloExchangeImpl* c;
        ~CommRollback() noexcept {
            if (!c || c->owned_comm == MPI_COMM_NULL) return;
            int fin = 0;
            if (MPI_Finalized(&fin) == MPI_SUCCESS && !fin) {
                MPI_Comm tmp = c->owned_comm;
                c->owned_comm = MPI_COMM_NULL;
                (void)MPI_Comm_free(&tmp);
            }
        }
    } comm_rollback{&impl};
#endif
    
    // Store dimensions
    impl.ids = ids; impl.ide = ide;
    impl.jds = jds; impl.jde = jde;
    impl.kds = kds; impl.kde = kde;
    
    impl.ims = ims; impl.ime = ime;
    impl.jms = jms; impl.jme = jme;
    impl.kms = kms; impl.kme = kme;
    
    impl.ips = ips; impl.ipe = ipe;
    impl.jps = jps; impl.jpe = jpe;
    impl.kps = kps; impl.kpe = kpe;
    
    impl.nprocx = nprocx;
    impl.nprocy = nprocy;
    impl.mypx = mypx;
    impl.mypy = mypy;
    
    impl.halo_width_x = halo_width;
    impl.halo_width_y = halo_width;

    // Compute neighbor ranks — use Cart comm if WRF provided one, else fallback
#ifdef DMPARALLEL
    if (g_wrf_comm_set) {
        // PR 7B: own a duplicate, never the borrowed Fortran handle. The dup
        // gets MPI_ERRORS_RETURN so every failure reaches the always-on check
        // instead of MPI's default process kill. (The PREVIOUS owned comm is
        // freed at publish time — the candidate starts with none.)
        MPI_Comm incoming = MPI_Comm_f2c(g_wrf_fortran_comm);
        SDIRK3_MPI_CHECK(MPI_Comm_dup(incoming, &impl.owned_comm));
        SDIRK3_MPI_CHECK(MPI_Comm_set_errhandler(impl.owned_comm, MPI_ERRORS_RETURN));
        impl.comm = impl.owned_comm;
        SDIRK3_MPI_CHECK(MPI_Comm_rank(impl.comm, &impl.mpi_rank));
        SDIRK3_MPI_CHECK(MPI_Comm_size(impl.comm, &impl.mpi_size));

        // Verify communicator is Cartesian before using Cart APIs.
        // WRF's local_communicator is always created via MPI_Cart_create.
        int topo_status = MPI_UNDEFINED;
        SDIRK3_MPI_CHECK(MPI_Topo_test(impl.comm, &topo_status));
        TORCH_CHECK(topo_status == MPI_CART,
            "set_wrf_communicator: expected Cartesian communicator but got topo_status=",
            topo_status, " (MPI_CART=", MPI_CART, "). "
            "Ensure local_communicator (from MPI_Cart_create) is passed.");
        impl.has_cart_comm = true;

        int cart_ndims = 0;
        SDIRK3_MPI_CHECK(MPI_Cartdim_get(impl.comm, &cart_ndims));
        TORCH_CHECK(cart_ndims == 2,
            "SDIRK3_MPI_COMM_TOPOLOGY_MISMATCH: Cartesian communicator has ",
            cart_ndims, " dimension(s), expected 2 (y,x)");
        int cart_dims[2], cart_periods[2], cart_coords[2];
        SDIRK3_MPI_CHECK(MPI_Cart_get(impl.comm, 2, cart_dims, cart_periods, cart_coords));
        // PR 7B: the WRF-declared decomposition must MATCH the communicator,
        // never be silently replaced by it — a mismatch means the caller's
        // patch arithmetic used a different grid than the one messages will
        // route on.
        TORCH_CHECK(impl.mpi_size == cart_dims[0] * cart_dims[1],
            "SDIRK3_MPI_COMM_TOPOLOGY_MISMATCH: comm size ", impl.mpi_size,
            " != cart dims ", cart_dims[0], "x", cart_dims[1]);
        TORCH_CHECK(impl.nprocy == cart_dims[0] && impl.nprocx == cart_dims[1],
            "SDIRK3_MPI_COMM_TOPOLOGY_MISMATCH: declared grid ", impl.nprocx,
            "x", impl.nprocy, " (x,y) != communicator ", cart_dims[1], "x",
            cart_dims[0]);
        TORCH_CHECK(impl.mypy == cart_coords[0] && impl.mypx == cart_coords[1],
            "SDIRK3_MPI_COMM_COORD_MISMATCH: declared position (", impl.mypx,
            ",", impl.mypy, ") != communicator coords (", cart_coords[1], ",",
            cart_coords[0], ")");

        int ym, yp, xm, xp;
        SDIRK3_MPI_CHECK(MPI_Cart_shift(impl.comm, 0, 1, &ym, &yp));
        SDIRK3_MPI_CHECK(MPI_Cart_shift(impl.comm, 1, 1, &xm, &xp));
        impl.neighbor_south = (ym == MPI_PROC_NULL) ? -1 : ym;
        impl.neighbor_north = (yp == MPI_PROC_NULL) ? -1 : yp;
        impl.neighbor_west  = (xm == MPI_PROC_NULL) ? -1 : xm;
        impl.neighbor_east  = (xp == MPI_PROC_NULL) ? -1 : xp;
        impl.neighbor_ne = impl.neighbor_nw = -1;
        impl.neighbor_se = impl.neighbor_sw = -1;

        // DEFENSIVE CHECK: Detect if a periodic comm was accidentally passed.
        // A non-periodic comm (local_communicator) has cart_periods={0,0}.
        // If periodic, MPI_Cart_shift already returned wrap-around neighbors
        // even for boundary processes. Override non-periodic directions to -1.
        if (cart_periods[0] != 0 || cart_periods[1] != 0) {
            // PR 7B: the contract is a NON-periodic Cartesian communicator
            // with periodic wrap carried by the periodic_x/y flags. A
            // periodic communicator means Cart_shift already wrapped and the
            // flag logic would double-apply — hard failure, never a warning.
            TORCH_CHECK(false,
                "SDIRK3_MPI_COMM_PERIODIC_TOPOLOGY_UNSUPPORTED: communicator has "
                "periodic topology (periods=[", cart_periods[0], ",", cart_periods[1],
                "]); pass a non-periodic local_communicator and use the "
                "periodic_x/y flags");
        }

        // For periodic directions on a non-periodic comm, MPI_Cart_shift returned
        // MPI_PROC_NULL at boundaries. Override with wrap-around using MPI_Cart_rank.
        if (g_wrf_periodic_y && cart_dims[0] > 1) {
            if (impl.neighbor_south < 0) {
                int wrap_coords[2] = {(cart_coords[0] - 1 + cart_dims[0]) % cart_dims[0],
                                       cart_coords[1]};
                int wrap_rank;
                SDIRK3_MPI_CHECK(MPI_Cart_rank(impl.comm, wrap_coords, &wrap_rank));
                impl.neighbor_south = wrap_rank;
            }
            if (impl.neighbor_north < 0) {
                int wrap_coords[2] = {(cart_coords[0] + 1) % cart_dims[0],
                                       cart_coords[1]};
                int wrap_rank;
                SDIRK3_MPI_CHECK(MPI_Cart_rank(impl.comm, wrap_coords, &wrap_rank));
                impl.neighbor_north = wrap_rank;
            }
        }
        if (g_wrf_periodic_x && cart_dims[1] > 1) {
            if (impl.neighbor_west < 0) {
                int wrap_coords[2] = {cart_coords[0],
                                      (cart_coords[1] - 1 + cart_dims[1]) % cart_dims[1]};
                int wrap_rank;
                SDIRK3_MPI_CHECK(MPI_Cart_rank(impl.comm, wrap_coords, &wrap_rank));
                impl.neighbor_west = wrap_rank;
            }
            if (impl.neighbor_east < 0) {
                int wrap_coords[2] = {cart_coords[0],
                                      (cart_coords[1] + 1) % cart_dims[1]};
                int wrap_rank;
                SDIRK3_MPI_CHECK(MPI_Cart_rank(impl.comm, wrap_coords, &wrap_rank));
                impl.neighbor_east = wrap_rank;
            }
        }
    } else {
        // PR 7B: MPI_COMM_WORLD is never auto-promoted to the halo
        // communicator. Single rank = explicit serial/no-exchange state;
        // multi-rank without the WRF Cartesian communicator is a hard error.
        int world_size = 1;
        SDIRK3_MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &world_size));
        TORCH_CHECK(world_size == 1,
            "SDIRK3_MPI_COMM_REQUIRED: ", world_size, " ranks but no WRF "
            "Cartesian communicator was set; call sdirk3_set_mpi_comm_checked "
            "before halo init");
        impl.comm = MPI_COMM_NULL;  // serial: no exchange path may touch it
        impl.has_cart_comm = false;
        impl.mpi_rank = 0;
        impl.mpi_size = 1;
        impl.nprocx = 1; impl.nprocy = 1;
        impl.mypx = 0; impl.mypy = 0;
        impl.compute_neighbors();
    }
#else
    impl.compute_neighbors();
#endif

    // All validation passed. Contract precision (review): candidate BUILD/
    // VALIDATION failures preserve the previous valid state (strong
    // guarantee). A failure while RETIRING the previous communicator below
    // rolls the candidate back and fails closed, but after an MPI error the
    // MPI runtime itself is not guaranteed usable — that failure is
    // surfaced, not survived.
#ifdef DMPARALLEL
    // Free the PREVIOUS owned communicator FIRST — free_owned_comm can throw
    // (always-on MPI check), and until it succeeds the rollback must stay
    // armed so the candidate's fresh duplicate cannot leak on unwind.
    if (g_halo_impl) {
        // active stays TRUE through the replacement window (P1-3): the
        // single-flight Lifecycle scope already protects the old state, so a
        // concurrent worker passes the pre-check and is REJECTED at the
        // scope with a stable marker — an early active=false would instead
        // silently no-op an exchange that the configuration requires.
        free_owned_comm(*g_halo_impl);
    }
    comm_rollback.c = nullptr;  // both communicators accounted for: disarm
#endif
    bool exchange_active = false;
#ifdef DMPARALLEL
    // has_cart_comm exists only in the MPI shape; the MPI-free core build
    // must stay compilable (P1-1) and is always no-exchange.
    exchange_active =
        impl.has_cart_comm && impl.mpi_size > 1 &&
        (impl.nprocx > 1 || impl.nprocy > 1);
#endif
    g_halo_impl = std::move(candidate);
    g_halo_ready.store(true, std::memory_order_release);
    g_halo_exchange_active.store(exchange_active, std::memory_order_release);

    // FIX Round170: Gate initialization output to avoid hot-path spam
    // v11: Use file-scope g_halo_init_logged (reset by set_wrf_communicator on comm change)
    if (impl.mpi_rank == 0 && !g_halo_init_logged) {
        std::cout << "SDIRK3: Halo exchange initialized" << std::endl;
#ifdef DMPARALLEL
        std::cout << "  Communicator: " << (impl.has_cart_comm ? "WRF Cartesian owned duplicate" : "serial/no-exchange") << std::endl;
        std::cout << "  Process grid: " << impl.nprocx << " x " << impl.nprocy
                  << " (my position: " << impl.mypx << "," << impl.mypy << ")" << std::endl;
        std::cout << "  Neighbors N/S/E/W: " << impl.neighbor_north << "/" << impl.neighbor_south
                  << "/" << impl.neighbor_east << "/" << impl.neighbor_west << std::endl;
        if (g_wrf_comm_set) {
            std::cout << "  Periodic: X=" << (g_wrf_periodic_x ? "yes" : "no")
                      << " Y=" << (g_wrf_periodic_y ? "yes" : "no") << std::endl;
        }
#else
        std::cout << "  Process grid: " << nprocx << " x " << nprocy << std::endl;
#endif
        std::cout << "  Halo width: " << halo_width << std::endl;
        g_halo_init_logged = true;
    }
}

// Finalize halo exchange
// FIX Round176: Removed MPI derived type cleanup - types no longer created
// OPT Pass34: Use unique_ptr::reset() for automatic cleanup
// v10: Epoch incremented on every finalize (single point of increment on teardown)
static void halo_exchange_finalize_impl() {
#ifdef DMPARALLEL
    int mpi_finalized = 0;
    if (MPI_Finalized(&mpi_finalized) == MPI_SUCCESS && mpi_finalized) {
        g_halo_exchange_active.store(false, std::memory_order_release);
        g_halo_ready.store(false, std::memory_order_release);
        g_halo_impl.reset();
        ++g_halo_exchange_epoch;
        return;
    }
    if (g_halo_impl) {
        free_owned_comm(*g_halo_impl);  // exactly-once; no-op if never dup'd
    }
    // POLICY A: the communicator configuration's lifetime IS the halo
    // lifecycle — after an explicit finalize a new configuration is legal.
    g_wrf_comm_set = false;
#endif
    g_halo_exchange_active.store(false, std::memory_order_release);
    g_halo_ready.store(false, std::memory_order_release);
    g_halo_impl.reset();
    ++g_halo_exchange_epoch;
}

#ifdef DMPARALLEL
static void set_wrf_communicator_impl(MPI_Fint fortran_comm, bool periodic_x, bool periodic_y);
#endif

// PR 7B (3b-2): public lifecycle entry points take the single-flight
// Lifecycle scope; bodies live in the _impl functions so internal
// cross-calls (set_wrf_communicator -> finalize) never nest two Lifecycle
// scopes. Reconfiguration during an active exchange fails closed here.
void halo_exchange_init(int ids, int ide, int jds, int jde, int kds, int kde,
                       int ims, int ime, int jms, int jme, int kms, int kme,
                       int ips, int ipe, int jps, int jpe, int kps, int kpe,
                       int nprocx, int nprocy, int mypx, int mypy,
                       int halo_width) {
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::Lifecycle, "halo_exchange_init");
    halo_exchange_init_impl(ids, ide, jds, jde, kds, kde,
                            ims, ime, jms, jme, kms, kme,
                            ips, ipe, jps, jpe, kps, kpe,
                            nprocx, nprocy, mypx, mypy, halo_width);
}

void halo_exchange_finalize() {
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::Lifecycle, "halo_exchange_finalize");
    halo_exchange_finalize_impl();
}

#ifdef DMPARALLEL
void set_wrf_communicator(MPI_Fint fortran_comm, bool periodic_x, bool periodic_y) {
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::Lifecycle, "set_wrf_communicator");
    set_wrf_communicator_impl(fortran_comm, periodic_x, periodic_y);
}
#endif

// Step 1.5: Accessor for MPI neighbor ranks (Finding #45/#53)
// Returns neighbor ranks from g_halo_impl. -1 = no neighbor (edge of Cartesian grid).
// C++ namespace only — no Fortran interop, no ABI concern.
HaloNeighborInfo halo_exchange_get_neighbors() {
    TORCH_CHECK(g_halo_impl != nullptr,
        "halo_exchange_get_neighbors: halo exchange not initialized. "
        "Call halo_exchange_init() first.");
    return {g_halo_impl->neighbor_north, g_halo_impl->neighbor_south,
            g_halo_impl->neighbor_east,  g_halo_impl->neighbor_west};
}

HaloWidthInfo halo_exchange_get_widths() {
    TORCH_CHECK(g_halo_impl != nullptr,
        "halo_exchange_get_widths: halo exchange not initialized.");
    return {g_halo_impl->halo_width_x, g_halo_impl->halo_width_y};
}

bool halo_exchange_requires_exchange() noexcept {
    // True only when the CURRENT configuration performs real MPI halo
    // communication (multi-rank Cartesian). Distinct from is_initialized().
    return g_halo_exchange_active.load(std::memory_order_acquire);
}

bool halo_exchange_is_initialized() {
    // PR 7B (review): callers may be unscoped — read the race-free atomic,
    // never the unique_ptr. True only for a fully-validated published state.
    return g_halo_ready.load(std::memory_order_acquire);
}

uint64_t halo_exchange_get_epoch() { return g_halo_exchange_epoch; }

#ifdef DMPARALLEL
static void set_wrf_communicator_impl(MPI_Fint fortran_comm, bool periodic_x, bool periodic_y) {
    // PR 7B (Codex): a null/invalid/non-conforming communicator previously
    // UNSET the state and returned silently, so the checked C ABI reported
    // SUCCESS for garbage input. Validation now happens AT SET TIME and every
    // violation throws (the checked API turns that into status 0; the legacy
    // void API aborts). No state is mutated on rejection.
    int mpi_inited = 0, mpi_finalized = 0;
    MPI_Initialized(&mpi_inited);
    MPI_Finalized(&mpi_finalized);
    TORCH_CHECK(mpi_inited && !mpi_finalized,
        "SDIRK3_MPI_COMM_REQUIRED: set_wrf_communicator called while MPI is ",
        (mpi_inited ? "finalized" : "not initialized"));
    MPI_Comm test_comm = MPI_Comm_f2c(fortran_comm);
    TORCH_CHECK(test_comm != MPI_COMM_NULL,
        "SDIRK3_MPI_COMM_REQUIRED: null communicator handle");
    int test_rank;
    TORCH_CHECK(MPI_Comm_rank(test_comm, &test_rank) == MPI_SUCCESS,
        "SDIRK3_MPI_COMM_REQUIRED: communicator handle rejects MPI_Comm_rank");
    int topo_status = MPI_UNDEFINED;
    SDIRK3_MPI_CHECK(MPI_Topo_test(test_comm, &topo_status));
    TORCH_CHECK(topo_status == MPI_CART,
        "SDIRK3_MPI_COMM_REQUIRED: communicator is not Cartesian "
        "(topo_status=", topo_status, "); pass WRF's local_communicator");
    int t_ndims = 0;
    SDIRK3_MPI_CHECK(MPI_Cartdim_get(test_comm, &t_ndims));
    TORCH_CHECK(t_ndims == 2,
        "SDIRK3_MPI_COMM_TOPOLOGY_MISMATCH: Cartesian communicator has ",
        t_ndims, " dimension(s), expected 2 (y,x); MPI_Cart_get would read "
        "uninitialized entries");
    int t_dims[2], t_periods[2], t_coords[2];
    SDIRK3_MPI_CHECK(MPI_Cart_get(test_comm, 2, t_dims, t_periods, t_coords));
    TORCH_CHECK(t_periods[0] == 0 && t_periods[1] == 0,
        "SDIRK3_MPI_COMM_PERIODIC_TOPOLOGY_UNSUPPORTED: periods=[",
        t_periods[0], ",", t_periods[1], "]; pass a non-periodic "
        "local_communicator and use the periodic_x/y flags");
    // POLICY A (review): dynamic communicator replacement is UNSUPPORTED.
    // The repeated per-step setter call is a no-op only for the IDENTICAL
    // communicator (MPI_IDENT) with identical periodic flags. Anything else
    // while configured is a hard failure — comparing against the stored raw
    // Fortran handle is only sound because the CONTRACT requires WRF's
    // local_communicator to stay stable (never freed, never re-created) for
    // the domain lifetime; reconfiguration requires an explicit
    // halo_exchange_finalize() first. A congruent-but-recreated handle
    // cannot be reliably distinguished from a reused integer without a
    // caller-supplied generation, so it is rejected rather than guessed.
    if (g_wrf_comm_set) {
        int cmp = MPI_UNEQUAL;
        SDIRK3_MPI_CHECK(MPI_Comm_compare(
            MPI_Comm_f2c(g_wrf_fortran_comm), test_comm, &cmp));
        if (cmp == MPI_IDENT
            && g_wrf_periodic_x == periodic_x && g_wrf_periodic_y == periodic_y) {
            return;  // steady-state per-step call: no-op, no epoch change
        }
        TORCH_CHECK(false,
            "SDIRK3_MPI_COMM_RECONFIGURATION_UNSUPPORTED: a different "
            "communicator or changed periodic flags while configured "
            "(cmp=", cmp, "); call halo_exchange_finalize() before "
            "reconfiguring");
    }
    ++g_halo_exchange_epoch;   // First-time set: signal the epoch change
    // v11: Reset init log so re-init prints updated communicator status
    g_halo_init_logged = false;
    g_wrf_fortran_comm = fortran_comm;
    g_wrf_periodic_x = periodic_x;
    g_wrf_periodic_y = periodic_y;
    g_wrf_comm_set = true;
}

bool is_wrf_communicator_set() { return g_wrf_comm_set; }

MPI_Comm halo_exchange_get_comm() {
    TORCH_CHECK(g_halo_impl, "halo not initialized");
    return g_halo_impl->comm;
}

int halo_exchange_get_rank() {
    TORCH_CHECK(g_halo_impl, "halo not initialized");
    return g_halo_impl->mpi_rank;
}
#endif

HaloProcInfo halo_exchange_get_nproc() {
    TORCH_CHECK(g_halo_impl, "halo not initialized");
    return {g_halo_impl->nprocx, g_halo_impl->nprocy,
            g_halo_impl->mypx, g_halo_impl->mypy};
}

/**
 * Perform halo exchange for a single 3D tensor
 *
 * FIX Round171+Round175+Round177: Halo Indexing Contract
 * ======================================================
 * This function ONLY supports FULL HALO-INCLUDED tensors:
 *
 * FULL HALO-INCLUDED TENSOR (storage_offset == 0, REQUIRED)
 *    - Tensor includes both ghost cells and interior
 *    - Shape: [nj_total, nk, ni_total] where nj_total = nj_interior + 2*halo_y
 *    - South ghost: j = 0..halo_y-1
 *    - Interior:    j = halo_y..nj_total-halo_y-1
 *    - North ghost: j = nj_total-halo_y..nj_total-1
 *    - Pack/Send from interior edges, Receive/Unpack into ghost regions
 *
 * INTERIOR VIEW (storage_offset > 0, NOT SUPPORTED)
 *    - FIX Round175: Interior views are REJECTED because E/W ghost handling
 *      is fundamentally broken:
 *      - West ghost recv writes to i=0..halo_x-1 (corrupts interior data)
 *      - East ghost recv targets i=ni..ni+halo_x-1 (fails bounds check, never filled)
 *    - Proper interior view support would require complex storage offset-based
 *      pointer arithmetic, which is error-prone
 *
 * For WRF usage, full halo-included tensors are the expected pattern.
 */
void halo_exchange_3d_tensor(torch::Tensor& tensor, bool force_full_halo) {
    // FIX Round170: Add NoGradGuard to prevent AD graph pollution
    // Halo exchange is a communication operation, not computational
    torch::NoGradGuard no_grad;

#ifdef DMPARALLEL
    // PR 7B (review): historical serial/uninitialized NO-OP preserved via the
    // race-free atomic pre-check — the unique_ptr itself is only read under
    // the scope (an unsynchronized pointer read races lifecycle
    // publish/reset), and it is RE-CHECKED there.
    if (!g_halo_exchange_active.load(std::memory_order_acquire)) {
        return;  // serial/no-exchange or uninitialized: historical no-op
    }
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::FieldPrimitive, "halo_exchange_3d_tensor");
    if (!g_halo_impl ||
        !g_halo_exchange_active.load(std::memory_order_acquire)) {
        return;  // reconfigured/finalized between pre-check and scope
    }
    auto& impl = *g_halo_impl;

    // FIX Round168: Add CPU/contig/FP32 guard for safe data_ptr<float>() access
    // Halo exchange uses direct pointer arithmetic on tensor storage, which requires:
    // 1. CPU tensor (MPI cannot access GPU memory directly)
    // 2. Contiguous layout (pointer arithmetic assumes row-major packing)
    // 3. FP32 dtype (MPI_FLOAT expects 4-byte floats)
    TORCH_CHECK(tensor.is_cpu(),
        "halo_exchange_3d_tensor requires CPU tensor, got ", tensor.device());
    TORCH_CHECK(tensor.is_contiguous(),
        "halo_exchange_3d_tensor requires contiguous tensor");
    TORCH_CHECK(tensor.scalar_type() == torch::kFloat32,
        "halo_exchange_3d_tensor requires FP32 tensor, got ", tensor.scalar_type());

    // FIX Round170: Use int64_t for dimensions and offsets to prevent overflow
    int64_t nj = tensor.size(0);
    int64_t nk = tensor.size(1);
    int64_t ni = tensor.size(2);

    // Get raw data pointer (safe after CPU/contig/FP32 checks above)
    float* data = tensor.data_ptr<float>();

    // FIX Round176: Removed MPI derived type setup - actual communication uses MPI_FLOAT + packed buffers

    // FIX Round170: Calculate halo size with int64_t, then verify fits in int for MPI
    int64_t halo_ns_count = static_cast<int64_t>(impl.halo_width_y) * nk * ni;
    TORCH_CHECK(halo_ns_count <= std::numeric_limits<int>::max(),
        "N/S halo count ", halo_ns_count, " exceeds MPI int limit");
    int halo_ns_int = static_cast<int>(halo_ns_count);

    // FIX Round172+Round174: Detect if tensor is full halo tensor or interior view
    // This affects SEND offsets - we must send INTERIOR edges, not ghost regions
    //
    // Full halo tensor (storage_offset==0 AND dimensions match memory layout):
    //   - j=0..halo_y-1 = south GHOST (do NOT send this)
    //   - j=halo_y..nj-halo_y-1 = INTERIOR
    //   - j=nj-halo_y..nj-1 = north GHOST (do NOT send this)
    //   - Send south: j=halo_y..2*halo_y-1 (interior south edge)
    //   - Send north: j=nj-2*halo_y..nj-halo_y-1 (interior north edge)
    //
    // Interior view (storage_offset>0):
    //   - j=0..nj-1 = INTERIOR only
    //   - Ghost cells are at negative offsets
    //   - Send south: j=0..halo_y-1 (interior south edge)
    //   - Send north: j=nj-halo_y..nj-1 (interior north edge)
    //
    // FIX Round174: Improved detection using both storage_offset AND dimension matching
    // storage_offset==0 alone is not sufficient (could be a reallocated tensor)
    int64_t storage_offset_elements = tensor.storage_offset();
    int64_t expected_mem_nj = impl.jme - impl.jms + 1;
    int64_t expected_mem_ni = impl.ime - impl.ims + 1;

    // Check if dimensions match memory domain (ignoring storage_offset)
    bool dims_match_mem = (nj == expected_mem_nj || nj == expected_mem_nj + 2 * impl.halo_width_y) &&
                          (ni == expected_mem_ni || ni == expected_mem_ni + 2 * impl.halo_width_x);

    bool is_full_halo_tensor;
    if (force_full_halo) {
        // AD halo path: views into cloned packed state have storage_offset > 0
        // but are contiguous with correct shape. data_ptr() handles offset.
        TORCH_CHECK(dims_match_mem && tensor.is_contiguous(),
            "halo_exchange_3d_tensor: force_full_halo=true but tensor shape/contiguity "
            "doesn't match memory domain. nj=", nj, " (expected ", expected_mem_nj,
            "), ni=", ni, " (expected ", expected_mem_ni,
            "), contiguous=", tensor.is_contiguous());
        is_full_halo_tensor = true;
    } else {
        // Original path: strict storage_offset==0 check (unchanged)
        is_full_halo_tensor = (storage_offset_elements == 0) && dims_match_mem;
    }

    // FIX Round175: Reject interior views entirely (non-AD path)
    // Interior views have fundamentally broken E/W ghost handling:
    //   - West ghost recv writes to i=0..halo_x-1, which is interior data (corruption)
    //   - East ghost recv targets i=ni..ni+halo_x-1, which fails bounds check (never filled)
    // Proper support would require storage offset-based pointer arithmetic, which is complex
    // and error-prone. The safest approach is to require full halo tensors.
    TORCH_CHECK(is_full_halo_tensor,
        "halo_exchange_3d_tensor: Interior view tensors are not supported. "
        "E/W ghost handling requires full halo tensors with ghost regions inside tensor bounds. "
        "storage_offset=", storage_offset_elements, ", force_full_halo=", force_full_halo,
        ", nj=", nj, " (expected ", expected_mem_nj, " or ", expected_mem_nj + 2 * impl.halo_width_y, ")",
        ", ni=", ni, " (expected ", expected_mem_ni, " or ", expected_mem_ni + 2 * impl.halo_width_x, ")");

    // Calculate j-offsets for N/S sends based on tensor convention
    int64_t j_send_south_start = is_full_halo_tensor ? impl.halo_width_y : 0;
    int64_t j_send_north_start = is_full_halo_tensor ? (nj - 2 * impl.halo_width_y) : (nj - impl.halo_width_y);

    // v10: Phased Y→X exchange (RSL_LITE compatible)
    // Phase 1: N/S first (matches RSL_LITE c_code.c:1013)
    // Phase 2: E/W second (matches RSL_LITE c_code.c:1051)
    // Tags: Isend(tag=dest_rank), Irecv(tag=my_rank) — matches RSL_LITE convention
    // Irecv posted before Isend for each neighbor (standard MPI practice)

    // ---- Phase 1: North-South exchange (skip if nprocy==1) ----
    if (impl.nprocy > 1) {
        MPI_Request req_ns[4]; int nreq_ns = 0;

        if (impl.neighbor_north >= 0) {
            int64_t offset_send = j_send_north_start * nk * ni;
            int64_t offset_recv = (nj - impl.halo_width_y) * nk * ni;
            SDIRK3_MPI_CHECK(MPI_Irecv(data + offset_recv, halo_ns_int, MPI_FLOAT,
                     impl.neighbor_north, HALO_TAG_SOUTHWARD, impl.comm, &req_ns[nreq_ns++]));
            SDIRK3_MPI_CHECK(MPI_Isend(data + offset_send, halo_ns_int, MPI_FLOAT,
                     impl.neighbor_north, HALO_TAG_NORTHWARD, impl.comm, &req_ns[nreq_ns++]));
        }

        if (impl.neighbor_south >= 0) {
            int64_t offset_send = j_send_south_start * nk * ni;
            float* recv_ptr = data;  // South ghost at beginning of full-halo tensor
            SDIRK3_MPI_CHECK(MPI_Irecv(recv_ptr, halo_ns_int, MPI_FLOAT,
                     impl.neighbor_south, HALO_TAG_NORTHWARD, impl.comm, &req_ns[nreq_ns++]));
            SDIRK3_MPI_CHECK(MPI_Isend(data + offset_send, halo_ns_int, MPI_FLOAT,
                     impl.neighbor_south, HALO_TAG_SOUTHWARD, impl.comm, &req_ns[nreq_ns++]));
        }

        if (nreq_ns > 0)
            SDIRK3_MPI_CHECK(MPI_Waitall(nreq_ns, req_ns, MPI_STATUSES_IGNORE));
    }

    // ---- Phase 2: East-West exchange (skip if nprocx==1) ----
    // E/W exchange uses FULL j-range (0..nj-1), including N/S ghost rows for full-halo tensors.
    // This matches WRF's protocol: N/S fills ghost rows, E/W propagates corner values.
    if (impl.nprocx > 1) {
        int64_t ew_buffer_size = static_cast<int64_t>(impl.halo_width_x) * nj * nk;
        TORCH_CHECK(ew_buffer_size <= std::numeric_limits<int>::max(),
            "E/W halo buffer size ", ew_buffer_size, " exceeds MPI int limit");
        int ew_buffer_int = static_cast<int>(ew_buffer_size);

        // FIX Round187: Use cached buffers, only resize if dimension changed
        if (impl.cached_ew_buffer_size != ew_buffer_size) {
            impl.cached_send_buffer_east.resize(ew_buffer_size);
            impl.cached_recv_buffer_east.resize(ew_buffer_size);
            impl.cached_send_buffer_west.resize(ew_buffer_size);
            impl.cached_recv_buffer_west.resize(ew_buffer_size);
            impl.cached_ew_buffer_size = ew_buffer_size;
        }
        auto& send_buffer_east = impl.cached_send_buffer_east;
        auto& recv_buffer_east = impl.cached_recv_buffer_east;
        auto& send_buffer_west = impl.cached_send_buffer_west;
        auto& recv_buffer_west = impl.cached_recv_buffer_west;

        bool has_east_exchange = (impl.neighbor_east >= 0);
        bool has_west_exchange = (impl.neighbor_west >= 0);

        int64_t i_send_west_start = impl.halo_width_x;
        int64_t i_send_east_start = ni - 2 * impl.halo_width_x;

        // Pack send buffers
        if (has_east_exchange) {
            auto east_slab = tensor.slice(2, i_send_east_start,
                                          i_send_east_start + impl.halo_width_x).contiguous();
            std::memcpy(send_buffer_east.data(), east_slab.data_ptr<float>(),
                        static_cast<size_t>(ew_buffer_size) * sizeof(float));
        }
        if (has_west_exchange) {
            auto west_slab = tensor.slice(2, i_send_west_start,
                                          i_send_west_start + impl.halo_width_x).contiguous();
            std::memcpy(send_buffer_west.data(), west_slab.data_ptr<float>(),
                        static_cast<size_t>(ew_buffer_size) * sizeof(float));
        }

        MPI_Request req_ew[4]; int nreq_ew = 0;

        if (has_east_exchange) {
            SDIRK3_MPI_CHECK(MPI_Irecv(recv_buffer_east.data(), ew_buffer_int, MPI_FLOAT,
                     impl.neighbor_east, HALO_TAG_WESTWARD, impl.comm, &req_ew[nreq_ew++]));
            SDIRK3_MPI_CHECK(MPI_Isend(send_buffer_east.data(), ew_buffer_int, MPI_FLOAT,
                     impl.neighbor_east, HALO_TAG_EASTWARD, impl.comm, &req_ew[nreq_ew++]));
        }
        if (has_west_exchange) {
            SDIRK3_MPI_CHECK(MPI_Irecv(recv_buffer_west.data(), ew_buffer_int, MPI_FLOAT,
                     impl.neighbor_west, HALO_TAG_EASTWARD, impl.comm, &req_ew[nreq_ew++]));
            SDIRK3_MPI_CHECK(MPI_Isend(send_buffer_west.data(), ew_buffer_int, MPI_FLOAT,
                     impl.neighbor_west, HALO_TAG_WESTWARD, impl.comm, &req_ew[nreq_ew++]));
        }

        if (nreq_ew > 0)
            SDIRK3_MPI_CHECK(MPI_Waitall(nreq_ew, req_ew, MPI_STATUSES_IGNORE));

        // Unpack E/W recv buffers → tensor ghost regions
        if (has_east_exchange) {
            int64_t i_recv_east_start = ni - impl.halo_width_x;
            auto recv_east_t = torch::from_blob(recv_buffer_east.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {nj, nk, static_cast<int64_t>(impl.halo_width_x)},
                torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            tensor.slice(2, i_recv_east_start,
                         i_recv_east_start + impl.halo_width_x).copy_(recv_east_t);
        }
        if (has_west_exchange) {
            auto recv_west_t = torch::from_blob(recv_buffer_west.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {nj, nk, static_cast<int64_t>(impl.halo_width_x)},
                torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            tensor.slice(2, 0, static_cast<int64_t>(impl.halo_width_x)).copy_(recv_west_t);
        }
    }

#endif // DMPARALLEL
}

// Perform halo exchange for multiple 3D tensors
void halo_exchange_multiple(std::vector<torch::Tensor>& tensors, bool force_full_halo) {
    // For efficiency, pack multiple variables and exchange together

    // empty() reads no global state and stays a pre-scope no-op; the
    // g_halo_impl read moves BEHIND the scope (P1: an unsynchronized
    // unique_ptr read races lifecycle publish/reset).
    if (tensors.empty() ||
        !g_halo_exchange_active.load(std::memory_order_acquire)) {
        return;  // empty, serial/no-exchange, or uninitialized: no-op
    }

    // PR 7B (3b-2): the whole pack -> NS -> EW -> unpack sequence is ONE
    // logical exchange; nothing may interleave between fields.
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::ForwardBatch, "halo_exchange_multiple");
    if (!g_halo_impl ||
        !g_halo_exchange_active.load(std::memory_order_acquire)) {
        return;  // reconfigured/finalized between pre-check and scope
    }

    // FIX Round170: Add NoGradGuard to prevent AD graph pollution
    // Halo exchange is a communication operation, not a computational one
    torch::NoGradGuard no_grad;

#ifdef DMPARALLEL
    auto& impl = *g_halo_impl;

    // FIX Round173: Use int64_t for dimensions to prevent overflow on large grids
    // Example: 1000 * 100 * 1000 = 100M elements - safe in int64_t but overflows int32
    int64_t nj = tensors[0].size(0);
    int64_t nk = tensors[0].size(1);
    int64_t ni = tensors[0].size(2);
    int num_vars = static_cast<int>(tensors.size());

    // FIX Round170: Validate all tensors are FP32 and have matching dimensions
    // Using data_ptr<float>() on non-FP32 tensor is UB
    for (int v = 0; v < num_vars; v++) {
        TORCH_CHECK(tensors[v].scalar_type() == torch::kFloat32,
            "halo_exchange_multiple: tensor[", v, "] must be FP32, got ",
            tensors[v].scalar_type());
        TORCH_CHECK(tensors[v].dim() == 3,
            "halo_exchange_multiple: tensor[", v, "] must be 3D, got ",
            tensors[v].dim(), "D");
        TORCH_CHECK(tensors[v].size(0) == nj && tensors[v].size(1) == nk && tensors[v].size(2) == ni,
            "halo_exchange_multiple: tensor[", v, "] shape {", tensors[v].size(0), ",",
            tensors[v].size(1), ",", tensors[v].size(2), "} doesn't match tensor[0] shape {",
            nj, ",", nk, ",", ni, "}");
    }

    // FIX Round169: Use size_t for buffer sizes to prevent overflow on large grids
    // Example: halo_width=4 * nk=100 * ni=1000 * num_vars=10 = 4M (safe in size_t)
    size_t ns_size = static_cast<size_t>(impl.halo_width_y) * nk * ni * num_vars;
    size_t ew_size = static_cast<size_t>(impl.halo_width_x) * nj * nk * num_vars;

    // FIX Round169: Verify sizes fit in int for MPI (MPI uses int for counts)
    TORCH_CHECK(ns_size <= static_cast<size_t>(std::numeric_limits<int>::max()),
        "N/S halo buffer size ", ns_size, " exceeds MPI int limit");
    TORCH_CHECK(ew_size <= static_cast<size_t>(std::numeric_limits<int>::max()),
        "E/W halo buffer size ", ew_size, " exceeds MPI int limit");
    int ns_size_int = static_cast<int>(ns_size);
    int ew_size_int = static_cast<int>(ew_size);
    
    std::vector<float> ns_send_north(ns_size);
    std::vector<float> ns_recv_north(ns_size);
    std::vector<float> ns_send_south(ns_size);
    std::vector<float> ns_recv_south(ns_size);

    // OPT Pass34: MPI_Request array sizing documentation
    // Actual max: 8 requests (4 directions × 2 ops: N/S/E/W × send/recv)
    // Array sized at 16 for 2× safety margin against off-by-one and future expansion.
    // INVARIANT: nreq must never exceed 16. Current code uses exactly 8 when all
    // neighbors are present, fewer when at domain boundary.
    MPI_Request requests[16];
    int nreq = 0;

    // FIX Round170+Round171: Cache CPU tensor views to avoid multiple GPU→CPU copies
    // Previously: ensure_cpu_contiguous() was called multiple times per tensor
    // Now: copy to CPU once, use for all operations, copy back once at end
    std::vector<torch::Tensor> cpu_tensors(num_vars);
    std::vector<torch::Device> orig_devices(num_vars, torch::kCPU);
    std::vector<bool> needs_copyback(num_vars, false);

    // FIX Round174+Round176: Improved full-halo detection for ALL tensors
    // storage_offset==0 alone is ambiguous (narrow(0,...) can also have offset 0)
    // Compare tensor dimensions with expected memory domain size for definitive detection
    // FIX Round176: Verify ALL tensors have consistent layout, not just tensors[0]
    int64_t expected_mem_nj = impl.jme - impl.jms + 1;  // Memory domain j-size with ghost
    int64_t expected_mem_ni = impl.ime - impl.ims + 1;  // Memory domain i-size with ghost

    for (int v = 0; v < num_vars; v++) {
        int64_t storage_offset_v = tensors[v].storage_offset();
        // Full-halo tensor: storage_offset==0 AND dimensions match memory domain
        bool is_full_halo_v = (storage_offset_v == 0) &&
                              (nj == expected_mem_nj || nj == expected_mem_nj + 2 * impl.halo_width_y) &&
                              (ni == expected_mem_ni || ni == expected_mem_ni + 2 * impl.halo_width_x);

        // FIX Round175: Reject interior views entirely (regardless of device/contiguity)
        // Interior views have fundamentally broken E/W ghost handling:
        //   - West ghost recv writes to i=0..halo_x-1, which is interior data (corruption)
        //   - East ghost recv targets i=ni..ni+halo_x-1, which fails bounds check (never filled)
        TORCH_CHECK(is_full_halo_v,
            "halo_exchange_multiple: tensor[", v, "] is not a full-halo tensor. "
            "E/W ghost handling requires full halo tensors with ghost regions inside tensor bounds. "
            "storage_offset=", storage_offset_v,
            ", nj=", nj, " (expected ", expected_mem_nj, " or ", expected_mem_nj + 2 * impl.halo_width_y, ")",
            ", ni=", ni, " (expected ", expected_mem_ni, " or ", expected_mem_ni + 2 * impl.halo_width_x, ")");
    }

    for (int v = 0; v < num_vars; v++) {
        orig_devices[v] = tensors[v].device();
        // FIX Round171: Generalize device check to handle XPU/HIP/etc., not just CUDA/MPS
        // Any non-CPU device needs copy to CPU for MPI, and copy-back after
        if (!tensors[v].is_cpu()) {
            cpu_tensors[v] = tensors[v].to(torch::kCPU).contiguous();
            needs_copyback[v] = true;
        } else if (!tensors[v].is_contiguous()) {
            // FIX Round171: CPU but non-contiguous - contiguous() creates a new tensor
            // Need to copy back to original after modification
            cpu_tensors[v] = tensors[v].contiguous();
            needs_copyback[v] = true;
        } else {
            // CPU and contiguous - can modify in-place, no copy-back needed
            cpu_tensors[v] = tensors[v];
            needs_copyback[v] = false;
        }
    }

    // All tensors validated as full-halo above (TORCH_CHECK rejects non-full-halo).
    // Calculate j/i offsets for sends (full-halo: skip ghost regions, send interior edges).
    constexpr bool is_full_halo_tensor = true;  // enforced by TORCH_CHECK above
    int64_t j_send_south_start = impl.halo_width_y;
    int64_t j_send_north_start = nj - 2 * impl.halo_width_y;
    int64_t i_send_west_start = impl.halo_width_x;
    int64_t i_send_east_start = ni - 2 * impl.halo_width_x;

    // FIX Round174: E/W exchange j-range policy documentation
    // E/W exchange uses FULL j-range (0..nj-1 for full-halo), including N/S ghost rows.
    // This is intentional and matches WRF's halo exchange protocol:
    //   1. N/S exchange fills north/south ghost rows with interior data from neighbors
    //   2. E/W exchange then includes these ghost rows, propagating corner values
    // The corner cells (ghost-ghost intersections) get their values from diagonal neighbors
    // via this E/W-after-N/S propagation pattern.
    // DO NOT restrict E/W j-range to interior - it would leave corners uninitialized.

    // FIX Round173: Validate full-halo offset calculations to catch edge cases
    // For full halo tensor, interior must have positive size after subtracting ghost regions
    if (is_full_halo_tensor) {
        TORCH_CHECK(nj >= 2 * impl.halo_width_y,
            "Full halo tensor: nj=", nj, " too small for halo_width_y=", impl.halo_width_y);
        TORCH_CHECK(ni >= 2 * impl.halo_width_x,
            "Full halo tensor: ni=", ni, " too small for halo_width_x=", impl.halo_width_x);
        TORCH_CHECK(j_send_north_start >= j_send_south_start + impl.halo_width_y,
            "Full halo tensor: insufficient interior j range for halo exchange");
        TORCH_CHECK(i_send_east_start >= i_send_west_start + impl.halo_width_x,
            "Full halo tensor: insufficient interior i range for halo exchange");
    }

    // v10: Phased Y→X exchange for multi-variable (RSL_LITE compatible)
    // Tags: Isend(tag=dest_rank), Irecv(tag=my_rank)

    // ---- Phase 1: N/S exchange (skip if nprocy==1) ----
    if (impl.nprocy > 1 && (impl.neighbor_north >= 0 || impl.neighbor_south >= 0)) {
        // Pack data for north/south exchange
        size_t idx_north = 0, idx_south = 0;

        for (int v = 0; v < num_vars; v++) {
            const float* tensor_data = cpu_tensors[v].data_ptr<float>();
            for (int64_t j = 0; j < impl.halo_width_y; j++) {
                for (int64_t k = 0; k < nk; k++) {
                    for (int64_t i = 0; i < ni; i++) {
                        int64_t j_south = j_send_south_start + j;
                        ns_send_south[idx_south++] = tensor_data[jki_index(j_south, k, i, nk, ni)];
                        int64_t j_north = j_send_north_start + j;
                        ns_send_north[idx_north++] = tensor_data[jki_index(j_north, k, i, nk, ni)];
                    }
                }
            }
        }

        MPI_Request req_ns[4]; int nreq_ns = 0;
        if (impl.neighbor_north >= 0) {
            SDIRK3_MPI_CHECK(MPI_Irecv(ns_recv_north.data(), ns_size_int, MPI_FLOAT,
                     impl.neighbor_north, HALO_TAG_SOUTHWARD, impl.comm, &req_ns[nreq_ns++]));
            SDIRK3_MPI_CHECK(MPI_Isend(ns_send_north.data(), ns_size_int, MPI_FLOAT,
                     impl.neighbor_north, HALO_TAG_NORTHWARD, impl.comm, &req_ns[nreq_ns++]));
        }
        if (impl.neighbor_south >= 0) {
            SDIRK3_MPI_CHECK(MPI_Irecv(ns_recv_south.data(), ns_size_int, MPI_FLOAT,
                     impl.neighbor_south, HALO_TAG_NORTHWARD, impl.comm, &req_ns[nreq_ns++]));
            SDIRK3_MPI_CHECK(MPI_Isend(ns_send_south.data(), ns_size_int, MPI_FLOAT,
                     impl.neighbor_south, HALO_TAG_SOUTHWARD, impl.comm, &req_ns[nreq_ns++]));
        }
        if (nreq_ns > 0)
            SDIRK3_MPI_CHECK(MPI_Waitall(nreq_ns, req_ns, MPI_STATUSES_IGNORE));

        // Unpack N/S immediately (needed before E/W for corner propagation)
        if (impl.neighbor_north >= 0) {
            size_t idx = 0;
            int64_t j_recv_north = nj - impl.halo_width_y;
            for (int v = 0; v < num_vars; v++) {
                float* td = cpu_tensors[v].data_ptr<float>();
                int64_t t_nk = cpu_tensors[v].size(1);
                int64_t t_ni = cpu_tensors[v].size(2);
                int64_t t_nj = cpu_tensors[v].size(0);
                for (int64_t j = 0; j < impl.halo_width_y; j++) {
                    for (int64_t k = 0; k < nk; k++) {
                        for (int64_t i = 0; i < ni; i++) {
                            int64_t jg = j_recv_north + j;
                            if (jg < t_nj)
                                td[jki_index(jg, k, i, t_nk, t_ni)] = ns_recv_north[idx];
                            idx++;
                        }
                    }
                }
            }
        }
        if (impl.neighbor_south >= 0) {
            size_t idx = 0;
            for (int v = 0; v < num_vars; v++) {
                float* td = cpu_tensors[v].data_ptr<float>();
                int64_t t_nk = cpu_tensors[v].size(1);
                int64_t t_ni = cpu_tensors[v].size(2);
                int64_t t_nj = cpu_tensors[v].size(0);
                for (int64_t j = 0; j < impl.halo_width_y; j++) {
                    for (int64_t k = 0; k < nk; k++) {
                        for (int64_t i = 0; i < ni; i++) {
                            if (j < t_nj)
                                td[jki_index(j, k, i, t_nk, t_ni)] = ns_recv_south[idx];
                            idx++;
                        }
                    }
                }
            }
        }
    }

    // ---- Phase 2: E/W exchange (skip if nprocx==1) ----
    std::vector<float> ew_send_east, ew_recv_east;
    std::vector<float> ew_send_west, ew_recv_west;
    bool has_east = (impl.neighbor_east >= 0);
    bool has_west = (impl.neighbor_west >= 0);

    if (impl.nprocx > 1 && (has_east || has_west)) {
        if (has_east) {
            ew_send_east.resize(ew_size);
            ew_recv_east.resize(ew_size);
        }
        if (has_west) {
            ew_send_west.resize(ew_size);
            ew_recv_west.resize(ew_size);
        }

        // Pack E/W data for all variables using cached CPU tensors
        size_t idx_east = 0, idx_west = 0;
        for (int v = 0; v < num_vars; v++) {
            const float* tensor_data = cpu_tensors[v].data_ptr<float>();
            for (int64_t j = 0; j < nj; j++) {
                for (int64_t k = 0; k < nk; k++) {
                    if (has_east) {
                        for (int64_t h = 0; h < impl.halo_width_x; h++) {
                            int64_t i = i_send_east_start + h;
                            ew_send_east[idx_east++] = tensor_data[jki_index(j, k, i, nk, ni)];
                        }
                    }
                    if (has_west) {
                        for (int64_t h = 0; h < impl.halo_width_x; h++) {
                            int64_t i = i_send_west_start + h;
                            ew_send_west[idx_west++] = tensor_data[jki_index(j, k, i, nk, ni)];
                        }
                    }
                }
            }
        }

        MPI_Request req_ew[4]; int nreq_ew = 0;
        if (has_east) {
            SDIRK3_MPI_CHECK(MPI_Irecv(ew_recv_east.data(), ew_size_int, MPI_FLOAT,
                     impl.neighbor_east, HALO_TAG_WESTWARD, impl.comm, &req_ew[nreq_ew++]));
            SDIRK3_MPI_CHECK(MPI_Isend(ew_send_east.data(), ew_size_int, MPI_FLOAT,
                     impl.neighbor_east, HALO_TAG_EASTWARD, impl.comm, &req_ew[nreq_ew++]));
        }
        if (has_west) {
            SDIRK3_MPI_CHECK(MPI_Irecv(ew_recv_west.data(), ew_size_int, MPI_FLOAT,
                     impl.neighbor_west, HALO_TAG_EASTWARD, impl.comm, &req_ew[nreq_ew++]));
            SDIRK3_MPI_CHECK(MPI_Isend(ew_send_west.data(), ew_size_int, MPI_FLOAT,
                     impl.neighbor_west, HALO_TAG_WESTWARD, impl.comm, &req_ew[nreq_ew++]));
        }
        if (nreq_ew > 0)
            SDIRK3_MPI_CHECK(MPI_Waitall(nreq_ew, req_ew, MPI_STATUSES_IGNORE));
    }

    // N/S unpack already done in Phase 1 above.
    // E/W unpack: must be inside nprocx guard to match exchange above.
    // Without this guard, periodic Cart comm with nprocx==1 would have
    // has_east/has_west=true but empty recv buffers → UB.
    if (impl.nprocx > 1 && (has_east || has_west)) {
        int64_t i_recv_east_start = ni - impl.halo_width_x;
        int64_t i_recv_west_start = 0;

        // East unpack
        if (has_east) {
            size_t idx = 0;
            for (int v = 0; v < num_vars; v++) {
                float* tensor_data = cpu_tensors[v].data_ptr<float>();
                int64_t t_nk = cpu_tensors[v].size(1);
                int64_t t_ni = cpu_tensors[v].size(2);
                int64_t t_nj = cpu_tensors[v].size(0);

                for (int64_t j = 0; j < t_nj; j++) {
                    for (int64_t k = 0; k < nk; k++) {
                        for (int64_t h = 0; h < impl.halo_width_x; h++) {
                            // FIX Round172: East ghost region
                            int64_t i_ghost = i_recv_east_start + h;
                            if (i_ghost < t_ni) {
                                tensor_data[jki_index(j, k, i_ghost, t_nk, t_ni)] = ew_recv_east[idx];
                            }
                            idx++;
                        }
                    }
                }
            }
        }

        // West unpack
        if (has_west) {
            size_t idx = 0;
            for (int v = 0; v < num_vars; v++) {
                float* tensor_data = cpu_tensors[v].data_ptr<float>();
                int64_t t_nk = cpu_tensors[v].size(1);
                int64_t t_ni = cpu_tensors[v].size(2);
                int64_t t_nj = cpu_tensors[v].size(0);

                for (int64_t j = 0; j < t_nj; j++) {
                    for (int64_t k = 0; k < nk; k++) {
                        for (int64_t h = 0; h < impl.halo_width_x; h++) {
                            // West ghost region (same for both conventions)
                            int64_t i_ghost = i_recv_west_start + h;
                            if (i_ghost >= 0 && i_ghost < t_ni) {
                                tensor_data[jki_index(j, k, i_ghost, t_nk, t_ni)] = ew_recv_west[idx];
                            }
                            idx++;
                        }
                    }
                }
            }
        }
    }

    // FIX Round170+Round173: Single copy-back for all tensors that need it
    // Optimized: Skip .to() if already on correct device (cpu_tensors are always CPU here)
    for (int v = 0; v < num_vars; v++) {
        if (needs_copyback[v]) {
            // FIX Round173: Optimize copy-back by checking if device transfer needed
            if (orig_devices[v].is_cpu()) {
                // Original was CPU (but non-contiguous) - direct copy without device transfer
                tensors[v].copy_(cpu_tensors[v]);
            } else {
                // Original was GPU - need device transfer
                tensors[v].copy_(cpu_tensors[v].to(orig_devices[v]));
            }
        }
    }

#endif // DMPARALLEL
}

// C interface for Fortran
extern "C" {

void sdirk3_halo_exchange(float* state_array, int nx, int ny, int nz, int num_vars,
                         int ids, int ide, int jds, int jde, int kds, int kde,
                         int ims, int ime, int jms, int jme, int kms, int kme,
                         int ips, int ipe, int jps, int jpe, int kps, int kpe) {
  // PR 7B: exceptions (incl. the always-on MPI check) must never cross the
  // extern "C" boundary into Fortran. Fail loudly and stop instead.
  try {
    
    if (!g_halo_impl) {
        std::cerr << "Error: halo exchange not initialized" << std::endl;
        return;
    }
    
    // Convert Fortran array to torch tensors
    std::vector<torch::Tensor> tensors;

    // FIX 2025-12-27: Use data_ptr for efficient copy without per-element overhead
    int t_nj = jme - jms + 1;
    int t_nk = kme - kms + 1;
    int t_ni = ime - ims + 1;

    for (int v = 0; v < num_vars; v++) {
        // Calculate offset for this variable
        // FIX Round192: Use int64_t to prevent overflow on large grids (nx*ny*nz > 2^31)
        int64_t var_offset = static_cast<int64_t>(v) * t_ni * t_nk * t_nj;
        float* var_data = state_array + var_offset;

        // Create tensor with proper layout conversion
        // Fortran: (i,k,j) -> C++: [j,k,i]
        // FIX Round194: Explicit CPU device for Fortran array conversion
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto tensor = torch::zeros({t_nj, t_nk, t_ni}, options);
        float* tensor_data = tensor.data_ptr<float>();

        // Copy with layout conversion using direct pointer access
        for (int j = 0; j < t_nj; j++) {
            for (int k = 0; k < t_nk; k++) {
                for (int i = 0; i < t_ni; i++) {
                    // FIX Round192: Use int64_t for 3D index calculation
                    int64_t fortran_idx = i + static_cast<int64_t>(k) * t_ni + static_cast<int64_t>(j) * t_ni * t_nk;
                    tensor_data[jki_index(j, k, i, t_nk, t_ni)] = var_data[fortran_idx];
                }
            }
        }

        tensors.push_back(tensor);
    }

    // Perform halo exchange
    halo_exchange_multiple(tensors);

    // Copy back to Fortran array using data_ptr (avoids .item<float>() GPU sync)
    for (int v = 0; v < num_vars; v++) {
        // FIX Round192: Use int64_t to prevent overflow on large grids
        int64_t var_offset = static_cast<int64_t>(v) * t_ni * t_nk * t_nj;
        float* var_data = state_array + var_offset;

        // Ensure CPU contiguous for copy-back
        torch::Tensor cpu_tensor = ensure_cpu_contiguous(tensors[v]);
        const float* tensor_data = cpu_tensor.data_ptr<float>();

        for (int j = 0; j < t_nj; j++) {
            for (int k = 0; k < t_nk; k++) {
                for (int i = 0; i < t_ni; i++) {
                    // FIX Round192: Use int64_t for 3D index calculation
                    int64_t fortran_idx = i + static_cast<int64_t>(k) * t_ni + static_cast<int64_t>(j) * t_ni * t_nk;
                    var_data[fortran_idx] = tensor_data[jki_index(j, k, i, t_nk, t_ni)];
                }
            }
        }
    }
  } catch (const std::exception& e) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_exchange", e.what());
  } catch (...) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_exchange", nullptr);
  }
}

void sdirk3_halo_init(int ids, int ide, int jds, int jde, int kds, int kde,
                     int ims, int ime, int jms, int jme, int kms, int kme,
                     int ips, int ipe, int jps, int jpe, int kps, int kpe,
                     int nprocx, int nprocy, int mypx, int mypy,
                     int halo_width) {
  // halo_exchange_init reaches the throwing MPI check (Comm_rank/size,
  // Topo_test, Cart_get/shift/rank) and TORCH_CHECK; nothing may unwind
  // through this C ABI.
  try {
    halo_exchange_init(ids, ide, jds, jde, kds, kde,
                      ims, ime, jms, jme, kms, kme,
                      ips, ipe, jps, jpe, kps, kpe,
                      nprocx, nprocy, mypx, mypy, halo_width);
  } catch (const std::exception& e) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_init", e.what());
  } catch (...) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_init", nullptr);
  }
}

void sdirk3_halo_finalize() {
  try {
    halo_exchange_finalize();
  } catch (const std::exception& e) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_finalize", e.what());
  } catch (...) {
      wrf::sdirk3::mpi_safety::abort_c_abi_exception("sdirk3_halo_finalize", nullptr);
  }
}

} // extern "C"

} // namespace sdirk3
} // namespace wrf