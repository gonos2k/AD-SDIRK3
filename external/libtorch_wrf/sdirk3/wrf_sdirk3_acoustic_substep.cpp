// wrf_sdirk3_acoustic_substep.cpp — differentiable WRF split-explicit acoustic sub-step (Inc 5).
// One-to-one port of module_small_step_em.F. Autograd-safe (out-of-place; no .item() on the
// compute path). See wrf_sdirk3_acoustic_substep.h for the state/const contracts.
//
// STATUS (Inc 5 build in progress):
//   advance_uv     — hydrostatic 3-term PGF + frozen ru_tend/rv_tend IMPLEMENTED;
//                    non-hydrostatic 4th term (needs php/dpn) + divergence damping = TODO.
//   advance_mu_t / advance_w / calc_p_rho — signatures in place; bodies to follow.
// Validation is deferred to the ASSEMBLED sub-step vs a dyn_em [PARITY substep] dump.
#include "wrf_sdirk3_acoustic_substep.h"

namespace wrf { namespace sdirk3 { namespace acoustic {

namespace {
using torch::indexing::Slice;
using torch::indexing::None;
inline torch::indexing::Slice SL() { return torch::indexing::Slice(); }

// mass-field i-difference X[i] - X[i-1] over the last (nx) dim -> {..., nx-1} interior u-points.
inline torch::Tensor di(const torch::Tensor& X) {
    return X.index({SL(), SL(), Slice(1, None)}) - X.index({SL(), SL(), Slice(None, -1)});
}
// mass-field i-sum X[i] + X[i-1] -> {..., nx-1}
inline torch::Tensor si(const torch::Tensor& X) {
    return X.index({SL(), SL(), Slice(1, None)}) + X.index({SL(), SL(), Slice(None, -1)});
}
} // namespace

// advance_uv (module_small_step_em.F:654-871 for u; :873+ for v). em_b_wave: msfux/msfuy=1, cqu=1.
//   u += dts*ru_tend                                                             (:805)
//   dpxy = 0.5*rdx*(c1h*muu+c2h)*[ (ph(k+1)+ph(k) i-diff) + (alt+alt_im1)*di(p) + (al+al_im1)*di(pb=0 here) ]  (:828-831)
//   u -= dts*cqu*dpxy                                                            (:868, div-damping term TODO)
// NOTE (Inc 5 partial): pb i-difference term (base pressure) is dropped here (perturbation p only,
// base pb gradient is part of the slow PGF already frozen in ru_tend); non-hydro 4th term = TODO.
State advance_uv(const State& s, const Const& c) {
    State o = s;                                        // copy handles (functional, autograd-safe)
    const int nz = s.p.size(1);
    const int nx = s.p.size(2);                         // MASS-point count in x
    // --- u ---
    // frozen slow tendency (u & ru_tend both on u-points; shapes match)
    torch::Tensor u_new = s.u + c.dts * c.ru_tend;
    // hydrostatic PGF at the nx-1 INTERIOR u-points (u-point i, i=1..nx-1, uses mass i-1,i).
    // All operands below are {ny, nz, nx-1}; ph on full levels uses ph(k)+ph(k+1).
    auto ph_k   = s.ph.index({SL(), Slice(0, nz),   SL()});   // {ny,nz,nx}
    auto ph_kp1 = s.ph.index({SL(), Slice(1, nz+1), SL()});   // {ny,nz,nx}
    auto D_ph = di(ph_kp1) + di(ph_k);                        // {ny,nz,nx-1}
    // (c1h*muu+c2h) at interior u-points 1..nx-1: slice muu to nx-1 (valid whether muu is nx or nx+1 wide)
    auto muu_int = c.muu.index({SL(), Slice(1, nx)});         // {ny, nx-1}
    auto coef = c.c1h.view({1, nz, 1}) * muu_int.unsqueeze(1) + c.c2h.view({1, nz, 1}); // {ny,nz,nx-1}
    // Two dominant hydrostatic terms: geopotential gradient + alpha_full*d(p'). (Inc 3 validated the
    // discretization.) TODO(Inc 5): al'*d(pb) base-pressure term (needs pb in Const), the
    // non-hydrostatic 4th term (php/dpn), and divergence damping (emdiv).
    auto dpxy = 0.5f * c.rdx * coef * (D_ph + si(c.alt) * di(s.p));  // {ny,nz,nx-1}
    // subtract from interior u-points 1..nx-1 (indices 1..nx-1 == Slice(1,nx), exactly nx-1 columns);
    // boundary u-points (0 and, if staggered, nx) are set by the caller / periodic halo.
    auto u_int = u_new.index({SL(), SL(), Slice(1, nx)}) - c.dts * dpxy;   // cqu=1
    u_new.index_put_({SL(), SL(), Slice(1, nx)}, u_int);
    o.u = u_new;
    // --- v --- (frozen tendency only for now; symmetric PGF in y = TODO)
    o.v = s.v + c.dts * c.rv_tend;
    return o;
}

// TODO (Inc 5, next): mass continuity + theta (dvdxi flux divergence, DMDT, ww, muave, wdtn).
State advance_mu_t(const State& s, const Const& /*c*/) {
    return s;   // stub
}

// TODO (Inc 5, next): assemble RHS (off-centered vert-PGF :1318/:1409 + buoyancy) then Thomas
// solve (Inc 2b calc_coef_w a/alpha/gamma) then ph update (:1462).
State advance_w(const State& s, const Const& /*c*/) {
    return s;   // stub
}

// TODO (Inc 5, next): al' from geopotential gradient (:522-523), p' from linearized EOS (:527-528),
// divergence damping (smdiv, step>0).
State calc_p_rho(const State& s, const Const& /*c*/, int /*step*/) {
    return s;   // stub
}

State advance_substep(const State& s, const Const& c, int step) {
    State x = advance_uv(s, c);
    x = advance_mu_t(x, c);
    x = advance_w(x, c);
    x = calc_p_rho(x, c, step);
    return x;
}

}}} // namespace wrf::sdirk3::acoustic
