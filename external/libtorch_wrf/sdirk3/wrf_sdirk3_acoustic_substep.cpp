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

// advance_mu_t (module_small_step_em.F:969-1175). em_b_wave: msf=1. MASS CONTINUITY core here
// (produces mu'/muave/muts/ww for advance_w); THETA update (t/t_2ave) is the immediate next TODO.
//   dvdxi = rdx*d_i(uflux) + rdy*d_j(vflux),  uflux = u' + (c1h*muu+c2h)*u_1  (:1094-1098)
//   DMDT  = sum_k dnw(k)*dvdxi(k)                                              (:1099)
//   mu'  += dts*(DMDT+mu_tend); muts=mut+mu'; muave=.5((1+eps)mu'+(1-eps)mu'_old)  (:1104-1107)
//   ww(kk)= ww(kk-1) - dnw(kk-1)*(c1h*DMDT + dvdxi(kk-1) + c1h*mu_tend); ww-=ww_1  (:1112,:1119)
State advance_mu_t(const State& s, const Const& c) {
    State o = s;
    const int nz = s.p.size(1), nx = s.p.size(2), ny = s.p.size(0);
    const int nzw = s.w.size(1);
    const float dts = c.dts, eps = c.epssm;
    // coupled fluxes at u-/v-points (msf=1): uflux = u' + (c1h*muu+c2h)*u_1
    auto muu3 = c.muu.unsqueeze(1);                    // {ny,1,nx_u}
    auto uflux = s.u + (c.c1h.view({1, nz, 1}) * muu3 + c.c2h.view({1, nz, 1})) * c.u_1;   // {ny,nz,nx_u}
    auto muv3 = c.muv.unsqueeze(1);                    // {ny_v,1,nx}
    auto vflux = s.v + (c.c1h.view({1, nz, 1}) * muv3 + c.c2h.view({1, nz, 1})) * c.v_1;   // {ny_v,nz,nx}
    // horizontal divergence at mass points: rdx*(uflux[i+1]-uflux[i]) + rdy*(vflux[j+1]-vflux[j])
    auto div_x = c.rdx * (uflux.index({SL(), SL(), Slice(1, None)}) - uflux.index({SL(), SL(), Slice(None, -1)}));
    auto div_y = c.rdy * (vflux.index({Slice(1, None), SL(), SL()}) - vflux.index({Slice(None, -1), SL(), SL()}));
    auto dvdxi = div_x + div_y;                        // {ny,nz,nx}
    // column-integrated mass tendency DMDT = sum_k dnw(k)*dvdxi(k)  {ny,nx}
    auto DMDT = (c.dnw.view({1, nz, 1}) * dvdxi).sum(1);   // {ny,nx}
    // mu' update + muave + muts
    auto mu_old = s.mu;
    auto mu_new = s.mu + dts * (DMDT + c.mu_tend);
    o.mu   = mu_new;
    o.muts = c.mut + mu_new;
    o.muave = 0.5f * ((1.0f + eps) * mu_new + (1.0f - eps) * mu_old);
    // ww vertical integral (out-of-place cumulative): ww[kf] = ww[kf-1] - dnw[kf-1]*(c1h*DMDT + dvdxi[kf-1] + c1h*mu_tend)
    std::vector<torch::Tensor> ww(nzw);
    ww[0] = torch::zeros({ny, nx}, s.w.options());
    for (int kf = 1; kf <= nzw - 1; ++kf) {
        auto c1h_km = c.c1h[kf - 1];
        auto term = c1h_km * DMDT + dvdxi.index({SL(), kf - 1, SL()}) + c1h_km * c.mu_tend;
        ww[kf] = ww[kf - 1] - c.dnw[kf - 1] * term;
    }
    ww[nzw - 1] = torch::zeros({ny, nx}, s.w.options());   // top BC ww(kde)=0
    o.ww = torch::stack(ww, 1) - c.ww_1;                   // subtract large-timestep ww (:1119)
    // TODO(Inc 5c-theta): t_ave=t; t+=msfty*dts*ft; wdtn=ww*(fnm*t_1+fnp*t_1[k-1]); theta advection (:1141-1173).
    return o;
}

// advance_w (module_small_step_em.F:1178-1469). em_b_wave: msf=1, cqw=1, flat terrain (w[0]=0).
// The von-Neumann-validated CORE: frozen rw_tend + off-centered vertical-PGF RHS + Thomas solve
// (Inc 2b factors) + ph update.
// !!! INCOMPLETE — MUST NOT be integrated as correct until these REQUIRED terms are added (they
// depend on advance_mu_t's outputs, which do not exist yet): ph_tend + ww*d(phi) advection in rhs
// (:1318/:1336), and the buoyancy/thermal term dts*g*(rdn*(c2a*alt*t_2ave diff) - c1f*muave)
// (:1414-1417). Rayleigh damping (:1445) also TODO. Inc 5f wiring is gated until these land.
// Vertical indexing mirrors the validated calc_coef_w: full-level libtorch index kf; mass fields
// c2a[kf]/rdnw[kf], mh[kf]=c1h[kf]*mut+c2h[kf]; rhs/ph on full levels (nz_w=nz+1).
State advance_w(const State& s, const Const& c) {
    State o = s;
    const int nz  = s.p.size(1);          // mass levels
    const int nzw = s.w.size(1);          // full levels = nz+1
    const int kde = nzw - 1;              // top full index (0-based)
    auto mut2 = c.mut.unsqueeze(1);       // {ny,1,nx}  base column mass (fixed; rhs denom :1368)
    // ph-update denom (:1462) uses the muts UPDATED by advance_mu_t this substep -> s.muts, not c.muts.
    auto muts2 = s.muts.unsqueeze(1);
    // mf_full(k) = c1f(k)*mut + c2f(k)  {ny,nz_w,nx};  mh(k)=c1h(k)*mut+c2h(k) {ny,nz,nx}
    auto mf_full = c.c1f.view({1, nzw, 1}) * mut2 + c.c2f.view({1, nzw, 1});
    auto mf_full_ts = c.c1f.view({1, nzw, 1}) * muts2 + c.c2f.view({1, nzw, 1});
    auto mh = c.c1h.view({1, nz, 1}) * mut2 + c.c2h.view({1, nz, 1});      // {ny,nz,nx}
    const float eps = c.epssm, g = c.g, dts = c.dts;
    // rhs = ph + dts*0.5*g*(1-eps)*w / mf_full   (old-w half of the phi eqn; +ph_tend/ww = TODO)
    torch::Tensor rhs = s.ph + (dts * 0.5f * g * (1.0f - eps)) * s.w / mf_full;
    rhs.index_put_({SL(), 0, SL()}, torch::zeros_like(rhs.index({SL(), 0, SL()})));
    // w RHS (write-once index_put_): interior full levels kf=1..kde-1, then top kf=kde.
    torch::Tensor w_rhs = s.w.clone();
    for (int kf = 1; kf <= kde - 1; ++kf) {
        auto c2a_k  = c.c2a.index({SL(), kf,   SL()});
        auto c2a_km = c.c2a.index({SL(), kf-1, SL()});
        auto rdnw_k = c.rdnw[kf];   auto rdnw_km = c.rdnw[kf-1];   auto rdn_k = c.rdn[kf];
        auto mh_k   = mh.index({SL(), kf,   SL()});
        auto mh_km  = mh.index({SL(), kf-1, SL()});
        auto rhs_kp = rhs.index({SL(), kf+1, SL()}); auto rhs_k = rhs.index({SL(), kf, SL()}); auto rhs_km = rhs.index({SL(), kf-1, SL()});
        auto ph_kp  = s.ph.index({SL(), kf+1, SL()}); auto ph_k = s.ph.index({SL(), kf, SL()}); auto ph_km = s.ph.index({SL(), kf-1, SL()});
        auto up = c2a_k  * rdnw_k  / mh_k  * ((1.0f+eps)*(rhs_kp-rhs_k) + (1.0f-eps)*(ph_kp-ph_k));
        auto lo = c2a_km * rdnw_km / mh_km * ((1.0f+eps)*(rhs_k-rhs_km) + (1.0f-eps)*(ph_k-ph_km));
        // + dts*rw_tend (frozen slow w-tendency, :1405) — REQUIRED, mirrors ru_tend in advance_uv.
        auto val = s.w.index({SL(), kf, SL()}) + dts * c.rw_tend.index({SL(), kf, SL()})
                   + 0.5f*dts*g*rdn_k*(up - lo);
        w_rhs.index_put_({SL(), kf, SL()}, val);
    }
    // top kf=kde (WRF :1421-1429): -0.5*dts*g/mh(kde-1)*rdnw(kde-1)^2*2*c2a(kde-1)*(off-centered grad)
    {
        auto c2a_km = c.c2a.index({SL(), kde-1, SL()});
        auto mh_km  = mh.index({SL(), kde-1, SL()});
        auto rdnw_km = c.rdnw[kde-1];
        auto rhs_k = rhs.index({SL(), kde, SL()}); auto rhs_km = rhs.index({SL(), kde-1, SL()});
        auto ph_k  = s.ph.index({SL(), kde, SL()}); auto ph_km = s.ph.index({SL(), kde-1, SL()});
        auto grad = (1.0f+eps)*(rhs_k-rhs_km) + (1.0f-eps)*(ph_k-ph_km);
        auto val = s.w.index({SL(), kde, SL()}) + dts * c.rw_tend.index({SL(), kde, SL()})
                   - 0.5f*dts*g/mh_km*rdnw_km*rdnw_km*2.0f*c2a_km*grad;
        w_rhs.index_put_({SL(), kde, SL()}, val);
    }
    w_rhs.index_put_({SL(), 0, SL()}, torch::zeros_like(w_rhs.index({SL(), 0, SL()})));  // bottom BC
    // Thomas solve — OUT-OF-PLACE (Inc 2b lesson: in-place recurrence breaks backward).
    std::vector<torch::Tensor> w(nzw);
    for (int kf = 0; kf < nzw; ++kf) w[kf] = w_rhs.index({SL(), kf, SL()});
    for (int kf = 1; kf <= kde; ++kf)
        w[kf] = (w[kf] - c.a.index({SL(), kf, SL()}) * w[kf-1]) * c.alpha.index({SL(), kf, SL()});
    for (int kf = kde - 1; kf >= 1; --kf)
        w[kf] = w[kf] - c.gamma.index({SL(), kf, SL()}) * w[kf+1];
    w[0] = torch::zeros_like(w[0]);
    torch::Tensor w_sol = torch::stack(w, 1);           // {ny,nz_w,nx}
    o.w = w_sol;
    // ph update (:1462): ph(k) = rhs(k) + 0.5*dts*g*(1+eps)*w(k)/(c1f*muts+c2f)
    o.ph = rhs + (0.5f * dts * g * (1.0f + eps)) * w_sol / mf_full_ts;
    return o;
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
