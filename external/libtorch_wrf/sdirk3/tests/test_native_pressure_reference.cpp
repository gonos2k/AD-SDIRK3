#include "../wrf_sdirk3_native_pressure_reference.h"
#include "../wrf_sdirk3_types.h"
#include "../wrf_hydrostatic_pressure.h"
#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_unified_rhs.h"
#include "../wrf_sdirk3_config.h"
#include <cmath>
#include <iostream>

using namespace wrf::sdirk3;
int main() {
    constexpr int nx=4,ny=4,nz=2,nw=3,n=nx-1,m=ny-1;
    auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    auto grid=std::make_shared<WRFGridInfo>(); auto& g=*grid;
    g.nx=nx;g.ny=ny;g.nz=nz;g.nx_u=nx+1;g.ny_v=ny+1;g.nz_w=nw;
    g.ids=g.its=g.jds=g.jts=1;g.ide=g.ite=nx;g.jde=g.jte=ny;
    g.periodic_x=true;g.periodic_y=false;g.symmetric_ys=true;g.symmetric_ye=true;
    g.p_base=torch::full({ny,nz,nx},80000.0,opts);g.mu_base=torch::full({ny,nx},80000.0,opts);
    g.c1h=torch::tensor({1.0,0.5},opts);g.c2h=torch::tensor({0.0,40000.0},opts);
    g.c1f=torch::tensor({1.0,0.8,0.5},opts);g.c2f=torch::tensor({0.0,16000.0,40000.0},opts);
    g.rdnw=torch::tensor({2.0,2.0},opts);g.rdn=torch::tensor({2.0,2.0},opts);
    pressure_stage_probe::PressureEosContext e;
    e.captured=true;e.rd=287.0f;e.cv=717.5f;e.cp=1004.5f;e.p0=100000.0f;e.t0=300.0f;
    e.ph=torch::zeros({ny,nw,nx},opts);e.t=torch::zeros({ny,nz,nx},opts);
    e.mu=torch::zeros({ny,nx},opts);e.mu_base=g.mu_base.clone();e.p_base=g.p_base.clone();
    e.c1h=g.c1h.clone();e.c2h=g.c2h.clone();e.rdnw=g.rdnw.clone();
    e.alb=torch::full({ny,nz,nx},1.0,opts);
    g.alb=e.alb.clone();
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        e.mu[j][i]=100+j*10+i;
        for(int k=0;k<nz;++k) e.t[j][k][i]=5+j+0.1*i+k;
    }
    const auto eos=calc_p_rho_wrf(e.ph,e.t,e.mu,e.mu_base,e.alb,e.p_base,e.rdnw,e.c1h,e.c2h,
                                  e.rd,e.cv,e.cp,e.p0,e.t0);
    e.alt=eos.alt;e.p_pert=eos.p_pert;
    auto& geom=e.geometry;geom.whole_single_rank=true;geom.packed_periodic_x_symmetric_y=true;
    geom.rdx=1.0f/100000.0f;geom.rdy=1.0f/100000.0f;geom.gravity=g.g;
    geom.c1f=g.c1f;geom.c2f=g.c2f;geom.rdn=g.rdn;
    geom.msftx=geom.msfty=torch::ones({ny,nx},opts);
    geom.msfux=geom.msfuy=torch::ones({ny,nx+1},opts);
    geom.msfvx=geom.msfvy=torch::ones({ny+1,nx},opts);
    geom.ground_phi=torch::zeros({ny,nx},opts);
    const auto layout=StateLayout::from_grid_dims(nx,ny,nz,nx+1,ny+1,nw);
    const double a=600.0*(1767732205903.0/4055673282236.0);
    const auto result=derive_native_pressure_profiles(e,g,layout,a);
    TORCH_CHECK(result.nx_physical==n && result.ny_physical==m && result.nz==nz &&
                result.dx==1.0/static_cast<double>(geom.rdx),"profile geometry mismatch");
    // Independent explicit arithmetic mean of one nonuniform live coefficient.
    double qsum=0, psum=0;
    const double kap=static_cast<double>(e.cp)/e.cv;
    for(int j=0;j<m;++j) for(int i=0;i<n;++i) {
        const double p=e.p_pert[j][0][i].item<double>()+80000.0;
        const double alpha=e.alt[j][0][i].item<double>();
        const double H=80000.0+e.mu[j][i].item<double>();
        qsum+=kap*p*2.0/(alpha*H); psum+=p;
    }
    TORCH_CHECK(std::abs(result.profiles.q[0]-qsum/(m*n))<1.e-14 &&
                std::abs(result.p_ref[0]-psum/(m*n))<1.e-10,"profile mean mismatch");
    // Packed endpoint aliases deliberately contain extreme but finite values:
    // no profile may include them.
    auto changed=e;
    changed.p_pert=e.p_pert.clone();changed.t=e.t.clone();changed.alt=e.alt.clone();changed.mu=e.mu.clone();
    for(auto* x:{&changed.p_pert,&changed.t,&changed.alt}) {
        x->select(0,ny-1).fill_(1.e6);x->select(2,nx-1).fill_(1.e6);
    }
    changed.mu.select(0,ny-1).fill_(1.e6);changed.mu.select(1,nx-1).fill_(1.e6);
    const auto no_alias=derive_native_pressure_profiles(changed,g,layout,a);
    TORCH_CHECK(result.profiles.q==no_alias.profiles.q && result.profiles.etheta==no_alias.profiles.etheta &&
                result.profiles.emu==no_alias.profiles.emu && result.profiles.gz_edge==no_alias.profiles.gz_edge &&
                result.profiles.H==no_alias.profiles.H && result.profiles.nu==no_alias.profiles.nu &&
                result.p_ref==no_alias.p_ref && result.alpha_ref==no_alias.alpha_ref && result.theta_ref==no_alias.theta_ref,
                "packed alias changed physical-core reference averages");
    auto bad=e;bad.geometry.msftx=geom.msftx.clone();bad.geometry.msftx[0][0]=1.01;
    bool rejected=false;try { (void)derive_native_pressure_profiles(bad,g,layout,a); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"nonunit map accepted");
    bad=e;bad.geometry.ground_phi=geom.ground_phi.clone();bad.geometry.ground_phi[0][0]=1;
    rejected=false;try { (void)derive_native_pressure_profiles(bad,g,layout,a); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"nonflat ground accepted");
    bad=e;bad.geometry.whole_single_rank=false;
    rejected=false;try { (void)derive_native_pressure_profiles(bad,g,layout,a); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"unverified rank/whole-tile context accepted");
    auto& cfg=g_sdirk3_config;
    cfg=SDIRK3Config{}; cfg.imex_split_mode=3;cfg.precond_type=2;cfg.precond_acoustic_4x4=1;
    auto physics=std::make_shared<PhysicsConfig>();
    UnifiedPreconditioner P(grid,physics,600.0f,0.4358665215f);
    TORCH_CHECK(P.raw_principal_enabled(),"test raw principal selection failed");
    auto state=torch::zeros({layout.total_size},opts);
    const auto block=[&](torch::Tensor x,int k,torch::IntArrayRef shape) {
        const auto& b=layout.blocks[k];return x.slice(0,b.start,b.start+b.size).view(shape);
    };
    block(state,3,{ny,nw,nx}).copy_(e.ph);
    block(state,4,{ny,nz,nx}).copy_(e.t);
    block(state,5,{ny,nx}).copy_(e.mu);
    P.bind_raw_principal_state_or_throw(state,2,"reference factory test");
    P.update_time_coefficients(600.0f,0.4358665215f);
    const auto raw_digest=P.pressure_probe_raw_digest();
    TORCH_CHECK(raw_digest==P.pressure_probe_raw_digest(),"raw-cache digest changed without mutation");
    auto adapter=NativePressureReference::create(e,P,layout,state,a);
    auto rhs=torch::zeros_like(state);
    const auto bottom=2.0+0.2*torch::arange(m,opts).view({m,1,1})+
        0.1*torch::arange(n,opts).view({1,1,n});
    block(rhs,3,{ny,nw,nx}).narrow(0,0,m).narrow(1,0,1).narrow(2,0,n).copy_(bottom);
    block(rhs,0,{ny,nz,nx+1})[ny-1][0][0]=3.0; // nonzero projection complement
    block(rhs,1,{ny+1,nz,nx})[0][0][0]=4.0; // y-wall identity
    const auto output=adapter->apply(rhs);
    PressurePackedAdapter packed(n,m,nz);
    const auto complement_error=(output-packed.project(output))-(rhs-packed.project(rhs));
    TORCH_CHECK(torch::equal(block(output,3,{ny,nw,nx}).narrow(0,0,m).narrow(1,0,1).narrow(2,0,n),bottom) &&
                block(output,1,{ny+1,nz,nx})[0][0][0].item<double>()==4.0 &&
                complement_error.abs().max().item<double>()<1.e-12,
                "packed Phi0, wall or projection-complement identity failed");
    const auto& receipt=adapter->receipt();
    ReferencePressureModel physical(n,m,receipt.profiles,a,receipt.gravity,receipt.dx,receipt.dy);
    auto zero3=torch::zeros({m,nz,n},opts),zero_v=torch::zeros({m-1,nz,n},opts),zero_mu=torch::zeros({m,n},opts);
    const auto expected=physical.solve({zero3,zero_v,zero3,-bottom.expand({m,nz,n}),zero3,zero_mu});
    const auto actual_w=block(output,2,{ny,nw,nx}).narrow(0,0,m).narrow(1,1,nz).narrow(2,0,n);
    const auto actual_phi=block(output,3,{ny,nw,nx}).narrow(0,0,m).narrow(1,1,nz).narrow(2,0,n);
    TORCH_CHECK(expected.w.abs().max().item<double>()>0 &&
                (actual_w-expected.w).abs().max().item<double>()<1.e-12 &&
                (actual_phi-(expected.phi+bottom)).abs().max().item<double>()<1.e-12,
                "known nonzero Phi0 was not coupled into reference pressure solve");
    auto changed_state=state.clone();
    block(changed_state,5,{ny,nx})[0][0]+=1.0;
    P.bind_raw_principal_state_or_throw(changed_state,2,"changed reference factory test");
    P.update_time_coefficients(600.0f,0.4358665215f);
    TORCH_CHECK(P.pressure_probe_raw_digest()!=raw_digest,"raw-cache digest missed a changed binding");
    std::cout << "NATIVE_PRESSURE_REFERENCE_PROFILE core=" << n << 'x' << m
              << " levels=" << nz << " q0=" << result.profiles.q[0]
              << " p0=" << result.p_ref[0] << " alias_invariant=1\n";
}
