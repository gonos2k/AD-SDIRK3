#include "../wrf_sdirk3_pressure_v_direct.h"
#include <torch/torch.h>
#include <cmath>
#include <iostream>
#include <limits>
using namespace wrf::sdirk3;

int main() {
 torch::set_num_threads(1);torch::manual_seed(812);
 constexpr int n=2,m=3,nz=2;const double a=0.7;
 const auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
 ReferencePressureProfiles profiles{{1.1,1.3},{0.4,0.6},{0.2,0.15},{0.8,0.9},
                                    {0.7,0.8},{0.9,1.0},{1.2,1.4},{0.1,0.2},{-0.4,-0.6}};
 ReferencePressureModel model(n,m,profiles,a,9.81,2.0,3.0);
 PressurePackedAdapter adapter(n,m,nz);const auto layout=StateLayout::from_grid_dims(n+1,m+1,nz,n+2,m+2,nz+1);
 auto S=0.6+torch::rand({layout.total_size},opts); // deliberately different scales at aliases
 auto coeff=(0.03+0.02*torch::rand({m-1,nz,n},opts));
 const auto primary=[&](const torch::Tensor& ph,const torch::Tensor& t,const torch::Tensor& mu) {
   auto mid=0.5*(ph.slice(1,0,nz)+ph.slice(1,1,nz+1));
   auto p=mid+0.3*t+0.2*mu.unsqueeze(1);
   return -coeff*(p.slice(0,1,m)-p.slice(0,0,m-1));
 };
 DirectPressureV direct(model,S,primary);
 const auto block=[&](const torch::Tensor& x,int k,torch::IntArrayRef shape) {
   const auto& b=layout.blocks[k];return x.slice(0,b.start,b.start+b.size).view(shape);
 };
 const auto primary_packed=[&](const torch::Tensor& x) {
   auto f=adapter.extract(x);auto ph=block(x,3,{m+1,nz+1,n+1});
   auto bottom=ph.slice(0,0,m).slice(1,0,1).slice(2,0,n);
   return primary(torch::cat({bottom,f.phi},1),f.theta,f.mu);
 };
 // This independent packed P1 oracle takes every non-V row from the actual
 // adapter P0, while replacing only physical V with V-a*Jprimary.
 const auto p1=[&](const torch::Tensor& x) {
   auto out=adapter.apply(model,x);auto fx=adapter.extract(x),fo=adapter.extract(out);
   auto zero=adapter.extract(torch::zeros_like(x));
   zero.v=fx.v-a*primary_packed(x)-fo.v;
   return out+adapter.extend(zero);
 };
 auto eye=torch::eye(layout.total_size,opts);std::vector<torch::Tensor> columns;
 for(int64_t j=0;j<layout.total_size;++j) columns.push_back(p1(eye.select(1,j)));
 const auto matrix=torch::stack(columns,1);
 auto b=torch::randn({layout.total_size},opts);
 // Nonzero Phi0 and arbitrary wall/alias/complement are present in random b.
 TORCH_CHECK(block(b,3,{m+1,nz+1,n+1}).select(1,0).abs().max().item<double>()>0 &&
             block(b,1,{m+2,nz,n+1}).select(0,0).abs().max().item<double>()>0,
             "fixture missing bottom/wall complement");
 const auto dense=torch::linalg_solve(matrix,b);
 const auto dense_v=adapter.extract(dense).v;
 const auto recover=direct.recover_for_v(b,dense_v);
 const auto packed=b+adapter.extend({recover.u-adapter.extract(b).u,recover.v-adapter.extract(b).v,
   recover.w-adapter.extract(b).w,recover.phi-adapter.extract(b).phi,
   recover.theta-adapter.extract(b).theta,recover.mu-adapter.extract(b).mu});
 TORCH_CHECK((packed-dense).abs().max().item<double>()<1e-10 &&
             (p1(packed)-b).abs().max().item<double>()<1e-10,
             "direct pressure recovery differs from independent dense P1");
 auto probe=torch::randn({m-1,nz,n},opts);auto blank=adapter.extract(torch::zeros_like(b));blank.v=probe;
 const double packed_norm=(adapter.extend(blank)/S).norm().item<double>();
 TORCH_CHECK(std::abs(direct.weight_v(probe).norm().item<double>()-packed_norm)<1e-12,
             "V metric misses alias-specific reciprocal scaling");
 const auto check_solve=[&](DirectPressureV& solver,const torch::Tensor& rhs) {
   auto result=solver.solve(rhs);TORCH_CHECK(result.converged && result.x.defined() && result.arnoldi<=4,
                                             "direct bounded solve failed: ",result.reason);
   auto z=torch::zeros({m-1,nz,n},opts);auto base=solver.recover_for_v(rhs,z);auto raw=adapter.extract(rhs);
   auto base_packed=rhs+adapter.extend({base.u-raw.u,base.v-raw.v,base.w-raw.w,base.phi-raw.phi,
                                        base.theta-raw.theta,base.mu-raw.mu});
   auto schur_rhs=adapter.extract(rhs-p1(base_packed)).v;
   auto residual=rhs-p1(result.x);auto rv=adapter.extract(residual).v;
   auto ratio=direct.weight_v(rv).norm().item<double>()/direct.weight_v(schur_rhs).norm().item<double>();
   TORCH_CHECK(std::isfinite(ratio) && ratio<=0.01+1e-12 && std::abs(ratio-result.true_rel)<1e-10,
               "direct true reduced residual disagrees with packed P1");
   auto nonv=residual.clone();const auto& vb=layout.blocks[1];nonv.slice(0,vb.start,vb.start+vb.size).zero_();
   TORCH_CHECK(nonv.abs().max().item<double>()<1e-10,"direct solve corrupted non-V P1 rows");
 };
 check_solve(direct,b);
 // Pressure forcing must produce a nonzero affine V RHS even when b_V=0.
 auto pressure_forced=b.clone();auto pv=block(pressure_forced,1,{m+2,nz,n+1});pv.zero_();
 auto base=direct.recover_for_v(pressure_forced,torch::zeros({m-1,nz,n},opts));
 TORCH_CHECK(primary(torch::cat({block(pressure_forced,3,{m+1,nz+1,n+1}).slice(0,0,m).slice(1,0,1).slice(2,0,n),base.phi},1),base.theta,base.mu).norm().item<double>()>0,
             "pressure-forced case has zero V coupling");
 check_solve(direct,pressure_forced);
 // A zero native V action still requires the pressure row and dynamic top
 // to be solved, while the reduced V equation is exactly V=b_V.
 DirectPressureV zero_primary(model,S,[](const torch::Tensor& ph,const torch::Tensor&,const torch::Tensor&) {
    return torch::zeros({ph.size(0)-1,ph.size(1)-1,ph.size(2)},ph.options());
 });
 auto zero_result=zero_primary.solve(b);
 TORCH_CHECK(zero_result.converged && zero_result.x.defined() && zero_result.arnoldi<=4,
             "zero primary bounded solve failed");
 auto ref=adapter.apply(model,zero_result.x);auto ref_fields=adapter.extract(ref);
 auto x_fields=adapter.extract(zero_result.x);auto reference_only=adapter.extract(torch::zeros_like(b));
 reference_only.v=x_fields.v-ref_fields.v;
 auto zero_residual=b-(ref+adapter.extend(reference_only));
 TORCH_CHECK(zero_residual.abs().max().item<double>()<1e-10,
             "zero primary failed full packed P1");
 // A deterministic, strongly coupled asymmetric primary map exercises the
 // actual four-Arnoldi failure path rather than merely rejecting input.
 auto stiff_matrix=1000.0*torch::randn({(m-1)*nz*n,m*(nz+1)*n},opts);
 DirectPressureV stiff(model,S,[&](const torch::Tensor& ph,const torch::Tensor&,const torch::Tensor&) {
    return torch::matmul(stiff_matrix,ph.flatten()).view({m-1,nz,n});
 });
 auto capped=stiff.solve(b);
 TORCH_CHECK(!capped.converged && !capped.x.defined() && capped.arnoldi==4 &&
             capped.failure==DirectPressureV::Failure::Budget,
             "stiff reduced solve did not fail closed at the four-step cap: ",capped.reason);
 auto nan=b.clone();nan[0]=std::numeric_limits<double>::quiet_NaN();auto bad=direct.solve(nan);
 TORCH_CHECK(!bad.converged && !bad.x.defined() && bad.failure==DirectPressureV::Failure::Invalid,
             "nonfinite input must fail closed as invalid");
 std::cout<<"PRESSURE_V_DIRECT_DENSE_PASS size="<<layout.total_size<<"\n";
}
