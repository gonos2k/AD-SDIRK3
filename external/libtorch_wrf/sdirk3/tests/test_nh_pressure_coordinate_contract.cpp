// Actual nonhydrostatic pressure-coordinate RHS contract, including the
// native vertical-coordinate coefficients, map layout, scalar oracle, and
// forward-mode AD/central-difference agreement.

#include "tile_test_fixture.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include <array>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;
using wrf::sdirk3::g_sdirk3_config;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                    const float*, const float*);
    friend type access(CoeffTag);
};
template<class Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kRdx = 1.0 / 100000.0;

// Deliberately stretched vertical metric. The last two mass-level rdnw values
// drive the top cfn/cfn1 rule in the candidate; the scalar oracle uses the same
// source values independently.
float rdnw_value(int k) {
    constexpr float rdnw[nz] = {4.0f, 3.5f, 5.25f, 2.75f};
    return rdnw[k];
}
float top_cfn() {
    const float dnw_below = 1.0f / rdnw_value(nz-1);
    const float dn_top = 1.0f / rdnw_value(nz-1);
    return (0.5f * dnw_below + dn_top) / dn_top;
}
float top_cfn1() {
    const float dnw_below = 1.0f / rdnw_value(nz-1);
    const float dn_top = 1.0f / rdnw_value(nz-1);
    return -0.5f * dnw_below / dn_top;
}

// Non-unit geometry/coefficient fixture. The candidate must consume each
// native face map and the c1f/c2f hybrid factor; unit values would hide wiring
// errors through cancellation.
float mass_map_value(int j, int i) {
    return 1.0f + 0.013f * static_cast<float>(j + 1)
                 + 0.007f * static_cast<float>(i + 1);
}
float u_map_value(int j, int i) {
    return 1.0f + 0.009f * static_cast<float>(j + 1)
                 + 0.005f * static_cast<float>(i + 1);
}
float v_map_value(int j, int i) {
    return 1.0f + 0.011f * static_cast<float>(j + 1)
                 + 0.004f * static_cast<float>(i + 1);
}
float mass_perturbation(int j, int i, int m, int n) {
    const double x = 2.0*kPi*(static_cast<double>(i)+0.37)/n;
    const double y = kPi*(static_cast<double>(j)+0.41)/m;
    return static_cast<float>(900.0 + 135.0*std::sin(x)
                              + 57.0*std::cos(2.0*y)
                              + 23.0*std::sin(x-y));
}
float c1f_value(int k) {
    constexpr float c1f[nw] = {0.91f, 1.07f, 0.83f, 1.19f, 0.97f};
    return c1f[k];
}
float c2f_value(int k) {
    constexpr float c2f[nw] = {2.0f, -3.0f, 4.0f, -1.5f, 2.5f};
    return c2f[k];
}

size_t u_index(int j, int k, int i) { return static_cast<size_t>((j*nz+k)*nu+i); }
size_t v_index(int j, int k, int i) { return static_cast<size_t>((j*nz+k)*nx+i); }
size_t ph_index(int j, int k, int i) { return static_cast<size_t>((j*nw+k)*nx+i); }
size_t mu_index(int j, int i) { return static_cast<size_t>(j*nx+i); }

int periodic(int i, int n) {
    int out = i % n;
    return out < 0 ? out + n : out;
}
void configure(TileCase& tile, bool packed) {
    tile.solver.setWRFIndices(
        1, packed ? nx : nx+1, 1, packed ? ny : ny+1, 1, nz,
        1, packed ? nx : nx+1, 1, packed ? ny : ny+1, 1, packed ? nz : nz+1,
        1, packed ? nu : nx+4, 1, packed ? nv : ny+4, 1, nz+1);
    tile.solver.setBoundaryConditions(true, false, false, false, true, true,
                                      false, false, false, false);
    for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i)
        tile.mass_map[j*nx+i] = mass_map_value(j,i);
    for (int j=0; j<ny; ++j) for (int i=0; i<nu; ++i)
        tile.u_map[j*nu+i] = u_map_value(j,i);
    for (int j=0; j<nv; ++j) for (int i=0; i<nx; ++i)
        tile.v_map[j*nx+i] = v_map_value(j,i);
    // Match WRF set_physical_bc2d's packed aliases.  Periodic X aliases are
    // installed before the reflected-Y row aliases, since the latter copy the
    // complete row.  The full layout still carries the periodic east U face.
    const int m = packed ? ny-1 : ny;
    const int n = packed ? nx-1 : nx;
    if (!packed) {
        for (int j=0; j<ny; ++j)
            tile.u_map[j*nu+nx] = tile.u_map[j*nu+0];
    } else {
        for (int j=0; j<ny; ++j) {
            const int source_j = std::min(j, m-1);
            tile.mass_map[j*nx+n] = tile.mass_map[source_j*nx+0];
            tile.u_map[j*nu+n] = tile.u_map[source_j*nu+0];
            tile.u_map[j*nu+n+1] = tile.u_map[source_j*nu+1];
        }
        for (int j=0; j<nv; ++j) {
            const int source_j = std::min(j, m);
            tile.v_map[j*nx+n] = tile.v_map[source_j*nx+0];
        }
        // Row aliases are copied after the X aliases, as in the WRF packed
        // fixture: U/mass are even; the V map is an even ghost map.
        for (int i=0; i<nx; ++i)
            tile.mass_map[m*nx+i] = tile.mass_map[(m-1)*nx+i];
        for (int i=0; i<nu; ++i) {
            tile.u_map[m*nu+i] = tile.u_map[(m-1)*nu+i];
        }
        for (int i=0; i<nx; ++i)
            tile.v_map[(m+1)*nx+i] = tile.v_map[(m-1)*nx+i];
    }
    TORCH_CHECK(m >= 1 && n >= 1, "actual fixture requires nonempty physical domain");
    if (packed) {
        for (int j=0; j<m; ++j) {
            TORCH_CHECK(tile.mass_map[j*nx+n] == tile.mass_map[j*nx+0],
                        "packed mass-map periodic endpoint mismatch");
            TORCH_CHECK(tile.u_map[j*nu+n] == tile.u_map[j*nu+0] &&
                        tile.u_map[j*nu+n+1] == tile.u_map[j*nu+1],
                        "packed U-map periodic endpoints mismatch");
            TORCH_CHECK(tile.v_map[j*nx+n] == tile.v_map[j*nx+0],
                        "packed V-map periodic endpoint mismatch");
        }
        for (int i=0; i<nx; ++i)
            TORCH_CHECK(tile.mass_map[m*nx+i] == tile.mass_map[(m-1)*nx+i],
                        "packed mass-map reflected row mismatch");
        for (int i=0; i<nu; ++i)
            TORCH_CHECK(tile.u_map[m*nu+i] == tile.u_map[(m-1)*nu+i],
                        "packed U-map reflected row mismatch");
        for (int i=0; i<nx; ++i)
            TORCH_CHECK(tile.v_map[(m+1)*nx+i] == tile.v_map[(m-1)*nx+i],
                        "packed V-map reflected row mismatch");
    } else {
        for (int j=0; j<ny; ++j)
            TORCH_CHECK(tile.u_map[j*nu+nx] == tile.u_map[j*nu+0],
                        "full U-map periodic endpoint mismatch");
    }
    for (int k=0; k<nw; ++k)
        tile.metric[k] = -rdnw_value(std::min(k, nz-1));
    std::fill(tile.one.begin(), tile.one.end(), 1.0f);
    std::fill(tile.zero.begin(), tile.zero.end(), 0.0f);
    std::fill(tile.half.begin(), tile.half.end(), 0.5f);
    // Publish the current indices, maps and metric through the normal fixture ABI.
    tile.step(1.0e-4f);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode() == 0, "fixture setup step failed");
    tile.solver.getGridInfo()->msfty = torch::from_blob(
        tile.mass_map.data(), {ny,nx}, torch::kFloat32).clone();
    const float c1f[nw] = {0.91f, 1.07f, 0.83f, 1.19f, 0.97f};
    const float c2f[nw] = {2.0f, -3.0f, 4.0f, -1.5f, 2.5f};
    const float c1h[nz] = {1,1,1,1};
    const float c2h[nz] = {0,0,0,0};
    (tile.solver.*access(CoeffTag{}))(c1f,c2f,c1h,c2h);
}

torch::Tensor make_direction(bool packed) {
    // Components are dyadic-friendly FP32 values; run_nh uses eps=2^-2,
    // so both +/- state perturbations are representable rather than rounded
    // away beneath the NH subtraction signal.
    const int m=ny-int(packed), n=nx-int(packed);
    std::vector<float> d(total,0.0f);
    const int ph0=su+sv+sw, mu0=ph0+sw+st;
    for(int j=0;j<ny;++j)for(int i=0;i<nx;++i){
        const int jj=std::min(j,m-1), ii=periodic(i,n);
        const double x=2*kPi*(ii+.17)/n, y=kPi*(jj+.27)/m;
        d[mu0+mu_index(j,i)]=float(89.6+28.16*std::sin(x-y));
        for(int k=0;k<nw;++k)
            d[ph0+ph_index(j,k,i)]=float((20.48+3.328*k)*std::cos(x)+10.24*std::sin(2*y));
    }
    return torch::from_blob(d.data(),{total},torch::kFloat32).clone();
}

torch::Tensor make_uv_probe(const torch::Tensor& delta, bool packed) {
    const int m=ny-int(packed), n=nx-int(packed);
    torch::NoGradGuard no_grad;
    double best=-1.0;
    int64_t best_q=-1;
    for(int j=0;j<m;++j)for(int k=0;k<nz;++k)for(int i=0;i<n;++i){
        const int64_t q=u_index(j,k,i);
        const double a=std::abs(delta[q].item<double>());
        if(a>best){best=a;best_q=q;}
    }
    for(int j=0;j<m+1;++j)for(int k=0;k<nz;++k)for(int i=0;i<n;++i){
        const int64_t q=su+v_index(j,k,i);
        const double a=std::abs(delta[q].item<double>());
        if(a>best){best=a;best_q=q;}
    }
    TORCH_CHECK(best_q>=0 && best>0.0,"FWAD probe has no nonzero UV signal");
    auto w=torch::zeros({total},delta.options());
    w.index_put_({best_q},1.0f);
    return w;
}

#define FIELD_TAG(Tag, member) \
struct Tag {using type=torch::Tensor TileSDIRK3UnifiedSolver::*;friend type access(Tag);}; \
template struct Accessor<Tag,&TileSDIRK3UnifiedSolver::member>
FIELD_TAG(PTag,p_pert_);
FIELD_TAG(PbTag,ph_base_);
FIELD_TAG(MTag,mu_base_);
FIELD_TAG(PressureBaseTag,p_base_);
FIELD_TAG(ThetaBaseTag,th_base_);
FIELD_TAG(FnmTag,fnm_);
FIELD_TAG(FnpTag,fnp_);
FIELD_TAG(C1hTag,c1h_);
FIELD_TAG(C2hTag,c2h_);
FIELD_TAG(MsfuxTag,msfux_);
FIELD_TAG(MsfuyTag,msfuy_);
FIELD_TAG(MsfvxTag,msfvx_);
FIELD_TAG(MsfvyTag,msfvy_);
#undef FIELD_TAG
// A configured positive call precedes each mutation. Rejection must occur
// before changing either the input or the pressure cache.
template<class Mutate>
void expect_nh_guard(const char* marker, Mutate mutate) {
    g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    g_sdirk3_config.imex_split_mode = 3;
    g_sdirk3_config.mass_coordinate_mode = 1;
    g_sdirk3_config.hevi_split = false;
    TileCase tile;
    configure(tile, true);
    tile.solver.setNonHydrostatic(true);
    const auto state = tile.state();
    const auto input_before = state.clone();
    (tile.solver.*access(RhsTag{}))(state, RhsMode::Full);
    const auto pressure_before = (tile.solver.*access(PTag{})).clone();
    mutate(tile);
    bool rejected = false;
    try {
        (tile.solver.*access(RhsTag{}))(state, RhsMode::Full);
    } catch (const c10::Error& error) {
        rejected = std::string(error.what()).find(marker) != std::string::npos;
    }
    TORCH_CHECK(rejected, "NH admission did not reject with ", marker);
    TORCH_CHECK(torch::equal(state, input_before), "NH rejection changed input");
    TORCH_CHECK(torch::equal(tile.solver.*access(PTag{}), pressure_before),
                "NH rejection changed pressure cache");
}

void run_nh_guards() {
    expect_nh_guard("SDIRK3_NH_GEOMETRY_UNSUPPORTED", [](TileCase&) {
        g_sdirk3_config.mass_coordinate_mode = 0;
    });
    expect_nh_guard("SDIRK3_NH_BASE_STATE_MISSING", [](TileCase& t) {
        t.solver.*access(PressureBaseTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_BASE_STATE_MISSING", [](TileCase& t) {
        t.solver.*access(ThetaBaseTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_BASE_STATE_MISSING", [](TileCase& t) {
        t.solver.*access(PbTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_BASE_STATE_MISSING", [](TileCase& t) {
        t.solver.*access(MTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(FnmTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(FnpTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(C1hTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(C2hTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(MsfuxTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(MsfuyTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(MsfvxTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(MsfvyTag{}) = torch::Tensor();
    });
    expect_nh_guard("SDIRK3_NH_BASE_STATE_MISSING", [](TileCase& t) {
        t.solver.*access(MTag{}) = torch::zeros({ny, nx + 1});
    });
    expect_nh_guard("SDIRK3_NH_INPUT_INVALID", [](TileCase& t) {
        t.solver.*access(FnmTag{}) = torch::zeros({nz - 1});
    });
    std::cout << "NH_NEGATIVE_ADMISSION PASS\n";
}

void run_nh(bool packed, RhsMode mode, bool top_lid) {
    g_sdirk3_config=wrf::sdirk3::SDIRK3Config{};
    g_sdirk3_config.imex_split_mode=3;
    g_sdirk3_config.mass_coordinate_mode=1;
    g_sdirk3_config.hevi_split=false;
    g_sdirk3_config.buoyancy_use_current_w=true;
    g_sdirk3_config.split_explicit_top_lid=top_lid;
    g_sdirk3_config.debug_level=0;
    TileCase tile; configure(tile,packed);
    const int m=ny-int(packed), n=nx-int(packed);
    auto tx=tile.mass_map, ty=tile.mass_map;
    auto ux=tile.u_map, uy=tile.u_map, vx=tile.v_map, vy=tile.v_map;
    for(auto& a:tx)a*=.95f; for(auto& a:ty)a*=1.07f;
    for(auto& a:ux)a*=1.2f; for(auto& a:uy)a*=.8f;
    for(auto& a:vx)a*=.9f; for(auto& a:vy)a*=1.3f;
    tile.solver.unifiedStep(tile.u.data(),tile.v.data(),tile.w.data(),tile.ph.data(),tile.theta.data(),tile.mu.data(),
        tile.ru.data(),tile.rv.data(),tile.rw.data(),tile.rph.data(),tile.rt.data(),tile.rm.data(),
        1.f/tile.spacing,1.f/tile.spacing,tile.metric.data(),tile.metric.data(),tx.data(),ty.data(),
        ux.data(),uy.data(),vx.data(),vy.data(),tile.one.data(),tile.zero.data(),tile.one.data(),tile.zero.data(),
        tile.half.data(),tile.half.data(),1,1.e-4f,nx,ny,nz,nu,nv,nw);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode()==0,"map publication failed");
    float cf[nw]={.91f,1.07f,.83f,1.19f,.97f}, df[nw]={2,-3,4,-1.5f,2.5f};
    float ch[nz]={.61f,.73f,.86f,.99f},dh[nz]={880,1020,1160,1300};
    float fm[nw]={0,.70f,.62f,.54f,0},fp[nw]={1,.30f,.38f,.46f,1};
    const float c0=.65f,c1=.25f,c2=.10f;
    (tile.solver.*access(CoeffTag{}))(cf,df,ch,dh);
    tile.solver.setVerticalInterpolationCoefficients(fm,fp,c0,c1,c2);
    std::fill(tile.u.begin(),tile.u.end(),0);std::fill(tile.v.begin(),tile.v.end(),0);std::fill(tile.w.begin(),tile.w.end(),0);
    for(int j=0;j<ny;++j)for(int i=0;i<nx;++i){
        int jj=std::min(j,m-1),ii=periodic(i,n);double x=2*kPi*(ii+.31)/n,y=kPi*(jj+.5)/m;
        tile.mu[j*nx+i]=float(700+90*std::sin(x)+31*std::cos(y));
        for(int k=0;k<nw;++k)tile.ph[(j*nw+k)*nx+i]=float((30+3*k)*std::sin(x)+17*std::cos(2*y));
        for(int k=0;k<nz;++k)tile.theta[(j*nz+k)*nx+i]=float(.3*(k+1)+.12*std::cos(x)+.09*std::cos(y));
    }
    auto state=tile.state(); tile.solver.setNonHydrostatic(false);
    auto off=(tile.solver.*access(RhsTag{}))(state,mode).detach().clone();
    auto p=(tile.solver.*access(PTag{})).detach().to(torch::kFloat64).contiguous();
    auto pb=(tile.solver.*access(PbTag{})).detach().to(torch::kFloat64).contiguous();
    auto mb=(tile.solver.*access(MTag{})).detach().to(torch::kFloat64).contiguous();
    tile.solver.setNonHydrostatic(true);
    auto on=(tile.solver.*access(RhsTag{}))(state,mode).detach().clone();
    TORCH_CHECK(torch::isfinite(on).all().item<bool>() && torch::isfinite(off).all().item<bool>(),"nonfinite RHS");
    TORCH_CHECK(torch::equal(on.slice(0,su+sv,total),off.slice(0,su+sv,total)),"NH changed non-UV fields");
    TORCH_CHECK(off.slice(0,0,su+sv).abs().max().item<double>()>1e-4,"primary PGF witness missing");
    constexpr double eps=std::numeric_limits<float>::epsilon();
    auto pa=[&](int j,int k,int i){return p.index({j,k,i}).item<double>();};
    auto php=[&](int j,int k,int i){return .5*(pb.index({j,k,i}).item<double>()+pb.index({j,k+1,i}).item<double>()+double(tile.ph[(j*nw+k)*nx+i])+double(tile.ph[(j*nw+k+1)*nx+i]));};
    auto phi_budget=[&](int j,int k,int i){return 4*eps*.5*(std::abs(pb.index({j,k,i}).item<double>())+std::abs(pb.index({j,k+1,i}).item<double>())+std::abs(double(tile.ph[(j*nw+k)*nx+i]))+std::abs(double(tile.ph[(j*nw+k+1)*nx+i])));};
    double worst=0,signal=0,maxerr=0,top_witness=0;int informative=0;
    for(int axis=0;axis<2;++axis)for(int j=0;j<(axis?m+1:m);++j)for(int k=0;k<nz;++k)for(int i=0;i<(axis?n:n+1);++i){
        int jl=axis?std::max(0,j-1):j,jr=axis?std::min(m-1,j):j;
        int il=axis?i:periodic(i-1,n),ir=axis?i:periodic(i,n);
        auto facep=[&](int kk){return .5*(pa(jl,kk,il)+pa(jr,kk,ir));};
        auto dpn=[&](int kk){if(kk==0)return double(c0)*facep(0)+double(c1)*facep(1)+double(c2)*facep(2);if(kk==nz)return top_lid?double(top_cfn())*facep(nz-1)+double(top_cfn1())*facep(nz-2):0.;return double(fm[kk])*facep(kk)+double(fp[kk])*facep(kk-1);};
        double mup=.5*(double(tile.mu[jl*nx+il])+double(tile.mu[jr*nx+ir]));
        double mass=mup+.5*(mb.index({jl,il}).item<double>()+mb.index({jr,ir}).item<double>());
        double diff=php(jr,k,ir)-php(jl,k,il);
        double vertical=-double(rdnw_value(k))*(dpn(k+1)-dpn(k))-double(ch[k])*mup;
        double ma=axis?vy[j*nx+i]:ux[j*nu+i],md=axis?vx[j*nx+i]:uy[j*nu+i];
        double alpha=(double(ch[k])*mass+double(dh[k]))/md;
        double expected=-(ma/md)*kRdx*diff*vertical/alpha;
        int64_t q=axis?su+(j*nz+k)*nx+i:(j*nz+k)*nu+i;
        double actual=on[q].item<double>()-off[q].item<double>();
        double pscale=0;for(int kk=0;kk<nz;++kk)pscale+=std::abs(facep(kk));
        double verr=16*eps*(rdnw_value(k)*pscale+std::abs(ch[k]*mup));
        double derr=phi_budget(jr,k,ir)+phi_budget(jl,k,il);
        double bound=std::abs(ma/md*kRdx/alpha)*(std::abs(vertical)*derr+std::abs(diff)*verr)
                    +32*eps*(std::abs(on[q].item<double>())+std::abs(off[q].item<double>())+std::abs(expected));
        double error=std::abs(actual-expected);worst=std::max(worst,error/std::max(bound,1e-30));maxerr=std::max(maxerr,error);signal=std::max(signal,std::abs(expected));
        if(k==nz-1) top_witness=std::max(top_witness,std::abs(dpn(nz)));
        informative+=std::abs(expected)>100*bound;
        TORCH_CHECK(error<=bound,"NH scalar mismatch axis=",axis," jki=",j,",",k,",",i," got=",actual," expected=",expected," error=",error," bound=",bound);
    }
    TORCH_CHECK(informative>20 && signal>1e-6,"NH oracle lacks resolved nonzero witnesses");
    if(top_lid) TORCH_CHECK(top_witness>1.0e-4,"top-lid cfn/cfn1 witness vanished");
    // Actual RHS forward-mode control on the same configured tile.  The
    // direction is FP32-representable and touches only Phi'/mu'; the dot
    // weights exclude periodic U aliases and symmetric V wall rows.
    auto direction=make_direction(packed).to(torch::kFloat64);
    const auto ad_state=state.to(torch::kFloat64);
    auto eval_delta=[&](const torch::Tensor& x) {
        tile.solver.setNonHydrostatic(false);
        const auto rhs_off=(tile.solver.*access(RhsTag{}))(x,mode);
        tile.solver.setNonHydrostatic(true);
        const auto rhs_on=(tile.solver.*access(RhsTag{}))(x,mode);
        return rhs_on-rhs_off;
    };
    const auto base_delta=on-off;
    auto weights=make_uv_probe(base_delta,packed).to(torch::kFloat64);
    bool used_fd=false; std::string why;
    const auto jvp=wrf::sdirk3::compute_jvp_fwad_or_fd(
        [&](const torch::Tensor& x){return eval_delta(x);},
        ad_state,direction,0,0.0f,&used_fd,&why);
    TORCH_CHECK(!used_fd,"FWAD fallback: ",why);
    const double ad=(jvp*weights).sum().item<double>();
    const double ad_signal=std::abs((base_delta.to(torch::kFloat64)*weights).sum().item<double>());
    TORCH_CHECK(std::isfinite(ad) && std::abs(ad)>1e-10,"missing AD signal");
    double rel_ad_fd=0;
    std::array<double,3> errors{}; int ei=0;
    for(int power: {-2,-4,-6}) {
        const double e=std::ldexp(1.0,power);
        const auto plus=ad_state+e*direction, minus=ad_state-e*direction;
        TORCH_CHECK(torch::any(plus!=ad_state).item<bool>() && torch::any(minus!=ad_state).item<bool>(),"lost FD perturbation");
        const double jp=(eval_delta(plus).detach()*weights).sum().item<double>();
        const double jm=(eval_delta(minus).detach()*weights).sum().item<double>();
        const double fd=(jp-jm)/(2*e);
        rel_ad_fd=std::abs(ad-fd)/std::max(std::abs(ad),std::abs(fd));
        std::cout<<"FP64_AD_SCAN packed="<<packed<<" mode="<<int(mode)<<" lid="<<top_lid<<" power="<<power<<" ad="<<ad<<" fd="<<fd<<" rel="<<rel_ad_fd<<std::endl;
        TORCH_CHECK(std::isfinite(fd),"nonfinite FD");
        errors[ei++]=rel_ad_fd;
    }
    TORCH_CHECK(errors[2]<1e-7 && errors[0]>8*errors[1] && errors[1]>8*errors[2],
                "FP64 AD/FD must agree and central differences converge quadratically");
    TORCH_CHECK(torch::equal(base_delta.slice(0,su+sv,total),
                             torch::zeros({total-su-sv},base_delta.options())),
                "FWAD NH changed non-UV channels");
    std::cout<<"NH_ROOT packed="<<packed<<" mode="<<int(mode)<<" top_lid="<<top_lid<<" max_error="<<maxerr<<" max_budget_ratio="<<worst<<" signal="<<signal<<" top_witness="<<top_witness<<" informative="<<informative<<" ad_signal="<<ad_signal<<" ad_fd_rel="<<rel_ad_fd<<" PASS\n";
}
}
int main(){
    torch::set_num_threads(1);
    run_nh_guards();
    for(bool top_lid:{false,true})
        for(bool packed:{false,true})
            for(auto mode:{RhsMode::Full,RhsMode::ImplicitOnly})
                run_nh(packed,mode,top_lid);
}
