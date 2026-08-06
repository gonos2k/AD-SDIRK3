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

#include <torch/torch.h>

#include <algorithm>
#include <cstdint>
#include <functional>
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

    // ------- 7. THE CHECKED mu EXTRACTOR: fail-CLOSED, because P depends on the mass state.
    // D94 shipped the fail-OPEN form -- shape checks that silently skipped the binding and
    // let the adjoint run against a preconditioner bound to the wrong state.
    {
        auto opts = torch::TensorOptions().dtype(torch::kFloat32);
        auto state = torch::arange(static_cast<double>(L.total_size), opts);

        auto mu = wrf::sdirk3::extract_mu_pert_2d(L, state, NY, NX);
        check(mu.dim() == 2 && mu.size(0) == NY && mu.size(1) == NX,
              "extract: returns a 2-D {ny, nx} tensor");
        const auto& mu_blk = L.blocks.back();
        check(mu.flatten()[0].item<double>() == static_cast<double>(mu_blk.start),
              "extract: takes the mu block, not some other slice");
        // A clone, so binding cannot alias the caller's checkpoint.
        state.index_put_({mu_blk.start}, -12345.0f);
        check(mu.flatten()[0].item<double>() != -12345.0,
              "extract: returns a CLONE -- binding cannot alias the checkpoint");

        auto throws = [&](const std::function<void()>& f) {
            try { f(); return false; } catch (const std::exception&) { return true; }
        };
        check(throws([&]{ wrf::sdirk3::extract_mu_pert_2d(L, state, NY + 1, NX); }),
              "extract: THROWS on ny mismatch (fail-closed, not silently skipped)");
        check(throws([&]{ auto short_state = torch::zeros({L.total_size - 1}, opts);
                          wrf::sdirk3::extract_mu_pert_2d(L, short_state, NY, NX); }),
              "extract: THROWS when the state size disagrees with the layout");
        check(throws([&]{ auto bad = L; bad.blocks.back().name = "not_mu";
                          wrf::sdirk3::extract_mu_pert_2d(bad, state, NY, NX); }),
              "extract: THROWS when the last block is not mu");
        check(throws([&]{ auto twod = torch::zeros({NY, NX}, opts);
                          wrf::sdirk3::extract_mu_pert_2d(L, twod, NY, NX); }),
              "extract: THROWS on a non-1-D packed state");
    }

    // ---- 8. MUTATION TESTS: is_valid() must actually REJECT, not merely accept (D96).
    // PRODUCTION gates on is_valid() -- extract_mu_pert_2d calls it first -- while the order
    // and contiguity assertions above live only here. Before D96 is_valid() summed sizes and
    // nothing else, so every mutation below except the last one PASSED it. A validator nobody
    // has watched say "no" is an unproven validator.
    {
        auto mutate = [&](const std::function<void(wrf::sdirk3::StateLayout&)>& f) {
            auto m = L; f(m); return m.is_valid();
        };

        check(!mutate([](auto& m){ std::swap(m.blocks[2], m.blocks[3]); }),
              "REJECTS rw/ph swapped -- the case a sum check is blind to");
        check(!mutate([](auto& m){ m.blocks[4].name = "theta"; }),
              "REJECTS a renamed block (names are part of the contract)");
        check(!mutate([&](auto& m){ m.blocks.pop_back(); m.total_size -= L.blocks[5].size; }),
              "REJECTS a layout with five blocks even though the total still adds up");
        check(!mutate([](auto& m){ m.blocks[3].start += 1; m.blocks[4].start += 1; }),
              "REJECTS a gap between blocks");
        check(!mutate([](auto& m){ m.blocks[1].start -= 1; }),
              "REJECTS overlapping blocks");
        check(!mutate([&](auto& m){ m.blocks[5].size = -m.blocks[5].size;
                                    m.total_size -= 2 * L.blocks[5].size; }),
              "REJECTS a negative block size even if the sum is consistent");
        check(!mutate([](auto& m){ m.total_size += 1; }),
              "REJECTS a total that disagrees with the blocks");
        check(mutate([](auto&){}), "ACCEPTS the unmutated layout (not vacuously strict)");

        // A default-constructed layout must be invalid, because the fail-closed diagnostic
        // path in newton_solver.cpp relies on exactly that.
        wrf::sdirk3::StateLayout empty;
        check(!empty.is_valid(), "REJECTS a default-constructed (empty) layout");
        check(empty.total_size == 0 && !empty.is_exact,
              "default-constructed layout is well-defined, not indeterminate");
    }

    // ---- 9. from_grid_dims refuses bad input rather than producing a plausible layout.
    {
        auto throws = [&](const std::function<void()>& f) {
            try { f(); return false; } catch (const std::exception&) { return true; }
        };
        check(throws([]{ wrf::sdirk3::StateLayout::from_grid_dims(0, NY, NZ, NXU, NYV, NZW); }),
              "from_grid_dims THROWS on a zero dimension");
        check(throws([]{ wrf::sdirk3::StateLayout::from_grid_dims(NX, -1, NZ, NXU, NYV, NZW); }),
              "from_grid_dims THROWS on a negative dimension");
        // int64 overflow must be refused, not wrapped into a plausible-looking layout.
        // 2e6 does NOT overflow -- 2e6^3 = 8.0e18 < int64max 9.22e18 -- and the first version
        // of this check used it, asserting an overflow that never happened. Pointing the
        // assertion the right way is what caught it. 3e6^3 = 2.7e19 genuinely wraps.
        const int big = 3000000;
        check(throws([&]{ wrf::sdirk3::StateLayout::from_grid_dims(
                  big, big, big, big, big, big); }),
              "from_grid_dims THROWS on int64 MULTIPLICATION overflow");

        // 9F.D98 (review section 11): the SUM can overflow while every individual block is
        // representable -- checked_mul alone was half the job, and the D96 fixture above only
        // exercised a single multiplication, so this case was untested as well as unchecked.
        // 1.6e6^3 = 4.096e18 fits; five such blocks total 2.05e19 and do not.
        const int sum_ovf = 1600000;
        check(throws([&]{ wrf::sdirk3::StateLayout::from_grid_dims(
                  sum_ovf, sum_ovf, sum_ovf, sum_ovf, sum_ovf, sum_ovf); }),
              "from_grid_dims THROWS when the SUM overflows though each block fits");
    }

    // 9F.D104 (review section 8.1): is_exact is part of the extraction contract.
    {
        auto opts_e = torch::TensorOptions().dtype(torch::kFloat32);
        auto state = torch::zeros({L.total_size}, opts_e);
        auto not_exact = L;
        not_exact.is_exact = false;          // structurally identical, provenance unknown
        check(not_exact.is_valid(), "a non-exact layout can still be structurally VALID");
        bool threw = false;
        try { wrf::sdirk3::extract_mu_pert_2d(not_exact, state, NY, NX); }
        catch (const std::exception&) { threw = true; }
        check(threw, "extract REFUSES a structurally-valid but non-grid-derived layout");
    }

    // ---- 10. the momentum BASIS is declared, and it disagrees with the block names.
    // Registry.EM_COMMON declares u as "x-wind component" [m s-1] and ru as "mu-coupled u"
    // [Pa m s-1]; module_implicit_sdirk3.F passes grid%u_2. So the packed state is VELOCITY
    // while these blocks are named ru/rv/rw. Pinned here because the basis decides the units of
    // every coupling coefficient -- dF_mu/du ~ mu*H for velocity, ~H for coupled momentum -- and
    // a reader who trusts the name derives the wrong preconditioner.
    {
        check(L.momentum_basis == wrf::sdirk3::MomentumBasis::Velocity,
              "the packed state's momentum basis is VELOCITY (Registry), not coupled momentum");
        check(L.blocks[0].name == "ru",
              "the first block is still NAMED ru -- the name contradicts the basis, on purpose");

        // A declared basis that nothing rejects is decoration. These assert the refusal.
        auto coupled = L;
        coupled.momentum_basis = wrf::sdirk3::MomentumBasis::CoupledMomentum;
        bool threw_req = false;
        try { wrf::sdirk3::require_velocity_basis(coupled, "test"); }
        catch (const std::exception&) { threw_req = true; }
        check(threw_req, "require_velocity_basis REJECTS a CoupledMomentum layout");

        // The narrow helper only guards callers who remember it. is_valid() is the gate every
        // consumer already passes, so the invariant lives there and this asserts it does.
        check(!coupled.is_valid(),
              "is_valid() REJECTS a CoupledMomentum layout -- all 16 existing gates enforce it");

        auto opts_b = torch::TensorOptions().dtype(torch::kFloat32);
        auto st_b = torch::zeros({L.total_size}, opts_b);
        bool threw_ex = false;
        try { wrf::sdirk3::extract_mu_pert_2d(coupled, st_b, NY, NX); }
        catch (const std::exception&) { threw_ex = true; }
        check(threw_ex, "extract REFUSES a CoupledMomentum layout, so the field is load-bearing");

        check(wrf::sdirk3::StateLayout::from_grid_dims(NX, NY, NZ, NXU, NYV, NZW).momentum_basis
                  == wrf::sdirk3::MomentumBasis::Velocity,
              "from_grid_dims STATES the basis rather than inheriting the member default");
    }

    constexpr int expected_checks = 46;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "STATE_LAYOUT: PASS" << std::endl; return 0; }
    std::cout << "STATE_LAYOUT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
