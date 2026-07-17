// ============================================================================
// test_mpi_runtime_contract.cpp — PR 7B (3b) runtime fail-close contracts
//
// Codifies the 3b-1/3b-2/3b-3 scratch probes as a standing CTest matrix
// (MPI_Runtime_Contract_np{1,2,4}). One binary, sections ordered so that
// process-PERMANENT effects (the lifecycle fault latch) run last; the two
// scenarios that cannot share a process with the main flow (candidate
// rollback fault, void-ABI coordinated abort) run as re-exec'd MPI-singleton
// CHILDREN with the launcher environment scrubbed.
//
// Sections (main mode, in order):
//   0  scope entered before baseline establishment -> fail closed
//   1  underscore-ABI init establishes the baseline (both spellings equal)
//   2  single-flight scope: worker rejection, illegal/legal nesting
//   3  checked communicator: null / non-Cartesian / periodic / 1-D / valid
//   4  checked prepare: fingerprint + geometry + thread + Policy A contracts
//   5  freshness: lifecycle-scoped credits, publication/consumption gates,
//      checked notify/require ABIs (3b-3 P1 + part 2)
//   6  AD lifecycle-epoch binding (3b-3 part 3)
//   7  field semantics: interior preservation, H(Hx)==Hx (bitwise),
//      halo_exchange_multiple == sequential exchanges (bitwise)
//   8  children (rank 0): rollback-fault latch (exit 0 expected) and
//      void sdirk3_halo_finalize abort (nonzero + SDIRK3_C_ABI_EXCEPTION)
//   9  LAST: explicit-finalize failure -> permanent lifecycle fault latch
// ============================================================================

#include <mpi.h>
#include <torch/torch.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wrf_sdirk3_halo_exchange.h"
#include "wrf_sdirk3_halo_c_api.h"
#include "wrf_sdirk3_ad_halo_exchange.h"
#include "wrf_sdirk3_mpi_safety.h"
#include "wrf_sdirk3_config.h"

extern "C" void sdirk3_mpi_safety_init(void);
extern "C" void sdirk3_mpi_safety_init_(void);
extern "C" void sdirk3_notify_halo_fresh(void) noexcept;
extern "C" int sdirk3_notify_halo_fresh_checked(void) noexcept;
// Defined in wrf_sdirk3_interface_zerocopy.cpp, which is compiled into this
// target as a FRESH -DDMPARALLEL object: the production core archive is
// MPI-free and its variant of this ABI returns 1 unconditionally — linking
// that would pass the null/non-Cartesian negatives for the wrong reason.
extern "C" int sdirk3_set_mpi_comm_checked(int, int, int);

extern char** environ;

using namespace wrf::sdirk3;
using G = mpi_safety::HaloFreshnessGuard;

// Coverage-ratchet expected per-section check counts on rank 0, {np1,np2,np4}.
// Baked from the instrumented run (RATCHET-MEASURE). Update DELIBERATELY when
// a contract is added/removed — a silent count drift means a vanished check.
// prepare grows with np: +3 (alt-mypy) at np2, +5 (alt-mypx+mypy) at np4.
// field is 6 checks/case x 4 periodic x 2 width x topologies (2 at np2, 1 else).
#define SCOPE_W   6,  6,  6
#define COMM_W    9,  9,  9
#define PREP_W   15, 18, 20
#define FRESH_W  15, 35, 35
#define AD_W      4, 10, 10
#define FIELD_W  48, 96, 48
#define CHILD_W   9,  9,  9
#define LATCH_W   6,  6,  6

namespace {

int g_fails = 0;
long g_checks = 0;

// Coverage ratchet (review P2): every check bumps BOTH the global tally and
// the CURRENT section's counter. Rank 0 (a member of every grid, and the
// sole member of the 1x1 comm/prepare grids) runs all sections, so its
// per-section counts are asserted against exact baked values at the end — a
// vanished section or negative then fails the ratchet, not just the sum.
long* g_section = nullptr;

// Per-section counters; asserted against exact baked values on rank 0.
long c_scope = 0, c_comm = 0, c_prepare = 0, c_freshness = 0;
long c_ad = 0, c_field = 0, c_child = 0, c_latch = 0;

void tally() { ++g_checks; if (g_section) ++*g_section; }

void fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("FAIL: ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
    ++g_fails;
}

#define CHECK_TRUE(cond, ...) \
    do { tally(); if (!(cond)) fail(__VA_ARGS__); } while (0)

bool threw_with_marker(const std::function<void()>& op, const char* marker,
                       std::string* msg_out = nullptr) {
    try { op(); return false; }
    catch (const std::exception& e) {
        if (msg_out) *msg_out = e.what();
        return std::strstr(e.what(), marker) != nullptr;
    }
}

// Capture everything the checked C ABIs write to fd 2 during `op` (they
// return a status and PRINT the reason instead of throwing). Restores fd 2
// even if op throws. Flushes both the C++ stream and C stdio so the marker
// is on disk before we read it back.
std::string capture_fd2(const std::function<void()>& op) {
    std::fflush(stderr);
    std::cerr.flush();
    char path[] = "/tmp/sdirk3_rt_stderr_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { fail("capture_fd2 mkstemp: %s", std::strerror(errno)); op(); return {}; }
    int saved = dup(2);
    dup2(fd, 2);
    try { op(); } catch (...) { /* markers already on fd 2; rethrow below */
        std::fflush(stderr); std::cerr.flush(); dup2(saved, 2); close(saved);
        close(fd); unlink(path); throw;
    }
    std::fflush(stderr);
    std::cerr.flush();
    dup2(saved, 2);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    std::string out;
    char buf[4096]; ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
    close(fd);
    unlink(path);
    return out;
}

// A checked-ABI negative: it must return `want_status` AND the captured
// stderr must carry the EXACT reason marker. This closes the laundering path
// where an unrelated earlier failure also yields a nonzero/zero status.
void expect_status_marker(const std::function<int()>& call, int want_status,
                          const char* marker, const char* label) {
    int status = ~want_status;
    std::string err = capture_fd2([&] { status = call(); });
    CHECK_TRUE(status == want_status,
        "%s: status=%d want=%d (stderr=<<%s>>)", label, status, want_status,
        err.c_str());
    CHECK_TRUE(err.find(marker) != std::string::npos,
        "%s: marker '%s' absent from reason (stderr=<<%s>>)", label, marker,
        err.c_str());
}

// ---------------------------------------------------------------------------
// Shared geometry: 8x8 domain, width-1 halo, full-world Cartesian grid.
// ---------------------------------------------------------------------------
struct FullGrid {
    MPI_Comm cart = MPI_COMM_NULL;
    int npx = 1, npy = 1, px = 0, py = 0;
    int nx = 8, ny = 8;             // per-rank patch extents
    int ips = 1, ipe = 8, jps = 1, jpe = 8;
};

FullGrid make_full_grid(int size, int rank) {
    FullGrid g;
    int dims[2] = { size > 1 ? 2 : 1, size > 3 ? 2 : 1 }, per[2] = {0, 0};
    MPI_Dims_create(size, 2, dims);
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, per, 0, &g.cart);
    int coords[2];
    MPI_Cart_coords(g.cart, rank, 2, coords);
    g.npy = dims[0]; g.npx = dims[1]; g.py = coords[0]; g.px = coords[1];
    g.nx = 8 / g.npx; g.ny = 8 / g.npy;
    g.ips = 1 + g.px * g.nx; g.ipe = g.ips + g.nx - 1;
    g.jps = 1 + g.py * g.ny; g.jpe = g.jps + g.ny - 1;
    return g;
}

void publish_full_grid(const FullGrid& g) {
    set_wrf_communicator(MPI_Comm_c2f(g.cart), false, false);
    g_sdirk3_config.halo_width = 1;
    int st = sdirk3_halo_prepare_checked(
        1,8,1,8,1,3, g.ips-1,g.ipe+1,g.jps-1,g.jpe+1,1,3,
        g.ips,g.ipe,g.jps,g.jpe,1,3, g.npx,g.npy,g.px,g.py, 1);
    CHECK_TRUE(st == 1, "full-grid prepare st=%d", st);
}

// ===========================================================================
// Sections 0-2: baseline establishment and single-flight scope
// ===========================================================================
void run_scope_section() {
    g_section = &c_scope;
    using namespace mpi_safety;
    // 0: NEVER adopt the first caller as the baseline
    {
        std::string msg;
        CHECK_TRUE(threw_with_marker(
            [] { MPIExchangeScope s(MPIExchangeKind::Lifecycle, "pre-establish"); },
            "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION", &msg),
            "scope before establishment admitted (%s)", msg.c_str());
    }
    // 1: the UNDERSCORE ABI spelling must establish exactly like the primary
    sdirk3_mpi_safety_init_();
    CHECK_TRUE(mpi_baseline_thread_level() >= MPI_THREAD_SINGLE,
        "MPI baseline path did not execute (level=%d)",
        mpi_baseline_thread_level());
    {
        MPIExchangeScope s(MPIExchangeKind::Lifecycle, "post-underscore-init");
        tally();
    }
    // 2a: a worker thread is rejected BEFORE any MPI call
    {
        bool wok = false; std::string wmsg;
        std::thread w([&] {
            wok = threw_with_marker(
                [] { MPIExchangeScope s(MPIExchangeKind::FieldPrimitive, "worker"); },
                "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION", &wmsg);
        });
        w.join();
        CHECK_TRUE(wok, "worker scope entry admitted (%s)", wmsg.c_str());
    }
    // 2b: only FieldPrimitive-inside-batch nesting is legal
    try {
        mpi_safety::MPIExchangeScope outer(
            mpi_safety::MPIExchangeKind::ForwardBatch, "outer");
        CHECK_TRUE(threw_with_marker(
            [] { mpi_safety::MPIExchangeScope inner(
                     mpi_safety::MPIExchangeKind::Lifecycle, "inner"); },
            "SDIRK3_MPI_CONCURRENT_EXCHANGE_UNSUPPORTED"),
            "Lifecycle nested inside batch admitted");
        mpi_safety::MPIExchangeScope legal(
            mpi_safety::MPIExchangeKind::FieldPrimitive, "legal nested");
        tally();
    } catch (const std::exception& e) {
        fail("legal nesting rejected: %s", e.what());
    }
}

// ===========================================================================
// Section 3: checked communicator contracts (3b-1)
// ===========================================================================
void run_comm_section() {
    g_section = &c_comm;
    // Each negative asserts status 0 AND the EXACT reason marker on stderr —
    // a status-only check goes green even when the wrong branch fired.
    expect_status_marker(
        [] { return sdirk3_set_mpi_comm_checked(MPI_Comm_c2f(MPI_COMM_NULL), 0, 0); },
        0, "SDIRK3_MPI_COMM_REQUIRED", "null comm");
    expect_status_marker(
        [] { return sdirk3_set_mpi_comm_checked(MPI_Comm_c2f(MPI_COMM_WORLD), 0, 0); },
        0, "SDIRK3_MPI_COMM_REQUIRED", "non-Cartesian comm");
    // 1x1 probe grids exclude ranks >= 1 at np>1 (MPI_COMM_NULL there);
    // only grid members exercise the shaped-communicator contracts.
    int dims[2] = {1, 1}, periods[2] = {1, 0};
    MPI_Comm per; MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &per);
    if (per != MPI_COMM_NULL) {
        expect_status_marker(
            [&] { return sdirk3_set_mpi_comm_checked(MPI_Comm_c2f(per), 0, 0); },
            0, "SDIRK3_MPI_COMM_PERIODIC_TOPOLOGY_UNSUPPORTED", "periodic comm");
    }
    int d1[1] = {1}, p1[1] = {0}; MPI_Comm cart1d;
    MPI_Cart_create(MPI_COMM_WORLD, 1, d1, p1, 0, &cart1d);
    if (cart1d != MPI_COMM_NULL) {
        expect_status_marker(
            [&] { return sdirk3_set_mpi_comm_checked(MPI_Comm_c2f(cart1d), 0, 0); },
            0, "SDIRK3_MPI_COMM_TOPOLOGY_MISMATCH", "1-D Cartesian");
    }
    int p0[2] = {0, 0}; MPI_Comm ok;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, p0, 0, &ok);
    if (ok != MPI_COMM_NULL) {
        CHECK_TRUE(sdirk3_set_mpi_comm_checked(MPI_Comm_c2f(ok), 0, 0) == 1,
            "valid comm rejected");
    }
    halo_exchange_finalize();  // Policy A: end this configuration lifetime
}

// ===========================================================================
// Section 4: checked prepare contracts (3b-2, sole geometry authority)
// The 1x1 grid excludes ranks >= 1; the contracts are rank-local.
// ===========================================================================
void run_prepare_section() {
    g_section = &c_prepare;
    int dims[2] = {1,1}, p0[2] = {0,0}; MPI_Comm cart;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, p0, 0, &cart);
    if (cart == MPI_COMM_NULL) return;  // outside the probe grid at np>1
    set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
    g_sdirk3_config.halo_width = 2;
    // Baseline geometry: patch i/j = 1..10, memory = -1..12 (width-2 margin),
    // vertical = 1..5. Every negative below asserts the EXACT reason marker.
    CHECK_TRUE(sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                           1,10,1,10,1,5, 1,1,0,0, 2) == 1,
        "first prepare rejected");
    CHECK_TRUE(sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                           1,10,1,10,1,5, 1,1,0,0, 2) == 1,
        "identical re-prepare not a no-op");
    // FINGERPRINT-ONLY mutation (review P1): horizontal geometry, margins,
    // bound ordering, and process grid all stay valid — ONLY the vertical
    // triplet changes (1..5 -> 1..6), so the prepare reaches the 23-input
    // fingerprint equality and rejects with RECONFIGURATION_UNSUPPORTED.
    // The old prep(9) failed at the exact-margin check and never proved this.
    expect_status_marker(
        [] { return sdirk3_halo_prepare_checked(1,10,1,10,1,6, -1,12,-1,12,1,6,
                                                1,10,1,10,1,6, 1,1,0,0, 2); },
        0, "SDIRK3_MPI_HALO_RECONFIGURATION_UNSUPPORTED",
        "vertical fingerprint mutation");
    // Geometry negatives — each rejected BEFORE the fingerprint, at the
    // GEOMETRY_UNSUPPORTED gate (undersized margin / hostile bounds /
    // asymmetric margin / out-of-range coordinate on a 1x1 grid).
    expect_status_marker(
        [] { return sdirk3_halo_prepare_checked(1,10,1,10,1,5, 0,11,0,11,1,5,
                                                1,10,1,10,1,5, 1,1,0,0, 2); },
        0, "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED", "undersized memory halo");
    expect_status_marker(
        [] { return sdirk3_halo_prepare_checked(1,10,1,10,1,5,
                                                INT32_MIN,INT32_MAX,-1,12,1,5,
                                                1,10,1,10,1,5, 1,1,0,0, 2); },
        0, "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED", "hostile INT bounds");
    expect_status_marker(
        [] { return sdirk3_halo_prepare_checked(1,10,1,10,1,5, -2,12,-1,12,1,5,
                                                1,10,1,10,1,5, 1,1,0,0, 2); },
        0, "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED", "asymmetric margin");
    expect_status_marker(
        [] { return sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                                1,10,1,10,1,5, 1,1,0,1, 2); },
        0, "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED", "out-of-range coordinate");
    // Worker prepare: the scope rejects it and the checked wrapper surfaces
    // the THREAD_CONTRACT reason (captured across the joined worker thread).
    {
        std::string err = capture_fd2([&] {
            int wst = 1;
            std::thread w([&] {
                wst = sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                                  1,10,1,10,1,5, 1,1,0,0, 2);
            });
            w.join();
            CHECK_TRUE(wst == 0, "worker prepare returned %d", wst);
        });
        CHECK_TRUE(err.find("SDIRK3_MPI_THREAD_CONTRACT_VIOLATION") != std::string::npos,
            "worker prepare missing THREAD_CONTRACT reason (stderr=<<%s>>)",
            err.c_str());
    }
    halo_exchange_finalize();
    // Policy A: finalize ended the configuration lifetime — a new one
    // begins with a fresh set_wrf_communicator.
    set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
    CHECK_TRUE(sdirk3_halo_prepare_checked(1,9,1,10,1,5, -1,11,-1,12,1,5,
                                           1,9,1,10,1,5, 1,1,0,0, 2) == 1,
        "post-finalize reconfigure rejected");
    halo_exchange_finalize();
}

// ===========================================================================
// Section 4b: mypx/mypy ARE in the fingerprint (review P1). Needs a real
// multi-rank grid: publish at the true coordinate, then re-prepare with a
// DIFFERENT but IN-RANGE coordinate (same patch bounds). The coordinate-range
// gate passes, so the rejection can only come from the fingerprint.
// ===========================================================================
void run_coordinate_fingerprint_section(int size, int rank) {
    g_section = &c_prepare;
    if (size == 1) return;  // no alternate coordinate exists on a 1x1 grid
    FullGrid g = make_full_grid(size, rank);
    publish_full_grid(g);
    auto reprep = [&](int px, int py) {
        return sdirk3_halo_prepare_checked(
            1,8,1,8,1,3, g.ips-1,g.ipe+1,g.jps-1,g.jpe+1,1,3,
            g.ips,g.ipe,g.jps,g.jpe,1,3, g.npx,g.npy,px,py, 1);
    };
    if (g.npx > 1) {
        int alt = (g.px + 1) % g.npx;
        expect_status_marker([&] { return reprep(alt, g.py); }, 0,
            "SDIRK3_MPI_HALO_RECONFIGURATION_UNSUPPORTED", "alternate mypx");
    }
    if (g.npy > 1) {
        int alt = (g.py + 1) % g.npy;
        expect_status_marker([&] { return reprep(g.px, alt); }, 0,
            "SDIRK3_MPI_HALO_RECONFIGURATION_UNSUPPORTED", "alternate mypy");
    }
    halo_exchange_finalize();
}

// ===========================================================================
// Section 5: freshness contracts (3b-3 parts 1-2)
// ===========================================================================
void expect_stale(uint64_t t, int id, const char* label) {
    tally();
    std::string msg;
    if (!threw_with_marker(
            [&] { G::requireFreshHaloEpoch(t, id, label); },
            "SDIRK3_MPI_HALO_STALE", &msg)) {
        fail("%s: expected SDIRK3_MPI_HALO_STALE (%s)", label,
             msg.empty() ? "no throw" : msg.c_str());
    }
}

void run_freshness_section(int size, int rank) {
    g_section = &c_freshness;
    // not-required raw require succeeds; the CHECKED wrapper adds the
    // prepared gate the raw API lacks (NOT_PREPARED before rank prepare)
    G::requireFreshHaloEpoch(1, 1, "pre-publish");
    tally();
    CHECK_TRUE(!G::inspectHaloFreshness().required, "required before publish");
    expect_status_marker([] { return sdirk3_require_halo_fresh_checked(1, 1); },
        0, "SDIRK3_MPI_HALO_NOT_PREPARED", "require before prepare");
    // pre-publish notify (any thread) is a TRUE no-op: status 1, no credit
    {
        auto before = G::inspectHaloFreshness();
        const uint64_t marks_before = G::getMarkEventCount();
        int wst = -1;
        std::thread w([&] { wst = sdirk3_notify_halo_fresh_checked(); });
        w.join();
        auto after = G::inspectHaloFreshness();
        CHECK_TRUE(wst == 1, "no-op worker notify st=%d", wst);
        CHECK_TRUE(after.published == before.published &&
                   G::getMarkEventCount() == marks_before,
            "no-op notify accumulated credit");
    }
    FullGrid g = make_full_grid(size, rank);
    publish_full_grid(g);
    CHECK_TRUE(G::inspectHaloFreshness().required == (size > 1),
        "required=%d for size=%d", (int)G::inspectHaloFreshness().required, size);
    // hostile inputs are rejected BEFORE any consumption attempt, at the
    // dedicated input gate (not swallowed by a later stale/prepared check)
    expect_status_marker([] { return sdirk3_require_halo_fresh_checked(-1, 1); },
        0, "SDIRK3_MPI_HALO_FRESHNESS_INVALID_INPUT", "negative timestep");
    expect_status_marker([] { return sdirk3_require_halo_fresh_checked(1, 0); },
        0, "SDIRK3_MPI_HALO_FRESHNESS_INVALID_INPUT", "read_id=0");
    if (size == 1) {
        // prepared no-exchange configuration: trivially fresh, status 1,
        // nothing consumed (the WRF single-rank production path)
        auto before = G::inspectHaloFreshness();
        CHECK_TRUE(sdirk3_require_halo_fresh_checked(5, 1) == 1,
            "serial require rejected");
        auto after = G::inspectHaloFreshness();
        CHECK_TRUE(before.consumed == after.consumed &&
                   before.published == after.published,
            "serial require consumed state");
    } else {
        expect_stale(1, 1, "require-without-mark");
        // WORKER FORGE (P1-2): a worker must not be able to publish
        {
            auto before = G::inspectHaloFreshness();
            const uint64_t marks_before = G::getMarkEventCount();
            int wst = -1; bool wmark = false; std::string wmsg;
            std::thread w([&] {
                wst = sdirk3_notify_halo_fresh_checked();
                wmark = threw_with_marker([] { G::markHaloFresh(); },
                    "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION", &wmsg);
            });
            w.join();
            auto after = G::inspectHaloFreshness();
            CHECK_TRUE(wst == 0, "worker checked notify st=%d", wst);
            CHECK_TRUE(wmark, "worker mark admitted (%s)", wmsg.c_str());
            CHECK_TRUE(after.published == before.published &&
                       after.consumed == before.consumed &&
                       G::getMarkEventCount() == marks_before,
                "worker forge mutated freshness state");
        }
        CHECK_TRUE(sdirk3_notify_halo_fresh_checked() == 1,
            "baseline checked notify rejected");
        // WORKER-FIRST: a worker's require is rejected BEFORE consuming
        {
            bool wok = false; std::string wmsg;
            auto before = G::inspectHaloFreshness();
            std::thread w([&] {
                wok = threw_with_marker(
                    [] { G::requireFreshHaloEpoch(1, 1, "worker-first"); },
                    "SDIRK3_MPI_THREAD_CONTRACT_VIOLATION", &wmsg);
            });
            w.join();
            auto after = G::inspectHaloFreshness();
            CHECK_TRUE(wok, "worker require admitted (%s)", wmsg.c_str());
            CHECK_TRUE(before.consumed == after.consumed,
                "worker require consumed state");
        }
        // inspection never consumes
        {
            auto s1 = G::inspectHaloFreshness();
            auto s2 = G::inspectHaloFreshness();
            CHECK_TRUE(s1.consumed == s2.consumed && s1.published == s2.published,
                "inspect consumed state");
        }
        G::requireFreshHaloEpoch(1, 1, "first");       tally();
        G::requireFreshHaloEpoch(1, 1, "idempotent");  tally();
        expect_stale(2, 1, "new-timestep-no-mark");
        sdirk3_notify_halo_fresh();  // legacy void spelling still publishes
        // the checked ABI consumes exactly like the raw API; a worker
        // calling it is status 0 without touching consumed state
        {
            int wst = -1;
            auto before = G::inspectHaloFreshness();
            std::thread w([&] { wst = sdirk3_require_halo_fresh_checked(2, 1); });
            w.join();
            auto after = G::inspectHaloFreshness();
            CHECK_TRUE(wst == 0, "worker checked require st=%d", wst);
            CHECK_TRUE(before.consumed == after.consumed,
                "worker checked require consumed");
        }
        CHECK_TRUE(sdirk3_require_halo_fresh_checked(2, 1) == 1,
            "checked consume rejected");
        CHECK_TRUE(sdirk3_require_halo_fresh_checked(2, 1) == 1,
            "checked idempotent re-check rejected");
        // seed the idempotence key that lifecycle B must NOT honor
        sdirk3_notify_halo_fresh();
        G::requireFreshHaloEpoch(7, 1, "seed-key"); tally();
        // leave an UNCONSUMED credit behind, then end lifecycle A
        sdirk3_notify_halo_fresh();
        halo_exchange_finalize();
        CHECK_TRUE(!G::inspectHaloFreshness().required, "required after finalize");
        // lifecycle B (Policy A: new configuration after explicit finalize)
        publish_full_grid(g);
        // P1-1a: lifecycle A's unconsumed mark must NOT satisfy lifecycle B
        expect_stale(8, 1, "inherited-credit");
        // P1-1b: lifecycle A's (t=7,id=1) key must NOT pass idempotently
        expect_stale(7, 1, "inherited-idempotence-key");
        CHECK_TRUE(sdirk3_notify_halo_fresh_checked() == 1, "lifecycle B notify");
        G::requireFreshHaloEpoch(8, 1, "lifecycle-B-consume"); tally();
    }
    halo_exchange_finalize();
    CHECK_TRUE(!G::inspectHaloFreshness().required, "required after final finalize");
}

// ===========================================================================
// Section 6: AD lifecycle-epoch binding (3b-3 part 3)
// ===========================================================================
struct ADSetup {
    ADHaloConfig cfg{};
    int64_t packed_len = 0;
};

ADSetup make_ad_setup(const FullGrid& g, int size) {
    publish_full_grid(g);
    auto nbrs = halo_exchange_get_neighbors();
    ADSetup s;
    auto& cfg = s.cfg;
    const int nk = 3;
    cfg.nx = g.nx; cfg.ny = g.ny; cfg.nz = nk;
    cfg.nx_u = g.nx + 1; cfg.ny_v = g.ny + 1; cfg.nz_w = nk + 1;
    cfg.ni_mem = g.nx + 2; cfg.nj_mem = g.ny + 2;
    cfg.i_off = 1; cfg.j_off = 1; cfg.halo_size = 1;
    cfg.periodic_x = false; cfg.periodic_y = false;
    cfg.sym_xs = cfg.sym_xe = cfg.sym_ys = cfg.sym_ye = true;
    cfg.open_xs = cfg.open_xe = cfg.open_ys = cfg.open_ye = false;
    cfg.its = g.ips; cfg.ite = g.ipe; cfg.jts = g.jps; cfg.jte = g.jpe;
    cfg.ids = 1; cfg.ide = 8; cfg.jds = 1; cfg.jde = 8;
    cfg.neighbor_north = nbrs.neighbor_north;
    cfg.neighbor_south = nbrs.neighbor_south;
    cfg.neighbor_east = nbrs.neighbor_east;
    cfg.neighbor_west = nbrs.neighbor_west;
    cfg.exchange_performed = (size > 1);
    int64_t plane = int64_t{cfg.nj_mem} * cfg.ni_mem;
    s.packed_len = plane * (3 * cfg.nz + 2 * cfg.nz_w) + plane;
    return s;
}

void expect_epoch_changed(torch::Tensor& x, torch::Tensor& loss,
                          const char* label) {
    tally();
    std::string msg = "backward admitted";
    bool ok = false;
    try { loss.backward(); }
    catch (const std::exception& e) {
        ok = std::strstr(e.what(), "SDIRK3_MPI_HALO_EPOCH_CHANGED") != nullptr;
        if (!ok) msg = e.what();
    }
    if (!ok) fail("%s: %s", label, msg.c_str());
    CHECK_TRUE(!x.grad().defined(), "%s: gradient produced on failure", label);
}

void run_ad_epoch_section(int size, int rank) {
    g_section = &c_ad;
    FullGrid g = make_full_grid(size, rank);
    torch::manual_seed(4321 + rank);
    // A. stable lifecycle: forward -> backward succeeds with finite grad
    {
        ADSetup s = make_ad_setup(g, size);
        auto x = torch::rand({s.packed_len}, torch::kFloat32).requires_grad_(true);
        auto z = performADHaloExchange(x, s.cfg);
        auto loss = z.pow(2).sum();
        loss.backward();
        bool grad_ok = x.grad().defined();
        if (grad_ok) {
            torch::NoGradGuard no_grad;
            grad_ok = x.grad().isfinite().all().item<bool>();
        }
        CHECK_TRUE(grad_ok, "stable-lifecycle grad missing/nonfinite");
        halo_exchange_finalize();
    }
    if (size > 1) {
        // B. forward -> finalize -> backward = EPOCH_CHANGED, gradients
        // unproduced, no MPI call (all ranks return without deadlock)
        {
            ADSetup s = make_ad_setup(g, size);
            auto x = torch::rand({s.packed_len}, torch::kFloat32).requires_grad_(true);
            auto z = performADHaloExchange(x, s.cfg);
            auto loss = z.pow(2).sum();
            halo_exchange_finalize();
            expect_epoch_changed(x, loss, "finalize-then-backward");
        }
        // C. forward -> finalize -> RE-PREPARE (active=true again) ->
        // backward: a live lifecycle is still not the CAPTURED lifecycle
        {
            ADSetup s = make_ad_setup(g, size);
            auto x = torch::rand({s.packed_len}, torch::kFloat32).requires_grad_(true);
            auto z = performADHaloExchange(x, s.cfg);
            auto loss = z.pow(2).sum();
            halo_exchange_finalize();
            publish_full_grid(g);
            CHECK_TRUE(halo_exchange_requires_exchange(), "re-prepare not active");
            expect_epoch_changed(x, loss, "reprepare-then-backward");
            halo_exchange_finalize();
        }
    } else {
        // D. np1 identity path stays unenforced
        ADSetup s = make_ad_setup(g, size);
        auto x = torch::rand({s.packed_len}, torch::kFloat32).requires_grad_(true);
        auto z = performADHaloExchange(x, s.cfg);
        auto loss = z.pow(2).sum();
        halo_exchange_finalize();
        loss.backward();
        CHECK_TRUE(x.grad().defined(), "np1 identity backward rejected");
    }
}

// ===========================================================================
// Section 7: field-semantics contracts (review P2) over the FULL risk matrix
//   topology: np1 1x1 | np2 2x1 AND 1x2 | np4 2x2
//   periodicity: (f,f) (t,f) (f,t) (t,t)   — 2-rank periodic same-peer
//   width: 1 and 2
// For every cell, all three properties hold BITWISE (torch::equal):
//   - the exchange never touches INTERIOR points
//   - H(Hx) == Hx (idempotence)
//   - halo_exchange_multiple({a,b,c}) == sequential(a,b,c)
// The MPI cart is always NON-periodic (dim0=y, dim1=x); logical wraparound
// is the set_wrf_communicator flag, exactly like the raw halo contract.
// ===========================================================================
void block_range(int n, int p, int coord, int& start, int& end) {
    int base = n / p, rem = n % p;
    start = coord * base + std::min(coord, rem);
    end = start + base + (coord < rem ? 1 : 0) - 1;  // inclusive
}

void run_field_case(int npx, int npy, bool per_x, bool per_y, int width,
                    const char* label) {
    int dims[2] = {npy, npx}, periods[2] = {0, 0};
    MPI_Comm cart;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart);
    int coords[2], cdims[2], cper[2];
    MPI_Cart_get(cart, 2, cdims, cper, coords);
    int mypy = coords[0], mypx = coords[1];
    // uneven global domain (13 x 11) so block_range remainders are exercised
    const int ids = 1, ide = 13, jds = 1, jde = 11, kds = 1, kde = 5;
    int i0, i1, j0, j1;
    block_range(ide - ids + 1, npx, mypx, i0, i1);
    block_range(jde - jds + 1, npy, mypy, j0, j1);
    int ips = ids + i0, ipe = ids + i1, jps = jds + j0, jpe = jds + j1;
    int ims = ips - width, ime = ipe + width;
    int jms = jps - width, jme = jpe + width;
    int ni_mem = ime - ims + 1, nj_mem = jme - jms + 1, nk = kde - kds + 1;

    set_wrf_communicator(MPI_Comm_c2f(cart), per_x, per_y);
    g_sdirk3_config.halo_width = width;
    int st = sdirk3_halo_prepare_checked(
        ids, ide, jds, jde, kds, kde, ims, ime, jms, jme, kds, kde,
        ips, ipe, jps, jpe, kds, kde, npx, npy, mypx, mypy, width);
    CHECK_TRUE(st == 1, "%s: prepare st=%d", label, st);

    using torch::indexing::Slice;
    int nx = ipe - ips + 1, ny = jpe - jps + 1;
    auto interior = [&](const torch::Tensor& t) {
        return t.index({Slice(width, width + ny), Slice(), Slice(width, width + nx)});
    };
    auto base = torch::rand({nj_mem, nk, ni_mem}, torch::kFloat32);
    // interior preservation
    auto once = base.clone();
    halo_exchange_3d_tensor(once);
    CHECK_TRUE(torch::equal(interior(once), interior(base)),
        "%s: exchange mutated interior points", label);
    // bitwise idempotence
    auto twice = once.clone();
    halo_exchange_3d_tensor(twice);
    CHECK_TRUE(torch::equal(twice, once), "%s: H(Hx) != Hx", label);
    // batched == sequential (bitwise) — 3 fields, so corner propagation and
    // the periodic same-peer crossover are exercised per field
    std::vector<torch::Tensor> batch = {
        base.clone(), (base * 2).clone(), (base + 1).clone() };
    std::vector<torch::Tensor> seq = {
        base.clone(), (base * 2).clone(), (base + 1).clone() };
    halo_exchange_multiple(batch);
    for (auto& t : seq) halo_exchange_3d_tensor(t);
    for (size_t i = 0; i < batch.size(); ++i) {
        CHECK_TRUE(torch::equal(batch[i], seq[i]),
            "%s: halo_exchange_multiple[%zu] != sequential", label, i);
    }
    halo_exchange_finalize();
    MPI_Comm_free(&cart);
}

void run_field_semantics_section(int size, int rank) {
    g_section = &c_field;
    torch::manual_seed(97 + rank);
    std::vector<std::pair<int, int>> topos;   // (npx, npy)
    if (size == 1)      topos = {{1, 1}};
    else if (size == 2) topos = {{2, 1}, {1, 2}};
    else                topos = {{2, 2}};
    const bool per[4][2] = {{false,false},{true,false},{false,true},{true,true}};
    for (auto [npx, npy] : topos) {
        for (const auto& p : per) {
            for (int width : {1, 2}) {
                char label[64];
                std::snprintf(label, sizeof label, "%dx%d p%d%d w%d",
                              npx, npy, (int)p[0], (int)p[1], width);
                run_field_case(npx, npy, p[0], p[1], width, label);
            }
        }
    }
}

// ===========================================================================
// Section 8: children. The candidate-rollback fault latches the process
// permanently and the void-ABI failure ABORTS — neither can share the main
// process. Rank 0 re-execs this binary as an MPI SINGLETON with the
// launcher environment scrubbed.
// ===========================================================================
struct ChildResult {
    bool ran = false;
    bool nonzero = false;   // exited nonzero OR killed by a signal
    int exit_code = -1;
    std::string output;
};

// Build the child environment ENTIRELY in the parent (review P2): a scrubbed
// copy of environ (launcher vars removed) plus the child-mode selector. No
// allocation or env rewriting happens between fork and exec — posix_spawn
// does the fd wiring and exec atomically from the prepared arrays.
std::vector<std::string> scrubbed_child_env(const char* mode) {
    static const char* prefixes[] = {
        "PMI_", "PMIX_", "OMPI_", "HYDRA_", "MPIR_", "MPIEXEC_",
        "PRTE_", "PALS_", "SLURM_", "FLUX_", "WRF_SDIRK3_RUNTIME_CONTRACT_CHILD=",
    };
    std::vector<std::string> env;
    for (char** e = environ; *e != nullptr; ++e) {
        bool drop = false;
        for (const char* p : prefixes) {
            if (std::strncmp(*e, p, std::strlen(p)) == 0) { drop = true; break; }
        }
        if (!drop) env.emplace_back(*e);
    }
    env.emplace_back(std::string("WRF_SDIRK3_RUNTIME_CONTRACT_CHILD=") + mode);
    return env;
}

ChildResult run_child(const char* argv0, const char* mode) {
    ChildResult r;
    char path[] = "/tmp/sdirk3_runtime_child_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { fail("mkstemp: %s", std::strerror(errno)); return r; }
    close(fd);  // posix_spawn reopens it via file actions

    std::vector<std::string> env = scrubbed_child_env(mode);
    std::vector<char*> envp;
    for (auto& s : env) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);
    char* argv[] = { const_cast<char*>(argv0), nullptr };

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // stdout and stderr -> the temp file (open once on fd 1, dup to fd 2)
    posix_spawn_file_actions_addopen(&fa, 1, path,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);

    pid_t pid = -1;
    int rc = posix_spawn(&pid, argv0, &fa, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        fail("posix_spawn(%s): %s", mode, std::strerror(rc));
        unlink(path);
        return r;
    }
    // a hung child must read as FAILURE well inside the ctest TIMEOUT
    int status = 0;
    const int deadline_ds = 1200;  // 120 s in 100 ms polls
    int waited = 0;
    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        if (got == pid) break;
        if (got < 0) { fail("waitpid(%s): %s", mode, std::strerror(errno)); return r; }
        if (++waited > deadline_ds) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fail("child mode '%s' hung past 120 s", mode);
            return r;
        }
        usleep(100000);
    }
    r.ran = true;
    if (WIFEXITED(status)) {
        r.exit_code = WEXITSTATUS(status);
        r.nonzero = (r.exit_code != 0);
    } else if (WIFSIGNALED(status)) {
        r.exit_code = -WTERMSIG(status);
        r.nonzero = true;
    }
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    r.output = ss.str();
    unlink(path);
    return r;
}

void run_children_section(const char* argv0, int rank) {
    g_section = &c_child;
    if (rank == 0) {
        // candidate-rollback fault: injected rollback free failure must
        // latch the (child) process permanently; child validates and
        // exits 0 on success.
        {
            ChildResult r = run_child(argv0, "rollback-fault");
            CHECK_TRUE(r.ran && r.exit_code == 0,
                "rollback-fault child exit=%d\n--- child output ---\n%s",
                r.exit_code, r.output.c_str());
        }
        // void-ABI coordinated abort: a checked failure behind the void
        // sdirk3_halo_finalize must terminate loudly, never return.
        {
            ChildResult r = run_child(argv0, "abort-finalize");
            CHECK_TRUE(r.ran && r.nonzero,
                "abort-finalize child exited 0 (abort did not fire)\n%s",
                r.output.c_str());
            CHECK_TRUE(r.output.find("SDIRK3_C_ABI_EXCEPTION") != std::string::npos,
                "abort-finalize child missing SDIRK3_C_ABI_EXCEPTION marker\n%s",
                r.output.c_str());
        }
        // PR 9C.3 commit 4: MPI-thread-level fatal policy.
        // Baseline-thread fatal -> coordinated MPI_Abort (no LOCAL_ABORT).
        {
            ChildResult r = run_child(argv0, "baseline-abort");
            CHECK_TRUE(r.ran && r.nonzero,
                "baseline-abort child exited 0\n%s", r.output.c_str());
            CHECK_TRUE(r.output.find("SDIRK3_C_ABI_EXCEPTION") != std::string::npos,
                "baseline-abort child missing marker\n%s", r.output.c_str());
            CHECK_TRUE(r.output.find("SDIRK3_C_ABI_EXCEPTION_LOCAL_ABORT") == std::string::npos,
                "baseline-abort child took the LOCAL_ABORT path\n%s",
                r.output.c_str());
        }
        // Non-baseline worker fatal under FUNNELED/SERIALIZED -> NO MPI
        // calls: LOCAL_ABORT marker + local hard abort.
        {
            ChildResult r = run_child(argv0, "worker-abort");
            CHECK_TRUE(r.ran && r.nonzero,
                "worker-abort child exited 0\n%s", r.output.c_str());
            CHECK_TRUE(r.output.find("SDIRK3_C_ABI_EXCEPTION") != std::string::npos,
                "worker-abort child missing marker\n%s", r.output.c_str());
            CHECK_TRUE(r.output.find("SDIRK3_C_ABI_EXCEPTION_LOCAL_ABORT") != std::string::npos,
                "worker-abort child missing the LOCAL_ABORT marker (it "
                "called MPI off-baseline?)\n%s", r.output.c_str());
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

// ===========================================================================
// Section 9 (LAST): explicit-finalize failure -> permanent fault latch.
// After this the process may not re-enable MPI halo — nothing runs after.
// ===========================================================================
void run_finalize_latch_section() {
    g_section = &c_latch;
    int dims[2] = {1,1}, p0[2] = {0,0}; MPI_Comm cart;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, p0, 0, &cart);
    if (cart == MPI_COMM_NULL) return;  // rank-local latch contracts
    set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
    g_sdirk3_config.halo_width = 2;
    CHECK_TRUE(sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                           1,10,1,10,1,5, 1,1,0,0, 2) == 1,
        "latch-section prepare rejected");
    setenv("WRF_SDIRK3_TEST_FAIL_COMM_FREE", "1", 1);
    CHECK_TRUE(threw_with_marker([] { halo_exchange_finalize(); },
                                 "SDIRK3_MPI_CALL_FAILED"),
        "injected finalize failure did not surface");
    unsetenv("WRF_SDIRK3_TEST_FAIL_COMM_FREE");
    CHECK_TRUE(!halo_exchange_is_initialized(), "ready survived failed finalize");
    CHECK_TRUE(!halo_exchange_requires_exchange(), "active survived failed finalize");
    CHECK_TRUE(sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                           1,10,1,10,1,5, 1,1,0,0, 2) == 0,
        "prepare after fault admitted");
    CHECK_TRUE(threw_with_marker(
        [&] { set_wrf_communicator(MPI_Comm_c2f(cart), false, false); },
        "SDIRK3_MPI_HALO_LIFECYCLE_FAULTED"),
        "setter after fault admitted or wrong marker");
}

// ===========================================================================
// Child-mode bodies (MPI singletons; launcher env already scrubbed)
// ===========================================================================
int child_rollback_fault_main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    sdirk3_mpi_safety_init();
    int dims[2] = {1,1}, p0[2] = {0,0}; MPI_Comm cart;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, p0, 0, &cart);
    set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
    g_sdirk3_config.halo_width = 2;
    int f = 0;
    // declared 2x1 grid vs 1-rank communicator: validation fails AFTER the
    // candidate duplicate; the injected rollback free failure must latch.
    setenv("WRF_SDIRK3_TEST_FAIL_CANDIDATE_ROLLBACK_FREE", "1", 1);
    if (sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                    1,10,1,10,1,5, 2,1,0,0, 2) != 0)
        { std::printf("CHILD FAIL: mismatched grid admitted\n"); f++; }
    unsetenv("WRF_SDIRK3_TEST_FAIL_CANDIDATE_ROLLBACK_FREE");
    if (halo_exchange_is_initialized()) { std::printf("CHILD FAIL: ready true\n"); f++; }
    if (halo_exchange_requires_exchange()) { std::printf("CHILD FAIL: active true\n"); f++; }
    // corrected geometry must STILL be rejected: process fault is permanent
    if (sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                    1,10,1,10,1,5, 1,1,0,0, 2) != 0)
        { std::printf("CHILD FAIL: prepare after rollback fault admitted\n"); f++; }
    try {
        set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
        std::printf("CHILD FAIL: setter after rollback fault admitted\n"); f++;
    } catch (const std::exception& e) {
        if (!std::strstr(e.what(), "SDIRK3_MPI_HALO_LIFECYCLE_FAULTED"))
            { std::printf("CHILD FAIL: wrong latch marker: %s\n", e.what()); f++; }
    }
    std::printf(f ? "CHILD ROLLBACK: %d failure(s)\n"
                  : "CHILD ROLLBACK: all contracts hold\n", f);
    MPI_Finalize();
    return f;
}

int child_abort_finalize_main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    sdirk3_mpi_safety_init();
    int dims[2] = {1,1}, p0[2] = {0,0}; MPI_Comm cart;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, p0, 0, &cart);
    set_wrf_communicator(MPI_Comm_c2f(cart), false, false);
    g_sdirk3_config.halo_width = 2;
    if (sdirk3_halo_prepare_checked(1,10,1,10,1,5, -1,12,-1,12,1,5,
                                    1,10,1,10,1,5, 1,1,0,0, 2) != 1) {
        std::printf("CHILD FAIL: abort-mode prepare rejected\n");
        MPI_Finalize();
        return 0;  // parent expects NONZERO — a zero exit reads as failure
    }
    setenv("WRF_SDIRK3_TEST_FAIL_COMM_FREE", "1", 1);
    sdirk3_halo_finalize();  // void ABI: must coordinated-abort, never return
    std::printf("CHILD FAIL: void finalize returned after injected failure\n");
    MPI_Finalize();
    return 0;  // ditto: zero exit == the abort contract is broken
}

int child_baseline_abort_main(int argc, char** argv) {
    // PR 9C.3 commit 4: a fatal on the BASELINE thread must take the
    // coordinated MPI_Abort path — marker present, LOCAL_ABORT absent.
    MPI_Init(&argc, &argv);
    sdirk3_mpi_safety_init();
    wrf::sdirk3::mpi_safety::abort_c_abi_exception(
        "baseline-abort-child", "deliberate baseline fatal");
    // unreachable
}

int child_worker_abort_main(int argc, char** argv) {
    // PR 9C.3 commit 4: a fatal on a NON-baseline thread under a
    // FUNNELED/SERIALIZED runtime must NOT call MPI — the LOCAL_ABORT
    // marker fires and the process hard-aborts locally.
    MPI_Init(&argc, &argv);
    sdirk3_mpi_safety_init();  // establishes THIS (main) thread as baseline
    std::thread worker([] {
        wrf::sdirk3::mpi_safety::abort_c_abi_exception(
            "worker-abort-child", "deliberate worker fatal");
    });
    worker.join();  // never reached: the worker aborts the process
    std::printf("CHILD FAIL: worker abort returned\n");
    MPI_Finalize();
    return 0;  // parent expects NONZERO
}

// Coverage ratchet: rank 0 runs every section, so its per-section counts are
// fixed by the contract structure. A vanished section/negative changes a
// count and fails HERE, localizing which bundle regressed. Baked from the
// instrumented run; update deliberately when adding/removing a contract.
void assert_section_ratchet(int size, int rank) {
    g_section = nullptr;  // these meta-checks belong to no section
    if (rank != 0) return;
    const int si = (size == 1) ? 0 : (size == 2) ? 1 : 2;
    struct Row { const char* name; long got; long want[3]; };
    const Row rows[] = {
        {"scope",     c_scope,     {SCOPE_W}},
        {"comm",      c_comm,      {COMM_W}},
        {"prepare",   c_prepare,   {PREP_W}},
        {"freshness", c_freshness, {FRESH_W}},
        {"ad",        c_ad,        {AD_W}},
        {"field",     c_field,     {FIELD_W}},
        {"child",     c_child,     {CHILD_W}},
        {"latch",     c_latch,     {LATCH_W}},
    };
    for (const auto& r : rows) {
        if (r.want[si] < 0) {   // measurement mode: report, do not gate
            std::fprintf(stderr, "RATCHET-MEASURE np=%d %s=%ld\n",
                         size, r.name, r.got);
            continue;
        }
        CHECK_TRUE(r.got == r.want[si],
            "coverage ratchet: section '%s' ran %ld checks, expected %ld (np=%d)",
            r.name, r.got, r.want[si], size);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (const char* mode = std::getenv("WRF_SDIRK3_RUNTIME_CONTRACT_CHILD")) {
        if (std::strcmp(mode, "rollback-fault") == 0)
            return child_rollback_fault_main(argc, argv);
        if (std::strcmp(mode, "abort-finalize") == 0)
            return child_abort_finalize_main(argc, argv);
        if (std::strcmp(mode, "baseline-abort") == 0)
            return child_baseline_abort_main(argc, argv);
        if (std::strcmp(mode, "worker-abort") == 0)
            return child_worker_abort_main(argc, argv);
        std::fprintf(stderr, "unknown child mode: %s\n", mode);
        return 125;
    }
    MPI_Init(&argc, &argv);
    int size = 1, rank = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    run_scope_section();
    MPI_Barrier(MPI_COMM_WORLD);
    run_comm_section();
    MPI_Barrier(MPI_COMM_WORLD);
    run_prepare_section();
    MPI_Barrier(MPI_COMM_WORLD);
    run_coordinate_fingerprint_section(size, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    run_freshness_section(size, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    run_ad_epoch_section(size, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    run_field_semantics_section(size, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    run_children_section(argv[0], rank);
    run_finalize_latch_section();   // LAST: latches the process fault
    MPI_Barrier(MPI_COMM_WORLD);
    assert_section_ratchet(size, rank);

    int total_fails = 0;
    long total_checks = 0;
    MPI_Allreduce(&g_fails, &total_fails, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&g_checks, &total_checks, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD);
    if (rank == 0) {
        if (total_fails == 0) {
            std::printf("MPI RUNTIME CONTRACT PASSED: %ld checks, np=%d\n",
                        total_checks, size);
        } else {
            std::printf("MPI RUNTIME CONTRACT FAILED: %d failure(s), np=%d\n",
                        total_fails, size);
        }
    }
    MPI_Finalize();
    return total_fails == 0 ? 0 : 1;
}
