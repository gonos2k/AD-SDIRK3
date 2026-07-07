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
// mass-field j-difference / j-sum along ny (dim 0) -> {ny-1, ...} at interior v-points
inline torch::Tensor dj(const torch::Tensor& X) {
    return X.index({Slice(1, None), SL(), SL()}) - X.index({Slice(None, -1), SL(), SL()});
}
inline torch::Tensor sj(const torch::Tensor& X) {
    return X.index({Slice(1, None), SL(), SL()}) + X.index({Slice(None, -1), SL(), SL()});
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
    // Three hydrostatic terms (:828-831): geopotential gradient + alt*d(p') + al'*d(pb).
    // TODO(Inc 5): the non-hydrostatic 4th term (php/dpn) and divergence damping (emdiv).
    auto dpxy = 0.5f * c.rdx * coef * (D_ph + si(c.alt) * di(s.p) + si(s.al) * di(c.pb));  // {ny,nz,nx-1}
    // divergence damping (:809/:868): + c1h(k)*mudf_xy, mudf_xy=-emdiv*dx*(mudf(i)-mudf(i-1)) (msf=1).
    // Uses s.mudf from the PREVIOUS substep (advance_uv precedes advance_mu_t in the loop).
    float dx = c.rdx > 0.0f ? 1.0f / c.rdx : 0.0f;
    auto mudf_xy = (-c.emdiv * dx) * (s.mudf.index({SL(), Slice(1, nx)}) - s.mudf.index({SL(), Slice(0, nx - 1)})); // {ny,nx-1}
    // subtract PGF + add div damping at interior u-points 1..nx-1 (Slice(1,nx), nx-1 columns).
    auto u_int = u_new.index({SL(), SL(), Slice(1, nx)}) - c.dts * dpxy
                 + c.c1h.view({1, nz, 1}) * mudf_xy.unsqueeze(1);   // cqu=1
    u_new.index_put_({SL(), SL(), Slice(1, nx)}, u_int);
    o.u = u_new;
    // --- v --- symmetric hydrostatic PGF in y (v-point j uses mass j-1,j; j-difference along ny).
    const int ny = s.p.size(0);
    torch::Tensor v_new = s.v + c.dts * c.rv_tend;
    auto ph_k_v   = s.ph.index({SL(), Slice(0, nz),   SL()});
    auto ph_kp1_v = s.ph.index({SL(), Slice(1, nz+1), SL()});
    auto D_ph_v = dj(ph_kp1_v) + dj(ph_k_v);                  // {ny-1,nz,nx}
    auto muv_int = c.muv.index({Slice(1, ny), SL()});         // {ny-1,nx}
    auto coef_v = c.c1h.view({1, nz, 1}) * muv_int.unsqueeze(1) + c.c2h.view({1, nz, 1});
    auto dpxy_v = 0.5f * c.rdy * coef_v * (D_ph_v + sj(c.alt) * dj(s.p) + sj(s.al) * dj(c.pb)); // {ny-1,nz,nx}
    // v divergence damping (:880), symmetric to u: mudf_yv = -emdiv*dy*(mudf(j)-mudf(j-1)) (msf=1)
    float dy = c.rdy > 0.0f ? 1.0f / c.rdy : 0.0f;
    auto mudf_yv = (-c.emdiv * dy) * (s.mudf.index({Slice(1, ny), SL()}) - s.mudf.index({Slice(0, ny - 1), SL()})); // {ny-1,nx}
    auto v_int = v_new.index({Slice(1, ny), SL(), SL()}) - c.dts * dpxy_v
                 + c.c1h.view({1, nz, 1}) * mudf_yv.unsqueeze(1);  // cqv=1
    v_new.index_put_({Slice(1, ny), SL(), SL()}, v_int);
    o.v = v_new;
    // TODO(Inc 5): non-hydrostatic 4th term (php/dpn) and divergence damping (emdiv), both u & v.
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
    o.mudf = DMDT + c.mu_tend;                        // :1105 (divergence-damping filter source)
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

    // --- THETA update (:1138-1173, msf=1) ---
    // frozen theta tendency; then vertical + horizontal(x) flux advection.
    torch::Tensor t_new = s.t + dts * c.ft;
    // vertical theta flux wdtn(k) = ww(k)*(fnm(k)*t_1(k)+fnp(k)*t_1(k-1)); wdtn(0)=wdtn(top)=0.  (:1148-1154)
    std::vector<torch::Tensor> wdtn(nzw);
    wdtn[0]       = torch::zeros({ny, nx}, s.w.options());
    wdtn[nzw - 1] = torch::zeros({ny, nx}, s.w.options());
    for (int kf = 1; kf <= nzw - 2; ++kf) {
        auto tv = c.fnm[kf] * c.t_1.index({SL(), kf, SL()}) + c.fnp[kf] * c.t_1.index({SL(), kf - 1, SL()});
        wdtn[kf] = o.ww.index({SL(), kf, SL()}) * tv;
    }
    auto wdtn_t = torch::stack(wdtn, 1);                    // {ny,nz_w,nx}
    // vertical advection rdnw(k)*(wdtn(k+1)-wdtn(k)) at mass level k  (:1171)
    auto vert = c.rdnw.view({1, nz, 1}) * (wdtn_t.index({SL(), Slice(1, nz + 1), SL()}) - wdtn_t.index({SL(), Slice(0, nz), SL()}));
    t_new = t_new - dts * vert;
    // horizontal x theta-flux at INTERIOR mass points 1..nx-2 (boundary via halo = cross-cutting TODO,
    // same deferral as advance_uv PGF): Fx(iu)=u(iu)*(t_1(iu)+t_1(iu-1)); 0.5*rdx*(Fx(i+1)-Fx(i)).  (:1168-1170)
    auto Fx = s.u.index({SL(), SL(), Slice(1, nx)})
              * (c.t_1.index({SL(), SL(), Slice(1, nx)}) + c.t_1.index({SL(), SL(), Slice(0, nx - 1)})); // {ny,nz,nx-1}
    auto divx = 0.5f * c.rdx * (Fx.index({SL(), SL(), Slice(1, None)}) - Fx.index({SL(), SL(), Slice(None, -1)})); // {ny,nz,nx-2}
    auto t_i = t_new.index({SL(), SL(), Slice(1, nx - 1)}) - dts * divx;
    t_new.index_put_({SL(), SL(), Slice(1, nx - 1)}, t_i);
    // horizontal y theta-flux (:1165-1167), symmetric: Fy(jv)=v(jv)*(t_1(jv)+t_1(jv-1)); 0.5*rdy*(Fy(j+1)-Fy(j)).
    // divy is independent of t_new, so subtracting after divx is additive (WRF applies both to the same t).
    auto Fy = s.v.index({Slice(1, ny), SL(), SL()})
              * (c.t_1.index({Slice(1, ny), SL(), SL()}) + c.t_1.index({Slice(0, ny - 1), SL(), SL()})); // {ny-1,nz,nx}
    auto divy = 0.5f * c.rdy * (Fy.index({Slice(1, None), SL(), SL()}) - Fy.index({Slice(None, -1), SL(), SL()})); // {ny-2,nz,nx}
    auto t_iy = t_new.index({Slice(1, ny - 1), SL(), SL()}) - dts * divy;
    t_new.index_put_({Slice(1, ny - 1), SL(), SL()}, t_iy);
    o.t = t_new;
    // TODO(Inc 5): horizontal boundary halo (shared cross-cutting concern with advance_uv PGF).
    return o;
}

// advance_w (module_small_step_em.F:1178-1469). em_b_wave: msf=1, cqw=1, flat terrain (w[0]=0).
// Frozen rw_tend + t_2ave + off-centered vertical-PGF RHS + buoyancy/thermal term + Thomas solve
// (Inc 2b factors) + ph update.
// REMAINING before integration (Inc 5f): the phi-equation forcing added to rhs — ph_tend and the
// ww*d(phi) vertical advection (:1318/:1336) — and Rayleigh damping (:1445). The dominant coupling
// (vert-PGF + buoyancy + solve) is present; these are additive corrections.
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
    // rhs = ph + dts*(ph_tend + 0.5*g*(1-eps)*w) / mf_full   (:1318/:1368; ww*d(phi) advection = TODO, needs php)
    torch::Tensor rhs = s.ph + dts * (c.ph_tend + (0.5f * g * (1.0f - eps)) * s.w) / mf_full;
    rhs.index_put_({SL(), 0, SL()}, torch::zeros_like(rhs.index({SL(), 0, SL()})));
    // t_2ave (:1314-1317): running time-avg of coupled theta, normalized by full mass*temperature.
    // Uses advance_mu_t's muave/muts (per-substep, in State) + reference t_1, t0.
    auto mh_ts = c.c1h.view({1, nz, 1}) * s.muts.unsqueeze(1) + c.c2h.view({1, nz, 1});   // c1h*muts+c2h
    auto t2ave = 0.5f * ((1.0f + eps) * s.t + (1.0f - eps) * s.t_2ave);
    t2ave = (t2ave + c.c1h.view({1, nz, 1}) * s.muave.unsqueeze(1) * c.t0) / (mh_ts * (c.t0 + c.t_1));
    o.t_2ave = t2ave;
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
        // buoyancy/thermal term (:1414-1417): dts*g*(rdn(k)*(c2a*alt*t_2ave diff) - c1f(k)*muave)
        auto alt_k  = c.alt.index({SL(), kf,   SL()});   auto alt_km = c.alt.index({SL(), kf-1, SL()});
        auto t2a_k  = t2ave.index({SL(), kf,   SL()});   auto t2a_km = t2ave.index({SL(), kf-1, SL()});
        auto buoy = dts*g*(rdn_k*(c2a_k*alt_k*t2a_k - c2a_km*alt_km*t2a_km) - c.c1f[kf]*s.muave);
        // + dts*rw_tend (frozen slow w-tendency, :1405) — REQUIRED, mirrors ru_tend in advance_uv.
        auto val = s.w.index({SL(), kf, SL()}) + dts * c.rw_tend.index({SL(), kf, SL()})
                   + 0.5f*dts*g*rdn_k*(up - lo) + buoy;
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
        // top buoyancy (:1427-1429): -dts*g*(2*rdnw*c2a*alt*t_2ave + c1f(kde)*muave)
        auto alt_km = c.alt.index({SL(), kde-1, SL()});
        auto t2a_km = t2ave.index({SL(), kde-1, SL()});
        auto buoy_top = -dts*g*(2.0f*rdnw_km*c2a_km*alt_km*t2a_km + c.c1f[kde]*s.muave);
        auto val = s.w.index({SL(), kde, SL()}) + dts * c.rw_tend.index({SL(), kde, SL()})
                   - 0.5f*dts*g/mh_km*rdnw_km*rdnw_km*2.0f*c2a_km*grad + buoy_top;
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

// calc_p_rho (module_small_step_em.F:438-568, non-hydrostatic). Diagnoses the acoustic PERTURBATIONS
// al' (from the geopotential gradient) and p' (temporally-linearized EOS) — NOT the Inc 1 full-state
// EOS (uses alt/c2a as inputs but distinct formulas). Then divergence damping.
//   al'(k) = -1/mh * ( alt*(c1h*mu') + rdnw(k)*(ph(k+1)-ph(k)) )                         (:522-523)
//   p'(k)  = c2a * ( alt*(t' - (c1h*mu')*t_1) / (mh*(t0+t_1)) - al' )                    (:527-528)
//   div damping: step==0 -> pm1=p'; else p' += smdiv*(p'-pm1'), pm1'=old p'             (:549-567)
State calc_p_rho(const State& s, const Const& c, int step) {
    State o = s;
    const int nz = s.p.size(1);
    // WRF passes grid%muts (per-substep UPDATED full mass) as calc_p_rho's 'mut' arg (solve_em.F:1352),
    // NOT the base mut. Use s.muts (from advance_mu_t this substep), not c.mut, or the density is stale.
    auto mut2   = s.muts.unsqueeze(1);                                    // {ny,1,nx}
    auto mh     = c.c1h.view({1, nz, 1}) * mut2 + c.c2h.view({1, nz, 1}); // {ny,nz,nx}
    auto c1h_mu = c.c1h.view({1, nz, 1}) * s.mu.unsqueeze(1);             // {ny,nz,nx} = c1h*mu'
    // al' from the vertical geopotential-perturbation gradient
    auto dph = s.ph.index({SL(), Slice(1, nz + 1), SL()}) - s.ph.index({SL(), Slice(0, nz), SL()}); // {ny,nz,nx}
    auto al  = -1.0f / mh * (c.alt * c1h_mu + c.rdnw.view({1, nz, 1}) * dph);
    o.al = al;
    // p' from the temporally-linearized EOS (t' = coupled theta perturbation = s.t)
    auto p = c.c2a * (c.alt * (s.t - c1h_mu * c.t_1) / (mh * (c.t0 + c.t_1)) - al);
    // divergence damping (forward-weighted pressure filter)
    if (step == 0) {
        o.p = p;  o.pm1 = p;
    } else {
        o.p = p + c.smdiv * (p - s.pm1);  o.pm1 = p;
    }
    return o;
}

// advance_substep is the LOOP BODY for small_step = 1..N (solve_em.F:1525-1869). `small_step` is
// 1-BASED: every loop substep applies divergence damping (calc_p_rho step>=1). The pressure INIT
// (calc_p_rho with step=0, which sets pm1 and does NOT damp) is a SEPARATE pre-loop call the caller
// must run once before the first substep (solve_em.F:1352) — it is NOT part of advance_substep.
State small_step_prep(const PrepInput& in, Saves& saves) {
    // coef(c1,c2,mass) = c1(k)*mass + c2(k), broadcast to {ny, nlev, nx*}. Couple each field as
    // (coef_updated * X_1 - coef_base * X_2) [/ or * msf], mirroring module_small_step_em.F:238-279.
    auto coef = [](const torch::Tensor& c1, const torch::Tensor& c2, const torch::Tensor& mass) {
        int nl = c1.size(0);
        return c1.view({1, nl, 1}) * mass.unsqueeze(1) + c2.view({1, nl, 1});
    };
    State s;
    saves.u  = in.u_2;
    s.u  = (coef(in.c1h, in.c2h, in.muus) * in.u_1 - coef(in.c1h, in.c2h, in.muu) * in.u_2) / in.msfuy.unsqueeze(1);
    saves.v  = in.v_2;                              // note: v uses *msfvx_inv (a multiply), not a divide
    s.v  = (coef(in.c1h, in.c2h, in.muvs) * in.v_1 - coef(in.c1h, in.c2h, in.muv) * in.v_2) * in.msfvx_inv.unsqueeze(1);
    saves.t  = in.t_2;
    s.t  = coef(in.c1h, in.c2h, in.muts) * in.t_1 - coef(in.c1h, in.c2h, in.mut) * in.t_2;   // theta: no msf
    saves.w  = in.w_2;
    s.w  = (coef(in.c1f, in.c2f, in.muts) * in.w_1 - coef(in.c1f, in.c2f, in.mut) * in.w_2) / in.msfty.unsqueeze(1);
    saves.ph = in.ph_2;
    s.ph = in.ph_1 - in.ph_2;                       // geopotential perturbation
    saves.ww = in.ww;
    s.ww = in.ww;
    s.mu   = in.mu_2;                               // column-mass perturbation (already uncoupled)
    s.muts = in.muts;
    // diagnostics zero-init; caller runs calc_p_rho(step=0) to fill p/al/pm1 before the loop.
    s.p = torch::zeros_like(in.t_2); s.al = torch::zeros_like(in.t_2); s.pm1 = torch::zeros_like(in.t_2);
    s.t_2ave = torch::zeros_like(in.t_2); s.muave = torch::zeros_like(in.mu_2); s.mudf = torch::zeros_like(in.mu_2);
    return s;
}

State advance_substep(const State& s, const Const& c, int small_step) {
    TORCH_CHECK(small_step >= 1,
        "advance_substep: small_step must be 1-based (>=1); the step=0 pressure init is a separate "
        "pre-loop calc_p_rho call, not the loop body.");
    State x = advance_uv(s, c);
    x = advance_mu_t(x, c);
    x = advance_w(x, c);
    x = calc_p_rho(x, c, small_step);   // small_step>=1 => divergence damping
    return x;
}

}}} // namespace wrf::sdirk3::acoustic
