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
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {

struct StateLayout {
    struct Block {
        std::string name;
        int64_t start;
        int64_t size;
    };
    std::vector<Block> blocks;
    int64_t total_size;
    bool is_exact;  // True if computed from grid dims, false if heuristic

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

        int64_t size_u  = ny64 * nz64 * nx_u64;
        int64_t size_v  = ny_v64 * nz64 * nx64;
        int64_t size_w  = ny64 * nz_w64 * nx64;
        int64_t size_ph = ny64 * nz_w64 * nx64;  // ph on w-stagger
        int64_t size_t  = ny64 * nz64 * nx64;    // t on mass points
        int64_t size_mu = ny64 * nx64;            // mu is 2D

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

    // DEAD CODE: Never called — layout always from from_grid_dims(). Retained for reference.
    [[deprecated("Use from_grid_dims() instead")]]
    static StateLayout infer_from_size(int64_t state_size) {
        StateLayout layout;
        layout.total_size = state_size;
        layout.is_exact = false;  // Heuristic, not exact

        // Heuristic proportions for typical WRF grids
        // WARNING: Will be wrong for nested grids, varying nz, different moisture slots
        int64_t size_mu = (state_size * 3) / 1000;  // ~0.3%
        int64_t size_3d_total = state_size - size_mu;

        // Distribute 3D fields with stagger-aware proportions
        int64_t size_u  = (size_3d_total * 202) / 1000;  // ~20.2%
        int64_t size_v  = (size_3d_total * 199) / 1000;  // ~19.9%
        int64_t size_w  = (size_3d_total * 200) / 1000;  // ~20.0%
        int64_t size_ph = (size_3d_total * 200) / 1000;  // ~20.0%
        int64_t size_t  = size_3d_total - (size_u + size_v + size_w + size_ph);  // ~19.7%

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

    // Validate layout matches actual state size
    bool is_valid() const {
        int64_t computed = 0;
        for (const auto& b : blocks) {
            computed += b.size;
        }
        return computed == total_size;
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
