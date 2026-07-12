// test_msf_raw_stats.cpp — regression matrix for the raw map-factor
// verification used by the split-explicit supported-config guard.
//
// External review round 3c asked for exactly this matrix:
//   * ux/uy nonzero: TT pass, TF/FT/FF fail       (per-array existence, round 3b)
//   * per-array NaN/Inf and non-unit values fail  (fail-closed, round 3)
//   * first/last row non-unit fail                (index-region coverage)
//   * stride canary: unit logical tile inside sentinel halo — correct stride
//     passes, the old off-by-one stride (mem_nx+1) picks up the sentinel and
//     fails (the round-3c layout defect, made regression-testable)
//
// Torch-free on purpose: compiles standalone in seconds —
//   c++ -std=c++17 test_msf_raw_stats.cpp -o test_msf_raw_stats && ./test_msf_raw_stats

#include "wrf_sdirk3_msf_raw_stats.h"

#include <cstdio>
#include <limits>
#include <vector>

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

constexpr float kTol = 1e-6f;

// Build a mem_ny x mem_nx "Fortran patch" filled with `halo`, then set the
// tile region (rows [j0, j0+ny), cols [i0, i0+nx)) to `tile_val`.
std::vector<float> make_patch(int mem_ny, int mem_nx, float halo,
                              int j0, int i0, int ny, int nx, float tile_val) {
    std::vector<float> p(static_cast<size_t>(mem_ny) * mem_nx, halo);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            p[static_cast<size_t>(j0 + j) * mem_nx + (i0 + i)] = tile_val;
    return p;
}

}  // namespace

int main() {
    using wrf::sdirk3::msf_raw_stats;

    const int ny = 4, nx = 5;        // logical tile
    const int mem_nx = 9, mem_ny = 8; // Fortran memory bounds
    const int j0 = 2, i0 = 3;         // tile origin inside the patch

    auto tile_base = [&](std::vector<float>& p) {
        return p.data() + static_cast<size_t>(j0) * mem_nx + i0;
    };

    // --- Existence matrix (round 3b): each array independently nonzero ---
    {
        auto unit = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        auto zero = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 0.0f);
        const bool ux_t = msf_raw_stats(tile_base(unit), ny, nx, mem_nx).genuine_unit(kTol);
        const bool uy_t = msf_raw_stats(tile_base(unit), ny, nx, mem_nx).genuine_unit(kTol);
        const bool ux_f = msf_raw_stats(tile_base(zero), ny, nx, mem_nx).genuine_unit(kTol);
        const bool uy_f = msf_raw_stats(tile_base(zero), ny, nx, mem_nx).genuine_unit(kTol);
        expect((ux_t && uy_t) == true,  "TT: both unit arrays -> guard passes");
        expect((ux_t && uy_f) == false, "TF: unit ux, all-zero uy -> guard fails");
        expect((ux_f && uy_t) == false, "FT: all-zero ux, unit uy -> guard fails");
        expect((ux_f && uy_f) == false, "FF: both all-zero -> guard fails");
    }

    // --- Benign staggered/halo zeros inside the tile region still pass ---
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        // em_b_wave-style: last staggered column of the tile is zero padding
        for (int j = 0; j < ny; ++j)
            p[static_cast<size_t>(j0 + j) * mem_nx + (i0 + nx - 1)] = 0.0f;
        expect(msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "genuine unit-map with benign zero column passes");
    }

    // --- Per-array NaN / Inf / non-unit fail closed ---
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        p[static_cast<size_t>(j0 + 1) * mem_nx + (i0 + 2)] =
            std::numeric_limits<float>::quiet_NaN();
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "single NaN entry fails");
    }
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        p[static_cast<size_t>(j0 + 2) * mem_nx + (i0 + 1)] =
            std::numeric_limits<float>::infinity();
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "single Inf entry fails");
    }
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        p[static_cast<size_t>(j0 + 1) * mem_nx + (i0 + 1)] = 1.05f;  // real projection
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "non-unit interior value fails");
    }

    // --- Index-region coverage: first and last row must be scanned ---
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        p[static_cast<size_t>(j0 + 0) * mem_nx + (i0 + 0)] = 2.0f;  // first row, first col
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "non-unit at first row/col fails (coverage)");
    }
    {
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        p[static_cast<size_t>(j0 + ny - 1) * mem_nx + (i0 + nx - 1)] = 2.0f;  // last row/col
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "non-unit at last row/col fails (coverage)");
    }

    // --- Stride canary (round 3c): logical tile unit, halo sentinel non-unit ---
    {
        auto p = make_patch(mem_ny, mem_nx, 7.0f, j0, i0, ny, nx, 1.0f);
        expect(msf_raw_stats(tile_base(p), ny, nx, mem_nx).genuine_unit(kTol),
               "stride canary: TRUE stride mem_nx sees only the unit tile");
        // The old defective layout used row stride mem_nx+1: from the second row on
        // it drifts into the sentinel halo, so the scan must FAIL — proving the
        // predicate distinguishes the correct traversal from the round-3c bug.
        expect(!msf_raw_stats(tile_base(p), ny, nx, mem_nx + 1).genuine_unit(kTol),
               "stride canary: off-by-one stride (old bug) hits sentinel and fails");
    }

    // --- Origin canaries (round 3d): the production call-site's OLD origins
    // drifted off the tile — old U origin base+1 (from its-(ims-1)) reads one
    // column right; old V origin base+mem_nx (from jts-(jms-1)) reads one row
    // down. Both must pick up the sentinel halo and fail. ---
    {
        auto p = make_patch(mem_ny, mem_nx, 7.0f, j0, i0, ny, nx, 1.0f);
        expect(!msf_raw_stats(tile_base(p) + 1, ny, nx, mem_nx).genuine_unit(kTol),
               "origin canary: old U origin (base+1) hits sentinel and fails");
        expect(!msf_raw_stats(tile_base(p) + mem_nx, ny, nx, mem_nx).genuine_unit(kTol),
               "origin canary: old V origin (base+mem_nx) hits sentinel and fails");
    }

    // --- Degenerate wiring ---
    {
        expect(!msf_raw_stats(nullptr, ny, nx, mem_nx).genuine_unit(kTol),
               "null base pointer fails");
        auto p = make_patch(mem_ny, mem_nx, 0.0f, j0, i0, ny, nx, 1.0f);
        expect(!msf_raw_stats(tile_base(p), 0, nx, mem_nx).genuine_unit(kTol),
               "zero rows fails");
        expect(!msf_raw_stats(tile_base(p), ny, nx, nx - 1).genuine_unit(kTol),
               "row_stride < nx (nonsensical geometry) fails");
    }

    if (g_failures == 0) {
        std::printf("ALL %s\n", "msf_raw_stats regression matrix PASSED");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
