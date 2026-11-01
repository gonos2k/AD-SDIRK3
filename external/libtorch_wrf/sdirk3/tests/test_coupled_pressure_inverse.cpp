#include "../wrf_sdirk3_coupled_pressure_inverse.h"
#include "../wrf_sdirk3_pressure_packed_adapter.h"
#include <torch/torch.h>
#include <iostream>
#include <limits>
using wrf::sdirk3::solve_coupled_pressure;


static void real_packed_fixture() {
    using namespace wrf::sdirk3;
    constexpr int n=3,m=3,nz=2;
    const auto o=torch::TensorOptions().dtype(torch::kFloat64);
    ReferencePressureProfiles profiles{{0.7,1.1},{2.0,2.5},{0.3,0.4},{0.5,0.8},
                                       {0.9,0.6},{1.2,1.4},{2.0,3.0},{0.1,-0.05},{-0.4,-0.6}};
    const double a=0.7;
    ReferencePressureModel model(n,m,profiles,a,2.0,3.0,4.0);
    PressurePackedAdapter adapter(n,m,nz);
    const auto layout=StateLayout::from_grid_dims(n+1,m+1,nz,n+2,m+2,nz+1);
    const auto view=[&](const torch::Tensor& x,int block,torch::IntArrayRef shape) {
        const auto& b=layout.blocks[block];return x.slice(0,b.start,b.start+b.size).view(shape);
    };
    torch::manual_seed(923);auto r=torch::randn({adapter.size()},o);
    view(r,3,{m+1,nz+1,n+1}).select(1,0).fill_(2.5);
    const auto inv=[&](const torch::Tensor& x){return adapter.solve(model,x);};
    const auto action=[&](const torch::Tensor& x){
        auto f=adapter.extract(torch::zeros_like(x));
        auto ph0=view(x,3,{m+1,nz+1,n+1}).select(1,0).slice(0,0,m).slice(1,0,n);
        auto mu=view(x,5,{m+1,n+1}).slice(0,0,m).slice(1,0,n);
        auto scalar=ph0+0.3*mu;
        f.v=0.04*(scalar.slice(0,1,m)-scalar.slice(0,0,m-1)).unsqueeze(1).expand({m-1,nz,n});
        return adapter.extend(f);
    };
    auto z0=inv(r);auto g=action(z0);
    TORCH_CHECK(g.norm().item<double>()>0,"fixture has inactive coupling");
    TORCH_CHECK(torch::equal(adapter.project(g),g),"V range violates aliases/walls");
    auto gv=view(g,1,{m+2,nz,n+1});
    TORCH_CHECK(gv.select(0,0).abs().max().item<double>()==0 &&
                gv.select(0,m).abs().max().item<double>()==0 &&
                torch::equal(gv.select(2,n),gv.select(2,0)),"V walls or periodic aliases failed");
    const auto out=solve_coupled_pressure(r,a,inv,action);
    TORCH_CHECK(out.converged && out.x.defined(),"real packed reduced solve failed");
    auto residual=adapter.apply(model,out.x)-a*action(out.x)-r;
    for(int b:{0,2,3,4,5}) {
        const auto& block=layout.blocks[b];
        TORCH_CHECK(residual.slice(0,block.start,block.start+block.size).abs().max().item<double>()<1.e-10,
                    "packed non-V P1 row changed");
    }
    TORCH_CHECK(torch::allclose(view(out.x,3,{m+1,nz+1,n+1}).select(1,0),
                                view(r,3,{m+1,nz+1,n+1}).select(1,0),1e-12,1e-12),
                "Phi0 complement did not survive");
    const double actual=residual.norm().item<double>()/(std::abs(a)*g.norm().item<double>());
    TORCH_CHECK(actual<=1e-2+1e-11 && std::abs(actual-out.true_relative_residual)<1e-10,
                "packed actual P1/reduced residual identity failed");
    std::cout << "packed fixture arnoldi=" << out.arnoldi << " reduced_rel=" << out.true_relative_residual
              << " actual_p1_over_a_g=" << actual << " phi0_alias_walls=passed\n";
}

int main() {
    try {
        torch::NoGradGuard guard;
        torch::set_num_threads(1);
        real_packed_fixture();
        auto o=torch::TensorOptions().dtype(torch::kFloat64);
        // B=P0^-1 couples V rows 2,3,4 back into the other fields. The
        // C output has two V values and an alias copy at row 4.
        auto B=torch::tensor({{1.,0.,0.2,0.1,0.,0.},
                              {0.,1.,0.,0.15,0.1,0.},
                              {0.1,0.,1.,0.,0.,0.},
                              {0.,0.1,0.,1.,0.,0.},
                              {0.1,0.,0.,0.,1.,0.},
                              {0.,0.,0.1,0.2,0.,1.}},o);
        auto C=torch::zeros({6,6},o);
        C[2][0]=0.4; C[2][1]=-0.1; C[2][5]=0.2;
        C[3][0]=-0.2; C[3][1]=0.35; C[3][5]=0.1;
        C[4].copy_(C[2]);
        auto P0=torch::linalg_inv(B);
        auto r=torch::tensor({-0.137,-0.661,1.212,-2.15,-0.903,-0.464},o);
        const double a=2.0;
        auto solve=[&](const torch::Tensor& x){return B.matmul(x);};
        auto action=[&](const torch::Tensor& x){return C.matmul(x);};
        const auto result=solve_coupled_pressure(r,a,solve,action);
        TORCH_CHECK(result.converged && result.x.defined() && result.arnoldi==2 &&
                    result.true_relative_residual<=1e-2);
        const auto P1=P0-a*C;
        const auto direct=torch::linalg_solve(P1,r);
        TORCH_CHECK(torch::equal(result.base,solve(r)),"base result mismatch");
        TORCH_CHECK(torch::allclose(result.x,direct,1e-10,1e-10),"dense solution mismatch");
        const auto actual=P1.matmul(result.x)-r;
        TORCH_CHECK(actual.norm().item<double>()/r.norm().item<double>()<1e-10,
                    "full P1 residual mismatch");
        for(int i:{0,1,5}) TORCH_CHECK(std::abs(actual[i].item<double>())<1e-12,
                                       "non-V row changed");
        TORCH_CHECK(C.matmul(C).abs().max().item<double>()==0.0);
        TORCH_CHECK(C.matmul(B).matmul(C).abs().max().item<double>()>0.0,
                    "P0 inverse coupling was accidentally omitted");
        // For any reduced V trial y, P1(z0+a B y)-r = -a(g-Hy).
        auto trial=torch::tensor({0.,0.,0.3,-0.2,0.3,0.},o);
        auto z0=solve(r), g=action(z0);
        auto Htrial=trial-a*action(solve(trial));
        auto reconstructed=z0+a*solve(trial);
        TORCH_CHECK(torch::allclose(P1.matmul(reconstructed)-r,-a*(g-Htrial),1e-12,1e-12),
                    "reduced residual identity failed");
        auto zero=solve_coupled_pressure(r,a,solve,[&](const torch::Tensor& x){return torch::zeros_like(x);});
        TORCH_CHECK(zero.converged && zero.arnoldi==0 && torch::equal(zero.x,z0));

        // Five distinct reduced eigenvalues require more than four directions.
        auto B5=torch::eye(10,o), C5=torch::zeros({10,10},o), r5=torch::zeros({10},o);
        const double c[5]={4.,2.,0.9,-1.,-4.};
        for(int i=0;i<5;i++) { B5[i][i+5]=1.; C5[i+5][i]=c[i]; r5[i]=1./c[i]; }
        auto limited=solve_coupled_pressure(r5,1.0,
            [&](const torch::Tensor& x){return B5.matmul(x);},
            [&](const torch::Tensor& x){return C5.matmul(x);});
        TORCH_CHECK(!limited.converged && !limited.x.defined() && limited.arnoldi==4,
                    "budget exhaustion returned a usable vector");
        auto bad=solve_coupled_pressure(r,a,solve,[&](const torch::Tensor& x){
            return torch::full_like(x,std::numeric_limits<double>::quiet_NaN());});
        TORCH_CHECK(!bad.converged && !bad.x.defined(),"nonfinite action accepted");
        auto singular=solve_coupled_pressure(r,1.0,[](const torch::Tensor& x){return x;},
                                             [](const torch::Tensor& x){return x;});
        TORCH_CHECK(!singular.converged && !singular.x.defined(),"singular reduced operator accepted");
        bool alloc_thrown=false;
        try { (void)solve_coupled_pressure(r,a,[](const torch::Tensor&) -> torch::Tensor { throw std::bad_alloc(); },action); }
        catch(const std::bad_alloc&) { alloc_thrown=true; }
        TORCH_CHECK(alloc_thrown,"bad_alloc swallowed");
        bool oom_thrown=false;
        try { (void)solve_coupled_pressure(r,a,[](const torch::Tensor&) -> torch::Tensor {
            throw c10::OutOfMemoryError({__func__,__FILE__,__LINE__},"expected"); },action); }
        catch(const c10::OutOfMemoryError&) { oom_thrown=true; }
        TORCH_CHECK(oom_thrown,"c10 OOM swallowed");
        std::cout << "coupled pressure inverse tests passed arnoldi=" << result.arnoldi
                  << " reduced_rel=" << result.true_relative_residual
                  << " full_rel=" << actual.norm().item<double>()/r.norm().item<double>()
                  << " dense_error=" << (result.x-direct).norm().item<double>()
                  << " budget_rel=" << limited.true_relative_residual
                  << " nan_and_singular_rejected=1 alloc_exceptions_propagated=1\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
