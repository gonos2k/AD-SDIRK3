#include "wrf_sdirk3_reference_pressure.h"
#include <ATen/ops/fft_fft.h>
#include <ATen/ops/fft_ifft.h>
#include <cmath>
#include <stdexcept>

namespace wrf::sdirk3 {
namespace {
void require(bool ok, const char* what) { if (!ok) throw std::invalid_argument(what); }
bool positive(double x) { return std::isfinite(x) && x > 0.; }
}
ReferencePressure::ReferencePressure(int64_t nx, int64_t ny, const std::vector<double>& q,
    const std::vector<double>& edge, const std::vector<double>& horizontal,
    double a, double g, double dx, double dy)
    : nx_(nx), ny_(ny), nz_(q.size()), a2g_(a*a*g), a2_(a*a),
      dx2_(dx*dx), dy2_(dy*dy), q_(q), edge_(edge), horizontal_(horizontal) {
  torch::NoGradGuard guard;
  require(nx > 0 && ny > 0 && nz_ > 0 && edge.size() == q.size() &&
          horizontal.size() == q.size(), "invalid pressure dimensions");
  require(std::isfinite(a) && a >= 0 && positive(g) && positive(dx) && positive(dy) &&
          std::isfinite(a2g_) && std::isfinite(a2_) && positive(dx2_) && positive(dy2_),
          "invalid pressure scalars");
  for (int64_t k=0;k<nz_;++k)
    require(positive(q[k]) && positive(edge[k]) && positive(horizontal[k]), "invalid pressure profile");
  auto opts = torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);
  auto idx = torch::arange(ny_, opts);
  const double pi = std::acos(-1.);
  cosine_ = torch::cos((pi/ny_) * idx.unsqueeze(1) * (idx.unsqueeze(0)+0.5));
  cosine_.select(0,0).mul_(std::sqrt(1.0/ny_));
  if (ny_ > 1) cosine_.narrow(0,1,ny_-1).mul_(std::sqrt(2.0/ny_));
  auto lx = 4*torch::pow(torch::sin(pi*torch::arange(nx_,opts)/nx_),2)/dx2_;
  auto ly = 4*torch::pow(torch::sin(pi*idx/(2*ny_)),2)/dy2_;
  auto lambda = ly.unsqueeze(1)+lx.unsqueeze(0);
  for (int64_t k=0;k<nz_;++k) {
    double low = k ? edge_[k-1] : 0.;
    auto pivot = 1 + a2g_*q_[k]*(low+edge_[k]) + a2_*horizontal_[k]*lambda;
    if (k) pivot = pivot - (-a2g_*q_[k]*low)*upper_factor_[k-1];
    require(torch::isfinite(pivot).all().item<bool>() && (pivot > 0).all().item<bool>(), "invalid pressure pivot");
    inverse_pivot_.push_back(pivot.reciprocal());
    if (k+1<nz_) upper_factor_.push_back((-a2g_*q_[k]*edge_[k])*inverse_pivot_.back());
  }
  cosine_ = cosine_.to(torch::kComplexDouble);
}
void ReferencePressure::check_field(const torch::Tensor& p) const {
  require(p.defined() && p.device().is_cpu() && p.scalar_type()==torch::kFloat64 &&
          p.dim()==3 && p.size(0)==ny_ && p.size(1)==nz_ && p.size(2)==nx_, "pressure must be CPU float64 (j,k,i)");
  require(torch::isfinite(p).all().item<bool>(), "nonfinite pressure input");
}
torch::Tensor ReferencePressure::solve(const torch::Tensor& rhs) const {
  torch::NoGradGuard guard; check_field(rhs);
  auto x = at::fft_fft(rhs, nx_, 2, "ortho");
  x = torch::matmul(cosine_, x.reshape({ny_,nz_*nx_})).reshape({ny_,nz_,nx_});
  std::vector<torch::Tensor> d; d.reserve(nz_);
  for (int64_t k=0;k<nz_;++k) {
    auto r=x.select(1,k);
    if(k) r=r-(-a2g_*q_[k]*edge_[k-1])*d.back();
    d.push_back(r*inverse_pivot_[k]);
  }
  for(int64_t k=nz_-1;k-->0;) d[k]=d[k]-upper_factor_[k]*d[k+1];
  x=torch::stack(d,1);
  x=torch::matmul(cosine_.transpose(0,1), x.reshape({ny_,nz_*nx_})).reshape({ny_,nz_,nx_});
  return torch::real(at::fft_ifft(x,nx_,2,"ortho"));
}
torch::Tensor ReferencePressure::apply_operator(const torch::Tensor& p) const {
  torch::NoGradGuard guard; check_field(p);
  auto out=p.clone();
  auto lapx=(2*p-torch::roll(p,{1},{2})-torch::roll(p,{-1},{2}))/dx2_;
  auto lapy=torch::zeros_like(p);
  if(ny_>1) {
    auto diff=(p.narrow(0,1,ny_-1)-p.narrow(0,0,ny_-1))/dy2_;
    lapy.narrow(0,0,ny_-1).sub_(diff); lapy.narrow(0,1,ny_-1).add_(diff);
  }
  for(int64_t k=0;k<nz_;++k) {
    auto center=p.select(1,k);
    auto vertical=edge_[k]*(k+1<nz_ ? center-p.select(1,k+1) : center);
    if(k) vertical=vertical+edge_[k-1]*(center-p.select(1,k-1));
    out.select(1,k).add_(a2g_*q_[k]*vertical+a2_*horizontal_[k]*(lapx.select(1,k)+lapy.select(1,k)));
  }
  return out;
}
}
