// 9F.D93 (review section 8.3): the packed-state layout is ONE authority, and its ORDER is
// part of the contract.
//
// The review's point, and the defect in my own D89 code: a guard that only checks
//
//     n_ru + n_rv + n_rw + n_ph + n_t + n_mu == n_total
//
// is not evidence that the ordering or the offsets are right. On this grid rw and ph are
// BOTH ny*nz_w*nx, so swapping them leaves the sum -- and any sum-based guard -- perfectly
// happy while every consumer reads the wrong field. That is the failure mode this file
// exists to make impossible, so ORDER_IS_LOAD_BEARING below asserts the sequence itself.

#include "../wrf_sdirk3_state_layout.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// The em_b_wave geometry the campaign measures against.
constexpr int NX = 41, NY = 81, NZ = 64;
constexpr int NXU = 42, NYV = 82, NZW = 65;

}  // namespace

int main() {
    std::cout << "=== State_Layout_Contract ===" << std::endl;

    const auto L = wrf::sdirk3::StateLayout::from_grid_dims(NX, NY, NZ, NXU, NYV, NZW);

    // ---------------------------------------------------------------- 1. shape and totals
    check(L.is_exact, "from_grid_dims is marked exact");
    check(L.is_valid(), "is_valid(): block sizes sum to total_size");
    check(L.blocks.size() == 6, "six blocks");

    // The live measurement this campaign runs against reports numel = 1080491. If the layout
    // ever stops agreeing with the model's packed vector, that is the number that moves.
    check(L.total_size == 1080491,
          "total_size matches the measured em_b_wave state (" +
              std::to_string(L.total_size) + ")");

    // ------------------------------------------- 2. ORDER IS LOAD-BEARING (the review's point)
    {
        const std::vector<std::string> expected = {"ru", "rv", "rw", "ph", "t", "mu"};
        bool order_ok = true;
        for (size_t i = 0; i < expected.size() && i < L.blocks.size(); ++i) {
            if (L.blocks[i].name != expected[i]) order_ok = false;
        }
        check(order_ok, "packed order is exactly ru, rv, rw, ph, t, mu");

        // rw and ph are the SAME SIZE on this grid, which is what makes a sum-only guard
        // blind: transposing them is undetectable by any total.
        check(L.blocks[2].size == L.blocks[3].size,
              "rw and ph are the same size here -- so a sum check CANNOT see them swapped");
    }

    // ------------------------------------------------- 3. offsets are contiguous and exact
    {
        int64_t running = 0;
        bool contiguous = true;
        for (const auto& b : L.blocks) {
            if (b.start != running) contiguous = false;
            running += b.size;
        }
        check(contiguous, "block starts are contiguous with no gaps or overlaps");
        check(running == L.total_size, "the final offset lands exactly on total_size");
    }

    // ------------------------------------------------------ 4. each size is the right formula
    {
        const int64_t nx = NX, ny = NY, nz = NZ, nxu = NXU, nyv = NYV, nzw = NZW;
        check(L.blocks[0].size == ny * nz * nxu,  "ru = ny*nz*nx_u   (u-staggered)");
        check(L.blocks[1].size == nyv * nz * nx,  "rv = ny_v*nz*nx   (v-staggered)");
        check(L.blocks[2].size == ny * nzw * nx,  "rw = ny*nz_w*nx   (w-staggered)");
        check(L.blocks[3].size == ny * nzw * nx,  "ph = ny*nz_w*nx   (on the w-stagger)");
        check(L.blocks[4].size == ny * nz * nx,   "t  = ny*nz*nx     (mass points)");
        check(L.blocks[5].size == ny * nx,        "mu = ny*nx        (2-D)");
    }

    // --------------------------------------------- 5. staggering is not silently symmetric
    // If nx_u == nx (or ny_v == ny, nz_w == nz) the staggered blocks collapse onto the mass
    // sizes and several distinctness checks above become vacuous. Assert the fixture really
    // exercises staggering.
    check(NXU > NX && NYV > NY && NZW > NZ,
          "fixture actually staggers (nx_u>nx, ny_v>ny, nz_w>nz) -- checks are not vacuous");

    // ------------------------------------------------------------- 6. int64 throughout
    // A large domain must not overflow. 2000^3-ish grids exceed int32 in the 3-D blocks.
    {
        const auto big = wrf::sdirk3::StateLayout::from_grid_dims(
            2000, 2000, 200, 2001, 2001, 201);
        check(big.is_valid(), "large grid: still valid");
        check(big.total_size > 2147483647LL,
              "large grid exceeds int32 (" + std::to_string(big.total_size) +
                  ") without overflow");
    }

    constexpr int expected_checks = 17;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "STATE_LAYOUT: PASS" << std::endl; return 0; }
    std::cout << "STATE_LAYOUT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
