#include "wrf_sdirk3_native_pressure_reference.h"

#ifdef WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_types.h"
#include <cmath>
#include <limits>
#include <utility>

namespace wrf::sdirk3 {
namespace {
torch::Tensor fp64(const torch::Tensor& x) {
    TORCH_CHECK(x.defined() && x.is_cpu() && x.numel()>0 && torch::isfinite(x).all().item<bool>(),
                "native pressure reference requires finite CPU source tensors");
    return x.detach().to(torch::kFloat64).contiguous();
}
void exact_value(const torch::Tensor& x,double value,const char* name) {
    const auto t=fp64(x);
    TORCH_CHECK((t==value).all().item<bool>(),"native pressure reference requires ",name);
}
void same(const torch::Tensor& x,const torch::Tensor& y,const char* name) {
    const auto a=fp64(x), b=fp64(y);
    TORCH_CHECK(a.sizes()==b.sizes() && torch::equal(a,b),"native pressure reference ",name," mismatch");
}
}
NativePressureReference::NativePressureReference(const StateLayout& layout,int64_t nx,int64_t ny,int64_t nz,
                                                   NativePressureProfileReceipt receipt)
    : layout_(layout),nx_(nx),ny_(ny),nz_(nz),n_(nx-1),m_(ny-1),receipt_(std::move(receipt)),
      model_(n_,m_,receipt_.profiles,receipt_.a,receipt_.gravity,receipt_.dx,receipt_.dy),
      adapter_(n_,m_,nz_) {}

NativePressureProfileReceipt derive_native_pressure_profiles(
    const pressure_stage_probe::PressureEosContext& eos,const WRFGridInfo& grid,
    const StateLayout& layout,double a) {
    torch::NoGradGuard guard;
    const auto& geom=eos.geometry;
    TORCH_CHECK(geom.whole_single_rank && geom.packed_periodic_x_symmetric_y,
                "native pressure reference requires verified whole single-rank packed periodic-X/symmetric-Y tile");
    TORCH_CHECK(std::isfinite(geom.rdx) && geom.rdx>0 && std::isfinite(geom.rdy) && geom.rdy>0 &&
                std::isfinite(a) && a>0,"native pressure reference requires actual positive FP32 reciprocal spacing and a");
    exact_value(geom.msftx,1,"unit msftx"); exact_value(geom.msfty,1,"unit msfty");
    exact_value(geom.msfux,1,"unit msfux"); exact_value(geom.msfuy,1,"unit msfuy");
    exact_value(geom.msfvx,1,"unit msfvx"); exact_value(geom.msfvy,1,"unit msfvy");
    TORCH_CHECK(eos.captured && !eos.duplicate && !eos.failed,
                "native pressure reference requires one live EOS capture");
    const auto* g=&grid;
    TORCH_CHECK(g->nx>=3 && g->ny>=3 && g->nz>=2 && g->nx_u==g->nx+1 &&
                g->ny_v==g->ny+1 && g->nz_w==g->nz+1,
                "native pressure reference requires packed native stagger dimensions");
    const auto nx=g->nx, ny=g->ny, nz=g->nz, n=nx-1, m=ny-1;
    TORCH_CHECK(geom.msftx.sizes()==torch::IntArrayRef({ny,nx}) &&
                geom.msfty.sizes()==torch::IntArrayRef({ny,nx}) &&
                geom.msfux.sizes()==torch::IntArrayRef({ny,nx+1}) &&
                geom.msfuy.sizes()==torch::IntArrayRef({ny,nx+1}) &&
                geom.msfvx.sizes()==torch::IntArrayRef({ny+1,nx}) &&
                geom.msfvy.sizes()==torch::IntArrayRef({ny+1,nx}),
                "native pressure reference map staggering mismatch");
    const auto ground=fp64(geom.ground_phi);
    TORCH_CHECK(ground.sizes()==torch::IntArrayRef({ny,nx}),
                "native pressure reference invalid captured ground shape");
    const auto ground_core=ground.narrow(0,0,m).narrow(1,0,n);
    TORCH_CHECK(torch::equal(ground_core,torch::full_like(ground_core,ground_core.flatten()[0].item<double>())),
                "native pressure reference requires flat captured ground geopotential");
    // Effective boundary flags are attested at the live tile EOS call above;
    // WRFGridInfo's symmetry flags are not synchronized by the boundary setter.
    TORCH_CHECK(g->its==g->ids && g->ite==g->ide && g->jts==g->jds && g->jte==g->jde &&
                nx==g->ide-g->ids+1 && ny==g->jde-g->jds+1,
                "native pressure reference grid metadata do not identify the packed physical core");
    const auto expected=StateLayout::from_grid_dims(nx,ny,nz,g->nx_u,g->ny_v,g->nz_w);
    TORCH_CHECK(layout.is_valid() && layout.is_exact && layout.total_size==expected.total_size &&
                layout.blocks.size()==expected.blocks.size(),"native pressure reference invalid layout");
    for(size_t k=0;k<layout.blocks.size();++k)
        TORCH_CHECK(layout.blocks[k].name==expected.blocks[k].name &&
                    layout.blocks[k].start==expected.blocks[k].start &&
                    layout.blocks[k].size==expected.blocks[k].size,"native pressure reference layout mismatch");
    const auto p=fp64(eos.p_pert)+fp64(eos.p_base), alpha=fp64(eos.alt);
    const auto tpert=fp64(eos.t), theta=tpert+static_cast<double>(eos.t0);
    const auto mu=fp64(eos.mu), mub=fp64(eos.mu_base);
    const auto c1=fp64(eos.c1h).flatten(), c2=fp64(eos.c2h).flatten();
    const auto rnw=fp64(eos.rdnw).flatten();
    const auto rdn=fp64(geom.rdn).flatten(), c1f=fp64(geom.c1f).flatten(), c2f=fp64(geom.c2f).flatten();
    TORCH_CHECK(p.sizes()==torch::IntArrayRef({ny,nz,nx}) && alpha.sizes()==p.sizes() &&
                theta.sizes()==p.sizes() && mu.sizes()==torch::IntArrayRef({ny,nx}) && mub.sizes()==mu.sizes() &&
                c1.numel()>=nz && c2.numel()>=nz && rnw.numel()>=nz && rdn.numel()>=nz &&
                c1f.numel()>=nz+1 && c2f.numel()>=nz+1,
                "native pressure reference EOS/vertical shapes invalid");
    same(eos.mu_base,g->mu_base,"live/raw base mass");
    same(eos.p_base,g->p_base,"live/raw base pressure");
    const auto grid_c1=fp64(g->c1h).flatten(), grid_c2=fp64(g->c2h).flatten();
    TORCH_CHECK(grid_c1.numel()>=nz && grid_c2.numel()>=nz,"native pressure reference raw hybrid levels missing");
    same(c1.slice(0,0,nz),grid_c1.slice(0,0,nz),"live/raw c1h");
    same(c2.slice(0,0,nz),grid_c2.slice(0,0,nz),"live/raw c2h");
    const auto grid_rnw=fp64(g->rdnw).flatten();
    TORCH_CHECK(grid_rnw.numel()>=nz && torch::equal(rnw.slice(0,0,nz),grid_rnw.slice(0,0,nz)),
                "native pressure reference live/raw rdnw mismatch");
    const auto grid_rdn=fp64(g->rdn).flatten(),grid_c1f=fp64(g->c1f).flatten(),grid_c2f=fp64(g->c2f).flatten();
    TORCH_CHECK(grid_rdn.numel()>=nz && grid_c1f.numel()>=nz+1 && grid_c2f.numel()>=nz+1,
                "native pressure reference raw vertical coefficients missing");
    same(rdn.slice(0,0,nz),grid_rdn.slice(0,0,nz),"live/raw rdn");
    same(c1f.slice(0,0,nz+1),grid_c1f.slice(0,0,nz+1),"live/raw c1f");
    same(c2f.slice(0,0,nz+1),grid_c2f.slice(0,0,nz+1),"live/raw c2f");
    TORCH_CHECK(std::isfinite(eos.cp) && eos.cp>0 && std::isfinite(eos.cv) && eos.cv>0 &&
                std::isfinite(eos.t0) && eos.t0>0 && std::isfinite(geom.gravity) && geom.gravity>0 &&
                geom.gravity==g->g,
                "native pressure reference invalid physical constants");
    const double kappa=static_cast<double>(eos.cp)/eos.cv;
    const double eps=static_cast<double>(1.0e-10f); // RHS theta denominator in FP64
    NativePressureProfileReceipt r;
    r.nx_physical=n; r.ny_physical=m; r.nz=nz; r.a=a; r.gravity=geom.gravity;
    r.rdx=geom.rdx; r.rdy=geom.rdy; r.dx=1.0/static_cast<double>(geom.rdx); r.dy=1.0/static_cast<double>(geom.rdy);
    auto& q=r.profiles;
    for(auto* vec:{&q.q,&q.etheta,&q.emu,&q.gz_edge,&q.alpha,&q.beta,&q.H,&q.nu,&q.dnw,
                   &r.p_ref,&r.alpha_ref,&r.theta_ref}) vec->resize(nz);
    const auto pp=p.accessor<double,3>(), aa=alpha.accessor<double,3>(), tt=theta.accessor<double,3>();
    const auto tp=tpert.accessor<double,3>();
    const auto mm=mu.accessor<double,2>(), mb=mub.accessor<double,2>();
    const auto v1=c1.accessor<double,1>(),v2=c2.accessor<double,1>(),zr=rnw.accessor<double,1>();
    const auto vf1=c1f.accessor<double,1>(),vf2=c2f.accessor<double,1>(),vr=rdn.accessor<double,1>();
    // Fixed j,i traversal makes means reproducible and excludes packed aliases.
    for(int64_t k=0;k<nz;++k) {
        TORCH_CHECK(zr[k]>0,"native pressure reference requires positive live rdnw");
        double ps=0,as=0,ts=0,qs=0,ets=0,ems=0,hs=0,nus=0,edges=0;
        for(int64_t j=0;j<m;++j) for(int64_t i=0;i<n;++i) {
            const double M=mb[j][i]+mm[j][i], H=v1[k]*M+v2[k];
            const double pk=pp[j][k][i], ak=aa[j][k][i], T=tt[j][k][i];
            const int64_t wk=k+1;
            const double L=vf1[wk]*M+vf2[wk];
            const double r=wk==nz ? 2.0*zr[nz-1] : vr[wk];
            TORCH_CHECK(std::isfinite(M) && M>0 && std::isfinite(H) && H>0 &&
                        std::isfinite(pk) && pk>0 && std::isfinite(ak) && ak>0 &&
                        std::isfinite(T) && T>0 && std::isfinite(L) && L>0 && std::isfinite(r) && r>0,
                        "native pressure reference nonphysical EOS/vertical state");
            ps+=pk; as+=ak; ts+=T; qs+=kappa*pk*zr[k]/(ak*H);
            ets+=kappa*pk/T; ems+=kappa*pk*v1[k]/H; hs+=H;
            nus+=tp[j][k][i]*v1[k]/(H+eps); edges+=geom.gravity*r/L;
        }
        const double count=static_cast<double>(m)*static_cast<double>(n);
        r.p_ref[k]=static_cast<double>(ps/count); r.alpha_ref[k]=static_cast<double>(as/count);
        r.theta_ref[k]=static_cast<double>(ts/count);
        q.q[k]=static_cast<double>(qs/count); q.etheta[k]=static_cast<double>(ets/count);
        q.emu[k]=static_cast<double>(ems/count); q.H[k]=static_cast<double>(hs/count);
        q.nu[k]=static_cast<double>(nus/count); q.gz_edge[k]=static_cast<double>(edges/count);
        q.alpha[k]=r.alpha_ref[k]; q.beta[k]=kappa*r.p_ref[k]*eos.t0/r.theta_ref[k];
        q.dnw[k]=-1.0/zr[k];
    }
    return r;
}

std::unique_ptr<NativePressureReference> NativePressureReference::create(
    const pressure_stage_probe::PressureEosContext& eos,const UnifiedPreconditioner& raw,
    const StateLayout& layout,const torch::Tensor& U_eval,double a) {
    TORCH_CHECK(raw.raw_principal_enabled(),"native pressure reference requires bound raw preconditioner");
    const auto g=raw.pressure_probe_grid();
    TORCH_CHECK(g && U_eval.defined() && U_eval.is_cpu() && U_eval.scalar_type()==torch::kFloat64 &&
                U_eval.dim()==1 && U_eval.numel()==layout.total_size && torch::isfinite(U_eval).all().item<bool>(),
                "native pressure reference requires exact FP64 stage state");
    (void)raw.pressure_probe_column((g->ny-1)/2,(g->nx-1)/2,U_eval);
    auto receipt=derive_native_pressure_profiles(eos,*g,layout,a);
    const auto block=[&](int k,torch::IntArrayRef shape) {
        const auto& b=layout.blocks[k]; return U_eval.slice(0,b.start,b.start+b.size).view(shape);
    };
    const int64_t n=g->nx-1,m=g->ny-1;
    const auto core3=[&](const torch::Tensor& x) { return fp64(x).narrow(0,0,m).narrow(2,0,n); };
    const auto core2=[&](const torch::Tensor& x) { return fp64(x).narrow(0,0,m).narrow(1,0,n); };
    same(core3(eos.ph),core3(block(3,{g->ny,g->nz_w,g->nx})),"captured/Ueval physical phi");
    same(core3(eos.t),core3(block(4,{g->ny,g->nz,g->nx})),"captured/Ueval physical theta");
    same(core2(eos.mu),core2(block(5,{g->ny,g->nx})),"captured/Ueval physical mu");
    return std::unique_ptr<NativePressureReference>(
        new NativePressureReference(layout,g->nx,g->ny,g->nz,std::move(receipt)));
}

torch::Tensor NativePressureReference::apply(const torch::Tensor& rhs) const {
    TORCH_CHECK(rhs.defined() && rhs.is_cpu() && rhs.scalar_type()==torch::kFloat64 &&
                rhs.dim()==1 && rhs.numel()==layout_.total_size && torch::isfinite(rhs).all().item<bool>(),
                "native pressure reference packed input must be finite CPU FP64");
    return adapter_.solve(model_,rhs);
}
}
#endif
