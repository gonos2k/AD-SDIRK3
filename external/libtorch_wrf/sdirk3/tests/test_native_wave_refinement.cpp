// Dynamic-grid native-W wave refinement gate.  This intentionally owns a tiny
// fixture rather than changing the fixed 8x6x4 fixtures used by older tests.
#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_hydrostatic_balance.h"
#include "../wrf_hydrostatic_pressure.h"
#include <torch/torch.h>
#include <torch/version.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

extern "C" void sdirk3_set_timestep_i4(int*);

namespace {
using namespace wrf::sdirk3;
struct RhsTag { using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode); friend type access(RhsTag); };
struct PhiTag { using type = torch::Tensor TileSDIRK3UnifiedSolver::*; friend type access(PhiTag); };
struct DtTag { using type = float TileSDIRK3UnifiedSolver::*; friend type access(DtTag); };
struct RefTag { using type = torch::Tensor TileSDIRK3UnifiedSolver::*; friend type access(RefTag); };
struct NewtonSolverTag {
    using type = std::unique_ptr<wrf::sdirk3::WRFNewtonKrylovSolver>
        TileSDIRK3UnifiedSolver::*;
    friend type access(NewtonSolverTag);
};
template<class Tag, typename Tag::type Member> struct Accessor { friend typename Tag::type access(Tag) { return Member; } };
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<PhiTag, &TileSDIRK3UnifiedSolver::ph_base_>;
template struct Accessor<DtTag, &TileSDIRK3UnifiedSolver::dt_stage_>;
template struct Accessor<RefTag, &TileSDIRK3UnifiedSolver::U_ref_stage_>;
template struct Accessor<NewtonSolverTag, &TileSDIRK3UnifiedSolver::newton_solver_>;

constexpr double pi = 3.1415926535897932384626433832795;
void configure(bool implicit_divergence=false,float kdamp=0.0f) {
    auto& c=g_sdirk3_config;
    c=SDIRK3Config{}; c.imex_split_mode=3; c.mass_coordinate_mode=1;
    c.imex_slow_in_tangent=true; c.use_autograd=true; c.non_hydrostatic=true;
    c.internal_fp64=true; c.internal_fp64_state_carry=true; c.retain_graph_for_adjoint=true;
    c.do_curvature=true; c.buoyancy_use_current_w=true; c.omega_w_blend=1.0f;
    c.diffusion_option=2; c.khdif=0.0f; c.kvdif=0.0f; c.precond_type=0;
    c.n_threads=1; c.max_newton_iter=40; c.newton_tol=1.0e-10f; c.newton_rtol=0.0f;
    c.ewt_rtol=1.0e-6f; c.krylov_tol=1.0e-8f; c.stage_fail_action=1;
    c.advection_order=2; c.gmres_warmstart=false; c.inn_warmstart_enable=false;
    c.implicit_divergence=implicit_divergence; c.kdamp=kdamp;
    c.stage_damp_rel_threshold=1.0e6f;
}
struct Grid {
    int nx, ny, nz, nu, nv, nw;
    double lx=40000.0, ly=30000.0;
    float dx,dy;
    std::vector<float> u,v,w,ph,th,mu, ru,rv,rw,rph,rth,rmu;
    std::vector<float> pbase, thbase, phbase, mubase, rho, metric, one, zero, half;
    std::vector<float> c1f,c2f,c1h,c2h;
    std::vector<float> maps, f, e, setup_state, setup_density;
    TileSDIRK3UnifiedSolver solver;

    Grid(int x, int y, int z)
      : nx(x),ny(y),nz(z),nu(x+1),nv(y+1),nw(z+1),
        dx(static_cast<float>(lx/x)),dy(static_cast<float>(ly/y)),
        u(ny*nz*nu),v(nv*nz*nx),w(ny*nw*nx),ph(ny*nw*nx),
        th(ny*nz*nx),mu(ny*nx),ru(u.size()),rv(v.size()),rw(w.size()),
        rph(ph.size()),rth(th.size()),rmu(mu.size()),pbase(ny*nz*nx),
        thbase(pbase.size()),phbase(ph.size()),mubase(mu.size(),80000.0f),
        rho(nv*nw*nu,1.0f),metric(nw,-static_cast<float>(z)),
        one(nw,1.0f),zero(nw,0.0f),half(nw,0.5f),c1f(nw,1.0f),c2f(nw,0.0f),
        c1h(nw,1.0f),c2h(nw,0.0f),maps((nv+1)*(nu+1),1.0f),
        f(maps.size(),0.0f),e(maps.size(),0.0f),
        setup_state(nv*nw*nu,0.0f),setup_density(nv*nw*nu,1.0f),
        solver(x,y,z,dx,dy,{1.0f/dx},{1.0f/dy},metric,0) {
        solver.setWRFIndices(1,nx+1,1,ny+1,1,nz,
                             1,nx+1,1,ny+1,1,nw,
                             -2,nx+4,-2,ny+4,1,nw);
        solver.setBoundaryConditions(true,false,false,false,true,true,false,false,false,false);
        const auto eta_mid=torch::empty({nz},torch::kFloat32);
        for(int k=0;k<nz;++k) {
            const double eta=1.0-(k+0.5)/nz;
            eta_mid[k]=static_cast<float>(eta);
            const float p=static_cast<float>(20000.0+80000.0*eta);
            for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
                const auto q=(j*nz+k)*nx+i;
                pbase[q]=p; thbase[q]=0.0f;
            }
        }
        std::vector<float> pcol(nz), eta_metric(nz,-static_cast<float>(nz));
        for(int k=0;k<nz;++k) pcol[k]=pbase[k*nx];
        const auto pten=torch::from_blob(pcol.data(),{nz},torch::kFloat32).clone();
        const auto tten=torch::full({nz},300.0f,torch::kFloat32);
        const auto alpha=compute_inverse_density(tten,pten,287.0f,717.5f,1004.5f,100000.0f);
        const auto phi=integrate_phb_hydrostatic(eta_metric,
            std::vector<float>(alpha.data_ptr<float>(),alpha.data_ptr<float>()+nz),
            std::vector<float>(nz,1.0f),std::vector<float>(nz,0.0f),80000.0f,0.0f);
        for(int j=0;j<ny;++j) for(int k=0;k<nw;++k) for(int i=0;i<nx;++i)
            phbase[(j*nw+k)*nx+i]=phi[k];
        solver.setBaseState(pbase.data(),thbase.data(),phbase.data(),mubase.data());
        for(int j=0;j<nv;++j) for(int k=0;k<nw;++k) for(int i=0;i<nu;++i) {
            const float base_alpha=alpha[std::min(k,nz-1)].item<float>();
            rho[(j*nw+k)*nu+i]=base_alpha;
            setup_density[(j*nw+k)*nu+i]=base_alpha;
        }
        ZeroCopyConfig setup{};
        setup.nx=nx; setup.ny=ny; setup.nz=nz; setup.nx_u=nu; setup.ny_v=nv; setup.nz_w=nw;
        setup.ids=setup.its=setup.ims=1; setup.ide=setup.ite=setup.ime=nu;
        setup.jds=setup.jts=setup.jms=1; setup.jde=setup.jte=setup.jme=nv;
        setup.kds=setup.kts=setup.kms=1; setup.kde=setup.kme=nw; setup.kte=nz;
        setup.u_ptr=setup.v_ptr=setup.w_ptr=setup.ph_ptr=setup.t_ptr=setup.p_ptr=setup_state.data();
        setup.mu_ptr=setup_state.data(); setup.al_ptr=setup_density.data();
        setup.kdamp=g_sdirk3_config.kdamp;
        setup.rdx=1.0f/dx; setup.rdy=1.0f/dy; setup.rdnw_ptr=setup.rdn_ptr=metric.data();
        setup.msftx_ptr=setup.msfty_ptr=setup.msfux_ptr=setup.msfuy_ptr=maps.data();
        setup.msfvx_ptr=setup.msfvy_ptr=maps.data();
        setup.c1f_ptr=setup.c1h_ptr=one.data(); setup.c2f_ptr=setup.c2h_ptr=zero.data();
        setup.fnm_ptr=setup.fnp_ptr=half.data(); setup.f_ptr=f.data(); setup.e_ptr=setup.sina_ptr=e.data();
        setup.cosa_ptr=maps.data();
        solver.advanceZeroCopy(setup,2,0.01f);
        auto grid=std::static_pointer_cast<WRFGridInfoExtended>(solver.getGridInfo());
        TORCH_CHECK(grid,"missing native grid info after zero-copy setup");
        grid->smagorinsky_opt=1; grid->g=9.81f;
        grid->fzm=torch::from_blob(half.data(),{nw},torch::kFloat32).clone();
        grid->fzp=torch::from_blob(half.data(),{nw},torch::kFloat32).clone();
        solver.setVerticalInterpolationCoefficients(half.data(),half.data(),2.0f,-1.5f,0.5f);
        solver.setTerrainSlopes(torch::zeros({ny,nw,nx},torch::kFloat32),
                                torch::zeros({ny,nw,nx},torch::kFloat32));
    }
    std::array<int,6> sizes() const { return {ny*nz*nu,nv*nz*nx,ny*nw*nx,ny*nw*nx,ny*nz*nx,ny*nx}; }
    std::vector<float*> fields() { return {&u[0],&v[0],&w[0],&ph[0],&th[0],&mu[0]}; }
    torch::Tensor state() {
        std::vector<torch::Tensor> b;
        auto fs=fields(); auto ss=sizes();
        for(int i=0;i<6;++i) b.push_back(torch::from_blob(fs[i],{ss[i]},torch::kFloat32).clone());
        return torch::cat(b);
    }
    void set(const torch::Tensor& z) {
        auto x=z.to(torch::kFloat32).contiguous(); const float* p=x.data_ptr<float>();
        auto fs=fields(); auto ss=sizes();
        for(int i=0;i<6;++i) { std::copy(p,p+ss[i],fs[i]); p+=ss[i]; }
    }
    torch::Tensor rhs(const torch::Tensor& z,RhsMode mode=RhsMode::Full) {
        solver.*access(DtTag{})=1.0f;
        solver.*access(RefTag{})=z.detach().clone();
        return (solver.*access(RhsTag{}))(z,mode).to(torch::kFloat64).detach();
    }
    torch::Tensor rhsGraph(const torch::Tensor& z,RhsMode mode=RhsMode::Full) {
        solver.*access(DtTag{})=1.0f;
        solver.*access(RefTag{})=z.detach().clone();
        return (solver.*access(RhsTag{}))(z,mode).to(torch::kFloat64);
    }
    void step(float dt,int i) {
        int host=i+1; sdirk3_set_timestep_i4(&host);
        solver.unifiedStep(u.data(),v.data(),w.data(),ph.data(),th.data(),mu.data(),
            ru.data(),rv.data(),rw.data(),rph.data(),rth.data(),rmu.data(),
            1.0f/dx,1.0f/dy,metric.data(),metric.data(),maps.data(),maps.data(),
            maps.data(),maps.data(),maps.data(),maps.data(),
            c1f.data(),c2f.data(),c1h.data(),c2h.data(),half.data(),half.data(),
            1,dt,nx,ny,nz,nu,nv,nw);
    }
};

wrf::sdirk3::WRFNewtonKrylovSolver& newton_solver(Grid& g) {
    auto& solver = g.solver.*access(NewtonSolverTag{});
    TORCH_CHECK(solver,"replay-smoke grid has no Newton solver");
    return *solver;
}

void validate_replay_carried_state(
        const wrf::sdirk3::WRFNewtonKrylovSolver::CarriedState& state,int step) {
    TORCH_CHECK(!state.stage3_warmstart_disabled &&
                !state.stage2_hopeless_budget_mode && state.stage2_hopeless_streak==0 &&
                !state.stage3_hopeless_budget_mode && state.stage3_hopeless_streak==0 &&
                state.precond_fallback_count==0 &&
                state.stage2_predictor.defined() && state.stage3_predictor.defined(),
                "replay carried state lacks expected predictors or left the fixed inactive profile at step ",step);
}

torch::Tensor background_state(Grid& g);

torch::Tensor initialize_physical_wave_equilibrium(Grid& g,const torch::Tensor& base) {
    g.set(base);
    g.solver.requestFixedTrajectory(1,{0.25f},base);
    g.step(0.25f,0);
    TORCH_CHECK(g.solver.getLastStepOutcomeCode()==0,
                "physical-wave equilibrium initialization step was not accepted");
    g.solver.closeFixedTrajectory();
    g.set(base);
    return g.rhs(base);
}

void validate_same_physical_wave_context(Grid& reference,Grid& candidate,
                                         const torch::Tensor& base,
                                         const torch::Tensor& reference_rhs,
                                         const torch::Tensor& reference_phi) {
    const auto candidate_base=background_state(candidate);
    TORCH_CHECK(torch::equal(candidate_base,base),
                "replay grid background state differs from baseline");
    const auto candidate_rhs=initialize_physical_wave_equilibrium(candidate,candidate_base);
    const auto candidate_phi=(candidate.solver.*access(PhiTag{})).detach().clone()
        .to(torch::kFloat64);
    TORCH_CHECK(torch::equal(candidate_rhs,reference_rhs) &&
                torch::equal(candidate_phi,reference_phi) &&
                reference.pbase==candidate.pbase && reference.thbase==candidate.thbase &&
                reference.phbase==candidate.phbase && reference.mubase==candidate.mubase &&
                reference.metric==candidate.metric && reference.maps==candidate.maps &&
                reference.f==candidate.f && reference.e==candidate.e,
                "replay grid static context differs from baseline");
}

torch::Tensor replay_carried_state_block(
        Grid& reference,Grid& replay_grid,const torch::Tensor& base,
        const torch::Tensor& reference_rhs,const torch::Tensor& reference_phi,
        const std::vector<torch::Tensor>& expected_endpoints,
        const wrf::sdirk3::WRFNewtonKrylovSolver::CarriedState& snapshot,
        std::uint64_t snapshot_digest,int begin,int count,float dt,int block_id,
        const torch::Tensor& start,const std::vector<torch::Tensor>& endpoint_cotangents,
        int& steps_checked,double& block_max_abs,std::ofstream& out) {
    TORCH_CHECK(begin>=0 && count>0 &&
                expected_endpoints.size()>=static_cast<size_t>(begin+count+1) &&
                endpoint_cotangents.size()==static_cast<size_t>(count),
                "replay block inputs have inconsistent lengths");
    validate_same_physical_wave_context(reference,replay_grid,base,reference_rhs,reference_phi);
    replay_grid.solver.resetInternalFp64Carry();
    auto& replay_newton=newton_solver(replay_grid);
    const auto before_digest=replay_newton.carried_state_digest();
    replay_newton.restore_carried_state(snapshot);
    const auto after_digest=replay_newton.carried_state_digest();
    const auto restored=replay_newton.capture_carried_state();
    const bool stage2_predictor_match=
        snapshot.stage2_predictor.defined()==restored.stage2_predictor.defined() &&
        (!snapshot.stage2_predictor.defined() ||
         torch::equal(snapshot.stage2_predictor,restored.stage2_predictor));
    const bool stage3_predictor_match=
        snapshot.stage3_predictor.defined()==restored.stage3_predictor.defined() &&
        (!snapshot.stage3_predictor.defined() ||
         torch::equal(snapshot.stage3_predictor,restored.stage3_predictor));
    const bool digest_match=after_digest==snapshot_digest;
    out<<"M,replay_block_"<<block_id<<"_state_digest_before_restore,"<<before_digest<<"\n";
    out<<"M,replay_block_"<<block_id<<"_state_digest_after_restore,"<<after_digest<<"\n";
    out<<"M,replay_block_"<<block_id<<"_state_digest_matches_snapshot,"
       <<(digest_match?1:0)<<"\n";
    out<<"M,replay_block_"<<block_id<<"_stage2_predictor_restore_match,"
       <<(stage2_predictor_match?1:0)<<"\n";
    out<<"M,replay_block_"<<block_id<<"_stage3_predictor_restore_match,"
       <<(stage3_predictor_match?1:0)<<"\n";
    TORCH_CHECK(digest_match && stage2_predictor_match && stage3_predictor_match,
                "replay carried-state restore did not roundtrip for block ",block_id);

    TORCH_CHECK(torch::equal(start,expected_endpoints[static_cast<size_t>(begin)]),
                "replay block start does not match retained boundary ",begin);
    replay_grid.set(start);
    replay_grid.solver.requestFixedTrajectory(count,
        std::vector<float>(static_cast<size_t>(count),dt),start);
    block_max_abs=0.0;
    for(int local=0;local<count;++local) {
        const int absolute=begin+local+1;
        replay_grid.step(dt,absolute-1);
        if(replay_grid.solver.getLastStepOutcomeCode()!=0) {
            out<<"M,replay_status,step_not_accepted\n";
            out<<"M,replay_steps_checked,"<<steps_checked<<"\n";
            out<<"M,replay_first_mismatch_index,"<<absolute<<"\n";
            out<<"M,replay_first_mismatch_block,"<<block_id<<"\n";
            out<<"M,replay_failure,unaccepted_step\n";
            out<<"S,replay_block_"<<block_id<<"_max_abs_endpoint_delta,"
               <<std::setprecision(17)<<block_max_abs<<"\n";
            out.flush();
            replay_grid.solver.closeFixedTrajectory();
            return {};
        }
        const auto current=replay_grid.solver.getFixedTrajectoryFp64Checkpoints();
        if(current.size()!=static_cast<size_t>(local+1)) {
            out<<"M,replay_status,endpoint_count_mismatch\n";
            out<<"M,replay_steps_checked,"<<steps_checked<<"\n";
            out<<"M,replay_first_mismatch_index,"<<absolute<<"\n";
            out<<"M,replay_first_mismatch_block,"<<block_id<<"\n";
            out<<"M,replay_failure,endpoint_count_mismatch\n";
            out<<"S,replay_block_"<<block_id<<"_max_abs_endpoint_delta,"
               <<std::setprecision(17)<<block_max_abs<<"\n";
            out.flush();
            replay_grid.solver.closeFixedTrajectory();
            return {};
        }
        const auto& actual=current.back();
        const auto& expected=expected_endpoints[static_cast<size_t>(absolute)];
        const double max_abs=(actual-expected).abs().max().item<double>();
        block_max_abs=std::max(block_max_abs,max_abs);
        ++steps_checked;
        if(!torch::equal(replay_grid.state(),actual.to(torch::kFloat32))) {
            out<<"M,replay_status,publication_mismatch\n";
            out<<"M,replay_steps_checked,"<<steps_checked<<"\n";
            out<<"M,replay_first_mismatch_index,"<<absolute<<"\n";
            out<<"M,replay_first_mismatch_block,"<<block_id<<"\n";
            out<<"M,replay_failure,publication_mismatch\n";
            out<<"S,replay_block_"<<block_id<<"_max_abs_endpoint_delta,"
               <<std::setprecision(17)<<block_max_abs<<"\n";
            out.flush();
            replay_grid.solver.closeFixedTrajectory();
            return {};
        }
        if(!torch::equal(actual,expected)) {
            out<<"M,replay_status,endpoint_mismatch\n";
            out<<"M,replay_steps_checked,"<<steps_checked<<"\n";
            out<<"M,replay_first_mismatch_index,"<<absolute<<"\n";
            out<<"M,replay_first_mismatch_block,"<<block_id<<"\n";
            out<<"S,replay_first_mismatch_max_abs_endpoint_delta,"
               <<std::setprecision(17)<<max_abs<<"\n";
            out<<"S,replay_block_"<<block_id<<"_max_abs_endpoint_delta,"
               <<std::setprecision(17)<<block_max_abs<<"\n";
            out.flush();
            replay_grid.solver.closeFixedTrajectory();
            return {};
        }
    }
    const auto pullback=replay_grid.solver.pullbackFixedTrajectory(endpoint_cotangents);
    TORCH_CHECK(pullback.scalar_type()==torch::kFloat64 &&
                torch::isfinite(pullback).all().item<bool>(),
                "replay block pullback is invalid for block ",block_id);
    out<<"S,replay_block_"<<block_id<<"_max_abs_endpoint_delta,"
       <<std::setprecision(17)<<block_max_abs<<"\n";
    replay_grid.solver.closeFixedTrajectory();
    replay_grid.solver.resetInternalFp64Carry();
    return pullback;
}

void put(std::ofstream& o,const std::string& name,const torch::Tensor& t) {
    auto x=t.to(torch::kFloat64).contiguous().view({-1});
    o<<"A,"<<name<<','<<x.numel();
    for(int64_t i=0;i<x.numel();++i) o<<','<<std::setprecision(17)<<x[i].item<double>();
    o<<'\n';
}
torch::Tensor read_fp64_vector(const std::string& path,int64_t expected,const char* name) {
    std::ifstream input(path);
    TORCH_CHECK(input,"cannot open ",name," file: ",path);
    int64_t count=0;
    input>>count;
    TORCH_CHECK(input && count==expected,name," length mismatch: expected ",expected,
                " values, got ",count);
    std::vector<double> values(static_cast<size_t>(count));
    for(double& value:values)
        TORCH_CHECK(static_cast<bool>(input>>value),"malformed ",name," file: ",path);
    input>>std::ws;
    TORCH_CHECK(input.eof(),"extra data in ",name," file: ",path);
    auto result=torch::from_blob(values.data(),{count},torch::kFloat64).clone();
    TORCH_CHECK(torch::isfinite(result).all().item<bool>(),name," contains nonfinite values");
    return result;
}
struct PhysicalWObservation {
    double x,y,z,w150,w300;
};
std::vector<PhysicalWObservation> read_physical_w_observations(const std::string& path) {
    std::ifstream input(path);
    TORCH_CHECK(input,"cannot open physical-W observations: ",path);
    std::string header;
    int count=0;
    input>>header>>count;
    TORCH_CHECK(input && header=="PHYSICAL_WAVE_OBSERVATIONS_V1" && count==105,
                "physical-W observations must start with PHYSICAL_WAVE_OBSERVATIONS_V1 and count 105");
    std::vector<PhysicalWObservation> observations(static_cast<size_t>(count));
    for(auto& obs:observations) {
        TORCH_CHECK(static_cast<bool>(input>>obs.x>>obs.y>>obs.z>>obs.w150>>obs.w300),
                    "malformed physical-W observation row: ",path);
        TORCH_CHECK(std::isfinite(obs.x) && std::isfinite(obs.y) && std::isfinite(obs.z) &&
                    std::isfinite(obs.w150) && std::isfinite(obs.w300),
                    "physical-W observations contain nonfinite values");
    }
    input>>std::ws;
    TORCH_CHECK(input.eof(),"extra data after physical-W observations: ",path);
    return observations;
}
struct PhysicalWInterpolation {
    torch::Tensor prediction;
    std::vector<int> brackets;
    double minimum_face_distance=std::numeric_limits<double>::infinity();
};
double physical_grid_gravity(Grid& g) {
    const auto info=std::static_pointer_cast<WRFGridInfoExtended>(g.solver.getGridInfo());
    TORCH_CHECK(info && std::isfinite(info->g) && info->g>0.0f,
                "physical-W interpolation requires finite positive native gravity");
    return static_cast<double>(info->g);
}
PhysicalWInterpolation interpolate_physical_w(Grid& g,const torch::Tensor& state,
        const std::vector<PhysicalWObservation>& observations,
        const std::vector<float>* base_phi=nullptr) {
    const int w_start=g.ny*g.nz*g.nu+g.nv*g.nz*g.nx;
    const int ph_start=w_start+g.ny*g.nw*g.nx;
    const auto phi=state.slice(0,ph_start,ph_start+g.ny*g.nw*g.nx).view({g.ny,g.nw,g.nx});
    const auto w=state.slice(0,w_start,w_start+g.ny*g.nw*g.nx).view({g.ny,g.nw,g.nx});
    TORCH_CHECK(!base_phi || base_phi->size()==static_cast<size_t>(g.ny*g.nw*g.nx),
                "physical-W interpolation PHB shape mismatch");
    const double gravity=physical_grid_gravity(g);
    const auto periodic_index=[&](int i) {
        int wrapped=i%g.nx;
        return wrapped<0?wrapped+g.nx:wrapped;
    };
    std::vector<torch::Tensor> predictions;
    predictions.reserve(observations.size());
    std::vector<int> brackets;
    brackets.reserve(observations.size()*4);
    double minimum_face_distance=std::numeric_limits<double>::infinity();
    for(const auto& obs:observations) {
        double x=std::fmod(obs.x,g.lx);
        if(x<0.0) x+=g.lx;
        const double qx=x/static_cast<double>(g.dx)-0.5;
        const int ix0=static_cast<int>(std::floor(qx));
        const int ix1=ix0+1;
        const double fx=qx-ix0;
        const double qy=obs.y/static_cast<double>(g.dy)-0.5;
        const int iy0=static_cast<int>(std::floor(qy));
        const int iy1=iy0+1;
        const double fy=qy-iy0;
        TORCH_CHECK(iy0>=0 && iy1<g.ny && fx>=0.0 && fx<=1.0 && fy>=0.0 && fy<=1.0,
                    "physical observation lies outside the horizontal interpolation domain");
        torch::Tensor value=torch::zeros({},state.options());
        const int ys[]={iy0,iy1};
        const int xs[]={periodic_index(ix0),periodic_index(ix1)};
        const double yw[]={1.0-fy,fy}, xw[]={1.0-fx,fx};
        for(int jy=0;jy<2;++jy) for(int jx=0;jx<2;++jx) {
            const int j=ys[jy],i=xs[jx];
            auto height=[&](int k) {
                const double phb=base_phi?(*base_phi)[(j*g.nw+k)*g.nx+i]:0.0;
                return (phi[j][k][i]+phb)/gravity;
            };
            int lower=-1;
            double nearest=std::numeric_limits<double>::infinity();
            double previous=height(0).detach().item<double>();
            nearest=std::abs(obs.z-previous);
            for(int k=0;k<g.nz;++k) {
                const double next=height(k+1).detach().item<double>();
                TORCH_CHECK(std::isfinite(previous) && std::isfinite(next) && next>previous,
                            "physical-W interpolation requires strictly increasing PHB+PH face heights");
                nearest=std::min(nearest,std::abs(obs.z-next));
                if(obs.z>=previous && obs.z<=next && lower<0) lower=k;
                previous=next;
            }
            minimum_face_distance=std::min(minimum_face_distance,nearest);
            TORCH_CHECK(lower>=0,
                        "physical observation height is outside the current column: z=",obs.z);
            brackets.push_back(lower);
            const auto z0=height(lower),z1=height(lower+1);
            const auto alpha=(obs.z-z0)/(z1-z0);
            const auto vertical=w[j][lower][i]+alpha*(w[j][lower+1][i]-w[j][lower][i]);
            value=value+(yw[jy]*xw[jx])*vertical;
        }
        predictions.push_back(value);
    }
    return {torch::stack(predictions),std::move(brackets),minimum_face_distance};
}
torch::Tensor wave_direction(const Grid& g,int block) {
    const int sizes[]={g.ny*g.nz*g.nu,g.nv*g.nz*g.nx,g.ny*g.nw*g.nx,g.ny*g.nw*g.nx,g.ny*g.nz*g.nx,g.ny*g.nx};
    int offset=0; for(int b=0;b<block;++b) offset+=sizes[b];
    auto d=torch::zeros({static_cast<int64_t>(offset+sizes[block])},torch::kFloat64);
    const double k=2*pi/g.lx;
    if(block==0) for(int j=0;j<g.ny;++j) for(int z=0;z<g.nz;++z) for(int i=0;i<g.nu;++i)
        d[offset+(j*g.nz+z)*g.nu+i]=0.2*std::sin(k*i*g.dx);
    // V is intentionally zero: this reference is an x-wave, uniform in Y.
    if(block==2) for(int j=0;j<g.ny;++j) for(int z=1;z<=g.nz;++z) for(int i=0;i<g.nx;++i)
        d[offset+(j*g.nw+z)*g.nx+i]=0.01*std::cos(k*(i+0.5)*g.dx)*std::sin(pi*z/(g.nz+1));
    if(block==3) for(int j=0;j<g.ny;++j) for(int z=1;z<=g.nz;++z) for(int i=0;i<g.nx;++i)
        d[offset+(j*g.nw+z)*g.nx+i]=0.1*std::cos(k*(i+0.5)*g.dx)*std::sin(pi*z/(g.nz+1));
    if(block==4) for(int j=0;j<g.ny;++j) for(int z=0;z<g.nz;++z) for(int i=0;i<g.nx;++i)
        d[offset+(j*g.nz+z)*g.nx+i]=0.01*std::cos(k*(i+0.5)*g.dx)*std::sin(pi*(z+0.5)/g.nz);
    if(block==5) for(int j=0;j<g.ny;++j) for(int i=0;i<g.nx;++i)
        d[offset+j*g.nx+i]=5.0*std::cos(k*(i+0.5)*g.dx);
    return d;
}
torch::Tensor canonical_column_direction(const Grid& g,int source_column) {
    const int n=g.nz;
    const int sizes[]={g.ny*g.nz*g.nu,g.nv*g.nz*g.nx,g.ny*g.nw*g.nx,
        g.ny*g.nw*g.nx,g.ny*g.nz*g.nx,g.ny*g.nx};
    const int source_sizes[]={n,n,n,n,1};
    int source_block=0,within=source_column;
    while(source_block<4 && within>=source_sizes[source_block]) {
        within-=source_sizes[source_block]; ++source_block;
    }
    TORCH_CHECK(source_column>=0 && source_column<4*n+1,"canonical source column is out of range");
    const int state_block[]={0,2,3,4,5};
    const int block=state_block[source_block];
    int offset=0; for(int b=0;b<block;++b) offset+=sizes[b];
    auto d=torch::zeros({static_cast<int64_t>(offset+sizes[block])},torch::kFloat64);
    const double k=2.0*pi/g.lx;
    if(source_block==0) {
        const double amplitude=0.2;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nu;++i)
            d[offset+(j*g.nz+within)*g.nu+i]=amplitude*std::cos(k*i*g.dx);
    } else if(source_block==1) {
        const double amplitude=0.01;
        const int face=within+1;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nx;++i)
            d[offset+(j*g.nw+face)*g.nx+i]=amplitude*std::cos(k*(i+0.5)*g.dx);
    } else if(source_block==2) {
        const double amplitude=0.1;
        const int face=within+1;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nx;++i)
            d[offset+(j*g.nw+face)*g.nx+i]=amplitude*std::cos(k*(i+0.5)*g.dx);
    } else if(source_block==3) {
        const double amplitude=0.01;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nx;++i)
            d[offset+(j*g.nz+within)*g.nx+i]=amplitude*std::cos(k*(i+0.5)*g.dx);
    } else {
        const double amplitude=5.0;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nx;++i)
            d[offset+j*g.nx+i]=amplitude*std::cos(k*(i+0.5)*g.dx);
    }
    auto packed=torch::zeros({g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+2*g.ny*g.nw*g.nx+
        g.ny*g.nz*g.nx+g.ny*g.nx},torch::kFloat64);
    packed.slice(0,offset,offset+sizes[block]).copy_(d.slice(0,offset,offset+sizes[block]));
    return packed;
}
torch::Tensor divergence_probe_direction(const Grid& g,const std::string& component) {
    TORCH_CHECK(component=="U" || component=="V","damping component must be U or V");
    auto direction=torch::zeros({g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+2*g.ny*g.nw*g.nx+
        g.ny*g.nz*g.nx+g.ny*g.nx},torch::kFloat64);
    constexpr double amplitude=0.2;
    if(component=="U") {
        const int offset=0;
        const double wave_k=2.0*pi/g.lx;
        for(int j=0;j<g.ny;++j) for(int i=0;i<g.nu;++i)
            direction[offset+(j*g.nz)*g.nu+i]=amplitude*std::cos(wave_k*i*g.dx);
    } else {
        const int offset=g.ny*g.nz*g.nu;
        for(int j=0;j<g.nv;++j) for(int i=0;i<g.nx;++i)
            direction[offset+(j*g.nz)*g.nx+i]=amplitude*std::sin(pi*j/g.ny);
    }
    return direction;
}
torch::Tensor expected_divergence_damping_tangent(const Grid& g,const torch::Tensor& direction,
                                                   const std::string& component,double kdamp) {
    auto expected=torch::zeros_like(direction);
    // The native ABI stores reciprocal spacing as float32; reproduce that
    // declared input before applying the independently transcribed stencil.
    const double rdx=static_cast<double>(1.0f/g.dx);
    const double rdy=static_cast<double>(1.0f/g.dy);
    if(component=="U") {
        const auto u=direction.slice(0,0,g.ny*g.nz*g.nu).view({g.ny,g.nz,g.nu});
        const auto div=rdx*(u.slice(2,1,g.nu)-u.slice(2,0,g.nu-1));
        const auto ddiv=rdx*(div.slice(2,1,g.nx)-div.slice(2,0,g.nx-1));
        expected.slice(0,0,g.ny*g.nz*g.nu).view({g.ny,g.nz,g.nu})
            .copy_(torch::constant_pad_nd(kdamp*ddiv,{1,g.nu-g.nx}));
    } else {
        const int v0=g.ny*g.nz*g.nu;
        const auto v=direction.slice(0,v0,v0+g.nv*g.nz*g.nx).view({g.nv,g.nz,g.nx});
        const auto div=rdy*(v.slice(0,1,g.nv)-v.slice(0,0,g.nv-1));
        const auto ddiv=rdy*(div.slice(0,1,g.ny)-div.slice(0,0,g.ny-1));
        expected.slice(0,v0,v0+g.nv*g.nz*g.nx).view({g.nv,g.nz,g.nx})
            .copy_(torch::constant_pad_nd(kdamp*ddiv,{0,0,0,0,1,g.nv-g.ny}));
    }
    return expected;
}
torch::Tensor background_state(Grid& g) {
    // Base theta is 300 K; the physical stable profile and full 84 kPa mass
    // are represented by theta/MU/PH perturbations around that WRF base.
    auto z=torch::zeros({g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+2*g.ny*g.nw*g.nx+g.ny*g.nz*g.nx+g.ny*g.nx},torch::kFloat64);
    int off=g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+2*g.ny*g.nw*g.nx+g.ny*g.nz*g.nx;
    z.slice(0,off,z.numel()).fill_(4000.0);
    const double R=287, cp=1004.5, cv=717.5, p0=100000, mub=80000, mtotal=84000;
    const int ph0=g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+g.ny*g.nw*g.nx;
    const int theta0=g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+2*g.ny*g.nw*g.nx;
    for(int j=0;j<g.ny;++j) for(int k=0;k<g.nz;++k) {
        const double eta=1.0-(k+0.5)/g.nz;
        const double theta=317.0-8.0*eta;
        const double pb=20000.0+mub*eta, p=20000.0+mtotal*eta;
        const double ab=R*300.0/p0*std::pow(pb/p0,-cv/cp);
        const double a=R*theta/p0*std::pow(p/p0,-cv/cp);
        const double dphi=(mtotal*a-mub*ab)/g.nz;
        for(int i=0;i<g.nx;++i) {
            z[theta0+(j*g.nz+k)*g.nx+i]=theta-300.0;
            z[ph0+(j*g.nw+k+1)*g.nx+i]=z[ph0+(j*g.nw+k)*g.nx+i]+dphi;
        }
    }
    return z;
}
std::array<double,3> validate_eos_target(Grid& g,const torch::Tensor& state) {
    const int su=g.ny*g.nz*g.nu, sv=g.nv*g.nz*g.nx, sw=g.ny*g.nw*g.nx;
    const int t0=su+sv+2*sw, m0=t0+g.ny*g.nz*g.nx;
    auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    auto pbase=torch::from_blob(g.pbase.data(),{g.ny,g.nz,g.nx},torch::kFloat32).clone().to(opts);
    auto mu_base=torch::from_blob(g.mubase.data(),{g.ny,g.nx},torch::kFloat32).clone().to(opts);
    auto ph_base=(g.solver.*access(PhiTag{})).to(opts);
    auto alpha_base=compute_inverse_density(torch::full_like(pbase,300.0),pbase,
        287.0f,717.5f,1004.5f,100000.0f).to(opts);
    auto ph=state.slice(0,su+sv+sw,su+sv+2*sw).view({g.ny,g.nw,g.nx});
    auto theta=state.slice(0,t0,t0+g.ny*g.nz*g.nx).view({g.ny,g.nz,g.nx});
    auto mu=state.slice(0,m0,state.numel()).view({g.ny,g.nx});
    auto c1h=torch::ones({g.nz},opts), c2h=torch::zeros({g.nz},opts);
    auto rdnw=torch::full({g.nz},static_cast<double>(g.nz),opts);
    const auto native=calc_p_rho_wrf(ph,theta,mu,mu_base,alpha_base,pbase,rdnw,c1h,c2h,
        287.0f,717.5f,1004.5f,100000.0f,300.0f);
    auto eta=1.0-(torch::arange(g.nz,opts)+0.5)/g.nz;
    auto expected_p=eta.view({1,g.nz,1})*4000.0;
    auto target_theta=theta+300.0;
    auto target_pressure=pbase+expected_p;
    auto alpha_target=compute_inverse_density(target_theta,target_pressure,
        287.0f,717.5f,1004.5f,100000.0f).to(opts);
    const double pressure_error=(native.p_pert-expected_p).abs().max().item<double>();
    const double pressure_budget=4096.0*std::numeric_limits<double>::epsilon()*
        std::max(1.0,pbase.abs().max().item<double>());
    const double alpha_error=((native.alt-alpha_target).abs()/alpha_target.abs().max()).max().item<double>();
    auto full_mass=mu_base+mu;
    auto ph_full=ph+ph_base;
    auto geometric_alpha=rdnw.view({1,g.nz,1})*
        (ph_full.slice(1,1,g.nw)-ph_full.slice(1,0,g.nz))/full_mass.unsqueeze(1);
    const double geometry_error=((geometric_alpha-alpha_target)/alpha_target).abs().max().item<double>();
    std::cout<<"NATIVE_WAVE_EOS p_pert_error="<<pressure_error<<" p_pert_budget="<<pressure_budget
             <<" alpha_relative_error="<<alpha_error<<" geometry_relative_error="<<geometry_error<<"\n";
    TORCH_CHECK(torch::isfinite(native.p_pert).all().item<bool>() &&
                torch::isfinite(native.alt).all().item<bool>() && pressure_error<=pressure_budget &&
                alpha_error<=512.0*std::numeric_limits<double>::epsilon() &&
                geometry_error<=16.0*std::numeric_limits<float>::epsilon(),
                "native EOS/background hydrostatic descriptor failed before solver step");
    return {pressure_error,alpha_error,geometry_error};
}
void validate_physical_handoff_state(Grid& g,const torch::Tensor& state) {
    TORCH_CHECK(state.device().is_cpu() && state.scalar_type()==torch::kFloat64 &&
                state.numel()==g.ny*g.nz*g.nu+g.nv*g.nz*g.nx+
                    2*g.ny*g.nw*g.nx+g.ny*g.nz*g.nx+g.ny*g.nx,
                "manual handoff state has unexpected dtype, device, or packed size");
    TORCH_CHECK(torch::isfinite(state).all().item<bool>(),
                "manual handoff state contains nonfinite values");
    const int su=g.ny*g.nz*g.nu, sv=g.nv*g.nz*g.nx, sw=g.ny*g.nw*g.nx;
    const int ph0=su+sv+sw, theta0=su+sv+2*sw, mu0=theta0+g.ny*g.nz*g.nx;
    const auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    const auto ph=state.slice(0,ph0,ph0+sw).view({g.ny,g.nw,g.nx});
    const auto theta=state.slice(0,theta0,theta0+g.ny*g.nz*g.nx)
        .view({g.ny,g.nz,g.nx});
    const auto mu=state.slice(0,mu0,state.numel()).view({g.ny,g.nx});
    const auto phi_base=torch::from_blob(g.phbase.data(),{g.ny,g.nw,g.nx},
        torch::kFloat32).clone().to(opts);
    const auto mu_base=torch::from_blob(g.mubase.data(),{g.ny,g.nx},
        torch::kFloat32).clone().to(opts);
    const auto theta_total=theta+300.0;
    const auto mu_total=mu_base+mu;
    const auto phi_total=phi_base+ph;
    TORCH_CHECK((theta_total>0.0).all().item<bool>(),
                "manual handoff state has nonpositive total potential temperature");
    TORCH_CHECK((mu_total>0.0).all().item<bool>(),
                "manual handoff state has nonpositive total column mass");
    TORCH_CHECK((phi_total.slice(1,1,g.nw)-phi_total.slice(1,0,g.nz)>0.0)
                    .all().item<bool>(),
                "manual handoff state has non-increasing total geopotential in an owned column");
}
}

int main(int argc,char** argv) {
    const bool damping_probe=argc==7 && std::string(argv[5])=="--damping-probe";
    const bool quadratic_probe=argc==7 && std::string(argv[5])=="--quadratic-probe";
    const bool quadratic_forward=argc==9 && std::string(argv[5])=="--quadratic-forward";
    const bool quadratic_trajectory=argc==9 && std::string(argv[5])=="--quadratic-trajectory";
    const bool upwind_trajectory=argc==9 && std::string(argv[5])=="--upwind-trajectory";
    const bool physical_wave_inverse=argc>=10 &&
        std::string(argv[5])=="--physical-wave-inverse";
    bool physical_wave_forward_only=false;
    bool physical_wave_bounded_tape_forward_only=false;
    bool physical_wave_bounded_replay_pullback=false;
    bool physical_wave_split_observation_pullbacks=false;
    bool physical_wave_replay_smoke=false;
    float physical_wave_newton_tol=1.0e-12f;
    float physical_wave_krylov_tol=1.0e-8f;
    const bool quadratic_trajectory_mode=quadratic_forward || quadratic_trajectory || upwind_trajectory;
    TORCH_CHECK(argc==5 || argc==6 || damping_probe || quadratic_probe || quadratic_trajectory_mode ||
                physical_wave_inverse,
        "usage: test_native_wave_refinement nx ny nz output.csv [initial_state.txt | --damping-probe <U|V> | --quadratic-probe direction.txt | --quadratic-forward direction.txt steps dt | --quadratic-trajectory direction.txt steps dt | --upwind-trajectory direction.txt steps dt | --physical-wave-inverse initial_state.txt observations.txt steps dt [--newton-tol 1e-12|1e-13|1e-14] [--krylov-tol 1e-8|1e-10|1e-12] [--forward-only (skip VJP; tape retained) | --bounded-tape-forward-only (one-step FP64 tape handoff) | --bounded-replay-pullback (960-step, 8-step VJP windows) | --split-observation-pullbacks (h5 only)]]");
    const bool descriptor_only=argc==6 && std::string(argv[5])=="--descriptor-only";
    bool implicit_divergence=false;
    float kdamp=0.0f;
    std::string damping_component;
    if(damping_probe) {
        damping_component=argv[6];
        TORCH_CHECK(damping_component=="U" || damping_component=="V",
                    "invalid damping probe component: ",damping_component);
        implicit_divergence=true;
        kdamp=0.2f;
    }
    int quadratic_steps=0;
    float quadratic_dt=0.0f;
    if(quadratic_trajectory_mode) {
        quadratic_steps=std::stoi(argv[7]);
        quadratic_dt=std::stof(argv[8]);
        // The accepted-step tape retains FP64 input/output graphs. Keep the
        // memory bound explicit: at most 90 adjoint steps or 360 forward steps.
        const int max_steps=quadratic_forward ? 360 : 90;
        TORCH_CHECK(quadratic_steps>0 && quadratic_steps<=max_steps,
                    "quadratic trajectory steps must be between 1 and ",max_steps);
        TORCH_CHECK(std::isfinite(quadratic_dt) && quadratic_dt>0.0f,
                    "quadratic trajectory dt must be finite and positive");
        const double max_duration=quadratic_forward ? 900.0 : 90.0;
        TORCH_CHECK(static_cast<double>(quadratic_steps)*quadratic_dt<=max_duration,
                    "quadratic ",quadratic_forward?"forward":"adjoint trajectory",
                    " physical duration must not exceed ",max_duration," seconds");
    }
    int physical_wave_steps=0;
    float physical_wave_dt=0.0f;
    if(physical_wave_inverse) {
        physical_wave_steps=std::stoi(argv[8]);
        physical_wave_dt=std::stof(argv[9]);
        bool newton_tol_seen=false;
        bool krylov_tol_seen=false;
        for(int arg=10;arg<argc;) {
            const std::string flag(argv[arg++]);
            if(flag=="--forward-only") {
                TORCH_CHECK(!physical_wave_forward_only,"duplicate --forward-only flag");
                physical_wave_forward_only=true;
            } else if(flag=="--bounded-tape-forward-only") {
                TORCH_CHECK(!physical_wave_bounded_tape_forward_only,
                            "duplicate --bounded-tape-forward-only flag");
                physical_wave_bounded_tape_forward_only=true;
            } else if(flag=="--bounded-replay-pullback") {
                TORCH_CHECK(!physical_wave_bounded_replay_pullback,
                            "duplicate --bounded-replay-pullback flag");
                physical_wave_bounded_replay_pullback=true;
            } else if(flag=="--split-observation-pullbacks") {
                TORCH_CHECK(!physical_wave_split_observation_pullbacks,
                            "duplicate --split-observation-pullbacks flag");
                physical_wave_split_observation_pullbacks=true;
            } else if(flag=="--replay-smoke") {
                TORCH_CHECK(!physical_wave_replay_smoke,"duplicate --replay-smoke flag");
                physical_wave_replay_smoke=true;
            } else if(flag=="--newton-tol") {
                TORCH_CHECK(!newton_tol_seen && arg<argc,"--newton-tol requires one value");
                const std::string value(argv[arg++]);
                TORCH_CHECK(value=="1e-12" || value=="1e-13" || value=="1e-14",
                            "physical-wave Newton tolerance must be 1e-12, 1e-13, or 1e-14");
                physical_wave_newton_tol=std::stof(value);
                newton_tol_seen=true;
            } else if(flag=="--krylov-tol") {
                TORCH_CHECK(!krylov_tol_seen && arg<argc,"--krylov-tol requires one value");
                const std::string value(argv[arg++]);
                TORCH_CHECK(value=="1e-8" || value=="1e-10" || value=="1e-12",
                            "physical-wave Krylov tolerance must be 1e-8, 1e-10, or 1e-12");
                physical_wave_krylov_tol=std::stof(value);
                krylov_tol_seen=true;
            } else {
                TORCH_CHECK(false,"unknown physical-wave inverse flag: ",flag);
            }
        }
        TORCH_CHECK(!(physical_wave_forward_only && physical_wave_bounded_tape_forward_only) &&
                    !(physical_wave_forward_only && physical_wave_bounded_replay_pullback) &&
                    !(physical_wave_bounded_tape_forward_only && physical_wave_bounded_replay_pullback),
                    "physical-wave forward-only and bounded modes are mutually exclusive");
        TORCH_CHECK(!physical_wave_split_observation_pullbacks ||
                    (!physical_wave_forward_only && !physical_wave_bounded_tape_forward_only &&
                     !physical_wave_bounded_replay_pullback),
                    "--split-observation-pullbacks requires the retained trajectory/VJP path");
        TORCH_CHECK(!physical_wave_replay_smoke ||
                    (!physical_wave_forward_only && !physical_wave_bounded_tape_forward_only &&
                     !physical_wave_bounded_replay_pullback &&
                     !physical_wave_split_observation_pullbacks),
                    "--replay-smoke is a separate retained-trajectory check mode");
        const bool accepted_taped_schedule=(physical_wave_steps==30 && physical_wave_dt==10.0f) ||
                                           (physical_wave_steps==60 && physical_wave_dt==5.0f) ||
                                           (physical_wave_steps==120 && physical_wave_dt==2.5f) ||
                                           (physical_wave_steps==240 && physical_wave_dt==1.25f);
        const bool accepted_long_handoff_schedule=physical_wave_bounded_tape_forward_only &&
            ((physical_wave_steps==480 && physical_wave_dt==0.625f) ||
             (physical_wave_steps==960 && physical_wave_dt==0.3125f));
        const bool accepted_bounded_replay_schedule=physical_wave_bounded_replay_pullback &&
            physical_wave_steps==960 && physical_wave_dt==0.3125f;
        const bool accepted_replay_smoke_schedule=physical_wave_replay_smoke &&
            physical_wave_steps==16 && physical_wave_dt==0.3125f;
        TORCH_CHECK(!physical_wave_bounded_replay_pullback ||
                    (physical_wave_steps==960 && physical_wave_dt==0.3125f),
                    "--bounded-replay-pullback is supported only for (960,0.3125)");
        TORCH_CHECK(accepted_taped_schedule || accepted_long_handoff_schedule ||
                    accepted_bounded_replay_schedule ||
                    accepted_replay_smoke_schedule,
                    "physical-wave inverse supports (30,10), (60,5), (120,2.5), or (240,1.25) at T=300 s; ",
                    "(480,0.625) and (960,0.3125) require --bounded-tape-forward-only; ",
                    "(16,0.3125) is the two-block --replay-smoke preflight only");
        TORCH_CHECK(!physical_wave_split_observation_pullbacks ||
                    (physical_wave_steps==60 && physical_wave_dt==5.0f),
                    "--split-observation-pullbacks is supported only for the h5 (60,5) center");
    }
    configure(implicit_divergence,kdamp); // solver policy is captured by its constructor.
    if(quadratic_trajectory_mode) g_sdirk3_config.newton_tol=1.0e-12f;
    if(physical_wave_inverse) {
        g_sdirk3_config.newton_tol=physical_wave_newton_tol;
        g_sdirk3_config.krylov_tol=physical_wave_krylov_tol;
    }
    if(physical_wave_replay_smoke || physical_wave_bounded_replay_pullback) {
        TORCH_CHECK(g_sdirk3_config.newton_tol==1.0e-14f &&
                    g_sdirk3_config.krylov_tol==1.0e-12f &&
                    g_sdirk3_config.precond_type==0 && !g_sdirk3_config.gmres_warmstart &&
                    !g_sdirk3_config.inn_warmstart_enable && !g_sdirk3_config.stage3_warmstart &&
                    g_sdirk3_config.nk_adaptive_tol && !g_sdirk3_config.adaptive_timestep &&
                    g_sdirk3_config.adaptive_retune_mode==0 &&
                    g_sdirk3_config.ewt_rtol==1.0e-6f &&
                    g_sdirk3_config.stage2_max_krylov_restarts==0 &&
                    g_sdirk3_config.stage3_max_krylov_restarts==0,
                    "replay-smoke requires the fixed inactive GMRES/INN/Stage-3/adaptive profile");
    }
    Grid g(std::stoi(argv[1]),std::stoi(argv[2]),std::stoi(argv[3]));
    auto base=background_state(g);
    const auto eos_errors=validate_eos_target(g,base);
    const int sizes[]={g.ny*g.nz*g.nu,g.nv*g.nz*g.nx,g.ny*g.nw*g.nx,g.ny*g.nw*g.nx,g.ny*g.nz*g.nx,g.ny*g.nx};
    auto d=torch::zeros_like(base);
    int off=0;
    for(int b=0;b<6;++b) {
        auto db=wave_direction(g,b); d.slice(0,off,off+sizes[b]).copy_(db.slice(0,off,off+sizes[b])); off+=sizes[b];
    }
    if(quadratic_probe || quadratic_trajectory_mode)
        d=read_fp64_vector(argv[6],base.numel(),"perturbation direction");
    auto rb=initialize_physical_wave_equilibrium(g,base);
    const auto phb=(g.solver.*access(PhiTag{})).detach().clone().to(torch::kFloat64);
    std::ofstream out(argv[4]); TORCH_CHECK(out,"cannot open output file");
    out<<"M,nx,"<<g.nx<<"\nM,ny,"<<g.ny<<"\nM,nz,"<<g.nz<<"\n";
    out<<"T,native_torch_version,"<<TORCH_VERSION<<"\n";
    out<<"T,native_compiler_version,"<<__VERSION__<<"\n";
    const float native_rdx=1.0f/g.dx,native_rdy=1.0f/g.dy;
    std::uint32_t rdx_bits=0,rdy_bits=0;
    std::memcpy(&rdx_bits,&native_rdx,sizeof(rdx_bits));
    std::memcpy(&rdy_bits,&native_rdy,sizeof(rdy_bits));
    out<<"T,rdx_fp32_bits,0x"<<std::hex<<rdx_bits<<std::dec<<"\n";
    out<<"T,rdy_fp32_bits,0x"<<std::hex<<rdy_bits<<std::dec<<"\n";
    out<<"M,kdamp_config,"<<std::setprecision(17)<<g_sdirk3_config.kdamp<<"\n";
    out<<"M,implicit_divergence,"<<(g_sdirk3_config.implicit_divergence?1:0)<<"\n";
    auto native_grid=std::static_pointer_cast<WRFGridInfoExtended>(g.solver.getGridInfo());
    out<<"M,kdamp_grid,"<<native_grid->kdamp<<"\n";
    out<<"M,reradius_grid,"<<native_grid->reradius<<"\n";
    out<<"M,sign_smooth_delta_config,"<<g_sdirk3_config.sign_smooth_delta<<"\n";
    out<<"M,omega_w_blend_config,"<<g_sdirk3_config.omega_w_blend<<"\n";
    out<<"M,newton_tol_config,"<<g_sdirk3_config.newton_tol<<"\n";
    out<<"M,krylov_tol_config,"<<g_sdirk3_config.krylov_tol<<"\n";
    out<<"M,nk_adaptive_tol_config,"<<(g_sdirk3_config.nk_adaptive_tol?1:0)<<"\n";
    out<<"M,ewt_rtol_config,"<<g_sdirk3_config.ewt_rtol<<"\n";
    out<<"M,do_curvature_config,"<<(g_sdirk3_config.do_curvature?1:0)<<"\n";
    out<<"M,effective_wrf_omega_ww_cp,"<<(g_sdirk3_config.effective_wrf_omega_ww_cp()?1:0)<<"\n";
    out<<"M,advection_order_config,"<<g_sdirk3_config.advection_order<<"\n";
    out<<"M,non_hydrostatic_config,"<<(g_sdirk3_config.non_hydrostatic?1:0)<<"\n";
    auto host_max_abs=[](const std::vector<float>& values) {
        double result=0.0;
        for(const float value:values)
            result=std::max(result,std::abs(static_cast<double>(value)));
        return result;
    };
    out<<"M,f_input_max_abs,"<<host_max_abs(g.f)<<"\n";
    out<<"M,e_input_max_abs,"<<host_max_abs(g.e)<<"\n";
    out<<"M,wrf_w_damping_config,"<<g_sdirk3_config.wrf_w_damping<<"\n";
    out<<"M,wrf_damp_opt_config,"<<g_sdirk3_config.wrf_damp_opt<<"\n";
    out<<"M,diffusion_option_config,"<<g_sdirk3_config.diffusion_option<<"\n";
    out<<"M,khdif_config,"<<g_sdirk3_config.khdif<<"\n";
    out<<"M,kvdif_config,"<<g_sdirk3_config.kvdif<<"\n";
    out<<"M,rayleigh_damp_coef_config,"<<g_sdirk3_config.rayleigh_damp_coef<<"\n";
    out<<"M,rayleigh_damp_depth_config,"<<g_sdirk3_config.rayleigh_damp_depth<<"\n";
    double map_input_max_deviation=0.0;
    for(const float value:g.maps)
        map_input_max_deviation=std::max(map_input_max_deviation,std::abs(static_cast<double>(value)-1.0));
    out<<"M,map_input_max_deviation,"<<std::setprecision(17)<<map_input_max_deviation<<"\n";
    out<<"M,eos_pressure_error,"<<std::setprecision(17)<<eos_errors[0]<<"\n";
    out<<"M,eos_alpha_relative_error,"<<eos_errors[1]<<"\n";
    out<<"M,eos_geometry_relative_error,"<<eos_errors[2]<<"\n";
    put(out,"phb",phb); put(out,"pbase",torch::from_blob(g.pbase.data(),{static_cast<int64_t>(g.pbase.size())},torch::kFloat32).clone());
    put(out,"thbase_perturb",torch::from_blob(g.thbase.data(),{static_cast<int64_t>(g.thbase.size())},torch::kFloat32).clone());
    put(out,"mubase",torch::from_blob(g.mubase.data(),{static_cast<int64_t>(g.mubase.size())},torch::kFloat32).clone());
    put(out,"base",base); put(out,"direction",d); put(out,"rhs_base",rb);
    if(descriptor_only) { put(out,"initial",base); return 0; }
    if(physical_wave_inverse) {
        const auto observations=read_physical_w_observations(argv[7]);
        const auto initial=read_fp64_vector(argv[6],base.numel(),"physical-wave initial state");
        constexpr double sigma=3.0e-4;
        const auto collect_target=[&](bool at_150) {
            std::vector<double> values; values.reserve(observations.size());
            for(const auto& obs:observations) values.push_back(at_150?obs.w150:obs.w300);
            return torch::from_blob(values.data(),{static_cast<int64_t>(values.size())},
                torch::TensorOptions().dtype(torch::kFloat64)).clone();
        };
        const auto observed_150=collect_target(true),observed_300=collect_target(false);
        torch::Tensor checkpoint_150,checkpoint_300;
        std::vector<torch::Tensor> bounded_replay_states;
        std::vector<wrf::sdirk3::WRFNewtonKrylovSolver::CarriedState>
            bounded_replay_snapshots;
        std::vector<std::uint64_t> bounded_replay_snapshot_digests;
        if(physical_wave_replay_smoke) {
            constexpr int replay_block_steps=8;
            const int su=g.ny*g.nz*g.nu, sv=g.nv*g.nz*g.nx;
            const int sw=g.ny*g.nw*g.nx, w0=su+sv, ph0=w0+sw;
            const auto options=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
            const auto coordinate=torch::arange(sw,options);
            const auto make_cotangent=[&](double phase) {
                auto cotangent=torch::zeros_like(initial);
                cotangent.slice(0,w0,w0+sw).copy_(
                    torch::sin((coordinate+1.0)*0.173+phase)/std::sqrt(static_cast<double>(sw)));
                cotangent.slice(0,ph0,ph0+sw).copy_(
                    torch::cos((coordinate+0.5)*0.119-phase)/std::sqrt(static_cast<double>(sw)));
                return cotangent;
            };
            const auto cotangent_8=make_cotangent(0.23);
            const auto cotangent_16=make_cotangent(-0.41);
            validate_physical_handoff_state(g,initial);
            g.solver.resetInternalFp64Carry();
            g.set(initial);
            auto& baseline_newton=newton_solver(g);
            const std::uint64_t snapshot_0_digest=baseline_newton.carried_state_digest();
            const auto snapshot_0=baseline_newton.capture_carried_state();
            TORCH_CHECK(baseline_newton.carried_state_digest()==snapshot_0_digest,
                        "replay-smoke boundary-0 snapshot changed solver state");
            g.solver.requestFixedTrajectory(physical_wave_steps,
                std::vector<float>(static_cast<size_t>(physical_wave_steps),physical_wave_dt),initial);
            wrf::sdirk3::WRFNewtonKrylovSolver::CarriedState snapshot_8;
            std::uint64_t snapshot_8_digest=0;
            bool captured_snapshot_8=false;
            for(int n=0;n<physical_wave_steps;++n) {
                g.step(physical_wave_dt,n);
                TORCH_CHECK(g.solver.getLastStepOutcomeCode()==0,
                            "replay-smoke retained baseline step was not accepted at index ",n);
                if(n+1==replay_block_steps) {
                    snapshot_8=baseline_newton.capture_carried_state();
                    snapshot_8_digest=baseline_newton.carried_state_digest();
                    captured_snapshot_8=true;
                }
            }
            TORCH_CHECK(captured_snapshot_8,
                        "replay-smoke did not capture the step-8 carried state");
            const auto record_snapshot_profile=[&](
                    const wrf::sdirk3::WRFNewtonKrylovSolver::CarriedState& state,int step) {
                validate_replay_carried_state(state,step);
                out<<"M,replay_snapshot_"<<step<<"_stage2_predictor_defined,"
                   <<(state.stage2_predictor.defined()?1:0)<<"\n";
                out<<"M,replay_snapshot_"<<step<<"_stage3_predictor_defined,"
                   <<(state.stage3_predictor.defined()?1:0)<<"\n";
                out<<"M,replay_snapshot_"<<step<<"_gmres_warmstart_slots,"
                   <<state.warmstart_stage.size()<<"\n";
                out<<"M,replay_snapshot_"<<step<<"_gmres_warmstart_relerr_slots,"
                   <<state.warmstart_relerr.size()<<"\n";
            };
            record_snapshot_profile(snapshot_0,0);
            record_snapshot_profile(snapshot_8,8);
            const auto retained_states=g.solver.getFixedTrajectoryFp64Checkpoints();
            TORCH_CHECK(retained_states.size()==static_cast<size_t>(physical_wave_steps),
                        "replay-smoke baseline did not retain all 16 FP64 endpoints");
            for(int n=0;n<physical_wave_steps;++n)
                validate_physical_handoff_state(g,retained_states[static_cast<size_t>(n)]);
            TORCH_CHECK(torch::equal(g.state(),retained_states.back().to(torch::kFloat32)),
                        "replay-smoke baseline endpoint/publication mismatch");
            std::vector<torch::Tensor> retained_cotangents(
                static_cast<size_t>(physical_wave_steps));
            retained_cotangents[replay_block_steps-1]=cotangent_8;
            retained_cotangents.back()=cotangent_16;
            const auto retained_gradient=g.solver.pullbackFixedTrajectory(retained_cotangents);
            TORCH_CHECK(retained_gradient.scalar_type()==torch::kFloat64 &&
                        torch::isfinite(retained_gradient).all().item<bool>(),
                        "replay-smoke retained baseline pullback is invalid");
            put(out,"initial_state",initial);
            put(out,"replay_retained_initial_pullback",retained_gradient);
            g.solver.closeFixedTrajectory();
            g.solver.resetInternalFp64Carry();

            out<<"M,physical_wave_replay_smoke,1\n";
            out<<"M,replay_block_steps,"<<replay_block_steps<<"\n";
            out<<"M,replay_baseline_status,completed\n";
            out<<"M,replay_status,running_diagnostic_only\n";
            out<<"M,replay_endpoint_comparison,bitwise_exact_required\n";
            out<<"M,physical_state_admissibility_checks,"
               <<(physical_wave_steps+1)<<"\n";
            out<<"M,replay_snapshot_profile_checks,2\n";
            out<<"M,replay_cotangent_steps,8;16\n";
            out<<"M,replay_steps,"<<physical_wave_steps<<"\n";
            out<<"M,replay_dt_fp32,"<<std::setprecision(17)<<physical_wave_dt<<"\n";
            out<<"M,replay_solver_state_snapshot,NewtonCarriedState\n";
            out<<"M,replay_snapshot_steps,0;8\n";
            out<<"M,replay_snapshot_0_digest,"<<snapshot_0_digest<<"\n";
            out<<"M,replay_snapshot_8_digest,"<<snapshot_8_digest<<"\n";
            out<<"M,replay_precond_type,"<<g_sdirk3_config.precond_type<<"\n";
            out<<"M,replay_gmres_warmstart,0\nM,replay_inn_warmstart,0\n";
            out<<"M,replay_stage3_warmstart,0\nM,replay_nk_adaptive_tol,1\n";
            out<<"M,replay_adaptive_timestep,0\nM,replay_adaptive_retune_mode,0\n";
            out<<"M,replay_stage2_max_krylov_restarts,0\n";
            out<<"M,replay_stage3_max_krylov_restarts,0\n";
            out<<"M,replay_claim,short_state_only_block_replay_preflight_not_optimizer_gradient\n";
            out<<"M,replay_steps_checked,0\n";
            out<<"S,replay_block_1_max_abs_endpoint_delta,0\n";
            out<<"S,replay_block_2_max_abs_endpoint_delta,0\n";
            out.flush();

            std::vector<torch::Tensor> replay_endpoints;
            replay_endpoints.reserve(retained_states.size()+1);
            replay_endpoints.push_back(initial.detach().clone());
            for(const auto& state:retained_states) replay_endpoints.push_back(state);
            int replay_steps_checked=0;
            Grid replay_block_2_grid(g.nx,g.ny,g.nz);
            double block_2_max_abs=0.0;
            std::vector<torch::Tensor> block_2_cotangents(replay_block_steps);
            block_2_cotangents.back()=cotangent_16;
            const auto block_2_initial_cotangent=replay_carried_state_block(
                g,replay_block_2_grid,base,rb,phb,replay_endpoints,snapshot_8,
                snapshot_8_digest,replay_block_steps,replay_block_steps,physical_wave_dt,
                2,replay_endpoints[replay_block_steps],block_2_cotangents,
                replay_steps_checked,block_2_max_abs,out);
            if(!block_2_initial_cotangent.defined()) return 2;

            Grid replay_block_1_grid(g.nx,g.ny,g.nz);
            double block_1_max_abs=0.0;
            std::vector<torch::Tensor> block_1_cotangents(replay_block_steps);
            block_1_cotangents.back()=cotangent_8+block_2_initial_cotangent;
            const auto replay_gradient=replay_carried_state_block(
                g,replay_block_1_grid,base,rb,phb,replay_endpoints,snapshot_0,
                snapshot_0_digest,0,replay_block_steps,physical_wave_dt,1,
                replay_endpoints[0],block_1_cotangents,replay_steps_checked,
                block_1_max_abs,out);
            if(!replay_gradient.defined()) return 2;
            const double gradient_delta=(replay_gradient-retained_gradient).norm().item<double>();
            const double retained_norm=retained_gradient.norm().item<double>();
            const double gradient_relative=gradient_delta/
                std::max(retained_norm,std::numeric_limits<double>::min());
            put(out,"replay_initial_pullback",replay_gradient);
            constexpr double replay_gradient_tolerance=1.0e-8;
            const std::array<const char*,6> gradient_blocks={"u","v","w","ph","theta","mu"};
            int gradient_offset=0;
            bool gradient_gate_passed=std::isfinite(gradient_relative) &&
                gradient_relative<=replay_gradient_tolerance;
            for(size_t block=0;block<gradient_blocks.size();++block) {
                const int end=gradient_offset+sizes[block];
                const auto retained_block=retained_gradient.slice(0,gradient_offset,end);
                const auto replay_block_gradient=replay_gradient.slice(0,gradient_offset,end);
                const double block_delta=(replay_block_gradient-retained_block).norm().item<double>();
                const double block_norm=retained_block.norm().item<double>();
                const double block_relative=block_norm==0.0 ?
                    (block_delta==0.0 ? 0.0 : std::numeric_limits<double>::infinity()) :
                    block_delta/block_norm;
                gradient_gate_passed=gradient_gate_passed && std::isfinite(block_relative) &&
                    block_relative<=replay_gradient_tolerance;
                out<<"S,replay_gradient_delta_l2_"<<gradient_blocks[block]<<','
                   <<std::setprecision(17)<<block_delta<<"\n";
                out<<"S,replay_retained_gradient_norm_"<<gradient_blocks[block]<<','
                   <<block_norm<<"\n";
                out<<"S,replay_gradient_relative_error_"<<gradient_blocks[block]<<','
                   <<block_relative<<"\n";
                gradient_offset=end;
            }
            out<<"M,replay_gradient_gate_tolerance,"<<std::setprecision(17)
               <<replay_gradient_tolerance<<"\n";
            out<<"M,replay_gradient_block_gate,"<<(gradient_gate_passed?"passed":"failed")<<"\n";
            out<<"M,replay_status,"<<(gradient_gate_passed?
                "completed_diagnostic_only":"gradient_mismatch")<<"\n";
            out<<"M,replay_steps_checked,"<<replay_steps_checked<<"\n";
            out<<"M,replay_endpoint_parity,all_16_bitwise_equal\n";
            out<<"M,replay_endpoints_admissible_via_exact_parity,1\n";
            out<<"M,replay_gradient_comparison,diagnostic_only\n";
            out<<"S,replay_block_1_max_abs_endpoint_delta,"<<std::setprecision(17)
               <<block_1_max_abs<<"\n";
            out<<"S,replay_block_2_max_abs_endpoint_delta,"<<block_2_max_abs<<"\n";
            out<<"S,replay_gradient_delta_l2,"<<gradient_delta<<"\n";
            out<<"S,replay_gradient_relative_error,"<<gradient_relative<<"\n";
            out<<"S,replay_retained_gradient_norm,"<<retained_norm<<"\n";
            out<<"M,replay_optimizer_run,0\nM,replay_claim,no_optimizer_or_960_step_gradient_claim\n";
            out.flush();
            TORCH_CHECK(gradient_gate_passed,
                        "replay-smoke composed full/component pullback mismatch exceeds ",
                        replay_gradient_tolerance);
            std::cout<<"REPLAY_SMOKE status=completed_diagnostic_only steps_checked="
                     <<replay_steps_checked<<" gradient_relative_error="<<gradient_relative<<"\n";
            return 0;
        }
        if(physical_wave_bounded_tape_forward_only || physical_wave_bounded_replay_pullback) {
            g.solver.resetInternalFp64Carry();
            auto handoff_state=initial.detach().clone();
            validate_physical_handoff_state(g,handoff_state);
            if(physical_wave_bounded_replay_pullback) {
                g.set(handoff_state);
                auto& baseline_newton=newton_solver(g);
                bounded_replay_states.reserve(static_cast<size_t>(physical_wave_steps+1));
                bounded_replay_states.push_back(handoff_state.detach().clone());
                bounded_replay_snapshots.reserve(
                    static_cast<size_t>(physical_wave_steps/8+1));
                bounded_replay_snapshot_digests.reserve(
                    static_cast<size_t>(physical_wave_steps/8+1));
                const auto snapshot=baseline_newton.capture_carried_state();
                validate_replay_carried_state(snapshot,0);
                bounded_replay_snapshot_digests.push_back(
                    baseline_newton.carried_state_digest());
                bounded_replay_snapshots.push_back(snapshot);
            }
            for(int n=0;n<physical_wave_steps;++n) {
                g.set(handoff_state);
                g.solver.requestFixedTrajectory(1,{physical_wave_dt},handoff_state);
                g.step(physical_wave_dt,n);
                TORCH_CHECK(g.solver.getLastStepOutcomeCode()==0,
                            "physical-wave one-step tape handoff was not accepted at index ",n);
                const auto checkpoints=g.solver.getFixedTrajectoryFp64Checkpoints();
                TORCH_CHECK(checkpoints.size()==1 && checkpoints[0].defined() &&
                            checkpoints[0].scalar_type()==torch::kFloat64 &&
                            torch::isfinite(checkpoints[0]).all().item<bool>() &&
                            torch::equal(g.state(),checkpoints[0].to(torch::kFloat32)),
                            "physical-wave one-step tape handoff checkpoint/publication guard failed at index ",n);
                handoff_state=checkpoints[0].detach().clone();
                validate_physical_handoff_state(g,handoff_state);
                g.solver.closeFixedTrajectory();
                if(physical_wave_bounded_replay_pullback) {
                    bounded_replay_states.push_back(handoff_state.detach().clone());
                    if((n+1)%8==0) {
                        const auto snapshot=newton_solver(g).capture_carried_state();
                        validate_replay_carried_state(snapshot,n+1);
                        bounded_replay_snapshot_digests.push_back(
                            newton_solver(g).carried_state_digest());
                        bounded_replay_snapshots.push_back(snapshot);
                    }
                }
                if(n==physical_wave_steps/2-1) checkpoint_150=handoff_state.detach().clone();
                if(n==physical_wave_steps-1) checkpoint_300=handoff_state.detach().clone();
            }
            if(physical_wave_bounded_replay_pullback)
                TORCH_CHECK(bounded_replay_states.size()==
                                static_cast<size_t>(physical_wave_steps+1) &&
                            bounded_replay_snapshots.size()==
                                static_cast<size_t>(physical_wave_steps/8+1) &&
                            bounded_replay_snapshot_digests.size()==bounded_replay_snapshots.size(),
                            "bounded replay did not retain all endpoints and 8-step boundary snapshots");
            g.solver.resetInternalFp64Carry();
        } else {
            g.set(initial);
            g.solver.requestFixedTrajectory(physical_wave_steps,
                std::vector<float>(static_cast<size_t>(physical_wave_steps),physical_wave_dt),initial);
            for(int n=0;n<physical_wave_steps;++n) g.step(physical_wave_dt,n);
            const auto checkpoints=g.solver.getFixedTrajectoryFp64Checkpoints();
            TORCH_CHECK(checkpoints.size()==static_cast<size_t>(physical_wave_steps),
                        "physical-wave inverse did not retain every accepted FP64 endpoint");
            checkpoint_150=checkpoints[static_cast<size_t>(physical_wave_steps/2-1)];
            checkpoint_300=checkpoints.back();
        }
        TORCH_CHECK(checkpoint_150.defined() && checkpoint_300.defined() &&
                    torch::isfinite(checkpoint_150).all().item<bool>() &&
                    torch::isfinite(checkpoint_300).all().item<bool>(),
                    "physical-wave inverse checkpoints are missing or nonfinite");
        const auto evaluate_observations=[&](const torch::Tensor& checkpoint,bool at_150) {
            auto state_leaf=checkpoint.detach().clone();
            if(!physical_wave_forward_only && !physical_wave_bounded_tape_forward_only)
                state_leaf.requires_grad_(true);
            auto interpolation=interpolate_physical_w(g,state_leaf,observations,&g.phbase);
            const auto& target=at_150?observed_150:observed_300;
            const auto residual=(interpolation.prediction-target)/sigma;
            const auto loss=0.5*residual.square().sum();
            torch::Tensor cotangent;
            if(!physical_wave_forward_only && !physical_wave_bounded_tape_forward_only)
                cotangent=torch::autograd::grad({loss},{state_leaf}, {}, true, false)[0];
            return std::tuple<torch::Tensor,torch::Tensor,torch::Tensor,double,
                              std::vector<int>>{interpolation.prediction.detach(),loss.detach(),
                cotangent,interpolation.minimum_face_distance,std::move(interpolation.brackets)};
        };
        auto eval_150=evaluate_observations(checkpoint_150,true);
        auto eval_300=evaluate_observations(checkpoint_300,false);
        const auto prediction_150=std::get<0>(eval_150);
        const auto loss_150=std::get<1>(eval_150);
        const auto cotangent_150=std::get<2>(eval_150);
        const auto prediction_300=std::get<0>(eval_300);
        const auto loss_300=std::get<1>(eval_300);
        const auto cotangent_300=std::get<2>(eval_300);
        const double objective=loss_150.item<double>()+loss_300.item<double>();
        TORCH_CHECK(std::isfinite(objective) && torch::isfinite(prediction_150).all().item<bool>() &&
                    torch::isfinite(prediction_300).all().item<bool>(),
                    "physical-wave observation loss or prediction is nonfinite");
        out<<"M,physical_wave_inverse,1\n";
        out<<"M,forward_only,"<<((physical_wave_forward_only || physical_wave_bounded_tape_forward_only)?1:0)<<"\n";
        if(physical_wave_bounded_replay_pullback) {
            out<<"M,bounded_replay_pullback,1\nM,execution_mode,bounded_replay_pullback\n";
            out<<"M,tape_window_steps,8\nM,checkpoint_precision,detached_fp64_endpoint_history\n";
            out<<"M,admissibility_checked_every_step,1\nM,retains_tape,1\n";
            out<<"M,pullback_requested,1\nM,replay_exact,running\n";
            out<<"M,replay_steps_checked,0\n";
            out<<"M,replay_snapshot_boundary_stride,8\nM,replay_snapshot_count,121\n";
            out<<"M,retained_fp64_endpoint_count,961\nM,replay_optimizer_run,0\n";
            out<<"M,physical_state_admissibility_checks,961\n";
            out<<"M,replay_snapshot_profile_checks,121\n";
            out<<"M,replay_claim,bounded_960_step_initial_pullback_not_optimizer_update\n";
        } else if(physical_wave_bounded_tape_forward_only) {
            out<<"M,execution_mode,single_step_tape_handoff\nM,tape_window_steps,1\n";
            out<<"M,checkpoint_precision,single_step_retained_fp64_handoff\n";
            out<<"M,admissibility_checked_every_step,1\n";
            out<<"M,retains_tape,1\nM,pullback_requested,0\n";
        } else {
            out<<"M,checkpoint_precision,retained_fp64_trajectory\nM,retains_tape,1\n";
            out<<"M,pullback_requested,"<<(physical_wave_forward_only?0:1)<<"\n";
        }
        out<<"M,internal_fp64,"<<(g_sdirk3_config.internal_fp64?1:0)<<"\n";
        out<<"M,internal_fp64_state_carry,"<<(g_sdirk3_config.internal_fp64_state_carry?1:0)<<"\n";
        out<<"M,retain_graph_for_adjoint,"<<(g_sdirk3_config.retain_graph_for_adjoint?1:0)<<"\n";
        out<<"M,trajectory_steps,"<<physical_wave_steps<<"\n";
        out<<"M,trajectory_dt_fp32,"<<std::setprecision(17)<<physical_wave_dt<<"\n";
        out<<"M,trajectory_seconds,300\nM,observation_count_per_time,105\n";
        out<<"M,observation_time_150_seconds,150\nM,observation_time_300_seconds,300\n";
        out<<"M,physical_observation_sigma_m_s,"<<sigma<<"\n";
        out<<"M,observation_xyz_from_input,1\nM,observation_height_uses_current_PH,1\n";
        out<<"M,observation_xyz_units,m\nM,observation_height_formula,(PHB+PH)/g\n";
        out<<"M,observation_gravity_m_s2,"<<std::setprecision(17)<<physical_grid_gravity(g)<<"\n";
        out<<"M,physical_observation_domain_guard,passed\nM,vertical_height_monotonic_guard,passed\n";
        out<<"M,objective_normalization,0.5_sum_over_times_and_points_of_residual_over_sigma_squared\n";
        out<<"M,minimum_height_to_any_W_face_150_m,"<<std::get<3>(eval_150)<<"\n";
        out<<"M,minimum_height_to_any_W_face_300_m,"<<std::get<3>(eval_300)<<"\n";
        const auto& brackets_150=std::get<4>(eval_150);
        const auto& brackets_300=std::get<4>(eval_300);
        int bracket_changes=0;
        for(size_t i=0;i<brackets_150.size();++i)
            if(brackets_150[i]!=brackets_300[i]) ++bracket_changes;
        out<<"M,bracket_corner_changes_150_to_300,"<<bracket_changes<<"\n";
        out<<"M,bracket_note,locally_piecewise_differentiable_no_cross_trial_rejection\n";
        put(out,"initial_state",initial);
        put(out,"checkpoint_150",checkpoint_150);
        put(out,"checkpoint_300",checkpoint_300);
        put(out,"predicted_150",prediction_150);
        put(out,"predicted_300",prediction_300);
        put(out,"observed_150",observed_150);
        put(out,"observed_300",observed_300);
        const auto bracket_options=torch::TensorOptions().dtype(torch::kFloat64);
        put(out,"bracket_index_150",torch::tensor(brackets_150,bracket_options));
        put(out,"bracket_index_300",torch::tensor(brackets_300,bracket_options));
        out<<"S,objective_physical_w,"<<std::setprecision(17)<<objective<<"\n";
        if(!physical_wave_forward_only && !physical_wave_bounded_tape_forward_only) {
            const int w0=g.ny*g.nz*g.nu+g.nv*g.nz*g.nx;
            const int w1=w0+g.ny*g.nw*g.nx;
            const int ph1=w1+g.ny*g.nw*g.nx;
            const auto w_cotangent=[](const torch::Tensor& cotangent,int begin,int end) {
                auto result=torch::zeros_like(cotangent);
                result.slice(0,begin,end).copy_(cotangent.slice(0,begin,end));
                return result;
            };
            const auto cot150_w=w_cotangent(cotangent_150,w0,w1);
            const auto cot150_ph=w_cotangent(cotangent_150,w1,ph1);
            const auto cot300_w=w_cotangent(cotangent_300,w0,w1);
            const auto cot300_ph=w_cotangent(cotangent_300,w1,ph1);
            put(out,"observation_cotangent_150",(prediction_150-observed_150)/(sigma*sigma));
            put(out,"observation_cotangent_300",(prediction_300-observed_300)/(sigma*sigma));
            put(out,"checkpoint_cotangent_150",cotangent_150);
            put(out,"checkpoint_cotangent_300",cotangent_300);
            put(out,"checkpoint_cotangent_150_W",cot150_w);
            put(out,"checkpoint_cotangent_150_PH",cot150_ph);
            put(out,"checkpoint_cotangent_300_W",cot300_w);
            put(out,"checkpoint_cotangent_300_PH",cot300_ph);
            torch::Tensor initial_pullback;
            if(physical_wave_bounded_replay_pullback) {
                constexpr int replay_window_steps=8;
                TORCH_CHECK(bounded_replay_states.size()==
                                static_cast<size_t>(physical_wave_steps+1) &&
                            bounded_replay_snapshots.size()==
                                static_cast<size_t>(physical_wave_steps/replay_window_steps+1),
                            "bounded replay endpoint/snapshot history is incomplete before pullback");
                auto incoming_cotangent=torch::Tensor{};
                int replay_steps_checked=0;
                for(int end=physical_wave_steps;end>0;end-=replay_window_steps) {
                    const int begin=end-replay_window_steps;
                    std::vector<torch::Tensor> block_cotangents(
                        replay_window_steps,torch::zeros_like(initial));
                    for(int local=0;local<replay_window_steps;++local) {
                        const int absolute=begin+local+1;
                        if(absolute==physical_wave_steps/2)
                            block_cotangents[static_cast<size_t>(local)]=
                                block_cotangents[static_cast<size_t>(local)]+cotangent_150;
                        if(absolute==physical_wave_steps)
                            block_cotangents[static_cast<size_t>(local)]=
                                block_cotangents[static_cast<size_t>(local)]+cotangent_300;
                    }
                    if(incoming_cotangent.defined())
                        block_cotangents.back()=block_cotangents.back()+incoming_cotangent;
                    Grid replay_grid(g.nx,g.ny,g.nz);
                    double block_max_abs=0.0;
                    incoming_cotangent=replay_carried_state_block(
                        g,replay_grid,base,rb,phb,bounded_replay_states,
                        bounded_replay_snapshots[static_cast<size_t>(begin/replay_window_steps)],
                        bounded_replay_snapshot_digests[static_cast<size_t>(begin/replay_window_steps)],
                        begin,replay_window_steps,physical_wave_dt,begin/replay_window_steps+1,
                        bounded_replay_states[static_cast<size_t>(begin)],block_cotangents,
                        replay_steps_checked,block_max_abs,out);
                    if(!incoming_cotangent.defined()) return 2;
                    out<<"M,replay_steps_checked,"<<replay_steps_checked<<"\n";
                }
                TORCH_CHECK(replay_steps_checked==physical_wave_steps &&
                            incoming_cotangent.defined(),
                            "bounded replay pullback did not cover the full trajectory");
                initial_pullback=incoming_cotangent;
                out<<"M,replay_exact,1\nM,replay_status,completed\n";
                out<<"M,replay_endpoint_parity,all_960_bitwise_equal\n";
                out<<"M,replay_endpoints_admissible_via_exact_parity,1\n";
                out<<"M,replay_steps_checked,"<<replay_steps_checked<<"\n";
                out<<"M,replay_block_count,"<<physical_wave_steps/replay_window_steps<<"\n";
            } else {
                std::vector<torch::Tensor> output_cotangents(
                    static_cast<size_t>(physical_wave_steps));
                output_cotangents[static_cast<size_t>(physical_wave_steps/2-1)]=cotangent_150;
                output_cotangents.back()=cotangent_300;
                initial_pullback=g.solver.pullbackFixedTrajectory(output_cotangents);
            }
            TORCH_CHECK(initial_pullback.scalar_type()==torch::kFloat64 &&
                        torch::isfinite(initial_pullback).all().item<bool>(),
                        "physical-wave inverse pullback is missing, non-FP64, or nonfinite");
            put(out,"initial_pullback",initial_pullback);
            if(physical_wave_split_observation_pullbacks) {
                std::vector<torch::Tensor> output_cotangents_150(
                    static_cast<size_t>(physical_wave_steps));
                std::vector<torch::Tensor> output_cotangents_300(
                    static_cast<size_t>(physical_wave_steps));
                output_cotangents_150[static_cast<size_t>(physical_wave_steps/2-1)]=cotangent_150;
                output_cotangents_300.back()=cotangent_300;
                const auto initial_pullback_150=
                    g.solver.pullbackFixedTrajectory(output_cotangents_150);
                const auto initial_pullback_300=
                    g.solver.pullbackFixedTrajectory(output_cotangents_300);
                TORCH_CHECK(initial_pullback_150.scalar_type()==torch::kFloat64 &&
                            initial_pullback_300.scalar_type()==torch::kFloat64 &&
                            torch::isfinite(initial_pullback_150).all().item<bool>() &&
                            torch::isfinite(initial_pullback_300).all().item<bool>(),
                            "split observation-time pullbacks are missing, non-FP64, or nonfinite");
                put(out,"initial_pullback_150",initial_pullback_150);
                put(out,"initial_pullback_300",initial_pullback_300);
                const auto split_sum=initial_pullback_150+initial_pullback_300;
                const double sum_error=(split_sum-initial_pullback).norm().item<double>();
                const double combined_norm=initial_pullback.norm().item<double>();
                out<<"M,split_observation_pullbacks,1\n";
                out<<"M,split_pullback_consistency,diagnostic_only\n";
                out<<"S,split_pullback_sum_error_l2,"<<std::setprecision(17)<<sum_error<<"\n";
                out<<"S,split_pullback_sum_error_relative,"<<
                    (combined_norm>0.0?sum_error/combined_norm:sum_error)<<"\n";
            }
            out<<"M,cotangent_nonzero_blocks,W;PH\n";
            out<<"M,cotangent_150_W_l2,"<<cot150_w.norm().item<double>()<<"\n";
            out<<"M,cotangent_150_PH_l2,"<<cot150_ph.norm().item<double>()<<"\n";
            out<<"M,cotangent_300_W_l2,"<<cot300_w.norm().item<double>()<<"\n";
            out<<"M,cotangent_300_PH_l2,"<<cot300_ph.norm().item<double>()<<"\n";
        }
        if(!physical_wave_bounded_tape_forward_only &&
           !physical_wave_bounded_replay_pullback)
            g.solver.closeFixedTrajectory();
        return 0;
    }
    if(quadratic_trajectory_mode) {
        const auto initial=base+d;
        g.set(initial);
        g.solver.requestFixedTrajectory(quadratic_steps,
            std::vector<float>(quadratic_steps,quadratic_dt),initial);
        for(int n=0;n<quadratic_steps;++n) g.step(quadratic_dt,n);
        const auto checkpoints=g.solver.getFixedTrajectoryFp64Checkpoints();
        TORCH_CHECK(checkpoints.size()==static_cast<size_t>(quadratic_steps),
                    "quadratic trajectory did not retain every accepted endpoint");
        out<<"M,quadratic_trajectory,1\n";
        const int objective_harmonic=upwind_trajectory?3:2;
        if(upwind_trajectory) out<<"M,upwind_trajectory,1\n";
        out<<"M,objective_x_harmonic,"<<objective_harmonic<<"\n";
        out<<"M,trajectory_steps,"<<quadratic_steps<<"\n";
        out<<"M,trajectory_dt_fp32,"<<std::setprecision(17)<<quadratic_dt<<"\n";
        put(out,"initial",initial);
        auto terminal=torch::zeros_like(base);
        const int w0=sizes[0]+sizes[1];
        double terminal_norm2=0.0;
        for(int j=0;j<g.ny;++j) for(int k=1;k<=g.nz;++k) for(int i=0;i<g.nx;++i) {
            const double value=std::cos(2.0*pi*objective_harmonic*(i+0.5)*g.dx/g.lx)*
                std::sin(pi*k/(g.nz+1.0));
            terminal[w0+(j*g.nw+k)*g.nx+i]=value;
            terminal_norm2+=value*value;
        }
        terminal/=std::sqrt(terminal_norm2);
        put(out,"terminal_cotangent",terminal);
        for(int n=0;n<quadratic_steps;++n) {
            put(out,"checkpoint_"+std::to_string(n),checkpoints[n]);
            out<<"M,checkpoint_time_"<<n<<','<<std::setprecision(17)
               <<(n+1)*static_cast<double>(quadratic_dt)<<"\n";
        }
        const auto& final_state=checkpoints.back();
        const double objective=(final_state*terminal).sum().item<double>();
        put(out,"final",final_state);
        out<<"S,objective_w_m"<<objective_harmonic<<"_projection,"
           <<std::setprecision(17)<<objective<<"\n";
        if(quadratic_trajectory || upwind_trajectory) {
            const auto initial_pullback=g.solver.pullbackFixedTrajectory(terminal);
            put(out,"initial_pullback",initial_pullback);
        }
        g.solver.closeFixedTrajectory();
        return 0;
    }
    if(quadratic_probe) {
        constexpr double h=1.0;
        const auto rhs_e_center=g.rhs(base,RhsMode::ExplicitOnly);
        const auto rhs_i_center=g.rhs(base,RhsMode::ImplicitOnly);
        std::array<torch::Tensor,3> explicit_positive,implicit_positive;
        put(out,"rhs_E_center",rhs_e_center); put(out,"rhs_I_center",rhs_i_center);
        std::array<torch::Tensor,3> positive,negative;
        for(int level=0;level<3;++level) {
            const double amplitude=h/std::pow(2.0,level);
            positive[level]=g.rhs(base+amplitude*d);
            negative[level]=g.rhs(base-amplitude*d);
            explicit_positive[level]=g.rhs(base+amplitude*d,RhsMode::ExplicitOnly);
            implicit_positive[level]=g.rhs(base+amplitude*d,RhsMode::ImplicitOnly);
            put(out,"rhs_plus_"+std::to_string(level),positive[level]);
            put(out,"rhs_minus_"+std::to_string(level),negative[level]);
            put(out,"rhs_E_plus_"+std::to_string(level),explicit_positive[level]);
            put(out,"rhs_I_plus_"+std::to_string(level),implicit_positive[level]);
            out<<"M,probe_amplitude_"<<level<<','<<std::setprecision(17)<<amplitude<<"\n";
        }
        auto quadratic_positive=(positive[0]-2.0*positive[1]+rb)/(2.0*(h/2.0)*(h/2.0));
        auto quadratic_negative=(negative[0]-2.0*negative[1]+rb)/(2.0*(h/2.0)*(h/2.0));
        auto quadratic_positive_small=(positive[1]-2.0*positive[2]+rb)/(2.0*(h/4.0)*(h/4.0));
        auto quadratic_negative_small=(negative[1]-2.0*negative[2]+rb)/(2.0*(h/4.0)*(h/4.0));
        put(out,"quadratic_positive",quadratic_positive); put(out,"quadratic_negative",quadratic_negative);
        put(out,"quadratic_positive_small",quadratic_positive_small); put(out,"quadratic_negative_small",quadratic_negative_small);
        put(out,"quadratic_mean",0.5*(quadratic_positive+quadratic_negative));
        put(out,"quadratic_mean_small",0.5*(quadratic_positive_small+quadratic_negative_small));
        put(out,"quadratic_side_difference",quadratic_positive-quadratic_negative);
        put(out,"quadratic_E_positive",2.0*(explicit_positive[0]-2.0*explicit_positive[1]+rhs_e_center)/(h*h));
        put(out,"quadratic_I_positive",2.0*(implicit_positive[0]-2.0*implicit_positive[1]+rhs_i_center)/(h*h));
        put(out,"quadratic_E_positive_small",8.0*(explicit_positive[1]-2.0*explicit_positive[2]+rhs_e_center)/(h*h));
        put(out,"quadratic_I_positive_small",8.0*(implicit_positive[1]-2.0*implicit_positive[2]+rhs_i_center)/(h*h));
        return 0;
    }
    if(damping_probe) {
        const auto direction=divergence_probe_direction(g,damping_component);
        // Terminal is dE/dx for E=0.5*sum(m*velocity^2): the 0.5
        // cancels in the derivative. The periodic U endpoint aliases have
        // zero damping response, so their half dual weights do not contribute.
        const auto terminal=direction*(g.dx*g.dy*84000.0/g.nz/9.81);
        const double damping_coefficient_readback=g.solver.getGridInfo()->kdamp;
        const auto expected=expected_divergence_damping_tangent(
            g,direction,damping_component,damping_coefficient_readback);
        constexpr double h=1.0e-3;
        const int sizes_state[]={g.ny*g.nz*g.nu,g.nv*g.nz*g.nx,g.ny*g.nw*g.nx,
            g.ny*g.nw*g.nx,g.ny*g.nz*g.nx,g.ny*g.nx};
        const int block=damping_component=="U" ? 0 : 1;
        const int start=block==0 ? 0 : sizes_state[0];
        const int count=sizes_state[block];
        auto set_damping=[&](bool implicit,float coefficient,bool split_explicit=false) {
            g_sdirk3_config.implicit_divergence=implicit;
            g_sdirk3_config.kdamp=coefficient;
            g_sdirk3_config.split_explicit=split_explicit;
            g.solver.getGridInfo()->kdamp=coefficient;
        };
        struct DampingProbeResult {
            torch::Tensor wide,narrow;
            double operand_wide=0.0,operand_narrow=0.0;
        };
        auto tangent_pair=[&](bool implicit,float coefficient,RhsMode mode=RhsMode::Full,
                              bool split_explicit=false) {
            set_damping(implicit,coefficient,split_explicit);
            const auto plus=g.rhs(base+h*direction,mode),minus=g.rhs(base-h*direction,mode);
            const auto plus_narrow=g.rhs(base+0.5*h*direction,mode);
            const auto minus_narrow=g.rhs(base-0.5*h*direction,mode);
            DampingProbeResult r;
            r.wide=(plus-minus)/(2.0*h); r.narrow=(plus_narrow-minus_narrow)/h;
            r.operand_wide=std::max(plus.slice(0,start,start+count).abs().max().item<double>(),
                                    minus.slice(0,start,start+count).abs().max().item<double>());
            r.operand_narrow=std::max(plus_narrow.slice(0,start,start+count).abs().max().item<double>(),
                                      minus_narrow.slice(0,start,start+count).abs().max().item<double>());
            return r;
        };
        const auto on=tangent_pair(true,0.2f);
        const auto implicit_off=tangent_pair(false,0.2f);
        const auto coefficient_off=tangent_pair(true,0.0f);
        const auto on_implicit=tangent_pair(true,0.2f,RhsMode::ImplicitOnly);
        const auto off_implicit=tangent_pair(false,0.2f,RhsMode::ImplicitOnly);
        const auto on_explicit=tangent_pair(true,0.2f,RhsMode::ExplicitOnly);
        const auto off_explicit=tangent_pair(false,0.2f,RhsMode::ExplicitOnly);
        const auto on_split=tangent_pair(true,0.2f,RhsMode::Full,true);
        const auto off_split=tangent_pair(false,0.2f,RhsMode::Full,true);
        const auto damping_implicit=on.wide-implicit_off.wide;
        const auto damping_coefficient=on.wide-coefficient_off.wide;
        const auto damping_implicit_path=on_implicit.wide-off_implicit.wide;
        const auto damping_explicit_path=on_explicit.wide-off_explicit.wide;
        const auto damping_explicit_narrow=on_explicit.narrow-off_explicit.narrow;
        const auto damping_split_path=on_split.wide-off_split.wide;
        const auto damping_split_narrow=on_split.narrow-off_split.narrow;
        const auto damping_implicit_narrow=on_implicit.narrow-off_implicit.narrow;
        const auto damping_full_vs_implicit=damping_implicit-damping_implicit_path;
        const auto damping_full_vs_implicit_narrow=(on.narrow-implicit_off.narrow)-damping_implicit_narrow;
        const double full_implicit_scatter=(damping_full_vs_implicit-
            damping_full_vs_implicit_narrow).slice(0,start,start+count).abs().max().item<double>();
        const double full_implicit_error=std::max(
            damping_full_vs_implicit.slice(0,start,start+count).abs().max().item<double>(),
            damping_full_vs_implicit_narrow.slice(0,start,start+count).abs().max().item<double>());
        const double full_implicit_operand=std::max({on.operand_wide/h,on.operand_narrow/(0.5*h),
            implicit_off.operand_wide/h,implicit_off.operand_narrow/(0.5*h),
            on_implicit.operand_wide/h,on_implicit.operand_narrow/(0.5*h),
            off_implicit.operand_wide/h,off_implicit.operand_narrow/(0.5*h)});
        const double full_implicit_budget=std::max(16.0*std::numeric_limits<double>::epsilon()*full_implicit_operand,
            std::max(64.0*std::numeric_limits<double>::epsilon(),2.0*full_implicit_scatter));
        TORCH_CHECK(full_implicit_error<=full_implicit_budget,
                    "Full/ImplicitOnly damping difference mismatch: abs=",full_implicit_error,
                    " budget=",full_implicit_budget);
        auto require_excluded=[&](const torch::Tensor& delta,const torch::Tensor& delta_narrow,
                                  const DampingProbeResult& active,const DampingProbeResult& inactive,
                                  const char* name) {
            const double maximum=std::max(delta.slice(0,start,start+count).abs().max().item<double>(),
                delta_narrow.slice(0,start,start+count).abs().max().item<double>());
            const double scatter=(delta-delta_narrow).slice(0,start,start+count).abs().max().item<double>();
            const double operand=std::max({active.operand_wide/h,active.operand_narrow/(0.5*h),
                inactive.operand_wide/h,inactive.operand_narrow/(0.5*h)});
            const double budget=std::max(16.0*std::numeric_limits<double>::epsilon()*operand,
                std::max(64.0*std::numeric_limits<double>::epsilon(),2.0*scatter));
            TORCH_CHECK(maximum<=budget,name," divergence-damping exclusion failed: abs=",maximum,
                        " budget=",budget," fd_scatter=",scatter);
            return std::array<double,3>{{maximum,budget,scatter}};
        };
        const auto explicit_exclusion=require_excluded(damping_explicit_path,damping_explicit_narrow,
            on_explicit,off_explicit,"ExplicitOnly");
        const auto split_exclusion=require_excluded(damping_split_path,damping_split_narrow,
            on_split,off_split,"split_explicit");
        auto vjp_dot=[&](bool off_implicit,float off_kdamp) {
            auto state=base.detach().clone().set_requires_grad(true);
            set_damping(true,0.2f,false);
            const auto rhs_on=g.rhsGraph(state);
            set_damping(off_implicit,off_kdamp,false);
            const auto rhs_off=g.rhsGraph(state);
            const auto objective=((rhs_on-rhs_off)*terminal).sum();
            const auto gradient=torch::autograd::grad({objective},{state})[0];
            return (gradient*direction).sum().item<double>();
        };
        const double vjp_implicit=vjp_dot(false,0.2f);
        const double vjp_coefficient=vjp_dot(true,0.0f);
        const double jvp_dot=(expected*terminal).sum().item<double>();
        const double fd_implicit_wide=(damping_implicit*terminal).sum().item<double>();
        const double fd_coefficient_wide=(damping_coefficient*terminal).sum().item<double>();
        const double fd_implicit_narrow=((on.narrow-implicit_off.narrow)*terminal).sum().item<double>();
        const double fd_coefficient_narrow=((on.narrow-coefficient_off.narrow)*terminal).sum().item<double>();
        const double energy_implicit=fd_implicit_wide;
        const double energy_coefficient=fd_coefficient_wide;
        const double operand_roundoff=16.0*std::numeric_limits<double>::epsilon()*
            std::max(on.operand_wide/h,on.operand_narrow/(0.5*h));
        out<<"M,damping_probe,1\nT,damping_component,"<<damping_component<<"\n";
        for(const auto& item : std::array<std::pair<const char*,const DampingProbeResult*>,3>{{
                {"enabled",&on},{"implicit_off",&implicit_off},{"coefficient_off",&coefficient_off}}}) {
            out<<"S,rhs_operand_"<<item.first<<"_wide,"<<std::setprecision(17)<<item.second->operand_wide<<"\n";
            out<<"S,rhs_operand_"<<item.first<<"_narrow,"<<std::setprecision(17)<<item.second->operand_narrow<<"\n";
        }
        out<<"S,damping_jvp_dot,"<<jvp_dot<<"\n";
        out<<"S,damping_vjp_dot_implicit_toggle,"<<vjp_implicit<<"\n";
        out<<"S,damping_vjp_dot_coefficient_toggle,"<<vjp_coefficient<<"\n";
        out<<"S,damping_fd_dot_implicit_wide,"<<fd_implicit_wide<<"\n";
        out<<"S,damping_fd_dot_implicit_narrow,"<<fd_implicit_narrow<<"\n";
        out<<"S,damping_fd_dot_coefficient_wide,"<<fd_coefficient_wide<<"\n";
        out<<"S,damping_fd_dot_coefficient_narrow,"<<fd_coefficient_narrow<<"\n";
        out<<"S,damping_energy_rate_implicit_toggle,"<<energy_implicit<<"\n";
        out<<"S,damping_energy_rate_coefficient_toggle,"<<energy_coefficient<<"\n";
        out<<"S,damping_operand_roundoff_abs,"<<operand_roundoff<<"\n";
        out<<"S,damping_full_vs_implicit_abs,"<<damping_full_vs_implicit.slice(0,start,start+count).abs().max().item<double>()<<"\n";
        out<<"S,damping_full_vs_implicit_narrow_abs,"<<damping_full_vs_implicit_narrow.slice(0,start,start+count).abs().max().item<double>()<<"\n";
        out<<"S,damping_full_vs_implicit_budget,"<<full_implicit_budget<<"\n";
        out<<"S,damping_full_vs_implicit_fd_scatter,"<<full_implicit_scatter<<"\n";
        out<<"S,damping_explicit_only_abs,"<<explicit_exclusion[0]<<"\n";
        out<<"S,damping_explicit_only_budget,"<<explicit_exclusion[1]<<"\n";
        out<<"S,damping_explicit_only_width_scatter,"<<explicit_exclusion[2]<<"\n";
        out<<"S,damping_split_explicit_abs,"<<split_exclusion[0]<<"\n";
        out<<"S,damping_split_explicit_budget,"<<split_exclusion[1]<<"\n";
        out<<"S,damping_split_explicit_width_scatter,"<<split_exclusion[2]<<"\n";
        put(out,"damping_direction",direction); put(out,"damping_expected",expected);
        put(out,"damping_on_wide",on.wide); put(out,"damping_on_narrow",on.narrow);
        put(out,"damping_implicit_off_wide",implicit_off.wide); put(out,"damping_implicit_off_narrow",implicit_off.narrow);
        put(out,"damping_coefficient_off_wide",coefficient_off.wide); put(out,"damping_coefficient_off_narrow",coefficient_off.narrow);
        put(out,"damping_on_implicit_wide",on_implicit.wide); put(out,"damping_on_implicit_narrow",on_implicit.narrow);
        put(out,"damping_implicit_path_wide",damping_implicit_path);
        put(out,"damping_implicit_path_narrow",damping_implicit_narrow);
        put(out,"damping_explicit_path_wide",damping_explicit_path);
        put(out,"damping_explicit_path_narrow",damping_explicit_narrow);
        put(out,"damping_split_path_wide",damping_split_path);
        put(out,"damping_split_path_narrow",damping_split_narrow);
        return 0;
    }
    const double eps=1.0e-3;
    auto rp=g.rhs(base+eps*d), rm=g.rhs(base-eps*d);
    auto tangent=(rp-rm)/(2.0*eps);
    const double eps_narrow=0.5*eps;
    auto rp_narrow=g.rhs(base+eps_narrow*d), rm_narrow=g.rhs(base-eps_narrow*d);
    auto tangent_narrow=(rp_narrow-rm_narrow)/(2.0*eps_narrow);
    put(out,"tangent",tangent); put(out,"tangent_narrow",tangent_narrow);
    for(int col=0;col<4*g.nz+1;++col) {
        const auto column_direction=canonical_column_direction(g,col);
        auto plus=g.rhs(base+eps*column_direction);
        auto minus=g.rhs(base-eps*column_direction);
        auto plus_narrow=g.rhs(base+eps_narrow*column_direction);
        auto minus_narrow=g.rhs(base-eps_narrow*column_direction);
        const char* field_names[]={"U","V","W","PH","THETA","MU"};
        int field_offset=0;
        for(int block=0;block<6;++block) {
            const int block_size=sizes[block];
            const double operand_wide=std::max(
                plus.slice(0,field_offset,field_offset+block_size).abs().max().item<double>(),
                minus.slice(0,field_offset,field_offset+block_size).abs().max().item<double>());
            const double operand_narrow=std::max(
                plus_narrow.slice(0,field_offset,field_offset+block_size).abs().max().item<double>(),
                minus_narrow.slice(0,field_offset,field_offset+block_size).abs().max().item<double>());
            out<<"S,rhs_operand_"<<col<<"_wide_"<<field_names[block]<<','
               <<std::setprecision(17)<<operand_wide<<"\n";
            out<<"S,rhs_operand_"<<col<<"_narrow_"<<field_names[block]<<','
               <<std::setprecision(17)<<operand_narrow<<"\n";
            field_offset+=block_size;
        }
        put(out,"direction_col_"+std::to_string(col),column_direction);
        put(out,"tangent_col_"+std::to_string(col),(plus-minus)/(2.0*eps));
        put(out,"tangent_narrow_col_"+std::to_string(col),
            (plus_narrow-minus_narrow)/(2.0*eps_narrow));
    }
    // Native accepted one/two-step FP64 carry and its VJP are intentionally
    // exercised by the orchestrator using the public trajectory interface.
    constexpr float dt=1.0f;
    auto z0=base+1.0e-3*d;
    if(argc==6 && !descriptor_only) {
        std::ifstream input(argv[5]); TORCH_CHECK(input,"cannot open initial-state descriptor");
        int64_t count=0; input>>count;
        TORCH_CHECK(count==base.numel(),"initial-state descriptor length mismatch");
        std::vector<double> values(static_cast<size_t>(count));
        for(double& value:values) input>>value;
        TORCH_CHECK(input.good() || input.eof(),"malformed initial-state descriptor");
        z0=torch::from_blob(values.data(),{count},torch::kFloat64).clone();
        TORCH_CHECK(torch::isfinite(z0).all().item<bool>(),"initial-state descriptor is nonfinite");
    }
    put(out,"initial",z0);
    g.set(z0); g.solver.requestFixedTrajectory(2,{dt,dt},z0);
    g.step(dt,0); g.step(dt,1);
    auto checkpoints=g.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(checkpoints.size()==2,"native carry did not complete two steps");
    put(out,"step1",checkpoints[0]); put(out,"step2",checkpoints[1]);
    auto terminal=torch::zeros_like(checkpoints.back());
    terminal.slice(0,sizes[0]+sizes[1],sizes[0]+sizes[1]+sizes[2]).copy_(
        d.slice(0,sizes[0]+sizes[1],sizes[0]+sizes[1]+sizes[2]));
    auto grad=g.solver.pullbackFixedTrajectory(terminal); put(out,"vjp",grad);
    out<<"M,dt,"<<dt<<"\n";
    out<<"M,propagation_steps,90\n";
    auto expanded_direction=[&](int block) {
        auto packed=torch::zeros_like(base);
        auto local=wave_direction(g,block);
        int off=0; const int local_sizes[]={g.ny*g.nz*g.nu,g.nv*g.nz*g.nx,g.ny*g.nw*g.nx,
            g.ny*g.nw*g.nx,g.ny*g.nz*g.nx,g.ny*g.nx};
        for(int b=0;b<block;++b) off+=local_sizes[b];
        packed.slice(0,off,off+local_sizes[block]).copy_(local.slice(0,off,off+local_sizes[block]));
        return packed;
    };
    auto eval_objective=[&](int steps,const torch::Tensor& initial) {
        configure();
        Grid trial(g.nx,g.ny,g.nz); trial.set(initial);
        trial.solver.requestFixedTrajectory(steps,std::vector<float>(steps,dt),initial);
        for(int n=0;n<steps;++n) trial.step(dt,n);
        auto final_state=trial.solver.getFixedTrajectoryFp64Checkpoints().back();
        const double value=(final_state*terminal).sum().item<double>();
        trial.solver.closeFixedTrajectory();
        return value;
    };
    for(int block : {2,3,5}) {
        const char* names[]={"","","W","PH","","MU"};
        const auto direction=expanded_direction(block);
        const double dot=(grad*direction).sum().item<double>();
        out<<"S,dot2_"<<names[block]<<','<<std::setprecision(17)<<dot<<"\n";
        for(const auto& probe : std::array<std::pair<double,const char*>,2>{{{0.1,"wide"},{0.05,"narrow"}}}) {
            const double plus=eval_objective(2,z0+probe.first*direction);
            const double minus=eval_objective(2,z0-probe.first*direction);
            out<<"S,fd2_"<<probe.second<<'_'<<names[block]<<','<<std::setprecision(17)
               <<(plus-minus)/(2.0*probe.first)<<"\n";
        }
    }
    g.solver.closeFixedTrajectory();
    for(int block : {2,3,5}) {
        const char* names[]={"","","W","PH","","MU"};
        const auto direction=expanded_direction(block);
        configure();
        Grid one(g.nx,g.ny,g.nz); one.set(z0);
        one.solver.requestFixedTrajectory(1,{dt},z0); one.step(dt,0);
        auto final_state=one.solver.getFixedTrajectoryFp64Checkpoints().back();
        auto terminal_one=torch::zeros_like(final_state);
        terminal_one.slice(0,sizes[0]+sizes[1],sizes[0]+sizes[1]+sizes[2]).copy_(
            d.slice(0,sizes[0]+sizes[1],sizes[0]+sizes[1]+sizes[2]));
        const double value=(final_state*terminal_one).sum().item<double>();
        auto grad_one=one.solver.pullbackFixedTrajectory(terminal_one);
        const double dot=(grad_one*direction).sum().item<double>();
        one.solver.closeFixedTrajectory();
        out<<"S,objective1_"<<names[block]<<','<<std::setprecision(17)<<value<<"\n";
        out<<"S,dot1_"<<names[block]<<','<<std::setprecision(17)<<dot<<"\n";
        for(const auto& probe : std::array<std::pair<double,const char*>,2>{{{0.1,"wide"},{0.05,"narrow"}}}) {
            const double plus=eval_objective(1,z0+probe.first*direction);
            const double minus=eval_objective(1,z0-probe.first*direction);
            out<<"S,fd1_"<<probe.second<<'_'<<names[block]<<','<<std::setprecision(17)
               <<(plus-minus)/(2.0*probe.first)<<"\n";
        }
    }
    constexpr int propagation_steps=90;
    g.set(z0);
    g.solver.requestFixedTrajectory(propagation_steps,
        std::vector<float>(propagation_steps,dt),z0);
    for(int n=0;n<propagation_steps;++n) g.step(dt,n);
    const auto long_checkpoints=g.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(long_checkpoints.size()==propagation_steps,
                "native eigenmode propagation did not retain all accepted endpoints");
    put(out,"prop90",long_checkpoints.back());
    g.solver.closeFixedTrajectory();
    return 0;
}
