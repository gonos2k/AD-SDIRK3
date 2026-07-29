// wrf_sdirk3_base_state_layout.h -- base-state tile geometry, computed once, in 64-bit.
//
// 9F.D45 (review sections 2 and 3). Two defects motivated this, both of them mine:
//
// (1) PARTIAL SWEEP. D44 fixed index_3d's 32-bit multiply and left index_2d and
//     j_stride computing in `int` and only then widening to int64_t -- the same defect,
//     two lines away. Fixing one instance of a pattern and not grepping for its
//     siblings is a recurring failure in this campaign, so the arithmetic now lives in
//     ONE place that cannot be half-fixed.
//
// (2) THROWING ACROSS extern "C". D44's validator used TORCH_CHECK and sat before the
//     caller's try block, so invalid geometry propagated a c10::Error out through a C
//     ABI instead of returning the 0 the checked contract promises. This header throws
//     std::invalid_argument and the ABI wraps everything; see the caller.
//
// TORCH-FREE ON PURPOSE. It is pure integer geometry, so the contract test can exercise
// INT_MAX-adjacent inputs with no libtorch, no solver and no allocation.

#ifndef WRF_SDIRK3_BASE_STATE_LAYOUT_H
#define WRF_SDIRK3_BASE_STATE_LAYOUT_H

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

struct BaseStateLayout {
    std::int64_t nx = 0, ny = 0, nz = 0;   // tile extents
    std::int64_t stride_k = 0;             // 3-D: elements between k levels
    std::int64_t stride_j = 0;             // 3-D: elements between j rows
    std::int64_t stride_j_2d = 0;          // 2-D: elements between j rows
    std::int64_t offset_3d = 0;            // tile start within the 3-D memory domain
    std::int64_t offset_2d = 0;            // tile start within the 2-D memory domain
    std::int64_t count_3d = 0;             // total 3-D elements in the memory domain
    std::int64_t count_2d = 0;             // total 2-D elements in the memory domain
};

namespace layout_detail {

// Multiply with an explicit overflow test. int64 makes overflow unreachable for any
// real grid, but the check is what lets the contract test assert on INT_MAX-adjacent
// inputs rather than assuming they cannot occur.
inline std::int64_t mul_checked(std::int64_t a, std::int64_t b, const char* what) {
    if (a != 0 && (b > std::numeric_limits<std::int64_t>::max() / a ||
                   b < std::numeric_limits<std::int64_t>::min() / a)) {
        throw std::invalid_argument(std::string("base-state layout: ") + what +
                                    " overflows int64");
    }
    return a * b;
}

inline void require(bool ok, const std::string& msg) {
    if (!ok) throw std::invalid_argument("base-state layout: " + msg);
}

inline std::string range(const char* n, long long lo, long long hi) {
    return std::string(n) + "[" + std::to_string(lo) + "," + std::to_string(hi) + "]";
}

}  // namespace layout_detail

// Validates the geometry and computes every derived quantity. Throws
// std::invalid_argument naming the offending property; never returns a partial result.
//
// Arguments are the WRF tile and memory bounds, taken as int because that is what the
// Fortran ABI passes -- they are widened before ANY arithmetic, which is the point.
inline BaseStateLayout make_base_state_layout(
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme) {
    using namespace layout_detail;

    // Widen FIRST. Every expression below is int64; nothing is computed in int and
    // widened afterwards, which is exactly the bug this replaces.
    const std::int64_t i_ts = its, i_te = ite, i_ms = ims, i_me = ime;
    const std::int64_t j_ts = jts, j_te = jte, j_ms = jms, j_me = jme;
    const std::int64_t k_ts = kts, k_te = kte, k_ms = kms, k_me = kme;

    require(i_ms <= i_ts && i_ts <= i_te && i_te <= i_me,
            "i tile " + range("", i_ts, i_te) + " outside memory " + range("", i_ms, i_me));
    require(j_ms <= j_ts && j_ts <= j_te && j_te <= j_me,
            "j tile " + range("", j_ts, j_te) + " outside memory " + range("", j_ms, j_me));
    require(k_ms <= k_ts && k_ts <= k_te && k_te <= k_me,
            "k tile " + range("", k_ts, k_te) + " outside memory " + range("", k_ms, k_me));

    // phb lives on w-levels: one MORE than the mass levels this tile spans. Computed in
    // int64 so a kte at INT_MAX cannot overflow the check itself -- the old
    // `TORCH_CHECK(kte + 1 <= kme)` did that addition in int.
    require(k_te + 1 <= k_me,
            "phb w-level kte+1=" + std::to_string(k_te + 1) +
            " exceeds memory kme=" + std::to_string(k_me));

    BaseStateLayout L;
    L.nx = i_te - i_ts + 1;
    L.ny = j_te - j_ts + 1;
    L.nz = k_te - k_ts + 1;
    require(L.nx > 0 && L.ny > 0 && L.nz > 0, "tile extent is not positive");

    const std::int64_t ni = i_me - i_ms + 1;
    const std::int64_t nk = k_me - k_ms + 1;
    const std::int64_t nj = j_me - j_ms + 1;

    L.stride_k    = ni;
    L.stride_j    = mul_checked(ni, nk, "j stride (ni*nk)");
    L.stride_j_2d = ni;

    L.count_3d = mul_checked(L.stride_j, nj, "3-D element count (ni*nk*nj)");
    L.count_2d = mul_checked(ni, nj, "2-D element count (ni*nj)");

    L.offset_3d = mul_checked(j_ts - j_ms, L.stride_j, "3-D j offset") +
                  mul_checked(k_ts - k_ms, L.stride_k, "3-D k offset") +
                  (i_ts - i_ms);
    L.offset_2d = mul_checked(j_ts - j_ms, L.stride_j_2d, "2-D j offset") +
                  (i_ts - i_ms);

    // The tile must fit. Without this, a valid-looking offset can still address past
    // the end of the caller's buffer once the tile extents are applied -- from_blob
    // would then create a view over memory the caller never owned, silently.
    const std::int64_t last_3d = L.offset_3d +
        mul_checked(L.ny - 1, L.stride_j, "3-D extent j") +
        mul_checked(L.nz - 1, L.stride_k, "3-D extent k") + (L.nx - 1);
    require(L.offset_3d >= 0 && last_3d < L.count_3d,
            "3-D tile view [" + std::to_string(L.offset_3d) + "," +
            std::to_string(last_3d) + "] escapes the memory domain (" +
            std::to_string(L.count_3d) + " elements)");

    const std::int64_t last_2d = L.offset_2d +
        mul_checked(L.ny - 1, L.stride_j_2d, "2-D extent j") + (L.nx - 1);
    require(L.offset_2d >= 0 && last_2d < L.count_2d,
            "2-D tile view [" + std::to_string(L.offset_2d) + "," +
            std::to_string(last_2d) + "] escapes the memory domain (" +
            std::to_string(L.count_2d) + " elements)");

    return L;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_BASE_STATE_LAYOUT_H
