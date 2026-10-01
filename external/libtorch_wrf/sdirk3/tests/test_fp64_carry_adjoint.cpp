// Internal FP64 trajectory, independent primal checkpoints, and active-effect VJP.
#include "tile_test_fixture.h"
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
Objective objective(const torch::Tensor& z,const torch::Tensor& observations,double observation_scale) {
    if(!observations.defined()) return objective(z);  // Preserve the original derivative case exactly.
    TORCH_CHECK(std::isfinite(observation_scale) && observation_scale>0.0 && observations.dim()==1 &&
                observations.numel()==static_cast<int64_t>(physical_w_indices().size()),
                "invalid inverse observations or scale");
    const auto indices=physical_w_index_tensor();
    const auto residual=(z.index_select(0,indices)-observations)/
        (observation_scale*std::sqrt(static_cast<double>(indices.numel())));
    auto terminal=torch::zeros_like(z);
    auto wbar=torch::zeros({sw},z.options());
    const auto local_indices=indices-su-sv;
    wbar.index_copy_(0,local_indices,
        residual/(observation_scale*std::sqrt(static_cast<double>(indices.numel()))));
    terminal.slice(0,su+sv,su+sv+sw).copy_(wbar);
    return {0.5*residual.square().sum().item<double>(),terminal};
}
void validate_physical_state(TileCase& tile,const torch::Tensor& state,const char* where) {
    tile.checkPhysicalState(state,where);
}
struct Result { double value; torch::Tensor gradient,last_only; std::vector<torch::Tensor> states; };
Result run(const torch::Tensor& z0,int steps,bool active,bool pullback,
           const torch::Tensor& observations=torch::Tensor(),double observation_scale=0.1) {
    configure(active);
    TileCase tile(5000.0f); prepare_native(tile); tile.set(z0);
    validate_physical_state(tile,z0,"initial state");
    tile.solver.requestFixedTrajectory(steps,std::vector<float>(steps,dt),z0);
    for(int n=0;n<steps;++n) { int host=n+1; sdirk3_set_timestep_i4(&host); tile.step(dt); }
    auto checkpoints=tile.solver.getFixedTrajectoryFp64Checkpoints();
    TORCH_CHECK(checkpoints.size()==static_cast<size_t>(steps),"incomplete checkpoints");
    for(size_t n=0;n<checkpoints.size();++n)
        validate_physical_state(tile,checkpoints[n],"accepted checkpoint");
    TORCH_CHECK(torch::equal(tile.state(),checkpoints.back().to(torch::kFloat32)),"publication mismatch");
    auto obj=objective(checkpoints.back(),observations,observation_scale);
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
    const auto residual=(final_w-observations)/
        (observation_scale*std::sqrt(static_cast<double>(observations.numel())));
    return 0.5*residual.square().sum().item<double>();
}
struct MapEval { double value=0.0, data=0.0; torch::Tensor gradient; Result simulation; };
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
            try {
                auto trial=evaluate_map(basis,trial_coefficients,observations,observation_scale,true);
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
            } catch(const std::exception&) {
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
}
int main(int argc,char** argv) {
    try {
        torch::set_num_threads(1); std::cout<<std::setprecision(17);
        const auto z0=initial();
        if(argc>1 && std::string(argv[1])=="--inverse") {
            run_inverse_experiment(z0);
            std::cout<<"FP64 carry twin inverse experiment passed\n";
            return 0;
        }
        check_initial_contract(z0);
        check_metric_precision(z0);
        check_cancelled_bootstrap(z0);
        check_incomplete(z0);
        for(int steps : {2,4}) check_derivatives(z0,steps);
        std::cout<<"FP64 carry active multistep adjoint passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
