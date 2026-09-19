#include "../wrf_sdirk3_phi_horizontal.h"
#include <torch/torch.h>
#include <cmath>
#include <iostream>
#include <vector>
using namespace wrf::sdirk3::phi_horizontal;
using torch::indexing::Slice;
struct Fields { torch::Tensor phi,mass,u,v,msfux,msfvy,msfty,c1f,c2f; int ny,nx,nz; };
static torch::Tensor pat(std::vector<int64_t> sh, double a, double b) {
    int64_t n=1; for (auto x:sh) n*=x;
    auto o=torch::TensorOptions().dtype(torch::kFloat64);
    return (b+a*torch::sin(0.17*torch::arange(n,o)+0.13)).reshape(sh);
}
static Fields fields(bool packed, bool maps, int ny=6, int nx=8, int nz=4) {
    auto o=torch::TensorOptions().dtype(torch::kFloat64);
    Fields f{pat({ny,nz+1,nx},2.1,8.0),pat({ny,nx},3.0,90000.0),
             pat({ny,nz,nx+1},0.7,4.0),pat({ny+1,nz,nx},0.5,3.0),
             maps?pat({ny,nx+1},0.12,1.1):torch::ones({ny,nx+1},o),
             maps?pat({ny+1,nx},0.09,1.2):torch::ones({ny+1,nx},o),
             maps?pat({ny,nx},0.08,1.3):torch::ones({ny,nx},o),
             pat({nz+1},0.11,0.9),pat({nz+1},0.07,-0.2),ny,nx,nz};
    if (!packed) return f;
    auto ep=[](const torch::Tensor& q){
        auto c=torch::cat({q,q.slice(2,0,1)},2); return torch::cat({c,c.slice(0,c.size(0)-1,c.size(0))},0);
    };
    auto ep2=[](const torch::Tensor& q){
        auto c=torch::cat({q,q.slice(1,0,1)},1); return torch::cat({c,c.slice(0,c.size(0)-1,c.size(0))},0);
    };
    Fields p{ep(f.phi),ep2(f.mass),ep(f.u),ep(f.v),ep2(f.msfux),ep2(f.msfvy),ep2(f.msfty),f.c1f,f.c2f,ny,nx,nz};
    return p;
}
static int yghost(int j,int ny) {
    const int period=2*ny; int t=j%period; if(t<0)t+=period;
    return t<ny ? t : period-1-t;
}
static double val3(const torch::Tensor& q,int a,int b,int c) { auto z=q.contiguous(); return z.accessor<double,3>()[a][b][c]; }
static double val2(const torch::Tensor& q,int a,int b) { auto z=q.contiguous(); return z.accessor<double,2>()[a][b]; }
static double val1(const torch::Tensor& q,int a) { auto z=q.contiguous(); return z.accessor<double,1>()[a]; }
static Result oracle(const Fields& f,int order,float rdx,float rdy,float cfn,float cfn1) {
    const int ny=f.ny,nx=f.nx,nz=f.nz,nzw=nz+1; auto o=f.phi.options();
    auto xo=torch::zeros({ny,nzw,nx},o), yo=torch::zeros({ny,nzw,nx},o);
    auto xa=xo.accessor<double,3>(), ya=yo.accessor<double,3>();
    auto ph=f.phi.contiguous(), mass=f.mass.contiguous(), u=f.u.contiguous(),v=f.v.contiguous();
    auto mux=f.msfux.contiguous(),mvy=f.msfvy.contiguous(),mty=f.msfty.contiguous(),c1=f.c1f.contiguous(),c2=f.c2f.contiguous();
    for(int j=0;j<ny;++j) for(int k=1;k<=nz;++k) for(int i=0;i<nx;++i) {
        auto gx=[&](int face){
            if(order==2) return val3(ph,j,k,((face%nx)+nx)%nx)-val3(ph,j,k,((face-1)%nx+nx)%nx);
            auto at=[&](int ii){ int z=((ii%nx)+nx)%nx; return val3(ph,j,k,z); };
            if(order==4) return (8*(at(i+1)-at(i-1))-(at(i+2)-at(i-2)))/12.0;
            return (45*(at(i+1)-at(i-1))-9*(at(i+2)-at(i-2))+(at(i+3)-at(i-3)))/60.0;
        };
        auto gy=[&](int face){
            auto at=[&](int jj){ return val3(ph, yghost(jj,ny), k, i); };
            if(order==2) return at(face)-at(face-1);
            if(order==4) return (8*(at(j+1)-at(j-1))-(at(j+2)-at(j-2)))/12.0;
            return (45*(at(j+1)-at(j-1))-9*(at(j+2)-at(j-2))+(at(j+3)-at(j-3)))/60.0;
        };
        auto mu_u=[&](int q){ return q==0||q==nx ? .5*(val2(mass,j,0)+val2(mass,j,nx-1)) : .5*(val2(mass,j,q)+val2(mass,j,q-1)); };
        auto mu_v=[&](int q){ return q==0 ? val2(mass,0,i) : q==ny ? val2(mass,ny-1,i) : .5*(val2(mass,q,i)+val2(mass,q-1,i)); };
        auto fx=[&](int q){ double vel=(k==nz) ? cfn*val3(u,j,nz-1,q)+cfn1*val3(u,j,nz-2,q) : val3(u,j,k,q)+val3(u,j,k-1,q); return 0.25*(val1(c1,k)*mu_u(q)+val1(c2,k))*vel*val2(mux,j,q)*gx(q); };
        auto fy=[&](int q){ double vel=(k==nz) ? cfn*val3(v,q,nz-1,i)+cfn1*val3(v,q,nz-2,i) : val3(v,q,k,i)+val3(v,q,k-1,i); return 0.25*(val1(c1,k)*mu_v(q)+val1(c2,k))*vel*val2(mvy,q,i)*gy(q); };
        // For top, WRF's .5 coefficient equals 2*.25 used by the interior form.
        if (order == 2) {
            double fxe=fx(i+1)*(k==nz?2:1), fxw=fx(i)*(k==nz?2:1);
            double fyn=fy(j+1)*(k==nz?2:1), fys=fy(j)*(k==nz?2:1);
            xa[j][k][i]=-rdx*(fxe+fxw)/val2(mty,j,i);
            ya[j][k][i]=-rdy*(fyn+fys)/val2(mty,j,i);
        } else {
            auto gxmass=[&](){ auto at=[&](int ii){ return val3(ph,j,k,((ii%nx)+nx)%nx); }; if(order==4) return (8*(at(i+1)-at(i-1))-(at(i+2)-at(i-2)))/12.0; return (45*(at(i+1)-at(i-1))-9*(at(i+2)-at(i-2))+(at(i+3)-at(i-3)))/60.0; }();
            auto gymass=[&](){ auto at=[&](int jj){ return val3(ph,yghost(jj,ny),k,i); }; if(order==4) return (8*(at(j+1)-at(j-1))-(at(j+2)-at(j-2)))/12.0; return (45*(at(j+1)-at(j-1))-9*(at(j+2)-at(j-2))+(at(j+3)-at(j-3)))/60.0; }();
            auto fx0=[&](int q){double vel=(k==nz)?cfn*val3(u,j,nz-1,q)+cfn1*val3(u,j,nz-2,q):val3(u,j,k,q)+val3(u,j,k-1,q);return (val1(c1,k)*mu_u(q)+val1(c2,k))*vel*val2(mux,j,q);};
            auto fy0=[&](int q){double vel=(k==nz)?cfn*val3(v,q,nz-1,i)+cfn1*val3(v,q,nz-2,i):val3(v,q,k,i)+val3(v,q,k-1,i);return (val1(c1,k)*mu_v(q)+val1(c2,k))*vel*val2(mvy,q,i);};
            xa[j][k][i]=-(k==nz?.5:.25)*rdx*(fx0(i+1)+fx0(i))*gxmass/val2(mty,j,i);
            ya[j][k][i]=-(k==nz?.5:.25)*rdy*(fy0(j+1)+fy0(j))*gymass/val2(mty,j,i);
        }
    }
    return {xo,yo};
}
static bool close(const torch::Tensor&a,const torch::Tensor&b,double& e) { e=(a-b).abs().max().item<double>(); return e<2e-11; }
static bool run_ad() {
    auto f=fields(false,true);
    auto phi=f.phi.clone().set_requires_grad(true);
    auto d=torch::cos(torch::arange(phi.numel(),phi.options()).reshape_as(phi)*.19);
    auto weights=torch::sin(torch::arange(phi.numel(),phi.options()).reshape_as(phi)*.23);
    auto r=native_w_phi_horizontal_core(phi,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,
                                            f.c1f,f.c2f,6,1.3e-3f,2.1e-3f,1.5f,-.5f);
    auto loss=(r.x*weights).sum()+(r.y*torch::cos(weights)).sum();
    loss.backward();
    const double analytic=((phi.grad()*d).sum()).item<double>();
    const double eps=1.0e-6;
    double fd;
    { torch::NoGradGuard ng;
    auto rp=native_w_phi_horizontal_core(f.phi+eps*d,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,
                                               f.c1f,f.c2f,6,1.3e-3f,2.1e-3f,1.5f,-.5f);
    auto rm=native_w_phi_horizontal_core(f.phi-eps*d,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,
                                               f.c1f,f.c2f,6,1.3e-3f,2.1e-3f,1.5f,-.5f);
      auto lp=(rp.x*weights).sum()+(rp.y*torch::cos(weights)).sum();
      auto lm=(rm.x*weights).sum()+(rm.y*torch::cos(weights)).sum();
      fd=((lp-lm)/(2.0*eps)).item<double>();
    }
    const double rel=std::abs(analytic-fd)/(std::abs(fd)+1e-30);
    bool ok=phi.grad().defined()&&phi.grad().isfinite().all().item<bool>()&&
            std::isfinite(analytic)&&std::isfinite(fd)&&std::abs(fd)>1e-8&&rel<1e-7;
    std::cout<<"AD directional VJP="<<analytic<<" FD="<<fd<<" rel="<<rel<<" "<<(ok?"PASS":"FAIL")<<"\n";
    return ok;
}
static bool run_local_linear_witness() {
    const int ny=6,nx=8,nz=2; auto o=torch::TensorOptions().dtype(torch::kFloat64);
    Fields f=fields(false,false,ny,nx,nz);
    f.phi=torch::arange(nx,o).view({1,1,nx}).expand({ny,nz+1,nx}).clone();
    f.mass=torch::ones({ny,nx},o); f.u=torch::ones({ny,nz,nx+1},o);
    f.v=torch::zeros({ny+1,nz,nx},o); f.msfux=torch::ones({ny,nx+1},o);
    f.msfvy=torch::ones({ny+1,nx},o); f.msfty=torch::ones({ny,nx},o);
    f.c1f=torch::ones({nz+1},o); f.c2f=torch::zeros({nz+1},o);
    auto r=native_w_phi_horizontal_core(f.phi,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,
                                            f.c1f,f.c2f,2,1.0f,1.0f,1.5f,-.5f);
    const double x=r.x.index({2,1,3}).item<double>(), y=r.y.index({2,1,3}).item<double>();
    const bool packed_last_distinct=(fields(true,false).phi.select(0,ny)-fields(true,false).phi.select(0,0)).abs().max().item<double>()>1e-12;
    const bool ok=std::abs(x+1.0)<1e-12&&std::abs(y)<1e-12&&packed_last_distinct;
    std::cout<<"order2 local-linear witness x="<<x<<" expected=-1 y="<<y
             <<" packed-last-row-distinct="<<(packed_last_distinct?"yes":"NO")<<" "<<(ok?"PASS":"FAIL")<<"\n";
    return ok;
}
static bool run_tiny() {
    bool ok=true;
    for(bool packed:{false,true}) for(int order:{2,4,6}) {
        auto f=fields(packed,false,1,1,2);
        auto r=native_w_phi_horizontal(f.phi,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,
                                       f.c1f,f.c2f,order,1.3e-3f,2.1e-3f,1.5f,-.5f,packed);
        auto core=fields(false,false,1,1,2); auto exp=oracle(core,order,1.3e-3f,2.1e-3f,1.5f,-.5f);
        if(packed){exp.x=extend_packed_scalar(exp.x);exp.y=extend_packed_scalar(exp.y);}
        double ex,ey; bool a=close(r.x,exp.x,ex),b=close(r.y,exp.y,ey); ok&=a&&b;
        std::cout<<"tiny packed="<<packed<<" order="<<order<<" x="<<ex<<" y="<<ey<<" "<<(a&&b?"PASS":"FAIL")<<"\n";
    }
    return ok;
}
int main(){ bool ok=true; for(bool packed:{false,true}) for(bool maps:{false,true}) for(int order:{2,4,6}) {
    auto f=fields(packed,maps); auto r=native_w_phi_horizontal(f.phi,f.mass,f.u,f.v,f.msfux,f.msfvy,f.msfty,f.c1f,f.c2f,order,1.3e-3f,2.1e-3f,1.5f,-.5f,packed);
    auto core=fields(false,maps); auto exp=oracle(core,order,1.3e-3f,2.1e-3f,1.5f,-.5f);
    if(packed){exp.x=extend_packed_scalar(exp.x);exp.y=extend_packed_scalar(exp.y);} double ex,ey; bool a=close(r.x,exp.x,ex),b=close(r.y,exp.y,ey); ok&=a&&b;
    std::cout<<"packed="<<packed<<" maps="<<maps<<" order="<<order<<" x="<<ex<<" y="<<ey<<" "<<(a&&b?"PASS":"FAIL")<<"\n";
  } ok&=run_local_linear_witness(); ok&=run_tiny(); ok&=run_ad(); return ok?0:1; }
