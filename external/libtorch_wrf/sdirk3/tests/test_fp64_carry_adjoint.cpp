// Internal FP64 trajectory, independent primal checkpoints, and active-effect VJP.
#include "tile_test_fixture.h"
#include "stable_wave_reference.h"
#include <algorithm>
#include <array>
#include <complex>
#include <iomanip>
#include <limits>
#include <string>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
extern "C" void sdirk3_set_timestep_i4(int*);
namespace {
using namespace wrf::sdirk3::test;
constexpr float dt = 0.25f;
void configure(bool active,float kv=10.0f) {
    auto& c = wrf::sdirk3::g_sdirk3_config;
    c = wrf::sdirk3::SDIRK3Config{};
    c.internal_fp64 = true;
    c.internal_fp64_state_carry = true;
    c.retain_graph_for_adjoint = true;
    c.imex_split_mode = 3; c.mass_coordinate_mode = 1;
    c.imex_slow_in_tangent = true; c.use_autograd = true;
    c.non_hydrostatic = true; c.do_curvature = true;
    c.buoyancy_use_current_w = true; c.omega_w_blend = 1.0f;
    c.diffusion_option = 2; c.khdif = active ? 1000.0f : 0.0f;
    c.kvdif = active ? kv : 0.0f;
    c.precond_type = 0; c.n_threads = 1;
    c.max_newton_iter = 40; c.newton_tol = 1e-10f; c.newton_rtol = 0.0f;
    c.ewt_rtol = 1e-6f; c.krylov_tol = 1e-8f;
    c.stage_fail_action = 1; c.gmres_warmstart = false; c.inn_warmstart_enable = false;
}
void prepare_native(TileCase& tile) {
    auto grid=std::static_pointer_cast<wrf::sdirk3::WRFGridInfoExtended>(tile.solver.getGridInfo());
    TORCH_CHECK(grid, "missing option-2 grid");
    grid->smagorinsky_opt=1;
    grid->g=9.81f;
    grid->fzm=torch::full({nw},0.5f,torch::kFloat32);
    grid->fzp=torch::full({nw},0.5f,torch::kFloat32);
}
torch::Tensor initial() {
    configure(true);
    TileCase t(5000.0f);
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) {
        for(int i=0;i<nu;++i) t.u[(j*nz+k)*nu+i] = i==nx ? 0.f :
            0.3f*std::sin(2.0*std::acos(-1.0)*i/nx);
        for(int i=0;i<nx;++i) t.theta[(j*nz+k)*nx+i] =
            0.5f*std::cos(2.0*std::acos(-1.0)*i/nx)*std::cos(2.0*std::acos(-1.0)*j/(ny-1));
    }
    return t.state().to(torch::kFloat64);
}
torch::Tensor direction(int block) {
    const int sizes[] = {su,sv,sw,sw,st,sm};
    int start=0; for(int b=0;b<block;++b) start+=sizes[b];
    auto a=torch::arange(sizes[block],torch::kFloat64);
    auto v=torch::sin(0.17*a)+0.3*torch::cos(0.071*a);
    v /= v.norm();
    const double scale=block==2 ? 0.1 : 100.0;
    auto out=torch::zeros({total},torch::kFloat64);
    out.slice(0,start,start+sizes[block]).copy_(scale*v);
    return out;
}
struct Objective { double value; torch::Tensor terminal; };
double physical_observation_cost(const torch::Tensor& predicted,const torch::Tensor& observed,
                                double physical_std);
Objective objective(const torch::Tensor& z) {
    auto a=torch::arange(sw,torch::kFloat64);
    auto residual=z.slice(0,su+sv,su+sv+sw)/0.1-torch::sin(0.13*a);
    auto terminal=torch::zeros_like(z);
    terminal.slice(0,su+sv,su+sv+sw).copy_(residual/(0.1*sw));
    return {0.5*residual.square().mean().item<double>(),terminal};
}
std::vector<int64_t> physical_w_indices() {
    std::vector<int64_t> indices;
    indices.reserve((ny-1)*(nx-1)*(nw-2));
    const int64_t offset=su+sv;
    for(int j=0;j<ny-1;++j) for(int k=1;k<nw-1;++k) for(int i=0;i<nx-1;++i)
        indices.push_back(offset+(j*nw+k)*nx+i);
    return indices;
}
torch::Tensor physical_w_index_tensor() {
    const auto indices=physical_w_indices();
    return torch::from_blob(const_cast<int64_t*>(indices.data()),
        {static_cast<int64_t>(indices.size())},
        torch::TensorOptions().dtype(torch::kInt64).device(torch::kCPU)).clone();
}
torch::Tensor physical_w_values(const torch::Tensor& z) {
    return z.index_select(0,physical_w_index_tensor());
}
double max_block_state_relative_difference(const torch::Tensor& reference,
                                          const torch::Tensor& candidate) {
    TORCH_CHECK(reference.dim()==1 && candidate.dim()==1 &&
                reference.numel()==total && candidate.numel()==total,
                "packed endpoint comparison requires matching state vectors");
    const int sizes[]={su,sv,sw,sw,st,sm};
    // Reference-unit floors: 1 for velocity/theta blocks, 100 for geopotential
    // and column-mass blocks, each scaled by sqrt(block size).
    const double unit_floors[]={1.0,1.0,1.0,100.0,1.0,100.0};
    double maximum=0.0;
    int offset=0;
    for(int block=0;block<6;++block) {
        const auto ref=reference.slice(0,offset,offset+sizes[block]);
        const auto delta=candidate.slice(0,offset,offset+sizes[block])-ref;
        const double denominator=std::max(ref.norm().item<double>(),
            unit_floors[block]*std::sqrt(static_cast<double>(sizes[block])));
        maximum=std::max(maximum,delta.norm().item<double>()/denominator);
        offset+=sizes[block];
    }
    return maximum;
}
Objective objective(const torch::Tensor& z,const torch::Tensor& observations,double observation_scale) {
    if(!observations.defined()) return objective(z);  // Preserve the original derivative case exactly.
    TORCH_CHECK(std::isfinite(observation_scale) && observation_scale>0.0 && observations.dim()==1 &&
                observations.numel()==static_cast<int64_t>(physical_w_indices().size()),
                "invalid inverse observations or scale");
    const double physical_std=observation_scale*
        std::sqrt(static_cast<double>(observations.numel()));
    const auto indices=physical_w_index_tensor();
    const auto residual=(z.index_select(0,indices)-observations)/physical_std;
    auto terminal=torch::zeros_like(z);
    auto wbar=torch::zeros({sw},z.options());
    const auto local_indices=indices-su-sv;
    wbar.index_copy_(0,local_indices,residual/physical_std);
    terminal.slice(0,su+sv,su+sv+sw).copy_(wbar);
    return {physical_observation_cost(z.index_select(0,indices),observations,physical_std),terminal};
}
void validate_physical_state(TileCase& tile,const torch::Tensor& state,const char* where) {
    tile.checkPhysicalState(state,where);
}
struct Result {
    double value; torch::Tensor gradient,last_only; std::vector<torch::Tensor> states;
    std::vector<torch::Tensor> output_cotangents;
    torch::Tensor time2_only,time4_only,terminal_vector,legacy_terminal,zero_cotangents,shifted_time2;
};
Result run(const torch::Tensor& z0,int steps,bool active,bool pullback,
           const torch::Tensor& observations=torch::Tensor(),double observation_scale=0.1,
           const std::vector<torch::Tensor>& timed_observations={},double timed_physical_std=0.0,
           bool probe_time_pullbacks=false,float newton_tol=1e-10f,
           float step_dt=dt,float kv=10.0f,bool wave_case=false) {
    configure(active,kv);
    TORCH_CHECK(std::isfinite(newton_tol) && newton_tol>0.0f,"invalid test-only Newton tolerance");
    TORCH_CHECK(std::isfinite(step_dt) && step_dt>0.0f,"invalid fixed-trajectory timestep");
    wrf::sdirk3::g_sdirk3_config.newton_tol=newton_tol;
    if(wave_case) {
        // The low-amplitude balanced background has a resolved R/K ratio
        // above the default post-damp trigger from EOS cancellation alone.
        // Keep the accepted existing cap (1e6) identical for primal, FD and VJP.
        wrf::sdirk3::g_sdirk3_config.stage_damp_rel_threshold=1.0e6f;
    }
    TileCase tile(5000.0f); prepare_native(tile);
    if(wave_case) tile.solver.setVerticalInterpolationCoefficients(tile.half.data(),tile.half.data(),2.0f,-1.5f,0.5f);
    tile.set(z0);
    validate_physical_state(tile,z0,"initial state");
    tile.solver.requestFixedTrajectory(steps,std::vector<float>(steps,step_dt),z0);
    for(int n=0;n<steps;++n) { int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(step_dt); }
    auto checkpoints=tile.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(checkpoints.size()==static_cast<size_t>(steps),"incomplete checkpoints");
    for(size_t n=0;n<checkpoints.size();++n)
        validate_physical_state(tile,checkpoints[n],"accepted checkpoint");
    TORCH_CHECK(torch::equal(tile.state(),checkpoints.back().to(torch::kFloat32)),"publication mismatch");
    TORCH_CHECK(!observations.defined() || timed_observations.empty(),
                "single-time and time-indexed observations cannot be combined");
    std::vector<torch::Tensor> output_cotangents;
    Objective obj{0.0,torch::zeros_like(checkpoints.back())};
    if(timed_observations.empty()) {
        obj=objective(checkpoints.back(),observations,observation_scale);
    } else {
        TORCH_CHECK(timed_observations.size()==checkpoints.size() &&
                    std::isfinite(timed_physical_std) && timed_physical_std>0.0,
                    "time-indexed observations require one optional vector per accepted step and a positive physical sigma");
        output_cotangents.resize(checkpoints.size());
        for(size_t step=0;step<checkpoints.size();++step) {
            const auto& obs=timed_observations[step];
            if(!obs.defined()) continue;
            TORCH_CHECK(obs.dim()==1 && obs.numel()==static_cast<int64_t>(physical_w_indices().size()),
                        "time-indexed observation has the wrong physical-W size");
            const auto prediction=physical_w_values(checkpoints[step]);
            const auto residual=(prediction-obs)/timed_physical_std;
            obj.value+=physical_observation_cost(prediction,obs,timed_physical_std);
            auto terminal=torch::zeros_like(checkpoints[step]);
            auto wbar=torch::zeros({sw},checkpoints[step].options());
            wbar.index_copy_(0,physical_w_index_tensor()-su-sv,residual/timed_physical_std);
            terminal.slice(0,su+sv,su+sv+sw).copy_(wbar);
            output_cotangents[step]=terminal;
        }
        obj.terminal=output_cotangents.back().defined()
            ? output_cotangents.back() : torch::zeros_like(checkpoints.back());
    }
    torch::Tensor gradient,last;
    torch::Tensor time2_only,time4_only,terminal_vector,legacy_terminal,zero_cotangents,shifted_time2;
    if(pullback) {
        last=tile.solver.pullbackLastStep(obj.terminal);
        if(timed_observations.empty()) {
            gradient=tile.solver.pullbackFixedTrajectory(obj.terminal);
        } else {
            gradient=tile.solver.pullbackFixedTrajectory(output_cotangents);
            if(probe_time_pullbacks) {
                TORCH_CHECK(steps==4 && output_cotangents[1].defined() &&
                            output_cotangents[3].defined(),
                            "time pullback probes require observations at steps 2 and 4");
                auto only2=std::vector<torch::Tensor>(steps);
                auto only4=std::vector<torch::Tensor>(steps);
                auto shifted=std::vector<torch::Tensor>(steps);
                only2[1]=output_cotangents[1];
                only4[3]=output_cotangents[3];
                shifted[0]=output_cotangents[1];
                shifted[3]=output_cotangents[3];
                std::vector<torch::Tensor> zeros(steps);
                time2_only=tile.solver.pullbackFixedTrajectory(only2);
                time4_only=tile.solver.pullbackFixedTrajectory(only4);
                terminal_vector=tile.solver.pullbackFixedTrajectory(only4);
                legacy_terminal=tile.solver.pullbackFixedTrajectory(output_cotangents[3]);
                zero_cotangents=tile.solver.pullbackFixedTrajectory(zeros);
                shifted_time2=tile.solver.pullbackFixedTrajectory(shifted);
                auto expect_bad_cotangent=[&](const std::vector<torch::Tensor>& bad) {
                    bool rejected=false;
                    try { (void)tile.solver.pullbackFixedTrajectory(bad); }
                    catch(const c10::Error&) { rejected=true; }
                    TORCH_CHECK(rejected,"malformed output-cotangent vector was accepted");
                };
                expect_bad_cotangent(std::vector<torch::Tensor>(steps-1));
                auto bad_shape=only4; bad_shape[3]=torch::zeros({total+1},torch::kFloat64);
                expect_bad_cotangent(bad_shape);
                auto bad_dtype=only4; bad_dtype[3]=torch::zeros({total},torch::kFloat32);
                expect_bad_cotangent(bad_dtype);
                auto bad_finite=only4; bad_finite[3]=output_cotangents[3].clone();
                bad_finite[3][0]=std::numeric_limits<double>::quiet_NaN();
                expect_bad_cotangent(bad_finite);
            }
        }
        TORCH_CHECK(gradient.scalar_type()==torch::kFloat64,"initial pullback lost precision");
    }
    tile.solver.closeFixedTrajectory();
    return {obj.value,gradient,last,std::move(checkpoints),std::move(output_cotangents),
            time2_only,time4_only,terminal_vector,legacy_terminal,zero_cotangents,shifted_time2};
}
void check_primal(const torch::Tensor& z0,const Result& production,int steps) {
    // Explicit FP64 reentry on the same fixed auxiliary context. Close each
    // one-step tape to clear solver-owned carry, then bootstrap the checkpoint.
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    auto previous=z0;
    for(int n=0;n<steps;++n) {
        tile.solver.requestFixedTrajectory(1,{dt},previous);
        int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(dt);
        previous=tile.solver.getFixedTrajectoryFp64Checkpoints().front();
        TORCH_CHECK(torch::equal(previous,production.states[n]),
                    "retained carry changed explicit-reentry checkpoint ",n);
        tile.solver.closeFixedTrajectory();
    }
}
void check_initial_contract(const torch::Tensor& z0) {
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    auto nan=z0.clone(); nan[0]=std::numeric_limits<double>::quiet_NaN();
    for(const auto& bad : {z0.to(torch::kFloat32),torch::zeros({total+1},torch::kFloat64),nan}) {
        bool rejected=false;
        try { tile.solver.requestFixedTrajectory(2,{dt,dt},bad); }
        catch(const c10::Error&) { rejected=true; }
        TORCH_CHECK(rejected && !tile.solver.fixedTrajectoryRequested(),
                    "malformed bootstrap armed a trajectory");
    }
    tile.solver.requestFixedTrajectory(2,{dt,dt},z0+1.0);
    int host=1; sdirk3_set_timestep_i4(&host);
    bool rejected=false;
    try { tile.step(dt); } catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"bootstrap publication mismatch accepted");
    tile.solver.invalidateCaches();
}
void check_metric_precision(const torch::Tensor& z0) {
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    tile.solver.requestFixedTrajectory(2,{dt,dt},z0);
    int host=1; sdirk3_set_timestep_i4(&host); tile.step(dt);
    auto grid=tile.solver.getGridInfo();
    grid->rdnw=grid->rdnw.to(torch::kFloat64).clone();
    auto published=grid->rdnw.to(torch::kFloat32);
    grid->rdnw.add_(1e-10);
    TORCH_CHECK(torch::equal(published,grid->rdnw.to(torch::kFloat32)),
                "metric perturbation was not below FP32 resolution");
    host=2; sdirk3_set_timestep_i4(&host);
    bool rejected=false;
    try { tile.step(dt); } catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"sub-FP32-ULP metric change was silently reused");
    tile.solver.closeFixedTrajectory();
}
void check_cancelled_bootstrap(const torch::Tensor& z0) {
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    tile.solver.requestFixedTrajectory(2,{dt,dt},z0);
    tile.solver.cancelFixedTrajectoryRequest();
    wrf::sdirk3::g_sdirk3_config.retain_graph_for_adjoint=false;
    int host=1; sdirk3_set_timestep_i4(&host); tile.step(dt);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode()==0,
                "cancelled bootstrap contaminated ordinary forward carry");
}
void check_incomplete(const torch::Tensor& z0) {
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    tile.solver.requestFixedTrajectory(2,{dt,dt},z0);
    int host=1; sdirk3_set_timestep_i4(&host); tile.step(dt);
    bool rejected=false;
    try { tile.solver.pullbackFixedTrajectory(torch::zeros_like(z0)); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"incomplete trajectory accepted a pullback");
    auto snapshots=tile.solver.getFixedTrajectoryFp64Checkpoints();
    auto original=snapshots.front().clone(); snapshots.front().zero_();
    TORCH_CHECK(torch::equal(original,tile.solver.getFixedTrajectoryFp64Checkpoints().front()),
                "checkpoint getter exposed mutable retained state");
    wrf::sdirk3::g_sdirk3_config.khdif+=1.0f;
    host=2; sdirk3_set_timestep_i4(&host);
    rejected=false;
    try { tile.step(dt); } catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"changed coefficient context was silently reused");
    tile.solver.closeFixedTrajectory();
    rejected=false;
    try { tile.solver.pullbackFixedTrajectory(torch::zeros_like(z0)); }
    catch(const c10::Error&) { rejected=true; }
    TORCH_CHECK(rejected,"closed trajectory accepted a pullback");
}
void check_derivatives(const torch::Tensor& z0,int steps) {
    const auto on=run(z0,steps,true,true),off=run(z0,steps,false,true);
    check_primal(z0,on,steps);
    for(int block : {2,3,5}) {
        auto v=direction(block);
        const double ad=v.dot(on.gradient).item<double>();
        const double increment_ad=v.dot(on.gradient-off.gradient).item<double>();
        double previous=0;
        for(double eps : {0.02,0.01,0.005}) {
            auto p=run(z0+eps*v,steps,true,false),m=run(z0-eps*v,steps,true,false);
            auto p0=run(z0+eps*v,steps,false,false),m0=run(z0-eps*v,steps,false,false);
            double fd=(p.value-m.value)/(2*eps);
            double inc=((p.value-p0.value)-(m.value-m0.value))/(2*eps);
            double floor=256*std::numeric_limits<double>::epsilon()*
                (std::abs(p.value)+std::abs(m.value)+std::abs(p0.value)+std::abs(m0.value))/(2*eps);
            double error=std::abs(inc-increment_ad),budget=floor+2e-3*std::max(std::abs(inc),std::abs(increment_ad));
            double remainder=std::abs(p.value-on.value-eps*ad);
            std::cout<<"FP64_CARRY_ADJOINT steps="<<steps<<" block="<<block<<" eps="<<eps
                <<" fd="<<fd<<" ad="<<ad<<" increment_fd="<<inc<<" increment_ad="<<increment_ad
                <<" increment_floor="<<floor<<" increment_error="<<error<<" budget="<<budget
                <<" Taylor="<<remainder<<" ratio="<<(previous ? previous/remainder : 0)<<'\n';
            TORCH_CHECK(std::abs(fd-ad)<=floor+5e-4*std::max(std::abs(fd),std::abs(ad)),"full derivative mismatch");
            TORCH_CHECK(std::abs(inc)>50*floor,"active derivative signal unresolved");
            TORCH_CHECK(error<=budget,"active increment derivative mismatch");
            TORCH_CHECK(std::abs(inc-2*increment_ad)>budget,
                        "doubled diffusion sensitivity mutation was not rejected");
            if(previous) TORCH_CHECK(previous/remainder>3.5 && previous/remainder<4.5,"Taylor remainder not quadratic");
            previous=remainder;
            if(steps==4 && block==2 && eps==0.01) {
                double wrong=v.dot(on.last_only-off.last_only).item<double>();
                TORCH_CHECK(std::abs(inc-wrong)>budget,"missing reverse steps mutation was not rejected");
            }
        }
    }
}
torch::Tensor physical_core_indices(int block) {
    std::vector<int64_t> indices;
    const int64_t w0=su+sv, ph0=w0+sw, mu0=ph0+sw+st;
    if(block<2) {
        const int64_t offset=block==0?w0:ph0;
        indices.reserve((ny-1)*(nx-1)*(nw-2));
        for(int j=0;j<ny-1;++j) for(int k=1;k<nw-1;++k) for(int i=0;i<nx-1;++i)
            indices.push_back(offset+(j*nw+k)*nx+i);
    } else {
        indices.reserve((ny-1)*(nx-1));
        for(int j=0;j<ny-1;++j) for(int i=0;i<nx-1;++i)
            indices.push_back(mu0+j*nx+i);
    }
    return torch::from_blob(indices.data(),{static_cast<int64_t>(indices.size())},
        torch::TensorOptions().dtype(torch::kInt64).device(torch::kCPU)).clone();
}
torch::Tensor physical_theta_values(const torch::Tensor& state) {
    std::vector<int64_t> indices;
    const int64_t offset=su+sv+2*sw;
    indices.reserve((ny-1)*(nx-1)*nz);
    for(int j=0;j<ny-1;++j) for(int k=0;k<nz;++k) for(int i=0;i<nx-1;++i)
        indices.push_back(offset+(j*nz+k)*nx+i);
    auto index=torch::from_blob(indices.data(),{static_cast<int64_t>(indices.size())},
        torch::TensorOptions().dtype(torch::kInt64).device(torch::kCPU)).clone();
    return state.index_select(0,index);
}
struct PhysicalFieldRmse { double w,phi,mu,theta; };
PhysicalFieldRmse physical_field_rmse(const TileCase& geometry,
                                      const torch::Tensor& estimate,const torch::Tensor& truth) {
    const bool packed=geometry.packedPeriodicLayout();
    const int m=ny-(packed?1:0),n=nx-(packed?1:0);
    const auto difference=estimate-truth;
    const auto rmse=[&](int offset,int levels,int count) {
        return difference.slice(0,offset,offset+count).view({ny,levels,nx})
            .slice(0,0,m).slice(2,0,n).square().mean().sqrt().item<double>();
    };
    return {rmse(su+sv,nw,sw),rmse(su+sv+sw,nw,sw),rmse(total-sm,1,sm),
            rmse(su+sv+2*sw,nz,st)};
}
torch::Tensor physical_state_block_values(const TileCase& geometry,
                                          const torch::Tensor& state,int block) {
    const bool packed=geometry.packedPeriodicLayout();
    const int m=ny-(packed?1:0),n=nx-(packed?1:0);
    TORCH_CHECK(state.dim()==1 && state.numel()==total && block>=0 && block<6,
                "invalid state block extraction");
    switch(block) {
    case 0:
        return state.slice(0,0,su).view({ny,nz,nu})
            .slice(0,0,m).slice(2,0,n+1).reshape(-1);
    case 1:
        return state.slice(0,su,su+sv).view({nv,nz,nx})
            .slice(0,0,m+1).slice(2,0,n).reshape(-1);
    case 2:
        return state.slice(0,su+sv,su+sv+sw).view({ny,nw,nx})
            .slice(0,0,m).slice(2,0,n).reshape(-1);
    case 3:
        return state.slice(0,su+sv+sw,su+sv+2*sw).view({ny,nw,nx})
            .slice(0,0,m).slice(2,0,n).reshape(-1);
    case 4:
        return state.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx})
            .slice(0,0,m).slice(2,0,n).reshape(-1);
    default:
        return state.slice(0,total-sm,total).view({ny,1,nx})
            .slice(0,0,m).slice(2,0,n).reshape(-1);
    }
}
torch::Tensor raw_inverse_control(int block,int phase) {
    auto raw=torch::zeros({total},torch::TensorOptions().dtype(torch::kFloat64));
    const int64_t w0=su+sv, ph0=w0+sw, mu0=ph0+sw+st;
    const int mode=block+1;  // W, PH, and MU use distinct sub-Nyquist X harmonics.
    const int64_t offset=block==0?w0:(block==1?ph0:mu0);
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        const double xphase=2.0*std::acos(-1.0)*mode*i/(nx-1);
        const double x=phase==0?std::sin(xphase):std::cos(xphase);
        const double y=1.0;  // Constant in symmetric Y; distinct X harmonics separate controls.
        if(block<2) {
            for(int k=0;k<nw;++k) {
                const double vertical=(k==0 || k==nw-1)?0.0:
                    std::sin(std::acos(-1.0)*k/(nw-1));
                raw[offset+(j*nw+k)*nx+i]=x*y*vertical;
            }
        } else {
            raw[offset+j*nx+i]=x*y;
        }
    }
    return raw;
}
struct InverseBasis { torch::Tensor state, columns; };
InverseBasis inverse_basis(TileCase& geometry,const torch::Tensor& raw_initial) {
    const auto zero=torch::zeros_like(raw_initial);
    const auto pzero=geometry.projectControlState(zero);
    const auto xb=geometry.projectControlState(raw_initial);
    std::vector<torch::Tensor> columns;
    columns.reserve(6);
    for(int block=0;block<3;++block) for(int phase=0;phase<2;++phase) {
        const auto projected=geometry.projectControlState(raw_inverse_control(block,phase))-pzero;
        const double norm=projected.index_select(0,physical_core_indices(block)).norm().item<double>();
        TORCH_CHECK(std::isfinite(norm) && norm>0.0,"empty inverse control core");
        const double scale=block==0?0.1:100.0;
        const auto column=projected*(scale/norm);
        TORCH_CHECK(torch::allclose(geometry.projectControlState(column),column,1e-13,1e-13),
                    "inverse control is outside the projected state space");
        const double core_norm=column.index_select(0,physical_core_indices(block)).norm().item<double>();
        TORCH_CHECK(std::abs(core_norm-scale)<=1e-11*scale,"inverse control core scale mismatch");
        columns.push_back(column);
    }
    validate_physical_state(geometry,xb,"projected inverse background");
    return {xb,torch::stack(columns,1)};
}
double observation_loss(const torch::Tensor& final_w,const torch::Tensor& observations,double observation_scale) {
    TORCH_CHECK(final_w.dim()==1 && observations.dim()==1 &&
                final_w.numel()==observations.numel(),
                "observation loss requires matching physical-W vectors");
    return physical_observation_cost(final_w,observations,
        observation_scale*std::sqrt(static_cast<double>(observations.numel())));
}
double physical_observation_cost(const torch::Tensor& predicted,const torch::Tensor& observed,
                                double physical_std) {
    TORCH_CHECK(predicted.dim()==1 && observed.dim()==1 &&
                predicted.numel()==observed.numel() && std::isfinite(physical_std) && physical_std>0.0,
                "physical observation vectors or standard deviation are invalid");
    return 0.5*((predicted-observed)/physical_std).square().sum().item<double>();
}
struct MapEval { double value=0.0, data=0.0; torch::Tensor gradient; Result simulation; };
struct TrialRejectionTally { int physical=0, nonconverged=0; std::string last; };
template <class Evaluate>
bool try_admissible_trial(Evaluate&& evaluate,TrialRejectionTally* tally=nullptr) {
    try {
        evaluate();
        return true;
    } catch(const PhysicalTrialRejection&) {
        if(tally) { ++tally->physical; tally->last="physical"; }
        return false;
    } catch(const NonConvergedTrialRejection&) {
        if(tally) { ++tally->nonconverged; tally->last="nonconverged"; }
        return false;
    }
}
void check_trial_rejection_policy() {
    TORCH_CHECK(try_admissible_trial([]{requireAcceptedTrialStep(
        static_cast<int>(wrf::sdirk3::StepOutcomeCode::OK_ADVANCED));}),
        "accepted solver outcome was rejected by trial policy");
    for(const auto outcome : {wrf::sdirk3::StepOutcomeCode::HARD_STAGE_ABORT,
                              wrf::sdirk3::StepOutcomeCode::SOFT_NO_PROGRESS})
        TORCH_CHECK(!try_admissible_trial([&]{requireAcceptedTrialStep(static_cast<int>(outcome));}),
                    "recoverable solver outcome did not request a backtrack");
    for(const auto outcome : {1,100,101}) {
        bool fatal_propagated=false;
        try { (void)try_admissible_trial([&]{requireAcceptedTrialStep(outcome);}); }
        catch(const c10::Error&) { fatal_propagated=true; }
        TORCH_CHECK(fatal_propagated,"fatal solver outcome was swallowed: ",outcome);
    }
    TORCH_CHECK(try_admissible_trial([]{}),"ordinary trial was rejected");
    TORCH_CHECK(!try_admissible_trial([]{throw PhysicalTrialRejection("synthetic physical rejection");}),
                "physical trial rejection was not classified as a backtrack");
    TORCH_CHECK(!try_admissible_trial([]{throw NonConvergedTrialRejection("synthetic convergence rejection");}),
                "nonconverged trial was not classified as a backtrack");
    bool logic_propagated=false;
    try { (void)try_admissible_trial([]{throw std::logic_error("synthetic programming error");}); }
    catch(const std::logic_error&) { logic_propagated=true; }
    TORCH_CHECK(logic_propagated,"std::logic_error was swallowed by trial policy");
    bool contract_propagated=false;
    try { (void)try_admissible_trial([]{TORCH_CHECK(false,"synthetic c10 contract failure");}); }
    catch(const c10::Error&) { contract_propagated=true; }
    TORCH_CHECK(contract_propagated,"c10 contract error was swallowed by trial policy");
}
MapEval evaluate_map(const InverseBasis& basis,const torch::Tensor& coefficients,
                     const torch::Tensor& observations,double observation_scale,bool pullback) {
    const auto initial_state=basis.state+basis.columns.mv(coefficients);
    auto simulation=run(initial_state,4,true,pullback,observations,observation_scale);
    const double prior=0.5*coefficients.square().sum().item<double>();
    torch::Tensor gradient;
    if(pullback) gradient=coefficients+basis.columns.t().mv(simulation.gradient);
    return {prior+simulation.value,simulation.value,gradient,std::move(simulation)};
}
void run_inverse_experiment(const torch::Tensor& raw_initial) {
    constexpr double observation_scale=1.0e-3;
    constexpr int steps=4;
    check_trial_rejection_policy();
    configure(true);
    TileCase geometry(5000.0f); prepare_native(geometry);
    const auto basis=inverse_basis(geometry,raw_initial);
    const auto truth=torch::tensor({0.06,-0.04,0.05,-0.035,0.04,-0.025},
        torch::TensorOptions().dtype(torch::kFloat64));
    const auto truth_state=basis.state+basis.columns.mv(truth);
    const auto truth_run=run(truth_state,steps,true,false);
    const auto observations=physical_w_values(truth_run.states.back());
    const auto background_run=run(basis.state,steps,true,false);
    const auto background_w=physical_w_values(background_run.states.back());
    auto coefficients=torch::zeros({6},torch::TensorOptions().dtype(torch::kFloat64));
    auto current=evaluate_map(basis,coefficients,observations,observation_scale,true);
    const double start_value=current.value, start_data=current.data;
    const double start_gradient=current.gradient.norm().item<double>();

    // Independent central-FD columns at two widths. Reuse these forward outputs
    // for identifiability, scalar-gradient, and Taylor checks.
    constexpr double fd_width=0.02;
    std::vector<torch::Tensor> jacobian_columns;
    std::vector<torch::Tensor> coarse_jacobian_columns;
    jacobian_columns.reserve(6);
    coarse_jacobian_columns.reserve(6);
    for(int k=0;k<6;++k) {
        auto e=torch::zeros({6},coefficients.options()); e[k]=1.0;
        const auto plus_h=run(basis.state+basis.columns.mv(fd_width*e),steps,true,false);
        const auto minus_h=run(basis.state-basis.columns.mv(fd_width*e),steps,true,false);
        const auto plus_half=run(basis.state+basis.columns.mv((fd_width/2.0)*e),steps,true,false);
        const auto minus_half=run(basis.state-basis.columns.mv((fd_width/2.0)*e),steps,true,false);
        const auto wp=physical_w_values(plus_h.states.back());
        const auto wm=physical_w_values(minus_h.states.back());
        const auto wph=physical_w_values(plus_half.states.back());
        const auto wmh=physical_w_values(minus_half.states.back());
        const auto jh=(wp-wm)/(2.0*fd_width);
        const auto jhalf=(wph-wmh)/fd_width;
        const double jscale=std::max(jh.norm().item<double>(),jhalf.norm().item<double>());
        TORCH_CHECK((jh-jhalf).norm().item<double>()<=1e-2*std::max(jscale,1e-14),
                    "FD observation-Jacobian column unstable at two widths: ",k);
        const double whitening=observation_scale*
            std::sqrt(static_cast<double>(observations.numel()));
        jacobian_columns.push_back(jhalf/whitening);
        coarse_jacobian_columns.push_back(jh/whitening);

        const double fph=observation_loss(wp,observations,observation_scale)+0.5*fd_width*fd_width;
        const double fmh=observation_loss(wm,observations,observation_scale)+0.5*fd_width*fd_width;
        const double fphalf=observation_loss(wph,observations,observation_scale)+0.5*(fd_width/2.0)*(fd_width/2.0);
        const double fmhalf=observation_loss(wmh,observations,observation_scale)+0.5*(fd_width/2.0)*(fd_width/2.0);
        const double fd_gradient=(fphalf-fmhalf)/fd_width;
        const double adjoint_gradient=current.gradient[k].item<double>();
        const double grad_floor=512.0*std::numeric_limits<double>::epsilon()*
            (std::abs(fphalf)+std::abs(fmhalf)+2.0*start_value)/fd_width;
        const double grad_budget=grad_floor+5e-4*
            std::max(std::abs(fd_gradient),std::abs(adjoint_gradient));
        if(std::max(std::abs(fd_gradient),std::abs(adjoint_gradient))<=50.0*grad_floor) {
            std::cout<<"INVERSE_FD_UNRESOLVED control="<<k
                     <<" fd="<<fd_gradient<<" ad="<<adjoint_gradient
                     <<" floor="<<grad_floor<<" (SVD gate checks column observability)\n";
            continue;
        }
        TORCH_CHECK(std::abs(fd_gradient-adjoint_gradient)<=grad_budget,
                    "inverse scalar FD/adjoint mismatch for control ",k,
                    ": fd=",fd_gradient," ad=",adjoint_gradient," tol=",grad_budget);
        const double r_h=std::abs(fph-start_value-fd_width*adjoint_gradient);
        const double r_half=std::abs(fphalf-start_value-(fd_width/2.0)*adjoint_gradient);
        const double taylor_floor=512.0*std::numeric_limits<double>::epsilon()*
            (std::abs(fph)+std::abs(fphalf)+2.0*start_value);
        std::cout<<"INVERSE_DERIVATIVE control="<<k<<" fd="<<fd_gradient
                 <<" ad="<<adjoint_gradient<<" error="<<std::abs(fd_gradient-adjoint_gradient)
                 <<" budget="<<grad_budget<<" floor="<<grad_floor
                 <<" taylor_ratio="<<r_h/r_half<<"\n";
        TORCH_CHECK(r_half>50.0*taylor_floor && r_h/r_half>3.5 && r_h/r_half<4.5,
                    "inverse Taylor remainder is not quadratic for control ",k,
                    ": ratio=",r_h/r_half," floor=",taylor_floor);
    }
    const auto A=torch::stack(jacobian_columns,1);
    const auto A_coarse=torch::stack(coarse_jacobian_columns,1);
    const double fd_operator_uncertainty=(A-A_coarse).norm().item<double>();
    const auto singular=torch::linalg_svdvals(A);
    const double smax=singular.max().item<double>(), smin=singular.min().item<double>();
    const double identifiability_floor=256.0*std::numeric_limits<double>::epsilon()*
        std::max(1.0,A.norm().item<double>());
    const double identifiability_bound=50.0*fd_operator_uncertainty+identifiability_floor;
    const double condition=smax/smin;
    TORCH_CHECK(std::isfinite(smin) && smin>identifiability_bound && condition<1e6,
                "six-control observation Jacobian is not identifiable: smin=",smin,
                " bound=",identifiability_bound," fd_uncertainty=",fd_operator_uncertainty,
                " condition=",condition);
    const auto metric=torch::eye(6,A.options())+A.t().matmul(A);
    const auto whitened_residual=(observations-background_w)/
        (observation_scale*std::sqrt(static_cast<double>(observations.numel())));
    const auto linear_map=torch::linalg_solve(metric,A.t().mv(whitened_residual));
    TORCH_CHECK(wrf::sdirk3::guarded_item<bool>(torch::isfinite(linear_map).all()),
                "linear MAP reference is non-finite");
    std::cout<<"INVERSE_IDENTIFIABILITY obs="<<observations.numel()
             <<" rank=6 smax="<<smax<<" smin="<<smin<<" cond="<<condition
             <<" fd_uncertainty="<<fd_operator_uncertainty
             <<" ident_bound="<<identifiability_bound
             <<" observation_scale="<<observation_scale<<"\n";
    std::cout<<"INVERSE_REFERENCE truth="<<truth<<" linear_map="<<linear_map<<"\n";

    constexpr double armijo=1e-4;
    int accepted=0, rejected=0;
    for(int iteration=0;iteration<15;++iteration) {
        const double gnorm=current.gradient.norm().item<double>();
        if(gnorm<1e-7) break;
        const auto direction=-torch::linalg_solve(metric,current.gradient);
        const double slope=current.gradient.dot(direction).item<double>();
        TORCH_CHECK(std::isfinite(slope) && slope<0.0,"inverse metric direction is not descent");
        bool found=false;
        double alpha=1.0;
        for(int line=0;line<12;++line,alpha*=0.5) {
            const auto trial_coefficients=coefficients+alpha*direction;
            MapEval trial;
            if(try_admissible_trial([&]{
                    trial=evaluate_map(basis,trial_coefficients,observations,observation_scale,true);
                })) {
                if(trial.value<=current.value+armijo*alpha*slope) {
                    std::cout<<"INVERSE_STEP iter="<<accepted<<" alpha="<<alpha
                             <<" J="<<current.value<<" -> "<<trial.value
                             <<" data="<<trial.data<<" |g|="<<gnorm<<"\n";
                    coefficients=trial_coefficients;
                    current=std::move(trial);
                    ++accepted;
                    found=true;
                    break;
                }
                ++rejected;
            } else {
                ++rejected;  // Invalid physical state or failed forward trial: reject, never clip.
            }
        }
        TORCH_CHECK(found,"inverse Armijo search found no admissible descent step");
    }
    TORCH_CHECK(accepted>0 && current.value<start_value && current.data<start_data &&
                current.gradient.norm().item<double>()<start_gradient,
                "inverse solve did not improve objective, observation misfit, and gradient");
    const double final_gradient=current.gradient.norm().item<double>();
    const double gradient_tolerance=1e-6*std::max(1.0,start_gradient);
    TORCH_CHECK(final_gradient<=gradient_tolerance,
                "inverse final gradient did not converge: |g|=",final_gradient,
                " tolerance=",gradient_tolerance);
    const double linear_gap=(coefficients-linear_map).norm().item<double>();
    const double linear_gap_tolerance=1e-2*std::max(1.0,linear_map.norm().item<double>());
    TORCH_CHECK(linear_gap<=linear_gap_tolerance,
                "small-signal nonlinear MAP differs too far from linear reference: gap=",
                linear_gap," tolerance=",linear_gap_tolerance);
    const auto replay=evaluate_map(basis,coefficients,observations,observation_scale,true);
    TORCH_CHECK(replay.value==current.value && torch::equal(replay.gradient,current.gradient),
                "inverse objective/gradient evaluation is not deterministic");
    TORCH_CHECK(replay.simulation.states.size()==current.simulation.states.size(),
                "inverse replay checkpoint count changed");
    for(size_t n=0;n<replay.simulation.states.size();++n)
        TORCH_CHECK(torch::equal(replay.simulation.states[n],current.simulation.states[n]),
                    "inverse replay is not deterministic at checkpoint ",n);
    const auto final_w=physical_w_values(current.simulation.states.back());
    const double final_rmse=std::sqrt(2.0*current.data);
    const double initial_rmse=std::sqrt(2.0*start_data);
    std::cout<<"INVERSE_RESULT accepted="<<accepted<<" rejected="<<rejected
             <<" truth="<<truth<<" nonlinear_map="<<coefficients
             <<" linear_map="<<linear_map<<" |g|="<<start_gradient
             <<" -> "<<final_gradient<<" normalized_observation_rmse="<<initial_rmse
             <<" -> "<<final_rmse<<" physical_w_rmse="<<observation_scale*initial_rmse
             <<" -> "<<observation_scale*final_rmse<<" J="<<start_value<<" -> "<<current.value
             <<" linear_gap="<<linear_gap<<"/"<<linear_gap_tolerance
             <<" w_norm="<<final_w.norm().item<double>()<<"\n";
    std::cout<<"INVERSE_SUMMARY J_initial="<<start_value<<" J_final="<<current.value
             <<" data_initial="<<start_data<<" data_final="<<current.data
             <<" gradient_initial="<<start_gradient<<" gradient_final="<<final_gradient
             <<" physical_w_rmse_initial="<<observation_scale*initial_rmse
             <<" physical_w_rmse_final="<<observation_scale*final_rmse
             <<" linear_gap="<<linear_gap<<" accepted="<<accepted<<" rejected="<<rejected<<"\n";
    for(int i=0;i<6;++i)
        std::cout<<"INVERSE_CONTROL index="<<i<<" truth="<<truth[i].item<double>()
                 <<" estimate="<<coefficients[i].item<double>()
                 <<" linear_map="<<linear_map[i].item<double>()
                 <<" final_gradient="<<current.gradient[i].item<double>()<<"\n";
    for(int i=0;i<6;++i)
        std::cout<<"INVERSE_SINGULAR index="<<i<<" value="<<singular[i].item<double>()<<"\n";
}

struct TwoTimeEval { double value=0.0,data=0.0; torch::Tensor gradient; Result simulation; };
struct WindowSpec {
    int analysis_steps=4;
    float step_dt=dt;
    int forecast_steps=0;
    float kv=10.0f;
    float newton_tol=1e-10f;
    std::vector<torch::Tensor> fixed_observations;
};
struct TwoTimeOutcome {
    torch::Tensor controls,gradient,truth,background;
    std::vector<torch::Tensor> observations;
    double value=0.0,data=0.0;
    torch::Tensor generated_truth_end,background_end,analysis_end;
    torch::Tensor initial_gradient,forecast_end;
};
TwoTimeOutcome run_two_time_experiment(const torch::Tensor& raw_initial,bool nonlinear=false,
                                       const WindowSpec& window=WindowSpec{});
std::array<double,6> block_step_drift(const torch::Tensor& tendency,double dt,const char* label) {
    const int sizes[]={su,sv,sw,sw,st,sm};
    const double unit_floors[]={1.0,1.0,1.0,100.0,1.0,100.0};
    const char* names[]={"U","V","W","PH","THETA","MU"};
    TORCH_CHECK(tendency.dim()==1 && tendency.numel()==total &&
                wrf::sdirk3::guarded_item<bool>(torch::isfinite(tendency).all()),
                label," RHS is malformed or non-finite");
    std::array<double,6> scaled{};
    int offset=0;
    for(int block=0;block<6;++block) {
        const double maximum=tendency.slice(0,offset,offset+sizes[block])
            .abs().max().item<double>();
        scaled[block]=dt*maximum/unit_floors[block];
        std::cout<<"BALANCED_DIAGNOSTIC kind="<<label<<" block="<<names[block]
                 <<" max_abs="<<maximum<<" unit_floor="<<unit_floors[block]
                 <<" dt_scaled="<<scaled[block]<<"\n";
        offset+=sizes[block];
    }
    return scaled;
}
void run_balanced_inverse_experiment(bool windows=false) {
    constexpr double dt_seconds=0.25;
    configure(true);
    TileCase tile(5000.0f); prepare_native(tile);
    double pressure_floor=0.0;
    const auto background=tile.nonzeroHydrostaticBackground(&pressure_floor);
    TORCH_CHECK(torch::allclose(tile.projectControlState(background),background,1e-13,1e-13),
                "nonzero hydrostatic background violates the solver boundary projector");
    // The setup-only call has not prepared native ARK face coefficients.
    // Use the actual equilibrium window to prepare the live context; its
    // checkpoints are themselves gated below, with no unmeasured warmup step.
    tile.set(background);
    tile.solver.requestFixedTrajectory(4,std::vector<float>(4,dt),background);
    for(int n=0;n<4;++n) {
        int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(dt);
    }
    const auto checkpoints=tile.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(checkpoints.size()==4,"hydrostatic window did not complete four steps");
    for(const auto& checkpoint : checkpoints)
        validate_physical_state(tile,checkpoint,"hydrostatic accepted checkpoint");
    const auto full=tile.rhsAt(background,wrf::sdirk3::RhsMode::Full,dt_seconds,background)
        .to(torch::kFloat64);
    const auto explicit_rhs=tile.rhsAt(background,wrf::sdirk3::RhsMode::ExplicitOnly,
                                        dt_seconds,background).to(torch::kFloat64);
    const auto implicit_rhs=tile.rhsAt(background,wrf::sdirk3::RhsMode::ImplicitOnly,
                                        dt_seconds,background).to(torch::kFloat64);
    const auto full_scaled=block_step_drift(full,dt_seconds,"Full");
    const auto explicit_scaled=block_step_drift(explicit_rhs,dt_seconds,"ExplicitOnly");
    const auto implicit_scaled=block_step_drift(implicit_rhs,dt_seconds,"ImplicitOnly");
    const auto split_error=block_step_drift(full-explicit_rhs-implicit_rhs,dt_seconds,
                                            "Full-minus-Explicit-minus-Implicit");
    const double rdnw_abs=std::abs(tile.metric[0]);
    const double mass_full=84000.0;
    const double gravity=tile.solver.getGridInfo()->g;
    // Pressure-error pairs feed W acceleration; compare its dt-integrated
    // velocity with the 1 m/s reference unit used by block_step_drift.
    const double w_pressure_rhs_budget=dt_seconds*gravity*(2.0*rdnw_abs)*pressure_floor/mass_full;
    const double fp64_scaled_floor=256.0*std::numeric_limits<double>::epsilon();
    const double rhs_budgets[]={fp64_scaled_floor,fp64_scaled_floor,w_pressure_rhs_budget,
                                fp64_scaled_floor,fp64_scaled_floor,fp64_scaled_floor};
    const int sizes[]={su,sv,sw,sw,st,sm};
    const double unit_floors[]={1.0,1.0,1.0,100.0,1.0,100.0};
    const char* names[]={"U","V","W","PH","THETA","MU"};
    for(int block=0;block<6;++block) {
        const double closure_budget=256.0*std::numeric_limits<double>::epsilon()*
            std::max(1.0,full_scaled[block]+explicit_scaled[block]+implicit_scaled[block]);
        std::cout<<"BALANCED_RHS_GATE block="<<names[block]
                 <<" full_scaled="<<full_scaled[block]<<" full_budget="<<rhs_budgets[block]
                 <<" split_error="<<split_error[block]<<" split_budget="<<closure_budget
                 <<" unit_floor="<<unit_floors[block]<<"\n";
        TORCH_CHECK(full_scaled[block]<=rhs_budgets[block],
                    "nonzero hydrostatic Full RHS exceeds EOS/FP64 budget, block ",names[block],
                    " scaled=",full_scaled[block]," budget=",rhs_budgets[block]);
        TORCH_CHECK(split_error[block]<=closure_budget,
                    "Full != ExplicitOnly + ImplicitOnly at the hydrostatic background, block ",
                    names[block]," scaled error=",split_error[block]," budget=",closure_budget);
    }

    for(size_t step=0;step<checkpoints.size();++step) {
        int offset=0;
        for(int block=0;block<6;++block) {
            const double maximum=(checkpoints[step]-background)
                .slice(0,offset,offset+sizes[block]).abs().max().item<double>();
            const double scaled=maximum/unit_floors[block];
            const double reference_magnitude=background.slice(0,offset,offset+sizes[block])
                .abs().max().item<double>()/unit_floors[block];
            // This is an equilibrium precision gate, not a conversion of the
            // K-form Newton residual tolerance into a state-error bound.
            const double state_roundoff=512.0*std::numeric_limits<double>::epsilon()*
                std::max(1.0,reference_magnitude);
            const double stationarity_budget=4.0*(rhs_budgets[block]+state_roundoff);
            std::cout<<"BALANCED_STATIONARITY step="<<(step+1)<<" block="<<names[block]
                     <<" max_drift="<<maximum<<" unit_floor="<<unit_floors[block]
                     <<" scaled_drift="<<scaled<<" predicted_budget="<<stationarity_budget
                     <<" state_roundoff="<<state_roundoff<<"\n";
            TORCH_CHECK(std::isfinite(scaled) && scaled<=stationarity_budget,
                        "four-step hydrostatic background drift exceeds RHS+Newton budget, step ",
                        step+1," block=",names[block]," scaled=",scaled,
                        " budget=",stationarity_budget);
            offset+=sizes[block];
        }
    }
    auto unbalanced=background.clone();
    unbalanced.slice(0,su+sv+sw,su+sv+2*sw).view({ny,nw,nx}).select(1,1).add_(1.0);
    validate_physical_state(tile,unbalanced,"hydrostatic falsifier");
    const auto bad_rhs=tile.rhsAt(unbalanced,wrf::sdirk3::RhsMode::Full,dt_seconds,background);
    const auto bad_scaled=block_step_drift(bad_rhs,dt_seconds,"PH-layer-falsifier");
    TORCH_CHECK(bad_scaled[2]>100.0*rhs_budgets[2],
                "broken PH hydrostatic balance was not distinguished from the equilibrium budget");
    std::cout<<"BALANCED_FALSIFIER delta_phi=1 scaled_w="<<bad_scaled[2]
             <<" equilibrium_budget="<<rhs_budgets[2]<<"\n";
    tile.solver.closeFixedTrajectory();
    std::cout<<"BALANCED_INVERSE truth_is_equilibrium=false controls_perturb_background=true"
             <<" pressure_roundoff_floor="<<pressure_floor
             <<" w_rhs_budget="<<w_pressure_rhs_budget
             <<" newton_tol="<<wrf::sdirk3::g_sdirk3_config.newton_tol<<"\n";
    if(windows) {
        for(int analysis_steps : {8,16}) {
            WindowSpec window;
            window.analysis_steps=analysis_steps;
            window.step_dt=dt;
            window.forecast_steps=analysis_steps/2;
            const auto outcome=run_two_time_experiment(background,false,window);
            std::cout<<"WINDOW_OUTCOME steps="<<analysis_steps<<" dt="<<window.step_dt
                     <<" forecast_steps="<<window.forecast_steps
                     <<" controls="<<outcome.controls<<" gradient="<<outcome.gradient<<"\n";
        }
    } else {
        run_two_time_experiment(background,false);
    }
}
TwoTimeOutcome run_two_time_experiment(const torch::Tensor& raw_initial,bool nonlinear,
                                      const WindowSpec& window) {
    const int steps=window.analysis_steps;
    TORCH_CHECK(steps>=2 && steps%2==0 && window.forecast_steps>=0,
                "two-time window requires a positive even analysis length and nonnegative forecast");
    TORCH_CHECK(std::isfinite(window.step_dt) && window.step_dt>0.0f &&
                std::isfinite(window.newton_tol) && window.newton_tol>0.0f,
                "two-time window timestep/Newton tolerance is invalid");
    const int first_observation=steps/2-1;
    const int final_observation=steps-1;
    constexpr double point_scale=1.0e-3;
    const double physical_std=std::sqrt((ny-1)*(nx-1)*(nw-2))*point_scale;
    check_trial_rejection_policy();
    configure(true,window.kv);
    TileCase geometry(5000.0f); prepare_native(geometry);
    const auto basis=inverse_basis(geometry,raw_initial);
    auto truth=torch::tensor({0.06,-0.04,0.05,-0.035,0.04,-0.025},
        torch::TensorOptions().dtype(torch::kFloat64));
    double truth_amplitude=1.0;
    wrf::sdirk3::test::PerturbationSize truth_size{};
    if(nonlinear) {
        const auto nominal_increment=basis.columns.mv(truth);
        truth_size=geometry.perturbationSize(basis.state,nominal_increment);
        TORCH_CHECK(truth_size.max_w>0.0 && truth_size.mass_fraction>0.0 &&
                    truth_size.layer_fraction>0.0,
                    "nonlinear truth controls do not produce positive physical perturbation metrics");
        truth_amplitude=std::min({10.0/truth_size.max_w,
                                  0.20/truth_size.mass_fraction,
                                  0.50/truth_size.layer_fraction});
        TORCH_CHECK(std::isfinite(truth_amplitude) && truth_amplitude>0.0,
                    "nonlinear truth physical-cap amplitude is invalid");
        truth*=truth_amplitude;
        const auto actual=geometry.perturbationSize(basis.state,basis.columns.mv(truth));
        TORCH_CHECK(actual.max_w<=10.0*(1.0+1e-12) &&
                    actual.mass_fraction<=0.20*(1.0+1e-12) &&
                    actual.layer_fraction<=0.50*(1.0+1e-12),
                    "nonlinear truth exceeds its W/mass/layer physical caps");
        validate_physical_state(geometry,basis.state+basis.columns.mv(truth),
                                "physical-cap nonlinear truth initial state");
        std::cout<<"NONLINEAR_TRUTH amplitude="<<truth_amplitude
                 <<" W_max="<<actual.max_w<<" W_cap=10"
                 <<" mass_fraction="<<actual.mass_fraction<<" mass_cap=0.20"
                 <<" layer_fraction="<<actual.layer_fraction<<" layer_cap=0.50"
                 <<" coefficients="<<truth<<"\n";
    }
    const auto truth_initial=basis.state+basis.columns.mv(truth);
    const auto truth_run=run(truth_initial,steps,true,false,torch::Tensor(),0.1,
                             {},0.0,false,window.newton_tol,window.step_dt,window.kv);
    std::vector<torch::Tensor> observations(steps);
    const auto generated_first=physical_w_values(truth_run.states[first_observation]);
    const auto generated_final=physical_w_values(truth_run.states[final_observation]);
    observations[first_observation]=generated_first;
    observations[final_observation]=generated_final;
    if(!window.fixed_observations.empty()) {
        TORCH_CHECK(window.fixed_observations.size()==2,
                    "fixed observation pair must contain half-window and end samples");
        const int64_t sample_count=static_cast<int64_t>(physical_w_indices().size());
        for(size_t i=0;i<window.fixed_observations.size();++i) {
            const auto& sample=window.fixed_observations[i];
            TORCH_CHECK(sample.defined() && sample.device().is_cpu() &&
                        sample.scalar_type()==torch::kFloat64 && sample.dim()==1 &&
                        sample.numel()==sample_count &&
                        wrf::sdirk3::guarded_item<bool>(torch::isfinite(sample).all()),
                        "fixed observation ",i,
                        " must be a finite CPU FP64 physical-W sample of length ",sample_count);
        }
        observations[first_observation]=window.fixed_observations[0].clone();
        observations[final_observation]=window.fixed_observations[1].clone();
    }
    const auto background=run(basis.state,steps,true,false,torch::Tensor(),0.1,
                              {},0.0,false,window.newton_tol,window.step_dt,window.kv);
    if(nonlinear) {
        for(int step=0;step<steps;++step) {
            const auto delta=truth_run.states[step]-background.states[step];
            const auto size=geometry.perturbationSize(background.states[step],delta);
            std::cout<<"NONLINEAR_TRAJECTORY_DELTA step="<<(step+1)
                     <<" max_w="<<size.max_w<<" mass_fraction="<<size.mass_fraction
                     <<" layer_fraction="<<size.layer_fraction<<"\n";
        }
    }
    const auto background2=physical_w_values(background.states[first_observation]);
    const auto background4=physical_w_values(background.states[final_observation]);
    const auto zero=torch::zeros({6},torch::TensorOptions().dtype(torch::kFloat64));
    auto coefficients=zero.clone();
    auto evaluate=[&](const torch::Tensor& c,bool with_vjp,float newton_tol=-1.0f) {
        if(newton_tol<0.0f) newton_tol=window.newton_tol;
        const bool probe=with_vjp && steps==4 && window.forecast_steps==0 &&
            first_observation==1 && final_observation==3 && torch::equal(c,zero);
        auto sim=run(basis.state+basis.columns.mv(c),steps,true,with_vjp,
                     torch::Tensor(),0.1,observations,physical_std,
                          probe,newton_tol,window.step_dt,window.kv);
        const double prior=0.5*c.square().sum().item<double>();
        torch::Tensor gradient;
        if(with_vjp) gradient=c+basis.columns.t().mv(sim.gradient);
        return TwoTimeEval{prior+sim.value,sim.value,gradient,std::move(sim)};
    };
    auto current=evaluate(zero,true);
    const double start_value=current.value,start_data=current.data;
    const double start_gradient=current.gradient.norm().item<double>();
    const auto baseline_grad=current.gradient.clone();

    // The two-width independent FD outputs also define the stacked and final-only
    // observation Jacobians and the six-coordinate scalar gradient check.
    constexpr double fd_width=0.02;
    std::vector<torch::Tensor> A24_columns,A4_columns,A24_coarse_columns;
    std::vector<double> fd_gradient(6),fd_budget(6);
    for(int k=0;k<6;++k) {
        auto e=torch::zeros_like(zero); e[k]=1.0;
        const auto ph=run(basis.state+basis.columns.mv(fd_width*e),steps,true,false,
                          torch::Tensor(),0.1,observations,physical_std,false,window.newton_tol,
                          window.step_dt,window.kv);
        const auto mh=run(basis.state-basis.columns.mv(fd_width*e),steps,true,false,
                          torch::Tensor(),0.1,observations,physical_std,false,window.newton_tol,
                          window.step_dt,window.kv);
        const auto p2=physical_w_values(ph.states[first_observation]);
        const auto p4=physical_w_values(ph.states[final_observation]);
        const auto m2=physical_w_values(mh.states[first_observation]);
        const auto m4=physical_w_values(mh.states[final_observation]);
        const auto php=run(basis.state+basis.columns.mv((fd_width/2.0)*e),steps,true,false,
                           torch::Tensor(),0.1,observations,physical_std,false,window.newton_tol,
                           window.step_dt,window.kv);
        const auto mhp=run(basis.state-basis.columns.mv((fd_width/2.0)*e),steps,true,false,
                           torch::Tensor(),0.1,observations,physical_std,false,window.newton_tol,
                           window.step_dt,window.kv);
        const auto p2h=physical_w_values(php.states[first_observation]);
        const auto p4h=physical_w_values(php.states[final_observation]);
        const auto m2h=physical_w_values(mhp.states[first_observation]);
        const auto m4h=physical_w_values(mhp.states[final_observation]);
        const auto j24=(torch::cat({p2-m2,p4-m4})/(2.0*fd_width))/physical_std;
        const auto j24h=(torch::cat({p2h-m2h,p4h-m4h})/fd_width)/physical_std;
        const auto j4=(p4h-m4h)/(fd_width*physical_std);
        const double jscale=std::max(j24.norm().item<double>(),j24h.norm().item<double>());
        TORCH_CHECK((j24-j24h).norm().item<double>()<=1e-2*std::max(jscale,1e-14),
                    "two-time FD Jacobian column unstable at two widths: ",k);
        A24_columns.push_back(j24h); A24_coarse_columns.push_back(j24);
        A4_columns.push_back(j4);
        auto cost=[&](const torch::Tensor& w2,const torch::Tensor& w4) {
            return physical_observation_cost(w2,observations[first_observation],physical_std)+
                   physical_observation_cost(w4,observations[final_observation],physical_std);
        };
        const double fph=cost(p2,p4)+0.5*fd_width*fd_width;
        const double fmh=cost(m2,m4)+0.5*fd_width*fd_width;
        const double fphalf=cost(p2h,p4h)+0.5*(fd_width/2.0)*(fd_width/2.0);
        const double fmhalf=cost(m2h,m4h)+0.5*(fd_width/2.0)*(fd_width/2.0);
        fd_gradient[k]=(fphalf-fmhalf)/fd_width;
        const double ad=current.gradient[k].item<double>();
        const double floor=512.0*std::numeric_limits<double>::epsilon()*
            (std::abs(fphalf)+std::abs(fmhalf)+2.0*start_value)/fd_width;
        fd_budget[k]=floor+5e-4*std::max(std::abs(fd_gradient[k]),std::abs(ad));
        TORCH_CHECK(std::max(std::abs(fd_gradient[k]),std::abs(ad))>50.0*floor,
                    "two-time FD gradient is unresolved for control ",k);
        TORCH_CHECK(std::abs(fd_gradient[k]-ad)<=fd_budget[k],
                    "two-time six-control FD/adjoint mismatch for control ",k,
                    " fd=",fd_gradient[k]," ad=",ad," tol=",fd_budget[k]);
    }
    const auto A24=torch::stack(A24_columns,1),A4=torch::stack(A4_columns,1);
    const double fd_uncertainty=(A24-torch::stack(A24_coarse_columns,1)).norm().item<double>();
    const auto singular=torch::linalg_svdvals(A24);
    const double smax=singular.max().item<double>(),smin=singular.min().item<double>();
    const double ident_floor=256.0*std::numeric_limits<double>::epsilon()*
        std::max(1.0,A24.norm().item<double>());
    const double ident_bound=50.0*fd_uncertainty+ident_floor;
    TORCH_CHECK(smin>ident_bound && smax/smin<1e6,
                "two-time observation Jacobian is not identifiable: smin=",smin,
                " bound=",ident_bound," cond=",smax/smin);
    const auto info_gain=A24.t().matmul(A24)-A4.t().matmul(A4);
    const double info_scale=std::max(1.0,A24.t().matmul(A24).norm().item<double>());
    TORCH_CHECK(torch::linalg_eigvalsh(0.5*(info_gain+info_gain.t())).min().item<double>()>=
                    -1024.0*std::numeric_limits<double>::epsilon()*info_scale,
                "adding step-2 observations reduced information");
    const auto cov24=torch::linalg_inv(torch::eye(6,A24.options())+A24.t().matmul(A24));
    const auto cov4=torch::linalg_inv(torch::eye(6,A4.options())+A4.t().matmul(A4));
    for(int k : {4,5})
        TORCH_CHECK(cov24[k][k].item<double>()<cov4[k][k].item<double>()-
                    512.0*std::numeric_limits<double>::epsilon()*std::max(1.0,cov4[k][k].item<double>()),
                    "two-time information did not reduce weak MU marginal variance ",k);
    const auto d=torch::cat({observations[first_observation]-background2,
                             observations[final_observation]-background4})/physical_std;
    const auto metric=torch::eye(6,A24.options())+A24.t().matmul(A24);
    auto inverse_metric=torch::linalg_inv(metric);
    const double metric_min_eigenvalue=torch::linalg_eigvalsh(metric).min().item<double>();
    TORCH_CHECK(std::isfinite(metric_min_eigenvalue) && metric_min_eigenvalue>0.0,
                "background preconditioner metric is not positive definite");
    const auto linear_map=torch::linalg_solve(metric,A24.t().mv(d));
    const double linearization_error_estimate=50.0*fd_uncertainty*
        (d.norm().item<double>()+2.0*A24.norm().item<double>()*linear_map.norm().item<double>()+
         fd_uncertainty*linear_map.norm().item<double>())+
        1024.0*std::numeric_limits<double>::epsilon()*std::max(1.0,linear_map.norm().item<double>());

    auto fd=torch::tensor(fd_gradient,zero.options());
    if(steps==4 && window.forecast_steps==0 && first_observation==1 && final_observation==3) {
        // Preserve the original short-window API-placement falsifiers unchanged.
        TORCH_CHECK(torch::equal(current.simulation.terminal_vector,current.simulation.legacy_terminal),
                    "terminal-only output cotangent differs from the legacy terminal pullback");
        TORCH_CHECK(torch::count_nonzero(current.simulation.zero_cotangents).item<int64_t>()==0,
                    "all-zero output cotangents produced a nonzero initial adjoint");
        const auto separate_sum=current.simulation.time2_only+current.simulation.time4_only;
        const auto additivity_error=(current.simulation.gradient-separate_sum).abs();
        const auto operand_budget=1e-12+1e-12*(current.simulation.time2_only.abs()+
                                              current.simulation.time4_only.abs());
        const int64_t worst=(additivity_error/operand_budget).argmax().item<int64_t>();
        std::cout<<"TIME_PULLBACK_ADDITIVITY max_error="<<additivity_error.max().item<double>()
                 <<" error_norm="<<additivity_error.norm().item<double>()
                 <<" combined_norm="<<current.simulation.gradient.norm().item<double>()
                 <<" separate_norm_sum="<<(current.simulation.time2_only.norm().item<double>()+
                                             current.simulation.time4_only.norm().item<double>())
                 <<" max_component_budget_ratio="<<(additivity_error/
                     (1e-12+1e-12*separate_sum.abs())).max().item<double>()
                 <<" max_operand_budget_ratio="<<(additivity_error/operand_budget).max().item<double>()
                 <<" worst_flat_index="<<worst
                 <<" worst_g2="<<current.simulation.time2_only[worst].item<double>()
                 <<" worst_g4="<<current.simulation.time4_only[worst].item<double>()
                 <<" worst_combined="<<current.simulation.gradient[worst].item<double>()<<std::endl;
        // The transpose solver normalizes whole cotangents. Superposition is
        // measured at the operand-vector scale, including small components;
        // a per-entry relative test would magnify their FP64 rounding noise.
        const double additivity_budget=1e-12*std::max(1.0,
            current.simulation.time2_only.norm().item<double>()+
            current.simulation.time4_only.norm().item<double>());
        TORCH_CHECK(additivity_error.norm().item<double>()<=additivity_budget,
            "separate output-time adjoints do not sum to the combined adjoint");
        std::vector<torch::Tensor> half_window_observations(first_observation+1);
        half_window_observations.back()=observations[first_observation];
        const auto independent_half=run(basis.state,first_observation+1,true,true,
            torch::Tensor(),0.1,half_window_observations,physical_std,false,window.newton_tol,
            window.step_dt,window.kv);
        TORCH_CHECK(torch::allclose(independent_half.gradient,current.simulation.time2_only,
                                    1e-12,1e-12),
                    "step-2 cotangent differs between independent shorter and full local graphs");
        const auto omitted=zero+basis.columns.t().mv(current.simulation.time4_only);
        const auto shifted=zero+basis.columns.t().mv(current.simulation.shifted_time2);
        double budget_norm=0.0;
        for(int k=0;k<6;++k) budget_norm+=fd_budget[k]*fd_budget[k];
        const double falsification_budget=std::sqrt(budget_norm);
        TORCH_CHECK((omitted-fd).norm().item<double>()>falsification_budget,
                    "omitting step-2 cotangent was not detected by independent FD");
        TORCH_CHECK((shifted-fd).norm().item<double>()>falsification_budget,
                "shifting step-2 cotangent to the wrong output was not detected by independent FD");
        std::cout<<"TWO_TIME_MUTATION omitted_error="<<(omitted-fd).norm().item<double>()
                 <<" shifted_error="<<(shifted-fd).norm().item<double>()
                 <<" budget="<<falsification_budget<<"\n";
    }

    if(nonlinear) {
        const auto check_coefficients=0.5*truth;
        const auto check_eval=evaluate(check_coefficients,true);
        const auto check_direction=truth/truth.norm();
        constexpr double check_width=0.01;
        auto directional_value=[&](double alpha) {
            return evaluate(check_coefficients+alpha*check_direction,false).value;
        };
        const double fplus=directional_value(check_width);
        const double fminus=directional_value(-check_width);
        const double fplus_half=directional_value(check_width/2.0);
        const double fminus_half=directional_value(-check_width/2.0);
        const double fd_direction=(fplus_half-fminus_half)/check_width;
        const double adjoint_direction=check_eval.gradient.dot(check_direction).item<double>();
        const double directional_floor=512.0*std::numeric_limits<double>::epsilon()*
            (std::abs(fplus_half)+std::abs(fminus_half)+2.0*std::abs(check_eval.value))/check_width;
        const double directional_budget=directional_floor+5e-4*
            std::max(std::abs(fd_direction),std::abs(adjoint_direction));
        const double rem_h=std::abs(fplus-check_eval.value-check_width*adjoint_direction);
        const double rem_half=std::abs(fplus_half-check_eval.value-
                                       (check_width/2.0)*adjoint_direction);
        const double taylor_floor=512.0*std::numeric_limits<double>::epsilon()*
            (std::abs(fplus)+std::abs(fplus_half)+2.0*std::abs(check_eval.value));
        TORCH_CHECK(std::max(std::abs(fd_direction),std::abs(adjoint_direction))>
                        50.0*directional_floor &&
                    std::abs(fd_direction-adjoint_direction)<=directional_budget,
                    "strong finite-point directional FD/adjoint mismatch: fd=",fd_direction,
                    " ad=",adjoint_direction," tol=",directional_budget);
        TORCH_CHECK(rem_half>50.0*taylor_floor && rem_h/rem_half>3.5 && rem_h/rem_half<4.5,
                    "strong finite-point Taylor remainder is not quadratic: ratio=",rem_h/rem_half,
                    " floor=",taylor_floor);
        std::cout<<"NONLINEAR_STRONG_POINT fraction=0.5 direction_fd="<<fd_direction
                 <<" direction_adjoint="<<adjoint_direction<<" budget="<<directional_budget
                 <<" taylor_ratio="<<rem_h/rem_half<<"\n";
    }

    constexpr double armijo=1e-4;
    int accepted=0,rejected=0,physical_rejections=0,nonconverged_rejections=0,armijo_rejections=0;
    int bfgs_updates=0,bfgs_skips=0;
    double minimum_accepted_alpha=1.0;
    std::vector<double> accepted_alphas;
    for(int iteration=0;iteration<(nonlinear?30:15);++iteration) {
        const double gnorm=current.gradient.norm().item<double>();
        if(gnorm<=1e-7*std::max(1.0,start_gradient)) break;
        const auto direction=nonlinear ? -inverse_metric.mv(current.gradient)
                                       : -torch::linalg_solve(metric,current.gradient);
        const double slope=current.gradient.dot(direction).item<double>();
        TORCH_CHECK(std::isfinite(slope)&&slope<0.0,"two-time metric direction is not descent");
        bool found=false; double alpha=1.0;
        for(int line=0;line<12;++line,alpha*=0.5) {
            const auto trial_c=coefficients+alpha*direction;
            TwoTimeEval trial;
            TrialRejectionTally trial_rejection;
            if(try_admissible_trial([&]{trial=evaluate(trial_c,true);},&trial_rejection)) {
                if(trial.value<=current.value+armijo*alpha*slope) {
                    std::cout<<"TWO_TIME_STEP iter="<<accepted<<" alpha="<<alpha
                             <<" J="<<current.value<<" -> "<<trial.value
                             <<" data="<<trial.data<<" |g|="<<gnorm<<"\n";
                    const auto old_coefficients=coefficients;
                    const auto old_gradient=current.gradient;
                    coefficients=trial_c; current=std::move(trial); ++accepted;
                    accepted_alphas.push_back(alpha); found=true;
                    minimum_accepted_alpha=std::min(minimum_accepted_alpha,alpha);
                    if(nonlinear) {
                        const auto s=coefficients-old_coefficients;
                        const auto y=current.gradient-old_gradient;
                        const double curvature=s.dot(y).item<double>();
                        const double curvature_floor=128.0*std::numeric_limits<double>::epsilon()*
                            s.norm().item<double>()*y.norm().item<double>();
                        if(std::isfinite(curvature) && curvature>curvature_floor) {
                            const double rho=1.0/curvature;
                            const auto eye=torch::eye(6,inverse_metric.options());
                            const auto left=eye-rho*torch::outer(s,y);
                            const auto right=eye-rho*torch::outer(y,s);
                            auto updated=left.matmul(inverse_metric).matmul(right)+rho*torch::outer(s,s);
                            updated=0.5*(updated+updated.t());
                            const auto eig=torch::linalg_eigvalsh(updated);
                            TORCH_CHECK(wrf::sdirk3::guarded_item<bool>(torch::isfinite(updated).all()) &&
                                        std::isfinite(eig.min().item<double>()) &&
                                        eig.min().item<double>()>0.0,
                                        "nonlinear inverse-BFGS update lost finite SPD invariants");
                            inverse_metric=updated;
                            ++bfgs_updates;
                            std::cout<<"NONLINEAR_BFGS iter="<<accepted-1<<" action=update"
                                     <<" curvature="<<curvature<<" curvature_floor="<<curvature_floor
                                     <<" min_eigenvalue="<<eig.min().item<double>()<<"\n";
                        } else {
                            ++bfgs_skips;
                            std::cout<<"NONLINEAR_BFGS iter="<<accepted-1<<" action=skip"
                                     <<" reason=unresolved_curvature curvature="<<curvature
                                     <<" curvature_floor="<<curvature_floor<<"\n";
                        }
                    }
                    break;
                }
                ++rejected; ++armijo_rejections;
                std::cout<<"TWO_TIME_BACKTRACK alpha="<<alpha<<" reason=armijo J="
                         <<current.value<<" trial_J="<<trial.value<<"\n";
            } else {
                ++rejected;
                physical_rejections+=trial_rejection.physical;
                nonconverged_rejections+=trial_rejection.nonconverged;
                std::cout<<"TWO_TIME_BACKTRACK alpha="<<alpha<<" reason="<<trial_rejection.last
                         <<" J="<<current.value<<" trial_J=unavailable\n";
            }
        }
        TORCH_CHECK(found,"two-time Armijo search found no admissible step");
    }
    const double final_gradient=current.gradient.norm().item<double>();
    const double grad_tolerance=1e-6*std::max(1.0,start_gradient);
    const double linear_gap=(coefficients-linear_map).norm().item<double>();
    const double linear_tolerance=1e-2*std::max(1.0,linear_map.norm().item<double>());
    TORCH_CHECK(accepted>0 && current.value<start_value && current.data<start_data &&
                final_gradient<grad_tolerance,
                "two-time inverse failed objective/misfit/gradient convergence: J ",
                start_value," -> ",current.value," data ",start_data," -> ",current.data,
                " |g|=",final_gradient," tol=",grad_tolerance);
    if(nonlinear) {
        TORCH_CHECK(bfgs_updates>0,"strong nonlinear solve did not exercise an inverse-BFGS update");
        TORCH_CHECK(armijo_rejections>0 && minimum_accepted_alpha<1.0,
                    "strong nonlinear solve did not demonstrate an Armijo alpha backtrack");
    } else {
        TORCH_CHECK(linear_gap<=linear_tolerance,
                    "two-time nonlinear MAP differs too far from linear reference: gap=",linear_gap,
                    " tol=",linear_tolerance);
    }
    double tight_gradient=final_gradient,tight_gradient_difference=0.0,tight_cost_difference=0.0;
    double tight_max_block_state_relative_difference=0.0,estimated_solve_control_delta=0.0;
    if(nonlinear) {
        const auto tight=evaluate(coefficients,true,1e-11f);
        tight_gradient=tight.gradient.norm().item<double>();
        tight_gradient_difference=(tight.gradient-current.gradient).norm().item<double>();
        tight_cost_difference=std::abs(tight.value-current.value);
        tight_max_block_state_relative_difference=max_block_state_relative_difference(
            current.simulation.states.back(),tight.simulation.states.back());
        estimated_solve_control_delta=inverse_metric.mv(tight.gradient-current.gradient).norm().item<double>();
        const double state_budget=1e-8;
        const double cost_budget=1e-8*std::max(1.0,std::abs(current.value));
        TORCH_CHECK(tight_gradient<grad_tolerance &&
                    tight_gradient_difference<=0.01*grad_tolerance &&
                    tight_cost_difference<=cost_budget &&
                    tight_max_block_state_relative_difference<=state_budget,
                    "tighter-Newton terminal check changed converged MAP beyond budget: |g|=",
                    tight_gradient," dg=",tight_gradient_difference," dJ=",tight_cost_difference,
                    " max_block_dstate_rel=",tight_max_block_state_relative_difference);
        const double coefficient_floor=1024.0*std::numeric_limits<double>::epsilon()*
            std::max(1.0,coefficients.norm().item<double>());
        const double separation_error_estimate=std::max({linearization_error_estimate,
            estimated_solve_control_delta,coefficient_floor});
        TORCH_CHECK(linear_gap>100.0*separation_error_estimate,
                    "strong nonlinear MAP is not separated from linearization/solve noise: gap=",
                    linear_gap," 100x_error_estimate=",100.0*separation_error_estimate,
                    " fd_linearization_error_estimate=",linearization_error_estimate,
                    " solve_control_estimate=",estimated_solve_control_delta,
                    " coefficient_floor=",coefficient_floor);
        std::cout<<"NONLINEAR_TIGHT_NEWTON tol=1e-11 gradient="<<tight_gradient
                 <<" gradient_difference="<<tight_gradient_difference
                 <<" cost_difference="<<tight_cost_difference
                 <<" cost_budget="<<cost_budget
                 <<" max_block_state_relative_difference="<<tight_max_block_state_relative_difference
                 <<" state_budget="<<state_budget
                 <<" estimated_solve_control_delta="<<estimated_solve_control_delta<<"\n";
        std::cout<<"NONLINEAR_SEPARATION gap="<<linear_gap
                 <<" fd_linearization_error_estimate="<<linearization_error_estimate
                 <<" solve_control_estimate="<<estimated_solve_control_delta
                 <<" coefficient_floor="<<coefficient_floor
                 <<" threshold="<<100.0*separation_error_estimate<<"\n";
    }
    const auto replay=evaluate(coefficients,true);
    const auto observation_gradient=current.gradient-coefficients;
    std::cout<<"WINDOW_GRADIENT_PARTS prior_norm="<<coefficients.norm().item<double>()
             <<" observation_norm="<<observation_gradient.norm().item<double>()
             <<" total_norm="<<current.gradient.norm().item<double>()<<"\n";
    TORCH_CHECK(replay.value==current.value && torch::equal(replay.gradient,current.gradient),
                "two-time objective/gradient replay is not deterministic");
    TORCH_CHECK(replay.simulation.states.size()==current.simulation.states.size(),
                "two-time replay checkpoint count changed");
    for(size_t n=0;n<replay.simulation.states.size();++n)
        TORCH_CHECK(torch::equal(replay.simulation.states[n],current.simulation.states[n]),
                    "two-time replay differs at checkpoint ",n);
    torch::Tensor forecast_end;
    if(window.forecast_steps>0) {
        const int total_steps=steps+window.forecast_steps;
        const auto analysis_initial=basis.state+basis.columns.mv(coefficients);
        const auto truth_full=run(truth_initial,total_steps,true,false,torch::Tensor(),0.1,
                                  {},0.0,false,window.newton_tol,window.step_dt,window.kv);
        const auto background_full=run(basis.state,total_steps,true,false,torch::Tensor(),0.1,
                                       {},0.0,false,window.newton_tol,window.step_dt,window.kv);
        const auto analysis_full=run(analysis_initial,total_steps,true,false,torch::Tensor(),0.1,
                                     {},0.0,false,window.newton_tol,window.step_dt,window.kv);
        forecast_end=analysis_full.states.back().clone();
        const auto report_budget=[&](const char* label,const torch::Tensor& input,const Result& trajectory) {
            const auto measured=geometry.dryTransportBudget(input,trajectory.states);
            std::cout<<"WINDOW_DRY_BUDGET trajectory="<<label
                     <<" mass_kg="<<measured.initial_mass
                     <<" theta_integral_kg_K="<<measured.initial_theta_integral
                     <<" max_mass_drift_kg="<<measured.max_mass_drift
                     <<" mass_allowance_kg="<<measured.mass_allowance
                     <<" max_theta_drift_kg_K="<<measured.max_theta_drift
                     <<" theta_allowance_kg_K="<<measured.theta_allowance
                     <<" constant_theta="<<(measured.constant_theta?1:0)<<"\n";
        };
        report_budget("truth",truth_initial,truth_full);
        report_budget("background",basis.state,background_full);
        report_budget("analysis",analysis_initial,analysis_full);
        for(int n=0;n<steps;++n) {
            TORCH_CHECK(torch::equal(truth_full.states[n],truth_run.states[n]) &&
                        torch::equal(background_full.states[n],background.states[n]) &&
                        torch::equal(analysis_full.states[n],current.simulation.states[n]),
                        "forecast trajectory changed an analysis-window prefix at step ",n+1);
        }
        const auto analysis_forecast_replay=run(analysis_initial,total_steps,true,false,
            torch::Tensor(),0.1,{},0.0,false,window.newton_tol,window.step_dt,window.kv);
        for(int n=0;n<total_steps;++n)
            TORCH_CHECK(torch::equal(analysis_full.states[n],analysis_forecast_replay.states[n]),
                        "held-out analysis forecast is not reproducible at step ",n+1);

        const int report_steps[]={first_observation,final_observation,total_steps-1};
        const char* report_names[]={"half","analysis_end","withheld"};
        for(int report=0;report<3;++report) {
            const int index=report_steps[report];
            const auto bg_error=physical_field_rmse(geometry,background_full.states[index],truth_full.states[index]);
            const auto map_error=physical_field_rmse(geometry,analysis_full.states[index],truth_full.states[index]);
            std::cout<<"WINDOW_FIELD_RMSE time="<<report_names[report]
                     <<" seconds="<<(index+1)*window.step_dt
                     <<" background_W_m_s="<<bg_error.w<<" analysis_W_m_s="<<map_error.w
                     <<" background_PH_m2_s2="<<bg_error.phi<<" analysis_PH_m2_s2="<<map_error.phi
                     <<" background_MU_Pa="<<bg_error.mu<<" analysis_MU_Pa="<<map_error.mu
                     <<" background_theta_K="<<bg_error.theta<<" analysis_theta_K="<<map_error.theta
                     <<"\n";
        }
        const auto w_truth=physical_w_values(truth_full.states.back());
        const double background_w_error=(physical_w_values(background_full.states.back())-w_truth)
            .norm().item<double>();
        const double analysis_w_error=(physical_w_values(analysis_full.states.back())-w_truth)
            .norm().item<double>();
        const double heldout_floor=512.0*std::numeric_limits<double>::epsilon()*
            std::max(1.0,w_truth.norm().item<double>());
        const bool heldout_w_resolved=background_w_error>50.0*heldout_floor;
        TORCH_CHECK(heldout_w_resolved,"withheld W forecast signal is insufficient for this skill gate");
        if(heldout_w_resolved)
            TORCH_CHECK(analysis_w_error<background_w_error,
                        "MAP did not improve resolved withheld W forecast error: analysis=",
                        analysis_w_error," background=",background_w_error,
                        " roundoff_floor=",heldout_floor);
        std::cout<<"WINDOW_HELDOUT_W resolved="<<(heldout_w_resolved?1:0)
                 <<" background_error_norm="<<background_w_error
                 <<" analysis_error_norm="<<analysis_w_error
                 <<" floor="<<heldout_floor
                 <<" forecast_steps="<<window.forecast_steps
                 <<" prefix_and_replay=exact\n";
    }
    const double observation_count=2.0*
        static_cast<double>(observations[first_observation].numel());
    const double initial_rmse=physical_std*std::sqrt(2.0*start_data/observation_count);
    const double final_rmse=physical_std*std::sqrt(2.0*current.data/observation_count);
    const auto singular4=torch::linalg_svdvals(A4);
    std::cout<<"TWO_TIME_IDENTIFIABILITY rows="<<d.numel()<<" rank=6 smax="<<smax
             <<" smin="<<smin<<" cond="<<smax/smin<<" fd_uncertainty="<<fd_uncertainty
             <<" sigma_physical="<<physical_std<<"\n";
    std::cout<<"TWO_TIME_INFORMATION mu_variance4="<<cov4[4][4].item<double>()
             <<" -> "<<cov24[4][4].item<double>()<<" mu_variance5="<<cov4[5][5].item<double>()
             <<" -> "<<cov24[5][5].item<double>()<<" gain_min_eig="
             <<torch::linalg_eigvalsh(0.5*(info_gain+info_gain.t())).min().item<double>()<<"\n";
    std::cout<<"TWO_TIME_FINAL_ONLY smax="<<singular4.max().item<double>()
             <<" smin="<<singular4.min().item<double>()
             <<" cond="<<(singular4.max()/singular4.min()).item<double>()<<"\n";
    std::cout<<(nonlinear?"NONLINEAR_RESULT":"TWO_TIME_RESULT")
             <<" accepted="<<accepted<<" rejected="<<rejected
             <<" truth="<<truth<<" nonlinear_map="<<coefficients<<" linear_map="<<linear_map
             <<" |g|="<<start_gradient<<" -> "<<final_gradient
             <<" physical_rmse="<<initial_rmse<<" -> "<<final_rmse
             <<" J="<<start_value<<" -> "<<current.value
             <<" linear_gap="<<linear_gap<<"/"
             <<(nonlinear?100.0*std::max({linearization_error_estimate,estimated_solve_control_delta,
                    1024.0*std::numeric_limits<double>::epsilon()*
                    std::max(1.0,coefficients.norm().item<double>())}):linear_tolerance)<<"\n";
    std::cout<<(nonlinear?"NONLINEAR_SUMMARY":"TWO_TIME_SUMMARY")
             <<" J_initial="<<start_value<<" J_final="<<current.value
             <<" data_initial="<<start_data<<" data_final="<<current.data
             <<" gradient_initial="<<start_gradient<<" gradient_final="<<final_gradient
             <<" tighter_newton_evaluated="<<(nonlinear?1:0)
             <<" gradient_tight="<<tight_gradient
             <<" gradient_tight_difference="<<tight_gradient_difference
             <<" cost_tight_difference="<<tight_cost_difference
             <<" state_tight_max_block_relative_difference="<<tight_max_block_state_relative_difference
             <<" physical_rmse_initial="<<initial_rmse<<" physical_rmse_final="<<final_rmse
             <<" linear_gap="<<linear_gap<<" linearization_error_estimate="<<linearization_error_estimate
             <<" accepted="<<accepted<<" rejected="<<rejected
             <<" physical_rejections="<<physical_rejections
             <<" nonconverged_rejections="<<nonconverged_rejections
             <<" armijo_rejections="<<armijo_rejections<<" truth_amplitude="<<truth_amplitude
             <<" bfgs_updates="<<bfgs_updates<<" bfgs_skips="<<bfgs_skips
             <<" min_accepted_alpha="<<minimum_accepted_alpha
             <<" accepted_alphas=";
    for(double alpha:accepted_alphas) std::cout<<alpha<<",";
    std::cout<<"\n";
    for(int k=0;k<6;++k)
        std::cout<<"TWO_TIME_CONTROL index="<<k<<" fd="<<fd_gradient[k]
                 <<" adjoint_background="<<baseline_grad[k].item<double>()
                 <<" gradient_final="<<current.gradient[k].item<double>()
                 <<" budget="<<fd_budget[k]<<" estimate="<<coefficients[k].item<double>()
                 <<" reference="<<linear_map[k].item<double>()<<"\n";
    std::cout<<"TWO_TIME_SUMMARY J_initial="<<start_value<<" J_final="<<current.value
             <<" data_initial="<<start_data<<" data_final="<<current.data
             <<" gradient_initial="<<start_gradient<<" gradient_final="<<final_gradient
             <<" physical_rmse_initial="<<initial_rmse<<" physical_rmse_final="<<final_rmse
             <<" linear_gap="<<linear_gap<<" accepted="<<accepted<<" rejected="<<rejected<<"\n";
    std::cout<<"TWO_TIME_VARIANCE terminal_mu4="<<cov4[4][4].item<double>()
             <<" both_mu4="<<cov24[4][4].item<double>()
             <<" terminal_mu5="<<cov4[5][5].item<double>()
             <<" both_mu5="<<cov24[5][5].item<double>()<<"\n";
    for(int k=0;k<6;++k)
        std::cout<<"TWO_TIME_SINGULAR index="<<k<<" terminal="<<singular4[k].item<double>()
                 <<" both="<<singular[k].item<double>()<<"\n";
    std::cout<<(nonlinear?"FP64 carry nonlinear two-time inverse experiment passed\n"
                         :"FP64 carry two-time inverse experiment passed\n");
    return {coefficients.clone(),current.gradient.clone(),truth.clone(),basis.state.clone(),
            {observations[first_observation].clone(),observations[final_observation].clone()},
            current.value,current.data,truth_run.states.back().clone(),
            background.states.back().clone(),current.simulation.states.back().clone(),
            baseline_grad.clone(),forecast_end};
}

void run_refinement_experiment() {
    configure(true);
    TileCase geometry(5000.0f);prepare_native(geometry);
    const auto bg=geometry.nonzeroHydrostaticBackground();
    const auto basis=inverse_basis(geometry,bg);
    WindowSpec fine_window;
    fine_window.analysis_steps=32;fine_window.step_dt=0.125f;fine_window.forecast_steps=16;
    const auto fine=run_two_time_experiment(bg,false,fine_window);
    WindowSpec coarse_window;
    coarse_window.analysis_steps=16;coarse_window.step_dt=0.25f;coarse_window.forecast_steps=8;
    coarse_window.fixed_observations=fine.observations;
    const auto coarse=run_two_time_experiment(bg,false,coarse_window);
    TORCH_CHECK(fine.observations.size()==2 && coarse.observations.size()==2 &&
                torch::equal(fine.observations[0],coarse.observations[0]) &&
                torch::equal(fine.observations[1],coarse.observations[1]),
                "time-refinement comparison changed the observation data");
    const double time_gradient_change=(fine.initial_gradient-coarse.initial_gradient).norm().item<double>();
    double max_solve_gradient_change=0.0;
    const WindowSpec specs[]={coarse_window,fine_window};
    const TwoTimeOutcome outcomes[]={coarse,fine};
    const double sigma=std::sqrt(105.0)*0.001;
    for(int arm=0;arm<2;++arm) {
        const auto& window=specs[arm];const auto& result=outcomes[arm];
        std::vector<torch::Tensor> obs(window.analysis_steps);
        obs[window.analysis_steps/2-1]=fine.observations[0];obs.back()=fine.observations[1];
        const auto tight_bg=run(bg,window.analysis_steps,true,true,{},0.1,obs,sigma,false,
                                 1e-11f,window.step_dt,window.kv);
        const auto tight_initial_gradient=basis.columns.t().mv(tight_bg.gradient);
        const double solve_change=(tight_initial_gradient-result.initial_gradient).norm().item<double>();
        max_solve_gradient_change=std::max(max_solve_gradient_change,solve_change);
        const auto z=basis.state+basis.columns.mv(result.controls);
        const auto tight=run(z,window.analysis_steps,true,true,{},0.1,obs,sigma,false,
                             1e-11f,window.step_dt,window.kv);
        const auto tight_gradient=result.controls+basis.columns.t().mv(tight.gradient);
        const double cost=0.5*result.controls.square().sum().item<double>()+tight.value;
        const double stationarity_gate=1e-6*std::max(1.0,result.initial_gradient.norm().item<double>());
        TORCH_CHECK(tight_gradient.norm().item<double>()<stationarity_gate,
                    "time-refinement MAP stationarity changes under tighter solve");
        std::cout<<"REFINEMENT_SOLVE dt="<<window.step_dt
                 <<" baseline_gradient_delta="<<solve_change
                 <<" final_gradient_delta="<<(tight_gradient-result.gradient).norm().item<double>()
                 <<" cost_delta="<<std::abs(cost-result.value)
                 <<" state_delta="<<max_block_state_relative_difference(result.analysis_end,tight.states.back())
                 <<" tighter_tol=1e-11\n";
    }
    const double floor=4096.0*std::numeric_limits<double>::epsilon()*
        std::max({1.0,coarse.initial_gradient.norm().item<double>(),fine.initial_gradient.norm().item<double>()});
    const bool resolved=time_gradient_change>50.0*floor;
    if(resolved) TORCH_CHECK(max_solve_gradient_change<0.1*time_gradient_change,
                            "solver error obscures the fixed-data temporal gradient comparison");

    // Split the six-second endpoint difference into a fixed-control time-grid
    // effect and a separate response to reoptimizing controls on the fine grid.
    // This extra N=48 run starts from the same background and uses fine-window
    // settings; no additional inverse solve or derivative evaluation is made.
    const auto coarse_controls_on_fine=run(
        basis.state+basis.columns.mv(coarse.controls),48,true,false,{},0.1,{},0.0,false,
        fine_window.newton_tol,fine_window.step_dt,fine_window.kv);
    TORCH_CHECK(coarse_controls_on_fine.states.size()==48 &&
                fine.forecast_end.defined() && coarse.forecast_end.defined(),
                "six-second forecast decomposition lacks an accepted endpoint");
    const auto coarse_forecast_fine=coarse_controls_on_fine.states.back();
    const char* block_names[]={"U","V","W","PH","THETA","MU"};
    for(int block=0;block<6;++block) {
        const auto coarse_endpoint=physical_state_block_values(geometry,coarse.forecast_end,block);
        const auto coarse_control_fine_endpoint=
            physical_state_block_values(geometry,coarse_forecast_fine,block);
        const auto fine_endpoint=physical_state_block_values(geometry,fine.forecast_end,block);
        const auto same_control_time=coarse_endpoint-coarse_control_fine_endpoint;
        const auto reoptimization=coarse_control_fine_endpoint-fine_endpoint;
        const auto combined=coarse_endpoint-fine_endpoint;
        const auto closure=combined-(same_control_time+reoptimization);
        const double same_norm=same_control_time.norm().item<double>();
        const double reopt_norm=reoptimization.norm().item<double>();
        const double combined_norm=combined.norm().item<double>();
        const double dot=same_control_time.dot(reoptimization).item<double>();
        const double squared_closure=combined.square().sum().item<double>()-
            (same_control_time.square().sum().item<double>()+
             reoptimization.square().sum().item<double>()+2.0*dot);
        const double state_scale=std::max({1.0,coarse_endpoint.norm().item<double>(),
            coarse_control_fine_endpoint.norm().item<double>(),fine_endpoint.norm().item<double>()});
        const double closure_budget=64.0*std::numeric_limits<double>::epsilon()*state_scale;
        const double squared_budget=256.0*std::numeric_limits<double>::epsilon()*state_scale*state_scale;
        TORCH_CHECK(closure.norm().item<double>()<=closure_budget &&
                    std::abs(squared_closure)<=squared_budget,
                    "six-second endpoint decomposition failed algebraic closure for ",block_names[block]);
        std::cout<<"REFINEMENT_FORECAST_DECOMP block="<<block_names[block]
                 <<" coarseopt_samecontrol_time_norm="<<same_norm
                 <<" reoptimization_norm="<<reopt_norm
                 <<" coarseopt_minus_fineopt_norm="<<combined_norm
                 <<" dot_samecontrol_reoptimization="<<dot
                 <<" twice_dot="<<(2.0*dot)
                 <<" vector_closure_norm="<<closure.norm().item<double>()
                 <<" squared_norm_closure="<<squared_closure
                 <<" closure_budget="<<closure_budget
                 <<" fine_dt="<<fine_window.step_dt<<" steps=48 duration_seconds=6\n";
    }
    const auto withheld_difference=physical_field_rmse(geometry,coarse.forecast_end,fine.forecast_end);
    std::cout<<"REFINEMENT_FIXED_DATA obs_t1=2 obs_t2=4 forecast_t=6 sigma="<<sigma
             <<" coarse_steps=16 fine_steps=32 data_equal=1"
             <<" control_delta="<<(coarse.controls-fine.controls).norm().item<double>()
             <<" objective_delta="<<std::abs(coarse.value-fine.value)
             <<" baseline_gradient_delta="<<time_gradient_change
             <<" max_solve_gradient_delta="<<max_solve_gradient_change
             <<" time_signal_resolved="<<(resolved?1:0)
             <<" withheld_W_delta="<<withheld_difference.w
             <<" withheld_PH_delta="<<withheld_difference.phi
             <<" withheld_MU_delta="<<withheld_difference.mu
             <<" withheld_theta_delta="<<withheld_difference.theta<<"\n";
}


// Mass centers lie at (i+.5)dx and U faces at idx. The periodic mode
// excludes the duplicate final U face from its coefficient reduction.
constexpr int wave_size=4*nz+1;
using WaveAmplitudes=std::array<std::complex<double>,wave_size>;
torch::Tensor wave_field(const WaveAmplitudes& q) {
    auto result=torch::zeros({total},torch::kFloat64);
    auto* values=result.data_ptr<double>();
    const double angle=2.0*std::acos(-1.0)/nx;
    for(int j=0;j<ny;++j) {
        for(int k=0;k<nz;++k) for(int i=0;i<nu;++i)
            values[(j*nz+k)*nu+i]=std::real(q[k]*std::polar(1.0,angle*i));
        for(int k=1;k<nw;++k) for(int i=0;i<nx;++i) {
            const auto phase=std::polar(1.0,angle*(i+0.5));
            values[su+sv+(j*nw+k)*nx+i]=std::real(q[nz+k-1]*phase);
            values[su+sv+sw+(j*nw+k)*nx+i]=std::real(q[2*nz+k-1]*phase);
        }
        for(int k=0;k<nz;++k) for(int i=0;i<nx;++i)
            values[su+sv+2*sw+(j*nz+k)*nx+i]=
                std::real(q[3*nz+k]*std::polar(1.0,angle*(i+0.5)));
        for(int i=0;i<nx;++i) values[total-sm+j*nx+i]=
            std::real(q[4*nz]*std::polar(1.0,angle*(i+0.5)));
    }
    return result;
}
WaveAmplitudes wave_coefficients(const torch::Tensor& input) {
    const auto data=input.to(torch::kCPU,torch::kFloat64).contiguous();
    TORCH_CHECK(data.numel()==total,"wave Fourier extraction needs packed state");
    const auto* values=data.data_ptr<double>();
    WaveAmplitudes result{};
    const double angle=2.0*std::acos(-1.0)/nx,weight=2.0/(nx*ny);
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        const auto mass_phase=std::polar(weight,-angle*(i+0.5));
        const auto face_phase=std::polar(weight,-angle*i);
        for(int k=0;k<nz;++k) {
            result[k]+=values[(j*nz+k)*nu+i]*face_phase;
            result[3*nz+k]+=values[su+sv+2*sw+(j*nz+k)*nx+i]*mass_phase;
        }
        for(int k=1;k<nw;++k) {
            result[nz+k-1]+=values[su+sv+(j*nw+k)*nx+i]*mass_phase;
            result[2*nz+k-1]+=values[su+sv+sw+(j*nw+k)*nx+i]*mass_phase;
        }
        result[4*nz]+=values[total-sm+j*nx+i]*mass_phase;
    }
    return result;
}
void run_wave_operator_probe() {
    configure(false,0.0f);
    TileCase geometry(5000.0f);prepare_native(geometry);
    geometry.solver.setVerticalInterpolationCoefficients(geometry.half.data(),geometry.half.data(),2.0f,-1.5f,0.5f);
    const auto bg=geometry.nonzeroHydrostaticBackground(nullptr,2.0);
    geometry.set(bg);geometry.solver.requestFixedTrajectory(1,{dt},bg);
    int host=1;sdirk3_set_timestep_i4(&host);geometry.step(dt);
    geometry.solver.closeFixedTrajectory();
    const auto column=wrf::sdirk3::test::stable_wave_reference::Column::fromStableFixture(geometry,bg);
    const auto reference=column.matrixTensor();
    auto native=torch::empty({wave_size,wave_size},reference.options());
    auto* native_values=native.data_ptr<c10::complex<double>>();
    constexpr double width=0.001;
    // Fill the production tangent in memory and compare it directly with the
    // independent Fortran-derived reference matrix.
    for(int column=0;column<wave_size;++column) {
        WaveAmplitudes basis{};basis[column]=1.0;
        const auto direction=wave_field(basis);
        const auto plus=geometry.rhsAt(bg+width*direction,wrf::sdirk3::RhsMode::Full,dt,bg);
        const auto minus=geometry.rhsAt(bg-width*direction,wrf::sdirk3::RhsMode::Full,dt,bg);
        const auto tangent=wave_coefficients((plus-minus)/(2.0*width));
        if(column==3*nz) {
            const auto spatial=(plus-minus)/(2.0*width);
            std::cout<<"WAVE_U_ROW ";
            for(int i=0;i<nu;++i) std::cout<<i<<"="<<spatial[i].item<double>()<<" ";
            std::cout<<"\n";
        }
        for(int row=0;row<wave_size;++row)
            native_values[row*wave_size+column]=c10::complex<double>(tangent[row].real(),tangent[row].imag());
    }
    const double operator_error=(native-reference).norm().item<double>()/
        std::max(reference.norm().item<double>(),1.0);
    TORCH_CHECK(operator_error<2e-6,
                "production wave tangent disagrees with independent source-derived matrix: ",operator_error);
    std::cout<<"WAVE_OPERATOR_CROSSCHECK independent_reference=1 relative_frobenius_error="
             <<operator_error<<" tolerance=2e-6\n";
    std::cout<<"WAVE_OPERATOR_PROBE production_tangent_only=1 dimension="<<wave_size<<"\n";
}

using WaveReference=wrf::sdirk3::test::stable_wave_reference::Column;
using WaveState=wrf::sdirk3::test::stable_wave_reference::State;
using WaveComplex=wrf::sdirk3::test::stable_wave_reference::Complex;
torch::Tensor wave_state_tensor(const WaveState& state) {
    auto out=torch::empty({wave_size},torch::TensorOptions().dtype(torch::kComplexDouble));
    auto* values=out.data_ptr<c10::complex<double>>();
    for(int i=0;i<wave_size;++i) values[i]={state[i].real(),state[i].imag()};
    return out;
}
WaveState wave_tensor_state(const torch::Tensor& input) {
    auto values=input.to(torch::kCPU,torch::kComplexDouble).contiguous();
    TORCH_CHECK(values.numel()==wave_size,"reference wave state must have 17 Fourier amplitudes");
    const auto* data=values.data_ptr<c10::complex<double>>();
    WaveState out{};
    for(int i=0;i<wave_size;++i) out[i]={data[i].real(),data[i].imag()};
    return out;
}
WaveState wave_reference_advance(const torch::Tensor& matrix,const WaveState& q,double seconds) {
    return wave_tensor_state(torch::matrix_exp(seconds*matrix).mv(wave_state_tensor(q)));
}
double wave_peak_w(const WaveState& q) {
    double peak=0.0;
    for(int k=0;k<4;++k) peak=std::max(peak,std::abs(q[WaveReference::W(k)]));
    return peak;
}
WaveState wave_scaled(const WaveState& q,WaveComplex scale) {
    WaveState result{};for(int k=0;k<wave_size;++k) result[k]=scale*q[k];return result;
}
torch::Tensor wave_observation(const torch::Tensor& background,const WaveState& q) {
    return physical_w_values(background+wave_field(q));
}
struct WaveModes { WaveState q0{},q1{}; WaveComplex eigenvalue{}; };
WaveModes dominant_sub_nmax_mode(const torch::Tensor& matrix,const WaveReference& column) {
    auto eig=torch::linalg_eig(matrix);
    const auto values=std::get<0>(eig).contiguous();
    const auto vectors=std::get<1>(eig).contiguous();
    const auto* lambda=values.data_ptr<c10::complex<double>>();
    double nmax=0.0;
    for(int k=0;k<nz;++k)
        nmax=std::max(nmax,std::sqrt(column.gravity/column.theta[k]*column.theta_z[k]));
    std::cout<<"WAVE_MODE_SPECTRUM N_max_rad_s="<<nmax<<" positive_oscillatory_candidates_rad_s=";
    int selected=-1;
    for(int n=0;n<wave_size;++n) {
        const WaveComplex candidate(lambda[n].real(),lambda[n].imag());
        if(candidate.imag()>1e-7) std::cout<<candidate.imag()<<",";
        // The stable-column buoyancy frequency sets a profile-derived cutoff;
        // choose the highest positive oscillatory branch below N_max.
        if(candidate.imag()>1e-7 && candidate.imag()<=nmax &&
           (selected<0 || candidate.imag()>lambda[selected].imag())) selected=n;
    }
    std::cout<<" selected_rad_s="<<(selected>=0?lambda[selected].imag():-1.0)<<"\n";
    TORCH_CHECK(selected>=0,"independent reference has no positive oscillatory branch below stable-column N_max");
    const auto selected_vector=vectors.select(1,selected).contiguous();
    const auto eigenvalue=values.select(0,selected);
    const double matrix_norm=matrix.norm().item<double>();
    const double vector_norm=selected_vector.norm().item<double>();
    const double eigenpair_relative_residual=(matrix.mv(selected_vector)-selected_vector*eigenvalue)
        .norm().item<double>()/
        std::max((matrix_norm+std::abs(WaveComplex(lambda[selected].real(),lambda[selected].imag())))*vector_norm,
                 std::numeric_limits<double>::min());
    WaveState plus{};
    const auto* selected_values=selected_vector.data_ptr<c10::complex<double>>();
    for(int r=0;r<wave_size;++r)
        plus[r]={selected_values[r].real(),selected_values[r].imag()};
    TORCH_CHECK(std::isfinite(eigenpair_relative_residual) &&
                eigenpair_relative_residual<=std::sqrt(std::numeric_limits<double>::epsilon()),
                "selected source-derived mode failed the scale-free eigenpair residual gate: ",
                eigenpair_relative_residual);
    int phase_pivot=WaveReference::W(0);
    for(int r=1;r<4;++r)
        if(std::abs(plus[WaveReference::W(r)])>std::abs(plus[phase_pivot])) phase_pivot=WaveReference::W(r);
    const WaveComplex phase=std::polar(1.0,-std::arg(plus[phase_pivot]));
    for(auto& x:plus) x*=phase;
    WaveState minus{};
    for(int r=0;r<wave_size;++r)
        minus[r]=(r<4 ? -std::conj(plus[r]) : std::conj(plus[r]));
    WaveModes modes;
    modes.eigenvalue={lambda[selected].real(),lambda[selected].imag()};
    for(int r=0;r<wave_size;++r) {
        modes.q0[r]=plus[r]+minus[r];
        modes.q1[r]=WaveComplex(0.0,1.0)*(plus[r]-minus[r]);
    }
    const double peak=wave_peak_w(modes.q0);
    double q0_norm=0.0,q1_norm=0.0,q01=0.0;
    for(int r=0;r<wave_size;++r) {
        q0_norm+=std::norm(modes.q0[r]);q1_norm+=std::norm(modes.q1[r]);
        q01+=std::real(std::conj(modes.q0[r])*modes.q1[r]);
    }
    TORCH_CHECK(std::isfinite(peak) && peak>0.0 && q1_norm>0.0 &&
                q0_norm*q1_norm-q01*q01>1e-12*q0_norm*q1_norm,
                "selected mode temporal quadratures are not independent");
    const double scale=0.01/peak;
    for(auto& x:modes.q0) x*=scale;
    for(auto& x:modes.q1) x*=scale;
    const auto q0_energy=column.physicalEnergy(modes.q0);
    const auto q1_energy=column.physicalEnergy(modes.q1);
    double pair_w_kinetic=0.0,pair_available_potential=0.0;
    for(const auto* energy:{&q0_energy,&q1_energy}) {
        const double bulk=energy->bulk();
        const double acoustic_fraction=energy->acoustic/bulk;
        TORCH_CHECK(std::isfinite(energy->kinetic_u) && energy->kinetic_u>=0.0 &&
                    std::isfinite(energy->kinetic_w) && energy->kinetic_w>=0.0 &&
                    std::isfinite(energy->available_potential) && energy->available_potential>=0.0 &&
                    std::isfinite(energy->acoustic) && energy->acoustic>=0.0 &&
                    std::isfinite(energy->top_surface_candidate) && energy->top_surface_candidate>=0.0 &&
                    std::isfinite(bulk) && bulk>0.0 &&
                    std::isfinite(acoustic_fraction) && acoustic_fraction>=0.0 && acoustic_fraction<=1.0,
                    "selected sub-N_max mode has invalid physical energy participation");
        pair_w_kinetic=std::max(pair_w_kinetic,energy->kinetic_w);
        pair_available_potential=std::max(pair_available_potential,energy->available_potential);
    }
    TORCH_CHECK(pair_w_kinetic>0.0 && pair_available_potential>0.0,
                "selected sub-N_max mode pair lacks finite positive W kinetic/available-potential participation");
    std::cout<<"WAVE_MODE_PHYSICAL mode=coupled_sub_Nmax_mode"
             <<" eigenpair_relative_residual="<<eigenpair_relative_residual
             <<" q0_W_kinetic="<<q0_energy.kinetic_w
             <<" q0_available_potential="<<q0_energy.available_potential
             <<" q0_acoustic_fraction="<<(q0_energy.acoustic/q0_energy.bulk())
             <<" q1_W_kinetic="<<q1_energy.kinetic_w
             <<" q1_available_potential="<<q1_energy.available_potential
             <<" q1_acoustic_fraction="<<(q1_energy.acoustic/q1_energy.bulk())<<"\n";
    return modes;
}
WaveState wave_combine(const WaveState& q0,const WaveState& q1,double a,double b) {
    WaveState result{};for(int k=0;k<wave_size;++k) result[k]=a*q0[k]+b*q1[k];return result;
}
struct WaveMap {
    double value=0.0;
    torch::Tensor gradient;
    Result simulation;
};
WaveMap evaluate_wave_inverse(const torch::Tensor& background,const WaveState& q0,const WaveState& q1,
        const torch::Tensor& observations,double physical_std,int steps,
        const torch::Tensor& coefficients,bool pullback,float newton_tol=1e-10f) {
    const auto a=coefficients[0].item<double>(),b=coefficients[1].item<double>();
    const auto state=wave_combine(q0,q1,a,b);
    std::vector<torch::Tensor> timed(steps);
    timed[steps/2-1]=observations.select(0,0);
    timed[steps-1]=observations.select(0,1);
    auto simulation=run(background+wave_field(state),steps,false,pullback,
        torch::Tensor(),0.1,timed,physical_std,false,newton_tol,10.0f,0.0f,true);
    auto gradient=torch::zeros({2},torch::TensorOptions().dtype(torch::kFloat64));
    if(pullback) {
        const auto gx=wave_coefficients(simulation.gradient);
        double ga=0.0,gb=0.0;
        for(int k=0;k<wave_size;++k) {
            ga+=gx[k].real()*q0[k].real()+gx[k].imag()*q0[k].imag();
            gb+=gx[k].real()*q1[k].real()+gx[k].imag()*q1[k].imag();
        }
        const double quadrature_weight=0.5*nx*ny;
        gradient[0]=quadrature_weight*ga;gradient[1]=quadrature_weight*gb;
    }
    return {simulation.value,gradient,std::move(simulation)};
}
struct WaveFixture {
    TileCase tile;
    torch::Tensor background;
    WaveReference reference;
    torch::Tensor matrix;
    WaveState mode,quadrature;
    WaveComplex eigenvalue;
    explicit WaveFixture() : tile(5000.0f),reference(),mode{},quadrature{},eigenvalue{} {
        configure(false,0.0f);prepare_native(tile);
        tile.solver.setVerticalInterpolationCoefficients(tile.half.data(),tile.half.data(),2.0f,-1.5f,0.5f);
        background=tile.nonzeroHydrostaticBackground(nullptr,2.0);
        reference=WaveReference::fromStableFixture(tile,background);
        matrix=reference.matrixTensor();
        const auto pair=dominant_sub_nmax_mode(matrix,reference);
        mode=pair.q0;quadrature=pair.q1;eigenvalue=pair.eigenvalue;
    }
};
std::array<double,2> wave_temporal_coordinates(const WaveState& q0,const WaveState& q1,
                                                const WaveState& q) {
    double g00=0.0,g01=0.0,g11=0.0,r0=0.0,r1=0.0;
    for(int k=0;k<wave_size;++k) {
        g00+=std::real(std::conj(q0[k])*q0[k]);
        g01+=std::real(std::conj(q0[k])*q1[k]);
        g11+=std::real(std::conj(q1[k])*q1[k]);
        r0+=std::real(std::conj(q0[k])*q[k]);
        r1+=std::real(std::conj(q1[k])*q[k]);
    }
    const double determinant=g00*g11-g01*g01;
    TORCH_CHECK(std::isfinite(determinant) && determinant>1e-12*g00*g11,
                "standing-wave temporal quadratures are not independent");
    return {(r0*g11-r1*g01)/determinant,(r1*g00-r0*g01)/determinant};
}
double energy_component_relative_error(double native,double reference) {
    return std::abs(native-reference)/std::max(std::abs(reference),1.0);
}
void run_wave_forward_experiment() {
    WaveFixture fixture;
    TORCH_CHECK(fixture.eigenvalue.imag()>0.0 && std::abs(fixture.eigenvalue.real())<1e-8,
                "selected source-derived sub-N_max mode is not oscillatory");
    const double period=2.0*std::acos(-1.0)/fixture.eigenvalue.imag();
    constexpr float step_dt=10.0f;
    const int steps=static_cast<int>(std::ceil(period/step_dt));
    const double duration=steps*step_dt;
    TORCH_CHECK(steps>=100 && duration>=period && duration-period<step_dt,
                "sub-N_max oscillation period was not resolved by the ten-second forecast");
    const WaveState initial_mode=fixture.mode;
    const auto truth=wave_reference_advance(fixture.matrix,initial_mode,duration);
    const auto quarter=wave_reference_advance(fixture.matrix,initial_mode,0.25*period);
    const auto half=wave_reference_advance(fixture.matrix,initial_mode,0.5*period);
    const auto reference_energy=fixture.reference.physicalEnergy(truth);
    const auto start_energy=fixture.reference.physicalEnergy(initial_mode);
    const auto quarter_energy=fixture.reference.physicalEnergy(quarter);
    const auto half_energy=fixture.reference.physicalEnergy(half);
    const auto forecast=run(fixture.background+wave_field(initial_mode),steps,false,false,
        torch::Tensor(),0.1,{},0.0,false,1e-10f,step_dt,0.0f,true);
    const auto half_forecast=run(fixture.background+wave_field(wave_scaled(initial_mode,{0.5,0.0})),
        steps,false,false,torch::Tensor(),0.1,{},0.0,false,1e-10f,step_dt,0.0f,true);
    TORCH_CHECK(forecast.states.size()==static_cast<size_t>(steps),
                "gravity wave forecast lost accepted FP64 checkpoints");
    TORCH_CHECK(half_forecast.states.size()==static_cast<size_t>(steps),
                "half-amplitude gravity wave forecast lost accepted checkpoints");
    const std::array<int,3> sample_steps{{std::max(1,static_cast<int>(std::lround(0.25*period/step_dt))),
        std::max(1,static_cast<int>(std::lround(0.5*period/step_dt))),steps}};
    std::array<wrf::sdirk3::test::stable_wave_reference::EnergyParts,3> sampled_reference{},sampled_native{};
    double max_energy_component_error=0.0;
    for(int n=0;n<3;++n) {
        const double sample_time=sample_steps[n]*step_dt;
        const auto reference_state=wave_reference_advance(fixture.matrix,initial_mode,sample_time);
        const auto native_state=wave_coefficients(forecast.states[sample_steps[n]-1]-fixture.background);
        sampled_reference[n]=fixture.reference.physicalEnergy(reference_state);
        sampled_native[n]=fixture.reference.physicalEnergy(native_state);
        const auto& a=sampled_native[n];const auto& b=sampled_reference[n];
        max_energy_component_error=std::max({max_energy_component_error,
            energy_component_relative_error(a.kinetic_u,b.kinetic_u),
            energy_component_relative_error(a.kinetic_w,b.kinetic_w),
            energy_component_relative_error(a.acoustic,b.acoustic),
            energy_component_relative_error(a.available_potential,b.available_potential)});
    }
    const WaveState observed=wave_coefficients(forecast.states.back()-fixture.background);
    double error=0.0,reference_norm=0.0;
    for(int k=0;k<wave_size;++k) {
        error+=std::norm(observed[k]-truth[k]);
        reference_norm+=std::norm(truth[k]);
    }
    const double relative_error=std::sqrt(error/std::max(reference_norm,1e-300));
    const auto coordinates=wave_temporal_coordinates(fixture.mode,fixture.quadrature,observed);
    const double phase_error=std::remainder(std::atan2(coordinates[1],coordinates[0])-
        fixture.eigenvalue.imag()*duration,2.0*std::acos(-1.0));
    const double measured_amplitude=std::hypot(coordinates[0],coordinates[1]);
    const double expected_amplitude=std::exp(fixture.eigenvalue.real()*duration);
    const WaveState half_observed=wave_coefficients(half_forecast.states.back()-fixture.background);
    double half_difference=0.0,half_norm=0.0,homogeneous_difference=0.0;
    for(int k=0;k<wave_size;++k) {
        half_difference+=std::norm(half_observed[k]-0.5*truth[k]);
        half_norm+=std::norm(0.5*truth[k]);
        homogeneous_difference+=std::norm(observed[k]-2.0*half_observed[k]);
    }
    const double half_reference_error=std::sqrt(half_difference/std::max(half_norm,1e-300));
    const double amplitude_scaling_error=std::sqrt(homogeneous_difference/
        std::max(reference_norm,1e-300));
    const auto native_start_energy=fixture.reference.physicalEnergy(wave_coefficients(wave_field(initial_mode)));
    const double exchange_scale=std::max(start_energy.kinetic_u+start_energy.kinetic_w,
                                         start_energy.available_potential);
    TORCH_CHECK(std::isfinite(exchange_scale) && exchange_scale>0.0,
                "standing-wave kinetic/potential exchange scale is invalid");
    const double reference_kinetic_delta=sampled_reference[0].kinetic_u+sampled_reference[0].kinetic_w-
        start_energy.kinetic_u-start_energy.kinetic_w;
    const double reference_potential_delta=sampled_reference[0].available_potential-
        start_energy.available_potential;
    const double native_kinetic_delta=sampled_native[0].kinetic_u+sampled_native[0].kinetic_w-
        native_start_energy.kinetic_u-native_start_energy.kinetic_w;
    const double native_potential_delta=sampled_native[0].available_potential-
        native_start_energy.available_potential;
    TORCH_CHECK(std::isfinite(relative_error) && relative_error<0.08,
                "one-period native wave state differs from independent linear forecast: ",relative_error);
    TORCH_CHECK(std::isfinite(phase_error) && std::abs(phase_error)<0.12,
                "one-period gravity wave phase differs from independent forecast: ",phase_error);
    TORCH_CHECK(std::isfinite(half_reference_error) && half_reference_error<0.08 &&
                amplitude_scaling_error<0.01,
                "half-amplitude wave forecast did not preserve independent linear scaling");
    const double material_exchange=0.1*exchange_scale;
    TORCH_CHECK(std::isfinite(reference_kinetic_delta) && std::isfinite(reference_potential_delta) &&
                reference_kinetic_delta*reference_potential_delta<0.0 &&
                std::abs(reference_kinetic_delta)>material_exchange &&
                std::abs(reference_potential_delta)>material_exchange &&
                std::isfinite(native_kinetic_delta) && std::isfinite(native_potential_delta) &&
                native_kinetic_delta*native_potential_delta<0.0 &&
                std::abs(native_kinetic_delta)>material_exchange &&
                std::abs(native_potential_delta)>material_exchange,
                "standing wave did not show material opposite-signed kinetic/available-potential exchange in reference and native states");
    const double amplitude_decay_relative_error=std::abs(measured_amplitude-expected_amplitude)/
        std::max(std::abs(expected_amplitude),std::numeric_limits<double>::min());
    TORCH_CHECK(std::isfinite(amplitude_decay_relative_error) && amplitude_decay_relative_error<0.01,
                "one-period modal amplitude decay differs from the source-derived eigenvalue: ",
                amplitude_decay_relative_error);
    TORCH_CHECK(std::isfinite(max_energy_component_error) && max_energy_component_error<0.01,
                "native standing-wave energy components differ from source-derived reference: ",
                max_energy_component_error);
    const auto quarter_parts=fixture.reference.rhs(quarter);
    double pressure_rhs_norm=0.0,buoyancy_rhs_norm=0.0;
    for(int k=0;k<4;++k) {
        pressure_rhs_norm+=std::norm(quarter_parts.w_pressure[k]);
        buoyancy_rhs_norm+=std::norm(quarter_parts.w_buoyancy[k]);
    }
    pressure_rhs_norm=std::sqrt(pressure_rhs_norm);
    buoyancy_rhs_norm=std::sqrt(buoyancy_rhs_norm);
    std::cout<<"WAVE_FORWARD_REFERENCE mode=coupled_sub_Nmax_mode omega_rad_s="<<fixture.eigenvalue.imag()
             <<" decay_rate_s="<<fixture.eigenvalue.real()<<" period_s="<<period
             <<" duration_s="<<duration<<" dt_s="<<step_dt<<" steps="<<steps
             <<" initial_peak_W_m_s="<<wave_peak_w(initial_mode)
             <<" endpoint_relative_state_error="<<relative_error
             <<" phase_error_rad="<<phase_error
             <<" temporal_amplitude="<<measured_amplitude
             <<" expected_amplitude="<<expected_amplitude
             <<" amplitude_decay_error="<<std::abs(measured_amplitude-expected_amplitude)
             <<" amplitude_decay_relative_error="<<amplitude_decay_relative_error
             <<" half_amplitude_reference_error="<<half_reference_error
             <<" A_vs_Ahalf_scaling_error="<<amplitude_scaling_error
             <<" energy_bulk_start="<<start_energy.bulk()
             <<" energy_bulk_end="<<reference_energy.bulk()
             <<" energy_bulk_quarter="<<quarter_energy.bulk()
             <<" energy_qtr_time_s="<<sample_steps[0]*step_dt
             <<" native_qtr_kinetic_w="<<sampled_native[0].kinetic_w
             <<" reference_qtr_kinetic_w="<<sampled_reference[0].kinetic_w
             <<" native_qtr_available_potential="<<sampled_native[0].available_potential
             <<" reference_qtr_available_potential="<<sampled_reference[0].available_potential
             <<" energy_half_time_s="<<sample_steps[1]*step_dt
             <<" native_half_kinetic_w="<<sampled_native[1].kinetic_w
             <<" reference_half_kinetic_w="<<sampled_reference[1].kinetic_w
             <<" native_half_available_potential="<<sampled_native[1].available_potential
             <<" reference_half_available_potential="<<sampled_reference[1].available_potential
             <<" native_end_kinetic_w="<<sampled_native[2].kinetic_w
             <<" reference_end_kinetic_w="<<sampled_reference[2].kinetic_w
             <<" native_end_available_potential="<<sampled_native[2].available_potential
             <<" reference_end_available_potential="<<sampled_reference[2].available_potential
             <<" max_energy_component_relative_error="<<max_energy_component_error
             <<" quarter_w_pressure_rhs_norm="<<pressure_rhs_norm
             <<" quarter_w_buoyancy_rhs_norm="<<buoyancy_rhs_norm
             <<" energy_kinetic_u_end="<<reference_energy.kinetic_u
             <<" energy_kinetic_w_end="<<reference_energy.kinetic_w
             <<" energy_kinetic_w_quarter="<<quarter_energy.kinetic_w
             <<" energy_acoustic_end="<<reference_energy.acoustic
             <<" energy_available_potential_end="<<reference_energy.available_potential
             <<" energy_available_potential_quarter="<<quarter_energy.available_potential
             <<" exchange_scale="<<exchange_scale
             <<" reference_quarter_delta_kinetic="<<reference_kinetic_delta
             <<" reference_quarter_delta_available_potential="<<reference_potential_delta
             <<" native_quarter_delta_kinetic="<<native_kinetic_delta
             <<" native_quarter_delta_available_potential="<<native_potential_delta
             <<" energy_half_bulk="<<half_energy.bulk()
             <<" top_surface_candidate_end="<<reference_energy.top_surface_candidate
             <<" energy_conservation_asserted=0 boundary_work_unclosed=1\n";
}
void run_wave_inverse_experiment() {
    WaveFixture fixture;
    constexpr int steps=30;
    constexpr double step_dt=10.0;
    constexpr double physical_std=3.0e-4;
    const WaveState truth=wave_combine(fixture.mode,fixture.quadrature,0.78,-0.36);
    const auto obs_first=wave_observation(fixture.background,
        wave_reference_advance(fixture.matrix,truth,0.5*steps*step_dt));
    const auto obs_final=wave_observation(fixture.background,
        wave_reference_advance(fixture.matrix,truth,steps*step_dt));
    const auto observations=torch::stack({obs_first,obs_final});
    auto x=torch::zeros({2},torch::TensorOptions().dtype(torch::kFloat64));
    auto current=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,observations,
        physical_std,steps,x,true);
    TORCH_CHECK(std::isfinite(current.value) && torch::isfinite(current.gradient).all().item<bool>(),
                "multi-output gravity wave inverse returned nonfinite objective or gradient");
    auto direction=torch::tensor({0.6,-0.8},x.options());
    const double epsilon=0.01;
    const auto plus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,observations,
        physical_std,steps,x+epsilon*direction,false);
    const auto minus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,observations,
        physical_std,steps,x-epsilon*direction,false);
    const double fd=(plus.value-minus.value)/(2.0*epsilon);
    const double adjoint=current.gradient.dot(direction).item<double>();
    const double gradient_error=std::abs(fd-adjoint)/std::max({1.0,std::abs(fd),std::abs(adjoint)});
    std::cout<<"WAVE_INVERSE_FD epsilon="<<epsilon<<" directional_fd="<<fd
             <<" adjoint="<<adjoint<<" relative_error="<<gradient_error<<"\n";
    for(double width:{0.02,0.005}) {
        const auto width_plus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,
            observations,physical_std,steps,x+width*direction,false);
        const auto width_minus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,
            observations,physical_std,steps,x-width*direction,false);
        const double width_fd=(width_plus.value-width_minus.value)/(2.0*width);
        const double width_error=std::abs(width_fd-adjoint)/
            std::max({1.0,std::abs(width_fd),std::abs(adjoint)});
        std::cout<<"WAVE_INVERSE_FD epsilon="<<width<<" directional_fd="<<width_fd
                 <<" adjoint="<<adjoint<<" relative_error="<<width_error<<"\n";
        TORCH_CHECK(std::isfinite(width_error) && width_error<2.0e-3,
                    "wave adjoint failed directional FD at width ",width,": ",width_error);
    }
    const auto tight=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,
        observations,physical_std,steps,x,true,1e-11f);
    const auto tight_plus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,
        observations,physical_std,steps,x+0.01*direction,false,1e-11f);
    const auto tight_minus=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,
        observations,physical_std,steps,x-0.01*direction,false,1e-11f);
    const double tight_fd=(tight_plus.value-tight_minus.value)/0.02;
    const double tight_adjoint=tight.gradient.dot(direction).item<double>();
    const double tight_error=std::abs(tight_fd-tight_adjoint)/
        std::max({1.0,std::abs(tight_fd),std::abs(tight_adjoint)});
    TORCH_CHECK(std::isfinite(tight_error) && tight_error<2.0e-3,
                "tighter-Newton wave adjoint disagrees with directional FD: ",tight_error);
    std::cout<<"WAVE_INVERSE_NEWTON_TOL loose=1e-10 tight=1e-11"
             <<" loose_gradient_norm="<<current.gradient.norm().item<double>()
             <<" tight_gradient_norm="<<tight.gradient.norm().item<double>()
             <<" gradient_delta="<<(current.gradient-tight.gradient).norm().item<double>()
             <<" tight_fd_epsilon=0.01 tight_fd="<<tight_fd
             <<" tight_adjoint="<<tight_adjoint<<" tight_relative_error="<<tight_error<<"\n";
    TORCH_CHECK(std::isfinite(fd) && gradient_error<2.0e-3,
                "multi-output FP64 adjoint disagrees with independent directional FD: ",gradient_error);
    std::cout<<"WAVE_INVERSE_CHECK directional_fd="<<fd<<" adjoint="<<adjoint
             <<" relative_gradient_error="<<gradient_error<<" observations=2 output_times_s="
             <<(steps*step_dt/2.0)<<","<<(steps*step_dt)<<" physical_W_sigma_m_s="
             <<physical_std<<" controls=modal_amplitude,modal_phase_quadrature\n";
    torch::Tensor hessian=torch::eye(2,x.options());
    int accepted=0;
    const double start_value=current.value,start_gradient=current.gradient.norm().item<double>();
    for(int iteration=0;iteration<12;++iteration) {
        const auto grad=current.gradient;
        if(grad.norm().item<double>()<1e-5) break;
        auto direction_step=-hessian.mv(grad);
        if(grad.dot(direction_step).item<double>()>=0.0) {
            hessian=torch::eye(2,x.options());direction_step=-grad;
        }
        const double direction_norm=direction_step.norm().item<double>();
        if(direction_norm>0.2) direction_step*=0.2/direction_norm;
        double alpha=1.0;bool found=false;
        WaveMap trial;
        for(int backtrack=0;backtrack<18;++backtrack,alpha*=0.5) {
            trial=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,observations,
                physical_std,steps,x+alpha*direction_step,false);
            if(trial.value<=current.value+1e-4*alpha*grad.dot(direction_step).item<double>()) {
                found=true;break;
            }
        }
        TORCH_CHECK(found,"gravity modal inverse Armijo search found no admissible descent step");
        auto next_x=x+alpha*direction_step;
        auto next=evaluate_wave_inverse(fixture.background,fixture.mode,fixture.quadrature,observations,
            physical_std,steps,next_x,true);
        const auto s=next_x-x,y=next.gradient-grad;
        const double ys=y.dot(s).item<double>();
        if(ys>1e-12) {
            const double rho=1.0/ys;
            auto identity=torch::eye(2,x.options());
            hessian=(identity-rho*torch::ger(s,y)).mm(hessian)
                .mm(identity-rho*torch::ger(y,s))+rho*torch::ger(s,s);
        }
        x=next_x;current=std::move(next);++accepted;
        std::cout<<"WAVE_INVERSE_BFGS iter="<<iteration<<" cost="<<current.value
                 <<" gradient_norm="<<current.gradient.norm().item<double>()
                 <<" alpha="<<alpha<<" curvature="<<ys<<"\n";
        if(current.gradient.norm().item<double>()<1e-5) break;
    }
    const double final_gradient_norm=current.gradient.norm().item<double>();
    TORCH_CHECK(accepted>0 && current.value<start_value &&
                final_gradient_norm<start_gradient && final_gradient_norm<1e-5,
                "wave amplitude/phase inverse did not reduce cost and gradient");
    const double amplitude=std::hypot(x[0].item<double>(),x[1].item<double>());
    const double phase=std::atan2(x[1].item<double>(),x[0].item<double>());
    TORCH_CHECK(std::abs(amplitude-std::hypot(0.78,0.36))<0.15,
                "wave inverse failed to recover modal amplitude within discretization tolerance");
    TORCH_CHECK(std::abs(std::remainder(phase-std::atan2(-0.36,0.78),2.0*std::acos(-1.0)))<0.2,
                "wave inverse failed to recover modal phase within discretization tolerance");
    const double analysis_time=steps*step_dt;
    const double withheld_duration=600.0;
    const int withheld_steps=static_cast<int>(withheld_duration/step_dt);
    TORCH_CHECK(current.simulation.states.size()==static_cast<size_t>(steps),
                "inverse analysis run did not retain its accepted native endpoint");
    const auto withheld=run(current.simulation.states.back(),withheld_steps,
        false,false,torch::Tensor(),0.1,{},0.0,false,1e-10f,step_dt,0.0f,true);
    const auto withheld_native=wave_coefficients(withheld.states.back()-fixture.background);
    const auto withheld_truth=wave_reference_advance(fixture.matrix,truth,analysis_time+withheld_duration);
    double withheld_state_delta=0.0,withheld_state_norm=0.0;
    for(int k=0;k<wave_size;++k) {
        withheld_state_delta+=std::norm(withheld_native[k]-withheld_truth[k]);
        withheld_state_norm+=std::norm(withheld_truth[k]);
    }
    const double withheld_state_error=std::sqrt(withheld_state_delta/std::max(withheld_state_norm,1e-300));
    const auto withheld_coordinates=wave_temporal_coordinates(fixture.mode,fixture.quadrature,withheld_native);
    const auto truth_coordinates=wave_temporal_coordinates(fixture.mode,fixture.quadrature,withheld_truth);
    const double withheld_phase_error=std::remainder(
        std::atan2(withheld_coordinates[1],withheld_coordinates[0])-
        std::atan2(truth_coordinates[1],truth_coordinates[0]),2.0*std::acos(-1.0));
    const double withheld_amplitude=std::hypot(withheld_coordinates[0],withheld_coordinates[1]);
    const double truth_amplitude=std::hypot(truth_coordinates[0],truth_coordinates[1]);
    const double withheld_amplitude_error=std::abs(withheld_amplitude-truth_amplitude)/
        std::max(truth_amplitude,1e-300);
    const auto withheld_w=physical_w_values(withheld.states.back());
    const auto withheld_truth_w=wave_observation(fixture.background,withheld_truth);
    const double withheld_w_error=(withheld_w-withheld_truth_w).norm().item<double>()/
        std::max(withheld_truth_w.norm().item<double>(),1e-300);
    const double baseline_w_error=(physical_w_values(fixture.background)-withheld_truth_w).norm().item<double>()/
        std::max(withheld_truth_w.norm().item<double>(),1e-300);
    TORCH_CHECK(withheld_state_error<0.01 && std::abs(withheld_phase_error)<0.12 &&
                withheld_amplitude_error<0.02 && withheld_w_error<0.01 &&
                withheld_w_error<baseline_w_error,
                "withheld gravity wave phase/amplitude forecast exceeded reference error budgets");
    long peak_rss_kib=0;
#if defined(__linux__) || defined(__APPLE__)
    struct rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) {
#if defined(__APPLE__)
        peak_rss_kib=usage.ru_maxrss/1024;
#else
        peak_rss_kib=usage.ru_maxrss;
#endif
    }
#endif
    std::cout<<"WAVE_INVERSE_RESULT amplitude="<<amplitude<<" phase_rad="<<phase
             <<" recovered_controls="<<x[0].item<double>()<<","<<x[1].item<double>()
             <<" start_cost="<<start_value<<" final_cost="<<current.value
             <<" start_gradient_norm="<<start_gradient
             <<" final_gradient_norm="<<final_gradient_norm<<" gradient_stop_tolerance=1e-5"
             <<" optimizer_iterations="<<accepted<<" adjoint_checkpoints="<<steps
             <<" withheld_time_s="<<(analysis_time+withheld_duration)
             <<" withheld_state_relative_error="<<withheld_state_error
             <<" withheld_phase_error_rad="<<withheld_phase_error
             <<" withheld_amplitude_relative_error="<<withheld_amplitude_error
             <<" withheld_W_baseline_relative_error="<<baseline_w_error
             <<" withheld_W_relative_error="<<withheld_w_error
             <<" withheld_W_residual_reduction="<<(baseline_w_error-withheld_w_error)
             <<" checkpoint_payload_MiB="<<(steps*total*sizeof(double)/(1024.0*1024.0))
             <<" peak_rss_kib="<<peak_rss_kib
             <<" observations_from_independent_reference=1 observation_times=2\n";
}

void run_stable_experiment() {
    configure(true,0.0f);
    TileCase tile(5000.0f);prepare_native(tile);
    double pressure_floor=0.0;
    const auto bg=tile.nonzeroHydrostaticBackground(&pressure_floor,2.0);
    const auto z=tile.fullMassCenterHeights(bg);
    const auto theta=bg.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx})+300.0;
    const double gravity=tile.solver.getGridInfo()->g;
    const auto dz=z.slice(1,1,nz)-z.slice(1,0,nz-1);
    const auto theta_bar=0.5*(theta.slice(1,1,nz)+theta.slice(1,0,nz-1));
    const auto theta_z=(theta.slice(1,1,nz)-theta.slice(1,0,nz-1))/dz;
    const auto n2=gravity/theta_bar*theta_z;
    TORCH_CHECK(torch::isfinite(n2).all().item<bool>() && n2.min().item<double>()>1e-7 &&
                n2.max().item<double>()<1e-3,"stable column N squared is invalid");
    std::cout<<"STABLE_N2 min_s2="<<n2.min().item<double>()<<" max_s2="<<n2.max().item<double>()
             <<" theta_min_K="<<theta.min().item<double>()<<" theta_max_K="<<theta.max().item<double>()
             <<" Kv=0 Kh=1000\n";
    tile.set(bg);tile.solver.requestFixedTrajectory(16,std::vector<float>(16,dt),bg);
    for(int n=0;n<16;++n) {int host=n+1;sdirk3_set_timestep_i4(&host);tile.step(dt);}
    const auto equilibrium=tile.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(equilibrium.size()==16,"stable equilibrium did not finish sixteen steps");
    double max_drift=0.0;
    for(const auto& point:equilibrium) {
        validate_physical_state(tile,point,"stable equilibrium");
        max_drift=std::max(max_drift,max_block_state_relative_difference(bg,point));
    }
    const double equilibrium_floor=16.0*(512.0*std::numeric_limits<double>::epsilon()+
        dt*gravity*8.0*pressure_floor/84000.0);
    TORCH_CHECK(max_drift<=equilibrium_floor,"stable equilibrium drift exceeds precision gate");
    const auto full=tile.rhsAt(bg,wrf::sdirk3::RhsMode::Full,dt,bg);
    const auto e=tile.rhsAt(bg,wrf::sdirk3::RhsMode::ExplicitOnly,dt,bg);
    const auto i=tile.rhsAt(bg,wrf::sdirk3::RhsMode::ImplicitOnly,dt,bg);
    TORCH_CHECK(max_block_state_relative_difference(full,e+i)<1e-12,
                "stable Full/split closure failed");
    torch::Tensor positive_small,negative_small,positive_large;
    for(double amplitude:{0.01,-0.01,0.02}) {
        auto pulse=bg.clone();auto w=pulse.slice(0,su+sv,su+sv+sw).view({ny,nw,nx});
        for(int k=0;k<nw;++k) w.select(1,k).fill_(
            amplitude*std::sin(std::acos(-1.0)*k/(nw-1)));
        const auto rhs=tile.rhsAt(pulse,wrf::sdirk3::RhsMode::Full,dt,bg);
        const auto phi_rate=rhs.slice(0,su+sv+sw,su+sv+2*sw).view({ny,nw,nx});
        const auto theta_rate=rhs.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
        const auto z_rate=0.5*(phi_rate.slice(1,1,nw)+phi_rate.slice(1,0,nz))/gravity;
        const auto w_mass=0.5*(w.slice(1,1,nw)+w.slice(1,0,nz));
        const double height_rate_error=(z_rate-w_mass).abs().max().item<double>();
        TORCH_CHECK(height_rate_error<1e-10*std::abs(amplitude),
                    "stable pulse Phi height rate is not physical W");
        const auto zm_mid=0.5*(z_rate.slice(1,1,nz)+z_rate.slice(1,0,nz-1));
        const auto wm_mid=0.5*(w_mass.slice(1,1,nz)+w_mass.slice(1,0,nz-1));
        const auto theta_eta_mid=0.5*(theta_rate.slice(1,1,nz)+theta_rate.slice(1,0,nz-1));
        const auto theta_eulerian=theta_eta_mid-zm_mid*theta_z;
        const auto bdot=gravity/theta_bar*theta_eulerian;
        const auto expected=-n2*wm_mid;
        const double restoring_error=(bdot-expected).norm().item<double>()/
            expected.norm().item<double>();
        TORCH_CHECK(expected.norm().item<double>()>1e-10 && restoring_error<1e-8 &&
                    bdot.mul(wm_mid).sum().item<double>()<0.0,
                    "stable coordinate-correct buoyancy response failed");
        std::cout<<"STABLE_RESTORING W_amplitude_m_s="<<amplitude
                 <<" N2W_norm_m_s3="<<expected.norm().item<double>()
                 <<" theta_eta_rate_max_K_s="<<theta_rate.abs().max().item<double>()
                 <<" eulerian_theta_rate_max_K_s="<<theta_eulerian.abs().max().item<double>()
                 <<" height_rate_error_m_s="<<height_rate_error
                 <<" relative_bdot_error="<<restoring_error<<"\n";
        if(amplitude==0.01) positive_small=bdot;
        else if(amplitude<0.0) negative_small=bdot;
        else positive_large=bdot;
    }
    TORCH_CHECK(torch::allclose(positive_small,-negative_small,1e-8,1e-14) &&
                torch::allclose(positive_large,2.0*positive_small,1e-8,1e-14),
                "stable restoring sign/amplitude scaling failed");
    tile.solver.closeFixedTrajectory();
    std::cout<<"STABLE_EQUILIBRIUM steps=16 seconds=4 max_block_relative_drift="<<max_drift
             <<" precision_gate="<<equilibrium_floor
             <<" restoring_kind=initial_Eulerian_buoyancy_tendency full_wave_period_unmeasured=1\n";
    WindowSpec window;window.analysis_steps=8;window.forecast_steps=4;window.kv=0.0f;
    (void)run_two_time_experiment(bg,false,window);
}

}
int main(int argc,char** argv) {
    try {
        torch::set_num_threads(1); std::cout<<std::setprecision(17);
        if(argc==2 && std::string(argv[1])=="--wave-probe") { run_wave_operator_probe();return 0; }
        if(argc==2 && std::string(argv[1])=="--stable") { run_stable_experiment();return 0; }
        if(argc==2 && std::string(argv[1])=="--wave") {
            run_wave_operator_probe();
            run_wave_forward_experiment();
            std::cout<<"FP64 carry one-period coupled sub-Nmax reference passed\n";
            return 0;
        }
        if(argc==2 && std::string(argv[1])=="--wave-inverse") {
            run_wave_inverse_experiment();
            std::cout<<"FP64 carry independent-observation wave inverse passed\n";
            return 0;
        }
        if(argc==2 && std::string(argv[1])=="--refinement") { run_refinement_experiment();return 0; }
        if(argc>1 && std::string(argv[1])=="--balanced") {
            TORCH_CHECK(argc==2,"--balanced does not accept additional arguments");
            run_balanced_inverse_experiment();
            std::cout<<"FP64 carry balanced-background two-time pilot passed\n";
            return 0;
        }
        if(argc>1 && std::string(argv[1])=="--windows") {
            TORCH_CHECK(argc==2,"--windows does not accept additional arguments");
            run_balanced_inverse_experiment(true);
            std::cout<<"FP64 carry balanced two-window forecast experiment passed\n";
            return 0;
        }
        const auto z0=initial();
        if(argc>1 && std::string(argv[1])=="--inverse") {
            run_inverse_experiment(z0);
            std::cout<<"FP64 carry twin inverse experiment passed\n";
            return 0;
        }
        if(argc>1 && std::string(argv[1])=="--two-times") {
            TORCH_CHECK(argc==2,"--two-times does not accept additional arguments");
            run_two_time_experiment(z0);
            return 0;
        }
        if(argc>1 && std::string(argv[1])=="--nonlinear") {
            TORCH_CHECK(argc==2,"--nonlinear does not accept additional arguments");
            run_two_time_experiment(z0,true);
            return 0;
        }
        TORCH_CHECK(argc==1,"unknown FP64 carry adjoint test argument: ",argv[1]);
        check_initial_contract(z0);
        check_metric_precision(z0);
        check_cancelled_bootstrap(z0);
        check_incomplete(z0);
        for(int steps : {2,4}) check_derivatives(z0,steps);
        std::cout<<"FP64 carry active multistep adjoint passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
