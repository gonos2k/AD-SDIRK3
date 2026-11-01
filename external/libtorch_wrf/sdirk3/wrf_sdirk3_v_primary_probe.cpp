#include "wrf_sdirk3_v_primary_probe.h"

#ifdef WRF_SDIRK3_PRESSURE_A5
#include "wrf_sdirk3_pressure_stage_probe.h"
#include "wrf_sdirk3_native_pressure_reference.h"
#include "wrf_sdirk3_acoustic_substep.h"
#include "wrf_sdirk3_state_layout.h"
#include "wrf_hydrostatic_pressure.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace wrf::sdirk3::v_primary_probe {
namespace {
double maxabs(const torch::Tensor& x) { return x.abs().max().item<double>(); }
double l2_norm(const torch::Tensor& x) { return x.to(torch::kFloat64).norm().item<double>(); }
void finite(const torch::Tensor& x,const char* name) {
    TORCH_CHECK(x.defined() && x.is_cpu() &&
                (x.scalar_type()==torch::kFloat32 || x.scalar_type()==torch::kFloat64) &&
                torch::isfinite(x).all().item<bool>(),"A5 invalid ",name);
}
void write_records(const char* path, const std::vector<std::pair<std::string,torch::Tensor>>& records) {
    const uint16_t endian=1;
    TORCH_CHECK(*reinterpret_cast<const unsigned char*>(&endian)==1,"A5 artifact requires little endian");
    const std::string tmp=std::string(path)+".tmp";
    TORCH_CHECK(!std::ifstream(path).good() && !std::ifstream(tmp).good(),"A5 refuses artifact overwrite");
    try {
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        TORCH_CHECK(out.good(),"A5 cannot create artifact");
        const char magic[8]={'S','D','I','R','K','A','5','3'};
        out.write(magic,sizeof(magic));
        uint64_t count=0; for (const auto& r:records) if(r.second.defined()) ++count;
        out.write(reinterpret_cast<const char*>(&count),sizeof(count));
        for(const auto& record:records) {
            if (!record.second.defined()) continue;
            const auto x=record.second.detach().to(torch::kCPU).contiguous();
            TORCH_CHECK(x.dim()<=4 && record.first.size()<32 &&
                        (x.scalar_type()==torch::kFloat32 || x.scalar_type()==torch::kFloat64),
                        "A5 invalid context tensor ",record.first);
            char name[32]={};std::memcpy(name,record.first.data(),record.first.size());
            const uint32_t itemsize=static_cast<uint32_t>(x.element_size()), ndim=static_cast<uint32_t>(x.dim());
            int64_t dims[4]={0,0,0,0};for(uint32_t k=0;k<ndim;++k)dims[k]=x.size(k);
            const uint64_t bytes=static_cast<uint64_t>(x.numel())*itemsize;
            out.write(name,sizeof(name));out.write(reinterpret_cast<const char*>(&itemsize),sizeof(itemsize));
            out.write(reinterpret_cast<const char*>(&ndim),sizeof(ndim));out.write(reinterpret_cast<const char*>(dims),sizeof(dims));
            out.write(reinterpret_cast<const char*>(&bytes),sizeof(bytes));
            out.write(static_cast<const char*>(x.const_data_ptr()),static_cast<std::streamsize>(bytes));
        }
        out.flush();TORCH_CHECK(out.good(),"A5 artifact write failed");out.close();
        TORCH_CHECK(std::rename(tmp.c_str(),path)==0,"A5 artifact rename failed");
    } catch (...) { std::remove(tmp.c_str()); throw; }
}
struct Fields { torch::Tensor ph,t,mu; };
Fields fields(const torch::Tensor& x,const StateLayout& l,int64_t ny,int64_t nz,int64_t nx) {
    const auto take=[&](int k) { const auto& b=l.blocks[k]; return x.slice(0,b.start,b.start+b.size); };
    return {take(3).view({ny,nz+1,nx}),take(4).view({ny,nz,nx}),take(5).view({ny,nx})};
}
torch::Tensor ydiff(const torch::Tensor& x) {
    auto zero=torch::zeros_like(x.slice(0,0,1));
    return torch::cat({zero,x.slice(0,1,x.size(0))-x.slice(0,0,x.size(0)-1),zero},0);
}
torch::Tensor project_v(const torch::Tensor& v,const torch::Tensor& like,const StateLayout& l,
                        const pressure_stage_probe::PressureVPrimaryContext& c) {
    auto packed=torch::zeros_like(like);
    packed.slice(0,l.blocks[1].start,l.blocks[1].start+l.blocks[1].size).copy_(v.flatten());
    auto q=c.project_native(packed);
    return q.slice(0,l.blocks[1].start,l.blocks[1].start+l.blocks[1].size).view(v.sizes());
}
struct Primal { torch::Tensor output,dpy,level,velocity,alpha_v,ratio,alt,ppert; };
Primal primary(const torch::Tensor& x,const StateLayout& layout,
               const pressure_stage_probe::PressureEosContext& e,
               const pressure_stage_probe::PressureVPrimaryContext& c) {
    const int64_t ny=e.t.size(0),nz=e.t.size(1),nx=e.t.size(2),nv=ny+1;
    const auto q=c.project_native(x);
    const auto f=fields(q,layout,ny,nz,nx);
    const auto prho=calc_p_rho_wrf(f.ph,f.t,f.mu,e.mu_base,e.alb,e.p_base,e.rdnw,e.c1h,e.c2h,
                                   e.rd,e.cv,e.cp,e.p0,e.t0);
    const auto mass=f.mu+e.mu_base;
    // The supported dry native case is strictly positive, so the source
    // mass-floor branch must be inactive for base and all FD perturbations.
    TORCH_CHECK(torch::isfinite(mass).all().item<bool>() && (mass>0).all().item<bool>(),
                "A5 mass-floor branch is active or nonfinite");
    const auto level=c.level_mass_v_of(mass);
    const auto vel2=c.avg_y_2d(f.mu)+c.avg_y_2d(e.mu_base)+c.velocity_eps;
    const auto velocity=vel2.unsqueeze(1).expand({nv,nz,nx});
    const auto vx=e.geometry.msfvx.to(x.options()).unsqueeze(1).expand({nv,nz,nx});
    const auto vy=e.geometry.msfvy.to(x.options()).unsqueeze(1).expand({nv,nz,nx});
    const float eps=1.0e-10f;
    const auto bad=(~torch::isfinite(vx)) | (vx.abs()<eps);
    TORCH_CHECK(!bad.any().item<bool>(),"A5 source map denominator floor active");
    const auto vx_safe=torch::where(bad,torch::ones_like(vx),vx);
    const auto ratio=torch::where(bad,torch::zeros_like(vx),vy/vx_safe);
    const auto dph=ydiff(f.ph.slice(1,1,nz+1))+ydiff(f.ph.slice(1,0,nz));
    const auto dp=ydiff(prho.p_pert), dpb=ydiff(e.p_base);
    const auto dpy=acoustic::horizontal_pgf_primary(c.rdy,ratio*level,dph,
        2.0f*c.avg_y_3d(prho.alt),dp,2.0f*c.avg_y_3d(prho.al_pert),dpb);
    const auto alpha_v=level/vx;
    TORCH_CHECK(torch::isfinite(level).all().item<bool>() && (level>0).all().item<bool>() &&
                torch::isfinite(velocity).all().item<bool>() && (velocity>0).all().item<bool>() &&
                torch::isfinite(alpha_v).all().item<bool>() && (alpha_v>0).all().item<bool>(),
                "A5 source layer/velocity denominator invalid");
    const auto raw=-c.cqv_at_v*dpy;
    const auto physical=(raw*(velocity/alpha_v))/velocity;
    return {project_v(physical,x,layout,c),dpy,level,velocity,alpha_v,ratio,prho.alt,prho.p_pert};
}
}

struct PreparedPrimary::Impl {
    torch::Tensor U,c1,R,H,T,p,alt,alt_v,bg_grad,factor;
    StateLayout layout;
    pressure_stage_probe::PressureEosContext e;
    pressure_stage_probe::PressureVPrimaryContext c;
    int64_t ny=0,nz=0,nx=0;
    Actions primal;
};
PreparedPrimary::PreparedPrimary(const torch::Tensor& U_eval,const StateLayout& layout,
                 const pressure_stage_probe::PressureEosContext& e,
                 const pressure_stage_probe::PressureVPrimaryContext& c) : impl_(new Impl) {
    finite(U_eval,"U_eval");
    TORCH_CHECK(layout.is_valid() && layout.is_exact && layout.blocks.size()==6 && U_eval.dim()==1 &&
                U_eval.scalar_type()==torch::kFloat64 && U_eval.numel()==layout.total_size &&
                e.captured && !e.failed && !e.duplicate && c.captured && !c.failed && !c.duplicate &&
                c.canonical_packed_dry && c.base_state_branch && c.use_wrf_mass_flux &&
                c.project_native && c.avg_y_3d && c.avg_y_2d && c.level_mass_v_of &&
                std::isfinite(c.rdy) && c.rdy>0 && std::isfinite(c.velocity_eps) && c.velocity_eps>0,
                "A5 unsupported or incomplete captured native context");
    for (const auto& x : {e.ph,e.t,e.mu,e.mu_base,e.alb,e.p_base,e.c1h,e.c2h,e.rdnw,e.alt,e.p_pert,
                          e.geometry.msfvx,e.geometry.msfvy,c.cqv_at_v,c.level_mass_v,c.velocity_mass_v,
                          c.alpha_v,c.msf_ratio_v,c.dpy_primary}) finite(x,"EOS/V source operand");
    const int64_t ny=e.t.size(0),nz=e.t.size(1),nx=e.t.size(2),nv=ny+1,m=ny-1,n=nx-1;
    const auto expected=StateLayout::from_grid_dims(nx,ny,nz,nx+1,nv,nz+1);
    TORCH_CHECK(n>=2 && m>=2 && nz>=2 && e.geometry.whole_single_rank &&
                e.geometry.packed_periodic_x_symmetric_y && e.ph.sizes()==torch::IntArrayRef({ny,nz+1,nx}) &&
                e.mu.sizes()==torch::IntArrayRef({ny,nx}) && e.mu_base.sizes()==e.mu.sizes() &&
                e.alb.sizes()==e.t.sizes() && e.p_base.sizes()==e.t.sizes() &&
                e.alt.sizes()==e.t.sizes() && e.p_pert.sizes()==e.t.sizes() &&
                e.c1h.numel()>=nz && e.c2h.numel()>=nz && e.rdnw.numel()>=nz &&
                std::isfinite(e.rd) && e.rd>0 && std::isfinite(e.cv) && e.cv>0 &&
                std::isfinite(e.cp) && e.cp>0 && std::isfinite(e.p0) && e.p0>0 &&
                std::isfinite(e.t0) &&
                e.geometry.msfvx.sizes()==torch::IntArrayRef({nv,nx}) &&
                e.geometry.msfvy.sizes()==torch::IntArrayRef({nv,nx}) &&
                c.cqv_at_v.sizes()==torch::IntArrayRef({nv,nz,nx}) &&
                c.level_mass_v.sizes()==c.cqv_at_v.sizes() && c.velocity_mass_v.sizes()==c.cqv_at_v.sizes() &&
                c.alpha_v.sizes()==c.cqv_at_v.sizes() && c.msf_ratio_v.sizes()==c.cqv_at_v.sizes() &&
                c.dpy_primary.sizes()==c.cqv_at_v.sizes() &&
                c.rdy==e.geometry.rdy,
                "A5 native shapes/topology/metric mismatch");
    TORCH_CHECK((e.geometry.msfvx==1).all().item<bool>() && (e.geometry.msfvy==1).all().item<bool>(),
                "A5 requires actual unit V map factors");
    TORCH_CHECK(layout.total_size==expected.total_size,"A5 native layout size mismatch");
    for(size_t k=0;k<layout.blocks.size();++k)
        TORCH_CHECK(layout.blocks[k].name==expected.blocks[k].name &&
                    layout.blocks[k].start==expected.blocks[k].start &&
                    layout.blocks[k].size==expected.blocks[k].size,"A5 native layout block mismatch");
    TORCH_CHECK((c.cqv_at_v.slice(0,1,m).slice(2,0,n)==1.0).all().item<bool>(),
                "A5 requires actual dry-unit cqv on physical interior V faces");
    const auto projected=c.project_native(U_eval);
    const auto f=fields(projected,layout,ny,nz,nx);
    finite(projected,"projected U_eval");
    auto& result=impl_->primal;
    result.projected_state_max=std::max({maxabs(f.ph-e.ph),maxabs(f.t-e.t),maxabs(f.mu-e.mu)});
    TORCH_CHECK(torch::equal(f.ph,e.ph) && torch::equal(f.t,e.t) && torch::equal(f.mu,e.mu),
                "A5 captured EOS state differs from native Q(U_eval)");
    const auto base=primary(U_eval,layout,e,c);
    result.primal_dpy_max=maxabs(base.dpy-c.dpy_primary);
    result.primal_level_mass_max=maxabs(base.level-c.level_mass_v);
    result.primal_velocity_mass_max=maxabs(base.velocity-c.velocity_mass_v);
    result.primal_alpha_v_max=maxabs(base.alpha_v-c.alpha_v);
    result.primal_ratio_max=maxabs(base.ratio-c.msf_ratio_v);
    result.primal_alpha_max=maxabs(base.alt-e.alt);
    result.primal_pressure_max=maxabs(base.ppert-e.p_pert);
    TORCH_CHECK(torch::equal(base.dpy,c.dpy_primary) && torch::equal(base.level,c.level_mass_v) &&
                torch::equal(base.velocity,c.velocity_mass_v) && torch::equal(base.alpha_v,c.alpha_v) &&
                torch::equal(base.ratio,c.msf_ratio_v) && torch::equal(base.alt,e.alt) &&
                torch::equal(base.ppert,e.p_pert),"A5 isolated primal differs from captured source operands");
    auto& d=*impl_;d.U=U_eval.detach().clone();d.layout=layout;d.e=e;d.c=c;
    d.ny=ny;d.nz=nz;d.nx=nx;
    // Owned snapshots prevent stale live-context mutation from changing this stage operator.
    d.e.alt=e.alt.detach().clone();d.e.t=e.t.detach().clone();d.e.p_pert=e.p_pert.detach().clone();
    d.e.p_base=e.p_base.detach().clone();d.c.cqv_at_v=c.cqv_at_v.detach().clone();
    d.e.geometry.msfvy=e.geometry.msfvy.detach().clone();
    d.c1=e.c1h.to(U_eval.options()).flatten().slice(0,0,nz).view({1,nz,1}).clone();
    d.R=e.rdnw.to(U_eval.options()).flatten().slice(0,0,nz).view({1,nz,1}).clone();
    const auto c2=e.c2h.to(U_eval.options()).flatten().slice(0,0,nz).view({1,nz,1});
    d.H=(d.c1*(e.mu_base+e.mu).unsqueeze(1)+c2).detach().clone();
    d.T=(e.t+e.t0).detach().clone();d.p=(e.p_pert+e.p_base).detach().clone();
    TORCH_CHECK((d.H>0).all().item<bool>() && (d.T>0).all().item<bool>() && (e.alt>0).all().item<bool>(),
                "A5 analytic EOS requires positive state");
    // The action only needs physical mass cells and interior V faces.
    d.H=d.H.slice(0,0,m).slice(2,0,n).contiguous();
    d.T=d.T.slice(0,0,m).slice(2,0,n).contiguous();
    d.p=d.p.slice(0,0,m).slice(2,0,n).contiguous();
    d.alt=d.e.alt.slice(0,0,m).slice(2,0,n).contiguous();
    d.alt_v=c.avg_y_3d(d.e.alt).slice(0,1,m).slice(2,0,n).contiguous();
    d.bg_grad=(ydiff(d.e.p_pert)+ydiff(d.e.p_base)).slice(0,1,m).slice(2,0,n).contiguous();
    d.factor=(-d.c.cqv_at_v.slice(0,1,m).slice(2,0,n)*
              d.e.geometry.msfvy.slice(0,1,m).slice(1,0,n).unsqueeze(1)*c.rdy).contiguous();
}
PreparedPrimary::~PreparedPrimary()=default;
PreparedPrimary::PreparedPrimary(PreparedPrimary&&) noexcept=default;
PreparedPrimary& PreparedPrimary::operator=(PreparedPrimary&&) noexcept=default;
Actions PreparedPrimary::compute_core(const torch::Tensor& ph,const torch::Tensor& t,
                                      const torch::Tensor& mu,bool summed) const {
    const auto& d=*impl_;const auto m=d.ny-1,n=d.nx-1,nz=d.nz;
    TORCH_CHECK(ph.defined() && t.defined() && mu.defined() && ph.is_cpu() && t.is_cpu() && mu.is_cpu() &&
                ph.scalar_type()==torch::kFloat64 && t.scalar_type()==torch::kFloat64 && mu.scalar_type()==torch::kFloat64 &&
                ph.sizes()==torch::IntArrayRef({m,nz+1,n}) && t.sizes()==torch::IntArrayRef({m,nz,n}) &&
                mu.sizes()==torch::IntArrayRef({m,n}),"A5 physical core direction mismatch");
    const auto ydiff_core=[&](const torch::Tensor& x) {return x.slice(0,1,m)-x.slice(0,0,m-1);};
    const auto dphi=ph.slice(1,1,nz+1)-ph.slice(1,0,nz);
    const auto dalpha=d.R*dphi/d.H-d.alt*d.c1*mu.unsqueeze(1)/d.H;
    const double kappa=static_cast<double>(d.e.cp)/d.e.cv;
    const auto dp=kappa*d.p*(t/d.T-dalpha/d.alt);
    const auto yphi=0.5*(ydiff_core(ph.slice(1,1,nz+1))+ydiff_core(ph.slice(1,0,nz)));
    const auto phi=d.factor*yphi;
    const auto pressure=d.factor*d.alt_v*ydiff_core(dp);
    const auto alpha=d.factor*(0.5*(dalpha.slice(0,0,m-1)+dalpha.slice(0,1,m)))*d.bg_grad;
    Actions result;
    if(summed) {result.phi=phi+pressure+alpha;return result;}
    result.phi=phi;result.pressure=pressure;result.alpha=alpha;return result;
}
Actions PreparedPrimary::compute(const torch::Tensor& z,bool summed) const {
    const auto& d=*impl_;const auto m=d.ny-1,n=d.nx-1,nz=d.nz;
    finite(z,"z");TORCH_CHECK(z.scalar_type()==torch::kFloat64 && z.sizes()==d.U.sizes(),"A5 direction mismatch");
    const auto df=fields(d.c.project_native(z),d.layout,d.ny,nz,d.nx);
    auto core=compute_core(df.ph.slice(0,0,m).slice(2,0,n),df.t.slice(0,0,m).slice(2,0,n),
                           df.mu.slice(0,0,m).slice(1,0,n),summed);
    const auto expand=[&](const torch::Tensor& x) {
        auto carrier=torch::zeros({d.ny+1,nz,d.nx},d.U.options());
        carrier.slice(0,1,m).slice(2,0,n).copy_(x);
        return project_v(carrier,d.U,d.layout,d.c);
    };
    core.phi=expand(core.phi);
    if(!summed) {core.pressure=expand(core.pressure);core.alpha=expand(core.alpha);}
    return core;
}
Actions PreparedPrimary::apply_terms(const torch::Tensor& z) const {return compute(z,false);}
torch::Tensor PreparedPrimary::apply_sum(const torch::Tensor& z) const {return compute(z,true).phi;}
torch::Tensor PreparedPrimary::apply_core(const torch::Tensor& ph,const torch::Tensor& t,const torch::Tensor& mu) const {
    return compute_core(ph,t,mu,true).phi;
}
Actions evaluate(const torch::Tensor& U_eval,const torch::Tensor& z,const StateLayout& layout,
                 const pressure_stage_probe::PressureEosContext& e,
                 const pressure_stage_probe::PressureVPrimaryContext& c) {
    PreparedPrimary prepared(U_eval,layout,e,c);
    Actions result=prepared.impl_->primal;
    {
        jvp_detail::DualLevelGuard level;
        auto dual=torch::_make_dual(U_eval,z,static_cast<int64_t>(level.level()));
        auto parts=torch::_unpack_dual(primary(dual,layout,e,c).output,static_cast<int64_t>(level.level()));
        result.fwad=std::get<1>(parts);
        TORCH_CHECK(result.fwad.defined(),"A5 isolated primary FWAD tangent missing");
    }
    constexpr std::array<double,3> steps={1.e-2,1.e-3,1.e-4};
    for(size_t k=0;k<steps.size();++k)
        result.fd[k]=(primary(U_eval+steps[k]*z,layout,e,c).output-
                      primary(U_eval-steps[k]*z,layout,e,c).output)/(2*steps[k]);

    const auto terms=prepared.apply_terms(z);
    result.phi=terms.phi;result.pressure=terms.pressure;result.alpha=terms.alpha;
    for (const auto& x:{result.phi,result.pressure,result.alpha,result.fwad,result.fd[0],result.fd[1],result.fd[2]})
        finite(x,"directional result");
    return result;
}

void run(const torch::Tensor& U_eval,const torch::Tensor& v,const torch::Tensor& z,
         const torch::Tensor& Az,const torch::Tensor& v_scaled,const torch::Tensor& z_scaled,
         const torch::Tensor& Az_scaled,const torch::Tensor& S_diag,double a,const StateLayout& layout,
         const pressure_stage_probe::PressureEosContext& eos,
         const pressure_stage_probe::PressureVPrimaryContext& c,
         const NativePressureReference& P0,const torch::Tensor& base_z_scaled) {
    torch::NoGradGuard guard;
    for (const auto& x:{U_eval,v,z,Az,v_scaled,z_scaled,Az_scaled}) finite(x,"Arnoldi vector");
    TORCH_CHECK(std::isfinite(a) && a>0 && v.scalar_type()==torch::kFloat64 &&
                z.scalar_type()==torch::kFloat64 && Az.scalar_type()==torch::kFloat64 &&
                v.sizes()==z.sizes() && Az.sizes()==z.sizes() &&
                U_eval.sizes()==z.sizes(),"A5 invalid same-direction input");
    TORCH_CHECK(v_scaled.sizes()==v.sizes() && z_scaled.sizes()==z.sizes() && Az_scaled.sizes()==Az.sizes(),
                "A5 scaled/physical vector shape mismatch");
    if (S_diag.defined()) {
        finite(S_diag,"S");
        TORCH_CHECK(S_diag.sizes()==z.sizes() && (S_diag>0).all().item<bool>() &&
                    torch::equal(v,S_diag*v_scaled) && torch::equal(z,S_diag*z_scaled) &&
                    torch::equal(Az,S_diag*Az_scaled),"A5 scaled-to-physical identity mismatch");
    } else {
        TORCH_CHECK(torch::equal(v,v_scaled) && torch::equal(z,z_scaled) && torch::equal(Az,Az_scaled),
                    "A5 identity-S vector mismatch");
    }
    const auto Pz=P0.apply_operator(z);
    finite(Pz,"P0 action");
        const auto& geo=eos.geometry;
        const auto& profile=P0.receipt();
        const auto& p=profile.profiles;
        const auto as_tensor=[](const std::vector<double>& values) { return torch::tensor(values,torch::kFloat64); };
        std::vector<std::pair<std::string,torch::Tensor>> context={
            {"U_eval",U_eval},{"P0z_physical",Pz},
            {"v_scaled",v_scaled},{"z_scaled",z_scaled},{"Az_scaled",Az_scaled},
            {"S_diag",S_diag.defined()?S_diag:torch::ones_like(z)},
            {"eos_ph",eos.ph},{"eos_t",eos.t},{"eos_mu",eos.mu},{"mu_base",eos.mu_base},
            {"alb",eos.alb},{"p_base",eos.p_base},{"c1h",eos.c1h},{"c2h",eos.c2h},
            {"rdnw",eos.rdnw},{"alt",eos.alt},{"p_pert",eos.p_pert},
            {"c1f",geo.c1f},{"c2f",geo.c2f},{"rdn",geo.rdn},{"msftx",geo.msftx},
            {"msfty",geo.msfty},{"msfux",geo.msfux},{"msfuy",geo.msfuy},
            {"msfvx",geo.msfvx},{"msfvy",geo.msfvy},{"ground_phi",geo.ground_phi},
            {"cqv_at_v",c.cqv_at_v},{"level_mass_v",c.level_mass_v},
            {"velocity_mass_v",c.velocity_mass_v},{"alpha_v",c.alpha_v},
            {"msf_ratio_v",c.msf_ratio_v},{"dpy_primary",c.dpy_primary},
            {"profile_q",as_tensor(p.q)},{"profile_etheta",as_tensor(p.etheta)},
            {"profile_emu",as_tensor(p.emu)},{"profile_gz_edge",as_tensor(p.gz_edge)},
            {"profile_alpha",as_tensor(p.alpha)},{"profile_beta",as_tensor(p.beta)},
            {"profile_H",as_tensor(p.H)},{"profile_nu",as_tensor(p.nu)},{"profile_dnw",as_tensor(p.dnw)},
            {"profile_p_ref",as_tensor(profile.p_ref)},{"profile_alpha_ref",as_tensor(profile.alpha_ref)},
            {"profile_theta_ref",as_tensor(profile.theta_ref)},
            {"constants",torch::tensor(std::vector<double>{static_cast<double>(eos.rd),static_cast<double>(eos.cv),
                 static_cast<double>(eos.cp),static_cast<double>(eos.p0),static_cast<double>(eos.t0),
                 static_cast<double>(geo.rdx),static_cast<double>(c.rdy),static_cast<double>(c.velocity_eps),a},torch::kFloat64)},
            {"flags",torch::tensor({static_cast<double>(c.captured),static_cast<double>(c.failed),
                 static_cast<double>(c.duplicate),static_cast<double>(c.canonical_packed_dry),
                 static_cast<double>(c.native_nh),static_cast<double>(c.base_state_branch),
                 static_cast<double>(c.use_wrf_mass_flux),static_cast<double>(S_diag.defined())},torch::kFloat64)}};
        if(base_z_scaled.defined()) {
            finite(base_z_scaled,"base_z_scaled");
            TORCH_CHECK(base_z_scaled.sizes()==z_scaled.sizes(),"A5 base z shape mismatch");
            context.emplace_back("base_z_scaled",base_z_scaled);
        }
        write_records("pressure_a5_direction.bin",context);
        std::ostringstream captured;
        captured << "[PRESSURE_A5_CAPTURE] {\"status\":\"captured\",\"path\":\"pressure_a5_direction.bin\",\"missing\":[";
        bool first=true; for(const auto& r:context) if(!r.second.defined()) {
            if(!first) captured << ','; first=false; captured << '\"' << r.first << '\"';
        }
        captured << "]}"; std::cout << captured.str() << std::endl;
    const auto reconstruction=v-Pz, mismatch=Pz-Az, defect=v-Az;
    const auto closure=defect-reconstruction-mismatch;
    const auto scaled_Pz=S_diag.defined()?Pz/S_diag:Pz;
    const auto scaled_reconstruction=v_scaled-scaled_Pz;
    const auto scaled_mismatch=scaled_Pz-Az_scaled;
    const auto scaled_closure=(v_scaled-Az_scaled)-scaled_reconstruction-scaled_mismatch;
    const double v_scaled_norm=l2_norm(v_scaled);
    const double q_scaled_norm=l2_norm(scaled_reconstruction);
    TORCH_CHECK(std::isfinite(v_scaled_norm) && std::isfinite(q_scaled_norm),
                "A5 nonfinite scaled inverse reconstruction");
    const double inverse_ratio=v_scaled_norm>0 ? q_scaled_norm/v_scaled_norm : -1.0;
    const bool inverse_ok=v_scaled_norm>0 && std::isfinite(inverse_ratio) && inverse_ratio<=1.0e-8;
    bool closure_ok=true;
    std::vector<double> closure_max,closure_budget,scaled_closure_max,scaled_closure_budget;
    for (const auto& block:layout.blocks) {
        const auto sub=[&](const torch::Tensor& x) { return x.slice(0,block.start,block.start+block.size); };
        const double scale=std::max({maxabs(sub(v)),maxabs(sub(Pz)),maxabs(sub(Az))});
        const double error=maxabs(sub(closure));
        const double budget=128.0*std::numeric_limits<double>::epsilon()*scale;
        const double sscale=std::max({maxabs(sub(v_scaled)),maxabs(sub(scaled_Pz)),maxabs(sub(Az_scaled))});
        const double serror=maxabs(sub(scaled_closure));
        const double sbudget=128.0*std::numeric_limits<double>::epsilon()*sscale;
        TORCH_CHECK(std::isfinite(scale) && std::isfinite(error) && std::isfinite(budget) &&
                    std::isfinite(sscale) && std::isfinite(serror) && std::isfinite(sbudget),
                    "A5 nonfinite packed block closure");
        closure_ok=closure_ok && error<=budget && serror<=sbudget;
        closure_max.push_back(error);closure_budget.push_back(budget);
        scaled_closure_max.push_back(serror);scaled_closure_budget.push_back(sbudget);
    }
    std::ostringstream decomposition;
    decomposition << std::setprecision(17) << "[PRESSURE_A5_DECOMPOSITION] {\"inverse_scaled_relative\":"
                  << inverse_ratio << ",\"packed_closure_ok\":" << (closure_ok?"true":"false")
                  << ",\"blocks\":[";
    for(size_t k=0;k<layout.blocks.size();++k) {
        if(k) decomposition << ',';
        decomposition << "{\"name\":\"" << layout.blocks[k].name << "\",\"closure_max\":"
                      << closure_max[k] << ",\"closure_budget\":" << closure_budget[k]
                      << ",\"scaled_closure_max\":" << scaled_closure_max[k]
                      << ",\"scaled_closure_budget\":" << scaled_closure_budget[k] << '}';
    }
    decomposition << "]}"; std::cout << decomposition.str() << std::endl;
    const auto acts=evaluate(U_eval,z,layout,eos,c);
    const auto& vb=layout.blocks[1];
    const int64_t ny=eos.t.size(0),nz=eos.t.size(1),nx=eos.t.size(2),nv=ny+1,m=ny-1,n=nx-1;
    const auto vview=[&](const torch::Tensor& x) {
        return x.slice(0,vb.start,vb.start+vb.size).view({nv,nz,nx});
    };
    const auto core=[&](const torch::Tensor& x) { return x.slice(0,1,m).slice(2,0,n); };
    const auto analytic=acts.phi+acts.pressure+acts.alpha;
    const auto check_pointwise=[&](const torch::Tensor& x,double rel) {
        return ((core(x-acts.fwad).abs()) <= (1.0e-12+rel*core(acts.fwad).abs())).all().item<bool>();
    };
    const bool ad_ok=check_pointwise(analytic,1.0e-9);
    std::array<bool,3> fd_ok{};
    for(size_t k=0;k<fd_ok.size();++k) fd_ok[k]=check_pointwise(acts.fd[k],1.0e-5);
    const int fd_passes=static_cast<int>(fd_ok[0])+static_cast<int>(fd_ok[1])+static_cast<int>(fd_ok[2]);
    const auto native_primary=a*acts.fwad;
    const auto p0_action=vview(z-Pz);
    const auto candidate=native_primary-p0_action;
    const auto rv=core(vview(mismatch)), tv=core(candidate), vv=core(vview(v));
    const double rv_norm=l2_norm(rv),tv_norm=l2_norm(tv),v_norm=l2_norm(vv);
    TORCH_CHECK(std::isfinite(rv_norm) && std::isfinite(tv_norm) && std::isfinite(v_norm),
                "A5 nonfinite attribution norm");
    const bool signal=v_norm>0 && rv_norm/v_norm>=1.0e-8 && rv_norm>0;
    const bool eligible=signal && inverse_ok && closure_ok && ad_ok && fd_passes>=2;
    const double score=signal ? std::pow(l2_norm(rv-tv)/rv_norm,2) : -1.0;
    const double dot=(rv*tv).sum().item<double>();
    const double cosine=(rv_norm>0 && tv_norm>0) ? dot/(rv_norm*tv_norm) : 0.0;
    const double fit=tv_norm>0 ? dot/(tv_norm*tv_norm) : 0.0;
    const double orthogonal=rv_norm>0 ? l2_norm(rv-fit*tv)/rv_norm : 0.0;
    TORCH_CHECK(std::isfinite(score) && std::isfinite(dot) && std::isfinite(cosine) &&
                std::isfinite(fit) && std::isfinite(orthogonal),"A5 nonfinite attribution statistic");
    const bool gate_pass=eligible && std::isfinite(score) && score<=0.5;
    const auto vm=vview(mismatch);
    const double v_wall=l2_norm(torch::cat({vm.slice(0,0,1).slice(2,0,n),
                                        vm.slice(0,m,m+1).slice(2,0,n)},0));
    const double v_alias_x=l2_norm(vm.slice(2,n,n+1));
    const double v_alias_y=l2_norm(vm.slice(0,m+1,m+2).slice(2,0,n));
    const auto& wb=layout.blocks[2];
    const auto wm=mismatch.slice(0,wb.start,wb.start+wb.size).view({ny,nz+1,nx})
        .slice(0,0,m).slice(2,0,n);
    const double w_top=l2_norm(wm.slice(1,nz,nz+1));
    const double w_interior=l2_norm(wm.slice(1,1,nz));

    write_records("pressure_a5_actions.bin",{
        {"Qz_physical",c.project_native(z)}, {"V_Jphi",acts.phi}, {"V_Jp",acts.pressure},
        {"V_Jalpha",acts.alpha}, {"V_Jfwad",acts.fwad}, {"V_P0_difference",p0_action}});
    std::ostringstream out; out << std::setprecision(17)
        << "[PRESSURE_A5] {\"status\":\"observed\",\"cost_eligible\":false,\"eligible\":" << (eligible?"true":"false")
        << ",\"gate_pass\":" << (gate_pass?"true":"false")
        << ",\"native_nh\":" << (c.native_nh?"true":"false")
        << ",\"base_state_branch\":" << (c.base_state_branch?"true":"false")
        << ",\"use_wrf_mass_flux\":" << (c.use_wrf_mass_flux?"true":"false")
        << ",\"S_diag_provided\":" << (S_diag.defined()?"true":"false")
        << ",\"scale_check\":\"torch_equal_numeric_signed_zero_equal\""
        << ",\"gate_threshold_squared_ratio\":0.5,\"minimum_signal_ratio\":1e-8"
        << ",\"inverse_scaled_relative\":" << inverse_ratio
        << ",\"inverse_scaled_ok\":" << (inverse_ok?"true":"false")
        << ",\"inverse_scaled_limit\":1e-8"
        << ",\"ad_pointwise_ok\":" << (ad_ok?"true":"false")
        << ",\"packed_closure_ok\":" << (closure_ok?"true":"false")
        << ",\"closure_budget\":\"128*float64_eps*maxabs(v,P0z,Az), physical and scaled separately per packed block\""
        << ",\"action_check\":\"per V point: 1e-12 m/s3 + relative*abs(FWAD), AD 1e-9, FD 1e-5, at least 2 of 3 FD widths\""
        << ",\"fd_pointwise_ok\":[" << (fd_ok[0]?"true":"false") << ','
        << (fd_ok[1]?"true":"false") << ',' << (fd_ok[2]?"true":"false") << ']'
        << ",\"fd_steps_seconds\":[0.01,0.001,0.0001]"
        << ",\"rV_norm\":" << rv_norm << ",\"vV_norm\":" << v_norm
        << ",\"candidate_norm\":" << tv_norm << ",\"unit_squared_ratio\":" << score
        << ",\"cosine\":" << cosine << ",\"fit_descriptive_only\":" << fit
        << ",\"orthogonal_fraction\":" << orthogonal
        << ",\"V_wall_mismatch_norm\":" << v_wall
        << ",\"V_x_alias_mismatch_norm\":" << v_alias_x
        << ",\"V_y_alias_mismatch_norm\":" << v_alias_y
        << ",\"W_dynamic_top_mismatch_norm\":" << w_top
        << ",\"W_interior_mismatch_norm\":" << w_interior
        << ",\"phi_action_norm\":" << l2_norm(core(acts.phi))
        << ",\"pressure_action_norm\":" << l2_norm(core(acts.pressure))
        << ",\"alpha_action_norm\":" << l2_norm(core(acts.alpha))
        << ",\"fwad_sum_error\":" << l2_norm(core(analytic-acts.fwad))
        << ",\"fd_error_norms\":[" << l2_norm(core(acts.fd[0]-acts.fwad)) << ','
        << l2_norm(core(acts.fd[1]-acts.fwad)) << ',' << l2_norm(core(acts.fd[2]-acts.fwad)) << ']'
        << ",\"primal_dpy_max\":" << acts.primal_dpy_max
        << ",\"primal_level_mass_max\":" << acts.primal_level_mass_max
        << ",\"primal_velocity_mass_max\":" << acts.primal_velocity_mass_max
        << ",\"primal_alpha_v_max\":" << acts.primal_alpha_v_max
        << ",\"primal_ratio_max\":" << acts.primal_ratio_max
        << ",\"primal_alpha_max\":" << acts.primal_alpha_max
        << ",\"primal_pressure_max\":" << acts.primal_pressure_max
        << ",\"projected_state_max\":" << acts.projected_state_max
        << ",\"units\":\"u,v,w:m/s2; phi:m2/s3; theta:K/s; mu:Pa/s; Jphi/Jp/Jalpha/Jfwad:m/s3; Vp0 and mismatch:m/s2\""
        << ",\"context_constants\":{\"rd\":" << eos.rd << ",\"cv\":" << eos.cv
        << ",\"cp\":" << eos.cp << ",\"p0\":" << eos.p0 << ",\"t0\":" << eos.t0
        << ",\"rdx\":" << eos.geometry.rdx << ",\"rdy\":" << c.rdy
        << ",\"velocity_eps\":" << c.velocity_eps << ",\"a\":" << a
        << ",\"gravity\":" << P0.receipt().gravity << ",\"dx\":" << P0.receipt().dx
        << ",\"dy\":" << P0.receipt().dy << ",\"nx_physical\":" << n
        << ",\"ny_physical\":" << m << ",\"nz\":" << nz << '}'
        << ",\"blocks\":[";
    for(size_t k=0;k<layout.blocks.size();++k) {
        if(k) out << ',';
        const auto& b=layout.blocks[k];const auto slice=[&](const torch::Tensor& x){return x.slice(0,b.start,b.start+b.size);};
        out << "{\"name\":\"" << b.name << "\",\"defect\":" << l2_norm(slice(defect))
            << ",\"reconstruction\":" << l2_norm(slice(reconstruction))
            << ",\"mismatch\":" << l2_norm(slice(mismatch))
            << ",\"closure\":" << l2_norm(slice(closure))
            << ",\"closure_max\":" << closure_max[k]
            << ",\"closure_budget\":" << closure_budget[k]
            << ",\"scaled_closure_max\":" << scaled_closure_max[k]
            << ",\"scaled_closure_budget\":" << scaled_closure_budget[k] << '}';
    }
    out << "],\"artifact\":\"" << "pressure_a5_direction.bin" << "\",\"actions_artifact\":\"pressure_a5_actions.bin\",\"artifact_format\":\"SDIRKA53:u64records;name32,u32itemsize,u32ndim,i64dims4,u64bytes,data\"}";
    std::cout << out.str() << std::endl;
}
}
#endif
