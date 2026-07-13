// wrf_sdirk3_msf_raw_stats.h
//
// Raw map-factor verification for the split-explicit supported-config guard
// (external review rounds 3/3b/3c). Deliberately torch-free so the regression
// matrix (test_msf_raw_stats.cpp) compiles and runs standalone in seconds.
//
// CONTRACT (per array — never accumulate across arrays; a joint accumulator
// silently weakens the per-array ∀ into an anywhere ∃, review round 3b):
//   A raw map-factor array is "genuine unit-map" iff
//     (1) it has at least one nonzero entry           (all-zero = never wired),
//     (2) every entry is finite                       (NaN/Inf never benign),
//     (3) every NONZERO entry is ~1.0                 (deviation = real projection).
//   Finite zeros are benign staggered/halo padding (e.g. em_b_wave) and are
//   ignored by (3) but do not satisfy (1).
//
// LAYOUT (review round 3c): the traversal must cover exactly the Fortran
// array's tile region. WRF allocates EVERY field, staggered or not, with the
// SAME memory bounds (ims:ime, jms:jme); staggered fields just use one more
// index WITHIN those bounds. So for all six map factors the element (i,j)
// lives at (j - jms) * mem_nx + (i - ims) from the (ims,jms) base pointer —
// row stride mem_nx, never mem_nx + 1, and tile offset its-ims / jts-jms,
// never its-(ims-1). Callers pass base = tile origin, row_stride = mem_nx.

#ifndef WRF_SDIRK3_MSF_RAW_STATS_H
#define WRF_SDIRK3_MSF_RAW_STATS_H

#include <cmath>
#include <cstdint>

namespace wrf {
namespace sdirk3 {

struct MsfRawStats {
    bool any_nonzero = false;
    bool any_nonfinite = false;
    float max_nonzero_dev = 0.0f;  // max |v - 1| over FINITE NONZERO entries

    // The genuine-unit predicate. tol is the unit tolerance (guard uses 1e-6f).
    bool genuine_unit(float tol) const {
        return any_nonzero && !any_nonfinite && max_nonzero_dev < tol;
    }
};

// Scan one raw map-factor array over its tile region.
//   base       : pointer to the tile origin element (row-major from Fortran
//                (ims,jms) base: config_ptr + (jts-jms)*row_stride + (its-ims))
//   ny, nx     : logical tile extents to verify (nx = nx_u for U-staggered)
//   row_stride : Fortran row stride = mem_nx (ime-ims+1) — NOT mem_nx+1
inline MsfRawStats msf_raw_stats(const float* base, int ny, int nx,
                                 std::int64_t row_stride) {
    MsfRawStats s;
    if (base == nullptr || ny <= 0 || nx <= 0 || row_stride < nx) {
        // Unwired / nonsensical geometry: report "nothing nonzero" so the
        // genuine-unit predicate fails closed.
        return s;
    }
    for (int j = 0; j < ny; ++j) {
        const float* row = base + static_cast<std::int64_t>(j) * row_stride;
        for (int i = 0; i < nx; ++i) {
            const float v = row[i];
            if (!std::isfinite(v)) {
                s.any_nonfinite = true;
                continue;
            }
            if (v != 0.0f) {
                s.any_nonzero = true;
                const float dev = std::fabs(v - 1.0f);
                if (dev > s.max_nonzero_dev) s.max_nonzero_dev = dev;
            }
        }
    }
    return s;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_MSF_RAW_STATS_H
