#include "../wrf_sdirk3_reference_pressure_model.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace wrf::sdirk3;
namespace {
constexpr int nx=3,ny=3,nz=2;
const auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
ReferencePressureProfiles profiles() {
    return {{0.7,1.1},{2.0,2.5},{0.3,0.4},{0.5,0.8},
            {0.9,0.6},{1.2,1.4},{2.0,3.0},{0.1,-0.05},{-0.4,-0.6}};
}
ReferencePressureFields unpack(const torch::Tensor& x) {
    int64_t off=0; const auto take=[&](int64_t n) { auto s=x.slice(0,off,off+n); off+=n; return s; };
    auto u=take(ny*nz*nx).reshape({ny,nz,nx});
    auto v=take((ny-1)*nz*nx).reshape({ny-1,nz,nx});
    auto w=take(ny*nz*nx).reshape({ny,nz,nx});
    auto ph=take(ny*nz*nx).reshape({ny,nz,nx});
    auto t=take(ny*nz*nx).reshape({ny,nz,nx});
    auto mu=take(ny*nx).reshape({ny,nx});
    TORCH_CHECK(off==x.numel(),"test layout mismatch");
    return {u,v,w,ph,t,mu};
}
torch::Tensor pack(const ReferencePressureFields& x) {
    return torch::cat({x.u.flatten(),x.v.flatten(),x.w.flatten(),x.phi.flatten(),x.theta.flatten(),x.mu.flatten()});
}
double relative(const torch::Tensor& x,const torch::Tensor& ref) {
    return (x-ref).norm().item<double>()/std::max(ref.norm().item<double>(),1.e-300);
}
}
int main() {
    torch::set_num_threads(1);
    constexpr double a=0.7,g=2.0,dx=3.0,dy=4.0;
    ReferencePressureModel model(nx,ny,profiles(),a,g,dx,dy);
    constexpr int64_t n=4*ny*nz*nx+(ny-1)*nz*nx+ny*nx;
    torch::manual_seed(742);
    auto b=torch::randn({n},opts), x=pack(model.solve(unpack(b)));
    const double residual=relative(pack(model.apply(unpack(x))),b);
    TORCH_CHECK(residual<2.e-12,"reference P0 physical residual ",residual);
    // Build a dense coupled matrix from the independent mixed-equation apply;
    // the inverse uses the separate pressure elimination implementation.
    std::vector<torch::Tensor> cols;
    for(int64_t i=0;i<n;++i) {
        auto e=torch::zeros({n},opts); e[i]=1.0;
        cols.push_back(pack(model.apply(unpack(e))));
    }
    auto dense=torch::stack(cols,1);
    auto direct=torch::linalg_solve(dense,b.unsqueeze(1)).squeeze(1);
    const double dense_error=relative(x,direct);
    TORCH_CHECK(dense_error<2.e-12,"reference P0 differs from dense coupled solve ",dense_error);
    // A diagonal coordinate transform must not change the physical solve.
    auto scale=0.5+torch::arange(n,opts)/n;
    auto bscaled=b/scale;
    auto xscaled=pack(model.solve(unpack(scale*bscaled)))/scale;
    auto scaled_residual=(pack(model.apply(unpack(scale*xscaled)))/scale-bscaled).norm().item<double>()/
                         bscaled.norm().item<double>();
    TORCH_CHECK(scaled_residual<2.e-12,"reference P0 scaled residual ",scaled_residual);
    // A spatially constant pressure has no horizontal pressure gradient;
    // choose phi recursively so the EOS maps it to p=1 with theta=mu=0.
    auto z=torch::zeros({n},opts); auto f=unpack(z);
    f.phi.select(1,0).fill_(-1.0/0.7);
    f.phi.select(1,1).fill_(-1.0/0.7-1.0/1.1);
    auto out=model.apply(f);
    TORCH_CHECK(out.u.abs().max().item<double>()<1.e-14 && out.v.abs().max().item<double>()<1.e-14,
                "constant pressure produced horizontal gradient");
    // Constant p produces only the one-sided dynamic-top W forcing.
    TORCH_CHECK(out.w.select(1,0).abs().max().item<double>()<1.e-14 &&
                std::abs(out.w.select(1,1).mean().item<double>()+a*0.8)<1.e-14,
                "dynamic top pressure row missing or incorrect");
    // Manufacture p(j,k,i)=j+cos(2*pi*i/nx) and independently check the
    // periodic west seam and the first interior y-face signs and magnitudes.
    auto varying=unpack(torch::zeros({n},opts));
    auto pattern=torch::arange(ny,opts).view({ny,1})+
        torch::cos(2.0*std::acos(-1.0)*torch::arange(nx,opts)/nx).view({1,nx});
    varying.phi.select(1,0).copy_(-pattern/0.7);
    varying.phi.select(1,1).copy_(-pattern/0.7-pattern/1.1);
    auto varying_out=model.apply(varying);
    const auto seam_expected=a*0.9*(pattern.select(1,0)-pattern.select(1,nx-1))/dx;
    const auto y_expected=a*0.9*(pattern.select(0,1)-pattern.select(0,0))/dy;
    TORCH_CHECK((varying_out.u.select(1,0).select(1,0)-seam_expected).abs().max().item<double>()<1.e-14 &&
                (varying_out.v.select(1,0).select(0,0)-y_expected).abs().max().item<double>()<1.e-14,
                "periodic seam or interior y-face pressure sign failed");
    bool rejected=false; try { auto bad=profiles();bad.q[0]=0;ReferencePressureModel invalid(nx,ny,bad,a,g,dx,dy); }
    catch(const std::invalid_argument&) { rejected=true; }
    TORCH_CHECK(rejected,"zero EOS coefficient accepted");
    rejected=false; try { auto bad=unpack(b);bad.v=torch::zeros({ny,nz,nx},opts);model.solve(bad); }
    catch(const std::invalid_argument&) { rejected=true; }
    TORCH_CHECK(rejected,"invalid wall-face layout accepted");
    rejected=false; try { auto bad=profiles();bad.dnw[0]=0;ReferencePressureModel invalid(nx,ny,bad,a,g,dx,dy); }
    catch(const std::invalid_argument&) { rejected=true; }
    TORCH_CHECK(rejected,"invalid signed eta thickness accepted");
    rejected=false; try { auto bad=profiles();bad.gz_edge.back()=0;ReferencePressureModel invalid(nx,ny,bad,a,g,dx,dy); }
    catch(const std::invalid_argument&) { rejected=true; }
    TORCH_CHECK(rejected,"zero dynamic-top coefficient accepted");
    rejected=false; try { auto bad=unpack(b.clone());bad.mu[0][0]=std::numeric_limits<double>::infinity();model.solve(bad); }
    catch(const std::invalid_argument&) { rejected=true; }
    TORCH_CHECK(rejected,"nonfinite mass input accepted");
    // A dimensional reference-scale case at the actual ARK diagonal. These
    // are illustrative profiles, not captured WRF stage coefficients.
    const double a600=600.0*(1767732205903.0/4055673282236.0);
    ReferencePressureProfiles dimensional{{0.05,0.08},{400.0,350.0},{0.8,0.5},
        {0.001,0.0015},{0.9,1.1},{100000.0,65000.0},
        {80000.0,50000.0},{0.0001,-0.00005},{-0.4,-0.6}};
    ReferencePressureModel model600(nx,ny,dimensional,a600,9.81,100000.0,100000.0);
    auto field_scale=torch::cat({torch::full({ny*nz*nx},0.01,opts),
        torch::full({(ny-1)*nz*nx},0.01,opts),torch::full({ny*nz*nx},0.001,opts),
        torch::full({ny*nz*nx},100.0,opts),torch::full({ny*nz*nx},0.01,opts),
        torch::full({ny*nx},10.0,opts)});
    auto x600=torch::randn({n},opts)*field_scale;
    auto b600=pack(model600.apply(unpack(x600)));
    auto solved600=pack(model600.solve(unpack(b600)));
    const double b600_physical=relative(pack(model600.apply(unpack(solved600))),b600);
    const double x600_scaled=((solved600-x600)/field_scale).norm().item<double>()/
                              (x600/field_scale).norm().item<double>();
    const double b600_scaled=((pack(model600.apply(unpack(solved600)))-b600)/field_scale).norm().item<double>()/
                              (b600/field_scale).norm().item<double>();
    TORCH_CHECK(x600_scaled<1.e-9 && b600_scaled<1.e-9 && b600_physical<1.e-9,
                "h600 illustrative reference reconstruction/residual failed: ",x600_scaled," / ",b600_scaled," / ",b600_physical);
    std::cout << "REFERENCE_PRESSURE_MODEL physical_rel=" << residual
              << " dense_rel=" << dense_error << " scaled_rel=" << scaled_residual
              << " a600=" << a600 << " h600_x_scaled=" << x600_scaled
              << " h600_residual_scaled=" << b600_scaled
              << " h600_residual_physical=" << b600_physical << '\n';
}
