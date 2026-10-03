// Internal FP64 trajectory, independent primal checkpoints, and active-effect VJP.
#include "tile_test_fixture.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <string>
extern "C" void sdirk3_set_timestep_i4(int*);
namespace {
using namespace wrf::sdirk3::test;
constexpr float dt = 0.25f;
void configure(bool active) {
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
    c.kvdif = active ? 10.0f : 0.0f;
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
           bool probe_time_pullbacks=false,float newton_tol=1e-10f) {
    configure(active);
    TORCH_CHECK(std::isfinite(newton_tol) && newton_tol>0.0f,"invalid test-only Newton tolerance");
    wrf::sdirk3::g_sdirk3_config.newton_tol=newton_tol;
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    validate_physical_state(tile,z0,"initial state");
    tile.solver.requestFixedTrajectory(steps,std::vector<float>(steps,dt),z0);
    for(int n=0;n<steps;++n) { int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(dt); }
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
void run_two_time_experiment(const torch::Tensor& raw_initial,bool nonlinear);
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
void run_balanced_inverse_experiment() {
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
    run_two_time_experiment(background,false);
}
void run_two_time_experiment(const torch::Tensor& raw_initial,bool nonlinear=false) {
    constexpr int steps=4;
    constexpr double point_scale=1.0e-3;
    const double physical_std=std::sqrt((ny-1)*(nx-1)*(nw-2))*point_scale;
    check_trial_rejection_policy();
    configure(true);
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
    const auto truth_run=run(basis.state+basis.columns.mv(truth),steps,true,false);
    std::vector<torch::Tensor> observations(steps);
    observations[1]=physical_w_values(truth_run.states[1]);
    observations[3]=physical_w_values(truth_run.states[3]);
    const auto background=run(basis.state,steps,true,false);
    if(nonlinear) {
        for(int step=0;step<steps;++step) {
            const auto delta=truth_run.states[step]-background.states[step];
            const auto size=geometry.perturbationSize(background.states[step],delta);
            std::cout<<"NONLINEAR_TRAJECTORY_DELTA step="<<(step+1)
                     <<" max_w="<<size.max_w<<" mass_fraction="<<size.mass_fraction
                     <<" layer_fraction="<<size.layer_fraction<<"\n";
        }
    }
    const auto background2=physical_w_values(background.states[1]);
    const auto background4=physical_w_values(background.states[3]);
    const auto zero=torch::zeros({6},torch::TensorOptions().dtype(torch::kFloat64));
    auto coefficients=zero.clone();
    auto evaluate=[&](const torch::Tensor& c,bool with_vjp,float newton_tol=1e-10f) {
        auto sim=run(basis.state+basis.columns.mv(c),steps,true,with_vjp,
                     torch::Tensor(),0.1,observations,physical_std,
                     with_vjp && torch::equal(c,zero),newton_tol);
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
                          torch::Tensor(),0.1,observations,physical_std);
        const auto mh=run(basis.state-basis.columns.mv(fd_width*e),steps,true,false,
                          torch::Tensor(),0.1,observations,physical_std);
        const auto p2=physical_w_values(ph.states[1]), p4=physical_w_values(ph.states[3]);
        const auto m2=physical_w_values(mh.states[1]), m4=physical_w_values(mh.states[3]);
        const auto php=run(basis.state+basis.columns.mv((fd_width/2.0)*e),steps,true,false,
                           torch::Tensor(),0.1,observations,physical_std);
        const auto mhp=run(basis.state-basis.columns.mv((fd_width/2.0)*e),steps,true,false,
                           torch::Tensor(),0.1,observations,physical_std);
        const auto p2h=physical_w_values(php.states[1]), p4h=physical_w_values(php.states[3]);
        const auto m2h=physical_w_values(mhp.states[1]), m4h=physical_w_values(mhp.states[3]);
        const auto j24=(torch::cat({p2-m2,p4-m4})/(2.0*fd_width))/physical_std;
        const auto j24h=(torch::cat({p2h-m2h,p4h-m4h})/fd_width)/physical_std;
        const auto j4=(p4h-m4h)/(fd_width*physical_std);
        const double jscale=std::max(j24.norm().item<double>(),j24h.norm().item<double>());
        TORCH_CHECK((j24-j24h).norm().item<double>()<=1e-2*std::max(jscale,1e-14),
                    "two-time FD Jacobian column unstable at two widths: ",k);
        A24_columns.push_back(j24h); A24_coarse_columns.push_back(j24);
        A4_columns.push_back(j4);
        auto cost=[&](const torch::Tensor& w2,const torch::Tensor& w4) {
            return physical_observation_cost(w2,observations[1],physical_std)+
                   physical_observation_cost(w4,observations[3],physical_std);
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
    const auto d=torch::cat({observations[1]-background2,observations[3]-background4})/physical_std;
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

    // The same baseline tape proves that the API sums output cotangents at their
    // correct accepted times and preserves the legacy terminal-only operation.
    TORCH_CHECK(torch::equal(current.simulation.terminal_vector,current.simulation.legacy_terminal),
                "terminal-only output cotangent differs from the legacy terminal pullback");
    TORCH_CHECK(torch::count_nonzero(current.simulation.zero_cotangents).item<int64_t>()==0,
                "all-zero output cotangents produced a nonzero initial adjoint");
    TORCH_CHECK(torch::allclose(current.simulation.gradient,
        current.simulation.time2_only+current.simulation.time4_only,1e-12,1e-12),
        "separate output-time adjoints do not sum to the combined adjoint");
    std::vector<torch::Tensor> two_step_observations(2);
    two_step_observations[1]=observations[1];
    const auto independent_time2=run(basis.state,2,true,true,torch::Tensor(),0.1,
                                     two_step_observations,physical_std);
    TORCH_CHECK(torch::allclose(independent_time2.gradient,current.simulation.time2_only,1e-12,1e-12),
                "step-2 cotangent differs between the N=2 and N=4 independent local graphs");
    auto fd=torch::tensor(fd_gradient,zero.options());
    const auto omitted=zero+basis.columns.t().mv(current.simulation.time4_only);
    const auto shifted=zero+basis.columns.t().mv(current.simulation.shifted_time2);
    double budget_norm=0.0;
    for(int k=0;k<6;++k) budget_norm+=fd_budget[k]*fd_budget[k];
    const double falsification_budget=std::sqrt(budget_norm);
    TORCH_CHECK((omitted-fd).norm().item<double>()>falsification_budget,
                "omitting step-2 cotangent was not detected by independent FD");
    TORCH_CHECK((shifted-fd).norm().item<double>()>falsification_budget,
                "shifting step-2 cotangent to the wrong output was not detected by independent FD");

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
    TORCH_CHECK(replay.value==current.value && torch::equal(replay.gradient,current.gradient),
                "two-time objective/gradient replay is not deterministic");
    TORCH_CHECK(replay.simulation.states.size()==current.simulation.states.size(),
                "two-time replay checkpoint count changed");
    for(size_t n=0;n<replay.simulation.states.size();++n)
        TORCH_CHECK(torch::equal(replay.simulation.states[n],current.simulation.states[n]),
                    "two-time replay differs at checkpoint ",n);
    const double observation_count=2.0*static_cast<double>(observations[1].numel());
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
    std::cout<<"TWO_TIME_MUTATION omitted_error="<<(omitted-fd).norm().item<double>()
             <<" shifted_error="<<(shifted-fd).norm().item<double>()
             <<" budget="<<falsification_budget<<"\n";
    std::cout<<"TWO_TIME_VARIANCE terminal_mu4="<<cov4[4][4].item<double>()
             <<" both_mu4="<<cov24[4][4].item<double>()
             <<" terminal_mu5="<<cov4[5][5].item<double>()
             <<" both_mu5="<<cov24[5][5].item<double>()<<"\n";
    for(int k=0;k<6;++k)
        std::cout<<"TWO_TIME_SINGULAR index="<<k<<" terminal="<<singular4[k].item<double>()
                 <<" both="<<singular[k].item<double>()<<"\n";
    std::cout<<(nonlinear?"FP64 carry nonlinear two-time inverse experiment passed\n"
                         :"FP64 carry two-time inverse experiment passed\n");
}
}
int main(int argc,char** argv) {
    try {
        torch::set_num_threads(1); std::cout<<std::setprecision(17);
        if(argc>1 && std::string(argv[1])=="--balanced") {
            TORCH_CHECK(argc==2,"--balanced does not accept additional arguments");
            run_balanced_inverse_experiment();
            std::cout<<"FP64 carry balanced-background two-time pilot passed\n";
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
