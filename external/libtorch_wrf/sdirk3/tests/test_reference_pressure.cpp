#include "../wrf_sdirk3_reference_pressure.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
using wrf::sdirk3::ReferencePressure;
using torch::Tensor;
static double maxabs(const Tensor& x) { return x.abs().max().item<double>(); }
static void check(bool x, const char* message) { if (!x) throw std::runtime_error(message); }
static void run(int nx,int ny,int nz,double a) {
  std::vector<double> q(nz), edge(nz), h(nz);
  for(int k=0;k<nz;++k) { q[k]=0.7+0.13*k; edge[k]=0.2+0.11*k; h[k]=1.1+0.17*k; }
  edge.back()=0.39; // active one-sided top, not a rigid lid
  auto start=std::chrono::steady_clock::now();
  ReferencePressure solver(nx,ny,q,edge,h,a,9.81,3.2,4.1);
  auto built=std::chrono::steady_clock::now();
  auto opts=torch::TensorOptions().dtype(torch::kFloat64);
  auto p=torch::randn({ny,nz,nx},opts)*1e4;
  auto b=solver.apply_operator(p);
  auto x=solver.solve(b);
  auto end=std::chrono::steady_clock::now();
  check(maxabs(x-p)/std::max(1.,maxabs(p))<1e-11,"manufactured solution");
  check(maxabs(solver.apply_operator(x)-b)/std::max(1.,maxabs(b))<1e-11,"physical residual");
  auto v=torch::randn_like(p), w=torch::randn_like(p);
  auto weight=torch::tensor(q,opts).reciprocal().reshape({1,nz,1});
  auto sv=solver.apply_operator(v), sw=solver.apply_operator(w);
  double lhs=(v*weight*sw).sum().item<double>(), rhs=(sv*weight*w).sum().item<double>();
  check(std::abs(lhs-rhs)<1e-11*std::max({1.,std::abs(lhs),std::abs(rhs)}),"weighted symmetry");
  check((v*weight*sv).sum().item<double>()>0,"weighted positivity");
  auto ones=torch::ones_like(p); auto expected=ones.clone();
  expected.select(1,nz-1).add_(a*a*9.81*q.back()*edge.back());
  check(maxabs(solver.apply_operator(ones)-expected)<1e-10*std::max(1.,maxabs(expected)),"constant dynamic top");
  // A real x/y eigenmode exercises complex Fourier coefficients and the DCT basis.
  const double pi=std::acos(-1.); int mx=nx>1 ? 1 : 0, my=ny>1 ? 1 : 0;
  auto ix=torch::arange(nx,opts), iy=torch::arange(ny,opts);
  auto mode=(torch::cos(2*pi*mx*ix/nx)+0.37*torch::sin(2*pi*mx*ix/nx)).reshape({1,1,nx}) *
            torch::cos(pi*my*(iy+0.5)/ny).reshape({ny,1,1});
  mode=mode.expand({ny,nz,nx}).clone();
  double lambda=4*std::pow(std::sin(pi*mx/nx),2)/(3.2*3.2)+4*std::pow(std::sin(pi*my/(2*ny)),2)/(4.1*4.1);
  auto expected_mode=mode.clone();
  for(int k=0;k<nz;++k) expected_mode.select(1,k).mul_(1+a*a*h[k]*lambda);
  expected_mode.select(1,nz-1).add_(a*a*9.81*q.back()*edge.back()*mode.select(1,nz-1));
  check(maxabs(solver.apply_operator(mode)-expected_mode)<1e-10*std::max(1.,maxabs(expected_mode)),"horizontal eigenmode");
  check(maxabs(solver.solve(expected_mode)-mode)<1e-11,"complex Fourier and DCT mode roundtrip");
  // Explicit matrix assembled from physical flux action, with independent dense solve.
  if(nx*ny*nz<=40) {
    int n=nx*ny*nz; auto eye=torch::eye(n,opts); std::vector<Tensor> cols;
    for(int i=0;i<n;++i) cols.push_back(solver.apply_operator(eye.select(1,i).reshape({ny,nz,nx})).reshape({n}));
    auto mat=torch::stack(cols,1); auto dense=torch::linalg_solve(mat,b.reshape({n,1})).reshape({ny,nz,nx});
    check(maxabs(x-dense)/std::max(1.,maxabs(dense))<1e-11,"explicit dense oracle");
  }
  std::cout<<nx<<"x"<<ny<<"x"<<nz<<" setup_us="<<std::chrono::duration_cast<std::chrono::microseconds>(built-start).count()<<" stencil_solve_us="<<std::chrono::duration_cast<std::chrono::microseconds>(end-built).count()<<"\n";
}
int main() {
  torch::manual_seed(13); torch::set_num_threads(1);
  run(1,1,1,0); run(1,1,1,0.7); run(1,4,3,0.3); run(5,1,3,0.3); run(4,3,3,0.4);
  run(9,7,8,600.*1767732205903./4055673282236.);
  bool threw=false; try { ReferencePressure bad(1,1,{1.},{0.},{1.},1,9.81,1,1); } catch(const std::invalid_argument&) { threw=true; } check(threw,"invalid top");
  ReferencePressure s(1,1,{1.},{1.},{1.},1,9.81,1,1); threw=false;
  try { s.solve(torch::ones({1,1},torch::kFloat64)); } catch(const std::invalid_argument&) { threw=true; } check(threw,"invalid shape");
  std::cout<<"reference pressure tests passed\n";
}
