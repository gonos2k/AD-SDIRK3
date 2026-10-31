#include "wrf_sdirk3_pressure_stage_probe.h"

#ifdef WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_state_layout.h"
#include "wrf_hydrostatic_pressure.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_mpi_safety.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace wrf::sdirk3::pressure_stage_probe {
namespace {
thread_local PressureEosContext* active_eos_context=nullptr;
double tensor_l2(const torch::Tensor& x) {
    return x.to(torch::kCPU,torch::kFloat64).norm().item<double>();
}

double maxabs(const torch::Tensor& x) {
    return x.to(torch::kCPU,torch::kFloat64).abs().max().item<double>();
}
bool same_bits(const torch::Tensor& a, const torch::Tensor& b) {
    if (a.sizes()!=b.sizes() || a.scalar_type()!=b.scalar_type()) return false;
    const auto ac=a.detach().to(torch::kCPU).contiguous();
    const auto bc=b.detach().to(torch::kCPU).contiguous();
    return std::memcmp(ac.data_ptr(),bc.data_ptr(),
                       static_cast<size_t>(ac.numel())*ac.element_size())==0;
}
struct EosErrors { double ad_abs, ad_rel; std::array<double,3> fd_abs, fd_rel; };
uint64_t tensor_digest(const torch::Tensor& x) {
    if (!x.defined()) return 0;
    const auto data=x.detach().to(torch::kCPU).contiguous();
    uint64_t h=14695981039346656037ULL;
    const auto mix=[&](const void* ptr,size_t n) {
        const auto* bytes=static_cast<const unsigned char*>(ptr);
        for (size_t i=0;i<n;++i) { h^=bytes[i]; h*=1099511628211ULL; }
    };
    const int type=static_cast<int>(data.scalar_type()); mix(&type,sizeof(type));
    const int64_t dim=data.dim(); mix(&dim,sizeof(dim));
    for (const auto size:data.sizes()) mix(&size,sizeof(size));
    mix(data.data_ptr(),static_cast<size_t>(data.numel())*data.element_size());
    return h;
}
}

ScopedEosCapture::ScopedEosCapture(PressureEosContext& context) : previous_(active_eos_context) {
    if (context.captured || context.duplicate) context.failed=true;
    active_eos_context=&context;
}
ScopedEosCapture::~ScopedEosCapture() noexcept { active_eos_context=previous_; }

void capture_eos(const torch::Tensor& ph,const torch::Tensor& t,const torch::Tensor& mu,
                 const torch::Tensor& mu_base,const torch::Tensor& alb,const torch::Tensor& p_base,
                 const torch::Tensor& c1h,const torch::Tensor& c2h,const torch::Tensor& rdnw,
                 const torch::Tensor& alt,const torch::Tensor& p_pert,
                 float rd,float cv,float cp,float p0,float t0) {
    if (!active_eos_context) return;
    auto& c=*active_eos_context;
    if (c.captured) { c.duplicate=true; return; }
    if (c.failed) return;
    try {
        torch::NoGradGuard guard;
        PressureEosContext snapshot;
        snapshot.ph=ph.detach().clone(); snapshot.t=t.detach().clone(); snapshot.mu=mu.detach().clone();
        snapshot.mu_base=mu_base.detach().clone(); snapshot.alb=alb.detach().clone();
        snapshot.p_base=p_base.detach().clone(); snapshot.c1h=c1h.detach().clone();
        snapshot.c2h=c2h.detach().clone(); snapshot.rdnw=rdnw.detach().clone();
        snapshot.alt=alt.detach().clone(); snapshot.p_pert=p_pert.detach().clone();
        snapshot.rd=rd; snapshot.cv=cv; snapshot.cp=cp; snapshot.p0=p0; snapshot.t0=t0;
        snapshot.captured=true;
        c=std::move(snapshot);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const c10::OutOfMemoryError&) {
        throw;
    } catch (...) {
        c.failed=true;
    }
}

void run(const torch::Tensor& U_eval, const torch::Tensor& F_eval, const torch::Tensor& K,
         const torch::Tensor& b_phys, const torch::Tensor& x_phys,
         const torch::Tensor& b_scaled, const torch::Tensor& x0_scaled,
         const torch::Tensor& x_scaled, const torch::Tensor& r_true_scaled,
         const torch::Tensor& S_diag,
         const torch::Tensor& S_inv_diag, const StateLayout& layout,
         double a, int stage, const PressureEosContext& live, const UnifiedPreconditioner& P) {
    TORCH_CHECK(stage==2 && std::isfinite(a) && a>0 && layout.is_valid() && layout.is_exact &&
                layout.blocks.size()==6 && U_eval.dim()==1 && U_eval.numel()==layout.total_size &&
                U_eval.scalar_type()==torch::kFloat64 && U_eval.is_cpu(),
                "pressure stage probe requires a stage-2 CPU FP64 exact-layout state");
    for (const auto& t : {U_eval,F_eval,K,b_phys,x_phys,b_scaled,x0_scaled,x_scaled,r_true_scaled})
        TORCH_CHECK(t.defined() && t.numel()==layout.total_size && torch::isfinite(t).all().item<bool>(),
                    "pressure stage probe input is missing/nonfinite");
    TORCH_CHECK(S_diag.defined()==S_inv_diag.defined(), "pressure probe scaling pair mismatch");
    if (S_diag.defined())
        TORCH_CHECK(S_diag.numel()==layout.total_size && S_inv_diag.numel()==layout.total_size &&
                    torch::isfinite(S_diag).all().item<bool>() && torch::isfinite(S_inv_diag).all().item<bool>() &&
                    (S_diag>0).all().item<bool>() && (S_inv_diag>0).all().item<bool>(),
                    "pressure probe scaling invalid");
    const auto g=P.pressure_probe_grid();
    TORCH_CHECK(g && g->nx>2 && g->ny>2 && g->nz>1 && g->nz_w==g->nz+1 &&
                g->th_base.defined() && g->p_base.defined() && g->alb.defined() &&
                g->mu_base.defined() && g->c1h.defined() && g->c2h.defined() &&
                g->c1f.defined() && g->c2f.defined() && g->rdnw.defined() && g->rdn.defined(),
                "pressure probe lacks native EOS/metric inputs");
    const int nx=g->nx,ny=g->ny,nz=g->nz,nw=g->nz_w,j=ny/2,i=nx/2;
    const auto expected_layout=StateLayout::from_grid_dims(nx,ny,nz,g->nx_u,g->ny_v,nw);
    TORCH_CHECK(layout.total_size==expected_layout.total_size,
                "pressure probe grid/layout total mismatch");
    for (size_t k=0;k<layout.blocks.size();++k)
        TORCH_CHECK(layout.blocks[k].start==expected_layout.blocks[k].start &&
                    layout.blocks[k].size==expected_layout.blocks[k].size &&
                    layout.blocks[k].name==expected_layout.blocks[k].name,
                    "pressure probe grid/layout block mismatch");
    const auto column=P.pressure_probe_column(j,i,U_eval);
    TORCH_CHECK(column.k_left.size()==static_cast<size_t>(nz) &&
                column.k_diag.size()==static_cast<size_t>(nz) &&
                column.k_right.size()==static_cast<size_t>(nz) &&
                column.b_left.size()==static_cast<size_t>(nz) &&
                column.b_diag.size()==static_cast<size_t>(nz),
                "pressure probe invalid raw column size");
    const auto opts=U_eval.options();
    TORCH_CHECK(live.captured && !live.duplicate && !live.failed,
                "pressure probe requires one successful live RHS EOS capture");
    for (const auto& x : {live.ph,live.t,live.mu,live.mu_base,live.alb,live.p_base,
                          live.c1h,live.c2h,live.rdnw,live.alt,live.p_pert})
        TORCH_CHECK(x.defined() && x.is_cpu() && torch::isfinite(x).all().item<bool>(),
                    "pressure probe captured EOS input/output missing, non-CPU or nonfinite");
    TORCH_CHECK(std::isfinite(live.rd) && live.rd>0 && std::isfinite(live.cv) && live.cv>0 &&
                std::isfinite(live.cp) && live.cp>0 && std::isfinite(live.p0) && live.p0>0 &&
                std::isfinite(live.t0),"pressure probe captured EOS constants invalid");
    const auto base_p=g->p_base.detach().clone(), base_theta=g->th_base.detach().clone();
    const auto pb=base_p.to(opts), thb=base_theta.to(opts), mub=g->mu_base.detach().clone().to(opts);
    const auto c1=g->c1h.detach().clone().to(opts).flatten().slice(0,0,nz);
    const auto c2=g->c2h.detach().clone().to(opts).flatten().slice(0,0,nz);
    const auto rnw=g->rdnw.detach().clone().to(opts).flatten().slice(0,0,nz).abs();
    const auto rn=g->rdn.detach().clone().to(opts).flatten().slice(0,0,nz).abs();
    const auto c1f=g->c1f.detach().clone().to(opts).flatten();
    const auto c2f=g->c2f.detach().clone().to(opts).flatten();
    // The live arrays were cloned at the original RHS EOS call, after the
    // RHS boundary projection and dtype conversion, without an extra RHS.
    const auto alb_rhs=live.alb.to(opts);
    const auto live_pb=live.p_base.to(opts), live_mub=live.mu_base.to(opts);
    const auto live_c1=live.c1h.to(opts).flatten().slice(0,0,nz);
    const auto live_c2=live.c2h.to(opts).flatten().slice(0,0,nz);
    const auto live_rnw=live.rdnw.to(opts).flatten().slice(0,0,nz);
    const auto alb_raw=g->alb.detach().clone().to(opts);
    for (const auto& x : {pb,thb,mub,c1,c2,rnw,rn,c1f,c2f,alb_rhs,alb_raw,live_pb,live_mub,live_c1,live_c2,live_rnw})
        TORCH_CHECK(torch::isfinite(x).all().item<bool>(),"pressure probe nonfinite EOS/metric input");
    TORCH_CHECK(std::isfinite(column.h),"pressure probe raw h nonfinite");
    const double alb_diff=maxabs(alb_rhs-alb_raw);
    const double alb_rel=alb_diff/std::max(maxabs(alb_rhs),1e-300);
    const auto ph0=layout.blocks[3].start, t0=layout.blocks[4].start, mu0=layout.blocks[5].start;
    const auto phi=[&](const torch::Tensor& x) { return x.slice(0,ph0,ph0+layout.blocks[3].size).view({ny,nw,nx}); };
    const auto theta=[&](const torch::Tensor& x) { return x.slice(0,t0,t0+layout.blocks[4].size).view({ny,nz,nx}); };
    const auto mu=[&](const torch::Tensor& x) { return x.slice(0,mu0,mu0+layout.blocks[5].size).view({ny,nx}); };
    TORCH_CHECK(live.ph.sizes()==phi(U_eval).sizes() && live.t.sizes()==theta(U_eval).sizes() &&
                live.mu.sizes()==mu(U_eval).sizes(),"pressure probe projected EOS state shape mismatch");
    const double projected_phi_diff=maxabs(live.ph.to(opts)-phi(U_eval));
    const double projected_theta_diff=maxabs(live.t.to(opts)-theta(U_eval));
    const double projected_mu_diff=maxabs(live.mu.to(opts)-mu(U_eval));
    auto eos_state=U_eval.detach().clone();
    phi(eos_state).copy_(live.ph.to(opts)); theta(eos_state).copy_(live.t.to(opts)); mu(eos_state).copy_(live.mu.to(opts));
    const auto eos=[&](const torch::Tensor& x) {
        return calc_p_rho_wrf(phi(x),theta(x),mu(x),live_mub,alb_rhs,live_pb,live_rnw,live_c1,live_c2,
                              live.rd,live.cv,live.cp,live.p0,live.t0);
    };
    const auto state=eos(eos_state);
    const double primal_p_error=maxabs(state.p_pert-live.p_pert.to(opts));
    const double primal_alpha_error=maxabs(state.alt-live.alt.to(opts));
    const auto pressure=live.p_pert.to(opts)+live_pb, alpha=live.alt.to(opts);
    const auto H=live_c1.view({1,nz,1})*(live_mub+mu(eos_state)).unsqueeze(1)+live_c2.view({1,nz,1});
    const auto Theta=theta(eos_state)+live.t0;
    TORCH_CHECK(torch::isfinite(pressure).all().item<bool>() && torch::isfinite(alpha).all().item<bool>() &&
                torch::isfinite(H).all().item<bool>() && torch::isfinite(Theta).all().item<bool>() &&
                (pressure>0).all().item<bool>() && (alpha>0).all().item<bool>() &&
                (H>0).all().item<bool>() && (Theta>0).all().item<bool>(),
                "pressure stage probe EOS state not positive");
    const double kappa=static_cast<double>(live.cp)/live.cv;
    const auto evaluate_direction=[&](const torch::Tensor& d) {
        const auto dp=phi(d).slice(1,1,nw)-phi(d).slice(1,0,nz);
        const auto analytic=kappa*pressure*(theta(d)/Theta-
            live_rnw.view({1,nz,1})*dp/(alpha*H)+live_c1.view({1,nz,1})*mu(d).unsqueeze(1)/H);
        torch::Tensor tangent;
        {
            jvp_detail::DualLevelGuard level;
            auto dual=torch::_make_dual(eos_state,d,static_cast<int64_t>(level.level()));
            auto result=torch::_unpack_dual(eos(dual).p_pert,static_cast<int64_t>(level.level()));
            tangent=std::get<1>(result);
        }
        TORCH_CHECK(tangent.defined() && torch::isfinite(analytic).all().item<bool>() &&
                    torch::isfinite(tangent).all().item<bool>(),"pressure EOS forward AD tangent missing/nonfinite");
        EosErrors result{};
        result.ad_abs=maxabs(analytic-tangent);
        result.ad_rel=result.ad_abs/std::max({maxabs(analytic),maxabs(tangent),1e-300});
        constexpr std::array<double,3> widths={1.0e-2,1.0e-3,1.0e-4};
        for (size_t k=0;k<widths.size();++k) {
            const double step=widths[k];
            const auto fd=(eos(eos_state+step*d).p_pert-eos(eos_state-step*d).p_pert)/(2*step);
            TORCH_CHECK(torch::isfinite(fd).all().item<bool>(),"pressure EOS finite difference nonfinite");
            result.fd_abs[k]=maxabs(analytic-fd);
            result.fd_rel[k]=result.fd_abs[k]/std::max({maxabs(analytic),maxabs(fd),1e-300});
        }
        return result;
    };
    std::vector<torch::Tensor> directions;
    for (int type=0;type<6;++type) {
        auto d=torch::zeros_like(U_eval);
        if (type==0) d.index_put_({ph0+(static_cast<int64_t>(j)*nw)*nx+i},1.0);
        if (type==1 || type==5) d.index_put_({ph0+(static_cast<int64_t>(j)*nw+1)*nx+i},1.0);
        if (type==2 || type==5) d.index_put_({ph0+(static_cast<int64_t>(j)*nw+nz)*nx+i},1.0);
        if (type==3 || type==5) d.index_put_({t0+(static_cast<int64_t>(j)*nz+nz/2)*nx+i},0.01);
        if (type==4 || type==5) d.index_put_({mu0+static_cast<int64_t>(j)*nx+i},1.0);
        directions.push_back(d);
    }
    std::vector<EosErrors> errors;
    for (const auto& d:directions) errors.push_back(evaluate_direction(d));

    // Reconstruct Q/T with the raw owner's imported ALB and compare its
    // actual stored FP32 K/B rows, including the one-sided dynamic top.
    const auto raw=calc_p_rho_wrf(phi(U_eval),theta(U_eval),mu(U_eval),mub,alb_raw,pb,rnw,c1,c2,
                                  g->rd,g->cv,g->cp,g->p0,g->t0);
    const auto praw=raw.p_pert+pb;
    TORCH_CHECK(torch::isfinite(praw).all().item<bool>() && torch::isfinite(raw.alt).all().item<bool>() &&
                (praw>0).all().item<bool>() && (raw.alt>0).all().item<bool>(),
                "pressure probe raw EOS nonfinite or nonpositive");
    const double raw_kappa=static_cast<double>(g->cp)/g->cv;
    const auto raw_Theta=theta(U_eval)+g->t0;
    TORCH_CHECK(torch::isfinite(raw_Theta).all().item<bool>() && (raw_Theta>0).all().item<bool>(),
                "pressure probe raw theta nonfinite or nonpositive");
    const auto raw_H=c1.view({1,nz,1})*(mub+mu(U_eval)).unsqueeze(1)+c2.view({1,nz,1});
    TORCH_CHECK(torch::isfinite(raw_H).all().item<bool>() && (raw_H>0).all().item<bool>(),
                "pressure probe raw hybrid mass nonfinite or nonpositive");
    auto q=(raw_kappa*praw*rnw.view({1,nz,1})/(raw.alt*raw_H)).select(0,j).select(1,i);
    auto t=(raw_kappa*praw/raw_Theta).select(0,j).select(1,i);
    const double mass=(mub+mu(U_eval)).index({j,i}).item<double>();
    double k_abs=0.0,k_rel=0.0,b_abs=0.0,b_rel=0.0,top_k_abs=0.0,top_b_abs=0.0;
    const auto compare=[&](double expected,float actual,bool top,bool is_k) {
        TORCH_CHECK(std::isfinite(expected) && std::isfinite(actual),"pressure probe nonfinite raw coefficient");
        const double e=std::abs(expected-static_cast<double>(actual));
        const double rel=e/std::max({std::abs(expected),std::abs(static_cast<double>(actual)),1e-300});
        if (is_k) { k_abs=std::max(k_abs,e); k_rel=std::max(k_rel,rel); if (top) top_k_abs=std::max(top_k_abs,e); }
        else { b_abs=std::max(b_abs,e); b_rel=std::max(b_rel,rel); if (top) top_b_abs=std::max(top_b_abs,e); }
    };
    for (int wk=1;wk<=nz;++wk) {
        const int row=wk-1; const bool top=wk==nz;
        const double denominator=c1f[wk].item<double>()*mass+c2f[wk].item<double>();
        const double factor=top ? 2.0*g->g*rnw[nz-1].item<double>()/denominator
                                : g->g*rn[wk].item<double>()/denominator;
        const double qlo=q[wk-1].item<double>(), thi=t[wk-1].item<double>();
        const double qhi=top ? 0.0:q[wk].item<double>();
        const double thi_next=top ? 0.0:t[wk].item<double>();
        compare(factor*qlo,column.k_left[row],top,true);
        compare(-factor*(qlo+qhi),column.k_diag[row],top,true);
        compare(top ? 0.0:factor*qhi,column.k_right[row],top,true);
        compare(top ? 0.0:factor*thi,column.b_left[row],top,false);
        compare(top ? factor*thi:-factor*thi_next,column.b_diag[row],top,false);
    }

    // FGMRES returned this RAW (not halo-masked or D-weighted) residual in
    // Krylov S coordinates. Do not call A/RHS after the original solve.
    const auto scaled_residual=r_true_scaled;
    const auto residual=S_diag.defined() ? S_diag*r_true_scaled : r_true_scaled;
    const auto x0_phys=S_diag.defined() ? S_diag*x0_scaled : x0_scaled;
    TORCH_CHECK(torch::isfinite(residual).all().item<bool>() &&
                torch::isfinite(x0_phys).all().item<bool>(),
                "pressure probe scaling produced nonfinite data");
    const double b_scale_error=tensor_l2(b_phys-(S_diag.defined() ? S_diag*b_scaled:b_scaled));
    const double x_scale_error=tensor_l2(x_phys-(S_diag.defined() ? S_diag*x_scaled:x_scaled));
    const double rnorm=tensor_l2(residual), bnorm=tensor_l2(b_phys);
    const double srnorm=tensor_l2(scaled_residual), sbnorm=tensor_l2(b_scaled);
    const double fnorm=tensor_l2(F_eval), knorm=tensor_l2(K), x0norm=tensor_l2(x0_phys);
    TORCH_CHECK(bnorm>0 && sbnorm>0 && std::isfinite(rnorm) && std::isfinite(bnorm) &&
                std::isfinite(srnorm) && std::isfinite(sbnorm) &&
                std::isfinite(fnorm) && std::isfinite(knorm) && std::isfinite(x0norm) &&
                std::isfinite(b_scale_error) && std::isfinite(x_scale_error) &&
                std::isfinite(k_abs) && std::isfinite(k_rel) &&
                std::isfinite(b_abs) && std::isfinite(b_rel) && std::isfinite(top_k_abs) &&
                std::isfinite(top_b_abs) && std::isfinite(alb_rel) &&
                std::isfinite(primal_p_error) && std::isfinite(primal_alpha_error) &&
                std::isfinite(projected_phi_diff) && std::isfinite(projected_theta_diff) &&
                std::isfinite(projected_mu_diff),
                "pressure probe invalid zero RHS or nonfinite coefficient observation");
    for (const auto& e:errors) {
        TORCH_CHECK(std::isfinite(e.ad_abs) && std::isfinite(e.ad_rel),"pressure probe nonfinite EOS AD comparison");
        for (size_t k=0;k<e.fd_abs.size();++k)
            TORCH_CHECK(std::isfinite(e.fd_abs[k]) && std::isfinite(e.fd_rel[k]),"pressure probe nonfinite EOS FD comparison");
    }
    std::ostringstream out;
    out << std::setprecision(17) << "PRESSURE_STAGE_PROBE {\"status\":\"observation\""
        << ",\"scope\":\"compact_digests_not_replayable\""
        << ",\"eos_base_scope\":\"captured_original_rhs_eos_call\""
        << ",\"eos_state_scope\":\"captured_projected_inputs\""
        << ",\"residual_scope\":\"saved_raw_unmasked_unweighted_not_independent\""
        << ",\"host_timestep\":" << mpi_safety::currentHostTimestep()
        << ",\"stage\":" << stage << ",\"newton_iteration\":0"
        << ",\"column_j\":" << j << ",\"column_i\":" << i << ",\"fd_steps\":[0.01,0.001,0.0001]"
        << ",\"live_rdnw_min\":" << live_rnw.min().item<double>()
        << ",\"live_rdnw_max\":" << live_rnw.max().item<double>()
        << ",\"a_newton\":" << a << ",\"a_raw_fp32\":" << column.h
        << ",\"raw_generation\":" << column.generation
        << ",\"live_vs_imported_alb_max\":" << alb_diff
        << ",\"live_vs_imported_alb_relative\":" << alb_rel
        << ",\"eos_primal_pressure_max_abs\":" << primal_p_error
        << ",\"eos_primal_alpha_max_abs\":" << primal_alpha_error
        << ",\"eos_primal_pressure_bit_equal\":" << (same_bits(state.p_pert,live.p_pert.to(opts)) ? "true" : "false")
        << ",\"eos_primal_alpha_bit_equal\":" << (same_bits(state.alt,live.alt.to(opts)) ? "true" : "false")
        << ",\"projected_phi_vs_Ueval_max\":" << projected_phi_diff
        << ",\"projected_theta_vs_Ueval_max\":" << projected_theta_diff
        << ",\"projected_mu_vs_Ueval_max\":" << projected_mu_diff
        << ",\"raw_K_max_abs_inverse_m\":" << k_abs << ",\"raw_K_max_relative\":" << k_rel
        << ",\"raw_B_max_abs_m_per_s2_K\":" << b_abs << ",\"raw_B_max_relative\":" << b_rel
        << ",\"raw_top_K_max_abs_inverse_m\":" << top_k_abs
        << ",\"raw_top_B_max_abs_m_per_s2_K\":" << top_b_abs
        << ",\"saved_native_checked_physical_residual_norm\":" << rnorm
        << ",\"physical_b_norm\":" << bnorm << ",\"saved_native_checked_physical_relative\":" << rnorm/bnorm
        << ",\"saved_native_checked_scaled_residual_norm\":" << srnorm
        << ",\"scaled_b_norm\":" << sbnorm << ",\"saved_native_checked_scaled_relative\":" << srnorm/sbnorm
        << ",\"F_eval_norm\":" << fnorm << ",\"K_norm\":" << knorm
        << ",\"physical_x0_norm\":" << x0norm
        << ",\"b_scale_error\":" << b_scale_error << ",\"x_scale_error\":" << x_scale_error
        << ",\"state_fnv64\":\"" << std::hex << tensor_digest(U_eval)
        << "\",\"F_fnv64\":\"" << tensor_digest(F_eval)
        << "\",\"K_fnv64\":\"" << tensor_digest(K)
        << "\",\"b_physical_fnv64\":\"" << tensor_digest(b_phys)
        << "\",\"b_scaled_fnv64\":\"" << tensor_digest(b_scaled)
        << "\",\"S_fnv64\":\"" << tensor_digest(S_diag)
        << "\",\"residual_scaled_fnv64\":\"" << tensor_digest(r_true_scaled) << "\""
        << std::dec << ",\"physical_block_saved_residual_norms\":[";
    for (size_t k=0;k<layout.blocks.size();++k) {
        if (k) out << ',';
        const auto& b=layout.blocks[k]; out << tensor_l2(residual.slice(0,b.start,b.start+b.size));
    }
    out << "],\"eos_directions\":[";
    const char* names[]={"phi_bottom_unprojected_eos","phi_bottom_adjacent","phi_dynamic_top","theta_mid","mu_column","mixed"};
    for (size_t k=0;k<errors.size();++k) {
        if (k) out << ',';
        const auto& e=errors[k];
        out << "{\"name\":\"" << names[k] << "\",\"ad_abs\":" << e.ad_abs
            << ",\"ad_relative\":" << e.ad_rel << ",\"fd_abs\":[";
        for (size_t n=0;n<e.fd_abs.size();++n) { if (n) out << ','; out << e.fd_abs[n]; }
        out << "],\"fd_relative\":[";
        for (size_t n=0;n<e.fd_rel.size();++n) { if (n) out << ','; out << e.fd_rel[n]; }
        out << "]}";
    }
    out << "]}";
    std::cout << out.str() << std::endl;
}
}
#endif
