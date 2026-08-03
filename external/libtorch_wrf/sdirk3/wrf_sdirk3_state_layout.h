// wrf_sdirk3_state_layout.h -- THE packed-state layout. One authority, shared by every path.
//
// 9F.D93 (review section 8.3). The six block sizes and their offsets were computed inline in
// at least five places -- the Newton solver, the preconditioner, packState, the RHS, and (my
// own addition, D89) the transpose block probe. Every copy is an independent chance to get
// the ORDER or an OFFSET wrong, and the guard I shipped with the fifth copy only checked
// that the sizes SUM to numel. As the review points out, a correct total is not evidence of
// a correct ordering: swap two equal-sized blocks and the sum is unchanged.
//
// This was already the de-facto authority (StateLayout::from_grid_dims) but it lived in
// wrf_sdirk3_newton_solver.cpp with only a forward declaration in the header, so it was an
// INCOMPLETE TYPE everywhere else -- which is precisely why the other copies exist. Moving
// the definition here is what makes the duplication removable, and it is the prerequisite
// for both stage-state binding and block/energy scaling.
//
// Layout, in packed order:
//     ru  ny * nz   * nx_u     u-staggered
//     rv  ny_v * nz * nx       v-staggered
//     rw  ny * nz_w * nx       w-staggered
//     ph  ny * nz_w * nx       geopotential, on the w-stagger
//     t   ny * nz   * nx       potential temperature, mass points
//     mu  ny * nx              column mass, 2-D

#ifndef WRF_SDIRK3_STATE_LAYOUT_H
#define WRF_SDIRK3_STATE_LAYOUT_H

#include <torch/torch.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {

namespace layout_detail {
// 9F.D96 (review section 10): multiplication that REFUSES to overflow rather than wrapping.
// The 3-D blocks are products of three dimensions; on a large domain those exceed int32, and
// a silent wrap would produce a layout whose blocks are plausible and wrong.
inline int64_t checked_mul(int64_t a, int64_t b, const char* what) {
    TORCH_CHECK(a >= 0 && b >= 0, "StateLayout: negative dimension in ", what);
    TORCH_CHECK(b == 0 || a <= (std::numeric_limits<int64_t>::max)() / b,
                "StateLayout: int64 overflow computing ", what);
    return a * b;
}
}  // namespace layout_detail

struct StateLayout {
    struct Block {
        std::string name;
        int64_t start;
        int64_t size;
    };
    std::vector<Block> blocks;
    // 9F.D96: default-initialised, so a default-constructed StateLayout is EMPTY and
    // is_valid() == false rather than reading an indeterminate total_size. The fail-closed
    // diagnostic path in newton_solver.cpp depends on exactly that.
    int64_t total_size = 0;
    bool is_exact = false;   // true only when computed from grid dims

    // Compute exact layout from grid dimensions
    // Layout: ru (ny*nz*nx_u), rv (ny_v*nz*nx), rw (ny*nz_w*nx),
    //         ph (ny*nz_w*nx), t (ny*nz*nx), mu (ny*nx)
    // All arithmetic in int64_t to prevent overflow on large domains.
    static StateLayout from_grid_dims(int nx, int ny, int nz,
                                      int nx_u, int ny_v, int nz_w) {
        StateLayout layout;
        layout.is_exact = true;

        int64_t nx64 = nx, ny64 = ny, nz64 = nz;
        int64_t nx_u64 = nx_u, ny_v64 = ny_v, nz_w64 = nz_w;

        TORCH_CHECK(nx > 0 && ny > 0 && nz > 0 && nx_u > 0 && ny_v > 0 && nz_w > 0,
                    "StateLayout::from_grid_dims: all grid dimensions must be positive");

        using layout_detail::checked_mul;
        int64_t size_u  = checked_mul(checked_mul(ny64, nz64, "ru"), nx_u64, "ru");
        int64_t size_v  = checked_mul(checked_mul(ny_v64, nz64, "rv"), nx64, "rv");
        int64_t size_w  = checked_mul(checked_mul(ny64, nz_w64, "rw"), nx64, "rw");
        int64_t size_ph = checked_mul(checked_mul(ny64, nz_w64, "ph"), nx64, "ph");
        int64_t size_t  = checked_mul(checked_mul(ny64, nz64, "t"), nx64, "t");
        int64_t size_mu = checked_mul(ny64, nx64, "mu");

        layout.total_size = size_u + size_v + size_w + size_ph + size_t + size_mu;

        layout.blocks = {
            {"ru", 0, size_u},
            {"rv", size_u, size_v},
            {"rw", size_u + size_v, size_w},
            {"ph", size_u + size_v + size_w, size_ph},
            {"t",  size_u + size_v + size_w + size_ph, size_t},
            {"mu", size_u + size_v + size_w + size_ph + size_t, size_mu}
        };

        return layout;
    }

    // 9F.D96 (review section 10): validate the layout, not just its TOTAL.
    //
    // The old body summed block sizes and compared to total_size. That is the same blind spot
    // the sum-guard had: rw and ph are the same size on this grid, so a swapped layout passes.
    // It mattered more than a weak test, because PRODUCTION calls this -- extract_mu_pert_2d
    // gates on it -- while the order/contiguity assertions lived only in the CTest. A property
    // asserted in CI and unchecked at runtime is not enforced where it counts.
    //
    // Every clause below has a mutation case in State_Layout_Contract that this must REJECT;
    // a validator nobody has watched say "no" is an unproven validator.
    bool is_valid() const {
        static const char* kExpected[6] = {"ru", "rv", "rw", "ph", "t", "mu"};
        if (blocks.size() != 6) return false;
        if (total_size < 0) return false;

        int64_t expected_start = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& b = blocks[i];
            if (b.name != kExpected[i]) return false;      // order AND naming
            if (b.size < 0 || b.start < 0) return false;
            if (b.start != expected_start) return false;   // contiguous: no gaps, no overlaps
            if (expected_start > (std::numeric_limits<int64_t>::max)() - b.size) return false;
            expected_start += b.size;
        }
        return expected_start == total_size;
    }
};

// 9F.D95 (review sections 4 and 6): extract the mu block of a packed state as a 2-D
// {ny, nx} tensor, or THROW.
//
// Fail-closed on purpose. D94 measured that the preconditioner genuinely depends on the mass
// state, so a layout that does not match is NOT a reason to skip the binding and continue --
// it means the adjoint would run against a preconditioner bound to the wrong state, and a
// wrong gradient is worse than no gradient. D94's version silently did nothing when its
// shape checks failed, which is the fail-OPEN form of the same code.
inline torch::Tensor extract_mu_pert_2d(const StateLayout& layout,
                                        const torch::Tensor& packed_state,
                                        int64_t ny, int64_t nx) {
    TORCH_CHECK(layout.is_valid(),
                "extract_mu_pert_2d: packed-state layout is invalid");
    TORCH_CHECK(packed_state.dim() == 1,
                "extract_mu_pert_2d: expected a 1-D packed state, got dim ",
                packed_state.dim());
    TORCH_CHECK(layout.total_size == packed_state.numel(),
                "extract_mu_pert_2d: layout total ", layout.total_size,
                " != state numel ", packed_state.numel());
    TORCH_CHECK(!layout.blocks.empty(), "extract_mu_pert_2d: layout has no blocks");
    const auto& mu = layout.blocks.back();
    TORCH_CHECK(mu.name == "mu",
                "extract_mu_pert_2d: last block is '", mu.name, "', expected 'mu'");
    TORCH_CHECK(ny > 0 && nx > 0,
                "extract_mu_pert_2d: non-positive grid dims ny=", ny, " nx=", nx);
    TORCH_CHECK(mu.size == ny * nx,
                "extract_mu_pert_2d: mu block size ", mu.size, " != ny*nx = ", ny * nx);
    return packed_state.slice(0, mu.start, mu.start + mu.size).reshape({ny, nx}).clone();
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_STATE_LAYOUT_H
