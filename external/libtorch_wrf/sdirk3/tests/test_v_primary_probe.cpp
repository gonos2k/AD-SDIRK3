#include "../wrf_sdirk3_v_primary_probe.h"
#include "../wrf_sdirk3_pressure_stage_probe.h"
#include "../wrf_sdirk3_pressure_packed_adapter.h"
#include "../wrf_sdirk3_acoustic_substep.h"
#include "../wrf_sdirk3_ww_cp.h"
#include "../wrf_hydrostatic_pressure.h"
#include <cmath>
#include <iostream>

using namespace wrf::sdirk3;
int main() {
    torch::set_num_threads(1);
    constexpr int nx=4,ny=4,nz=2,nw=3,n=nx-1,m=ny-1,nv=ny+1;
    const auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    const auto layout=StateLayout::from_grid_dims(nx,ny,nz,nx+1,nv,nw);
    PressurePackedAdapter adapter(n,m,nz);
    const auto block=[&](const torch::Tensor& x,int k,torch::IntArrayRef shape) {
        const auto& b=layout.blocks[k];return x.slice(0,b.start,b.start+b.size).view(shape);
    };
    // Qhat handles the horizontal aliases. Native Q differs at the two
    // vertical bottom rows: it preserves and extends their physical values.
    const auto project=[&](const torch::Tensor& x) {
        const auto q=adapter.project(x);
        const auto mass_extend=[&](const torch::Tensor& f) {
            const auto core=f.slice(0,0,m).slice(2,0,n);
            const auto seam=torch::cat({core,core.slice(2,0,1)},2);
            return torch::cat({seam,seam.slice(0,m-1,m)},0);
        };
        const auto w=mass_extend(block(x,2,{ny,nw,nx}));
        const auto ph=mass_extend(block(x,3,{ny,nw,nx}));
        return torch::cat({q.slice(0,0,layout.blocks[2].start),w.flatten(),ph.flatten(),
                           q.slice(0,layout.blocks[4].start,layout.total_size)});
    };
    auto U=torch::zeros({layout.total_size},opts);
    auto ph=block(U,3,{ny,nw,nx}),t=block(U,4,{ny,nz,nx}),mu=block(U,5,{ny,nx});
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        mu[j][i]=200+5*j+3*i;
        for(int k=0;k<nw;++k) ph[j][k][i]=10.0*k+0.4*j+0.3*i;
        for(int k=0;k<nz;++k) t[j][k][i]=8.0+0.3*j+0.2*i+k;
    }
    pressure_stage_probe::PressureEosContext e;
    e.captured=true;e.rd=287;e.cv=717.5f;e.cp=1004.5f;e.p0=100000;e.t0=300;
    e.mu_base=torch::full({ny,nx},80000.0,opts);
    e.alb=torch::full({ny,nz,nx},1.0,opts);
    e.p_base=torch::empty({ny,nz,nx},opts);
    for(int j=0;j<ny;++j) e.p_base.select(0,j).fill_(80000+100*j);
    e.c1h=torch::tensor({1.0,0.5},opts);e.c2h=torch::tensor({0.0,40000.0},opts);
    e.rdnw=torch::tensor({2.0,2.0},opts);
    e.geometry.whole_single_rank=true;e.geometry.packed_periodic_x_symmetric_y=true;
    e.geometry.rdy=1.0f/100000.0f;
    e.geometry.msfvx=e.geometry.msfvy=torch::ones({nv,nx},opts);
    const auto projected=project(U);
    e.ph=block(projected,3,{ny,nw,nx}).clone();e.t=block(projected,4,{ny,nz,nx}).clone();
    e.mu=block(projected,5,{ny,nx}).clone();
    const auto eos=calc_p_rho_wrf(e.ph,e.t,e.mu,e.mu_base,e.alb,e.p_base,e.rdnw,e.c1h,e.c2h,
                                  e.rd,e.cv,e.cp,e.p0,e.t0);
    e.alt=eos.alt;e.p_pert=eos.p_pert;
    pressure_stage_probe::PressureVPrimaryContext c;
    c.captured=true;c.canonical_packed_dry=true;c.base_state_branch=true;c.use_wrf_mass_flux=true;
    c.rdy=e.geometry.rdy;c.velocity_eps=1.0e-10f;
    c.cqv_at_v=torch::ones({nv,nz,nx},opts);c.project_native=project;
    const auto avg=[](const torch::Tensor& x) {
        return torch::cat({x.slice(0,0,1),0.5*(x.slice(0,0,x.size(0)-1)+x.slice(0,1,x.size(0))),
                           x.slice(0,x.size(0)-1,x.size(0))},0);
    };
    c.avg_y_3d=avg;c.avg_y_2d=avg;
    int calls=0;bool saw_changed_mass=false;const auto base_mass=e.mu+e.mu_base;
    c.level_mass_v_of=[&](const torch::Tensor& mass) {
        ++calls; if(!torch::equal(mass.detach(),base_mass)) saw_changed_mass=true;
        auto core=mass.slice(0,0,m).slice(1,0,n);
        auto mv=stagger_wrf_mass_field(core,0,WWCPBoundaryPolicy::SymmetricReplicate);
        auto q=e.c1h.view({1,nz,1})*mv.unsqueeze(1)+e.c2h.view({1,nz,1});
        auto x=torch::cat({q,q.slice(2,0,1)},2);
        return torch::cat({x,x.slice(0,m-1,m)},0);
    };
    c.level_mass_v=c.level_mass_v_of(base_mass).clone();
    c.velocity_mass_v=(avg(e.mu)+avg(e.mu_base)+c.velocity_eps).unsqueeze(1).expand({nv,nz,nx}).clone();
    c.alpha_v=c.level_mass_v.clone();c.msf_ratio_v=torch::ones_like(c.level_mass_v);
    const auto yd=[](const torch::Tensor& x) {
        auto z=torch::zeros_like(x.slice(0,0,1));
        return torch::cat({z,x.slice(0,1,x.size(0))-x.slice(0,0,x.size(0)-1),z},0);
    };
    const auto dph=yd(e.ph.slice(1,1,nw))+yd(e.ph.slice(1,0,nz));
    c.dpy_primary=acoustic::horizontal_pgf_primary(c.rdy,c.level_mass_v,dph,
        2.0f*avg(e.alt),yd(e.p_pert),2.0f*avg(eos.al_pert),yd(e.p_base)).clone();
    torch::manual_seed(23);auto z=torch::randn_like(U);
    block(z,3,{ny,nw,nx}).select(1,0).fill_(2.0); // native Q must retain Phi0
    auto out=v_primary_probe::evaluate(U,z,layout,e,c);
    const auto sum=out.phi+out.pressure+out.alpha;
    const auto core=[&](const torch::Tensor& x){return x.slice(0,1,m).slice(2,0,n);};
    const double ad=(core(sum-out.fwad)).norm().item<double>();
    const double scale=std::max(core(out.fwad).norm().item<double>(),1.e-300);
    TORCH_CHECK(ad/scale<1.e-9 && calls>=8 && saw_changed_mass,
                "isolated source-chain FWAD mismatch or frozen mass callback");
    int fd_passes=0;
    for(const auto& fd:out.fd)
        fd_passes+=((core(fd-out.fwad).abs()) <= (1.e-12+1.e-5*core(out.fwad).abs())).all().item<bool>();
    TORCH_CHECK(fd_passes>=2,"isolated nonuniform source-chain FD widths failed");
    TORCH_CHECK(core(out.phi).norm().item<double>()>0 && core(out.pressure).norm().item<double>()>0 &&
                core(out.alpha).norm().item<double>()>0,"isolated V-primary test lacks active terms");
    auto z_no_bottom=z.clone();block(z_no_bottom,3,{ny,nw,nx}).select(1,0).zero_();
    auto no_bottom=v_primary_probe::evaluate(U,z_no_bottom,layout,e,c);
    TORCH_CHECK(core(out.fwad-no_bottom.fwad).norm().item<double>()>1.e-12,
                "native projection silently dropped physical Phi0 direction");
    auto wrong_q=c;
    wrong_q.project_native=[&](const torch::Tensor& x) { return adapter.project(x); };
    bool rejected=false;
    try { (void)v_primary_probe::evaluate(U,z,layout,e,wrong_q); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"mismatched Qhat-only base projection accepted");
    auto wet=c;wet.cqv_at_v=c.cqv_at_v.clone();wet.cqv_at_v[1][0][0]=0.9;
    rejected=false;
    try { (void)v_primary_probe::evaluate(U,z,layout,e,wet); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"non-unit physical interior cqv accepted as dry");
    std::cout << "V_PRIMARY_KERNEL ad_relative=" << ad/scale
              << " fd_passes=" << fd_passes << " mass_calls=" << calls
              << " changed_mass=1 bottom_active=1\n";
}
