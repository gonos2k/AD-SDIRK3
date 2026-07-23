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
#include "wrf_sdirk3_stage_history_diag.h"  // process-global line-atomic diagnostic emitter

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace wrf { namespace sdirk3 { namespace acoustic {

namespace {
using torch::indexing::Slice;
using torch::indexing::None;
inline torch::indexing::Slice SL() { return torch::indexing::Slice(); }

// PR (ph-instability ablation 2026-07-22): env-gated ablation of the advance_w buoyancy/
// thermal + mass-feedback term (c2a*alt*t_2ave - c1f*muave). Default OFF => byte-identical.
// The dt=600 ph-decomposition (PR #69) localized the split instability to a SLOW new_w
// (w-channel) growth; the isolated vertical w-phi was proven |lambda|=0.998 but buoyancy
// (theta->w) + mass feedback were never offline-provable. If zeroing this term stops the
// new_w growth, it is the destabilizer. Diagnostic experiment, split path only.
inline bool ablate_buoy_w_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WRF_SDIRK3_ABLATE_BUOY_W");
        return e != nullptr && e[0] == '1' && e[1] == '\0';
    }();
    return on;
}
inline bool advance_w_ph_diag_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WRF_SDIRK3_ADVANCE_W_PH_DIAG");
        return e != nullptr && e[0] == '1' && e[1] == '\0';
    }();
    return on;
}

inline uint64_t advance_w_ph_diag_sequence(int small_step) {
    static thread_local uint64_t sequence = 0;
    if (small_step == 1) ++sequence;
    return sequence;
}

struct PhStats {
    double max_abs = std::numeric_limits<double>::quiet_NaN();
    double rms = std::numeric_limits<double>::quiet_NaN();
};

PhStats active_ph_stats(const torch::Tensor& t) {
    if (!t.defined() || t.dim() != 3 || t.size(1) <= 1) return {};
    auto active = t.detach().index({SL(), Slice(1, None), SL()});
    if (active.numel() == 0) return {};
    auto a64 = active.to(torch::kFloat64);
    return {a64.abs().max().item<double>(), torch::sqrt(a64.square().mean()).item<double>()};
}

std::pair<double, double> active_ph_minmax(const torch::Tensor& t) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!t.defined() || t.dim() != 3 || t.size(1) <= 1) return {nan, nan};
    auto active = t.detach().index({SL(), Slice(1, None), SL()});
    if (active.numel() == 0) return {nan, nan};
    auto a64 = active.to(torch::kFloat64);
    return {a64.min().item<double>(), a64.max().item<double>()};
}

torch::Tensor zero_bottom_level(torch::Tensor t) {
    t = t.clone();
    t.index_put_({SL(), 0, SL()}, torch::zeros_like(t.index({SL(), 0, SL()})));
    return t;
}

struct AdvanceWPhBreakdown {
    torch::Tensor ph_tend_coupled;
    torch::Tensor ph_tend_physical;
    torch::Tensor previous;
    torch::Tensor frozen;
    torch::Tensor old_w;
    torch::Tensor vertical_advection;
    torch::Tensor new_w;
    torch::Tensor actual_delta;
    torch::Tensor sum_delta;
    torch::Tensor closure;
    torch::Tensor mass_denom_old;
    torch::Tensor mass_denom_new;
};

AdvanceWPhBreakdown build_advance_w_ph_breakdown(
    const State& before_w, const Const& c, const State& after_w) {
    torch::NoGradGuard no_grad;
    const int nz = before_w.p.size(1);
    const int nzw = before_w.ph.size(1);
    TORCH_CHECK(nzw == nz + 1,
                "advance_w ph diagnostic: expected nzw=nz+1, got ", nzw, " vs ", nz);

    auto mut2 = c.mut.detach().unsqueeze(1);
    auto muts2 = before_w.muts.detach().unsqueeze(1);
    auto mf_old = c.c1f.detach().view({1, nzw, 1}) * mut2
                + c.c2f.detach().view({1, nzw, 1});
    auto mf_new = c.c1f.detach().view({1, nzw, 1}) * muts2
                + c.c2f.detach().view({1, nzw, 1});
    TORCH_CHECK(mf_old.isfinite().all().item<bool>() && mf_new.isfinite().all().item<bool>(),
                "advance_w ph diagnostic: non-finite old/new mass denominator");
    TORCH_CHECK(mf_old.abs().min().item<double>() > 0.0 &&
                mf_new.abs().min().item<double>() > 0.0,
                "advance_w ph diagnostic: zero old/new mass denominator");

    auto previous = zero_bottom_level(before_w.ph.detach());
    auto coupled = zero_bottom_level(c.ph_tend.detach());
    auto physical = zero_bottom_level(c.ph_tend.detach() / mf_old);
    auto frozen = zero_bottom_level(c.dts * c.ph_tend.detach() / mf_old);
    auto old_w = zero_bottom_level(
        (0.5f * c.dts * c.g * (1.0f - c.epssm)) * before_w.w.detach() / mf_old);

    auto phf = c.ph_1.detach() + c.phb.detach();
    auto dphf = phf.index({SL(), Slice(1, nz + 1), SL()})
               - phf.index({SL(), Slice(0, nz), SL()});
    auto ww_avg = 0.5f * (before_w.ww.detach().index({SL(), Slice(1, nz + 1), SL()})
                         + before_w.ww.detach().index({SL(), Slice(0, nz), SL()}));
    auto wdwn_body = ww_avg * (-c.rdnw.detach().view({1, nz, 1})) * dphf;
    auto wd_kp1 = wdwn_body.index({SL(), Slice(1, nz), SL()});
    auto wd_k = wdwn_body.index({SL(), Slice(0, nz - 1), SL()});
    auto fnm_i = c.fnm.detach().slice(0, 1, nz).view({1, nz - 1, 1});
    auto fnp_i = c.fnp.detach().slice(0, 1, nz).view({1, nz - 1, 1});
    auto adv_active = c.dts * (fnm_i * wd_kp1 + fnp_i * wd_k);
    auto zero_lvl = torch::zeros_like(previous.index({SL(), Slice(0, 1), SL()}));
    auto adv_full = torch::cat({zero_lvl, adv_active, zero_lvl}, 1);
    auto vertical_advection = -adv_full / mf_old;

    auto new_w = zero_bottom_level(
        (0.5f * c.dts * c.g * (1.0f + c.epssm)) * after_w.w.detach() / mf_new);
    auto actual = zero_bottom_level(after_w.ph.detach());
    auto actual_delta = actual - previous;
    auto sum_delta = frozen + old_w + vertical_advection + new_w;
    auto reconstructed = previous + sum_delta;
    auto closure = actual - reconstructed;

    return {coupled, physical, previous, frozen, old_w, vertical_advection, new_w,
            actual_delta, sum_delta, closure, mf_old, mf_new};
}

void emit_advance_w_ph_breakdown(
    uint64_t sequence, int small_step, float dts, const AdvanceWPhBreakdown& b) {
    auto append = [](std::ostringstream& os, const char* name, const torch::Tensor& t) {
        const auto s = active_ph_stats(t);
        os << ' ' << name << "_max=" << s.max_abs << ' ' << name << "_rms=" << s.rms;
    };
    const auto old_mm = active_ph_minmax(b.mass_denom_old);
    const auto new_mm = active_ph_minmax(b.mass_denom_new);
    std::ostringstream os;
    os << std::setprecision(std::numeric_limits<double>::max_digits10)
       << "SDIRK3_ADVANCE_W_PH_DIAG"
       << " sequence=" << sequence
       << " small_step=" << small_step
       << " dts=" << dts;
    append(os, "coupled", b.ph_tend_coupled);
    append(os, "physical", b.ph_tend_physical);
    append(os, "previous", b.previous);
    append(os, "frozen", b.frozen);
    append(os, "old_w", b.old_w);
    append(os, "vertical_adv", b.vertical_advection);
    append(os, "new_w", b.new_w);
    append(os, "actual_delta", b.actual_delta);
    append(os, "sum_delta", b.sum_delta);
    append(os, "closure", b.closure);
    os << " mass_old_min=" << old_mm.first << " mass_old_max=" << old_mm.second
       << " mass_new_min=" << new_mm.first << " mass_new_max=" << new_mm.second;
    wrf::sdirk3::emit_sdirk3_diag_line(os.str() + "\n");
}

std::string single_line_diag_reason(const char* reason) {
    const std::string input = reason ? reason : "unknown";
    std::string output;
    output.reserve(input.size());
    for (const unsigned char ch : input) {
        switch (ch) {
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (ch < 0x20 || ch == 0x7f) {
                    output += '?';
                } else {
                    output.push_back(static_cast<char>(ch));
                }
        }
    }
    return output.empty() ? std::string("unknown") : output;
}

void emit_advance_w_ph_diag_invalid(
    uint64_t sequence, int small_step, float dts, const char* reason) {
    const std::string clean_reason = single_line_diag_reason(reason);
    std::ostringstream os;
    os << "SDIRK3_ADVANCE_W_PH_DIAG_INVALID"
       << " sequence=" << sequence
       << " small_step=" << small_step
       << " dts=" << dts
       << " reason=" << clean_reason;
    wrf::sdirk3::emit_sdirk3_diag_line(os.str() + "\n");
}

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

// phi-consistent p'/al diagnostics — see header. GEOMETRIC (hypso-1) form, kept DELIBERATELY:
// the hypso-2 in-model variant was measured to inject float32 diagnosis noise (p' is a small
// difference of large EOS terms; alpha rel-eps 1e-4 -> p' +-14 Pa) that does NOT reproduce WRF's
// own noise (bit-order differs), and it degraded the previously-EXACT ru/rv PGF parity. The
// smooth geometric alpha gives EXACT horizontal-PGF parity (measured ru 33.3752/33.3729,
// rv 82.0871 == dyn_em) because the vertical-profile component of any alpha error cancels in
// horizontal differences.
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> diag_p_al(
    const torch::Tensor& ph, const torch::Tensor& phb, const torch::Tensor& t,
    const torch::Tensor& pb, const torch::Tensor& muts, const torch::Tensor& mub,
    const torch::Tensor& c1h, const torch::Tensor& c2h, const torch::Tensor& rdnw,
    float p0, float rd, float cpovcv, float t0) {
    const int nz = t.size(1);
    auto dk = [&](const torch::Tensor& X) {   // full-level difference X(k+1)-X(k) -> mass levels
        return X.index({SL(), Slice(1, nz + 1), SL()}) - X.index({SL(), Slice(0, nz), SL()});
    };
    auto mh  = c1h.view({1, nz, 1}) * muts.unsqueeze(1) + c2h.view({1, nz, 1});
    auto mhb = c1h.view({1, nz, 1}) * mub.unsqueeze(1)  + c2h.view({1, nz, 1});
    // module metrics are |rdnw| (WRF's are negative): al = +|rdnw|*dphi/mh.
    auto al_full = rdnw.view({1, nz, 1}) * (dk(ph) + dk(phb)) / mh;
    auto alb     = rdnw.view({1, nz, 1}) * dk(phb) / mhb;
    auto eos = (rd * (t0 + t)) / (p0 * al_full);
    auto p_pert = p0 * torch::pow(eos.clamp_min(1e-20f), cpovcv) - pb;   // EOS guard, WRF :1069
    return {p_pert, al_full - alb, al_full};
}

// WRF calc_p_rho_phi hypsometric_opt=2 (module_big_step_utilities_em.F:1033-1052 — the WRF
// DEFAULT, and what em_b_wave runs): al = dphi_full / (phm * LOG(pfd/pfu)) with the dry
// hydrostatic column pressures pf = c3*MUTS + c4 + ptop (hybrid off: c3f=znw, c3h=znu, c4=0).
// This is the p whose DISCRETE vertical balance pg_buoy_w measures: WRF's residual at the
// balanced IC is ~22 coupled, hypso-1's geometric alpha leaves ~120 (MEASURED 2026-07-11).
// Used ONLY for pg_buoy_w's p; the horizontal PGF keeps hypso-1 (EXACT ru/rv parity — the
// vertical-profile alpha error cancels in horizontal differences, see diag_p_al note).
torch::Tensor diag_p_hypso2(
    const torch::Tensor& ph, const torch::Tensor& phb, const torch::Tensor& t,
    const torch::Tensor& pb, const torch::Tensor& muts,
    const torch::Tensor& znw, const torch::Tensor& znu, const torch::Tensor& ptop,
    float p0, float rd, float cpovcv, float t0) {
    const int nz = t.size(1);
    auto dk = [&](const torch::Tensor& X) {   // full-level difference X(k+1)-X(k) -> mass levels
        return X.index({SL(), Slice(1, nz + 1), SL()}) - X.index({SL(), Slice(0, nz), SL()});
    };
    auto muts3 = muts.unsqueeze(1);                                          // {ny,1,nx}
    auto ptop3 = ptop.unsqueeze(1);                                          // {ny,1,nx}
    auto pfu = znw.slice(0, 1, nz + 1).view({1, nz, 1}) * muts3 + ptop3;     // pf @ full k+1
    auto pfd = znw.slice(0, 0, nz).view({1, nz, 1}) * muts3 + ptop3;         // pf @ full k
    auto phm = znu.view({1, nz, 1}) * muts3 + ptop3;                         // p  @ mass k
    auto al_full = dk(ph + phb) / (phm * torch::log(pfd / pfu));             // dphi>0, pfd>pfu
    auto eos = (rd * (t0 + t)) / (p0 * al_full);
    return p0 * torch::pow(eos.clamp_min(1e-20f), cpovcv) - pb;              // p' (EOS, WRF :1069)
}

// WRF pg_buoy_w — see header. WRF rdn/rdnw are NEGATIVE; module metrics are |.| (negate odd uses).
torch::Tensor pg_buoy_w_stage(
    const torch::Tensor& p, const torch::Tensor& mu, const torch::Tensor& msfty,
    const torch::Tensor& c1f, const torch::Tensor& rdn, const torch::Tensor& rdnw, float g) {
    const int ny = p.size(0), nz = p.size(1), nx = p.size(2);
    const int nzw = nz + 1;
    auto msf_inv = torch::reciprocal(msfty).unsqueeze(1);                       // {ny,1,nx}
    auto mu3 = mu.unsqueeze(1);                                                 // {ny,1,nx}
    // interior full levels kf=1..nz-1: g/msf * ( -|rdn(kf)|*(p(kf)-p(kf-1)) - c1f(kf)*mu' )
    auto dp = p.index({SL(), Slice(1, nz), SL()}) - p.index({SL(), Slice(0, nz - 1), SL()});
    auto rdn_i = rdn.slice(0, 1, nz).view({1, nz - 1, 1});
    auto c1f_i = c1f.slice(0, 1, nz).view({1, nz - 1, 1});
    auto interior = g * msf_inv * (-rdn_i * dp - c1f_i * mu3);
    // top kf=nz: g/msf * ( +2*|rdnw(nz-1)|*p(nz-1) - c1f(nz)*mu' )   [2*rdnw_wrf*(-p), rdnw<0]
    auto top = g * msf_inv * (2.0f * rdnw[nz - 1] * p.index({SL(), Slice(nz - 1, nz), SL()})
                              - c1f[nzw - 1] * mu3);
    auto zero_lvl = torch::zeros({ny, 1, nx}, p.options());
    return torch::cat({zero_lvl, interior, top}, 1);                            // {ny,nz_w,nx}
}

// WRF curvature, w row — see header.
torch::Tensor curvature_w_stage(
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx_inv,
    const torch::Tensor& msftx, const torch::Tensor& msfty,
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp, float reradius) {
    const int ny = msftx.size(0), nx = msftx.size(1);
    const int nz = u.size(1), nzw = nz + 1;
    // coupled momenta on their native grids (couple_momentum): ru = (c1h*muu+c2h)*u/msfuy
    auto mh_u = c1h.view({1, nz, 1}) * muu.unsqueeze(1) + c2h.view({1, nz, 1});
    auto mh_v = c1h.view({1, nz, 1}) * muv.unsqueeze(1) + c2h.view({1, nz, 1});
    auto ru = mh_u * u / msfuy.unsqueeze(1);                                    // {ny,nz,nx_u}
    auto rv = mh_v * v * msfvx_inv.unsqueeze(1);                                // {ny_v,nz,nx}
    // horizontal pair-sums to mass points: (X(i)+X(i+1)) / (X(j)+X(j+1))
    auto pair_u = [&](const torch::Tensor& X) {
        return X.index({SL(), SL(), Slice(0, nx)}) + X.index({SL(), SL(), Slice(1, nx + 1)});
    };
    auto pair_v = [&](const torch::Tensor& X) {
        return X.index({Slice(0, ny), SL(), SL()}) + X.index({Slice(1, ny + 1), SL(), SL()});
    };
    // vertical interpolation to full levels kf=1..nz-1: 0.5*(fnm(kf)*S(kf) + fnp(kf)*S(kf-1))
    auto to_w = [&](const torch::Tensor& S) {
        auto fnm_i = fnm.slice(0, 1, nz).view({1, nz - 1, 1});
        auto fnp_i = fnp.slice(0, 1, nz).view({1, nz - 1, 1});
        return 0.5f * (fnm_i * S.index({SL(), Slice(1, nz), SL()})
                     + fnp_i * S.index({SL(), Slice(0, nz - 1), SL()}));        // {ny,nz-1,nx}
    };
    auto term = reradius * (to_w(pair_u(ru)) * to_w(pair_u(u))
                            + (msftx / msfty).unsqueeze(1)
                              * to_w(pair_v(rv)) * to_w(pair_v(v)));            // {ny,nz-1,nx}
    auto zero_lvl = torch::zeros({ny, 1, nx}, u.options());
    return torch::cat({zero_lvl, term, zero_lvl}, 1);                           // {ny,nz_w,nx}
}

// NOTE (review 9F.D3, fzm/fzp vs fnm/fnp): WRF's curvature/Coriolis w-terms use fzm/fzp, but those
// are DEFINED identically to fnm/fnp — fzm(k)=0.5·dnw(k-1)/dn(k)=fnm(k), fzp(k)=0.5·dnw(k)/dn(k)=fnp(k)
// (module_advect_em.F:10688-9 vs module_initialize_ideal.F:733-4). So passing fnm/fnp here is faithful,
// not a coincidence-on-idealized-grid; the two names are the same weights.
// WRF w-momentum CORIOLIS term (module_big_step_utilities_em.F:3843), companion to curvature_w_stage.
//   rw_tend += e*(cosa*avg_w(ru) - (msftx/msfty)*sina*avg_w(rv))
// where avg_w = 0.5*(fnm(kf)*pair(kf) + fnp(kf)*pair(kf-1)) of the horizontal pair-sum of the coupled
// momentum (ru=(c1h*muu+c2h)*u/msfuy on u-points, rv likewise on v-points). Linear in ru/rv (unlike
// curvature's ru*u). e/cosa/sina are 2D {ny,nx}.
torch::Tensor coriolis_w_stage(
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx_inv,
    const torch::Tensor& msftx, const torch::Tensor& msfty,
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp,
    const torch::Tensor& e, const torch::Tensor& cosa, const torch::Tensor& sina) {
    const int ny = msftx.size(0), nx = msftx.size(1);
    const int nz = u.size(1);
    auto mh_u = c1h.view({1, nz, 1}) * muu.unsqueeze(1) + c2h.view({1, nz, 1});
    auto mh_v = c1h.view({1, nz, 1}) * muv.unsqueeze(1) + c2h.view({1, nz, 1});
    auto ru = mh_u * u / msfuy.unsqueeze(1);                                    // {ny,nz,nx_u}
    auto rv = mh_v * v * msfvx_inv.unsqueeze(1);                                // {ny_v,nz,nx}
    auto pair_u = [&](const torch::Tensor& X) {
        return X.index({SL(), SL(), Slice(0, nx)}) + X.index({SL(), SL(), Slice(1, nx + 1)});
    };
    auto pair_v = [&](const torch::Tensor& X) {
        return X.index({Slice(0, ny), SL(), SL()}) + X.index({Slice(1, ny + 1), SL(), SL()});
    };
    auto to_w = [&](const torch::Tensor& S) {
        auto fnm_i = fnm.slice(0, 1, nz).view({1, nz - 1, 1});
        auto fnp_i = fnp.slice(0, 1, nz).view({1, nz - 1, 1});
        return 0.5f * (fnm_i * S.index({SL(), Slice(1, nz), SL()})
                     + fnp_i * S.index({SL(), Slice(0, nz - 1), SL()}));        // {ny,nz-1,nx}
    };
    auto e3    = e.unsqueeze(1);                                                // {ny,1,nx}
    auto cosa3 = cosa.unsqueeze(1);
    auto sina3 = sina.unsqueeze(1);
    auto ratio = (msftx / msfty).unsqueeze(1);
    auto term = e3 * (cosa3 * to_w(pair_u(ru)) - ratio * sina3 * to_w(pair_v(rv)));  // {ny,nz-1,nx}
    auto zero_lvl = torch::zeros({ny, 1, nx}, u.options());
    return torch::cat({zero_lvl, term, zero_lvl}, 1);                           // {ny,nz_w,nx}
}

// WRF rhs_ph — see header.
torch::Tensor rhs_ph_stage(
    const torch::Tensor& ph, const torch::Tensor& phb,
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& ww, const torch::Tensor& w,
    const torch::Tensor& mut, const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& msfty,
    const torch::Tensor& c1f, const torch::Tensor& c2f,
    const torch::Tensor& fnm, const torch::Tensor& fnp, const torch::Tensor& rdnw,
    float cfn, float cfn1, float rdx, float rdy, float g) {
    const int ny = mut.size(0), nx = mut.size(1);
    const int nz = u.size(1), nzw = nz + 1;
    auto msf_inv = torch::reciprocal(msfty).unsqueeze(1);                       // {ny,1,nx}
    auto phf = ph + phb;                                                        // {ny,nz_w,nx}
    torch::Tensor pht = torch::zeros({ny, nzw, nx}, ph.options());

    // --- vertical advection (phi_adv_z default ELSE branch :1481-1497):
    //     wdwn0[m] (m=0..nz-1) == WRF wdwn(m+2): .5*(ww(m+2)+ww(m+1))*rdnw(m+1)*dphf   [1-based]
    // AUDIT FIX (2026-07-11): row kf (WRF k=kf+1) takes fnm(k)*wdwn(k+1)+fnp(k)*wdwn(k)
    //   == fnm0[kf]*wdwn0[kf] + fnp0[kf]*wdwn0[kf-1], rows kf=1..nz-1 (WRF k=2..kte-1, kte==kde).
    //   Previous code paired wdwn0[kf+1]/wdwn0[kf] (one level high) and dropped row nz-1 —
    //   the same array/pairing advance_w (:443) already had right.
    {
        auto dphf = phf.index({SL(), Slice(1, nz + 1), SL()}) - phf.index({SL(), Slice(0, nz), SL()});
        auto ww_avg = 0.5f * (ww.index({SL(), Slice(1, nz + 1), SL()}) + ww.index({SL(), Slice(0, nz), SL()}));
        auto wdwn = ww_avg * (-rdnw.view({1, nz, 1})) * dphf;   // wdwn0[m] == WRF wdwn(m+2), {ny,nz,nx}
        auto wd_k   = wdwn.index({SL(), Slice(1, nz), SL()});                   // wdwn0[kf]   = WRF wdwn(k+1)
        auto wd_km1 = wdwn.index({SL(), Slice(0, nz - 1), SL()});               // wdwn0[kf-1] = WRF wdwn(k)
        auto fnm_i = fnm.slice(0, 1, nz).view({1, nz - 1, 1});
        auto fnp_i = fnp.slice(0, 1, nz).view({1, nz - 1, 1});
        pht.index_put_({SL(), Slice(1, nz), SL()}, -(fnm_i * wd_k + fnp_i * wd_km1));
    }
    // --- mu g w (:1500-1516): ph_t(kf=1..nz) += (c1f*mut+c2f)*g*w/msfty
    // AUDIT FIX (2026-07-11): WRF resets ph_tend(kde)=0 then adds k=2..kte with kte==kde, so the
    // TOP full level kf=nz receives the term (previous Slice(1,nz) dropped it).
    {
        auto c1f_i = c1f.slice(0, 1, nzw).view({1, nz, 1});
        auto c2f_i = c2f.slice(0, 1, nzw).view({1, nz, 1});
        auto term = (c1f_i * mut.unsqueeze(1) + c2f_i) * g
                    * w.index({SL(), Slice(1, nzw), SL()}) * msf_inv;
        pht.index_put_({SL(), Slice(1, nzw), SL()},
                       pht.index({SL(), Slice(1, nzw), SL()}) + term);
    }

    // --- horizontal advection (6th-order centered, :1774-1819; em_b_wave: no open/specified,
    //     so the full range applies; msfvy/msfux == 1 idealized) ---
    // ghost-extended phf: x periodic (3 cols each side), y symmetric about the walls
    // (module_bc.F: ys ghost(-m)=row(m-1); ye ghost(nyt-1+m)=row(nyt-m)).
    auto phf_x = torch::cat({phf.index({SL(), SL(), Slice(nx - 3, nx)}), phf,
                             phf.index({SL(), SL(), Slice(0, 3)})}, 2);          // {ny,nzw,nx+6}
    auto phf_y = torch::cat({phf.index({Slice(0, 3), SL(), SL()}).flip(0), phf,
                             phf.index({Slice(ny - 3, ny), SL(), SL()}).flip(0)}, 0); // {ny+6,nzw,nx}
    auto stencil6 = [&](const torch::Tensor& Fe, int dim) {
        // (1/60)*(45*(F(+1)-F(-1)) - 9*(F(+2)-F(-2)) + (F(+3)-F(-3))) at the true points;
        // Fe is the +-3 ghost-extended field along `dim`.
        auto n = (dim == 2) ? nx : ny;
        auto sl = [&](int off) {
            return (dim == 2) ? Fe.index({SL(), SL(), Slice(3 + off, 3 + off + n)})
                              : Fe.index({Slice(3 + off, 3 + off + n), SL(), SL()});
        };
        return (1.0f / 60.0f) * (45.0f * (sl(1) - sl(-1)) - 9.0f * (sl(2) - sl(-2)) + (sl(3) - sl(-3)));
    };
    auto d6x = stencil6(phf_x, 2);                                              // {ny,nzw,nx}
    auto d6y = stencil6(phf_y, 0);                                              // {ny,nzw,nx}

    // velocity factor at full level kf: interior kf=1..nz-1 (WRF k=2..kte-1) uses the pair
    // vel(kf)+vel(kf-1); TOP kf=nz (WRF k=kte==kde) uses 2*(cfn*vel(nz-1)+cfn1*vel(nz-2)) —
    // the 2x folds WRF's 0.5 top prefactor into the shared 0.25 (:1791 vs :1807).
    // AUDIT FIX (2026-07-11): previous code placed the top form at kf=nz-1 and zeroed kf=nz
    // (one level low, and the cfn pair was vel(nz-2)/vel(nz-3)).
    auto vfac = [&](const torch::Tensor& vel) {                                  // {*,nz,*} -> {*,nz_w,*}
        auto inter = vel.index({SL(), Slice(1, nz), SL()}) + vel.index({SL(), Slice(0, nz - 1), SL()});
        auto lvl0 = torch::zeros_like(vel.index({SL(), Slice(0, 1), SL()}));
        auto top = 2.0f * (cfn * vel.index({SL(), Slice(nz - 1, nz), SL()})
                           + cfn1 * vel.index({SL(), Slice(nz - 2, nz - 1), SL()}));
        // levels: 0 (unused, zero), 1..nz-1 interior pairs, nz top form
        return torch::cat({lvl0, inter, top}, 1);
    };
    // x: mass col i multiplied by u-point factors i and i+1 (muuf = c1f*muu+c2f at u-points)
    {
        auto uf = vfac(u);                                                       // {ny,nz_w,nx_u}
        auto muu3 = muu.unsqueeze(1);                                            // {ny,1,nx_u}
        auto mf_u = c1f.view({1, nzw, 1}) * muu3 + c2f.view({1, nzw, 1});        // {ny,nz_w,nx_u}
        auto flux_u = mf_u * uf;                                                 // {ny,nz_w,nx_u}
        auto fac_x = flux_u.index({SL(), SL(), Slice(1, nx + 1)})
                   + flux_u.index({SL(), SL(), Slice(0, nx)});                   // {ny,nz_w,nx}
        pht = pht - (0.25f * rdx) * msf_inv * fac_x * d6x;
    }
    // y: mass row j multiplied by v-point factors j and j+1
    {
        auto vf = vfac(v);                                                       // {ny_v,nz_w,nx}
        auto muv3 = muv.unsqueeze(1);
        auto mf_v = c1f.view({1, nzw, 1}) * muv3 + c2f.view({1, nzw, 1});
        auto flux_v = mf_v * vf;                                                 // {ny_v,nz_w,nx}
        auto fac_y = flux_v.index({Slice(1, ny + 1), SL(), SL()})
                   + flux_v.index({Slice(0, ny), SL(), SL()});                   // {ny,nz_w,nx}
        pht = pht - (0.25f * rdy) * msf_inv * fac_y * d6y;
    }
    // Level 0: WRF loops start at k=2 (0-based 1) -> zero it explicitly.
    // AUDIT FIX (2026-07-11): do NOT zero the top level kf=nz — WRF's :1504 reset happens BEFORE
    // the mu-g-w add (:1507-1510, k up to kte==kde) and the horizontal top row (:1807 k=kte), so
    // the final ph_tend(kde) = mu-g-w + horizontal-top-row content (previous code wiped it).
    pht.index_put_({SL(), 0, SL()}, torch::zeros({ny, nx}, ph.options()));
    return pht;
}

// Coupled 4-term horizontal PGF — see header. Mirrors WRF's formula exactly:
//   dpx = 0.5*rdx*(c1h*muu+c2h)*[ d_i(ph(k+1))+d_i(ph(k)) + (alt+alt_im1)*d_i(p) + (al+al_im1)*d_i(pb) ]
//       + rdx*d_i(php)*( rdnw*(dpn(k+1)-dpn(k)) - 0.5*c1h*(mu+mu_im1) )      [non-hydro 4th term]
// (module_big_step_utilities_em.F:2385-2396 / module_small_step_em.F:828-865; v symmetric in y).
// dpn = p at full levels (surface cf1/cf2/cf3, interior fnm/fnp, top 0 lid-off); php = mass-level
// average of the full geopotential phf (calc_php, module_big_step_utilities_em.F:1265).
std::pair<torch::Tensor, torch::Tensor> horizontal_pgf(
    const torch::Tensor& ph, const torch::Tensor& p, const torch::Tensor& al,
    const torch::Tensor& alt, const torch::Tensor& pb, const torch::Tensor& phf,
    const torch::Tensor& mu, const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp, const torch::Tensor& rdnw,
    float rdx, float rdy, float cf1, float cf2, float cf3) {
    const int ny = p.size(0), nz = p.size(1), nx = p.size(2);
    const int nzw = ph.size(1), kde = nzw - 1;
    // wrapped x-difference/sum at u-points 0..nx-1: X[i] - X[i-1 mod nx] (periodic ghost)
    auto wrap_d = [](const torch::Tensor& X) { return X - torch::roll(X, 1, 2); };
    auto wrap_s = [](const torch::Tensor& X) { return X + torch::roll(X, 1, 2); };
    // dpn assembly shared by the u (wrapped) and v (j-diff) branches; psum = (p+p_neighbor).
    auto dpn_diff = [&](const torch::Tensor& psum) {
        std::vector<torch::Tensor> dpn(nzw);
        dpn[0] = 0.5f * (cf1 * psum.index({SL(), 0, SL()})
                       + cf2 * psum.index({SL(), 1, SL()})
                       + cf3 * psum.index({SL(), 2, SL()}));
        dpn[kde] = torch::zeros_like(dpn[0]);              // top_lid off; lid branch not ported
        for (int kf = 1; kf <= kde - 1; ++kf) {
            dpn[kf] = 0.5f * (fnm[kf] * psum.index({SL(), kf,     SL()})
                            + fnp[kf] * psum.index({SL(), kf - 1, SL()}));
        }
        auto dpn_t = torch::stack(dpn, 1);                 // {*,nz_w,nx}
        return dpn_t.index({SL(), Slice(1, nz + 1), SL()}) - dpn_t.index({SL(), Slice(0, nz), SL()});
    };
    auto php = 0.5f * (phf.index({SL(), Slice(0, nz), SL()}) + phf.index({SL(), Slice(1, nz + 1), SL()}));
    auto ph_k   = ph.index({SL(), Slice(0, nz),     SL()});
    auto ph_kp1 = ph.index({SL(), Slice(1, nz + 1), SL()});
    // --- u: all u-points 0..nx-1 (periodic wrap), then alias the seam column u[nx]=u[0] ---
    auto muu_u = muu.index({SL(), Slice(0, nx)});
    auto coef_u = c1h.view({1, nz, 1}) * muu_u.unsqueeze(1) + c2h.view({1, nz, 1});
    auto dpx = 0.5f * rdx * coef_u
               * (wrap_d(ph_kp1) + wrap_d(ph_k) + wrap_s(alt) * wrap_d(p) + wrap_s(al) * wrap_d(pb));
    // WRF's rdnw is NEGATIVE; the module receives positive |rdnw| -> negate the odd-power use.
    dpx = dpx + rdx * wrap_d(php)
          * (-rdnw.view({1, nz, 1}) * dpn_diff(wrap_s(p))
             - 0.5f * c1h.view({1, nz, 1}) * wrap_s(mu.unsqueeze(1)));
    dpx = torch::cat({dpx, dpx.index({SL(), SL(), Slice(0, 1)})}, 2);        // {ny,nz,nx+1}
    // --- v: interior v rows 1..ny-1 only (symmetric walls excluded, :790-797); wall rows ZERO ---
    auto muv_int = muv.index({Slice(1, ny), SL()});
    auto coef_v = c1h.view({1, nz, 1}) * muv_int.unsqueeze(1) + c2h.view({1, nz, 1});
    auto dpy_int = 0.5f * rdy * coef_v
                   * (dj(ph_kp1) + dj(ph_k) + sj(alt) * dj(p) + sj(al) * dj(pb));
    dpy_int = dpy_int + rdy * dj(php)
              * (-rdnw.view({1, nz, 1}) * dpn_diff(sj(p))
                 - 0.5f * c1h.view({1, nz, 1}) * sj(mu.unsqueeze(1)));
    auto zero_row = torch::zeros({1, nz, nx}, p.options());
    auto dpy = torch::cat({zero_row, dpy_int, zero_row}, 0);                 // {ny+1,nz,nx}
    return {dpx, dpy};
}

// advance_uv (module_small_step_em.F:654-871 for u; :873-946 for v). em_b_wave: msf*=1, cqu=cqv=1
// (dry: qv==0), periodic x, SYMMETRIC y walls.
//   u += dts*ru_tend                                                             (:805)
//   u -= dts*cqu*dpxy + c1h*mudf_xy   (dpxy = shared horizontal_pgf on the DEVIATIONS, :828-865)
// PERIODIC X (WRF: i_endu=ite, ghosts via PERIOD_BDY_EM_B3 + set_physical_bc even on 1 rank):
// BOTH seam u columns are advanced every substep; the seam invariant u[nx]==u[0] is enforced.
State advance_uv(const State& s, const Const& c) {
    TORCH_CHECK(c.ph_1.defined() && c.phb.defined(),
                "advance_uv: Const.ph_1/phb (save + base geopotential) must be wired for the "
                "non-hydrostatic 4th PGF term (module_small_step_em.F:834-865)");
    State o = s;                                        // copy handles (functional, autograd-safe)
    const int nz = s.p.size(1);
    const int nx = s.p.size(2);                         // MASS-point count in x
    const int ny = s.p.size(0);
    auto [dpx, dpy] = horizontal_pgf(s.ph, s.p, s.al, c.alt, c.pb, c.ph_1 + c.phb,
                                     s.mu, c.muu, c.muv, c.c1h, c.c2h, c.fnm, c.fnp,
                                     c.rdnw, c.rdx, c.rdy, c.cf1, c.cf2, c.cf3);
    // --- u: frozen tendency at ALL u-points (:805) - PGF + divergence damping (:809/:868),
    // mudf seam-wrapped (mudf(-1)=mudf(nx-1)); s.mudf is from the PREVIOUS substep.
    torch::Tensor u_new = s.u + c.dts * c.ru_tend;
    float dx = c.rdx > 0.0f ? 1.0f / c.rdx : 0.0f;
    auto mudf_xy = (-c.emdiv * dx) * (s.mudf - torch::roll(s.mudf, 1, 1));   // {ny,nx} @ u 0..nx-1
    auto u_upd = u_new.index({SL(), SL(), Slice(0, nx)})
                 - c.dts * dpx.index({SL(), SL(), Slice(0, nx)})
                 + c.c1h.view({1, nz, 1}) * mudf_xy.unsqueeze(1);   // cqu=1
    // WRF seam invariant u(ide)==u(ids) (PERIOD comm): alias the last u column from column 0. This
    // also makes any residual seam inconsistency in ru_tend non-accumulating.
    o.u = torch::cat({u_upd, u_upd.index({SL(), SL(), Slice(0, 1)})}, 2);   // {ny,nz,nx+1}
    // --- v: SYMMETRIC y walls (:790-797) — WRF advances ONLY interior v rows j=1..ny-1; the wall
    // rows get NEITHER the tendency NOR the PGF; they stay frozen at their prep values.
    torch::Tensor v_new = s.v.clone();
    float dy = c.rdy > 0.0f ? 1.0f / c.rdy : 0.0f;
    auto mudf_yv = (-c.emdiv * dy) * (s.mudf.index({Slice(1, ny), SL()}) - s.mudf.index({Slice(0, ny - 1), SL()})); // {ny-1,nx}
    v_new.index_put_({Slice(1, ny), SL(), SL()},
                     s.v.index({Slice(1, ny), SL(), SL()})
                     + c.dts * c.rv_tend.index({Slice(1, ny), SL(), SL()})
                     - c.dts * dpy.index({Slice(1, ny), SL(), SL()})
                     + c.c1h.view({1, nz, 1}) * mudf_yv.unsqueeze(1));  // cqv=1
    o.v = v_new;
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
    // Save the RAW coupled theta'' BEFORE the update (:1141): this is the (1-epssm) old-time arm
    // that advance_w's t_2ave off-centering reads THIS substep (WRF shares one t_2save array).
    o.t_ave = s.t;
    // frozen theta tendency; then vertical + horizontal flux advection.
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
    // WRF rdnw is NEGATIVE; module metrics are |rdnw| -> negate this odd-power use (:1171).
    auto vert = -c.rdnw.view({1, nz, 1}) * (wdtn_t.index({SL(), Slice(1, nz + 1), SL()}) - wdtn_t.index({SL(), Slice(0, nz), SL()}));
    t_new = t_new - dts * vert;
    // horizontal x theta-flux at ALL mass columns (WRF :1048-1049 i_start=its, i_end=ide-1; periodic
    // ghosts t_1(-1)=t_1(nx-1) via PERIOD_BDY_EM_B): Fx(iu)=u(iu)*(t_1(iu)+t_1(iu-1)) at u-points
    // 0..nx-1 with the wrapped t_1 pair; Fx at u-point nx equals Fx[0] (u seam alias + same wrap pair).
    auto Fx = s.u.index({SL(), SL(), Slice(0, nx)})
              * (c.t_1 + torch::roll(c.t_1, 1, 2));                          // {ny,nz,nx}
    auto divx = 0.5f * c.rdx * (torch::roll(Fx, -1, 2) - Fx);                // Fx(i+1)-Fx(i), Fx(nx)=Fx(0)
    t_new = t_new - dts * divx;
    // horizontal y theta-flux at ALL mass rows (WRF :1050-1051 j_start=jts, j_end=jde-1 — no symmetric
    // exclusion for theta): Fy(jv)=v(jv)*(t_1(jv)+t_1(jv-1)) at v-points 0..ny. Wall fluxes use the
    // symmetric ghost t_1(-1)=t_1(1) at the bottom; the top ghost is multiplied by wall v (frozen ~0
    // deviation), so its value is inert — use t_1(ny-1) doubled for definiteness (matches WRF, whose
    // symmetric_ye BC never writes the t_1(jde) ghost either).
    auto Fy_int = s.v.index({Slice(1, ny), SL(), SL()})
                  * (c.t_1.index({Slice(1, ny), SL(), SL()}) + c.t_1.index({Slice(0, ny - 1), SL(), SL()})); // {ny-1,nz,nx}
    auto Fy_bot = s.v.index({Slice(0, 1), SL(), SL()})
                  * (c.t_1.index({Slice(0, 1), SL(), SL()}) + c.t_1.index({Slice(1, 2), SL(), SL()}));       // {1,nz,nx}
    auto Fy_top = s.v.index({Slice(ny, ny + 1), SL(), SL()})
                  * (2.0f * c.t_1.index({Slice(ny - 1, ny), SL(), SL()}));                                   // {1,nz,nx}
    auto Fy = torch::cat({Fy_bot, Fy_int, Fy_top}, 0);                       // {ny+1,nz,nx} @ v-points 0..ny
    auto divy = 0.5f * c.rdy * (Fy.index({Slice(1, ny + 1), SL(), SL()}) - Fy.index({Slice(0, ny), SL(), SL()})); // {ny,nz,nx}
    t_new = t_new - dts * divy;
    o.t = t_new;
    return o;
}

// advance_w (module_small_step_em.F:1178-1469). em_b_wave: msf=1, cqw=1, flat terrain (w[0]=0),
// damp_opt=0 (the :1445 damp_opt==3 branch is inert and not ported).
// Frozen rw_tend + t_2ave + ww*d(phi) advection + off-centered vertical-PGF RHS + buoyancy/thermal
// term + Thomas solve (Inc 2b factors) + ph update.
// Vertical indexing mirrors the validated calc_coef_w: full-level libtorch index kf; mass fields
// c2a[kf]/rdnw[kf], mh[kf]=c1h[kf]*mut+c2h[kf]; rhs/ph on full levels (nz_w=nz+1).
State advance_w(const State& s, const Const& c) {
    TORCH_CHECK(c.ph_1.defined() && c.phb.defined(),
                "advance_w: Const.ph_1/phb (save + base geopotential) must be wired for the "
                "ww*d(phi) advection term (module_small_step_em.F:1340-1356)");
    TORCH_CHECK(s.t_ave.defined(),
                "advance_w: State.t_ave (advance_mu_t pre-update coupled theta) must be set — "
                "it is the (1-epssm) old-time arm of t_2ave (module_small_step_em.F:1141/:1314)");
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
    // rhs pass 1 (:1318): rhs_acc = dts*(ph_tend + 0.5*g*(1-eps)*w) on full levels.
    torch::Tensor rhs_acc = dts * (c.ph_tend + (0.5f * g * (1.0f - eps)) * s.w);
    // ww*d(phi)/dnu advection (:1340-1356, phi_adv_z default ELSE branch): destaggered omega times
    // the FULL reference geopotential gradient (save + base), restaggered with fnm/fnp.
    //   wdwn(K) = 0.5*(ww(K)+ww(K-1))*rdnw(K-1)*(phf(K)-phf(K-1))       [1-based K=2..kde]
    //   rhs(K) -= dts*(fnm(K)*wdwn(K+1) + fnp(K)*wdwn(K))               [1-based K=2..k_end]
    {
        auto phf  = c.ph_1 + c.phb;                                             // {ny,nz_w,nx}
        auto dphf = phf.index({SL(), Slice(1, nz + 1), SL()}) - phf.index({SL(), Slice(0, nz), SL()}); // {ny,nz,nx}
        auto ww_avg = 0.5f * (s.ww.index({SL(), Slice(1, nz + 1), SL()})
                            + s.ww.index({SL(), Slice(0, nz), SL()}));          // {ny,nz,nx}
        // wdwn_body[m] == wdwn at 0-based full level m+1 (m = 0..nz-1)
        auto wdwn_body = ww_avg * (-c.rdnw.view({1, nz, 1})) * dphf;   // WRF rdnw<0; ours |rdnw|
        // 0-based kf=1..nz-1: adv[kf] = dts*(fnm[kf]*wdwn0[kf+1] + fnp[kf]*wdwn0[kf])
        auto wd_kp1 = wdwn_body.index({SL(), Slice(1, nz), SL()});              // wdwn0[kf+1]
        auto wd_k   = wdwn_body.index({SL(), Slice(0, nz - 1), SL()});          // wdwn0[kf]
        auto fnm_i = c.fnm.slice(0, 1, nz).view({1, nz - 1, 1});
        auto fnp_i = c.fnp.slice(0, 1, nz).view({1, nz - 1, 1});
        auto adv = dts * (fnm_i * wd_kp1 + fnp_i * wd_k);                       // {ny,nz-1,nx} @ kf=1..nz-1
        auto zero_lvl = torch::zeros_like(rhs_acc.index({SL(), Slice(0, 1), SL()}));
        rhs_acc = rhs_acc - torch::cat({zero_lvl, adv, zero_lvl}, 1);           // kf=0 and kf=kde untouched
    }
    // rhs pass 2 (:1366-1368): rhs = ph + msfty*rhs_acc/(c1f*mut+c2f)  (msfty=1 idealized).
    torch::Tensor rhs = s.ph + rhs_acc / mf_full;
    rhs.index_put_({SL(), 0, SL()}, torch::zeros_like(rhs.index({SL(), 0, SL()})));
    // t_2ave (:1314-1317): off-centered average of the CURRENT coupled theta'' (s.t) and the RAW
    // pre-update coupled theta'' saved by advance_mu_t THIS substep (s.t_ave, WRF's shared t_2save
    // array — NOT the previous substep's normalized output), then normalized by full mass*temperature.
    auto mh_ts = c.c1h.view({1, nz, 1}) * s.muts.unsqueeze(1) + c.c2h.view({1, nz, 1});   // c1h*muts+c2h
    auto t2ave = 0.5f * ((1.0f + eps) * s.t + (1.0f - eps) * s.t_ave);
    t2ave = (t2ave + c.c1h.view({1, nz, 1}) * s.muave.unsqueeze(1) * c.t0) / (mh_ts * (c.t0 + c.t_1));
    o.t_2ave = t2ave;
    // w RHS (write-once index_put_): interior full levels kf=1..kde-1, then top kf=kde.
    torch::Tensor w_rhs = s.w.clone();
    for (int kf = 1; kf <= kde - 1; ++kf) {
        auto c2a_k  = c.c2a.index({SL(), kf,   SL()});
        auto c2a_km = c.c2a.index({SL(), kf-1, SL()});
        // WRF-signed metrics (WRF rdn/rdnw < 0; module stores |.|): the up/lo * rdn products are
        // sign-invariant (even), but the buoyancy's lone rdn (:1414) is odd — use WRF signs so
        // every formula below is verbatim module_small_step_em.F.
        auto rdnw_k = -c.rdnw[kf];  auto rdnw_km = -c.rdnw[kf-1];  auto rdn_k = -c.rdn[kf];
        auto mh_k   = mh.index({SL(), kf,   SL()});
        auto mh_km  = mh.index({SL(), kf-1, SL()});
        auto rhs_kp = rhs.index({SL(), kf+1, SL()}); auto rhs_k = rhs.index({SL(), kf, SL()}); auto rhs_km = rhs.index({SL(), kf-1, SL()});
        auto ph_kp  = s.ph.index({SL(), kf+1, SL()}); auto ph_k = s.ph.index({SL(), kf, SL()}); auto ph_km = s.ph.index({SL(), kf-1, SL()});
        auto up = c2a_k  * rdnw_k  / mh_k  * ((1.0f+eps)*(rhs_kp-rhs_k) + (1.0f-eps)*(ph_kp-ph_k));
        auto lo = c2a_km * rdnw_km / mh_km * ((1.0f+eps)*(rhs_k-rhs_km) + (1.0f-eps)*(ph_k-ph_km));
        // buoyancy/thermal term (:1414-1417): dts*g*(rdn(k)*(c2a*alt*t_2ave diff) - c1f(k)*muave)
        auto alt_k  = c.alt.index({SL(), kf,   SL()});   auto alt_km = c.alt.index({SL(), kf-1, SL()});
        auto t2a_k  = t2ave.index({SL(), kf,   SL()});   auto t2a_km = t2ave.index({SL(), kf-1, SL()});
        const float buoy_scale = ablate_buoy_w_enabled() ? 0.0f : 1.0f;  // ablation (default 1)
        auto buoy = buoy_scale*dts*g*(rdn_k*(c2a_k*alt_k*t2a_k - c2a_km*alt_km*t2a_km) - c.c1f[kf]*s.muave);
        // + dts*rw_tend (frozen slow w-tendency, :1405) — REQUIRED, mirrors ru_tend in advance_uv.
        auto val = s.w.index({SL(), kf, SL()}) + dts * c.rw_tend.index({SL(), kf, SL()})
                   + 0.5f*dts*g*rdn_k*(up - lo) + buoy;
        w_rhs.index_put_({SL(), kf, SL()}, val);
    }
    // top kf=kde (WRF :1421-1429): -0.5*dts*g/mh(kde-1)*rdnw(kde-1)^2*2*c2a(kde-1)*(off-centered grad)
    {
        auto c2a_km = c.c2a.index({SL(), kde-1, SL()});
        auto mh_km  = mh.index({SL(), kde-1, SL()});
        auto rdnw_km = -c.rdnw[kde-1];   // WRF-signed (see interior loop comment)
        auto rhs_k = rhs.index({SL(), kde, SL()}); auto rhs_km = rhs.index({SL(), kde-1, SL()});
        auto ph_k  = s.ph.index({SL(), kde, SL()}); auto ph_km = s.ph.index({SL(), kde-1, SL()});
        auto grad = (1.0f+eps)*(rhs_k-rhs_km) + (1.0f-eps)*(ph_k-ph_km);
        // top buoyancy (:1427-1429): -dts*g*(2*rdnw*c2a*alt*t_2ave + c1f(kde)*muave)
        auto alt_km = c.alt.index({SL(), kde-1, SL()});
        auto t2a_km = t2ave.index({SL(), kde-1, SL()});
        const float buoy_scale = ablate_buoy_w_enabled() ? 0.0f : 1.0f;  // ablation (default 1)
        auto buoy_top = -buoy_scale*dts*g*(2.0f*rdnw_km*c2a_km*alt_km*t2a_km + c.c1f[kde]*s.muave);
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
    auto al  = -1.0f / mh * (c.alt * c1h_mu + (-c.rdnw.view({1, nz, 1})) * dph);   // WRF rdnw<0; ours |rdnw|
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
AcousticSchedule acoustic_schedule(int rk_step, float dt, int num_sound_steps) {
    float dts = dt / static_cast<float>(num_sound_steps);       // base acoustic timestep
    if (rk_step == 1) return {dt / 3.0f, 1};                    // single big step of dt/3
    if (rk_step == 2) return {dts, num_sound_steps / 2};        // dts * N/2 = dt/2
    return {dts, num_sound_steps};                              // rk_step 3: dts * N = dt
}

torch::Tensor mass_to_upoint(const torch::Tensor& mu) {
    using torch::indexing::Slice;
    const int nx = mu.size(1);
    auto interior = 0.5f * (mu.index({Slice(), Slice(0, nx - 1)}) + mu.index({Slice(), Slice(1, nx)})); // {ny,nx-1}
    auto edge = 0.5f * (mu.index({Slice(), Slice(nx - 1, nx)}) + mu.index({Slice(), Slice(0, 1)}));      // periodic
    return torch::cat({edge, interior, edge}, 1);   // {ny, nx+1}
}
torch::Tensor mass_to_vpoint(const torch::Tensor& mu) {
    using torch::indexing::Slice;
    const int ny = mu.size(0);
    auto interior = 0.5f * (mu.index({Slice(0, ny - 1), Slice()}) + mu.index({Slice(1, ny), Slice()})); // {ny-1,nx}
    // y is SYMMETRIC (em_b_wave free-slip walls): ghost mass reflects (mut(-1)=mut(0)), so the boundary
    // v-point mass = the edge interior column mass, NOT a periodic wrap.
    auto bot = mu.index({Slice(0, 1), Slice()});          // muv[0]  = mut[0]
    auto top = mu.index({Slice(ny - 1, ny), Slice()});    // muv[ny] = mut[ny-1]
    return torch::cat({bot, interior, top}, 0);   // {ny+1, nx}
}

CoefW calc_coef_w(const torch::Tensor& c2a, const torch::Tensor& mu_full,
                  const torch::Tensor& cqw,
                  const torch::Tensor& c1h, const torch::Tensor& c2h,
                  const torch::Tensor& c1f, const torch::Tensor& c2f,
                  const torch::Tensor& rdn, const torch::Tensor& rdnw,
                  float dts, float g, float epssm, bool top_lid) {
    const int ny = c2a.size(0), nz = c2a.size(1), nx = c2a.size(2), nzw = nz + 1;
    auto opts = c2a.options();
    auto a = torch::zeros({ny, nzw, nx}, opts);
    auto b = torch::zeros({ny, nzw, nx}, opts);
    auto c = torch::zeros({ny, nzw, nx}, opts);
    auto alpha = torch::zeros({ny, nzw, nx}, opts);
    auto gamma = torch::zeros({ny, nzw, nx}, opts);
    const float cof = std::pow(0.5f * dts * g * (1.0f + epssm), 2.0f);

    auto put = [&](torch::Tensor& T, int kf, const torch::Tensor& v) { T.index_put_({SL(), kf, SL()}, v); };
    auto mh = [&](int k) { return c1h[k-1] * mu_full + c2h[k-1]; };  // WRF 1-based mass level
    auto mf = [&](int k) { return c1f[k-1] * mu_full + c2f[k-1]; };  // WRF 1-based full level
    auto c2a_k = [&](int k) { return c2a.index({SL(), k-1, SL()}); };
    auto cqw_k = [&](int k) { return cqw.index({SL(), k-1, SL()}); };
    const float lid_flag = top_lid ? 0.0f : 1.0f;

    // a-band: a(2)=0 from the bottom boundary; interior kk=3..kde-1 and top kk=kde.
    const int kde = nzw;
    for (int kk = 3; kk <= kde - 1; ++kk) {
        const int k = kk - 1;
        put(a, kk - 1, -cqw_k(kk) * cof * rdn[kk - 1] * rdnw[kk - 2] * c2a_k(kk - 1) / (mh(k) * mf(k)));
    }
    put(a, kde - 1,
        -2.0f * cof * rdnw[kde - 2] * rdnw[kde - 2] * c2a_k(kde - 1) * lid_flag
        / (mh(kde - 1) * mf(kde - 1)));

    // Thomas factors over full-level rows k=2..kde, stored 0-based at kf=k-1.
    for (int k = 2; k <= kde - 1; ++k) {
        auto bdiag = 1.0f + cqw_k(k) * cof * rdn[k - 1] * (
            rdnw[k - 1] * c2a_k(k)     / (mh(k)     * mf(k)) +
            rdnw[k - 2] * c2a_k(k - 1) / (mh(k - 1) * mf(k)));
        auto cup = -cqw_k(k) * cof * rdn[k - 1] * rdnw[k - 1] * c2a_k(k) / (mh(k) * mf(k + 1));
        auto al = 1.0f / (bdiag - a.index({SL(), k - 1, SL()}) * gamma.index({SL(), k - 2, SL()}));
        put(b, k - 1, bdiag);
        put(c, k - 1, cup);
        put(alpha, k - 1, al);
        put(gamma, k - 1, cup * al);
    }
    {
        auto bdiag = 1.0f + 2.0f * cof * rdnw[kde - 2] * rdnw[kde - 2]
                              * c2a_k(kde - 1) / (mh(kde - 1) * mf(kde));
        auto al = 1.0f / (bdiag - a.index({SL(), kde - 1, SL()}) * gamma.index({SL(), kde - 2, SL()}));
        put(b, kde - 1, bdiag);
        put(alpha, kde - 1, al);
    }
    return {a, alpha, gamma};
}

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
    saves.mu = in.mu_2;                             // slow-ref mu (added back by small_step_finish, :213)
    s.mu   = in.mu_1 - in.mu_2;                      // mu COUPLED like ph (:214): mu' = mu_1 - mu_2 (~0 @ rk1)
    s.muts = in.muts;
    // diagnostics zero-init; caller runs calc_p_rho(step=0) to fill p/al/pm1 before the loop.
    s.p = torch::zeros_like(in.t_2); s.al = torch::zeros_like(in.t_2); s.pm1 = torch::zeros_like(in.t_2);
    s.t_2ave = torch::zeros_like(in.t_2); s.muave = torch::zeros_like(in.mu_2); s.mudf = torch::zeros_like(in.mu_2);
    s.t_ave = s.t;   // advance_mu_t re-saves this every substep before advance_w consumes it (:1141)
    return s;
}

FinishOutput small_step_finish(const State& s, const Saves& saves, const PrepInput& in,
                               int rk_step, int rk_order, int n_small, float dts,
                               const torch::Tensor& h_diabatic) {
    // Inverse of small_step_prep (:379-432): full = (msf*pert + save*coef_base)/coef_updated.
    auto coef = [](const torch::Tensor& c1, const torch::Tensor& c2, const torch::Tensor& mass) {
        int nl = c1.size(0);
        return c1.view({1, nl, 1}) * mass.unsqueeze(1) + c2.view({1, nl, 1});
    };
    FinishOutput f;
    f.u = (in.msfuy.unsqueeze(1) * s.u + saves.u * coef(in.c1h, in.c2h, in.muu)) / coef(in.c1h, in.c2h, in.muus);
    auto msfvx = torch::reciprocal(in.msfvx_inv);   // prep used *msfvx_inv; finish uses *msfvx = 1/msfvx_inv
    f.v = (msfvx.unsqueeze(1) * s.v + saves.v * coef(in.c1h, in.c2h, in.muv)) / coef(in.c1h, in.c2h, in.muvs);
    f.w = (in.msfty.unsqueeze(1) * s.w + saves.w * coef(in.c1f, in.c2f, in.mut)) / coef(in.c1f, in.c2f, in.muts);
    f.ph = s.ph + saves.ph;
    f.ww = s.ww + saves.ww;                         // saves.ww stands in for ww_1 (large-timestep omega)
    auto ct_base = coef(in.c1h, in.c2h, in.mut), ct_up = coef(in.c1h, in.c2h, in.muts);
    if (rk_step < rk_order) {
        f.t = (s.t + saves.t * ct_base) / ct_up;
    } else {
        f.t = (s.t - (dts * n_small) * ct_base * h_diabatic + saves.t * ct_base) / ct_up;
    }
    f.mu = s.mu + saves.mu;
    return f;
}

State advance_substep(const State& s, const Const& c, int small_step) {
    TORCH_CHECK(small_step >= 1,
        "advance_substep: small_step must be 1-based (>=1); the step=0 pressure init is a separate "
        "pre-loop calc_p_rho call, not the loop body.");
    State x = advance_uv(s, c);
    x = advance_mu_t(x, c);
    if (advance_w_ph_diag_enabled()) {
        const uint64_t sequence = advance_w_ph_diag_sequence(small_step);
        State before_w = x;
        State after_w = advance_w(x, c);
        try {
            emit_advance_w_ph_breakdown(
                sequence, small_step, c.dts,
                build_advance_w_ph_breakdown(before_w, c, after_w));
        } catch (const std::exception& e) {
            emit_advance_w_ph_diag_invalid(sequence, small_step, c.dts, e.what());
        }
        x = after_w;
    } else {
        x = advance_w(x, c);
    }
    x = calc_p_rho(x, c, small_step);   // small_step>=1 => divergence damping
    return x;
}

}}} // namespace wrf::sdirk3::acoustic
