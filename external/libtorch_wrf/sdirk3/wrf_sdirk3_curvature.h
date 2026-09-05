#pragma once

// Tensor implementation of
// module_big_step_utilities_em.F's curvature operator on the native WRF core:
// periodic X, symmetric Y, u=[m,z,n+1], v=[m+1,z,n], w=[m,z+1,n].

#include <torch/torch.h>

namespace wrf::sdirk3 {

struct CurvatureTendencies {
    // Coupled-momentum curvature rates. The caller divides by alpha to get
    // physical velocity rates; the packed adapter restores aliases explicitly.
    torch::Tensor u, v, w;
};

inline void check_curvature_layout(
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& alpha_u, const torch::Tensor& alpha_v,
    const torch::Tensor& alpha_w, const torch::Tensor& msfux,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx,
    const torch::Tensor& msfvy, const torch::Tensor& msftx,
    const torch::Tensor& msfty, const torch::Tensor& xlat_radians,
    const torch::Tensor& fzm, const torch::Tensor& fzp,
    int map_proj, bool polar) {
    TORCH_CHECK(u.defined() && v.defined() && w.defined(),
                "curvature: velocity tensors must be defined");
    TORCH_CHECK(alpha_u.defined() && alpha_v.defined() && alpha_w.defined(),
                "curvature: all alpha tensors are required");
    TORCH_CHECK(msfux.defined() && msfuy.defined() && msfvx.defined() &&
                msfvy.defined() && msftx.defined() && msfty.defined(),
                "curvature: all native map factors are required");
    TORCH_CHECK(fzm.defined() && fzp.defined(),
                "curvature: fzm and fzp are required");
    const bool tan_branch = map_proj == 6 || polar;
    TORCH_CHECK(!tan_branch || xlat_radians.defined(),
                "curvature: xlat_radians is required for projection 6/polar");
    TORCH_CHECK(u.dim() == 3 && v.dim() == 3 && w.dim() == 3,
                "curvature: velocities must be rank-3");
    const int64_t m = u.size(0), z = u.size(1), n = u.size(2) - 1;
    TORCH_CHECK(m >= 2 && n >= 2 && z >= 2,
                "curvature: canonical core needs m>=2,n>=2,z>=2");
    TORCH_CHECK(u.sizes() == torch::IntArrayRef({m,z,n+1}) &&
                v.sizes() == torch::IntArrayRef({m+1,z,n}) &&
                w.sizes() == torch::IntArrayRef({m,z+1,n}),
                "curvature: invalid native velocity layout");
    TORCH_CHECK(alpha_u.sizes() == u.sizes() && alpha_v.sizes() == v.sizes() &&
                alpha_w.sizes() == w.sizes(),
                "curvature: alpha tensors must be native-staggered");
    TORCH_CHECK(msfux.sizes() == torch::IntArrayRef({m,n+1}) &&
                msfuy.sizes() == msfux.sizes() &&
                msfvx.sizes() == torch::IntArrayRef({m+1,n}) &&
                msfvy.sizes() == msfvx.sizes() &&
                msftx.sizes() == torch::IntArrayRef({m,n}) &&
                msfty.sizes() == msftx.sizes() &&
                (!xlat_radians.defined() || xlat_radians.sizes() == msftx.sizes()),
                "curvature: invalid map or latitude layout");
    TORCH_CHECK(fzm.dim() == 1 && fzp.dim() == 1 &&
                fzm.numel() >= z && fzp.numel() >= z,
                "curvature: fzm/fzp must contain every mass level");
    const auto same = [&](const torch::Tensor& q) {
        return q.device() == u.device() && q.scalar_type() == u.scalar_type();
    };
    TORCH_CHECK(same(v) && same(w) && same(alpha_u) && same(alpha_v) &&
                same(alpha_w) && same(msfux) && same(msfuy) && same(msfvx) &&
                same(msfvy) && same(msftx) && same(msfty) &&
                (!xlat_radians.defined() || same(xlat_radians)) &&
                same(fzm) && same(fzp),
                "curvature: all inputs must share device and scalar type");
}

// xlat_radians is WRF xlat converted to radians by the caller.
inline CurvatureTendencies wrf_curvature_tendencies(
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& alpha_u, const torch::Tensor& alpha_v,
    const torch::Tensor& alpha_w, const torch::Tensor& msfux,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx,
    const torch::Tensor& msfvy, const torch::Tensor& msftx,
    const torch::Tensor& msfty, const torch::Tensor& xlat_radians,
    const torch::Tensor& fzm, const torch::Tensor& fzp,
    double rdx, double rdy, double reradius, int map_proj, bool polar) {
    check_curvature_layout(u,v,w,alpha_u,alpha_v,alpha_w,msfux,msfuy,msfvx,msfvy,
                 msftx,msfty,xlat_radians,fzm,fzp,map_proj,polar);
    const int64_t m=u.size(0), z=u.size(1), n=u.size(2)-1;
    const auto ru=alpha_u*u, rv=alpha_v*v, rw=alpha_w*w;

    // Scalar-point v cross grad m, exactly the Fortran vxgm stencil.
    const auto ua=0.5*(u.slice(2,0,n)+u.slice(2,1,n+1));
    const auto va=0.5*(v.slice(0,0,m)+v.slice(0,1,m+1));
    const auto vxgm=ua*(msfvx.slice(0,1,m+1)-msfvx.slice(0,0,m)).unsqueeze(1)*rdy-
                    va*(msfuy.slice(1,1,n+1)-msfuy.slice(1,0,n)).unsqueeze(1)*rdx;

    // Periodic x-face averages.  The appended column is the physical alias
    // of column zero, so no scalar seam fallback is needed.
    const auto xleft = [](const torch::Tensor& q) {
        const auto last=q.slice(2,q.size(2)-1,q.size(2));
        return torch::cat({last,q},2);
    };
    const auto xright = [](const torch::Tensor& q) {
        return torch::cat({q,q.slice(2,0,1)},2);
    };
    const auto xleft2 = [](const torch::Tensor& q) {
        const auto last=q.slice(1,q.size(1)-1,q.size(1));
        return torch::cat({last,q},1);
    };
    const auto xright2 = [](const torch::Tensor& q) {
        return torch::cat({q,q.slice(1,0,1)},1);
    };
    const bool tan_branch = map_proj == 6 || polar;
    torch::Tensor lat_u, lat_v;
    if (tan_branch) {
        const auto lat_x=0.5*(xleft2(xlat_radians)+xright2(xlat_radians));
        lat_u=lat_x;
        lat_v=0.5*(xlat_radians.slice(0,0,m-1)+xlat_radians.slice(0,1,m));
    }
    const auto rw_x=0.5*(xleft(rw)+xright(rw));
    const auto rw_at_u=0.5*(rw_x.slice(1,0,z)+rw_x.slice(1,1,z+1));
    const auto rv_x=0.5*(xleft(rv)+xright(rv));
    const auto rv_at_u=0.5*(rv_x.slice(0,0,m)+rv_x.slice(0,1,m+1));
    const auto vx_x=0.5*(xleft(vxgm)+xright(vxgm));
    const auto vx_at_u=vx_x;
    const auto ratio_u=msfux/msfuy;
    torch::Tensor cu;
    if (map_proj == 6 || polar) {
        cu=u*reradius*(ratio_u.unsqueeze(1)*rv_at_u*
                       torch::tan(lat_u).unsqueeze(1)-rw_at_u);
    } else {
        cu=vx_at_u*rv_at_u-u*reradius*rw_at_u;
    }

    // Interior V rows only; the two symmetric walls are explicit zero rows.
    const auto ru_x=0.5*(ru.slice(2,0,n)+ru.slice(2,1,n+1));
    const auto ru_at_v=0.5*(ru_x.slice(0,0,m-1)+ru_x.slice(0,1,m));
    const auto rw_y=0.5*(rw.slice(0,0,m-1)+rw.slice(0,1,m));
    const auto rw_at_v=0.5*(rw_y.slice(1,0,z)+rw_y.slice(1,1,z+1));
    const auto vx_at_v=0.5*(vxgm.slice(0,0,m-1)+vxgm.slice(0,1,m));
    const auto v_inner=v.slice(0,1,m);
    const auto ratio_v=(msfvy/msfvx).slice(0,1,m);
    torch::Tensor cv_inner;
    if (map_proj == 6 || polar) {
        const auto u_at_v=0.5*(ua.slice(0,0,m-1)+ua.slice(0,1,m));
        cv_inner=-ratio_v.unsqueeze(1)*reradius*
            (u_at_v*torch::tan(lat_v).unsqueeze(1)*ru_at_v+v_inner*rw_at_v);
    } else {
        cv_inner=-vx_at_v*ru_at_v-
            ratio_v.unsqueeze(1)*v_inner*reradius*rw_at_v;
    }
    const auto vzero=torch::zeros_like(cv_inner.slice(0,0,1));
    const auto cv=torch::cat({vzero,cv_inner,vzero},0);

    // Interior W levels k=1..z-1.  fzm/fzp are required and are never
    // replaced by a scalar fallback.
    const auto ru_m=0.5*(ru.slice(2,0,n)+ru.slice(2,1,n+1));
    const auto u_m=0.5*(u.slice(2,0,n)+u.slice(2,1,n+1));
    const auto rv_m=0.5*(rv.slice(0,0,m)+rv.slice(0,1,m+1));
    const auto v_m=0.5*(v.slice(0,0,m)+v.slice(0,1,m+1));
    const auto fzm_i=fzm.slice(0,1,z).view({1,z-1,1});
    const auto fzp_i=fzp.slice(0,1,z).view({1,z-1,1});
    const auto ru_w=fzm_i*ru_m.slice(1,1,z)+fzp_i*ru_m.slice(1,0,z-1);
    const auto u_w=fzm_i*u_m.slice(1,1,z)+fzp_i*u_m.slice(1,0,z-1);
    const auto rv_w=fzm_i*rv_m.slice(1,1,z)+fzp_i*rv_m.slice(1,0,z-1);
    const auto v_w=fzm_i*v_m.slice(1,1,z)+fzp_i*v_m.slice(1,0,z-1);
    const auto cw_inner=reradius*(ru_w*u_w+(msftx/msfty).unsqueeze(1)*rv_w*v_w);
    const auto cwzero=torch::zeros_like(cw_inner.slice(1,0,1));
    const auto cw=torch::cat({cwzero,cw_inner,cwzero},1);
    return {cu,cv,cw};
}

// Packed whole-domain adapter.  The core still receives only the independent
// m-by-n cells; aliases are restored with tensor concatenation after the
// physical calculation.  This keeps seam and wall semantics explicit rather
// than relying on scalar indexing inside the operator.
inline CurvatureTendencies wrf_curvature_tendencies_packed(
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& alpha_u, const torch::Tensor& alpha_v,
    const torch::Tensor& alpha_w, const torch::Tensor& msfux,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx,
    const torch::Tensor& msfvy, const torch::Tensor& msftx,
    const torch::Tensor& msfty, const torch::Tensor& xlat_radians,
    const torch::Tensor& fzm, const torch::Tensor& fzp,
    int64_t m, int64_t n, double rdx, double rdy, double reradius,
    int map_proj, bool polar) {
    TORCH_CHECK(m >= 2 && n >= 2,
                "curvature packed: physical core needs m>=2,n>=2");
    const int64_t z = u.dim() == 3 ? u.size(1) : -1;
    TORCH_CHECK(u.sizes() == torch::IntArrayRef({m+1,z,n+2}) &&
                v.sizes() == torch::IntArrayRef({m+2,z,n+1}) &&
                w.sizes() == torch::IntArrayRef({m+1,z+1,n+1}) &&
                alpha_u.sizes() == u.sizes() && alpha_v.sizes() == v.sizes() &&
                alpha_w.sizes() == w.sizes(),
                "curvature packed: invalid full native velocity layout");
    TORCH_CHECK(!xlat_radians.defined() || xlat_radians.sizes() == torch::IntArrayRef({m+1,n+1}),
                "curvature packed: invalid latitude layout");
    TORCH_CHECK(msfux.sizes() == torch::IntArrayRef({m+1,n+2}) &&
                msfuy.sizes() == msfux.sizes() &&
                msfvx.sizes() == torch::IntArrayRef({m+2,n+1}) &&
                msfvy.sizes() == msfvx.sizes() &&
                msftx.sizes() == torch::IntArrayRef({m+1,n+1}) &&
                msfty.sizes() == msftx.sizes(),
                "curvature packed: invalid full map layout");
    const auto core = wrf_curvature_tendencies(
        u.slice(0,0,m).slice(2,0,n+1),
        v.slice(0,0,m+1).slice(2,0,n),
        w.slice(0,0,m).slice(2,0,n),
        alpha_u.slice(0,0,m).slice(2,0,n+1),
        alpha_v.slice(0,0,m+1).slice(2,0,n),
        alpha_w.slice(0,0,m).slice(2,0,n),
        msfux.slice(0,0,m).slice(1,0,n+1),
        msfuy.slice(0,0,m).slice(1,0,n+1),
        msfvx.slice(0,0,m+1).slice(1,0,n),
        msfvy.slice(0,0,m+1).slice(1,0,n),
        msftx.slice(0,0,m).slice(1,0,n),
        msfty.slice(0,0,m).slice(1,0,n),
        xlat_radians.defined() ? xlat_radians.slice(0,0,m).slice(1,0,n) : xlat_radians,
        fzm, fzp, rdx, rdy, reradius, map_proj, polar);
    const auto ux=torch::cat({core.u,core.u.slice(2,1,2)},2);
    const auto u_full=torch::cat({ux,ux.slice(0,m-1,m)},0);
    const auto vxv=torch::cat({core.v,core.v.slice(2,0,1)},2);
    const auto v_extra=torch::zeros_like(vxv.slice(0,m-1,m));
    const auto v_full=torch::cat({vxv,v_extra},0);
    const auto wx=torch::cat({core.w,core.w.slice(2,0,1)},2);
    const auto w_full=torch::cat({wx,wx.slice(0,m-1,m)},0);
    return {u_full,v_full,w_full};
}

}  // namespace wrf::sdirk3
