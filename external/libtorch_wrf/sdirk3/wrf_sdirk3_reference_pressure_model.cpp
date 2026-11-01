#include "wrf_sdirk3_reference_pressure_model.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace wrf::sdirk3 {
namespace {
void require(bool ok,const char* text) { if (!ok) throw std::invalid_argument(text); }
bool positive(double x) { return std::isfinite(x) && x>0; }
}
ReferencePressureModel::ReferencePressureModel(int64_t nx,int64_t ny,ReferencePressureProfiles p,
                                               double a,double g,double dx,double dy)
    : nx_(nx),ny_(ny),nz_(static_cast<int64_t>(p.q.size())),a_(a),g_(g),dx_(dx),dy_(dy) {
    require(nx_>=2 && ny_>=2 && nz_>=2 && std::isfinite(a_) && a_>=0 &&
            positive(g_) && positive(dx_) && positive(dy_),"invalid reference P0 dimensions/scalars");
    const auto n=static_cast<size_t>(nz_);
    for (const auto* v : {&p.etheta,&p.emu,&p.gz_edge,&p.alpha,&p.beta,&p.H,&p.nu,&p.dnw})
        require(v->size()==n,"invalid reference P0 profile length");
    std::vector<double> horizontal(n);
    for (size_t k=0;k<n;++k) {
        require(positive(p.q[k]) && positive(p.etheta[k]) && std::isfinite(p.emu[k]) &&
                positive(p.gz_edge[k]) && positive(p.alpha[k]) && positive(p.beta[k]) &&
                positive(p.H[k]) && std::isfinite(p.nu[k]) && std::isfinite(p.dnw[k]) && p.dnw[k]<0,
                "invalid reference P0 profile");
        horizontal[k]=p.alpha[k]*p.beta[k];
        require(positive(horizontal[k]),"invalid reference horizontal coefficient");
    }
    const auto opts=torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
    auto profile=[&](const std::vector<double>& v) { return torch::tensor(v,opts).view({1,nz_,1}); };
    q_=profile(p.q); et_=profile(p.etheta); em_=profile(p.emu); edge_=profile(p.gz_edge);
    alpha_=profile(p.alpha); beta_=profile(p.beta); H_=profile(p.H); nu_=profile(p.nu); dnw_=profile(p.dnw);
    schur_=std::make_unique<ReferencePressure>(nx_,ny_,p.q,p.gz_edge,horizontal,a_,g_,dx_,dy_);
}
void ReferencePressureModel::check(const ReferencePressureFields& x) const {
    const auto field=[&](const torch::Tensor& t,torch::IntArrayRef shape) {
        return t.defined() && t.is_cpu() && t.scalar_type()==torch::kFloat64 &&
               t.sizes()==shape && torch::isfinite(t).all().item<bool>();
    };
    require(field(x.u,{ny_,nz_,nx_}) && field(x.v,{ny_-1,nz_,nx_}) &&
            field(x.w,{ny_,nz_,nx_}) && field(x.phi,{ny_,nz_,nx_}) &&
            field(x.theta,{ny_,nz_,nx_}) && field(x.mu,{ny_,nx_}),
            "reference P0 fields must be finite CPU FP64 with physical staggered shapes");
}
torch::Tensor ReferencePressureModel::divergence_x(const torch::Tensor& u) const {
    return (torch::roll(u,{-1},{2})-u)/dx_;
}
torch::Tensor ReferencePressureModel::divergence_y(const torch::Tensor& v) const {
    auto walls=torch::zeros({1,nz_,nx_},v.options());
    auto faces=torch::cat({walls,v,walls},0);
    return (faces.slice(0,1,ny_+1)-faces.slice(0,0,ny_))/dy_;
}
torch::Tensor ReferencePressureModel::divergence(const torch::Tensor& u,const torch::Tensor& v) const {
    return divergence_x(u)+divergence_y(v);
}
torch::Tensor ReferencePressureModel::horizontal_x_pressure(const torch::Tensor& p) const {
    return -alpha_*(p-torch::roll(p,{1},{2}))/dx_;
}
torch::Tensor ReferencePressureModel::horizontal_y_pressure(const torch::Tensor& p) const {
    return -alpha_*(p.slice(0,1,ny_)-p.slice(0,0,ny_-1))/dy_;
}
std::pair<torch::Tensor,torch::Tensor> ReferencePressureModel::horizontal_pressure(const torch::Tensor& p) const {
    return {horizontal_x_pressure(p),horizontal_y_pressure(p)};
}
torch::Tensor ReferencePressureModel::vertical_pressure(const torch::Tensor& p) const {
    auto upper=torch::cat({p.slice(1,1,nz_),torch::zeros_like(p.slice(1,0,1))},1);
    return edge_*(p-upper);
}
torch::Tensor ReferencePressureModel::ephi(const torch::Tensor& phi) const {
    auto prev=torch::cat({torch::zeros_like(phi.slice(1,0,1)),phi.slice(1,0,nz_-1)},1);
    return -q_*(phi-prev);
}
torch::Tensor ReferencePressureModel::inverse_ephi(const torch::Tensor& pressure) const {
    return torch::cumsum(-pressure/q_,1);
}
torch::Tensor ReferencePressureModel::pressure(const ReferencePressureFields& x) const {
    return ephi(x.phi)+et_*x.theta+em_*x.mu.unsqueeze(1);
}
torch::Tensor ReferencePressureModel::cmu(const torch::Tensor& div) const {
    return (dnw_*H_*div).sum(1);
}
torch::Tensor ReferencePressureModel::pressure_row(const torch::Tensor& p, const torch::Tensor& div,
                                                     const torch::Tensor& w) const {
    return p+a_*beta_*div-a_*g_*ephi(w);
}
ReferencePressureModel::Thermodynamics ReferencePressureModel::recover_thermo(
    const torch::Tensor& p, const torch::Tensor& div, const ReferencePressureFields& b) const {
    auto md=cmu(div);
    auto mu=b.mu+a_*md;
    auto theta=b.theta-a_*nu_*md.unsqueeze(1);
    auto phi=inverse_ephi(p-et_*theta-em_*mu.unsqueeze(1));
    return {phi,theta,mu};
}
ReferencePressureFields ReferencePressureModel::recover_pv(const torch::Tensor& p, const torch::Tensor& v,
                                                              const ReferencePressureFields& b, torch::Tensor* div_out) const {
    auto u=b.u+a_*horizontal_x_pressure(p), w=b.w+a_*vertical_pressure(p);
    auto div=divergence(u,v);
    if (div_out) *div_out=div;
    auto t=recover_thermo(p,div,b);
    return {u,v,w,t.phi,t.theta,t.mu};
}
ReferencePressureFields ReferencePressureModel::solve(const ReferencePressureFields& b) const {
    torch::NoGradGuard guard; check(b);
    const auto rhs=pressure(b)-a_*beta_*divergence(b.u,b.v)+a_*g_*ephi(b.w);
    const auto p=schur_->solve(rhs);
    auto v=b.v+a_*horizontal_y_pressure(p);
    auto result=recover_pv(p,v,b); check(result); return result;
}
ReferencePressureFields ReferencePressureModel::apply(const ReferencePressureFields& x) const {
    torch::NoGradGuard guard; check(x);
    const auto p=pressure(x), div=divergence(x.u,x.v);
    const auto h=horizontal_pressure(p);
    const auto md=cmu(div);
    auto bu=x.u-a_*h.first, bv=x.v-a_*h.second, bw=x.w-a_*vertical_pressure(p);
    auto bmu=x.mu-a_*md;
    auto bt=x.theta+a_*nu_*md.unsqueeze(1);
    // Pressure row p-a(Bh Dv+g Ephi w), with Bh=-beta.
    auto bp=pressure_row(p,div,x.w);
    auto bphi=inverse_ephi(bp-et_*bt-em_*bmu.unsqueeze(1));
    ReferencePressureFields result{bu,bv,bw,bphi,bt,bmu}; check(result); return result;
}
}
