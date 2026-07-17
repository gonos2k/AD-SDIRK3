#pragma once
// PR 9B: the production W-damping term, extracted as a pure helper so the
// standing contracts exercise the PRODUCTION code path.
//
// PR 9C: WRF-parity semantics (dyn_em/module_big_step_utilities_em.f90,
// SUBROUTINE W_DAMP):
//
//   mass      = c1f(k)*mut + c2f(k)
//   vert_cfl  = | (ww/mass) * rdnw(k) * dt |      MASS-DECOUPLED motion —
//                                                 never |ww|*rdnw*dt and
//                                                 never |w|*rdnw*dt
//   active    = vert_cfl > gate_threshold         strict '>'; the CALLER
//                                                 resolves the gate:
//                                                 w_beta (module constant),
//                                                 or w_crit_cfl when
//                                                 zadvect_implicit/ieva > 0
//   term      = SIGN(1., w) * alpha * (vert_cfl - excess_offset) * mass
//               (excess_offset = w_crit_cfl — a DIFFERENT constant from the
//                gate; WRF applies the raw excess verbatim once gated)
//
// and the caller applies rw_tend -= term (WRF's sign convention).
//
// SIGN(1., w) uses the PHYSICAL w (not ww) with ORACLE-pinned zero
// semantics: gfortran (the WRF build compiler, configure.wrf SFC=gfortran)
// gives SIGN(1.,+0)=+1 and SIGN(1.,-0)=-1 (measured 2026-07-17) —
// implemented with torch::signbit; torch::sign (0 at 0) and `w<0 ? -1:+1`
// (+1 at -0) are both wrong here.
//
// Activation (config_flags%w_damping == 1, wired via the Fortran bridge) is
// the CALLER's gate: when disabled the term is not computed at all — WRF
// parity, and the WRF Registry default (0) means the damping is OFF unless
// the namelist enables it.
#include <torch/torch.h>
#include "wrf_sdirk3_rw_term_capture.h"

namespace wrf {
namespace sdirk3 {

// WRF activation CFL constant (share/module_model_constants.F:89:
// "REAL, PARAMETER :: w_beta = 1.0  ! activation cfl number").
inline constexpr float kWrfWBeta = 1.0f;

inline torch::Tensor compute_w_damping_term(
    const torch::Tensor& ww_momentum,   // omega / coupled vertical flux [ny,nz_w,nx]
    const torch::Tensor& w_velocity,    // PHYSICAL w — the sign source  [ny,nz_w,nx]
    const torch::Tensor& mu_full,       // full column mass              [ny,nx]
    const torch::Tensor& c1f,           // vertical coefficient          [nz_w]
    const torch::Tensor& c2f,           // vertical coefficient          [nz_w]
    const torch::Tensor& rdnw_interior, // 1/dnw on interior levels      [1,n,1]
    float dt,
    float w_alpha,
    float gate_threshold,
    float excess_offset,
    int k_start,
    int k_end,
    int nz_w) {
    const int n_interior = k_end - k_start;

    auto ww_int = ww_momentum.slice(1, k_start, k_end);
    auto w_int = w_velocity.slice(1, k_start, k_end);
    auto c1f_int = c1f.slice(0, k_start, k_end).reshape({1, n_interior, 1});
    auto c2f_int = c2f.slice(0, k_start, k_end).reshape({1, n_interior, 1});
    auto mass_factor = c1f_int * mu_full.unsqueeze(1) + c2f_int;

    // Mass-decoupled vertical CFL.
    auto vert_cfl = torch::abs(ww_int / mass_factor * rdnw_interior * dt);
    auto cfl_excess = vert_cfl - excess_offset;
    auto active = vert_cfl > gate_threshold;

    // Fortran SIGN(1., w) — oracle-pinned signbit semantics (see header).
    auto hard_sign = torch::where(torch::signbit(w_int),
                                  torch::full_like(w_int, -1.0f),
                                  torch::full_like(w_int, 1.0f));

    {
        auto& rw_cap = rw_term_capture_slot();
        rw_cap.add("wd_vert_cfl", vert_cfl);
        rw_cap.add("wd_cfl_excess", cfl_excess);
        rw_cap.add("wd_w_sign", hard_sign);
        rw_cap.add("wd_mass_factor", mass_factor);
    }

    auto raw_term = hard_sign * w_alpha * cfl_excess * mass_factor;
    auto w_damp = torch::where(active, raw_term, torch::zeros_like(raw_term));
    return torch::constant_pad_nd(w_damp, {0, 0, k_start, nz_w - k_end});
}

}  // namespace sdirk3
}  // namespace wrf
