#include "wrf_sdirk3_pressure_v_direct.h"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wrf::sdirk3 {
namespace {
int dimension(int64_t x) {
    TORCH_CHECK(x>=2 && x<=std::numeric_limits<int>::max()-2,"invalid reduced grid dimension");
    return static_cast<int>(x);
}
}
DirectPressureV::DirectPressureV(const ReferencePressureModel& model,const torch::Tensor& S_diag,PrimaryCore primary)
    : model_(model), adapter_(dimension(model.nx_),dimension(model.ny_),dimension(model.nz_)),
      layout_(StateLayout::from_grid_dims(dimension(model.nx_)+1,dimension(model.ny_)+1,
                 dimension(model.nz_),dimension(model.nx_)+2,dimension(model.ny_)+2,dimension(model.nz_)+1)),
      primary_(std::move(primary)) {
    torch::NoGradGuard guard;
    TORCH_CHECK(primary_ && S_diag.defined() && S_diag.dim()==1 && S_diag.numel()==layout_.total_size &&
                S_diag.is_cpu() && S_diag.scalar_type()==torch::kFloat64 &&
                torch::isfinite(S_diag).all().item<bool>() && (S_diag>0).all().item<bool>(),
                "DirectPressureV requires a primary action and positive finite packed CPU FP64 S");
    const auto n=model_.nx_,m=model_.ny_,nz=model_.nz_;
    const auto& vb=layout_.blocks[1];
    auto S=S_diag.slice(0,vb.start,vb.start+vb.size).view({m+2,nz,n+1});
    auto inverse_square=[](const torch::Tensor& x) { return x.reciprocal().square(); };
    auto w2=inverse_square(S.slice(0,1,m).slice(2,0,n)).clone();
    // Periodic seam duplicates i=0; north odd ghost duplicates j=m-1.
    w2.select(2,0).add_(inverse_square(S.slice(0,1,m).select(2,n)));
    w2.select(0,m-2).add_(inverse_square(S.select(0,m+1).slice(1,0,n)));
    w2.select(0,m-2).select(1,0).add_(inverse_square(S.select(0,m+1).select(1,n)));
    weight_=w2.sqrt();
    TORCH_CHECK(torch::isfinite(weight_).all().item<bool>() && (weight_>0).all().item<bool>(),
                "DirectPressureV V metric overflowed or underflowed");
    auto o=S_diag.options();
    zero_u_=torch::zeros({m,nz,n},o); zero_v_=torch::zeros({m-1,nz,n},o);
    zero_w_=torch::zeros({m,nz,n},o); zero_phi_=torch::zeros_like(zero_w_);
    zero_t_=torch::zeros_like(zero_w_); zero_mu_=torch::zeros({m,n},o);
    zero_={zero_u_,zero_v_,zero_w_,zero_phi_,zero_t_,zero_mu_};
}
void DirectPressureV::check_v(const torch::Tensor& v) const {
    TORCH_CHECK(v.defined() && v.is_cpu() && v.scalar_type()==torch::kFloat64 &&
                v.sizes()==torch::IntArrayRef({model_.ny_-1,model_.nz_,model_.nx_}) &&
                torch::isfinite(v).all().item<bool>(),"DirectPressureV invalid independent V");
}
torch::Tensor DirectPressureV::weight_v(const torch::Tensor& v) const { check_v(v); return weight_*v; }
DirectPressureV::Rhs DirectPressureV::unpack(const torch::Tensor& packed) const {
    auto raw=adapter_.extract(packed);
    TORCH_CHECK(torch::isfinite(packed).all().item<bool>(),"DirectPressureV rhs must be finite");
    const auto& b=layout_.blocks[3];
    auto p0=packed.slice(0,b.start,b.start+b.size)
        .view({model_.ny_+1,model_.nz_+1,model_.nx_+1})
        .slice(0,0,model_.ny_).select(1,0).slice(1,0,model_.nx_).clone();
    auto shifted=raw; shifted.phi=raw.phi-p0.unsqueeze(1);
    return {raw,shifted,p0};
}
torch::Tensor DirectPressureV::pressure_base(const ReferencePressureFields& b,int* count) const {
    const auto div_x=model_.divergence_x(b.u);
    const auto rhs=model_.pressure(b)-model_.a_*model_.beta_*div_x+
                   model_.a_*model_.g_*model_.ephi(b.w);
    if(count) ++*count;
    return model_.schur_->solve_x_vertical(rhs);
}
torch::Tensor DirectPressureV::pressure_homogeneous(const torch::Tensor& v,int* count) const {
    const auto div_y=model_.divergence_y(v);
    if(count) ++*count;
    return model_.schur_->solve_x_vertical(-model_.a_*model_.beta_*div_y);
}
torch::Tensor DirectPressureV::primary(const torch::Tensor& p,const torch::Tensor& v,
                                       const ReferencePressureFields& b,const torch::Tensor& phi0,int* count) const {
    const auto u=b.u+model_.a_*model_.horizontal_x_pressure(p);
    const auto div=model_.divergence(u,v);
    const auto t=model_.recover_thermo(p,div,b);
    const auto phi=torch::cat({phi0.unsqueeze(1),t.phi+phi0.unsqueeze(1)},1);
    if(count) ++*count;
    auto value=primary_(phi,t.theta,t.mu);
    check_v(value);
    return value;
}
torch::Tensor DirectPressureV::schur_action(const torch::Tensor& v) const {
    torch::NoGradGuard guard; check_v(v);
    auto p=pressure_homogeneous(v,nullptr);
    return v-model_.a_*primary(p,v,zero_,zero_mu_,nullptr);
}
ReferencePressureFields DirectPressureV::recover_for_v(const torch::Tensor& packed,const torch::Tensor& v) const {
    torch::NoGradGuard guard; check_v(v); const auto b=unpack(packed);
    auto p=pressure_base(b.shifted,nullptr)+pressure_homogeneous(v,nullptr);
    auto x=model_.recover_pv(p,v,b.shifted);
    x.phi=x.phi+b.phi0.unsqueeze(1);
    return x;
}
DirectPressureV::Result DirectPressureV::solve(const torch::Tensor& packed) const {
    Result out; out.reduced_size=(model_.ny_-1)*model_.nz_*model_.nx_;
    try {
        torch::NoGradGuard guard;
        const auto b=unpack(packed);
        const auto pbase=pressure_base(b.shifted,&out.pressure_subsolves);
        const auto rhs=b.raw.v+model_.a_*primary(pbase,zero_v_,b.shifted,b.phi0,&out.primary_core_calls);
        const auto weighted=[&](const torch::Tensor& value) { return weight_*value; };
        const double rhs_norm=weighted(rhs).norm().item<double>();
        TORCH_CHECK(std::isfinite(rhs_norm),"invalid V Schur RHS norm");
        auto v=zero_v_.clone(); auto p=pbase;
        auto true_residual=[&](const torch::Tensor& x,const torch::Tensor& pressure) {
            auto r=x-model_.a_*primary(pressure,x,b.shifted,b.phi0,&out.primary_core_calls)-b.raw.v;
            const double n=weighted(r).norm().item<double>();
            TORCH_CHECK(std::isfinite(n),"invalid true V residual");
            // The denominator is the affine reduced Schur RHS norm, not b.v.
            return rhs_norm>0 ? n/rhs_norm : (n==0 ? 0.0 : std::numeric_limits<double>::infinity());
        };
        if (rhs_norm==0) {
            out.true_rel=true_residual(v,p);
        } else {
            std::array<std::array<double,4>,5> H{};
            std::array<double,4> cs{},sn{}; std::array<double,5> g{}; g[0]=rhs_norm;
            // Arnoldi vectors are in W coordinates. Pressure basis entries
            // correspond to the unweighted physical direction q/W.
            std::vector<torch::Tensor> Q,PB; Q.reserve(4); PB.reserve(4);
            Q.push_back(weighted(rhs)/rhs_norm);
            for(int j=0;j<4;++j) {
                const auto vj=Q[j]/weight_;
                auto pj=pressure_homogeneous(vj,&out.pressure_subsolves); PB.push_back(pj);
                auto work=weighted(vj-model_.a_*primary(pj,vj,zero_,zero_mu_,&out.primary_core_calls));
                const double initial_work=work.norm().item<double>();
                TORCH_CHECK(std::isfinite(initial_work),"invalid initial V Arnoldi work norm");
                for(int pass=0;pass<2;++pass) for(int i=0;i<=j;++i) {
                    const double h=Q[i].flatten().dot(work.flatten()).item<double>();
                    TORCH_CHECK(std::isfinite(h),"nonfinite V Hessenberg entry");
                    H[i][j]+=h; work=work-h*Q[i];
                }
                const double next=work.norm().item<double>();
                TORCH_CHECK(std::isfinite(next),"invalid V Arnoldi vector"); H[j+1][j]=next;
                const bool breakdown=next<=32*std::numeric_limits<double>::epsilon()*initial_work;
                if(!breakdown && j<3) Q.push_back(work/next);
                for(int i=0;i<j;++i) {
                    const double x=H[i][j],y=H[i+1][j];
                    H[i][j]=cs[i]*x+sn[i]*y; H[i+1][j]=-sn[i]*x+cs[i]*y;
                }
                const double d=std::hypot(H[j][j],H[j+1][j]);
                TORCH_CHECK(std::isfinite(d) && d>0,"singular V Hessenberg");
                cs[j]=H[j][j]/d; sn[j]=H[j+1][j]/d; H[j][j]=d; H[j+1][j]=0;
                g[j+1]=-sn[j]*g[j]; g[j]=cs[j]*g[j];
                TORCH_CHECK(std::isfinite(g[j]) && std::isfinite(g[j+1]),"nonfinite V Givens estimate");
                out.arnoldi=j+1;
                const double estimate=std::abs(g[j+1])/rhs_norm;
                if(estimate<=0.01 || j==3 || breakdown) {
                    std::array<double,4> y{};
                    for(int i=j;i>=0;--i) {
                        double r=g[i]; for(int k=i+1;k<=j;++k) r-=H[i][k]*y[k];
                        TORCH_CHECK(std::isfinite(H[i][i]) && H[i][i]!=0,"singular V backsolve");
                        y[i]=r/H[i][i];
                        TORCH_CHECK(std::isfinite(y[i]),"nonfinite V backsolve coefficient");
                    }
                    auto q=torch::zeros_like(zero_v_); p=pbase.clone();
                    for(int i=0;i<=j;++i) { q=q+y[i]*Q[i]; p=p+y[i]*PB[i]; }
                    v=q/weight_;
                    out.true_rel=true_residual(v,p);
                    if(out.true_rel<=0.01 || breakdown) break;
                }
            }
        }
        if (!(std::isfinite(out.true_rel) && out.true_rel<=0.01)) {
            if (std::isfinite(out.true_rel) && out.true_rel>0.01 && out.arnoldi==4) {
                out.failure=Failure::Budget;
                out.reason="four-step V Arnoldi budget exhausted";
            } else {
                out.reason="invalid or early-breakdown V-row solve";
            }
            return out;
        }
        auto recovered=model_.recover_pv(p,v,b.shifted);
        recovered.phi=recovered.phi+b.phi0.unsqueeze(1);
        ReferencePressureFields delta{recovered.u-b.raw.u,recovered.v-b.raw.v,recovered.w-b.raw.w,
            recovered.phi-b.raw.phi,recovered.theta-b.raw.theta,recovered.mu-b.raw.mu};
        out.x=adapter_.extend(delta); ++out.full_packed_extensions;
        out.x.add_(packed);
        TORCH_CHECK(torch::isfinite(out.x).all().item<bool>(),"nonfinite recovered packed result");
        out.converged=true; out.failure=Failure::None; out.reason="true V-row residual accepted";
        return out;
    } catch (const std::bad_alloc&) { throw; }
      catch (const c10::OutOfMemoryError&) { throw; }
      catch (const std::exception& e) { out.x=torch::Tensor(); out.reason=e.what(); return out; }
}
}
