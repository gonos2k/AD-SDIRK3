// test_geometry_contract.cpp — regression matrix for the SINGLE per-call
// geometry contract used by validateCallGeometry (rounds 3g–3j).
//
// The geometry fail-close was the hardest contract to close in the stop-gate
// campaign, and until now its only evidence was manual runtime fault
// injection (WRF_SDIRK3_TEST_STAGGER_MISMATCH_AT / _INVALID_STAGGER_INIT).
// This exercises the EXACT shared predicate (wrf_sdirk3_geometry_contract.h)
// that production wraps, torch-free, so CI turns the one-time approval into a
// standing regression line.
//
// Covers the reviewer's requested matrix:
//   * base dim 0/negative                 -> call_sane fails
//   * stagger dim 0/negative              -> call_sane fails
//   * base-1 (stag < base)                -> stagger shape fails
//   * base+2 (stag > base+1)              -> stagger shape fails
//   * init/call mismatch                  -> matches_init fails
//   * normal collocated AND +1 stagger    -> ok
//   * INT_MAX-adjacent, no overflow UB    -> range rejects, no UB
//   * init geometry itself invalid        -> init_sane fails (the round-3j
//                                            "uninitialized => allow" escape)
//
//   c++ -std=c++17 test_geometry_contract.cpp -o t && ./t

#include "wrf_sdirk3_geometry_contract.h"

#include <climits>
#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (cond) {
        std::printf("PASS  %s\n", what);
    } else {
        std::printf("FAIL  %s\n", what);
        ++g_failures;
    }
}

using wrf::sdirk3::check_call_geometry;

// Convenience: check a (call == init) pair — the common case where the caller
// passes exactly what the solver was initialized with.
wrf::sdirk3::GeometryCheck same(long long nx, long long ny, long long nz,
                                long long nx_u, long long ny_v, long long nz_w) {
    return check_call_geometry(nx, ny, nz, nx_u, ny_v, nz_w,
                               nx, ny, nz, nx_u, ny_v, nz_w);
}

}  // namespace

int main() {
    // A representative valid em_b_wave-class geometry (base 41/81/64, +1 stagger).
    const long long NX = 41, NY = 81, NZ = 64;
    const long long NXU = NX + 1, NYV = NY + 1, NZW = NZ + 1;

    // --- Positive: +1 C-grid stagger (the em_b_wave shape) ---
    expect(same(NX, NY, NZ, NXU, NYV, NZW).ok(),
           "positive: +1 staggered geometry accepted");

    // --- Positive: fully collocated (stag == base) — the contract's memory-
    // safety layer accepts it; the split FEATURE separately requires +1. ---
    expect(same(NX, NY, NZ, NX, NY, NZ).ok(),
           "positive: collocated geometry accepted (contract layer)");

    // --- Positive: mixed (one axis collocated, others +1) ---
    expect(same(NX, NY, NZ, NX, NYV, NZW).ok(),
           "positive: mixed collocated/+1 axes accepted");

    // --- base dim 0 / negative -> call_sane fails ---
    {
        auto r = same(0, NY, NZ, 1, NYV, NZW);
        expect(!r.ok() && !r.call_sane, "base nx=0 rejected (call_sane)");
    }
    {
        auto r = same(NX, -3, NZ, NXU, -2, NZW);
        expect(!r.ok() && !r.call_sane, "base ny<0 rejected (call_sane)");
    }

    // --- stagger dim 0 / negative -> call_sane fails ---
    {
        auto r = same(NX, NY, NZ, 0, NYV, NZW);
        expect(!r.ok() && !r.call_sane, "stagger nx_u=0 rejected (call_sane)");
    }
    {
        auto r = same(NX, NY, NZ, NXU, NYV, -1);
        expect(!r.ok() && !r.call_sane, "stagger nz_w<0 rejected (call_sane)");
    }

    // --- base-1 (staggered SMALLER than base) -> stagger shape fails ---
    {
        auto r = same(NX, NY, NZ, NX - 1, NYV, NZW);
        expect(!r.ok() && r.call_sane && !r.call_stagger_ok,
               "nx_u = nx-1 rejected (stagger shape)");
    }

    // --- base+2 (staggered too LARGE) -> stagger shape fails ---
    {
        auto r = same(NX, NY, NZ, NXU, NY + 2, NZW);
        expect(!r.ok() && r.call_sane && !r.call_stagger_ok,
               "ny_v = ny+2 rejected (stagger shape)");
    }

    // --- init/call mismatch -> matches_init fails (call itself well-formed) ---
    {
        // call is a valid +1 geometry, but init is a DIFFERENT valid geometry.
        auto r = check_call_geometry(NX, NY, NZ, NXU, NYV, NZW,      // call
                                     NX, NY, NZ, NX,  NYV, NZW);      // init: nx_u collocated
        expect(!r.ok() && r.call_sane && r.call_stagger_ok &&
               r.init_sane && !r.matches_init,
               "call nx_u=nx+1 vs init nx_u=nx rejected (matches_init)");
    }
    {
        // The reviewer's original counterexample: init nx_u=nx+1, call nx_u=nx.
        auto r = check_call_geometry(NX, NY, NZ, NX,  NYV, NZW,       // call: collocated nx_u
                                     NX, NY, NZ, NXU, NYV, NZW);      // init: +1 nx_u
        expect(!r.ok() && r.call_sane && r.call_stagger_ok &&
               r.init_sane && !r.matches_init,
               "init nx_u=nx+1, call nx_u=nx rejected (round-3f counterexample)");
    }
    {
        auto r = check_call_geometry(NX, NY, NZ, NXU, NYV, NZW,       // call
                                     NX + 1, NY, NZ, NX + 2, NYV, NZW); // init: different base
        expect(!r.ok() && !r.matches_init,
               "call base nx=41 vs init base nx=42 rejected (matches_init)");
    }

    // --- init geometry itself invalid -> init_sane fails (round-3j escape) ---
    {
        // A zero/negative INIT base member must NOT disable the equality check.
        auto r = check_call_geometry(NX, NY, NZ, NXU, NYV, NZW,       // call: valid
                                     0,  NY, NZ, NXU, NYV, NZW);      // init: nx=0 corrupt
        expect(!r.ok() && r.call_sane && r.call_stagger_ok &&
               !r.init_sane && !r.matches_init,
               "init base nx=0 rejected (init_sane; no uninitialized escape)");
    }
    {
        auto r = check_call_geometry(NX, NY, NZ, NXU, NYV, NZW,
                                     NX, NY, NZ, -5, NYV, NZW);       // init: nx_u corrupt
        expect(!r.ok() && !r.init_sane,
               "init nx_u<0 rejected (init_sane)");
    }

    // --- INT_MAX / LLONG_MAX adjacent: rejected by range, NO overflow UB ---
    {
        const long long big = static_cast<long long>(INT_MAX);
        auto r = same(big, NY, NZ, big, NYV, NZW);
        expect(!r.ok() && !r.call_sane, "base nx=INT_MAX rejected (range, no UB)");
    }
    {
        // base exactly at the cap, stagger = base+1 exceeds the cap: the helper
        // must reject via range BEFORE relying on base+1 (which is well-defined
        // in int64 here anyway) — proves the ordering is range-first.
        auto r = same(wrf::sdirk3::kGeomMaxDim, NY, NZ,
                      wrf::sdirk3::kGeomMaxDim + 1, NYV, NZW);
        expect(!r.ok() && !r.call_sane,
               "nx=kGeomMaxDim, nx_u=cap+1 rejected (range-first, no overflow)");
    }
    {
        const long long big = LLONG_MAX;
        auto r = same(big, big, big, big, big, big);
        expect(!r.ok() && !r.call_sane, "all dims LLONG_MAX rejected (no UB)");
    }

    // --- boundary: exactly at the cap, collocated -> accepted ---
    {
        const long long cap = wrf::sdirk3::kGeomMaxDim;
        auto r = same(cap, cap, cap, cap, cap, cap);
        expect(r.ok(), "all dims == kGeomMaxDim, collocated -> accepted (boundary)");
    }

    if (g_failures == 0) {
        std::printf("ALL geometry_contract regression matrix PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
