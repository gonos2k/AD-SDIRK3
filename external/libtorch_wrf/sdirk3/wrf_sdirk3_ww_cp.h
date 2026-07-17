#pragma once
// PR 9C.1: the COMPLETE WRF calc_ww_cp state-to-omega diagnosis
// (dyn_em/module_big_step_utilities_em.f90, SUBROUTINE calc_ww_cp),
// extracted as a pure helper so the production W-damping path and the
// standing contract exercise the SAME function.
//
// Verbatim WRF algebra ([j,k,i] -> our [ny,nz,nx] layout):
//
//   mut = mup + mub                       (FULL column mass)
//   muu(i)   = 0.5*(mut(i) + mut(i-1))    (u-points; domain-edge REPLICATE)
//   muv(j)   = 0.5*(mut(j) + mut(j-1))    (v-points; domain-edge REPLICATE)
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
// Domain-edge policy: WRF evaluates muu/muv with memory-halo neighbors; this
// single-tile, halo-free helper REPLICATES the edge value (one-sided
// average), matching the tile's avg_x_to_u_2d / avg_y_to_v_2d convention.
// The scalar reference in the contract implements the same policy, so the
// interior algebra is contracted exactly and the edge convention is
// contracted explicitly.
#include <torch/torch.h>
#include <stdexcept>

namespace wrf {
namespace sdirk3 {

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
    const torch::Tensor& msfvx_inv)  // [ny_v, nx]
{
    using torch::indexing::Slice;
    const int ny = static_cast<int>(mup.size(0));
    const int nx = static_cast<int>(mup.size(1));
    const int nz = static_cast<int>(u.size(1));

    // PR 9C.1 fail-close: every operand must satisfy the calc_ww_cp shape
    // contract; a silent mismatch would broadcast into a wrong omega.
    if (u.dim() != 3 || v.dim() != 3 || mup.dim() != 2 || mub.dim() != 2 ||
        u.size(0) != ny || u.size(2) != nx + 1 || v.size(0) != ny + 1 ||
        v.size(1) != nz || v.size(2) != nx || mub.size(0) != ny ||
        mub.size(1) != nx || c1h.numel() < nz || c2h.numel() < nz ||
        dnw.numel() < nz || msftx.size(0) != ny || msftx.size(1) != nx ||
        msfuy.size(0) != ny || msfuy.size(1) != nx + 1 ||
        msfvx_inv.size(0) != ny + 1 || msfvx_inv.size(1) != nx) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: calc_ww_cp operand shape contract "
            "violated (u[ny,nz,nx+1], v[ny+1,nz,nx], mu[ny,nx], msf* on "
            "their staggers, c1h/c2h/dnw >= nz)");
    }

    auto mut = mup + mub;  // [ny, nx]

    // muu at u-points [ny, nx+1], edge REPLICATE — assembled out-of-place
    // (cat), never by slice/select assignment, so the autograd graph stays
    // intact through the staggered averages.
    auto muu = torch::cat(
        {mut.slice(1, 0, 1),
         0.5f * (mut.slice(1, 1, nx) + mut.slice(1, 0, nx - 1)),
         mut.slice(1, nx - 1, nx)},
        1);

    // muv at v-points [ny+1, nx], edge REPLICATE (same out-of-place policy)
    auto muv = torch::cat(
        {mut.slice(0, 0, 1),
         0.5f * (mut.slice(0, 1, ny) + mut.slice(0, 0, ny - 1)),
         mut.slice(0, ny - 1, ny)},
        0);

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

    std::vector<torch::Tensor> wwv(static_cast<size_t>(nz) + 1);
    wwv[0] = torch::zeros({ny, nx}, mut.options());
    for (int k = 1; k <= nz - 1; ++k) {
        wwv[k] = wwv[k - 1] - dnw[k - 1] * c1h[k - 1] * dmdt -
                 divv.index({Slice(), k - 1, Slice()});
    }
    // Explicit WRF top boundary: ww(i,kte,j) = 0.
    wwv[nz] = torch::zeros({ny, nx}, mut.options());

    return torch::stack(wwv, 1);  // [ny, nz+1, nx]
}

}  // namespace sdirk3
}  // namespace wrf
