// Internal FP64 trajectory, independent primal checkpoints, and active-effect VJP.
#include "tile_test_fixture.h"
#include <iomanip>
#include <limits>
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
Objective objective(const torch::Tensor& z) {
    auto a=torch::arange(sw,torch::kFloat64);
    auto residual=z.slice(0,su+sv,su+sv+sw)/0.1-torch::sin(0.13*a);
    auto terminal=torch::zeros_like(z);
    terminal.slice(0,su+sv,su+sv+sw).copy_(residual/(0.1*sw));
    return {0.5*residual.square().mean().item<double>(),terminal};
}
struct Result { double value; torch::Tensor gradient,last_only; std::vector<torch::Tensor> states; };
Result run(const torch::Tensor& z0,int steps,bool active,bool pullback) {
    configure(active);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    tile.solver.requestFixedTrajectory(steps,std::vector<float>(steps,dt),z0);
    for(int n=0;n<steps;++n) { int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(dt); }
    auto checkpoints=tile.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(checkpoints.size()==static_cast<size_t>(steps),"incomplete checkpoints");
    TORCH_CHECK(torch::equal(tile.state(),checkpoints.back().to(torch::kFloat32)),"publication mismatch");
    auto obj=objective(checkpoints.back());
    torch::Tensor gradient,last;
    if(pullback) {
        last=tile.solver.pullbackLastStep(obj.terminal);
        gradient=tile.solver.pullbackFixedTrajectory(obj.terminal);
        TORCH_CHECK(gradient.scalar_type()==torch::kFloat64,"initial pullback lost precision");
    }
    tile.solver.closeFixedTrajectory();
    return {obj.value,gradient,last,std::move(checkpoints)};
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
}
int main() {
    try {
        torch::set_num_threads(1); std::cout<<std::setprecision(17);
        const auto z0=initial();
        check_initial_contract(z0);
        check_metric_precision(z0);
        check_cancelled_bootstrap(z0);
        check_incomplete(z0);
        for(int steps : {2,4}) check_derivatives(z0,steps);
        std::cout<<"FP64 carry active multistep adjoint passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
