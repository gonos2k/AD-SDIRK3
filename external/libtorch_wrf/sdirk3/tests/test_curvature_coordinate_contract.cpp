#include "tile_test_fixture.h"
#include "curvature_scalar_oracle.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*, const float*, const float*);
    friend type access(CoeffTag);
};
template <typename Tag, typename Tag::type Member>
struct Accessor { friend typename Tag::type access(Tag) { return Member; } };
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag, &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

size_t ui(int j, int k, int i) { return (static_cast<size_t>(j) * nz + k) * nu + i; }
size_t vi(int j, int k, int i) { return (static_cast<size_t>(j) * nz + k) * nx + i; }
size_t wi(int j, int k, int i) { return (static_cast<size_t>(j) * nw + k) * nx + i; }

torch::Tensor t3(const std::vector<float>& a, int d0, int d1, int d2) {
    return torch::from_blob(const_cast<float*>(a.data()), {d0,d1,d2}, torch::kFloat32).clone();
}
torch::Tensor t2(const std::vector<float>& a, int d0, int d1) {
    return torch::from_blob(const_cast<float*>(a.data()), {d0,d1}, torch::kFloat32).clone();
}

torch::Tensor extend_u(const torch::Tensor& q, int m, int n) {
    const auto x = torch::cat({q, q.slice(2, 1, 2)}, 2);
    return torch::cat({x, x.slice(0, m-1, m)}, 0);
}
torch::Tensor extend_v(const torch::Tensor& q, int m, int n) {
    const auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
    return torch::cat({x, torch::zeros_like(x.slice(0, m-1, m))}, 0);
}
torch::Tensor extend_w(const torch::Tensor& q, int m, int n) {
    const auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
    return torch::cat({x, x.slice(0, m-1, m)}, 0);
}
torch::Tensor extend_v_alpha(const torch::Tensor& q, int m, int n) {
    const auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
    return torch::cat({x, x.slice(0, m-1, m)}, 0);
}

void configure(TileCase& tile, bool nonunit_maps, bool variable_maps, bool packed) {
    if (packed)
        tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                                  1, nx, 1, ny, 1, nz,
                                  1, nu, 1, nv, 1, nw);
    if (variable_maps) {
        for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i)
            tile.mass_map[j*nx+i] = 2.5f + .07f*j + .03f*i;
        for (int j=0; j<ny; ++j) for (int i=0; i<nu; ++i)
            tile.u_map[j*nu+i] = 1.2f + .04f*j + .02f*i;
        for (int j=0; j<nv; ++j) for (int i=0; i<nx; ++i)
            tile.v_map[j*nx+i] = 1.6f + .03f*j + .015f*i;
    } else if (nonunit_maps) {
        std::fill(tile.mass_map.begin(), tile.mass_map.end(), 3.0f);
        std::fill(tile.u_map.begin(), tile.u_map.end(), 2.0f);
        std::fill(tile.v_map.begin(), tile.v_map.end(), 4.0f);
    }
    const int m = ny - (packed ? 1 : 0), n = nx - (packed ? 1 : 0);
    for (int j=0;j<ny;++j) {
        tile.u_map[j*nu+n]=tile.u_map[j*nu];
        if (packed) tile.u_map[j*nu+n+1]=tile.u_map[j*nu+1];
    }
    if (packed) {
        for (int j=0;j<ny;++j) tile.mass_map[j*nx+n]=tile.mass_map[j*nx];
        for (int j=0;j<nv;++j) tile.v_map[j*nx+n]=tile.v_map[j*nx];
        for (int i=0;i<nx;++i) {
            tile.mass_map[m*nx+i]=tile.mass_map[(m-1)*nx+i];
            tile.v_map[(m+1)*nx+i]=tile.v_map[(m-1)*nx+i];
        }
        for (int i=0;i<nu;++i) tile.u_map[m*nu+i]=tile.u_map[(m-1)*nu+i];
    }
    tile.step(1.0e-4f);
    std::vector<float> c1h(nw, 2.0f), c2h(nw, 30000.0f),
                       c1f(nw, 1.0f), c2f(nw, 0.0f);
    (tile.solver.*access(CoeffTag{}))(c1f.data(), c2f.data(), c1h.data(), c2h.data());
}

void fill_state(TileCase& tile, bool variable_mass, bool packed) {
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    for (int j=0; j<ny; ++j) {
        for (int k=0; k<nz; ++k) {
            for (int i=0; i<nu; ++i)
                tile.u[ui(j,k,i)] = .4f + .03f*j + .02f*k + .01f*i + .001f*j*i;
            for (int i=0; i<nx; ++i)
                tile.v[vi(j,k,i)] = -.2f + .04f*j + .01f*k + .02f*i + .0007f*j*i;
        }
        for (int k=0; k<nw; ++k)
            for (int i=0; i<nx; ++i)
                tile.w[wi(j,k,i)] = .1f + .02f*j + .01f*k + .015f*i + .0004f*j*i;
        if (variable_mass)
            for (int i=0; i<nx; ++i)
                tile.mu[j*nx+i] = 1200.0f + 175.0f*j - 90.0f*i + 7.0f*j*i;
    }
    const int physical_m=ny-(packed?1:0), physical_n=nx-(packed?1:0);
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k)
        tile.u[ui(j,k,physical_n)]=tile.u[ui(j,k,0)];
    for (int k=0;k<nz;++k) for (int i=0;i<nx;++i) {
        tile.v[vi(0,k,i)]=0.0f; tile.v[vi(physical_m,k,i)]=0.0f;
    }
    for (int j=0;j<ny;++j) for (int i=0;i<nx;++i) tile.w[wi(j,0,i)]=0.0f;
    if (packed) {
        const int m=ny-1, n=nx-1;
        for (int j=0; j<m; ++j) for (int k=0; k<nz; ++k) {
            tile.u[ui(j,k,n)] = tile.u[ui(j,k,0)];
            tile.u[ui(j,k,n+1)] = tile.u[ui(j,k,1)];
            for (int i=0; i<nx; ++i) tile.v[vi(j,k,n)] = tile.v[vi(j,k,0)];
        }
        for (int k=0;k<nz;++k) for (int i=0;i<nu;++i)
            tile.u[ui(m,k,i)]=tile.u[ui(m-1,k,i)];
        for (int k=0; k<nz; ++k) for (int i=0; i<nx; ++i)
            tile.v[vi(m+1,k,i)] = -tile.v[vi(m-1,k,i)];
        for (int k=0; k<nw; ++k) for (int i=0; i<nx; ++i)
            tile.w[wi(m,k,i)] = tile.w[wi(m-1,k,i)];
        for (int j=0; j<m; ++j) for (int k=0; k<nw; ++k)
            tile.w[wi(j,k,n)] = tile.w[wi(j,k,0)];
        if (variable_mass) {
            for (int j=0; j<m; ++j) tile.mu[j*nx+n] = tile.mu[j*nx];
            for (int i=0; i<nx; ++i) tile.mu[m*nx+i] = tile.mu[(m-1)*nx+(i<n?i:0)];
        }
    }
}

void set_case_config(int map_proj) {
    wrf::sdirk3::g_sdirk3_config.do_curvature = false;
    wrf::sdirk3::g_sdirk3_config.map_proj = map_proj;
    wrf::sdirk3::g_sdirk3_config.wrf_omega_ww_cp = true;
    wrf::sdirk3::g_sdirk3_config.mass_coordinate_mode = 1;
    wrf::sdirk3::g_sdirk3_config.hevi_split = false;
    wrf::sdirk3::g_sdirk3_config.imex_split_mode = 3;
    wrf::sdirk3::g_sdirk3_config.diffusion_option = 0;
    wrf::sdirk3::g_sdirk3_config.khdif = 0.0f;
    wrf::sdirk3::g_sdirk3_config.kvdif = 0.0f;
}

torch::Tensor scalar_expected(TileCase& tile, int map_proj, bool packed) {
    const int m = ny - (packed ? 1 : 0), n = nx - (packed ? 1 : 0);
    auto u=t3(tile.u,ny,nz,nu), v=t3(tile.v,nv,nz,nx), w=t3(tile.w,ny,nw,nx);
    auto mass_pert=t2(tile.mu,ny,nx);
    auto g=tile.solver.getGridInfo();
    auto mu_full=mass_pert + g->mu_base.to(torch::kFloat32);
    auto mass=mu_full.slice(0,0,m).slice(1,0,n);
    auto mass_u=wrf::sdirk3::stagger_wrf_mass_field(mass,1,wrf::sdirk3::WWCPBoundaryPolicy::Periodic);
    auto mass_v=wrf::sdirk3::stagger_wrf_mass_field(mass,0,wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate);
    auto c1h=torch::full({nz},2.0f), c2h=torch::full({nz},30000.0f);
    auto c1f=torch::full({nw},1.0f), c2f=torch::zeros({nw});
    auto lm_u=c1h.view({1,nz,1})*mass_u.unsqueeze(1)+c2h.view({1,nz,1});
    auto lm_v=c1h.view({1,nz,1})*mass_v.unsqueeze(1)+c2h.view({1,nz,1});
    auto lm_w=c1f.view({1,nw,1})*mass.unsqueeze(1)+c2f.view({1,nw,1});
    auto msfux=t2(tile.u_map,ny,nu), msfuy=msfux;
    auto msfvx=t2(tile.v_map,nv,nx), msfvy=msfvx;
    auto msftx=t2(tile.mass_map,ny,nx), msfty=msftx;
    auto alpha_u=lm_u/msfuy.slice(0,0,m).slice(1,0,n+1).unsqueeze(1);
    auto alpha_v=lm_v/msfvx.slice(0,0,m+1).slice(1,0,n).unsqueeze(1);
    auto alpha_w=lm_w/msfty.slice(0,0,m).slice(1,0,n).unsqueeze(1);
    if (packed) {
        alpha_u=extend_u(alpha_u,m,n); alpha_v=extend_v_alpha(alpha_v,m,n); alpha_w=extend_w(alpha_w,m,n);
    }
    torch::Tensor lat;
    if (map_proj==6) {
        TORCH_CHECK(g->xlat.defined(), "oracle requires xlat for projection 6");
        lat=g->xlat.to(torch::kFloat32)*(static_cast<float>(M_PI/180.0));
    }
    auto fzm=g->fzm.to(torch::kFloat32), fzp=g->fzp.to(torch::kFloat32);
    auto C=curvature_oracle::curvature_canonical(
        u,v,w,alpha_u*u,alpha_v*v,alpha_w*w,
        msfux,msfuy,msfvx,msfvy,msftx,msfty,lat,fzm,fzp,
        m,n,packed,true,true,map_proj,false,1.0f/tile.spacing,1.0f/tile.spacing,g->reradius);
    auto eu=C.u/alpha_u, ev=C.v/alpha_v, ew=C.w/alpha_w;
    if (packed) {
        // The final RHS boundary projection restores the odd V ghost, even
        // though the raw curvature kernel only fills physical interior rows.
        ev.select(0,m+1).copy_(-ev.select(0,m-1));
    }
    if (!torch::isfinite(eu).all().item<bool>() || !torch::isfinite(ev).all().item<bool>() || !torch::isfinite(ew).all().item<bool>())
        std::cerr << "oracle physical nonfinite packed=" << packed << " counts " << torch::isfinite(eu).logical_not().sum().item<int>() << " " << torch::isfinite(ev).logical_not().sum().item<int>() << " " << torch::isfinite(ew).logical_not().sum().item<int>() << " alpha mins " << alpha_u.min().item<float>() << " " << alpha_v.min().item<float>() << " " << alpha_w.min().item<float>() << "\n";
    auto expected=torch::cat({eu.reshape({-1}), ev.reshape({-1}), ew.reshape({-1}),
                       torch::zeros({sw}),torch::zeros({st}),torch::zeros({sm})});
    if (!torch::isfinite(expected).all().item<bool>()) std::cerr << "expected bad count=" << torch::isfinite(expected).logical_not().sum().item<int>() << " eu=" << torch::isfinite(eu).all().item<bool>() << " ev=" << torch::isfinite(ev).all().item<bool>() << " ew=" << torch::isfinite(ew).all().item<bool>() << "\n";
    return expected;
}

torch::Tensor actual(TileCase& tile, bool curvature) {
    wrf::sdirk3::g_sdirk3_config.do_curvature=curvature;
    return (tile.solver.*access(RhsTag{}))(tile.state(),RhsMode::ExplicitOnly)
        .detach().to(torch::kCPU,torch::kFloat64).contiguous();
}

struct CaseResult { double maxerr=0, sumerr=0, maxdiff=0, sumdiff=0, ad_abs=0; };
CaseResult run_case(int map_proj, bool nonunit_maps, bool variable_maps, bool variable_mass, bool packed) {
    set_case_config(map_proj);
    TileCase tile; configure(tile,nonunit_maps,variable_maps,packed);
    tile.solver.getGridInfo()->map_proj=map_proj;
    if (map_proj==6)
        tile.solver.getGridInfo()->xlat=torch::full({ny,nx},30.0f,torch::kFloat32);
    fill_state(tile,variable_mass,packed);
    auto off=actual(tile,false), on=actual(tile,true), expected=scalar_expected(tile,map_proj,packed);
    TORCH_CHECK(torch::equal(on.slice(0,su+sv+sw), off.slice(0,su+sv+sw)),
                "curvature must leave PH, THM and MU unchanged");
    auto diff=on-off, err=(diff-expected).abs();
    CaseResult r;
    r.maxerr=err.max().item<double>(); r.sumerr=err.sum().item<double>();
    const auto worst=err.argmax().item<int64_t>();
    std::cout << "worst index="<<worst<<" on="<<on[worst].item<double>()
              <<" off="<<off[worst].item<double>()<<" expected="<<expected[worst].item<double>()<<"\n";
    if (!torch::isfinite(err).all().item<bool>()) std::cerr << "err nonfinite count=" << torch::isfinite(err).logical_not().sum().item<int>() << " expectedfinite=" << torch::isfinite(expected).all().item<bool>() << " difffinite=" << torch::isfinite(diff).all().item<bool>() << "\n";
    r.maxdiff=diff.abs().max().item<double>(); r.sumdiff=diff.abs().sum().item<double>();
    TORCH_CHECK(torch::isfinite(err).all().item<bool>(), "curvature error must be finite");
    const auto bound=8.0*std::numeric_limits<float>::epsilon()*
        (on.abs()+off.abs()+expected.abs().to(torch::kFloat64)) + 1.0e-13;
    TORCH_CHECK((err<=bound).all().item<bool>(),
                "actual curvature differs from scalar oracle: max error=",r.maxerr,
                " max excess=",(err-bound).max().item<double>());
    TORCH_CHECK(r.maxdiff>1.0e-8, "curvature fixture must have nonzero response");
    std::cout<<"oracle proj="<<map_proj<<(packed?" packed":" full")
             <<(nonunit_maps?" nonunit":" unit")<<(variable_maps?" variableMaps":"")
             <<(variable_mass?" variableM":"")<<" maxerr="<<std::setprecision(12)<<r.maxerr
             <<" sumerr="<<r.sumerr<<" maxdiff="<<r.maxdiff<<" sumdiff="<<r.sumdiff<<"\n";
    return r;
}

std::pair<double,double> actual_ad_check(int map_proj, bool packed) {
    set_case_config(map_proj);
    TileCase tile; configure(tile,true,true,packed); tile.solver.getGridInfo()->map_proj=map_proj;
    if (map_proj==6) tile.solver.getGridInfo()->xlat=torch::full({ny,nx},30.0f,torch::kFloat32);
    fill_state(tile,true,packed);
    auto state=tile.state().set_requires_grad(true);
    auto direction=torch::linspace(-0.3,0.4,total,torch::TensorOptions().dtype(torch::kFloat32));
    direction.slice(0,su+sv+sw,total).fill_(0.0f);
    auto off=actual(tile,false); // setup-only output; reset with differentiable state below
    (void)off;
    wrf::sdirk3::g_sdirk3_config.do_curvature=false;
    auto out_off=(tile.solver.*access(RhsTag{}))(state,RhsMode::ExplicitOnly);
    wrf::sdirk3::g_sdirk3_config.do_curvature=true;
    auto out_on=(tile.solver.*access(RhsTag{}))(state,RhsMode::ExplicitOnly);
    auto delta=out_on-out_off;
    auto scalar=(delta*direction).sum();
    auto grad=torch::autograd::grad({scalar},{state})[0];
    const double ad=(grad*direction).sum().item<double>();

    auto eval=[&](double eps) {
        TileCase t; configure(t,true,true,packed); t.solver.getGridInfo()->map_proj=map_proj;
        if (map_proj==6) t.solver.getGridInfo()->xlat=torch::full({ny,nx},30.0f,torch::kFloat32);
        fill_state(t,true,packed); t.set((state.detach()+eps*direction).to(torch::kFloat32));
        auto a=actual(t,false); auto b=actual(t,true);
        return ((b-a).to(torch::kFloat64)*direction.to(torch::kFloat64)).sum().item<double>();
    };
    const double eps=2.0e-2;
    const double fd=(eval(eps)-eval(-eps))/(2*eps);
    std::cout<<"actual-ad proj="<<map_proj<<(packed?" packed":" full")
             <<" reverse="<<std::setprecision(12)<<ad<<" central="<<fd
             <<" abs="<<std::abs(ad-fd)<<"\n";
    TORCH_CHECK(std::isfinite(ad) && std::isfinite(fd) && std::abs(ad)>1.0e-8,
                "uninformative curvature derivative check");
    TORCH_CHECK(std::abs(ad-fd) < 2.0e-4*std::max(std::abs(ad),std::abs(fd)),
                "actual curvature directional derivative mismatch: AD=",ad," FD=",fd);
    return {ad,fd};
}
}

int main() {
    torch::set_num_threads(1);
    run_case(1,false,false,false,false);
    run_case(1,true,true,true,false);
    run_case(6,true,true,true,false);
    run_case(1,true,true,true,true);
    run_case(6,true,true,true,true);
    actual_ad_check(1,false);
    actual_ad_check(1,true);
    actual_ad_check(6,false);
    actual_ad_check(6,true);
}
