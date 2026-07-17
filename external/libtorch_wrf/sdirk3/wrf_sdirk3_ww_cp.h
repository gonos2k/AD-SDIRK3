#pragma once
// PR 9C.1: the COMPLETE WRF calc_ww_cp state-to-omega diagnosis
// (dyn_em/module_big_step_utilities_em.f90, SUBROUTINE calc_ww_cp),
// extracted as a pure helper so the production W-damping path and the
// standing contract exercise the SAME function.
//
// Verbatim WRF algebra ([j,k,i] -> our [ny,nz,nx] layout):
//
//   mut = mup + mub                       (FULL column mass)
//   muu(i)   = 0.5*(mut(i) + mut(i-1))    (u-points)
//   muv(j)   = 0.5*(mut(j) + mut(j-1))    (v-points)
//   divv(k)  = msftx * dnw(k) * (
//                rdx * dx[ (c1h(k)*muu + c2h(k)) * u / msfuy ]
//              + rdy * dy[ (c1h(k)*muv + c2h(k)) * v * msfvx_inv ] )
//   dmdt     = sum_k divv(k)
//   ww(0) = 0;  ww(top) = 0
//   ww(k) = ww(k-1) - dnw(k-1)*c1h(k-1)*dmdt - divv(k-1),  k = 1..nz-1
//
// Notes pinned against the Fortran:
//  - msftx is the ONLY mass-point map factor used (msfty/msfux/msfvx/msfvy
//    are passed to the Fortran routine but unused in its loops);
//  - muu/muv average the FULL mass (mup+mub), not the perturbation;
//  - the recurrence uses c1h(k-1) — the em_b_wave simplification (c1h==1,
//    c2h==0, msf==1) is exactly what this helper replaces; it survives only
//    as the contract's negative control.
//
// Domain-edge policy: WRF evaluates muu(ids)/muv(jds) with MEMORY-HALO
// neighbors, so the edge value is a property of the lateral boundary
// condition, not of the averaging stencil. The caller must therefore state
// the per-axis policy explicitly:
//   Periodic           — the halo neighbor is the opposite domain edge
//                        (WRF periodic_x/periodic_y): the seam average wraps,
//                        muu(edge) = 0.5*(mut(first)+mut(last)).
//   SymmetricReplicate — the halo neighbor mirrors the boundary value (WRF
//                        symmetric_*s/_*e about the stagger point), so the
//                        one-sided average degenerates to the edge value.
//   HaloProvided       — an authoritative halo would be required (open /
//                        specified / nested boundaries, or an internal
//                        multi-tile / multi-rank seam). NOT implemented:
//                        fails closed.
//   Unsupported        — the caller determined no supported policy applies:
//                        fails closed.
// There is no default and no silent fallback; guessing a halo is exactly the
// class of defect this helper exists to remove.
#include <torch/torch.h>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

enum class WWCPBoundaryPolicy {
    Periodic,
    SymmetricReplicate,
    HaloProvided,
    Unsupported
};

// PR 9C.2: the single authority that decides whether the ENABLED W-damping
// parity path may run at all, and with which per-axis boundary policies.
// Judged BEFORE any Newton callback is constructed, in the reviewer-fixed
// order — and unsupported flags take PRIORITY over periodic/symmetric, so a
// conflicting flag combination can never smuggle a guessed physical
// boundary past the contract. Throws the stable marker on any violation;
// returns {active=false} when the damping is simply disabled.
struct WdampRuntimeContract {
    bool active = false;
    WWCPBoundaryPolicy x_policy = WWCPBoundaryPolicy::Unsupported;
    WWCPBoundaryPolicy y_policy = WWCPBoundaryPolicy::Unsupported;
};

inline WdampRuntimeContract resolve_wdamp_runtime_contract(
    bool w_damping_enabled,
    int nprocx, int nprocy,
    bool tile_covers_patch,
    bool periodic_x, bool symmetric_xs, bool symmetric_xe,
    bool open_xs, bool open_xe,
    bool periodic_y, bool symmetric_ys, bool symmetric_ye,
    bool open_ys, bool open_ye,
    bool specified, bool nested, bool polar) {
    if (!w_damping_enabled) {
        return {};  // inactive: no constraint on topology or boundaries
    }
    if (nprocx * nprocy != 1) {
        throw std::runtime_error(
            "SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: enabled W-damping on "
            "a multi-rank decomposition — patch edges are internal seams "
            "with no authoritative mass halo (HaloProvided is not "
            "implemented)");
    }
    if (!tile_covers_patch) {
        throw std::runtime_error(
            "SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: enabled W-damping on "
            "an internal tile — this tile does not cover the rank patch, so "
            "its edges are internal multi-tile seams, not physical "
            "boundaries");
    }
    if (specified || nested || polar) {
        throw std::runtime_error(
            "SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: enabled W-damping "
            "with specified/nested/polar boundaries — no authoritative mass "
            "halo policy exists for them");
    }
    const auto axis = [](bool open_s, bool open_e, bool periodic, bool sym_s,
                         bool sym_e, const char* name) {
        if (open_s || open_e) {
            throw std::runtime_error(
                std::string("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: "
                            "enabled W-damping with an open ") +
                name + "-boundary (takes priority over any periodic/"
                "symmetric flag also set)");
        }
        if (periodic) return WWCPBoundaryPolicy::Periodic;
        if (sym_s && sym_e) return WWCPBoundaryPolicy::SymmetricReplicate;
        throw std::runtime_error(
            std::string("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: enabled "
                        "W-damping ") +
            name + "-boundary is neither periodic nor two-sided symmetric");
    };
    WdampRuntimeContract c;
    c.active = true;
    c.x_policy = axis(open_xs, open_xe, periodic_x, symmetric_xs,
                      symmetric_xe, "x");
    c.y_policy = axis(open_ys, open_ye, periodic_y, symmetric_ys,
                      symmetric_ye, "y");
    return c;
}

inline const char* wwcp_policy_name(WWCPBoundaryPolicy p) {
    switch (p) {
        case WWCPBoundaryPolicy::Periodic: return "Periodic";
        case WWCPBoundaryPolicy::SymmetricReplicate: return "SymmetricReplicate";
        case WWCPBoundaryPolicy::HaloProvided: return "HaloProvided";
        case WWCPBoundaryPolicy::Unsupported: return "Unsupported";
    }
    return "?";
}

inline torch::Tensor compute_wrf_ww_cp(
    const torch::Tensor& u,          // [ny, nz, nx_u]
    const torch::Tensor& v,          // [ny_v, nz, nx]
    const torch::Tensor& mup,        // [ny, nx] perturbation column mass
    const torch::Tensor& mub,        // [ny, nx] base-state column mass
    const torch::Tensor& c1h,        // [nz]
    const torch::Tensor& c2h,        // [nz]
    const torch::Tensor& dnw,        // [nz] (WRF-negative eta thickness)
    float rdx,
    float rdy,
    const torch::Tensor& msftx,      // [ny, nx]
    const torch::Tensor& msfuy,      // [ny, nx_u]
    const torch::Tensor& msfvx_inv,  // [ny_v, nx]
    WWCPBoundaryPolicy x_policy,
    WWCPBoundaryPolicy y_policy)
{
    using torch::indexing::Slice;

    // PR 9C.1 fail-close, ordered so every violation ends in the STABLE
    // marker: definedness first, then rank, and only then are sizes read —
    // an undefined or under-ranked tensor must never reach a size()/view()
    // call and surface as a generic c10 exception instead of the contract
    // marker.
    if (!u.defined() || !v.defined() || !mup.defined() || !mub.defined() ||
        !c1h.defined() || !c2h.defined() || !dnw.defined() ||
        !msftx.defined() || !msfuy.defined() || !msfvx_inv.defined()) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp received an undefined "
            "tensor operand");
    }
    if (u.dim() != 3 || v.dim() != 3 || mup.dim() != 2 || mub.dim() != 2 ||
        msftx.dim() != 2 || msfuy.dim() != 2 || msfvx_inv.dim() != 2 ||
        c1h.dim() != 1 || c2h.dim() != 1 || dnw.dim() != 1) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp operand rank contract "
            "violated (u/v 3-D, mu/msf 2-D, c1h/c2h/dnw 1-D)");
    }

    const int ny = static_cast<int>(mup.size(0));
    const int nx = static_cast<int>(mup.size(1));
    const int nz = static_cast<int>(u.size(1));

    // Coefficient lengths are EXACT (the production caller slices to nz);
    // view({1,nz,1}) below would reject longer inputs anyway, so the
    // contract states it up front with the stable marker.
    if (u.size(0) != ny || u.size(2) != nx + 1 || v.size(0) != ny + 1 ||
        v.size(1) != nz || v.size(2) != nx || mub.size(0) != ny ||
        mub.size(1) != nx || c1h.numel() != nz || c2h.numel() != nz ||
        dnw.numel() != nz || msftx.size(0) != ny || msftx.size(1) != nx ||
        msfuy.size(0) != ny || msfuy.size(1) != nx + 1 ||
        msfvx_inv.size(0) != ny + 1 || msfvx_inv.size(1) != nx) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp operand shape contract "
            "violated (u[ny,nz,nx+1], v[ny+1,nz,nx], mu[ny,nx], msf* on "
            "their staggers, c1h/c2h/dnw exactly nz)");
    }
    {
        const auto dev = mup.device();
        const auto dtp = mup.scalar_type();
        for (const auto* t : {&u, &v, &mub, &c1h, &c2h, &dnw, &msftx, &msfuy,
                              &msfvx_inv}) {
            if (t->device() != dev || t->scalar_type() != dtp) {
                throw std::invalid_argument(
                    "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp operands must "
                    "share one device and dtype");
            }
        }
    }
    // Metric/coefficient sanity in ONE device sync: the map factors divide
    // (msfuy) or scale the fluxes, so a non-finite or non-positive metric
    // produces a silently wrong omega, not merely an inactive one.
    {
        torch::NoGradGuard no_grad;
        auto bad = torch::stack({(~msftx.isfinite()).any(),
                                 (~msfuy.isfinite()).any(),
                                 (~msfvx_inv.isfinite()).any(),
                                 (msfuy <= 0.0f).any(),
                                 (~c1h.isfinite()).any(),
                                 (~c2h.isfinite()).any(),
                                 (~dnw.isfinite()).any()})
                       .to(torch::kCPU);
        if (bad.any().item<bool>()) {
            throw std::invalid_argument(
                "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp metric/coefficient "
                "contract violated (msftx/msfuy/msfvx_inv/c1h/c2h/dnw must "
                "be finite; msfuy strictly positive)");
        }
    }

    const auto require_supported = [](WWCPBoundaryPolicy p, const char* axis) {
        if (p == WWCPBoundaryPolicy::Periodic ||
            p == WWCPBoundaryPolicy::SymmetricReplicate) {
            return;
        }
        throw std::runtime_error(
            std::string("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: "
                        "calc_ww_cp ") +
            axis + "-boundary policy '" + wwcp_policy_name(p) +
            "' has no authoritative mass halo (open/specified/nested "
            "boundaries and internal tile/rank seams are not implemented)");
    };
    require_supported(x_policy, "x");
    require_supported(y_policy, "y");

    auto mut = mup + mub;  // [ny, nx]

    // muu at u-points [ny, nx+1] — assembled out-of-place (cat), never by
    // slice/select assignment, so the autograd graph stays intact. The seam
    // columns carry the boundary policy: periodic wraps to the opposite
    // edge, symmetric degenerates to the edge value.
    auto muu_interior = 0.5f * (mut.slice(1, 1, nx) + mut.slice(1, 0, nx - 1));
    torch::Tensor muu;
    if (x_policy == WWCPBoundaryPolicy::Periodic) {
        auto seam =
            0.5f * (mut.slice(1, 0, 1) + mut.slice(1, nx - 1, nx));
        muu = torch::cat({seam, muu_interior, seam}, 1);
    } else {  // SymmetricReplicate
        muu = torch::cat(
            {mut.slice(1, 0, 1), muu_interior, mut.slice(1, nx - 1, nx)}, 1);
    }

    // muv at v-points [ny+1, nx] (same per-policy seam handling)
    auto muv_interior = 0.5f * (mut.slice(0, 1, ny) + mut.slice(0, 0, ny - 1));
    torch::Tensor muv;
    if (y_policy == WWCPBoundaryPolicy::Periodic) {
        auto seam =
            0.5f * (mut.slice(0, 0, 1) + mut.slice(0, ny - 1, ny));
        muv = torch::cat({seam, muv_interior, seam}, 0);
    } else {  // SymmetricReplicate
        muv = torch::cat(
            {mut.slice(0, 0, 1), muv_interior, mut.slice(0, ny - 1, ny)}, 0);
    }

    auto c1k = c1h.view({1, nz, 1});
    auto c2k = c2h.view({1, nz, 1});
    auto dnwk = dnw.view({1, nz, 1});

    // coupled fluxes with map factors
    auto cu = (c1k * muu.unsqueeze(1) + c2k) * u / msfuy.unsqueeze(1);
    auto cv = (c1k * muv.unsqueeze(1) + c2k) * v * msfvx_inv.unsqueeze(1);

    auto divx = rdx * (cu.index({Slice(), Slice(), Slice(1, nx + 1)}) -
                       cu.index({Slice(), Slice(), Slice(0, nx)}));
    auto divy = rdy * (cv.index({Slice(1, ny + 1), Slice(), Slice()}) -
                       cv.index({Slice(0, ny), Slice(), Slice()}));
    auto divv = msftx.unsqueeze(1) * dnwk * (divx + divy);  // [ny, nz, nx]
    auto dmdt = divv.sum(1);                                // [ny, nx]

    // The recurrence ww(k) = ww(k-1) - dnw(k-1)*c1h(k-1)*dmdt - divv(k-1)
    // with ww(0)=0 is a running sum: ww(k) = -cumsum over the first k
    // per-level contributions. One cumsum replaces the nz-1 small-tensor
    // loop + stack (contract-verified against the scalar reference).
    auto contrib = (dnwk * c1k).slice(1, 0, nz - 1) * dmdt.unsqueeze(1) +
                   divv.slice(1, 0, nz - 1);            // [ny, nz-1, nx]
    auto zeros_lvl = torch::zeros({ny, 1, nx}, mut.options());
    // Interior levels k=1..nz-1; explicit WRF BCs ww(0)=0 and ww(top)=0.
    return torch::cat({zeros_lvl, -contrib.cumsum(1), zeros_lvl}, 1);
}

}  // namespace sdirk3
}  // namespace wrf
