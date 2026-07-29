// 9F.D45: base-state tile geometry contract (review sections 3 and 4).
//
// WHY THIS EXISTS. D44 added geometry validation and an int64 widening and both were
// verified by source review plus a live fingerprint that only ever supplies VALID
// geometry. Nothing would have failed if index_2d went back to 32-bit, if j_stride lost
// its cast, or if a bound check were deleted -- and in fact D44's own fix WAS partial:
// it corrected index_3d and left index_2d and j_stride with the identical defect two
// lines away. A contract that only exercises correct input cannot catch that.
//
// Pure integers: no libtorch, no solver, no allocation. That is what makes the
// INT_MAX-adjacent cases below cheap enough to assert on rather than reason about.

#include "../wrf_sdirk3_base_state_layout.h"

#include <climits>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

bool rejects(int its, int ite, int jts, int jte, int kts, int kte,
             int ims, int ime, int jms, int jme, int kms, int kme) {
    try {
        (void)wrf::sdirk3::make_base_state_layout(its, ite, jts, jte, kts, kte,
                                                  ims, ime, jms, jme, kms, kme);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    using wrf::sdirk3::make_base_state_layout;

    // --- a normal tile: strides and offsets are what the WRF layout implies ---
    // WRF is (i,k,j) with i fastest, so stride_k = ni and stride_j = ni*nk.
    {
        const auto L = make_base_state_layout(/*i*/ 3, 10, /*j*/ 2, 8, /*k*/ 1, 20,
                                              /*im*/ 1, 12, /*jm*/ 1, 10, /*km*/ 1, 24);
        check(L.nx == 8 && L.ny == 7 && L.nz == 20, "tile extents");
        check(L.stride_k == 12, "stride_k == ni");
        check(L.stride_j == 12 * 24, "stride_j == ni*nk");
        check(L.stride_j_2d == 12, "stride_j_2d == ni");
        check(L.offset_3d == (2 - 1) * 12 * 24 + (1 - 1) * 12 + (3 - 1), "offset_3d");
        check(L.offset_2d == (2 - 1) * 12 + (3 - 1), "offset_2d");
        check(L.count_3d == 12 * 24 * 10, "count_3d == ni*nk*nj");
        check(L.count_2d == 12 * 10, "count_2d == ni*nj");
    }

    // --- inverted intervals in each axis ---
    check(rejects(10, 3, 2, 8, 1, 20, 1, 12, 1, 10, 1, 24), "inverted i REJECTED");
    check(rejects(3, 10, 8, 2, 1, 20, 1, 12, 1, 10, 1, 24), "inverted j REJECTED");
    check(rejects(3, 10, 2, 8, 20, 1, 1, 12, 1, 10, 1, 24), "inverted k REJECTED");

    // --- tile outside the memory domain, low and high, in each axis ---
    check(rejects(0, 10, 2, 8, 1, 20, 1, 12, 1, 10, 1, 24), "i below ims REJECTED");
    check(rejects(3, 13, 2, 8, 1, 20, 1, 12, 1, 10, 1, 24), "i above ime REJECTED");
    check(rejects(3, 10, 0, 8, 1, 20, 1, 12, 1, 10, 1, 24), "j below jms REJECTED");
    check(rejects(3, 10, 2, 11, 1, 20, 1, 12, 1, 10, 1, 24), "j above jme REJECTED");
    check(rejects(3, 10, 2, 8, 0, 20, 1, 12, 1, 10, 1, 24), "k below kms REJECTED");
    check(rejects(3, 10, 2, 8, 1, 25, 1, 12, 1, 10, 1, 24), "k above kme REJECTED");

    // --- phb's w-level: kte+1 must be inside kme ---
    // kte == kme is legal for MASS levels and illegal here, which is why this needs its
    // own case rather than being folded into the k range check above.
    check(rejects(3, 10, 2, 8, 1, 24, 1, 12, 1, 10, 1, 24),
          "kte == kme REJECTED (phb w-level would escape)");
    {
        bool threw = false;
        try { (void)make_base_state_layout(3, 10, 2, 8, 1, 23, 1, 12, 1, 10, 1, 24); }
        catch (const std::invalid_argument&) { threw = true; }
        check(!threw, "kte == kme-1 ACCEPTED (w-level exactly fits)");
    }

    // --- INT_MAX-adjacent: the checks must not overflow while checking ---
    // The old code did `TORCH_CHECK(kte + 1 <= kme)` in int, so a kte at INT_MAX
    // overflowed the guard itself. Widening first is what makes this expressible.
    check(rejects(1, 10, 1, 10, 1, INT_MAX, 1, 12, 1, 10, 1, INT_MAX),
          "kte == INT_MAX == kme REJECTED without overflowing the check");
    check(rejects(INT_MAX - 1, INT_MAX, 1, 10, 1, 5, 1, 12, 1, 10, 1, 24),
          "i tile at INT_MAX outside memory REJECTED");

    // --- a 2-D product that overflows int32 but fits int64 ---
    // This is the case D44 would have got WRONG: index_2d and j_stride multiplied in
    // int and widened afterwards, so ni*nj here wraps negative in 32-bit. int64
    // arithmetic must produce the true value instead.
    // Caught, not left to propagate: reverting the widening makes count_2d wrap
    // NEGATIVE, and the escape check below then throws. Verified by doing exactly that
    // -- the fixture aborted with "escapes the memory domain (-1494967296 elements)".
    // An abort proves the regression is detected but is unreadable in a CI log, so the
    // throw is turned into a named assertion instead.
    {
        const int ni = 70000, nj = 40000;          // ni*nj = 2.8e9 > INT_MAX
        bool threw = false;
        long long got_2d = 0, got_stride_j = 0;
        try {
            const auto L = make_base_state_layout(1, 10, 1, 10, 1, 5,
                                                  1, ni, 1, nj, 1, 8);
            got_2d = L.count_2d;
            got_stride_j = L.stride_j;
        } catch (const std::invalid_argument&) { threw = true; }
        check(!threw, "valid large domain does NOT throw (32-bit wrap would)");
        check(got_2d == 1LL * ni * nj,
              "2-D count exceeding INT_MAX computed in int64 (was 32-bit overflow)");
        check(got_2d > 0, "2-D count is POSITIVE (32-bit would wrap negative)");
        check(got_stride_j == 1LL * ni * 8, "stride_j == ni*nk in int64");
    }

    // --- a 3-D product that overflows int32 ---
    {
        const int ni = 5000, nk = 500, nj = 5000;  // ni*nk*nj = 1.25e10
        const auto L = make_base_state_layout(1, 10, 1, 10, 1, 5,
                                              1, ni, 1, nj, 1, nk);
        check(L.count_3d == 1LL * ni * nk * nj,
              "3-D count exceeding INT_MAX computed in int64");
        check(L.count_3d > 0, "3-D count is POSITIVE");
    }

    // --- the tile view must fit inside the memory domain ---
    // A valid-looking offset can still address past the end once the extents apply;
    // without this, from_blob would view memory the caller never owned.
    {
        const auto L = make_base_state_layout(1, 12, 1, 10, 1, 5, 1, 12, 1, 10, 1, 8);
        const long long last = L.offset_3d + (L.ny - 1) * L.stride_j
                                           + (L.nz - 1) * L.stride_k + (L.nx - 1);
        check(last < L.count_3d, "full-extent tile view stays inside the memory domain");
    }

    constexpr int expected_checks = 28;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_STATE_LAYOUT: PASS" << std::endl; return 0; }
    std::cout << "BASE_STATE_LAYOUT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
