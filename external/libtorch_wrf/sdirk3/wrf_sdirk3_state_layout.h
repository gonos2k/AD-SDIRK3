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
// 9F.D98 (review section 11): addition that refuses to overflow. checked_mul alone was half
// the job -- each block can be individually representable while the SUM is not. Five 3-D
// blocks of 4e18 each are fine one at a time and wrap when added, and the D96 overflow
// fixture only exercised a single multiplication, so that case was untested as well as
// unchecked.
inline int64_t checked_add(int64_t a, int64_t b, const char* what) {
    TORCH_CHECK(a >= 0 && b >= 0, "StateLayout: negative size in ", what);
    TORCH_CHECK(a <= (std::numeric_limits<int64_t>::max)() - b,
                "StateLayout: int64 overflow accumulating ", what);
    return a + b;
}
}  // namespace layout_detail

// What the first three packed blocks actually hold.
//
// SETTLED BY THE REGISTRY, not by the names in this file:
//     Registry.EM_COMMON:158  u   "x-wind component"  "m s-1"       <- VELOCITY
//     Registry.EM_COMMON:160  ru  "mu-coupled u"      "Pa m s-1"    <- coupled momentum
// and module_implicit_sdirk3.F passes grid%u_2, i.e. the velocity. The packed STATE is therefore
// velocity, and the block names "ru/rv/rw" below are a legacy misnomer for it.
//
// This is not cosmetic. The basis sets the units of every coupling coefficient: for a velocity
// basis dF_mu/du ~ mu*H, for a coupled-momentum basis dF_mu/dU ~ H. Reading one as the other
// changes the preconditioner formula, so the basis must be stated rather than inferred from a
// name. Renaming the blocks is a wide change and has not been done; this records which one is
// true so nobody derives coefficients from the label.
enum class MomentumBasis { Velocity, CoupledMomentum };

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
    bool is_exact = false;

    // Velocity, per the Registry declarations above. The block names say otherwise.
    MomentumBasis momentum_basis = MomentumBasis::Velocity;   // true only when computed from grid dims

    // Compute exact layout from grid dimensions
    // Layout: ru (ny*nz*nx_u), rv (ny_v*nz*nx), rw (ny*nz_w*nx),
    //         ph (ny*nz_w*nx), t (ny*nz*nx), mu (ny*nx)
    // All arithmetic in int64_t to prevent overflow on large domains.
    static StateLayout from_grid_dims(int nx, int ny, int nz,
                                      int nx_u, int ny_v, int nz_w) {
        StateLayout layout;
        layout.is_exact = true;
        // Stated, not inherited from the member default: a layout's basis is a property of how
        // it was built, and every consumer's coefficient units follow from it.
        layout.momentum_basis = MomentumBasis::Velocity;

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

        using layout_detail::checked_add;
        layout.total_size = checked_add(
            checked_add(checked_add(checked_add(checked_add(
                size_u, size_v, "total"), size_w, "total"), size_ph, "total"),
                size_t, "total"), size_mu, "total");

        // Offsets accumulate through the same checked addition, so a layout can never be
        // built with a wrapped start.
        const int64_t off_v  = checked_add(0, size_u, "offset");
        const int64_t off_w  = checked_add(off_v, size_v, "offset");
        const int64_t off_ph = checked_add(off_w, size_w, "offset");
        const int64_t off_t  = checked_add(off_ph, size_ph, "offset");
        const int64_t off_mu = checked_add(off_t, size_t, "offset");
        layout.blocks = {
            {"ru", 0,      size_u},
            {"rv", off_v,  size_v},
            {"rw", off_w,  size_w},
            {"ph", off_ph, size_ph},
            {"t",  off_t,  size_t},
            {"mu", off_mu, size_mu}
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
        // The basis belongs HERE, not in one narrow helper. is_valid() is the gate every
        // consumer already passes through -- solver, RHS, preconditioner, operator contract --
        // whereas a dedicated check only guards whoever remembers to call it. This core's
        // coefficients assume dF_mu/du ~ mu*H; a CoupledMomentum layout makes that ~H and
        // changes every coupling coefficient by a factor of mu, so it is not a valid layout for
        // this core rather than merely an unsupported option.
        if (momentum_basis != MomentumBasis::Velocity) return false;

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
// Every coefficient in this core assumes the velocity basis -- dF_mu/du ~ mu*H, not ~H. A layout
// declaring CoupledMomentum would silently change those units by a factor of mu (~8.9e+04), so
// consumers refuse it rather than proceeding on the wrong one. Without this the basis field is
// decoration: recorded, and unable to stop the mistake it exists to name.
// A cheap structural fingerprint of the layout, so a token can say WHICH partition an operator
// was formed against. Covers the block boundaries, not just the total: two layouts can share a
// total size and still cut it differently, which is exactly the confusion worth catching.
inline uint64_t layout_digest(const StateLayout& layout) {
    uint64_t h = 1469598103934665603ULL;                 // FNV-1a offset basis
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<uint64_t>(layout.total_size));
    for (const auto& b : layout.blocks) {
        mix(static_cast<uint64_t>(b.start));
        mix(static_cast<uint64_t>(b.size));
    }
    mix(static_cast<uint64_t>(layout.momentum_basis));
    return h ? h : 1ULL;                                 // never 0, which means "unset"
}

inline void require_velocity_basis(const StateLayout& layout, const char* who) {
    TORCH_CHECK(layout.momentum_basis == MomentumBasis::Velocity,
                who, ": this core's coefficients assume the VELOCITY momentum basis "
                "(Registry.EM_COMMON declares u in m s-1); a CoupledMomentum layout would change "
                "every coupling coefficient by a factor of mu");
}

inline torch::Tensor extract_mu_pert_2d(const StateLayout& layout,
                                        const torch::Tensor& packed_state,
                                        int64_t ny, int64_t nx) {
    require_velocity_basis(layout, "extract_mu_pert_2d");
    TORCH_CHECK(layout.is_valid(),
                "extract_mu_pert_2d: packed-state layout is invalid");
    // 9F.D104 (review section 8.1): require an EXACT grid-derived layout. is_valid() checks
    // structure, which a hand-built or estimated layout can satisfy while still describing
    // the wrong grid. The heuristic path is gone, but nothing stopped a future one from
    // being structurally plausible -- so the provenance flag is now part of the contract.
    TORCH_CHECK(layout.is_exact,
                "extract_mu_pert_2d: layout is structurally valid but NOT grid-derived; "
                "mu extraction requires an exact layout");
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
    const int64_t mu_expected = layout_detail::checked_mul(ny, nx, "mu (ny*nx)");
    TORCH_CHECK(mu.size == mu_expected,
                "extract_mu_pert_2d: mu block size ", mu.size, " != ny*nx = ", mu_expected);
    return packed_state.slice(0, mu.start, mu.start + mu.size).reshape({ny, nx}).clone();
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_STATE_LAYOUT_H
