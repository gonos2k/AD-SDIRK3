#ifndef WRF_SDIRK3_AD_HALO_UTILS_H
#define WRF_SDIRK3_AD_HALO_UTILS_H

/**
 * @file wrf_sdirk3_ad_halo_utils.h
 * @brief AD-safe utility functions for full-halo pack/unpack/pad/extract operations
 *
 * All functions preserve the autograd computational graph.
 * Used by ADHaloExchangeFunction and Newton solver in DA mode.
 */

#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

// ============================================================================
// Step 2c-bis: Single source of truth for full-halo packed size (Finding #20)
// ============================================================================

inline int64_t computeFullHaloPackedSize(int64_t nj_mem, int64_t ni_mem,
                                          int64_t nz, int64_t nz_w) {
    // U[nj_mem,nz,ni_mem] + V[nj_mem,nz,ni_mem] + T[nj_mem,nz,ni_mem] = 3*nz
    // W[nj_mem,nz_w,ni_mem] + PH[nj_mem,nz_w,ni_mem]                   = 2*nz_w
    // MU[nj_mem,1,ni_mem]                                                = 1
    return nj_mem * ni_mem * (3 * nz + 2 * nz_w + 1);
}

// ============================================================================
// Step 2a: padToFullHalo — per-field, AD-safe
// ============================================================================

// F::pad is differentiable. Backward = extract interior grad from full-halo grad.
// Pad spec (innermost dim first): {i_off, ni_mem-i_off-nx, 0, 0, j_off, nj_mem-j_off-ny}
// interior shape: [ny, nk, nx]
// output shape: [nj_mem, nk, ni_mem]
inline torch::Tensor padToFullHalo(
    const torch::Tensor& interior,  // [ny, nk, nx]
    int64_t nj_mem, int64_t ni_mem,
    int64_t j_off, int64_t i_off)
{
    int64_t ny = interior.size(0);
    int64_t nx = interior.size(2);
    // F::pad pad spec is innermost-dim first:
    // dim 2 (i): left=i_off, right=ni_mem-i_off-nx
    // dim 1 (k): left=0, right=0
    // dim 0 (j): left=j_off, right=nj_mem-j_off-ny
    return torch::nn::functional::pad(interior,
        torch::nn::functional::PadFuncOptions(
            {i_off, ni_mem - i_off - nx,
             0, 0,
             j_off, nj_mem - j_off - ny}));
}

// ============================================================================
// Step 2b: extractInteriorFromFullHalo — AD-safe view
// ============================================================================

// Returns a VIEW (not clone): gradient flows through naturally.
inline torch::Tensor extractInteriorFromFullHalo(
    const torch::Tensor& full_halo,  // [nj_mem, nk, ni_mem]
    int64_t j_off, int64_t ny, int64_t i_off, int64_t nx)
{
    return full_halo.slice(0, j_off, j_off + ny).slice(2, i_off, i_off + nx);
}

// ============================================================================
// Step 2c: viewFullHaloFields — view-based unpack (NO clone)
// ============================================================================

struct FullHaloFieldViews {
    torch::Tensor u, v, w, ph, t, mu;
};

// Mutable overload: for AD halo forward (in-place MPI + BC on clone)
inline FullHaloFieldViews viewFullHaloFields(
    torch::Tensor& packed,
    int64_t nj_mem, int64_t nk, int64_t ni_mem, int64_t nk_w)
{
    int64_t plane = nj_mem * ni_mem;
    int64_t off = 0;

    // U: [nj_mem, nk, ni_mem]
    auto u = packed.narrow(0, off, plane * nk).view({nj_mem, nk, ni_mem});
    off += plane * nk;

    // V: [nj_mem, nk, ni_mem]
    auto v = packed.narrow(0, off, plane * nk).view({nj_mem, nk, ni_mem});
    off += plane * nk;

    // W: [nj_mem, nk_w, ni_mem]
    auto w = packed.narrow(0, off, plane * nk_w).view({nj_mem, nk_w, ni_mem});
    off += plane * nk_w;

    // PH: [nj_mem, nk_w, ni_mem]
    auto ph = packed.narrow(0, off, plane * nk_w).view({nj_mem, nk_w, ni_mem});
    off += plane * nk_w;

    // T: [nj_mem, nk, ni_mem]
    auto t = packed.narrow(0, off, plane * nk).view({nj_mem, nk, ni_mem});
    off += plane * nk;

    // MU: [nj_mem, 1, ni_mem]
    auto mu = packed.narrow(0, off, plane * 1).view({nj_mem, 1, ni_mem});

    return {u, v, w, ph, t, mu};
}

// Const overload (Finding #24): for computeUnifiedRHSFullHalo, combineGhostAndInterior
inline FullHaloFieldViews viewFullHaloFields(
    const torch::Tensor& packed,
    int64_t nj_mem, int64_t nk, int64_t ni_mem, int64_t nk_w)
{
    // const_cast is safe here — we return views that are read-only in practice.
    // The returned views alias packed; no mutation occurs in const paths.
    torch::Tensor mutable_ref = packed;
    return viewFullHaloFields(mutable_ref, nj_mem, nk, ni_mem, nk_w);
}

// ============================================================================
// Step 2e: padInteriorToFullHaloState / extractInteriorFromFullHaloState
// ============================================================================

// Packed-state-level wrappers for interior↔full-halo conversion.
// These unpack → pad/extract each field → repack. All AD-safe.

// Forward: interior packed → full-halo packed
// interior_packed: flat 1D tensor of interior-sized fields
// Returns: flat 1D tensor of full-halo-sized fields
inline torch::Tensor padInteriorToFullHaloState(
    const torch::Tensor& interior_packed,
    int64_t nj_mem, int64_t ni_mem,
    int64_t j_off, int64_t i_off,
    int64_t ny, int64_t nx, int64_t nx_u, int64_t ny_v,
    int64_t nz, int64_t nz_w)
{
    int64_t off = 0;
    auto get_field = [&](int64_t nj, int64_t nk, int64_t ni) {
        int64_t sz = nj * nk * ni;
        auto f = interior_packed.narrow(0, off, sz).view({nj, nk, ni});
        off += sz;
        return f;
    };

    auto u_int  = get_field(ny,   nz,   nx_u);
    auto v_int  = get_field(ny_v, nz,   nx);
    auto w_int  = get_field(ny,   nz_w, nx);
    auto ph_int = get_field(ny,   nz_w, nx);
    auto t_int  = get_field(ny,   nz,   nx);
    auto mu_int = get_field(ny,   1,    nx);

    // Pad each field to full-halo dimensions
    // U: interior [ny, nz, nx_u] → full [nj_mem, nz, ni_mem]
    auto u_full  = padToFullHalo(u_int,  nj_mem, ni_mem, j_off, i_off);
    auto v_full  = padToFullHalo(v_int,  nj_mem, ni_mem, j_off, i_off);
    auto w_full  = padToFullHalo(w_int,  nj_mem, ni_mem, j_off, i_off);
    auto ph_full = padToFullHalo(ph_int, nj_mem, ni_mem, j_off, i_off);
    auto t_full  = padToFullHalo(t_int,  nj_mem, ni_mem, j_off, i_off);
    auto mu_full = padToFullHalo(mu_int, nj_mem, ni_mem, j_off, i_off);

    // Repack: flatten each and cat
    return torch::cat({
        u_full.flatten(), v_full.flatten(), w_full.flatten(),
        ph_full.flatten(), t_full.flatten(), mu_full.flatten()
    });
}

// Reverse: full-halo packed → interior packed
inline torch::Tensor extractInteriorFromFullHaloState(
    const torch::Tensor& full_packed,
    int64_t nj_mem, int64_t ni_mem,
    int64_t j_off, int64_t i_off,
    int64_t ny, int64_t nx, int64_t nx_u, int64_t ny_v,
    int64_t nz, int64_t nz_w)
{
    auto fields = viewFullHaloFields(full_packed, nj_mem, nz, ni_mem, nz_w);

    // Extract interior views per field
    auto u_int  = extractInteriorFromFullHalo(fields.u,  j_off, ny,   i_off, nx_u);
    auto v_int  = extractInteriorFromFullHalo(fields.v,  j_off, ny_v, i_off, nx);
    auto w_int  = extractInteriorFromFullHalo(fields.w,  j_off, ny,   i_off, nx);
    auto ph_int = extractInteriorFromFullHalo(fields.ph, j_off, ny,   i_off, nx);
    auto t_int  = extractInteriorFromFullHalo(fields.t,  j_off, ny,   i_off, nx);
    auto mu_int = extractInteriorFromFullHalo(fields.mu, j_off, ny,   i_off, nx);

    return torch::cat({
        u_int.flatten(), v_int.flatten(), w_int.flatten(),
        ph_int.flatten(), t_int.flatten(), mu_int.flatten()
    });
}

// ============================================================================
// Step 2f: precomputeGhostMask
// ============================================================================

// Returns bool tensor [nj_mem, nk, ni_mem]: true in ghost, false in interior.
// No gradient. Used with torch::where() for combineGhostAndInterior.
inline torch::Tensor precomputeGhostMask(
    int64_t nj_mem, int64_t nk, int64_t ni_mem,
    int64_t j_off, int64_t ny, int64_t i_off, int64_t nx)
{
    auto mask = torch::ones({nj_mem, nk, ni_mem}, torch::kBool);
    // Interior region = false (not ghost)
    mask.slice(0, j_off, j_off + ny).slice(2, i_off, i_off + nx).fill_(false);
    return mask;
}

// ============================================================================
// Step 7c helper: fullHaloSlice (Finding #38)
// ============================================================================

// Architectural enforcement of slice-only policy for computeUnifiedRHSFullHalo.
// Returns field.slice(0, j_s, j_e).slice(2, i_s, i_e) → interior or shifted view.
inline torch::Tensor fullHaloSlice(
    const torch::Tensor& field,
    int64_t j_s, int64_t j_e, int64_t i_s, int64_t i_e)
{
    return field.slice(0, j_s, j_e).slice(2, i_s, i_e);
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AD_HALO_UTILS_H
