#pragma once
// PR 9B: the production W-damping term (tile_unified_impl Step 6), extracted
// as a pure header-inline helper so the standing tangent contract
// (WDamp_Tangent_Contract) exercises the PRODUCTION code path — not a
// transcription of it. tile_unified_impl calls exactly this function; a
// future dual-severing edit here fails the contract.
//
// Semantics are byte-identical to the previous inline block: same ops, same
// order. The opt-in rw-term capture hooks (gate-arg-level factors) live
// inside so the bisection diagnostics keep working.
#include <torch/torch.h>
#include "wrf_sdirk3_rw_term_capture.h"

namespace wrf {
namespace sdirk3 {

inline torch::Tensor compute_w_damping_term(
    const torch::Tensor& w,         // [ny, nz_w, nx] vertical momentum (may be dual)
    const torch::Tensor& mu_full,   // [ny, nx] full column mass (may be dual)
    const torch::Tensor& c1f,       // [nz_w] vertical coefficient (constant)
    const torch::Tensor& c2f,       // [nz_w] vertical coefficient (constant)
    const torch::Tensor& dz,        // [1, n_interior, 1] eta thickness (constant)
    float dt,
    float w_alpha,
    float w_crit_cfl,
    float sign_smooth_delta,
    int k_start,
    int k_end,
    int nz_w) {
    const int n_interior = k_end - k_start;

    // w_interior: [ny, n_interior, nx]
    auto w_interior = w.slice(1, k_start, k_end);

    // Vertical CFL: [ny, n_interior, nx]
    auto vert_cfl = torch::abs(w_interior) * dt / dz;
    auto cfl_excess = vert_cfl - w_crit_cfl;

    // v20.8: Smooth sign — same fix as flux5_upwind (see comment there)
    torch::Tensor w_sign;
    if (sign_smooth_delta > 0.0f) {
        w_sign = w_interior /
                 torch::sqrt(w_interior * w_interior +
                             sign_smooth_delta * sign_smooth_delta);
    } else {
        w_sign = torch::sign(w_interior).detach();
    }

    // Mass coupling: c1f[k]*mu_full + c2f[k] for interior k
    auto c1f_int = c1f.slice(0, k_start, k_end).reshape({1, n_interior, 1});
    auto c2f_int = c2f.slice(0, k_start, k_end).reshape({1, n_interior, 1});
    auto mu_3d = mu_full.unsqueeze(1);
    auto mass_factor = c1f_int * mu_3d + c2f_int;  // [ny, n_interior, nx]

    {
        // PR 9B gate-arg-level capture: every factor of the damp chain.
        auto& rw_cap = rw_term_capture_slot();
        rw_cap.add("wd_vert_cfl", vert_cfl);
        rw_cap.add("wd_cfl_excess", cfl_excess);
        rw_cap.add("wd_w_sign", w_sign);
        rw_cap.add("wd_mass_factor", mass_factor);
    }

    // Damping: [ny, n_interior, nx], padded to [ny, nz_w, nx]
    auto w_damp = w_sign * w_alpha * cfl_excess * mass_factor;
    return torch::constant_pad_nd(w_damp, {0, 0, k_start, nz_w - k_end});
}

}  // namespace sdirk3
}  // namespace wrf
