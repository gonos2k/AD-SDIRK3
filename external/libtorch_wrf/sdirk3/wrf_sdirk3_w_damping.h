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
#include <cmath>
#include <stdexcept>
#include "wrf_sdirk3_rw_term_capture.h"

namespace wrf {
namespace sdirk3 {

// WRF activation CFL constant (share/module_model_constants.F:89:
// "REAL, PARAMETER :: w_beta = 1.0  ! activation cfl number").
inline constexpr float kWrfWBeta = 1.0f;

// WRF damping strength constant (share/module_model_constants.F:88:
// "REAL, PARAMETER :: w_alpha = 0.3  ! strength m/s/s"). The PARITY path
// must use THIS constant — WRF exposes no namelist for it. The legacy
// sdirk3 knob (w_damp_alpha) is a non-parity tuning input and no longer
// feeds the parity term.
inline constexpr float kWrfWAlpha = 0.3f;

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
    // PR 9C.1: fail-close input authority. WRF's W_DAMP divides by the
    // column mass and scales by dt — a non-finite or non-positive mass, a
    // bad dt, or mismatched shapes produce a silently wrong (not merely
    // inactive) term, so they are rejected, never repaired.
    if (!(dt > 0.0f) || !std::isfinite(dt) || !std::isfinite(gate_threshold) ||
        !std::isfinite(excess_offset) || !std::isfinite(w_alpha) ||
        k_start < 0 || k_end <= k_start || k_end > nz_w) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: dt/gate/offset/alpha must be finite "
            "(dt>0) and 0<=k_start<k_end<=nz_w");
    }
    if (!ww_momentum.defined() || !w_velocity.defined() ||
        !mu_full.defined() || ww_momentum.dim() != 3 ||
        w_velocity.dim() != 3 || mu_full.dim() != 2 ||
        ww_momentum.size(0) != mu_full.size(0) ||
        ww_momentum.size(2) != mu_full.size(1) ||
        w_velocity.sizes() != ww_momentum.sizes() ||
        c1f.numel() < k_end || c2f.numel() < k_end) {
        throw std::invalid_argument(
            "SDIRK3_WDAMP_INVALID_INPUT: ww/w [ny,nz_w,nx] and mu [ny,nx] "
            "shape contract violated (or c1f/c2f shorter than k_end)");
    }
    {
        // Mass positivity/finiteness gate — a diagnostic reduction, not a
        // graph participant. Runs only on the ENABLED path (w_damping=1).
        torch::NoGradGuard no_grad;
        if (!mu_full.isfinite().all().item<bool>() ||
            !(mu_full.min().item<float>() > 0.0f)) {
            throw std::invalid_argument(
                "SDIRK3_WDAMP_INVALID_MASS: full column mass must be finite "
                "and strictly positive");
        }
    }
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
    // signbit -> {0,1} -> {+1,-1} arithmetically (one allocation; the
    // where/full_like form allocated two full tensors per call).
    auto hard_sign =
        1.0f - 2.0f * torch::signbit(w_int).to(w_int.scalar_type());

    {
        auto& rw_cap = rw_term_capture_slot();
        rw_cap.add("wd_vert_cfl", vert_cfl);
        rw_cap.add("wd_cfl_excess", cfl_excess);
        rw_cap.add("wd_w_sign", hard_sign);
        rw_cap.add("wd_mass_factor", mass_factor);
    }

    auto raw_term = hard_sign * w_alpha * cfl_excess * mass_factor;
    // Hard where-gate (NOT active*raw_term: a multiplicative mask would let
    // a NaN/Inf in an INACTIVE cell poison the term as 0*NaN=NaN). The
    // zero branch broadcasts a 1-element tensor instead of a full clone.
    auto w_damp = torch::where(active, raw_term,
                               torch::zeros({1}, raw_term.options()));
    return torch::constant_pad_nd(w_damp, {0, 0, k_start, nz_w - k_end});
}

}  // namespace sdirk3
}  // namespace wrf
